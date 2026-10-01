/* Shared MIDI plumbing for link-port sync drivers (libretro build).
 *
 * Advanced config (optional): /userdata/system/configs/mgba-midisync.cfg
 *   device=/dev/snd/midiC1D0   (default: first /dev/snd/midiC*D0 found, skipping card 0)
 *   clock_div=1                (hand one tick to the console per N incoming F8)
 *   lead_ticks=0               (-24..24 extra/withheld ticks at start)
 *   in=1 / out=1               (0 disables that direction)
 *   log=1                      (0 disables the log file)
 *   The file is re-read about once a second.
 *
 * Log: /userdata/system/logs/mgba-midisync.log (falls back to /tmp)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "midi-host.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <math.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define MIDI_HOST_ENABLED 1
#endif

#define CONFIG_PATH "/userdata/system/configs/mgba-midisync.cfg"
#define LOG_PATH "/userdata/system/logs/mgba-midisync.log"
#define LOG_FALLBACK "/tmp/mgba-midisync.log"

/* polls happen every 0.5 ms of emulated time; ~1 s between slow checks */
#define SLOW_CHECK_POLLS 2000

/* ---- Emulated clock ---------------------------------------------------- */

void MidiEmuClockInit(struct MidiEmuClock* c, struct mTiming* timing, double frequency) {
	c->timing = timing;
	c->frequency = frequency > 0 ? frequency : 1;
	c->cycles = 0;
	c->last = timing ? (uint32_t) mTimingCurrentTime(timing) : 0;
}

void MidiEmuClockResync(struct MidiEmuClock* c) {
	if (c->timing) {
		c->last = (uint32_t) mTimingCurrentTime(c->timing);
	}
}

double MidiEmuClockMs(struct MidiEmuClock* c) {
	if (!c->timing) {
		return 0;
	}
	uint32_t now = (uint32_t) mTimingCurrentTime(c->timing);
	c->cycles += (uint32_t) (now - c->last);
	c->last = now;
	return c->cycles * 1000.0 / c->frequency;
}

/* ---- Delivery queue ---------------------------------------------------- */

void MidiDeliveryQueueClear(struct MidiDeliveryQueue* q) {
	q->head = q->tail = 0;
}

bool MidiDeliveryQueuePush(struct MidiDeliveryQueue* q, uint8_t byte, double releaseMs) {
	unsigned next = (q->tail + 1) % MIDI_DELIVERY_QUEUE_SIZE;
	if (next == q->head) {
		++q->dropped;
		return false;
	}
	q->bytes[q->tail] = byte;
	q->release[q->tail] = releaseMs;
	q->tail = next;
	return true;
}

bool MidiDeliveryQueueReady(const struct MidiDeliveryQueue* q, double nowMs) {
	return q->head != q->tail && q->release[q->head] <= nowMs;
}

uint8_t MidiDeliveryQueuePop(struct MidiDeliveryQueue* q) {
	uint8_t byte = q->bytes[q->head];
	q->head = (q->head + 1) % MIDI_DELIVERY_QUEUE_SIZE;
	return byte;
}

unsigned MidiDeliveryQueueDepth(const struct MidiDeliveryQueue* q) {
	return (q->tail + MIDI_DELIVERY_QUEUE_SIZE - q->head) % MIDI_DELIVERY_QUEUE_SIZE;
}

/* ---- Host -------------------------------------------------------------- */

double MidiHostRealMs(void) {
#ifdef MIDI_HOST_ENABLED
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#else
	return 0;
#endif
}

void MidiHostLog(struct MidiHost* h, const char* fmt, ...) {
	if (!h->log) {
		return;
	}
	va_list args;
	va_start(args, fmt);
	vfprintf(h->log, fmt, args);
	va_end(args);
	fputc('\n', h->log);
	fflush(h->log);
}

#ifdef MIDI_HOST_ENABLED
/* ---- Timed MIDI output ------------------------------------------------
 * The emulator computes a whole frame in a few ms and then waits, so bytes
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
		if (when > MidiHostRealMs() + 0.1) {
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

static void _loadConfig(struct MidiHost* h) {
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
			strncpy(h->devPath, value, sizeof(h->devPath) - 1);
			h->devPath[sizeof(h->devPath) - 1] = '\0';
		} else if (!strcmp(key, "clock_div")) {
			int div = atoi(value);
			h->clockDiv = div > 0 ? div : 1;
		} else if (!strcmp(key, "out")) {
			h->outEnabled = atoi(value) != 0;
		} else if (!strcmp(key, "in")) {
			h->inEnabled = atoi(value) != 0;
		} else if (!strcmp(key, "offset_ms") || !strcmp(key, "out_delay_ms") || !strcmp(key, "pace")) {
			h->ignoredKeys = true;
		} else if (!strcmp(key, "lead_ticks")) {
			int lead = atoi(value);
			h->leadTicks = lead > 24 ? 24 : (lead < -24 ? -24 : lead);
		} else if (!strcmp(key, "log")) {
			h->logEnabled = atoi(value) != 0;
		} else if (!strcmp(key, "pace_log")) {
			h->paceLog = atoi(value) != 0;
		}
	}
	fclose(f);
}

static bool _findDevice(char* out, size_t size) {
	glob_t g;
	bool found = false;
	if (glob("/dev/snd/midiC*D0", 0, NULL, &g) == 0) {
		size_t i;
		/* card 0 is usually the built-in codec; prefer USB devices */
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

