#include "motion_search.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static const int g_diamond_small[4][2] = {
    {  0, -1 },
    { -1,  0 },
    {  1,  0 },
    {  0,  1 }
};

static const int g_diamond_large[8][2] = {
    {  0, -2 },
    { -1, -1 },
    {  1, -1 },
    { -2,  0 },
    {  2,  0 },
    { -1,  1 },
    {  1,  1 },
    {  0,  2 }
};

static const int g_hexagon[6][2] = {
    { -2,  0 },
    { -1, -2 },
    {  1, -2 },
    {  2,  0 },
    {  1,  2 },
    { -1,  2 }
};

static const int g_square[8][2] = {
    { -1, -1 },
    {  0, -1 },
    {  1, -1 },
    { -1,  0 },
    {  1,  0 },
    { -1,  1 },
    {  0,  1 },
    {  1,  1 }
};

static inline int clip_mv(int v, int min_v, int max_v)
{
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static inline int abs_val(int v)
{
    return (v < 0) ? -v : v;
}

static inline int min_val(int a, int b)
{
    return (a < b) ? a : b;
}

static inline int median3(int a, int b, int c)
{
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c) { b = c; }
    if (a > b) { b = a; }
    return b;
}

int n148_sad_4x4(const uint8_t* cur, int cur_stride,
                 const uint8_t* ref, int ref_stride)
{
    int sad = 0;
    int y, x;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int d = (int)cur[y * cur_stride + x] - (int)ref[y * ref_stride + x];
            sad += abs_val(d);
        }
    }
    return sad;
}

int n148_sad_8x8(const uint8_t* cur, int cur_stride,
                 const uint8_t* ref, int ref_stride)
{
    int sad = 0;
    int y, x;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            int d = (int)cur[y * cur_stride + x] - (int)ref[y * ref_stride + x];
            sad += abs_val(d);
        }
    }
    return sad;
}

int n148_sad_16x16(const uint8_t* cur, int cur_stride,
                   const uint8_t* ref, int ref_stride)
{
    int sad = 0;
    int y, x;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            int d = (int)cur[y * cur_stride + x] - (int)ref[y * ref_stride + x];
            sad += abs_val(d);
        }
    }
    return sad;
}

static void hadamard_4x4(const int16_t in[16], int16_t out[16])
{
    int16_t tmp[16];
    int i;
    for (i = 0; i < 4; i++) {
        int a0 = in[i*4+0] + in[i*4+1];
        int a1 = in[i*4+0] - in[i*4+1];
        int a2 = in[i*4+2] + in[i*4+3];
        int a3 = in[i*4+2] - in[i*4+3];
        tmp[i*4+0] = (int16_t)(a0 + a2);
        tmp[i*4+1] = (int16_t)(a1 + a3);
        tmp[i*4+2] = (int16_t)(a0 - a2);
        tmp[i*4+3] = (int16_t)(a1 - a3);
    }
    for (i = 0; i < 4; i++) {
        int a0 = tmp[0*4+i] + tmp[1*4+i];
        int a1 = tmp[0*4+i] - tmp[1*4+i];
        int a2 = tmp[2*4+i] + tmp[3*4+i];
        int a3 = tmp[2*4+i] - tmp[3*4+i];
        out[0*4+i] = (int16_t)(a0 + a2);
        out[1*4+i] = (int16_t)(a1 + a3);
        out[2*4+i] = (int16_t)(a0 - a2);
        out[3*4+i] = (int16_t)(a1 - a3);
    }
}

int n148_satd_4x4(const uint8_t* cur, int cur_stride,
                  const uint8_t* ref, int ref_stride)
{
    int16_t diff[16];
    int16_t hadamard[16];
    int satd = 0;
    int y, x, i;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            diff[y*4+x] = (int16_t)((int)cur[y*cur_stride+x] - (int)ref[y*ref_stride+x]);
        }
    }
    hadamard_4x4(diff, hadamard);
    for (i = 0; i < 16; i++) {
        satd += abs_val(hadamard[i]);
    }
    return (satd + 1) >> 1;
}

