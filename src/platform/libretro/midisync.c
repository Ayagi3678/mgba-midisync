/* MIDI clock <-> GBA link port bridge for FMS (Lo-Bit Club), libretro build.
 *
 * FMS "GBA to GBA" sync uses NORMAL8 serial transfers. Observed bytes:
 *   0x02 = start, 0x01 = clock tick, 0x03 = stop
 * This driver:
 *   IN  (FMS Sync = In, external clock): reads raw MIDI from an ALSA rawmidi
 *       device and completes the waiting transfer with the mapped byte.
 *       F8 -> 01, FA/FB -> 02, FC -> 03
 *   OUT (FMS Sync = Out, internal clock): translates the byte FMS sends
 *       back into MIDI (01 -> F8, 02 -> FA, 03 -> FC) and writes it out.
 *
 * Config (optional): /userdata/system/configs/mgba-midisync.cfg
 *   device=/dev/snd/midiC1D0   (default: first /dev/snd/midiC*D0 found, skipping card 0)
 *   clock_div=1                (send one 01 per N incoming F8)
 *   out=1                      (0 disables MIDI output)
 *   log=1                      (0 disables the log file)
 *   pace=1                     (keep emulation at exactly real time; RetroArch
 *                               vsync otherwise runs it ~0.5-1% fast, which
 *                               makes FMS drift behind the MIDI clock)
 *   out_delay_ms=20            (MIDI out is sent on a steady real-time
 *                               schedule this long after it was generated,
 *                               instead of in per-frame bursts)
 *   lead_ticks=0               (on start, send N extra ticks at once so FMS
 *                               runs N clocks ahead to cancel audio latency)
 *
 * Log: /userdata/system/logs/mgba-midisync.log (falls back to /tmp)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "midisync.h"

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#define MIDISYNC_ENABLED 1
static void _setOutFd(int fd);
#endif

#define CONFIG_PATH "/userdata/system/configs/mgba-midisync.cfg"
#define LOG_PATH "/userdata/system/logs/mgba-midisync.log"
#define LOG_FALLBACK "/tmp/mgba-midisync.log"

/* 0.5 ms of emulated time between MIDI polls */
#define POLL_CYCLES (GBA_ARM7TDMI_FREQUENCY / 2000)
/* ~1 s between attempts to (re)open a missing device */
#define REOPEN_POLLS 2000
/* small gap between "FMS armed" and "byte arrives", like a real clock edge */
#define DELIVER_CYCLES 256

static void _log(struct GBASIOMidiSync* m, const char* fmt, ...) {
	if (!m->log) {
		return;
	}
	va_list args;
	va_start(args, fmt);
	vfprintf(m->log, fmt, args);
	va_end(args);
	fputc('\n', m->log);
	fflush(m->log);
}

static unsigned _queueDepth(const struct GBASIOMidiSync* m) {
	return (m->tail + MIDISYNC_QUEUE_SIZE - m->head) % MIDISYNC_QUEUE_SIZE;
}

static double _realMs(void) {
#ifdef MIDISYNC_ENABLED
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#else
	return 0;
#endif
}

static double _emuMs(struct GBASIOMidiSync* m) {
	if (!m->d.p || !m->d.p->p) {
		return 0;
	}
	struct GBA* gba = m->d.p->p;
	/* mTimingGlobalTime needs debugger support; accumulate wrap-safe deltas instead */
	uint32_t now = (uint32_t) mTimingCurrentTime(&gba->timing);
	m->emuCycles += (uint32_t) (now - m->emuLastNow);
	m->emuLastNow = now;
	return m->emuCycles * 1000.0 / GBA_ARM7TDMI_FREQUENCY;
}

static bool _queuePush(struct GBASIOMidiSync* m, uint8_t byte) {
	unsigned next = (m->tail + 1) % MIDISYNC_QUEUE_SIZE;
	if (next == m->head) {
		++m->dropped;
		return false;
	}
	m->queue[m->tail] = byte;
	m->tail = next;
	return true;
}

static bool _queueEmpty(const struct GBASIOMidiSync* m) {
	return m->head == m->tail;
}

static uint8_t _queuePop(struct GBASIOMidiSync* m) {
	uint8_t byte = m->queue[m->head];
	m->head = (m->head + 1) % MIDISYNC_QUEUE_SIZE;
	return byte;
}

