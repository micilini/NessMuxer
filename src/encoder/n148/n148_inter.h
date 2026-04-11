#ifndef NESS_N148_INTER_H
#define NESS_N148_INTER_H

#include <stdint.h>
#include "../../common/motion_search.h"
#include "../../common/motion_search_qpel.h"

typedef struct {
    int mode;
    int ref_idx;
    int mvx_q4;
    int mvy_q4;
    int cost_inter;
    int cost_intra;
    int cost_skip;
    int sad;
} N148InterDecision;

typedef struct {
    N148MEConfig me_config;
    N148MVField* mv_field;
    N148MVField* mv_field_prev;
    int mb_x;
    int mb_y;
    int use_enhanced_me;
} N148InterContext;

void n148_inter_ctx_init(N148InterContext* ctx);

void n148_inter_ctx_set_mb_pos(N148InterContext* ctx, int mb_x, int mb_y);

void n148_inter_ctx_set_mv_field(N148InterContext* ctx, N148MVField* field);

void n148_inter_ctx_set_prev_mv_field(N148InterContext* ctx, N148MVField* field);

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

int n148_inter_choose_enhanced(const uint8_t* cur_plane,
                               const uint8_t* const* ref_planes, int ref_count,
                               int stride,
                               int width, int height,
                               int bx, int by,
                               int qp,
                               int intra_sad_hint,
                               N148InterContext* ctx,
                               N148InterDecision* out);

int n148_inter_choose_8x8(const uint8_t* cur_plane,
                          const uint8_t* const* ref_planes, int ref_count,
                          int stride,
                          int width, int height,
                          int bx, int by,
                          int qp,
                          int intra_sad_hint,
                          N148InterContext* ctx,
                          N148InterDecision* out);

#endif