int n148_satd_8x8(const uint8_t* cur, int cur_stride,
                  const uint8_t* ref, int ref_stride)
{
    int satd = 0;
    satd += n148_satd_4x4(cur, cur_stride, ref, ref_stride);
    satd += n148_satd_4x4(cur + 4, cur_stride, ref + 4, ref_stride);
    satd += n148_satd_4x4(cur + 4*cur_stride, cur_stride, ref + 4*ref_stride, ref_stride);
    satd += n148_satd_4x4(cur + 4*cur_stride + 4, cur_stride, ref + 4*ref_stride + 4, ref_stride);
    return satd;
}

int n148_mv_cost(int mvx_q4, int mvy_q4, int pred_mvx_q4, int pred_mvy_q4, int lambda)
{
    int dx = abs_val(mvx_q4 - pred_mvx_q4);
    int dy = abs_val(mvy_q4 - pred_mvy_q4);
    int bits = 0;
    while (dx > 0) { bits++; dx >>= 1; }
    while (dy > 0) { bits++; dy >>= 1; }
    bits = bits * 2 + 2;
    return (lambda * bits + 128) >> 8;
}

void n148_get_mv_predictors(const N148MVField* mv_field,
                            int mb_x, int mb_y,
                            int blk_idx,
                            N148MVPredictors* preds)
{
    memset(preds, 0, sizeof(*preds));
    if (!mv_field || !mv_field->mv_field) return;
    
    if (mb_x > 0) {
        preds->left = n148_mv_field_get(mv_field, mb_x - 1, mb_y, 0);
        preds->left_valid = 1;
    }
    if (mb_y > 0) {
        preds->top = n148_mv_field_get(mv_field, mb_x, mb_y - 1, 0);
        preds->top_valid = 1;
    }
    if (mb_y > 0 && mb_x < mv_field->mb_width - 1) {
        preds->top_right = n148_mv_field_get(mv_field, mb_x + 1, mb_y - 1, 0);
        preds->top_right_valid = 1;
    } else if (mb_y > 0 && mb_x > 0) {
        preds->top_left = n148_mv_field_get(mv_field, mb_x - 1, mb_y - 1, 0);
        preds->top_left_valid = 1;
    }
    (void)blk_idx;
}

void n148_median_mv_predictor(const N148MVPredictors* preds, N148MV* out_mvp)
{
    int count = 0;
    int mvx[3] = {0, 0, 0};
    int mvy[3] = {0, 0, 0};
    
    if (preds->left_valid) {
        mvx[count] = preds->left.x;
        mvy[count] = preds->left.y;
        count++;
    }
    if (preds->top_valid) {
        mvx[count] = preds->top.x;
        mvy[count] = preds->top.y;
        count++;
    }
    if (preds->top_right_valid) {
        mvx[count] = preds->top_right.x;
        mvy[count] = preds->top_right.y;
        count++;
    } else if (preds->top_left_valid) {
        mvx[count] = preds->top_left.x;
        mvy[count] = preds->top_left.y;
        count++;
    }
    
    if (count == 0) {
        out_mvp->x = 0;
        out_mvp->y = 0;
    } else if (count == 1) {
        out_mvp->x = (int16_t)mvx[0];
        out_mvp->y = (int16_t)mvy[0];
    } else if (count == 2) {
        out_mvp->x = (int16_t)((mvx[0] + mvx[1] + 1) >> 1);
        out_mvp->y = (int16_t)((mvy[0] + mvy[1] + 1) >> 1);
    } else {
        out_mvp->x = (int16_t)median3(mvx[0], mvx[1], mvx[2]);
        out_mvp->y = (int16_t)median3(mvy[0], mvy[1], mvy[2]);
    }
}