#ifdef MIDISYNC_ENABLED
static void _loadConfig(struct GBASIOMidiSync* m) {
	FILE* f = fopen(CONFIG_PATH, "r");
	if (!f) {
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char* eq = strchr(line, '=');
		if (!eq || line[0] == '#') {
			continue;
		}
		*eq = '\0';
		char* key = line;
		char* value = eq + 1;
		value[strcspn(value, "\r\n")] = '\0';
		if (!strcmp(key, "device")) {
			strncpy(m->devPath, value, sizeof(m->devPath) - 1);
			m->devPath[sizeof(m->devPath) - 1] = '\0';
		} else if (!strcmp(key, "clock_div")) {
			int div = atoi(value);
			m->clockDiv = div > 0 ? div : 1;
		} else if (!strcmp(key, "out")) {
			m->outEnabled = atoi(value) != 0;
		} else if (!strcmp(key, "in")) {
			m->inEnabled = atoi(value) != 0;
		} else if (!strcmp(key, "out_delay_ms")) {
			m->outDelayMs = atof(value);
		} else if (!strcmp(key, "pace")) {
			m->paceEnabled = atoi(value) != 0;
		} else if (!strcmp(key, "lead_ticks")) {
			int lead = atoi(value);
			m->leadTicks = lead > 0 ? (lead > 24 ? 24 : lead) : 0;
		} else if (!strcmp(key, "log")) {
			m->logEnabled = atoi(value) != 0;
		}
	}
	fclose(f);
}

static bool _findDevice(char* out, size_t size) {
	glob_t g;
	bool found = false;
	if (glob("/dev/snd/midiC*D0", 0, NULL, &g) == 0) {
		size_t i;
		/* card 0 is the built-in codec on the Brick; prefer USB devices */
		for (i = 0; i < g.gl_pathc && !found; ++i) {
			if (strncmp(g.gl_pathv[i], "/dev/snd/midiC0D", 16) != 0) {
				strncpy(out, g.gl_pathv[i], size - 1);
				out[size - 1] = '\0';
				found = true;
			}
		}
		if (!found && g.gl_pathc) {
			strncpy(out, g.gl_pathv[0], size - 1);
			out[size - 1] = '\0';
			found = true;
		}
	}
	globfree(&g);
	return found;
}

static void _openDevice(struct GBASIOMidiSync* m) {
	char path[128];
	if (m->devPath[0]) {
		strncpy(path, m->devPath, sizeof(path));
	} else if (!_findDevice(path, sizeof(path))) {
		return;
	}
	m->fd = open(path, O_RDWR | O_NONBLOCK);
	if (m->fd < 0) {
		m->fd = open(path, O_RDONLY | O_NONBLOCK);
	}
	if (m->fd >= 0) {
		_setOutFd(m->fd);
		_log(m, "opened %s", path);
	}
}

/* ---- Timed MIDI output ------------------------------------------------
 * The emulator computes a whole frame in a few ms and then sleeps, so bytes
 * written straight from the emulation arrive in per-frame clumps (up to
 * ~16 ms of jitter). Instead each byte is stamped with the real time it
 * corresponds to and a small thread sends it at that moment. */
#define OUTQ_SIZE 256
static struct { uint8_t byte; double when; } _outq[OUTQ_SIZE];
static unsigned _outHead, _outTail;
static pthread_mutex_t _outMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _outCond;
static pthread_t _outThread;
static bool _outThreadRunning;
static bool _outStop;
static int _outFd = -1;

static void _msToTimespec(double ms, struct timespec* ts) {
	ts->tv_sec = (time_t) (ms / 1000.0);
	ts->tv_nsec = (long) ((ms - ts->tv_sec * 1000.0) * 1e6);
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec += 1;
		ts->tv_nsec -= 1000000000L;
	} else if (ts->tv_nsec < 0) {
		ts->tv_nsec = 0;
	}
}

