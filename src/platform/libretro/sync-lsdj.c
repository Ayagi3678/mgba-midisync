/* LSDj <-> MIDI clock over the emulated Game Boy link port.
 *
 * Same wire protocol as Arduinoboy:
 *   LSDj follows (LSDj SYNC = MIDI): between MIDI start and stop, every MIDI
 *     clock (F8) becomes one 8-bit transfer on LSDj's external clock. The data
 *     is unused (0x00). Start/stop only gate the clocks; LSDj starts on the
 *     first tick once you've pressed START on it.
 *   LSDj leads (LSDj SYNC = LSDJ): LSDj sends one byte per tick on its
 *     internal clock. The first byte is the row it started from. Each byte
 *     becomes F8 (preceded by FA on the first one); when bytes stop coming
 *     for a while, FC is sent.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "sync-lsdj.h"

#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/io.h>
#include <mgba/internal/gb/sio.h>

#include <string.h>

/* stop is assumed after this many ticks without a byte from LSDj ... */
#define OUT_STOP_TICKS 6
/* ... but never sooner than this */
#define OUT_STOP_MIN_MS 150

static double _emuMs(struct GBSIOLSDjSync* m) {
	return MidiEmuClockMs(&m->clock);
}

static int32_t _pollCycles(struct GBSIOLSDjSync* m) {
	/* 0.5 ms */
	return (int32_t) (m->timingFrequency / 2000);
}

static void _tryDeliver(struct GBSIOLSDjSync* m) {
	struct GB* gb = m->d.p->p;
	if (m->armed && MidiDeliveryQueueReady(&m->queue, _emuMs(m)) && !mTimingIsScheduled(&gb->timing, &m->deliverEvent)) {
		mTimingSchedule(&gb->timing, &m->deliverEvent, 16);
	}
}

static void _handleMidiByte(struct GBSIOLSDjSync* m, uint8_t byte) {
	struct MidiHost* h = &m->host;
	double release = _emuMs(m) + m->delayMs;
	switch (byte) {
	case 0xF8:
		MidiHostNoteClock(h);
		if (!m->running) {
			break;
		}
		++m->clocksIn;
		if (++m->clockCount >= h->clockDiv) {
			m->clockCount = 0;
			MidiDeliveryQueuePush(&m->queue, 0x00, release);
			if (m->extraPending > 0) {
				/* catch-up tick halfway to the next clock */
				MidiDeliveryQueuePush(&m->queue, 0x00, release + (h->tickMs > 1 ? h->tickMs / 2 : 5));
				--m->extraPending;
			}
		}
		if (m->clocksIn % 96 == 0) {
			MidiHostLog(h, "in: %u clocks, delivered %u, queued %u, dropped %u, frames skipped %llu", m->clocksIn, m->delivered,
			            MidiDeliveryQueueDepth(&m->queue), m->queue.dropped, (unsigned long long) h->framesSkipped);
		}
		break;
	case 0xFA:
	case 0xFB: {
		int earlyTicks;
		MidiDeliveryQueueClear(&m->queue);
		m->running = true;
		m->clockCount = 0;
		m->clocksIn = 0;
		m->delivered = 0;
		MidiHostStartOffset(h, &earlyTicks, &m->delayMs);
		m->extraPending = earlyTicks + (h->leadTicks > 0 ? h->leadTicks : 0);
		MidiHostLog(h, "in: %s", byte == 0xFA ? "start" : "continue");
		break;
	}
	case 0xFC:
		m->running = false;
		MidiDeliveryQueueClear(&m->queue);
		MidiHostLog(h, "in: stop after %u clocks", m->clocksIn);
		break;
	default:
		break;
	}
}

static void _checkOutStop(struct GBSIOLSDjSync* m) {
	if (!m->outStarted) {
		return;
	}
	double limit = m->outTickMs * OUT_STOP_TICKS;
	if (limit < OUT_STOP_MIN_MS) {
		limit = OUT_STOP_MIN_MS;
	}
	double now = _emuMs(m);
	if (now - m->lastOutEmu > limit) {
		m->outStarted = false;
		/* place the stop one tick after the last clock */
		MidiHostSend(&m->host, 0xFC, m->lastOutEmu + (m->outTickMs > 0 ? m->outTickMs : 20));
		MidiHostLog(&m->host, "out: stop after %u clocks", m->clocksOut);
	}
}

static void _pollEvent(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	struct GBSIOLSDjSync* m = context;
	uint8_t buf[64];
	size_t n = MidiHostPoll(&m->host, buf, sizeof(buf));
	size_t i;
	for (i = 0; i < n; ++i) {
		_handleMidiByte(m, buf[i]);
	}
	_tryDeliver(m);
	_checkOutStop(m);
	int32_t next = _pollCycles(m) - (int32_t) cyclesLate;
	mTimingSchedule(timing, &m->pollEvent, next > 0 ? next : 1);
}

