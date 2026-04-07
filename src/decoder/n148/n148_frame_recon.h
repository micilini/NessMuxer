#ifndef NESS_N148_FRAME_RECON_H
#define NESS_N148_FRAME_RECON_H

#include <stdint.h>
#include "../../codec/n148/n148_spec.h"
#include "../../codec/n148/n148_bitstream.h"


int n148_reconstruct_iframe(uint8_t* y_plane, uint8_t* uv_plane,
                            int stride, int width, int height,
                            int qp, N148BsReader* bs);


void n148_intra_pred_4x4(uint8_t* dst, int stride, int mode,
                          const uint8_t* above, const uint8_t* left,
                          int has_above, int has_left);


void n148_idct_4x4(const int16_t* coeff, int16_t* out);


void n148_dequant_4x4(const int16_t* levels, int16_t* coeff, int qp);

#endif