static void* _outThreadMain(void* arg) {
	UNUSED(arg);
	pthread_mutex_lock(&_outMutex);
	while (!_outStop) {
		if (_outHead == _outTail) {
			pthread_cond_wait(&_outCond, &_outMutex);
			continue;
		}
		double when = _outq[_outHead].when;
		double now = _realMs();
		if (when > now + 0.1) {
			struct timespec ts;
			_msToTimespec(when, &ts);
			pthread_cond_timedwait(&_outCond, &_outMutex, &ts);
			continue;
		}
		uint8_t byte = _outq[_outHead].byte;
		_outHead = (_outHead + 1) % OUTQ_SIZE;
		if (_outFd >= 0) {
			ssize_t r = write(_outFd, &byte, 1);
			UNUSED(r);
		}
	}
	pthread_mutex_unlock(&_outMutex);
	return NULL;
}

static void _outStart(void) {
	if (_outThreadRunning) {
		return;
	}
	pthread_condattr_t attr;
	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	pthread_cond_init(&_outCond, &attr);
	pthread_condattr_destroy(&attr);
	_outHead = _outTail = 0;
	_outStop = false;
	if (pthread_create(&_outThread, NULL, _outThreadMain, NULL) == 0) {
		_outThreadRunning = true;
	}
}

static void _outShutdown(void) {
	if (!_outThreadRunning) {
		return;
	}
	pthread_mutex_lock(&_outMutex);
	_outStop = true;
	pthread_cond_signal(&_outCond);
	pthread_mutex_unlock(&_outMutex);
	pthread_join(_outThread, NULL);
	pthread_cond_destroy(&_outCond);
	_outThreadRunning = false;
}

static void _setOutFd(int fd) {
	pthread_mutex_lock(&_outMutex);
	_outFd = fd;
	_outHead = _outTail = 0;
	pthread_mutex_unlock(&_outMutex);
}

static void _closeDevice(struct GBASIOMidiSync* m) {
	if (m->fd >= 0) {
		_setOutFd(-1);
		close(m->fd);
		m->fd = -1;
		_log(m, "device closed");
	}
}

static void _writeMidi(struct GBASIOMidiSync* m, uint8_t byte, double when) {
	if (m->fd < 0 || !m->outEnabled) {
		return;
	}
	if (!_outThreadRunning || when <= 0) {
		if (write(m->fd, &byte, 1) < 0 && errno != EAGAIN) {
			_log(m, "write error %d", errno);
		}
		return;
	}
	pthread_mutex_lock(&_outMutex);
	unsigned next = (_outTail + 1) % OUTQ_SIZE;
	if (next != _outHead) {
		_outq[_outTail].byte = byte;
		_outq[_outTail].when = when;
		_outTail = next;
		pthread_cond_signal(&_outCond);
	}
	pthread_mutex_unlock(&_outMutex);
}
#endif

static void _tryDeliver(struct GBASIOMidiSync* m) {
	struct GBA* gba = m->d.p->p;
	if (m->armed && !_queueEmpty(m) && !mTimingIsScheduled(&gba->timing, &m->deliverEvent)) {
		mTimingSchedule(&gba->timing, &m->deliverEvent, DELIVER_CYCLES);
	}
}

static void _handleMidiByte(struct GBASIOMidiSync* m, uint8_t byte) {
	switch (byte) {
	case 0xF8:
		++m->clocksIn;
		if (++m->clockCount >= m->clockDiv) {
			m->clockCount = 0;
			_queuePush(m, 0x01);
		}
		if (m->clocksIn % 96 == 0) {
			double real = _realMs() - m->startReal;
			double emu = _emuMs(m) - m->startEmu;
			_log(m, "in: %u clocks, delivered %u, queued %u, dropped %u, real %.0fms emu %.0fms (%.3fx)",
			     m->clocksIn, m->delivered, _queueDepth(m), m->dropped, real, emu, real > 0 ? emu / real : 0);
		}
		break;
	case 0xFA:
	case 0xFB: {
		/* Clocks received while stopped are stale; start from a clean slate */
		m->head = m->tail = 0;
		m->clockCount = 0;
		m->clocksIn = 0;
		m->delivered = 0;
		m->startReal = _realMs();
		m->startEmu = _emuMs(m);
		_queuePush(m, 0x02);
		int i;
		for (i = 0; i < m->leadTicks; ++i) {
			_queuePush(m, 0x01);
		}
		_log(m, "in: %s (lead %d)", byte == 0xFA ? "start" : "continue", m->leadTicks);
		break;
	}
	case 0xFC:
		_queuePush(m, 0x03);
		_log(m, "in: stop after %u clocks", m->clocksIn);
		break;
	default:
		/* everything else (notes, CC, active sensing...) is ignored */
		break;
	}
}