static void _openDevice(struct MidiHost* h) {
	char path[128];
	if (h->devPath[0]) {
		strncpy(path, h->devPath, sizeof(path));
		path[sizeof(path) - 1] = '\0';
	} else if (!_findDevice(path, sizeof(path))) {
		return;
	}
	h->fd = open(path, O_RDWR | O_NONBLOCK);
	if (h->fd < 0) {
		h->fd = open(path, O_RDONLY | O_NONBLOCK);
	}
	if (h->fd >= 0) {
		_setOutFd(h->fd);
		MidiHostLog(h, "opened %s", path);
	}
}

static void _closeDevice(struct MidiHost* h) {
	if (h->fd >= 0) {
		_setOutFd(-1);
		close(h->fd);
		h->fd = -1;
		MidiHostLog(h, "device closed");
	}
}
#endif

void MidiHostInit(struct MidiHost* h, const char* name) {
	memset(h, 0, sizeof(*h));
	h->name = name;
	h->fd = -1;
	h->clockDiv = 1;
	h->inEnabled = true;
	h->outEnabled = true;
	h->logEnabled = true;
	h->paceEnabled = true;
	h->outDelayMs = 55;
#ifdef MIDI_HOST_ENABLED
	_loadConfig(h);
	struct stat st;
	if (stat(CONFIG_PATH, &st) == 0) {
		h->configMtime = st.st_mtime;
	}
	_outStart();
	if (h->logEnabled) {
		h->log = fopen(LOG_PATH, "w");
		if (!h->log) {
			h->log = fopen(LOG_FALLBACK, "w");
		}
	}
	MidiHostLog(h, "mgba-midisync (%s): clock_div=%d lead_ticks=%d in=%d out=%d device=%s", name, h->clockDiv, h->leadTicks,
	            h->inEnabled, h->outEnabled, h->devPath[0] ? h->devPath : "(auto)");
	if (h->ignoredKeys) {
		MidiHostLog(h, "note: offset_ms / out_delay_ms / pace in the .cfg are ignored; set them in Quick Menu > Core Options > MIDI Sync");
	}
	_openDevice(h);
	if (h->fd < 0) {
		MidiHostLog(h, "no MIDI device yet, will retry");
	}
#endif
}

void MidiHostDeinit(struct MidiHost* h) {
#ifdef MIDI_HOST_ENABLED
	_outShutdown();
	_closeDevice(h);
#endif
	if (h->log) {
		fclose(h->log);
		h->log = NULL;
	}
}

size_t MidiHostPoll(struct MidiHost* h, uint8_t* buf, size_t size) {
#ifdef MIDI_HOST_ENABLED
	if (++h->reloadCounter >= SLOW_CHECK_POLLS) {
		h->reloadCounter = 0;
		struct stat st;
		if (stat(CONFIG_PATH, &st) == 0 && st.st_mtime != h->configMtime) {
			h->configMtime = st.st_mtime;
			/* keys removed from the file fall back to their defaults */
			h->leadTicks = 0;
			h->clockDiv = 1;
			h->paceLog = false;
			_loadConfig(h);
			MidiHostLog(h, "config reloaded: lead_ticks=%d clock_div=%d", h->leadTicks, h->clockDiv);
		}
	}
	if (h->fd < 0) {
		if (++h->reopenCounter >= SLOW_CHECK_POLLS) {
			h->reopenCounter = 0;
			_openDevice(h);
		}
		return 0;
	}
	if (!h->inEnabled) {
		return 0;
	}
	size_t total = 0;
	while (total < size) {
		ssize_t n = read(h->fd, buf + total, size - total);
		if (n > 0) {
			total += n;
			continue;
		}
		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			MidiHostLog(h, "read error %d", errno);
			_closeDevice(h);
		}
		break;
	}
	return total;
#else
	UNUSED(h);
	UNUSED(buf);
	UNUSED(size);
	return 0;
#endif
}

void MidiHostNoteClock(struct MidiHost* h) {
	double now = MidiHostRealMs();
	if (h->lastClockReal > 0) {
		double dt = now - h->lastClockReal;
		if (dt < 250) {
			/* reads arrive in per-frame clumps; the average is still right */
			h->tickMs = h->tickMs > 0 ? h->tickMs * 0.97 + dt * 0.03 : dt;
		}
	}
	h->lastClockReal = now;
}

