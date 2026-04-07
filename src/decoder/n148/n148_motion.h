#ifndef NESS_N148_MOTION_H
#define NESS_N148_MOTION_H

#include <stdint.h>

int n148_motion_compensate(uint8_t* dst, int dst_stride,
                           const uint8_t* ref, int ref_stride,
                           int width, int height,
                           int mvx, int mvy);

#endif