static int get_block_satd(const uint8_t* cur, int cur_stride,
                          const uint8_t* ref, int ref_stride,
                          int width, int height,
                          int bx, int by, int mvx, int mvy,
                          int block_size)
{
    int rx = bx + mvx;
    int ry = by + mvy;
    int bs = (block_size == N148_ME_BLOCK_4x4) ? 4 :
             (block_size == N148_ME_BLOCK_8x8) ? 8 : 16;

    if (rx < 0 || ry < 0 || rx + bs > width || ry + bs > height)
        return INT_MAX;

    {
        const uint8_t* cur_ptr = cur + by * cur_stride + bx;
        const uint8_t* ref_ptr = ref + ry * ref_stride + rx;

        if (block_size == N148_ME_BLOCK_4x4)
            return n148_satd_4x4(cur_ptr, cur_stride, ref_ptr, ref_stride);
        if (block_size == N148_ME_BLOCK_8x8)
            return n148_satd_8x8(cur_ptr, cur_stride, ref_ptr, ref_stride);

        return n148_satd_8x8(cur_ptr, cur_stride, ref_ptr, ref_stride) +
               n148_satd_8x8(cur_ptr + 8, cur_stride, ref_ptr + 8, ref_stride) +
               n148_satd_8x8(cur_ptr + 8 * cur_stride, cur_stride, ref_ptr + 8 * ref_stride, ref_stride) +
               n148_satd_8x8(cur_ptr + 8 * cur_stride + 8, cur_stride, ref_ptr + 8 * ref_stride + 8, ref_stride);
    }
}

static int get_block_sad(const uint8_t* cur, int cur_stride,
                         const uint8_t* ref, int ref_stride,
                         int width, int height,
                         int bx, int by, int mvx, int mvy,
                         int block_size)
{
    int rx = bx + mvx;
    int ry = by + mvy;
    int bs = (block_size == N148_ME_BLOCK_4x4) ? 4 :
             (block_size == N148_ME_BLOCK_8x8) ? 8 : 16;
    
    if (rx < 0 || ry < 0 || rx + bs > width || ry + bs > height)
        return INT_MAX;
    
    const uint8_t* cur_ptr = cur + by * cur_stride + bx;
    const uint8_t* ref_ptr = ref + ry * ref_stride + rx;
    
    if (block_size == N148_ME_BLOCK_4x4)
        return n148_sad_4x4(cur_ptr, cur_stride, ref_ptr, ref_stride);
    else if (block_size == N148_ME_BLOCK_8x8)
        return n148_sad_8x8(cur_ptr, cur_stride, ref_ptr, ref_stride);
    else
        return n148_sad_16x16(cur_ptr, cur_stride, ref_ptr, ref_stride);
}

int n148_motion_search_diamond_4x4(const uint8_t* cur_plane, int cur_stride,
                                   const uint8_t* ref_plane, int ref_stride,
                                   int width, int height,
                                   int bx, int by,
                                   int sample_stride, int sample_offset,
                                   int search_range,
                                   N148MotionResult* out)
{
    int best_mvx = 0;
    int best_mvy = 0;
    int best_sad;
    int improved = 1;
    int i;
    
    if (!cur_plane || !ref_plane || !out)
        return -1;
    
    best_sad = get_block_sad(cur_plane, cur_stride,
                             ref_plane, ref_stride,
                             width, height,
                             bx, by, 0, 0,
                             N148_ME_BLOCK_4x4);
    
    while (improved) {
        improved = 0;
        for (i = 0; i < 4; i++) {
            int cand_mvx = best_mvx + g_diamond_small[i][0];
            int cand_mvy = best_mvy + g_diamond_small[i][1];
            int cand_sad;
            
            if (cand_mvx < -search_range || cand_mvx > search_range ||
                cand_mvy < -search_range || cand_mvy > search_range)
                continue;
            
            cand_sad = get_block_sad(cur_plane, cur_stride,
                                     ref_plane, ref_stride,
                                     width, height,
                                     bx, by, cand_mvx, cand_mvy,
                                     N148_ME_BLOCK_4x4);
            
            if (cand_sad < best_sad) {
                best_sad = cand_sad;
                best_mvx = cand_mvx;
                best_mvy = cand_mvy;
                improved = 1;
            }
        }
    }
    
    out->mvx = best_mvx;
    out->mvy = best_mvy;
    out->sad = best_sad;
    (void)sample_stride;
    (void)sample_offset;
    return 0;
}

