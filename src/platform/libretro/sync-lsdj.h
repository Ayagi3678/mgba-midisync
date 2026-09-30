/* LSDj <-> MIDI clock over the emulated Game Boy link port.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_SYNC_LSDJ_H
#define LIBRETRO_SYNC_LSDJ_H

#include <mgba-util/common.h>

#include <mgba/gb/interface.h>

#include "midi-host.h"

struct GBSIOLSDjSync {
	struct GBSIODriver d;
	struct MidiHost host;
	struct MidiEmuClock clock;
	struct MidiDeliveryQueue queue;
	double timingFrequency;

	/* LSDj follows (LSDj sync = MIDI) */
	bool running;
	bool armed;
	int clockCount;
	int extraPending;
	double delayMs;
	unsigned clocksIn;
	unsigned delivered;

	/* LSDj leads (LSDj sync = LSDJ, as master) */
	bool outStarted;
	double lastOutEmu;
	double outTickMs;
	unsigned clocksOut;

	struct mTimingEvent pollEvent;
	struct mTimingEvent deliverEvent;
};

/* timingFrequency: core->timingFrequency(core) */
void GBSIOLSDjSyncCreate(struct GBSIOLSDjSync*, double timingFrequency);
void GBSIOLSDjSyncDestroy(struct GBSIOLSDjSync*);
void GBSIOLSDjSyncEnsureRunning(struct GBSIOLSDjSync*);
bool GBSIOLSDjSyncPaceFrame(struct GBSIOLSDjSync*, double frameMs);
void GBSIOLSDjSyncSetOptions(struct GBSIOLSDjSync*, double offsetMs, double outDelayMs, bool pace);

#endif