static void _pollEvent(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	struct GBASIOMidiSync* m = context;
#ifdef MIDISYNC_ENABLED
	if (m->fd < 0) {
		if (++m->reopenCounter >= REOPEN_POLLS) {
			m->reopenCounter = 0;
			_openDevice(m);
		}
	} else if (m->inEnabled) {
		uint8_t buf[64];
		ssize_t n;
		while ((n = read(m->fd, buf, sizeof(buf))) > 0) {
			ssize_t i;
			for (i = 0; i < n; ++i) {
				_handleMidiByte(m, buf[i]);
			}
		}
		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			_log(m, "read error %d", errno);
			_closeDevice(m);
		}
	}
#endif
	_emuMs(m);
	_tryDeliver(m);
	int32_t next = (int32_t) POLL_CYCLES - (int32_t) cyclesLate;
	mTimingSchedule(timing, &m->pollEvent, next > 0 ? next : 1);
}

static void _deliverEvent(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	UNUSED(timing);
	struct GBASIOMidiSync* m = context;
	if (!m->armed || _queueEmpty(m)) {
		return;
	}
	uint8_t byte = _queuePop(m);
	m->armed = false;
	++m->delivered;
	GBASIONormal8FinishTransfer(m->d.p, byte, cyclesLate);
	/* FMS re-arms from its IRQ handler; anything still queued goes next time */
}

static bool _init(struct GBASIODriver* driver) {
	struct GBASIOMidiSync* m = (struct GBASIOMidiSync*) driver;
	m->fd = -1;
	m->clockDiv = 1;
	m->outEnabled = true;
	m->logEnabled = true;
	m->paceEnabled = true;
	m->outDelayMs = 20;
	m->inEnabled = true;
	m->devPath[0] = '\0';
#ifdef MIDISYNC_ENABLED
	_loadConfig(m);
	_outStart();
	if (m->logEnabled) {
		m->log = fopen(LOG_PATH, "w");
		if (!m->log) {
			m->log = fopen(LOG_FALLBACK, "w");
		}
	}
	_log(m, "mgba-midisync: clock_div=%d lead_ticks=%d pace=%d out=%d out_delay_ms=%.1f device=%s", m->clockDiv, m->leadTicks, m->paceEnabled, m->outEnabled, m->outDelayMs, m->devPath[0] ? m->devPath : "(auto)");
	_openDevice(m);
	if (m->fd < 0) {
		_log(m, "no MIDI device yet, will retry");
	}
#endif
	return true;
}

static void _deinit(struct GBASIODriver* driver) {
	struct GBASIOMidiSync* m = (struct GBASIOMidiSync*) driver;
#ifdef MIDISYNC_ENABLED
	_outShutdown();
	_closeDevice(m);
#endif
	if (m->log) {
		fclose(m->log);
		m->log = NULL;
	}
}

static void _reset(struct GBASIODriver* driver) {
	struct GBASIOMidiSync* m = (struct GBASIOMidiSync*) driver;
	m->armed = false;
	m->startPending = false;
	m->head = m->tail = 0;
	if (driver->p && driver->p->p) {
		struct GBA* gba = driver->p->p;
		m->emuLastNow = (uint32_t) mTimingCurrentTime(&gba->timing);
	}
	GBASIOMidiSyncEnsureRunning(m);
}

static bool _handlesMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
	UNUSED(driver);
	return mode == GBA_SIO_NORMAL_8;
}

static int _connectedDevices(struct GBASIODriver* driver) {
	UNUSED(driver);
	return 1;
}

static bool _start(struct GBASIODriver* driver) {
	struct GBASIOMidiSync* m = (struct GBASIOMidiSync*) driver;
	/* The new SIOCNT value (clock source) is only visible in writeSIOCNT,
	 * which runs right after this. Completion is always handled there. */
	m->startPending = true;
	return false;
}

