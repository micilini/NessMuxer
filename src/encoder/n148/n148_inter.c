#include "n148_inter.h"
#include "../../common/interpolation.h"
#include <string.h>

static inline int abs_val(int v)
{
    return (v < 0) ? -v : v;
}

static int mv_bit_cost(int ref_idx, int mvx_q4, int mvy_q4)
{
    int ax = abs_val(mvx_q4);
    int ay = abs_val(mvy_q4);
    return 4 + ref_idx * 2 + ax / 2 + ay / 2;
}

static int compute_lambda(int qp)
{
    static const int lambda_table[52] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 3, 3, 3, 4, 4, 5,
        6, 6, 7, 8, 9, 10, 11, 13, 14, 16,
        18, 20, 23, 25, 29, 32, 36, 40, 45, 51,
        57, 64, 72, 81, 91, 102, 114, 128, 144, 161,
        181, 203
    };
    if (qp < 0) qp = 0;
    if (qp > 51) qp = 51;
    return lambda_table[qp];
}

void n148_inter_ctx_init(N148InterContext* ctx)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    n148_me_config_defaults(&ctx->me_config);
    ctx->use_enhanced_me = 1;
}

void n148_inter_ctx_set_mb_pos(N148InterContext* ctx, int mb_x, int mb_y)
{
    if (!ctx) return;
    ctx->mb_x = mb_x;
    ctx->mb_y = mb_y;
}

void n148_inter_ctx_set_mv_field(N148InterContext* ctx, N148MVField* field)
{
    if (!ctx) return;
    ctx->mv_field = field;
}

void n148_inter_ctx_set_prev_mv_field(N148InterContext* ctx, N148MVField* field)
{
    if (!ctx) return;
    ctx->mv_field_prev = field;
}

int n148_inter_choose_4x4(const uint8_t* cur_plane,
                          const uint8_t* const* ref_planes, int ref_count,
                          int stride,
                          int width, int height,
                          int bx, int by,
                          int sample_stride, int sample_offset,
                          int search_range,
                          int qp,
                          int intra_sad_hint,
                          N148InterDecision* out)
{
    N148QpelMotionResult mr;
    int lambda;

    if (!cur_plane || !ref_planes || ref_count <= 0 || !out)
        return -1;

    if (n148_motion_search_refs_qpel_4x4(cur_plane, stride,
                                         ref_planes, ref_count, stride,
                                         width, height,
                                         bx, by,
                                         sample_stride, sample_offset,
                                         search_range,
                                         &mr) != 0)
        return -1;

    lambda = compute_lambda(qp);

    out->ref_idx = mr.ref_idx;
    out->mvx_q4 = mr.mvx_q4;
    out->mvy_q4 = mr.mvy_q4;
    out->sad = mr.sad;
    out->cost_inter = mr.sad + lambda * mv_bit_cost(mr.ref_idx, mr.mvx_q4, mr.mvy_q4);
    out->cost_intra = intra_sad_hint + lambda * 3;
    out->cost_skip = (mr.ref_idx == 0 && mr.mvx_q4 == 0 && mr.mvy_q4 == 0) ? mr.sad : out->cost_inter + 1000;

    if (mr.ref_idx == 0 && mr.mvx_q4 == 0 && mr.mvy_q4 == 0 && mr.sad <= 4)
        out->mode = 0;
    else if (out->cost_inter + 2 < out->cost_intra)
        out->mode = 1;
    else
        out->mode = 2;

    return 0;
}

