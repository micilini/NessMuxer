#ifndef NESS_N148_INTRA_H
#define NESS_N148_INTRA_H

#include <stdint.h>

void n148_intra_build_prediction(const uint8_t* ref_plane, int stride,
                                 int width, int height,
                                 int bx, int by,
                                 int sample_stride, int sample_offset,
                                 int mode,
                                 uint8_t pred[16]);

int n148_intra_choose_mode(const uint8_t* src_plane,
                           const uint8_t* ref_plane,
                           int stride,
                           int width, int height,
                           int bx, int by,
                           int sample_stride, int sample_offset,
                           int encode_mode,
                           int qp,
                           uint8_t best_pred[16]);

#endif