int n148_motion_search_full(const uint8_t* cur, int cur_stride,
                            const uint8_t* ref, int ref_stride,
                            int width, int height,
                            int bx, int by,
                            int block_size,
                            int search_range,
                            const N148MV* mvp,
                            int lambda,
                            N148MECandidate* out)
{
    int best_mvx = 0;
    int best_mvy = 0;
    int best_cost = INT_MAX;
    int best_sad = INT_MAX;
    int mvx, mvy;
    int pred_mvx = mvp ? mvp->x * 4 : 0;
    int pred_mvy = mvp ? mvp->y * 4 : 0;
    
    for (mvy = -search_range; mvy <= search_range; mvy++) {
        for (mvx = -search_range; mvx <= search_range; mvx++) {
            int sad = get_block_sad(cur, cur_stride, ref, ref_stride,
                                    width, height, bx, by, mvx, mvy, block_size);
            if (sad < INT_MAX) {
                int mv_cost = n148_mv_cost(mvx * 4, mvy * 4, pred_mvx, pred_mvy, lambda);
                int cost = sad + mv_cost;
                if (cost < best_cost) {
                    best_cost = cost;
                    best_sad = sad;
                    best_mvx = mvx;
                    best_mvy = mvy;
                }
            }
        }
    }
    
    out->mv.x = (int16_t)best_mvx;
    out->mv.y = (int16_t)best_mvy;
    out->sad = best_sad;
    out->cost = best_cost;
    out->ref_idx = 0;
    out->satd = 0;
    return 0;
}

int n148_motion_search_diamond(const uint8_t* cur, int cur_stride,
                               const uint8_t* ref, int ref_stride,
                               int width, int height,
                               int bx, int by,
                               int block_size,
                               int search_range,
                               const N148MV* mvp,
                               int lambda,
                               N148MECandidate* out)
{
    int best_mvx = mvp ? clip_mv(mvp->x, -search_range, search_range) : 0;
    int best_mvy = mvp ? clip_mv(mvp->y, -search_range, search_range) : 0;
    int best_sad, best_cost;
    int pred_mvx = mvp ? mvp->x * 4 : 0;
    int pred_mvy = mvp ? mvp->y * 4 : 0;
    int improved;
    int step, i;
    
    best_sad = get_block_sad(cur, cur_stride, ref, ref_stride,
                             width, height, bx, by, best_mvx, best_mvy, block_size);
    best_cost = best_sad + n148_mv_cost(best_mvx * 4, best_mvy * 4, pred_mvx, pred_mvy, lambda);
    
    for (step = 2; step >= 1; step--) {
        const int (*pattern)[2] = (step == 2) ? g_diamond_large : g_diamond_small;
        int pattern_size = (step == 2) ? 8 : 4;
        
        improved = 1;
        while (improved) {
            improved = 0;
            for (i = 0; i < pattern_size; i++) {
                int dx = (step == 2) ? pattern[i][0] : pattern[i][0];
                int dy = (step == 2) ? pattern[i][1] : pattern[i][1];
                int cand_mvx = best_mvx + dx;
                int cand_mvy = best_mvy + dy;
                int cand_sad, cand_cost;
                
                if (cand_mvx < -search_range || cand_mvx > search_range ||
                    cand_mvy < -search_range || cand_mvy > search_range)
                    continue;
                
                cand_sad = get_block_sad(cur, cur_stride, ref, ref_stride,
                                         width, height, bx, by, cand_mvx, cand_mvy, block_size);
                if (cand_sad >= INT_MAX) continue;
                
                cand_cost = cand_sad + n148_mv_cost(cand_mvx * 4, cand_mvy * 4, pred_mvx, pred_mvy, lambda);
                
                if (cand_cost < best_cost) {
                    best_cost = cand_cost;
                    best_sad = cand_sad;
                    best_mvx = cand_mvx;
                    best_mvy = cand_mvy;
                    improved = 1;
                }
            }
        }
    }
    
    out->mv.x = (int16_t)best_mvx;
    out->mv.y = (int16_t)best_mvy;
    out->sad = best_sad;
    out->cost = best_cost;
    out->ref_idx = 0;
    out->satd = 0;
    return 0;
}

