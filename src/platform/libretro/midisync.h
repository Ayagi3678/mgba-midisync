/* MIDI clock <-> GBA link port bridge for FMS (Lo-Bit Club), libretro build.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_MIDISYNC_H
#define LIBRETRO_MIDISYNC_H

#include <mgba-util/common.h>

#include <mgba/core/timing.h>
#include <mgba/gba/interface.h>

#include <stdio.h>

#define MIDISYNC_QUEUE_SIZE 256

struct GBASIOMidiSync {
	struct GBASIODriver d;

	int fd;
	char devPath[128];
	int clockDiv;
	int clockCount;
	int leadTicks;
	bool outEnabled;
	bool inEnabled;
	bool logEnabled;
	bool paceEnabled;
	double paceBase;
	uint64_t paceFrames;
	bool paceValid;
	double frameRealStart;
	double frameEmuStartMs;
	double outDelayMs;
	FILE* log;

	bool armed;
	bool startPending;
	uint8_t queue[MIDISYNC_QUEUE_SIZE];
	unsigned head;
	unsigned tail;

	unsigned clocksIn;
	unsigned clocksOut;
	unsigned dropped;
	unsigned delivered;
	double startReal;
	double startEmu;
	uint64_t emuCycles;
	uint32_t emuLastNow;
	unsigned reopenCounter;

	struct mTimingEvent pollEvent;
	struct mTimingEvent deliverEvent;
};

void GBASIOMidiSyncCreate(struct GBASIOMidiSync* m);
void GBASIOMidiSyncEnsureRunning(struct GBASIOMidiSync* m);
/* Sleep as needed so emulated time tracks real time; call once per frame */
void GBASIOMidiSyncPaceFrame(struct GBASIOMidiSync* m, double frameSeconds);
/* Feed a raw MIDI byte directly (used for testing) */
void GBASIOMidiSyncInjectMidi(struct GBASIOMidiSync* m, uint8_t byte);

#endif
