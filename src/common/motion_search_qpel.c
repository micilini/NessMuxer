#include "motion_search_qpel.h"
#include "motion_search.h"
#include "interpolation.h"

#include <limits.h>
#include <string.h>

static const int g_hpel_pattern[8][2] = {
    { -2,  0 },
    {  2,  0 },
    {  0, -2 },
    {  0,  2 },
    { -2, -2 },
    {  2, -2 },
    { -2,  2 },
    {  2,  2 }
};

static const int g_qpel_pattern[8][2] = {
    { -1,  0 },
    {  1,  0 },
    {  0, -1 },
    {  0,  1 },
    { -1, -1 },
    {  1, -1 },
    { -1,  1 },
    {  1,  1 }
};

static inline int abs_val(int v)
{
    return (v < 0) ? -v : v;
}

static void load_cur_block_4x4(const uint8_t* plane, int stride,
                               int width, int height,
                               int bx, int by,
                               int sample_stride, int sample_offset,
                               uint8_t out[16])
{
    int y, x;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int px = bx + x;
            int py = by + y;
            if (px < width && py < height)
                out[y * 4 + x] = plane[py * stride + px * sample_stride + sample_offset];
            else
                out[y * 4 + x] = 128;
        }
    }
}

static void load_cur_block_8x8(const uint8_t* plane, int stride,
                               int width, int height,
                               int bx, int by,
                               uint8_t out[64])
{
    int y, x;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            int px = bx + x;
            int py = by + y;
            if (px < width && py < height)
                out[y * 8 + x] = plane[py * stride + px];
            else
                out[y * 8 + x] = 128;
        }
    }
}

static int block_sad_qpel_4x4(const uint8_t cur[16],
                              const uint8_t* ref_plane, int ref_stride,
                              int width, int height,
                              int bx, int by,
                              int mvx_q4, int mvy_q4,
                              int sample_stride, int sample_offset)
{
    uint8_t pred[16];
    int sad = 0;
    int i;

    n148_interp_block_4x4_qpel(pred,
                               ref_plane, ref_stride,
                               width, height,
                               bx, by,
                               mvx_q4, mvy_q4,
                               sample_stride, sample_offset);

    for (i = 0; i < 16; i++) {
        int d = (int)cur[i] - (int)pred[i];
        sad += abs_val(d);
    }

    return sad;
}

int n148_qpel_sad_4x4(const uint8_t* cur, int cur_stride,
                      const uint8_t* ref, int ref_stride,
                      int width, int height,
                      int bx, int by,
                      int mvx_q4, int mvy_q4)
{
    uint8_t cur_block[16];
    load_cur_block_4x4(cur, cur_stride, width, height, bx, by, 1, 0, cur_block);
    return block_sad_qpel_4x4(cur_block, ref, ref_stride, width, height,
                              bx, by, mvx_q4, mvy_q4, 1, 0);
}

int n148_qpel_sad_8x8(const uint8_t* cur, int cur_stride,
                      const uint8_t* ref, int ref_stride,
                      int width, int height,
                      int bx, int by,
                      int mvx_q4, int mvy_q4)
{
    int sad = 0;
    sad += n148_qpel_sad_4x4(cur, cur_stride, ref, ref_stride, width, height,
                             bx, by, mvx_q4, mvy_q4);
    sad += n148_qpel_sad_4x4(cur, cur_stride, ref, ref_stride, width, height,
                             bx + 4, by, mvx_q4, mvy_q4);
    sad += n148_qpel_sad_4x4(cur, cur_stride, ref, ref_stride, width, height,
                             bx, by + 4, mvx_q4, mvy_q4);
    sad += n148_qpel_sad_4x4(cur, cur_stride, ref, ref_stride, width, height,
                             bx + 4, by + 4, mvx_q4, mvy_q4);
    return sad;
}

static int n148_qpel_satd_4x4(const uint8_t* cur, int cur_stride,
                               const uint8_t* ref, int ref_stride,
                               int width, int height,
                               int bx, int by,
                               int mvx_q4, int mvy_q4)
{
    uint8_t cur_block[16];
    uint8_t pred[16];

    load_cur_block_4x4(cur, cur_stride, width, height, bx, by, 1, 0, cur_block);
    n148_interp_block_4x4_qpel(pred,
                               ref, ref_stride,
                               width, height,
                               bx, by,
                               mvx_q4, mvy_q4,
                               1, 0);

    return n148_satd_4x4(cur_block, 4, pred, 4);
}

