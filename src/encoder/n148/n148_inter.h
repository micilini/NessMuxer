#ifndef NESS_N148_INTER_H
#define NESS_N148_INTER_H

#include <stdint.h>
#include "../../common/motion_search_qpel.h"

typedef struct {
    int mode;
    int ref_idx;
    int mvx_q4;
    int mvy_q4;
    int cost_inter;
    int cost_intra;
} N148InterDecision;

int n148_inter_choose_4x4(const uint8_t* cur_plane,
                          const uint8_t* const* ref_planes, int ref_count,
                          int stride,
                          int width, int height,
                          int bx, int by,
                          int sample_stride, int sample_offset,
                          int search_range,
                          int qp,
                          int intra_sad_hint,
                          N148InterDecision* out);

#endif