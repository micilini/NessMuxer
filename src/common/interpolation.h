#ifndef NESS_INTERPOLATION_H
#define NESS_INTERPOLATION_H

#include <stdint.h>

uint8_t n148_interp_sample_qpel(const uint8_t* plane, int stride,
                                int width, int height,
                                int x_q4, int y_q4,
                                int sample_stride, int sample_offset);

void n148_interp_block_4x4_qpel(uint8_t out[16],
                                const uint8_t* plane, int stride,
                                int width, int height,
                                int bx, int by,
                                int mvx_q4, int mvy_q4,
                                int sample_stride, int sample_offset);

#endif