int n148_motion_search_hexagon(const uint8_t* cur, int cur_stride,
                               const uint8_t* ref, int ref_stride,
                               int width, int height,
                               int bx, int by,
                               int block_size,
                               int search_range,
                               const N148MV* mvp,
                               int lambda,
                               N148MECandidate* out)
{
    int best_mvx = mvp ? clip_mv(mvp->x, -search_range, search_range) : 0;
    int best_mvy = mvp ? clip_mv(mvp->y, -search_range, search_range) : 0;
    int best_sad, best_cost;
    int pred_mvx = mvp ? mvp->x * 4 : 0;
    int pred_mvy = mvp ? mvp->y * 4 : 0;
    int improved;
    int i;
    
    best_sad = get_block_sad(cur, cur_stride, ref, ref_stride,
                             width, height, bx, by, best_mvx, best_mvy, block_size);
    best_cost = best_sad + n148_mv_cost(best_mvx * 4, best_mvy * 4, pred_mvx, pred_mvy, lambda);
    
    improved = 1;
    while (improved) {
        improved = 0;
        for (i = 0; i < 6; i++) {
            int cand_mvx = best_mvx + g_hexagon[i][0];
            int cand_mvy = best_mvy + g_hexagon[i][1];
            int cand_sad, cand_cost;
            
            if (cand_mvx < -search_range || cand_mvx > search_range ||
                cand_mvy < -search_range || cand_mvy > search_range)
                continue;
            
            cand_sad = get_block_sad(cur, cur_stride, ref, ref_stride,
                                     width, height, bx, by, cand_mvx, cand_mvy, block_size);
            if (cand_sad >= INT_MAX) continue;
            
            cand_cost = cand_sad + n148_mv_cost(cand_mvx * 4, cand_mvy * 4, pred_mvx, pred_mvy, lambda);
            
            if (cand_cost < best_cost) {
                best_cost = cand_cost;
                best_sad = cand_sad;
                best_mvx = cand_mvx;
                best_mvy = cand_mvy;
                improved = 1;
            }
        }
    }
    
    improved = 1;
    while (improved) {
        improved = 0;
        for (i = 0; i < 4; i++) {
            int cand_mvx = best_mvx + g_diamond_small[i][0];
            int cand_mvy = best_mvy + g_diamond_small[i][1];
            int cand_sad, cand_cost;
            
            if (cand_mvx < -search_range || cand_mvx > search_range ||
                cand_mvy < -search_range || cand_mvy > search_range)
                continue;
            
            cand_sad = get_block_sad(cur, cur_stride, ref, ref_stride,
                                     width, height, bx, by, cand_mvx, cand_mvy, block_size);
            if (cand_sad >= INT_MAX) continue;
            
            cand_cost = cand_sad + n148_mv_cost(cand_mvx * 4, cand_mvy * 4, pred_mvx, pred_mvy, lambda);
            
            if (cand_cost < best_cost) {
                best_cost = cand_cost;
                best_sad = cand_sad;
                best_mvx = cand_mvx;
                best_mvy = cand_mvy;
                improved = 1;
            }
        }
    }
    
    out->mv.x = (int16_t)best_mvx;
    out->mv.y = (int16_t)best_mvy;
    out->sad = best_sad;
    out->cost = best_cost;
    out->ref_idx = 0;
    out->satd = 0;
    return 0;
}

