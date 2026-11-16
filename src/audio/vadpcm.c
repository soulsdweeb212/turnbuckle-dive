/* The N64's ADPCM decoder (src/audio/vadpcm.h). */
#include "vadpcm.h"

static int16_t clamp16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

void vadpcm_decode_frame(const uint8_t frame[9], const int16_t *book, int32_t order, int32_t predictors,
                         int16_t state[16], int16_t out[16]) {
    int32_t scale = 1 << (frame[0] >> 4);
    int32_t predictor = frame[0] & 0xF;
    const int16_t *coefficients;
    int32_t values[16];
    int half;
    int i;

    if (predictor >= predictors) {
        predictor = 0;
    }
    coefficients = book + predictor * order * 8;
    for (i = 0; i < 16; i++) {
        int32_t nibble = (frame[1 + i / 2] >> (i % 2 == 0 ? 4 : 0)) & 0xF;

        values[i] = (nibble >= 8 ? nibble - 16 : nibble) * scale;
    }
    /* Two halves of 8 samples. Each sample is the value, shifted up by 11, plus each of the `order` samples before the
     * half times its coefficient for this position, plus each earlier value of the same half times the last
     * coefficient row moved along (the predictor run forward over the half), all shifted back down by 11. */
    for (half = 0; half < 2; half++) {
        int16_t before[8];
        const int32_t *in = values + half * 8;
        int k;

        for (k = 0; k < order; k++) {
            before[k] = half == 0 ? state[16 - order + k] : out[8 - order + k];
        }
        for (i = 0; i < 8; i++) {
            int32_t sum = in[i] * 2048; /* the value shifted up by 11 (a negative one too) */
            int j;

            for (k = 0; k < order; k++) {
                sum += coefficients[k * 8 + i] * before[k];
            }
            for (j = 0; j < i; j++) {
                sum += coefficients[(order - 1) * 8 + (i - 1 - j)] * in[j];
            }
            out[half * 8 + i] = clamp16(sum >> 11);
        }
    }
    for (i = 0; i < 16; i++) {
        state[i] = out[i];
    }
}
