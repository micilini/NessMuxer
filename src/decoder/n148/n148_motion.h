#ifndef NESS_N148_MOTION_H
#define NESS_N148_MOTION_H

#include <stdint.h>

void n148_mc_copy_qpel_4x4(uint8_t* dst_plane, int dst_stride,
                           const uint8_t* ref_plane, int ref_stride,
                           int width, int height,
                           int dst_bx, int dst_by,
                           int mvx_q4, int mvy_q4,
                           int sample_stride, int sample_offset);

#endif