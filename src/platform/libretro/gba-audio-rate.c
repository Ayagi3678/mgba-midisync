/* Convert GBA audio to one fixed output rate. See gba-audio-rate.h.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "gba-audio-rate.h"

#include <string.h>

void GBAFixedRateReset(struct GBAFixedRate* r) {
	memset(r, 0, sizeof(*r));
}

size_t GBAFixedRateConvert(struct GBAFixedRate* r, const int16_t* in, size_t frames, unsigned inRate, unsigned outRate, int16_t* out) {
	size_t produced = 0;
	size_t i;
	if (inRate == outRate) {
		memcpy(out, in, frames * 2 * sizeof(int16_t));
		r->accCount = 0;
		return frames;
	}
	if (inRate < outRate) {
		/* 32768 -> 65536 Hz: x2, linear midpoint between neighbouring frames */
		r->accCount = 0;
		for (i = 0; i < frames; ++i) {
			int c;
			for (c = 0; c < 2; ++c) {
				int16_t s = in[i * 2 + c];
				out[produced * 2 + c] = (int16_t) (((int32_t) r->last[c] + s) / 2);
				out[(produced + 1) * 2 + c] = s;
				r->last[c] = s;
			}
			produced += 2;
		}
		return produced;
	}
	/* average each group of 2 / 4 / 8 frames (box filter) */
	unsigned factor = inRate / outRate;
	if (factor < 2) {
		factor = 2;
	}
	for (i = 0; i < frames; ++i) {
		r->acc[0] += in[i * 2];
		r->acc[1] += in[i * 2 + 1];
		if (++r->accCount == factor) {
			out[produced * 2] = (int16_t) (r->acc[0] / (int32_t) factor);
			out[produced * 2 + 1] = (int16_t) (r->acc[1] / (int32_t) factor);
			r->last[0] = out[produced * 2];
			r->last[1] = out[produced * 2 + 1];
			r->acc[0] = r->acc[1] = 0;
			r->accCount = 0;
			++produced;
		}
	}
	return produced;
}
