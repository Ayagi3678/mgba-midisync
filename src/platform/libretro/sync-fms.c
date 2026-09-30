/* FMS (Lo-Bit Club) <-> MIDI clock over the emulated GBA link port.
 *
 * FMS "GBA to GBA" sync uses NORMAL8 serial transfers:
 *   0x02 = start, 0x01 = clock tick (24 PPQN), 0x03 = stop
 *
 *   Sync In  (external clock): F8 -> 01, FA/FB -> 02, FC -> 03, handed to
 *            FMS whenever it arms a transfer.
 *   Sync Out (internal clock): 01 -> F8, 02 -> FA, 03 -> FC.
 *
 * FMS counts a burst of ticks as a single tick, so ticks sent early to
 * cancel audio latency are spread out, one per incoming clock.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "sync-fms.h"

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>

#include <string.h>

/* 0.5 ms of emulated time between MIDI polls */
#define POLL_CYCLES (GBA_ARM7TDMI_FREQUENCY / 2000)
/* small gap between "FMS armed" and "byte arrives", like a real clock edge */
#define DELIVER_CYCLES 256

static double _emuMs(struct GBASIOFMSSync* m) {
	return MidiEmuClockMs(&m->clock);
}

static void _push(struct GBASIOFMSSync* m, uint8_t byte, double release) {
	MidiDeliveryQueuePush(&m->queue, byte, release);
}

static void _tryDeliver(struct GBASIOFMSSync* m) {
	struct GBA* gba = m->d.p->p;
	if (m->armed && MidiDeliveryQueueReady(&m->queue, _emuMs(m)) && !mTimingIsScheduled(&gba->timing, &m->deliverEvent)) {
		mTimingSchedule(&gba->timing, &m->deliverEvent, DELIVER_CYCLES);
	}
}

static void _handleMidiByte(struct GBASIOFMSSync* m, uint8_t byte) {
	struct MidiHost* h = &m->host;
	double release = _emuMs(m) + m->delayMs;
	switch (byte) {
	case 0xF8:
		MidiHostNoteClock(h);
		++m->clocksIn;
		if (m->startHold > 0) {
			/* negative lead_ticks: FMS starts only after N clocks have passed */
			if (--m->startHold == 0) {
				_push(m, 0x02, release);
				m->clockCount = 0;
			}
			break;
		}
		if (++m->clockCount >= h->clockDiv) {
			m->clockCount = 0;
			_push(m, 0x01, release);
			if (m->extraPending > 0) {
				/* catch-up tick halfway to the next clock */
				_push(m, 0x01, release + (h->tickMs > 1 ? h->tickMs / 2 : 5));
				--m->extraPending;
			}
		}
		if (m->clocksIn % 96 == 0) {
			double real = MidiHostRealMs() - m->startReal;
			double emu = _emuMs(m) - m->startEmu;
			MidiHostLog(h, "in: %u clocks, delivered %u, queued %u, dropped %u, real %.0fms emu %.0fms (%.3fx), frames skipped %llu",
			            m->clocksIn, m->delivered, MidiDeliveryQueueDepth(&m->queue), m->queue.dropped, real, emu,
			            real > 0 ? emu / real : 0, (unsigned long long) h->framesSkipped);
		}
		break;
	case 0xFA:
	case 0xFB: {
		/* clocks received while stopped are stale; start from a clean slate */
		MidiDeliveryQueueClear(&m->queue);
		m->clockCount = 0;
		m->clocksIn = 0;
		m->delivered = 0;
		m->startReal = MidiHostRealMs();
		m->startEmu = _emuMs(m);
		m->startHold = 0;
		m->extraPending = 0;
		int earlyTicks;
		MidiHostStartOffset(h, &earlyTicks, &m->delayMs);
		release = _emuMs(m) + m->delayMs;
		if (h->leadTicks < 0) {
			m->startHold = -h->leadTicks;
		} else {
			_push(m, 0x02, release);
			m->extraPending = h->leadTicks + earlyTicks;
		}
		MidiHostLog(h, "in: %s (lead %d)", byte == 0xFA ? "start" : "continue", h->leadTicks);
		break;
	}
	case 0xFC:
		if (m->startHold > 0) {
			/* stopped before the delayed start happened: FMS never started */
			m->startHold = 0;
		} else {
			_push(m, 0x03, release);
		}
		MidiHostLog(h, "in: stop after %u clocks", m->clocksIn);
		break;
	default:
		break;
	}
}

static void _pollEvent(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	struct GBASIOFMSSync* m = context;
	uint8_t buf[64];
	size_t n = MidiHostPoll(&m->host, buf, sizeof(buf));
	size_t i;
	for (i = 0; i < n; ++i) {
		_handleMidiByte(m, buf[i]);
	}
	_tryDeliver(m);
	int32_t next = (int32_t) POLL_CYCLES - (int32_t) cyclesLate;
	mTimingSchedule(timing, &m->pollEvent, next > 0 ? next : 1);
}

