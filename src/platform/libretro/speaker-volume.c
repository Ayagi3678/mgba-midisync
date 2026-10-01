/* Built-in speaker volume (ALSA "Master"). See speaker-volume.h.
 *
 * Uses the amixer tool rather than libasound, so the core keeps no extra
 * library dependency. Only runs when the option is changed, not per frame.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "speaker-volume.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && !defined(__ANDROID__)
#include <unistd.h>
#define SPEAKER_VOLUME_ENABLED 1
#endif

static int originalRaw = -1; /* Master before our first change */
static int card = -1;
static char applied[16];     /* last value we set, "" if none */
static char status[96];

#ifdef SPEAKER_VOLUME_ENABLED
/* The built-in codec: the first card that isn't USB. Card numbers move when a
 * USB audio device (e.g. the M8) is present at boot. */
static int _findBuiltinCard(void) {
	int i;
	for (i = 0; i < 8; ++i) {
		char path[48];
		snprintf(path, sizeof(path), "/proc/asound/card%d", i);
		if (access(path, F_OK) != 0) {
			continue;
		}
		snprintf(path, sizeof(path), "/proc/asound/card%d/usbid", i);
		if (access(path, F_OK) != 0) {
			return i;
		}
	}
	return -1;
}

/* Raw Master value, e.g. 20 from "  Front Left: 20 [41%] [-30.80dB]" */
static int _readMaster(int c) {
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "amixer -c %d sget Master 2>/dev/null", c);
	FILE* p = popen(cmd, "r");
	if (!p) {
		return -1;
	}
	int value = -1;
	char line[160];
	while (value < 0 && fgets(line, sizeof(line), p)) {
		const char* colon = strstr(line, ": ");
		if (!colon || !strchr(colon, '[')) {
			continue;
		}
		char* end;
		long v = strtol(colon + 2, &end, 10);
		if (end != colon + 2 && *end == ' ') {
			value = (int) v;
		}
	}
	pclose(p);
	return value;
}

static bool _setMaster(int c, const char* value) {
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "amixer -q -c %d sset Master %s >/dev/null 2>&1", c, value);
	return system(cmd) == 0;
}
#endif

static bool _validPercent(const char* value) {
	size_t i, len = strlen(value);
	if (len < 2 || len > 4 || value[len - 1] != '%') {
		return false;
	}
	for (i = 0; i + 1 < len; ++i) {
		if (value[i] < '0' || value[i] > '9') {
			return false;
		}
	}
	return true;
}

void SpeakerVolumeApply(const char* value) {
	status[0] = '\0';
	if (!value || !strcmp(value, "unchanged")) {
		if (applied[0]) {
			SpeakerVolumeRestore();
		}
		return;
	}
	if (!_validPercent(value) || !strcmp(value, applied)) {
		return;
	}
#ifdef SPEAKER_VOLUME_ENABLED
	if (card < 0) {
		card = _findBuiltinCard();
	}
	if (card < 0) {
		snprintf(status, sizeof(status), "speaker volume: no built-in sound card found");
		return;
	}
	if (originalRaw < 0) {
		originalRaw = _readMaster(card);
		if (originalRaw < 0) {
			snprintf(status, sizeof(status), "speaker volume: card %d has no Master control (or amixer is missing)", card);
			return;
		}
	}
	if (_setMaster(card, value)) {
		snprintf(applied, sizeof(applied), "%s", value);
		snprintf(status, sizeof(status), "speaker volume: Master %s on card %d (was %d)", value, card, originalRaw);
	} else {
		snprintf(status, sizeof(status), "speaker volume: setting Master on card %d failed", card);
	}
#endif
}

void SpeakerVolumeRestore(void) {
	status[0] = '\0';
#ifdef SPEAKER_VOLUME_ENABLED
	if (applied[0] && card >= 0 && originalRaw >= 0) {
		char raw[16];
		snprintf(raw, sizeof(raw), "%d", originalRaw);
		_setMaster(card, raw);
		snprintf(status, sizeof(status), "speaker volume: Master back to %d on card %d", originalRaw, card);
	}
#endif
	applied[0] = '\0';
	originalRaw = -1;
}

const char* SpeakerVolumeStatus(void) {
	return status[0] ? status : NULL;
}
