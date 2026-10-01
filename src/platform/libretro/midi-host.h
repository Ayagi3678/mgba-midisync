/* Shared MIDI plumbing for link-port sync drivers (libretro build).
 *
 * Everything here is independent of which console or which program is being
 * synced: opening the ALSA rawmidi device, reading bytes, sending bytes on a
 * steady real-time schedule, estimating the incoming clock tempo, keeping
 * emulation in step with real time, and mapping emulated time to real time.
 *
 * The per-program protocol (FMS on GBA, LSDj on Game Boy, ...) lives in its
 * own SIO driver and only decides which bytes go where.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_MIDI_HOST_H
#define LIBRETRO_MIDI_HOST_H

#include <mgba-util/common.h>

#include <mgba/core/timing.h>

#include <stdio.h>

/* ---- Emulated clock ---------------------------------------------------- */

/* mTimingGlobalTime needs debugger support, so accumulate wrap-safe deltas */
struct MidiEmuClock {
	struct mTiming* timing;
	double frequency;
	uint64_t cycles;
	uint32_t last;
};

void MidiEmuClockInit(struct MidiEmuClock*, struct mTiming* timing, double frequency);
/* Call after the core resets its timing (mTimingClear) */
void MidiEmuClockResync(struct MidiEmuClock*);
double MidiEmuClockMs(struct MidiEmuClock*);

/* ---- Bytes waiting to be handed to the emulated console ---------------- */

#define MIDI_DELIVERY_QUEUE_SIZE 256

struct MidiDeliveryQueue {
	uint8_t bytes[MIDI_DELIVERY_QUEUE_SIZE];
	double release[MIDI_DELIVERY_QUEUE_SIZE]; /* emulated ms */
	unsigned head;
	unsigned tail;
	unsigned dropped;
};

void MidiDeliveryQueueClear(struct MidiDeliveryQueue*);
bool MidiDeliveryQueuePush(struct MidiDeliveryQueue*, uint8_t byte, double releaseMs);
bool MidiDeliveryQueueReady(const struct MidiDeliveryQueue*, double nowMs);
uint8_t MidiDeliveryQueuePop(struct MidiDeliveryQueue*);
unsigned MidiDeliveryQueueDepth(const struct MidiDeliveryQueue*);

/* ---- Device, config, log, timing --------------------------------------- */

struct MidiHost {
	const char* name;

	int fd;
	char devPath[128];
	bool inEnabled;
	bool outEnabled;
	bool logEnabled;
	FILE* log;

	/* advanced settings from the .cfg file */
	int clockDiv;
	int leadTicks;
	bool ignoredKeys;
	long configMtime;
	unsigned reloadCounter;
	unsigned reopenCounter;

	/* incoming clock tempo estimate */
	double tickMs;
	double lastClockReal;

	/* core options */
	double offsetMs;
	double outDelayMs;
	bool paceEnabled;

	/* emulated <-> real time */
	bool paceValid;
	double paceBase;
	double paceEmuBase;
	bool paceLog; /* cfg pace_log=1: pacing stats once a second */
	double paceLastCall; /* real ms of the previous MidiHostPaceBudget call */
	uint64_t paceCapped; /* calls limited to PACE_MAX_RUN_FRAMES */
	uint64_t framesSkipped; /* timeline restarts */
};

double MidiHostRealMs(void);

/* name is used in the log ("FMS", "LSDj", ...) */
void MidiHostInit(struct MidiHost*, const char* name);
void MidiHostDeinit(struct MidiHost*);
void MidiHostLog(struct MidiHost*, const char* fmt, ...);

/* Call about every 0.5 ms of emulated time. Reopens a missing device,
 * re-reads the .cfg about once a second, and returns the bytes received. */
size_t MidiHostPoll(struct MidiHost*, uint8_t* buf, size_t size);

/* Feed every incoming MIDI clock (F8) for the tempo estimate */
void MidiHostNoteClock(struct MidiHost*);

/* Turn offsetMs into whole ticks to run early plus a sub-tick delay.
 * A negative offset becomes a pure delay. Call on transport start. */
void MidiHostStartOffset(struct MidiHost*, int* earlyTicks, double* delayMs);

/* Real time (ms) that corresponds to an emulated time, 0 if unknown */
double MidiHostRealForEmu(struct MidiHost*, double emuMs);

/* Send a MIDI byte generated at emulated time emuMs; it goes out on a steady
 * real-time schedule outDelayMs later instead of in per-frame clumps. */
void MidiHostSend(struct MidiHost*, uint8_t byte, double emuMs);

/* Real-time slicing: how much emulated time (ms) to run now so emulated
 * time keeps equal to real time. Call once per retro_run. */
double MidiHostPaceBudget(struct MidiHost*, double emuMs, double frameMs);

void MidiHostSetOptions(struct MidiHost*, double offsetMs, double outDelayMs, bool pace);

#endif
