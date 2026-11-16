/* The N64's ADPCM sample format, decoded on the CPU (src/audio/vadpcm.c). C and C++.
 *
 * A frame is 9 bytes for 16 samples: a header byte, whose top four bits are a scale (each 4-bit value is multiplied by
 * 2 to that power) and whose low four bits pick one of the codebook's predictors, then the 16 values, two to a byte,
 * high four bits first. Each sample is its value plus a prediction from the samples before it, made with the
 * predictor's coefficients (fixed point, 11 fractional bits). The RSP's audio microcode decodes it so (its ADPCM
 * command); this is the same arithmetic. */
#ifndef TD_VADPCM_H
#define TD_VADPCM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes one frame into out[16]. book holds the codebook (predictors x order x 8 coefficients) and order is its
 * order (1 to 8); state holds the last 16 samples decoded before this frame (the decoder looks at the last `order`),
 * and becomes out. A predictor number past the book's is taken as predictor 0's (the RSP would read past the book). */
void vadpcm_decode_frame(const uint8_t frame[9], const int16_t *book, int32_t order, int32_t predictors,
                         int16_t state[16], int16_t out[16]);

#ifdef __cplusplus
}
#endif

#endif