static void _deliverEvent(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	UNUSED(timing);
	UNUSED(cyclesLate);
	struct GBSIOLSDjSync* m = context;
	if (!m->armed || !MidiDeliveryQueueReady(&m->queue, _emuMs(m))) {
		return;
	}
	struct GBSIO* sio = m->d.p;
	uint8_t byte = MidiDeliveryQueuePop(&m->queue);
	m->armed = false;
	++m->delivered;
	/* clock 8 bits in, like an external clock would */
	sio->pendingSB = byte;
	if (GBRegisterSCIsEnable(sio->p->memory.io[GB_REG_SC])) {
		sio->remainingBits = 8;
		mTimingDeschedule(&sio->p->timing, &sio->event);
		mTimingSchedule(&sio->p->timing, &sio->event, 0);
	}
}

static void _scheduleRunning(struct GBSIOLSDjSync* m) {
	struct GB* gb = m->d.p->p;
	if (!mTimingIsScheduled(&gb->timing, &m->pollEvent)) {
		mTimingSchedule(&gb->timing, &m->pollEvent, _pollCycles(m));
	}
}

static bool _init(struct GBSIODriver* driver) {
	struct GBSIOLSDjSync* m = (struct GBSIOLSDjSync*) driver;
	/* GBSIOReset re-initialises the driver; keep one host across resets */
	if (!m->host.name) {
		MidiHostInit(&m->host, "LSDj");
	}
	MidiEmuClockInit(&m->clock, &driver->p->p->timing, m->timingFrequency);
	m->armed = false;
	m->running = false;
	m->outStarted = false;
	MidiDeliveryQueueClear(&m->queue);
	_scheduleRunning(m);
	return true;
}

static void _deinit(struct GBSIODriver* driver) {
	UNUSED(driver);
	/* the host is closed in GBSIOLSDjSyncDestroy, not on every reset */
}

static void _writeSB(struct GBSIODriver* driver, uint8_t value) {
	UNUSED(driver);
	UNUSED(value);
}

static uint8_t _writeSC(struct GBSIODriver* driver, uint8_t value) {
	struct GBSIOLSDjSync* m = (struct GBSIOLSDjSync*) driver;
	struct GB* gb = driver->p->p;
	if (!GBRegisterSCIsEnable(value)) {
		m->armed = false;
		return value;
	}
	if (!GBRegisterSCIsShiftClock(value)) {
		/* external clock: LSDj waits for a tick */
		m->armed = true;
		_tryDeliver(m);
		return value;
	}
	/* internal clock: LSDj leads and sends one byte per tick */
	uint8_t sent = gb->memory.io[GB_REG_SB];
	double now = _emuMs(m);
	if (!m->outStarted) {
		m->outStarted = true;
		m->clocksOut = 0;
		m->outTickMs = 0;
		MidiHostSend(&m->host, 0xFA, now);
		MidiHostLog(&m->host, "out: start at row %02X", sent);
	} else {
		double dt = now - m->lastOutEmu;
		if (dt > 0 && dt < 250) {
			m->outTickMs = m->outTickMs > 0 ? m->outTickMs * 0.9 + dt * 0.1 : dt;
		}
	}
	m->lastOutEmu = now;
	MidiHostSend(&m->host, 0xF8, now);
	if (++m->clocksOut % 96 == 0) {
		MidiHostLog(&m->host, "out: %u clocks, tick %.2fms", m->clocksOut, m->outTickMs);
	}
	return value;
}

void GBSIOLSDjSyncCreate(struct GBSIOLSDjSync* m, double timingFrequency) {
	memset(m, 0, sizeof(*m));
	m->host.fd = -1;
	m->timingFrequency = timingFrequency > 0 ? timingFrequency : CGB_SM83_FREQUENCY;
	m->d.init = _init;
	m->d.deinit = _deinit;
	m->d.writeSB = _writeSB;
	m->d.writeSC = _writeSC;

	m->pollEvent.context = m;
	m->pollEvent.name = "LSDj sync poll";
	m->pollEvent.callback = _pollEvent;
	m->pollEvent.priority = 0x81;

	m->deliverEvent.context = m;
	m->deliverEvent.name = "LSDj sync deliver";
	m->deliverEvent.callback = _deliverEvent;
	m->deliverEvent.priority = 0x82;
}

void GBSIOLSDjSyncDestroy(struct GBSIOLSDjSync* m) {
	if (m->host.name) {
		MidiHostDeinit(&m->host);
		m->host.name = NULL;
	}
}

void GBSIOLSDjSyncEnsureRunning(struct GBSIOLSDjSync* m) {
	if (!m->d.p || !m->d.p->p) {
		return;
	}
	_scheduleRunning(m);
}

bool GBSIOLSDjSyncPaceFrame(struct GBSIOLSDjSync* m, double frameMs) {
	return MidiHostPaceFrame(&m->host, _emuMs(m), frameMs);
}

void GBSIOLSDjSyncSetOptions(struct GBSIOLSDjSync* m, double offsetMs, double outDelayMs, bool pace) {
	MidiHostSetOptions(&m->host, offsetMs, outDelayMs, pace);
}