static int n148_qpel_satd_8x8(const uint8_t* cur, int cur_stride,
                               const uint8_t* ref, int ref_stride,
                               int width, int height,
                               int bx, int by,
                               int mvx_q4, int mvy_q4)
{
    int satd = 0;
    satd += n148_qpel_satd_4x4(cur, cur_stride, ref, ref_stride, width, height,
                               bx, by, mvx_q4, mvy_q4);
    satd += n148_qpel_satd_4x4(cur, cur_stride, ref, ref_stride, width, height,
                               bx + 4, by, mvx_q4, mvy_q4);
    satd += n148_qpel_satd_4x4(cur, cur_stride, ref, ref_stride, width, height,
                               bx, by + 4, mvx_q4, mvy_q4);
    satd += n148_qpel_satd_4x4(cur, cur_stride, ref, ref_stride, width, height,
                               bx + 4, by + 4, mvx_q4, mvy_q4);
    return satd;
}

static int subpel_refine_4x4(const uint8_t cur[16],
                             const uint8_t* ref_plane, int ref_stride,
                             int width, int height,
                             int bx, int by,
                             int sample_stride, int sample_offset,
                             int base_mvx_q4, int base_mvy_q4,
                             int* out_mvx_q4, int* out_mvy_q4,
                             int pred_mvx_q4, int pred_mvy_q4,
                             int lambda)
{
    int best_mvx = base_mvx_q4;
    int best_mvy = base_mvy_q4;
    int best_sad = block_sad_qpel_4x4(cur, ref_plane, ref_stride,
                                       width, height, bx, by,
                                       best_mvx, best_mvy,
                                       sample_stride, sample_offset);
    int best_cost = best_sad + n148_mv_cost(best_mvx, best_mvy, pred_mvx_q4, pred_mvy_q4, lambda);
    int i;
    int improved;

    improved = 1;
    while (improved) {
        improved = 0;
        for (i = 0; i < 8; i++) {
            int cand_mvx = best_mvx + g_hpel_pattern[i][0];
            int cand_mvy = best_mvy + g_hpel_pattern[i][1];
            int cand_sad = block_sad_qpel_4x4(cur, ref_plane, ref_stride,
                                               width, height, bx, by,
                                               cand_mvx, cand_mvy,
                                               sample_stride, sample_offset);
            int cand_cost = cand_sad + n148_mv_cost(cand_mvx, cand_mvy, pred_mvx_q4, pred_mvy_q4, lambda);

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
        for (i = 0; i < 8; i++) {
            int cand_mvx = best_mvx + g_qpel_pattern[i][0];
            int cand_mvy = best_mvy + g_qpel_pattern[i][1];
            int cand_sad = block_sad_qpel_4x4(cur, ref_plane, ref_stride,
                                               width, height, bx, by,
                                               cand_mvx, cand_mvy,
                                               sample_stride, sample_offset);
            int cand_cost = cand_sad + n148_mv_cost(cand_mvx, cand_mvy, pred_mvx_q4, pred_mvy_q4, lambda);

            if (cand_cost < best_cost) {
                best_cost = cand_cost;
                best_sad = cand_sad;
                best_mvx = cand_mvx;
                best_mvy = cand_mvy;
                improved = 1;
            }
        }
    }

    *out_mvx_q4 = best_mvx;
    *out_mvy_q4 = best_mvy;
    return best_sad;
}

int n148_motion_search_refs_qpel_4x4(const uint8_t* cur_plane, int cur_stride,
                                     const uint8_t* const* ref_planes, int ref_count, int ref_stride,
                                     int width, int height,
                                     int bx, int by,
                                     int sample_stride, int sample_offset,
                                     int search_range,
                                     N148QpelMotionResult* out)
{
    uint8_t cur[16];
    int best_ref = 0;
    int best_mvx_q4 = 0;
    int best_mvy_q4 = 0;
    int best_sad = INT_MAX;
    int best_cost = INT_MAX;
    int r;
    int lambda = 32;

    if (!cur_plane || !ref_planes || ref_count <= 0 || !out)
        return -1;

    load_cur_block_4x4(cur_plane, cur_stride,
                       width, height,
                       bx, by,
                       sample_stride, sample_offset,
                       cur);

    for (r = 0; r < ref_count; r++) {
        N148MECandidate intpel_result;
        int refined_mvx_q4, refined_mvy_q4;
        int refined_sad;
        int refined_cost;

        if (!ref_planes[r])
            continue;

        n148_motion_search_diamond(cur_plane, cur_stride,
                                   ref_planes[r], ref_stride,
                                   width, height,
                                   bx, by,
                                   N148_ME_BLOCK_4x4,
                                   search_range,
                                   NULL,
                                   lambda,
                                   &intpel_result);

        refined_sad = subpel_refine_4x4(cur,
                                        ref_planes[r], ref_stride,
                                        width, height,
                                        bx, by,
                                        sample_stride, sample_offset,
                                        intpel_result.mv.x * 4,
                                        intpel_result.mv.y * 4,
                                        &refined_mvx_q4,
                                        &refined_mvy_q4,
                                        0, 0, lambda);

        refined_cost = refined_sad + n148_mv_cost(refined_mvx_q4, refined_mvy_q4, 0, 0, lambda);
        refined_cost += r * 8;

        if (refined_cost < best_cost) {
            best_cost = refined_cost;
            best_sad = refined_sad;
            best_ref = r;
            best_mvx_q4 = refined_mvx_q4;
            best_mvy_q4 = refined_mvy_q4;
        }
    }

    out->ref_idx = best_ref;
    out->mvx_q4 = best_mvx_q4;
    out->mvy_q4 = best_mvy_q4;
    out->sad = best_sad;
    out->cost = best_cost;
    return 0;
}

int n148_motion_search_enhanced_qpel(const uint8_t* cur_plane, int cur_stride,
                                     const uint8_t* const* ref_planes, int ref_count, int ref_stride,
                                     int width, int height,
                                     int bx, int by,
                                     int block_size,
                                     const N148MEConfig* config,
                                     const N148MVPredictors* preds,
                                     N148QpelMotionResult* out)
{
    uint8_t cur[16];
    N148MV mvp;
    int best_ref = 0;
    int best_mvx_q4 = 0;
    int best_mvy_q4 = 0;
    int best_sad = INT_MAX;
    int best_cost = INT_MAX;
    int r;

    if (!cur_plane || !ref_planes || ref_count <= 0 || !out || !config)
        return -1;

    load_cur_block_4x4(cur_plane, cur_stride,
                       width, height,
                       bx, by, 1, 0, cur);

    n148_median_mv_predictor(preds, &mvp);

    for (r = 0; r < ref_count && r < config->max_refs; r++) {
        N148MECandidate intpel_result;
        int refined_mvx_q4, refined_mvy_q4;
        int refined_sad;
        int refined_cost;
        int pred_mvx_q4 = mvp.x * 4;
        int pred_mvy_q4 = mvp.y * 4;

        if (!ref_planes[r])
            continue;

        n148_motion_search_enhanced(cur_plane, cur_stride,
                                    ref_planes[r], ref_stride,
                                    width, height,
                                    bx, by,
                                    block_size,
                                    config,
                                    preds,
                                    &intpel_result);

        if (config->subpel_refine > 0) {
            refined_sad = subpel_refine_4x4(cur,
                                            ref_planes[r], ref_stride,
                                            width, height,
                                            bx, by, 1, 0,
                                            intpel_result.mv.x * 4,
                                            intpel_result.mv.y * 4,
                                            &refined_mvx_q4,
                                            &refined_mvy_q4,
                                            pred_mvx_q4, pred_mvy_q4,
                                            config->lambda);
        } else {
            refined_mvx_q4 = intpel_result.mv.x * 4;
            refined_mvy_q4 = intpel_result.mv.y * 4;
            refined_sad = intpel_result.sad;
        }

        if (config->enable_satd) {
            int satd_cost;
            if (block_size == N148_ME_BLOCK_8x8)
                satd_cost = n148_qpel_satd_8x8(cur_plane, cur_stride, ref_planes[r], ref_stride,
                                               width, height, bx, by, refined_mvx_q4, refined_mvy_q4);
            else
                satd_cost = n148_qpel_satd_4x4(cur_plane, cur_stride, ref_planes[r], ref_stride,
                                               width, height, bx, by, refined_mvx_q4, refined_mvy_q4);
            refined_cost = ((refined_sad) + satd_cost + 1) / 2;
        } else {
            refined_cost = refined_sad;
        }

        refined_cost += n148_mv_cost(refined_mvx_q4, refined_mvy_q4,
                                     pred_mvx_q4, pred_mvy_q4, config->lambda);
        refined_cost += r * 8;

        if (r > 0 && best_cost < INT_MAX) {
            int required_gain = 12 + (r * 4);
            if (refined_cost + required_gain >= best_cost)
                continue;
        }

        if (refined_cost < best_cost) {
            best_cost = refined_cost;
            best_sad = refined_sad;
            best_ref = r;
            best_mvx_q4 = refined_mvx_q4;
            best_mvy_q4 = refined_mvy_q4;
        }
    }

    out->ref_idx = best_ref;
    out->mvx_q4 = best_mvx_q4;
    out->mvy_q4 = best_mvy_q4;
    out->sad = best_sad;
    out->cost = best_cost;
    return 0;
}