static uint16_t _writeSIOCNT(struct GBASIODriver* driver, uint16_t value) {
	struct GBASIOMidiSync* m = (struct GBASIOMidiSync*) driver;
	struct GBA* gba = driver->p->p;
	if (m->startPending) {
		m->startPending = false;
		if (GBASIONormalIsSc(value)) {
			/* Internal clock: FMS is the leader (Sync = Out) */
			uint8_t sent = gba->memory.io[GBA_REG(SIODATA8)] & 0xFF;
			uint8_t midi = 0;
			switch (sent) {
			case 0x01:
				midi = 0xF8;
				break;
			case 0x02:
				midi = 0xFA;
				break;
			case 0x03:
				midi = 0xFC;
				break;
			default:
				break;
			}
			if (sent != 0x01 || ++m->clocksOut % 96 == 0) {
				_log(m, "out: byte %02X (clocks %u)", sent, m->clocksOut);
			}
			if (sent == 0x02) {
				m->clocksOut = 0;
			}
#ifdef MIDISYNC_ENABLED
			if (midi) {
				double when = 0;
				if (m->paceValid) {
					when = m->frameRealStart + (_emuMs(m) - m->frameEmuStartMs) + m->outDelayMs;
				}
				_writeMidi(m, midi, when);
			}
#endif
			mTimingDeschedule(&gba->timing, &driver->p->completeEvent);
			mTimingSchedule(&gba->timing, &driver->p->completeEvent, GBASIOTransferCycles(GBA_SIO_NORMAL_8, value, 1));
		} else {
			/* External clock: FMS is the follower (Sync = In) */
			m->armed = true;
			_tryDeliver(m);
		}
	} else if (!GBASIONormalIsStart(value)) {
		m->armed = false;
	}
	return value;
}

static uint8_t _finishNormal8(struct GBASIODriver* driver) {
	UNUSED(driver);
	return 0xFF;
}

void GBASIOMidiSyncCreate(struct GBASIOMidiSync* m) {
	memset(m, 0, sizeof(*m));
	m->fd = -1;
	m->d.init = _init;
	m->d.deinit = _deinit;
	m->d.reset = _reset;
	m->d.handlesMode = _handlesMode;
	m->d.connectedDevices = _connectedDevices;
	m->d.start = _start;
	m->d.writeSIOCNT = _writeSIOCNT;
	m->d.finishNormal8 = _finishNormal8;

	m->pollEvent.context = m;
	m->pollEvent.name = "MIDI sync poll";
	m->pollEvent.callback = _pollEvent;
	m->pollEvent.priority = 0x81;

	m->deliverEvent.context = m;
	m->deliverEvent.name = "MIDI sync deliver";
	m->deliverEvent.callback = _deliverEvent;
	m->deliverEvent.priority = 0x82;
}

void GBASIOMidiSyncEnsureRunning(struct GBASIOMidiSync* m) {
	if (!m->d.p || !m->d.p->p) {
		return;
	}
	struct GBA* gba = m->d.p->p;
	if (!mTimingIsScheduled(&gba->timing, &m->pollEvent)) {
		mTimingSchedule(&gba->timing, &m->pollEvent, POLL_CYCLES);
	}
}

void GBASIOMidiSyncInjectMidi(struct GBASIOMidiSync* m, uint8_t byte) {
	_handleMidiByte(m, byte);
}

void GBASIOMidiSyncPaceFrame(struct GBASIOMidiSync* m, double frameSeconds) {
#ifdef MIDISYNC_ENABLED
	if (!m->paceEnabled) {
		return;
	}
	double now = _realMs();
	double frameMs = frameSeconds * 1000.0;
	if (m->paceFrames == 0) {
		m->paceBase = now;
	}
	++m->paceFrames;
	double target = m->paceBase + m->paceFrames * frameMs;
	double ahead = target - now;
	if (ahead > 0.5) {
		struct timespec ts;
		ts.tv_sec = (time_t) (ahead / 1000.0);
		ts.tv_nsec = (long) ((ahead - ts.tv_sec * 1000.0) * 1e6);
		nanosleep(&ts, NULL);
	} else if (ahead < -100.0) {
		/* Fell far behind (menu, loading, hiccup): restart the timeline */
		m->paceBase = now;
		m->paceFrames = 0;
		target = now;
	}
	/* The next frame's emulated time maps onto real time starting here */
	m->frameRealStart = target;
	m->frameEmuStartMs = _emuMs(m);
	m->paceValid = true;
#else
	UNUSED(m);
	UNUSED(frameSeconds);
#endif
}
