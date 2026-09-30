/* FMS (Lo-Bit Club) <-> MIDI clock over the emulated GBA link port.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_SYNC_FMS_H
#define LIBRETRO_SYNC_FMS_H

#include <mgba-util/common.h>

#include <mgba/gba/interface.h>

#include "midi-host.h"

struct GBASIOFMSSync {
	struct GBASIODriver d;
	struct MidiHost host;
	struct MidiEmuClock clock;
	struct MidiDeliveryQueue queue;

	/* Sync In */
	bool armed;
	bool startPending;
	int clockCount;
	int startHold;
	int extraPending;
	double delayMs;
	unsigned clocksIn;
	unsigned delivered;
	double startReal;
	double startEmu;

	/* Sync Out */
	unsigned clocksOut;

	struct mTimingEvent pollEvent;
	struct mTimingEvent deliverEvent;
};

void GBASIOFMSSyncCreate(struct GBASIOFMSSync*);
void GBASIOFMSSyncEnsureRunning(struct GBASIOFMSSync*);
bool GBASIOFMSSyncPaceFrame(struct GBASIOFMSSync*, double frameMs);
void GBASIOFMSSyncSetOptions(struct GBASIOFMSSync*, double offsetMs, double outDelayMs, bool pace);

#endif
