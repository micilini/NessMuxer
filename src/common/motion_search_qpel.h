#ifndef NESS_MOTION_SEARCH_QPEL_H
#define NESS_MOTION_SEARCH_QPEL_H

#include <stdint.h>

typedef struct {
    int ref_idx;
    int mvx_q4;
    int mvy_q4;
    int sad;
} N148QpelMotionResult;

int n148_motion_search_refs_qpel_4x4(const uint8_t* cur_plane, int cur_stride,
                                     const uint8_t* const* ref_planes, int ref_count, int ref_stride,
                                     int width, int height,
                                     int bx, int by,
                                     int sample_stride, int sample_offset,
                                     int search_range,
                                     N148QpelMotionResult* out);

#endif