int n148_inter_choose_enhanced(const uint8_t* cur_plane,
                               const uint8_t* const* ref_planes, int ref_count,
                               int stride,
                               int width, int height,
                               int bx, int by,
                               int qp,
                               int intra_sad_hint,
                               N148InterContext* ctx,
                               N148InterDecision* out)
{
    N148MVPredictors preds;
    N148QpelMotionResult mr;
    int lambda;
    int blk_x, blk_y;

    if (!cur_plane || !ref_planes || ref_count <= 0 || !out || !ctx)
        return -1;

    blk_x = bx / 4;
    blk_y = by / 4;

    memset(&preds, 0, sizeof(preds));
    if (ctx->mv_field) {
        n148_get_mv_predictors(ctx->mv_field, ctx->mb_x, ctx->mb_y, 0, &preds);
    }

    if (ctx->mv_field_prev) {
        preds.coloc = n148_mv_field_get(ctx->mv_field_prev, ctx->mb_x, ctx->mb_y, 0);
        preds.coloc_valid = 1;
    }

    lambda = compute_lambda(qp);
    ctx->me_config.lambda = lambda;
    ctx->me_config.qp = qp;

    if (n148_motion_search_enhanced_qpel(cur_plane, stride,
                                         ref_planes, ref_count, stride,
                                         width, height,
                                         bx, by,
                                         N148_ME_BLOCK_4x4,
                                         &ctx->me_config,
                                         &preds,
                                         &mr) != 0)
        return -1;

    out->ref_idx = mr.ref_idx;
    out->mvx_q4 = mr.mvx_q4;
    out->mvy_q4 = mr.mvy_q4;
    out->sad = mr.sad;
    out->cost_inter = mr.cost;
    out->cost_intra = intra_sad_hint + lambda * 3;
    out->cost_skip = (mr.ref_idx == 0 && mr.mvx_q4 == 0 && mr.mvy_q4 == 0) ? mr.sad : out->cost_inter + 1000;

    if (mr.ref_idx == 0 && mr.mvx_q4 == 0 && mr.mvy_q4 == 0 && mr.sad <= 4)
        out->mode = 0;
    else if (out->cost_inter + 2 < out->cost_intra)
        out->mode = 1;
    else
        out->mode = 2;

    if (ctx->mv_field && out->mode <= 1) {
        N148MV stored_mv;
        stored_mv.x = (int16_t)(mr.mvx_q4 / 4);
        stored_mv.y = (int16_t)(mr.mvy_q4 / 4);
        int blk_idx = (blk_y % 4) * 4 + (blk_x % 4);
        n148_mv_field_set(ctx->mv_field, ctx->mb_x, ctx->mb_y, blk_idx, stored_mv);
    }

    return 0;
}

int n148_inter_choose_8x8(const uint8_t* cur_plane,
                          const uint8_t* const* ref_planes, int ref_count,
                          int stride,
                          int width, int height,
                          int bx, int by,
                          int qp,
                          int intra_sad_hint,
                          N148InterContext* ctx,
                          N148InterDecision* out)
{
    N148MVPredictors preds;
    N148QpelMotionResult mr;
    int lambda;

    if (!cur_plane || !ref_planes || ref_count <= 0 || !out || !ctx)
        return -1;

    memset(&preds, 0, sizeof(preds));
    if (ctx->mv_field) {
        n148_get_mv_predictors(ctx->mv_field, ctx->mb_x, ctx->mb_y, 0, &preds);
    }

    if (ctx->mv_field_prev) {
        preds.coloc = n148_mv_field_get(ctx->mv_field_prev, ctx->mb_x, ctx->mb_y, 0);
        preds.coloc_valid = 1;
    }

    lambda = compute_lambda(qp);
    ctx->me_config.lambda = lambda;
    ctx->me_config.qp = qp;

    if (n148_motion_search_enhanced_qpel(cur_plane, stride,
                                         ref_planes, ref_count, stride,
                                         width, height,
                                         bx, by,
                                         N148_ME_BLOCK_8x8,
                                         &ctx->me_config,
                                         &preds,
                                         &mr) != 0)
        return -1;

    out->ref_idx = mr.ref_idx;
    out->mvx_q4 = mr.mvx_q4;
    out->mvy_q4 = mr.mvy_q4;
    out->sad = mr.sad;
    out->cost_inter = mr.cost;
    out->cost_intra = intra_sad_hint + lambda * 6;
    out->cost_skip = (mr.ref_idx == 0 && mr.mvx_q4 == 0 && mr.mvy_q4 == 0) ? mr.sad : out->cost_inter + 1000;

    if (mr.ref_idx == 0 && mr.mvx_q4 == 0 && mr.mvy_q4 == 0 && mr.sad <= 16)
        out->mode = 0;
    else if (out->cost_inter + 4 < out->cost_intra)
        out->mode = 1;
    else
        out->mode = 2;

    return 0;
}