void MidiHostStartOffset(struct MidiHost* h, int* earlyTicks, double* delayMs) {
	*earlyTicks = 0;
	*delayMs = 0;
#ifdef MIDI_HOST_ENABLED
	if (h->offsetMs > 0 && h->tickMs > 1) {
		int ticks = (int) ceil(h->offsetMs / h->tickMs);
		if (ticks > 24) {
			ticks = 24;
		}
		*earlyTicks = ticks;
		*delayMs = ticks * h->tickMs - h->offsetMs;
		if (*delayMs < 0) {
			*delayMs = 0;
		}
	} else if (h->offsetMs < 0) {
		*delayMs = -h->offsetMs;
	}
#endif
	MidiHostLog(h, "in: offset %.1fms -> %d early ticks + %.1fms delay (tick %.2fms)", h->offsetMs, *earlyTicks, *delayMs, h->tickMs);
}

double MidiHostRealForEmu(struct MidiHost* h, double emuMs) {
	if (!h->paceValid) {
		return 0;
	}
	return h->paceBase + (emuMs - h->paceEmuBase);
}

void MidiHostSend(struct MidiHost* h, uint8_t byte, double emuMs) {
#ifdef MIDI_HOST_ENABLED
	if (h->fd < 0 || !h->outEnabled) {
		return;
	}
	double when = MidiHostRealForEmu(h, emuMs);
	if (!_outThreadRunning || when <= 0) {
		if (write(h->fd, &byte, 1) < 0 && errno != EAGAIN) {
			MidiHostLog(h, "write error %d", errno);
		}
		return;
	}
	when += h->outDelayMs;
	pthread_mutex_lock(&_outMutex);
	unsigned next = (_outTail + 1) % OUTQ_SIZE;
	if (next != _outHead) {
		_outq[_outTail].byte = byte;
		_outq[_outTail].when = when;
		_outTail = next;
		pthread_cond_signal(&_outCond);
	}
	pthread_mutex_unlock(&_outMutex);
#else
	UNUSED(h);
	UNUSED(byte);
	UNUSED(emuMs);
#endif
}

/* Emulation runs this far ahead of real time, so RetroArch's audio buffer
 * always has some audio queued. Running exactly on time left it near empty
 * and any scheduling jitter became an underrun (click). */
#define PACE_LEAD_FRAMES 3
/* No call for this long: the content was paused (menu, loading) */
#define PACE_PAUSE_MS 300
/* Further behind than this: give up catching up and restart the timeline */
#define PACE_MAX_BEHIND_MS 500
/* At most this many frames (beyond the lead) per call while catching up */
#define PACE_MAX_RUN_FRAMES 8

double MidiHostPaceBudget(struct MidiHost* h, double emuMs, double frameMs) {
	/* Emulation is run in real-time slices instead of whole frames: each call
	 * returns how much emulated time to run now so that emulated time follows
	 * real time (plus a fixed lead). RetroArch's 60 Hz vsync would otherwise
	 * run the console a little fast, audio would pile up and a MIDI-synced
	 * game would drift. Skipping whole frames fixed the drift but left a
	 * periodic gap (click) in the audio; slicing keeps the audio continuous. */
	double now = MidiHostRealMs();
	double lead = frameMs * PACE_LEAD_FRAMES;
	double sinceLast = h->paceValid ? now - h->paceLastCall : 0;
	h->paceLastCall = now;
	if (!h->paceValid) {
		h->paceBase = now;
		h->paceEmuBase = emuMs;
		h->paceValid = true;
	}
	double budget = (now - h->paceBase + lead) - (emuMs - h->paceEmuBase);
	/* Only a real pause (menu, loading) or a large backlog restarts the
	 * timeline. Being called late now and then is normal on a busy handheld:
	 * dropping that time would cut the audio and shift the tempo, so catch up
	 * instead. */
	if (sinceLast > PACE_PAUSE_MS || budget - lead > PACE_MAX_BEHIND_MS) {
		++h->framesSkipped;
		MidiHostLog(h, "pace: restart, %.0fms behind, %.0fms since last call", budget - lead, sinceLast);
		h->paceBase = now;
		h->paceEmuBase = emuMs;
		budget = lead;
	}
	/* keep single calls short; the rest is run on the next calls */
	double maxRun = lead + frameMs * PACE_MAX_RUN_FRAMES;
	if (budget > maxRun) {
		budget = maxRun;
	}
	return budget > 0 ? budget : 0;
}

void MidiHostSetOptions(struct MidiHost* h, double offsetMs, double outDelayMs, bool pace) {
	bool changed = h->offsetMs != offsetMs || h->outDelayMs != outDelayMs || h->paceEnabled != pace;
	h->offsetMs = offsetMs;
	h->outDelayMs = outDelayMs;
	if (h->paceEnabled != pace) {
		h->paceEnabled = pace;
		h->paceValid = false;
	}
	if (changed) {
		MidiHostLog(h, "options: offset %.0fms, clock out delay %.0fms, pacing %s", offsetMs, outDelayMs, pace ? "on" : "off");
	}
}