int n148_motion_search_enhanced(const uint8_t* cur, int cur_stride,
                                const uint8_t* ref, int ref_stride,
                                int width, int height,
                                int bx, int by,
                                int block_size,
                                const N148MEConfig* config,
                                const N148MVPredictors* preds,
                                N148MECandidate* out)
{
    N148MV mvp;
    N148MV candidates[N148_ME_MAX_MV_CANDIDATES];
    int num_cands = 0;
    int best_cost = INT_MAX;
    int best_sad = INT_MAX;
    int best_mvx = 0;
    int best_mvy = 0;
    int pred_mvx, pred_mvy;
    int i;
    int bs = (block_size == N148_ME_BLOCK_4x4) ? 4 :
             (block_size == N148_ME_BLOCK_8x8) ? 8 : 16;
    
    n148_median_mv_predictor(preds, &mvp);
    pred_mvx = mvp.x * 4;
    pred_mvy = mvp.y * 4;
    
    candidates[num_cands].x = 0;
    candidates[num_cands].y = 0;
    num_cands++;
    
    candidates[num_cands] = mvp;
    num_cands++;
    
    if (preds->left_valid) {
        candidates[num_cands] = preds->left;
        num_cands++;
    }
    if (preds->top_valid) {
        candidates[num_cands] = preds->top;
        num_cands++;
    }
    if (preds->top_right_valid) {
        candidates[num_cands] = preds->top_right;
        num_cands++;
    }
    if (preds->coloc_valid) {
        candidates[num_cands] = preds->coloc;
        num_cands++;
    }
    
    for (i = 0; i < num_cands && i < N148_ME_MAX_MV_CANDIDATES; i++) {
        int mvx = clip_mv(candidates[i].x, -config->search_range, config->search_range);
        int mvy = clip_mv(candidates[i].y, -config->search_range, config->search_range);
        int rx = bx + mvx;
        int ry = by + mvy;
        int sad, cost;
        
        if (rx < 0 || ry < 0 || rx + bs > width || ry + bs > height)
            continue;
        
        sad = get_block_sad(cur, cur_stride, ref, ref_stride,
                            width, height, bx, by, mvx, mvy, block_size);
        if (sad >= INT_MAX) continue;

        if (config->enable_satd) {
            int satd = get_block_satd(cur, cur_stride, ref, ref_stride,
                                      width, height, bx, by, mvx, mvy, block_size);
            if (satd < INT_MAX)
                cost = ((sad) + satd + 1) / 2;
            else
                cost = sad;
        } else {
            cost = sad;
        }

        cost += n148_mv_cost(mvx * 4, mvy * 4, pred_mvx, pred_mvy, config->lambda);
        
        if (cost < best_cost) {
            best_cost = cost;
            best_sad = sad;
            best_mvx = mvx;
            best_mvy = mvy;
        }
    }
    
    if (config->enable_early_term && best_sad < config->early_term_threshold) {
        out->mv.x = (int16_t)best_mvx;
        out->mv.y = (int16_t)best_mvy;
        out->sad = best_sad;
        out->cost = best_cost;
        out->ref_idx = 0;
        out->satd = 0;
        return 0;
    }
    
    {
        N148MECandidate search_result;
        N148MV start_mv;
        start_mv.x = (int16_t)best_mvx;
        start_mv.y = (int16_t)best_mvy;
        
        if (config->search_method == N148_ME_SEARCH_HEXAGON) {
            n148_motion_search_hexagon(cur, cur_stride, ref, ref_stride,
                                       width, height, bx, by, block_size,
                                       config->search_range, &start_mv,
                                       config->lambda, &search_result);
        } else if (config->search_method == N148_ME_SEARCH_FULL) {
            n148_motion_search_full(cur, cur_stride, ref, ref_stride,
                                    width, height, bx, by, block_size,
                                    config->search_range, &start_mv,
                                    config->lambda, &search_result);
        } else {
            n148_motion_search_diamond(cur, cur_stride, ref, ref_stride,
                                       width, height, bx, by, block_size,
                                       config->search_range, &start_mv,
                                       config->lambda, &search_result);
        }
        
        if (search_result.cost < best_cost) {
            best_cost = search_result.cost;
            best_sad = search_result.sad;
            best_mvx = search_result.mv.x;
            best_mvy = search_result.mv.y;
        }
    }
    
    out->mv.x = (int16_t)best_mvx;
    out->mv.y = (int16_t)best_mvy;
    out->sad = best_sad;
    out->cost = best_cost;
    out->ref_idx = 0;
    out->satd = 0;
    return 0;
}

