/* Built-in speaker volume (ALSA "Master") for the MIDI sync core.
 *
 * Handhelds like the TrimUI Brick reset the hardware mixer to a low value at
 * boot. This lets the "Speaker Volume" core option set it while the game runs
 * and puts the previous value back when the game is closed.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_SPEAKER_VOLUME_H
#define LIBRETRO_SPEAKER_VOLUME_H

/* value: a core option value, "unchanged" or a percentage like "70%" */
void SpeakerVolumeApply(const char* value);
/* Put back the volume found before the first change, if any */
void SpeakerVolumeRestore(void);
/* Short description of what the last call did, for the log; NULL if nothing */
const char* SpeakerVolumeStatus(void);

#endif