static void _deliverEvent(struct mTiming* timing, void* context, uint32_t cyclesLate) {
	UNUSED(timing);
	struct GBASIOFMSSync* m = context;
	if (!m->armed || !MidiDeliveryQueueReady(&m->queue, _emuMs(m))) {
		return;
	}
	uint8_t byte = MidiDeliveryQueuePop(&m->queue);
	m->armed = false;
	++m->delivered;
	GBASIONormal8FinishTransfer(m->d.p, byte, cyclesLate);
}

static bool _init(struct GBASIODriver* driver) {
	struct GBASIOFMSSync* m = (struct GBASIOFMSSync*) driver;
	MidiHostInit(&m->host, "FMS");
	MidiEmuClockInit(&m->clock, &driver->p->p->timing, GBA_ARM7TDMI_FREQUENCY);
	return true;
}

static void _deinit(struct GBASIODriver* driver) {
	struct GBASIOFMSSync* m = (struct GBASIOFMSSync*) driver;
	MidiHostDeinit(&m->host);
}

static void _reset(struct GBASIODriver* driver) {
	struct GBASIOFMSSync* m = (struct GBASIOFMSSync*) driver;
	m->armed = false;
	m->startPending = false;
	MidiDeliveryQueueClear(&m->queue);
	MidiEmuClockResync(&m->clock);
	GBASIOFMSSyncEnsureRunning(m);
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
	struct GBASIOFMSSync* m = (struct GBASIOFMSSync*) driver;
	/* The new SIOCNT value (clock source) is only visible in writeSIOCNT,
	 * which runs right after this. Completion is always handled there. */
	m->startPending = true;
	return false;
}

static uint16_t _writeSIOCNT(struct GBASIODriver* driver, uint16_t value) {
	struct GBASIOFMSSync* m = (struct GBASIOFMSSync*) driver;
	struct GBA* gba = driver->p->p;
	if (m->startPending) {
		m->startPending = false;
		if (GBASIONormalIsSc(value)) {
			/* Internal clock: FMS leads (Sync = Out) */
			uint8_t sent = gba->memory.io[GBA_REG(SIODATA8)] & 0xFF;
			uint8_t midi = sent == 0x01 ? 0xF8 : sent == 0x02 ? 0xFA : sent == 0x03 ? 0xFC : 0;
			if (sent != 0x01 || ++m->clocksOut % 96 == 0) {
				MidiHostLog(&m->host, "out: byte %02X (clocks %u)", sent, m->clocksOut);
			}
			if (sent == 0x02) {
				m->clocksOut = 0;
			}
			if (midi) {
				MidiHostSend(&m->host, midi, _emuMs(m));
			}
			mTimingDeschedule(&gba->timing, &driver->p->completeEvent);
			mTimingSchedule(&gba->timing, &driver->p->completeEvent, GBASIOTransferCycles(GBA_SIO_NORMAL_8, value, 1));
		} else {
			/* External clock: FMS follows (Sync = In) */
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

void GBASIOFMSSyncCreate(struct GBASIOFMSSync* m) {
	memset(m, 0, sizeof(*m));
	m->host.fd = -1;
	m->d.init = _init;
	m->d.deinit = _deinit;
	m->d.reset = _reset;
	m->d.handlesMode = _handlesMode;
	m->d.connectedDevices = _connectedDevices;
	m->d.start = _start;
	m->d.writeSIOCNT = _writeSIOCNT;
	m->d.finishNormal8 = _finishNormal8;

	m->pollEvent.context = m;
	m->pollEvent.name = "FMS sync poll";
	m->pollEvent.callback = _pollEvent;
	m->pollEvent.priority = 0x81;

	m->deliverEvent.context = m;
	m->deliverEvent.name = "FMS sync deliver";
	m->deliverEvent.callback = _deliverEvent;
	m->deliverEvent.priority = 0x82;
}

void GBASIOFMSSyncEnsureRunning(struct GBASIOFMSSync* m) {
	if (!m->d.p || !m->d.p->p) {
		return;
	}
	struct GBA* gba = m->d.p->p;
	if (!mTimingIsScheduled(&gba->timing, &m->pollEvent)) {
		mTimingSchedule(&gba->timing, &m->pollEvent, POLL_CYCLES);
	}
}


void GBASIOFMSSyncSetOptions(struct GBASIOFMSSync* m, double offsetMs, double outDelayMs, bool pace) {
	MidiHostSetOptions(&m->host, offsetMs, outDelayMs, pace);
}