int n148_subpel_refine_hpel(const uint8_t* cur, int cur_stride,
                            const uint8_t* ref, int ref_stride,
                            int width, int height,
                            int bx, int by,
                            int block_size,
                            N148MECandidate* cand,
                            const N148MV* mvp,
                            int lambda)
{
    (void)cur; (void)cur_stride; (void)ref; (void)ref_stride;
    (void)width; (void)height; (void)bx; (void)by;
    (void)block_size; (void)mvp; (void)lambda;
    cand->mv.x = (int16_t)(cand->mv.x * 2);
    cand->mv.y = (int16_t)(cand->mv.y * 2);
    return 0;
}

int n148_subpel_refine_qpel(const uint8_t* cur, int cur_stride,
                            const uint8_t* ref, int ref_stride,
                            int width, int height,
                            int bx, int by,
                            int block_size,
                            N148MECandidate* cand,
                            const N148MV* mvp,
                            int lambda)
{
    (void)cur; (void)cur_stride; (void)ref; (void)ref_stride;
    (void)width; (void)height; (void)bx; (void)by;
    (void)block_size; (void)mvp; (void)lambda;
    cand->mv.x = (int16_t)(cand->mv.x * 2);
    cand->mv.y = (int16_t)(cand->mv.y * 2);
    return 0;
}

void n148_me_config_defaults(N148MEConfig* config)
{
    config->search_method = N148_ME_SEARCH_DIAMOND;
    config->search_range = 16;
    config->enable_satd = 1;
    config->enable_early_term = 1;
    config->early_term_threshold = 256;
    config->subpel_refine = 2;
    config->max_refs = 1;
    config->lambda = 32;
    config->qp = 26;
}

int n148_mv_field_alloc(N148MVField* field, int mb_width, int mb_height)
{
    int total = mb_width * mb_height * 16;
    field->mv_field = (N148MV*)calloc((size_t)total, sizeof(N148MV));
    if (!field->mv_field) return -1;
    field->mb_width = mb_width;
    field->mb_height = mb_height;
    field->stride = mb_width * 4;
    return 0;
}

void n148_mv_field_free(N148MVField* field)
{
    if (field) {
        free(field->mv_field);
        field->mv_field = NULL;
    }
}

void n148_mv_field_clear(N148MVField* field)
{
    if (field && field->mv_field) {
        memset(field->mv_field, 0, 
               (size_t)(field->mb_width * field->mb_height * 16) * sizeof(N148MV));
    }
}

void n148_mv_field_set(N148MVField* field, int mb_x, int mb_y, int blk_idx, N148MV mv)
{
    if (!field || !field->mv_field) return;
    if (mb_x < 0 || mb_x >= field->mb_width) return;
    if (mb_y < 0 || mb_y >= field->mb_height) return;
    if (blk_idx < 0 || blk_idx >= 16) return;
    
    int idx = (mb_y * field->mb_width + mb_x) * 16 + blk_idx;
    field->mv_field[idx] = mv;
}

N148MV n148_mv_field_get(const N148MVField* field, int mb_x, int mb_y, int blk_idx)
{
    N148MV zero = {0, 0};
    if (!field || !field->mv_field) return zero;
    if (mb_x < 0 || mb_x >= field->mb_width) return zero;
    if (mb_y < 0 || mb_y >= field->mb_height) return zero;
    if (blk_idx < 0 || blk_idx >= 16) return zero;
    
    int idx = (mb_y * field->mb_width + mb_x) * 16 + blk_idx;
    return field->mv_field[idx];
}