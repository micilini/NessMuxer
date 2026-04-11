#ifndef NESS_MOTION_SEARCH_H
#define NESS_MOTION_SEARCH_H

#include <stdint.h>

#define N148_ME_BLOCK_4x4   0
#define N148_ME_BLOCK_8x8   1
#define N148_ME_BLOCK_16x16 2

#define N148_ME_SEARCH_DIAMOND      0
#define N148_ME_SEARCH_HEXAGON      1
#define N148_ME_SEARCH_SQUARE       2
#define N148_ME_SEARCH_FULL         3

#define N148_ME_MAX_REFS 4
#define N148_ME_MAX_MV_CANDIDATES 8

typedef struct {
    int16_t x;
    int16_t y;
} N148MV;

typedef struct {
    int mvx;
    int mvy;
    int sad;
} N148MotionResult;

typedef struct {
    N148MV mv;
    int ref_idx;
    int sad;
    int satd;
    int cost;
} N148MECandidate;

typedef struct {
    N148MV left;
    N148MV top;
    N148MV top_right;
    N148MV top_left;
    N148MV coloc;
    N148MV zero;
    int left_valid;
    int top_valid;
    int top_right_valid;
    int top_left_valid;
    int coloc_valid;
} N148MVPredictors;

typedef struct {
    int search_method;
    int search_range;
    int enable_satd;
    int enable_early_term;
    int early_term_threshold;
    int subpel_refine;
    int max_refs;
    int lambda;
    int qp;
} N148MEConfig;

typedef struct {
    N148MV* mv_field;
    int mb_width;
    int mb_height;
    int stride;
} N148MVField;

int n148_sad_4x4(const uint8_t* cur, int cur_stride,
                 const uint8_t* ref, int ref_stride);

int n148_sad_8x8(const uint8_t* cur, int cur_stride,
                 const uint8_t* ref, int ref_stride);

int n148_sad_16x16(const uint8_t* cur, int cur_stride,
                   const uint8_t* ref, int ref_stride);

int n148_satd_4x4(const uint8_t* cur, int cur_stride,
                  const uint8_t* ref, int ref_stride);

int n148_satd_8x8(const uint8_t* cur, int cur_stride,
                  const uint8_t* ref, int ref_stride);

int n148_mv_cost(int mvx_q4, int mvy_q4, int pred_mvx_q4, int pred_mvy_q4, int lambda);

void n148_get_mv_predictors(const N148MVField* mv_field,
                            int mb_x, int mb_y,
                            int blk_idx,
                            N148MVPredictors* preds);

void n148_median_mv_predictor(const N148MVPredictors* preds, N148MV* out_mvp);

int n148_motion_search_diamond_4x4(const uint8_t* cur_plane, int cur_stride,
                                   const uint8_t* ref_plane, int ref_stride,
                                   int width, int height,
                                   int bx, int by,
                                   int sample_stride, int sample_offset,
                                   int search_range,
                                   N148MotionResult* out);

int n148_motion_search_full(const uint8_t* cur, int cur_stride,
                            const uint8_t* ref, int ref_stride,
                            int width, int height,
                            int bx, int by,
                            int block_size,
                            int search_range,
                            const N148MV* mvp,
                            int lambda,
                            N148MECandidate* out);

int n148_motion_search_diamond(const uint8_t* cur, int cur_stride,
                               const uint8_t* ref, int ref_stride,
                               int width, int height,
                               int bx, int by,
                               int block_size,
                               int search_range,
                               const N148MV* mvp,
                               int lambda,
                               N148MECandidate* out);

int n148_motion_search_hexagon(const uint8_t* cur, int cur_stride,
                               const uint8_t* ref, int ref_stride,
                               int width, int height,
                               int bx, int by,
                               int block_size,
                               int search_range,
                               const N148MV* mvp,
                               int lambda,
                               N148MECandidate* out);

int n148_motion_search_enhanced(const uint8_t* cur, int cur_stride,
                                const uint8_t* ref, int ref_stride,
                                int width, int height,
                                int bx, int by,
                                int block_size,
                                const N148MEConfig* config,
                                const N148MVPredictors* preds,
                                N148MECandidate* out);

int n148_subpel_refine_hpel(const uint8_t* cur, int cur_stride,
                            const uint8_t* ref, int ref_stride,
                            int width, int height,
                            int bx, int by,
                            int block_size,
                            N148MECandidate* cand,
                            const N148MV* mvp,
                            int lambda);

int n148_subpel_refine_qpel(const uint8_t* cur, int cur_stride,
                            const uint8_t* ref, int ref_stride,
                            int width, int height,
                            int bx, int by,
                            int block_size,
                            N148MECandidate* cand,
                            const N148MV* mvp,
                            int lambda);

void n148_me_config_defaults(N148MEConfig* config);

int n148_mv_field_alloc(N148MVField* field, int mb_width, int mb_height);
void n148_mv_field_free(N148MVField* field);
void n148_mv_field_clear(N148MVField* field);
void n148_mv_field_set(N148MVField* field, int mb_x, int mb_y, int blk_idx, N148MV mv);
N148MV n148_mv_field_get(const N148MVField* field, int mb_x, int mb_y, int blk_idx);

#endif