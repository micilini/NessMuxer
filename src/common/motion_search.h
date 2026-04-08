#ifndef NESS_MOTION_SEARCH_H
#define NESS_MOTION_SEARCH_H

#include <stdint.h>

typedef struct {
    int mvx;
    int mvy;
    int sad;
} N148MotionResult;

int n148_sad_4x4(const uint8_t* cur, int cur_stride,
                 const uint8_t* ref, int ref_stride);

int n148_motion_search_diamond_4x4(const uint8_t* cur_plane, int cur_stride,
                                   const uint8_t* ref_plane, int ref_stride,
                                   int width, int height,
                                   int bx, int by,
                                   int sample_stride, int sample_offset,
                                   int search_range,
                                   N148MotionResult* out);

#endif