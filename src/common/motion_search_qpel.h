#ifndef NESS_MOTION_SEARCH_QPEL_H
#define NESS_MOTION_SEARCH_QPEL_H

#include <stdint.h>
#include "motion_search.h"

typedef struct {
    int ref_idx;
    int mvx_q4;
    int mvy_q4;
    int sad;
    int cost;
} N148QpelMotionResult;

int n148_motion_search_refs_qpel_4x4(const uint8_t* cur_plane, int cur_stride,
                                     const uint8_t* const* ref_planes, int ref_count, int ref_stride,
                                     int width, int height,
                                     int bx, int by,
                                     int sample_stride, int sample_offset,
                                     int search_range,
                                     N148QpelMotionResult* out);

int n148_motion_search_enhanced_qpel(const uint8_t* cur_plane, int cur_stride,
                                     const uint8_t* const* ref_planes, int ref_count, int ref_stride,
                                     int width, int height,
                                     int bx, int by,
                                     int block_size,
                                     const N148MEConfig* config,
                                     const N148MVPredictors* preds,
                                     N148QpelMotionResult* out);

int n148_qpel_sad_4x4(const uint8_t* cur, int cur_stride,
                      const uint8_t* ref, int ref_stride,
                      int width, int height,
                      int bx, int by,
                      int mvx_q4, int mvy_q4);

int n148_qpel_sad_8x8(const uint8_t* cur, int cur_stride,
                      const uint8_t* ref, int ref_stride,
                      int width, int height,
                      int bx, int by,
                      int mvx_q4, int mvy_q4);

#endif