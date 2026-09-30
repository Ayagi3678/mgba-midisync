/* Convert GBA audio to one fixed output rate.
 *
 * The GBA only ever outputs 32768 << n Hz (SOUNDBIAS resolution 0-3), so a
 * fixed 65536 Hz output needs exact integer ratios: pass-through, x2
 * upsampling, or /2, /4 decimation. No fractional resampler is involved.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_GBA_AUDIO_RATE_H
#define LIBRETRO_GBA_AUDIO_RATE_H

#include <stddef.h>
#include <stdint.h>

#define GBA_FIXED_AUDIO_RATE 65536

struct GBAFixedRate {
	int16_t last[2]; /* previous input frame, for x2 upsampling */
	int32_t acc[2];  /* partial sums while decimating */
	unsigned accCount;
};

void GBAFixedRateReset(struct GBAFixedRate*);

/* in/out are interleaved stereo; returns output frames written.
 * out must hold at least frames * 2 frames. */
size_t GBAFixedRateConvert(struct GBAFixedRate*, const int16_t* in, size_t frames, unsigned inRate, int16_t* out);

#endif
