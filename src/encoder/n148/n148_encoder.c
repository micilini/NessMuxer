#include "n148_encoder.h"
#include "n148_intra.h"
#include "n148_inter.h"
#include "n148_transform.h"
#include "n148_quant.h"
#include "n148_entropy_enc_cavlc.h"
#include "n148_ratecontrol_basic.h"
#include "n148_reorder.h"
#include "../../common/entropy/n148_cavlc.h"
#include "../../common/entropy/n148_cabac.h"
#include "../../common/interpolation.h"
#include "../../common/motion_search_qpel.h"

#include "../../codec/n148/n148_spec.h"
#include "../../codec/n148/n148_bitstream.h"
#include "../../codec/n148/n148_codec.h"
#include "../../codec/n148/n148_codec_private.h"
#include "../../decoder/n148/n148_frame_recon.h"

#include "n148_ratecontrol.h"
#include "n148_profiles.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define N148_ENC_LOG(fmt, ...) \
    fprintf(stderr, "[N148 ENC] " fmt "\n", ##__VA_ARGS__)

typedef struct {
    int width;
    int height;
    int fps;
    int bitrate_kbps;

    int qp;
    int keyint;
    int search_range;
    int profile;
    int entropy_mode;

    int64_t frame_duration_hns;
    int64_t next_pts_hns;
    int64_t next_dts_hns;
    uint32_t frame_number;

    uint8_t codec_private[N148_SEQ_HEADER_SIZE];
    int codec_private_size;

    uint8_t* ref_y[4];
    uint8_t* ref_uv[4];
    int ref_count;
    int max_refs;
    int enable_qpel;

    N148MVField mv_field_cur;
    N148MVField mv_field_prev;
    N148InterContext inter_ctx;
    int use_enhanced_me;

    struct {
        uint8_t* data;
        int      size;
        int      is_keyframe;
        int64_t  pts_hns;
        int64_t  dts_hns;
        int64_t  duration_hns;
    } out_queue[8];

    int out_queue_head;
    int out_queue_tail;
    int out_queue_count;

    int max_bframes;
    int reorder_delay;

    N148ReorderQueue reorder_queue;

    N148RateControl rc_adv;
    int use_advanced_rc;
} N148EncoderCtx;

static uint8_t clip_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static int n148_queue_push_packet(N148EncoderCtx* ctx,
                                  uint8_t* data,
                                  int size,
                                  int is_keyframe,
                                  int64_t pts_hns,
                                  int64_t dts_hns,
                                  int64_t duration_hns)
{
    int slot;

    if (!ctx || !data || size <= 0)
        return -1;

    if (ctx->out_queue_count >= (int)(sizeof(ctx->out_queue) / sizeof(ctx->out_queue[0])))
        return -1;

    slot = ctx->out_queue_tail;

    ctx->out_queue[slot].data = data;
    ctx->out_queue[slot].size = size;
    ctx->out_queue[slot].is_keyframe = is_keyframe;
    ctx->out_queue[slot].pts_hns = pts_hns;
    ctx->out_queue[slot].dts_hns = dts_hns;
    ctx->out_queue[slot].duration_hns = duration_hns;

    ctx->out_queue_tail = (ctx->out_queue_tail + 1) % (int)(sizeof(ctx->out_queue) / sizeof(ctx->out_queue[0]));
    ctx->out_queue_count++;

    return 0;
}

static int n148_queue_pop_packet(N148EncoderCtx* ctx, NessEncodedPacket* pkt)
{
    int slot;

    if (!ctx || !pkt)
        return -1;

    if (ctx->out_queue_count <= 0)
        return 1;

    slot = ctx->out_queue_head;

    memset(pkt, 0, sizeof(*pkt));
    pkt->data = ctx->out_queue[slot].data;
    pkt->size = ctx->out_queue[slot].size;
    pkt->is_keyframe = ctx->out_queue[slot].is_keyframe;
    pkt->pts_hns = ctx->out_queue[slot].pts_hns;
    pkt->dts_hns = ctx->out_queue[slot].dts_hns;
    pkt->duration_hns = ctx->out_queue[slot].duration_hns;

    ctx->out_queue[slot].data = NULL;
    ctx->out_queue[slot].size = 0;
    ctx->out_queue[slot].is_keyframe = 0;
    ctx->out_queue[slot].pts_hns = 0;
    ctx->out_queue[slot].dts_hns = 0;
    ctx->out_queue[slot].duration_hns = 0;

    ctx->out_queue_head = (ctx->out_queue_head + 1) % (int)(sizeof(ctx->out_queue) / sizeof(ctx->out_queue[0]));
    ctx->out_queue_count--;

    return 0;
}

static void load_block(const uint8_t* plane, int stride,
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

static void store_block(uint8_t* plane, int stride,
                        int width, int height,
                        int bx, int by,
                        int sample_stride, int sample_offset,
                        const uint8_t in[16])
{
    int y, x;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int px = bx + x;
            int py = by + y;
            if (px < width && py < height)
                plane[py * stride + px * sample_stride + sample_offset] = in[y * 4 + x];
        }
    }
}

static int block_sad_from_buffers(const uint8_t src[16], const uint8_t pred[16], int* out_sad)
{
    int sad = 0;
    int i;

    if (!out_sad)
        return -1;

    for (i = 0; i < 16; i++) {
        int d = (int)src[i] - (int)pred[i];
        sad += (d < 0) ? -d : d;
    }

    *out_sad = sad;
    return 0;
}

static void build_inter_prediction_4x4(const uint8_t* ref_plane, int stride,
                                       int width, int height,
                                       int bx, int by,
                                       int mvx_q4, int mvy_q4,
                                       int sample_stride, int sample_offset,
                                       uint8_t pred[16])
{
    n148_interp_block_4x4_qpel(pred,
                               ref_plane, stride,
                               width, height,
                               bx, by,
                               mvx_q4, mvy_q4,
                               sample_stride, sample_offset);
}

static int ensure_ref_buffers(N148EncoderCtx* ctx, int y_size, int uv_size)
{
    int i;

    for (i = 0; i < ctx->max_refs; i++) {
        if (!ctx->ref_y[i]) {
            ctx->ref_y[i] = (uint8_t*)malloc((size_t)y_size);
            if (!ctx->ref_y[i])
                return -1;
        }
        if (!ctx->ref_uv[i]) {
            ctx->ref_uv[i] = (uint8_t*)malloc((size_t)uv_size);
            if (!ctx->ref_uv[i])
                return -1;
        }
    }

    return 0;
}

static void push_reference_frame(N148EncoderCtx* ctx,
                                 const uint8_t* recon_y, const uint8_t* recon_uv,
                                 int y_size, int uv_size)
{
    int i;

    for (i = ctx->max_refs - 1; i > 0; i--) {
        memcpy(ctx->ref_y[i], ctx->ref_y[i - 1], (size_t)y_size);
        memcpy(ctx->ref_uv[i], ctx->ref_uv[i - 1], (size_t)uv_size);
    }

    memcpy(ctx->ref_y[0], recon_y, (size_t)y_size);
    memcpy(ctx->ref_uv[0], recon_uv, (size_t)uv_size);

    if (ctx->ref_count < ctx->max_refs)
        ctx->ref_count++;
}

static int encode_one_block(N148EncoderCtx* ctx,
                            N148BsWriter* bs,
                            N148CabacSession* cabac_session,
                            const uint8_t* src_plane,
                            uint8_t* recon_plane,
                            const uint8_t* const* ref_planes,
                            int ref_count,
                            int stride,
                            int width, int height,
                            int bx, int by,
                            int sample_stride, int sample_offset,
                            int qp,
                            int search_range,
                            int allow_inter,
                            int entropy_mode)
{
    uint8_t src[16];
    uint8_t pred[16];
    uint8_t intra_pred[16];
    uint8_t recon_u8[16];
    int16_t residual[16];
    int16_t coeff[16];
    int16_t qzigzag[16];
    int16_t qnatural[16];
    int16_t dequant[16];
    int16_t spatial[16];
    int intra_mode;
    int intra_sad = 0;
    int coeff_count;
    int i;
    int final_mode = 2;
    int ref_idx = 0;
    int mvx_q4 = 0;
    int mvy_q4 = 0;

    load_block(src_plane, stride, width, height,
               bx, by, sample_stride, sample_offset, src);

    intra_mode = n148_intra_choose_mode(src_plane, recon_plane, stride,
                                        width, height,
                                        bx, by,
                                        sample_stride, sample_offset,
                                        qp,
                                        intra_pred);

    block_sad_from_buffers(src, intra_pred, &intra_sad);

    memcpy(pred, intra_pred, sizeof(pred));

        if (allow_inter && ref_planes && ref_count > 0) {
        N148InterDecision decision;

        int inter_ret;

        if (ctx &&
            ctx->use_enhanced_me &&
            sample_stride == 1 &&
            sample_offset == 0) {
            inter_ret = n148_inter_choose_enhanced(src_plane, ref_planes, ref_count,
                                                   stride,
                                                   width, height,
                                                   bx, by,
                                                   qp,
                                                   intra_sad,
                                                   &ctx->inter_ctx,
                                                   &decision);
        } else {
            inter_ret = n148_inter_choose_4x4(src_plane, ref_planes, ref_count,
                                              stride,
                                              width, height,
                                              bx, by,
                                              sample_stride, sample_offset,
                                              search_range,
                                              qp,
                                              intra_sad,
                                              &decision);
        }

        if (inter_ret == 0) {
            if (decision.mode == 0 || decision.mode == 1) {
                final_mode = decision.mode;
                ref_idx = decision.ref_idx;
                mvx_q4 = decision.mvx_q4;
                mvy_q4 = decision.mvy_q4;
                build_inter_prediction_4x4(ref_planes[ref_idx], stride, width, height,
                                           bx, by, mvx_q4, mvy_q4,
                                           sample_stride, sample_offset,
                                           pred);
            } else {
                final_mode = 2;
            }
        }
    }

    if (entropy_mode == N148_ENTROPY_CABAC) {
        if (n148_cabac_write_block_mode(cabac_session, bs, (uint32_t)final_mode) != 0)
            return -1;
    } else {
        if (n148_bs_write_ue(bs, (uint32_t)final_mode) != 0)
            return -1;
    }

    if (final_mode == 2) {
        if (entropy_mode == N148_ENTROPY_CABAC) {
            if (n148_cabac_write_intra_mode(cabac_session, bs, (uint32_t)intra_mode) != 0)
                return -1;
        } else {
            if (n148_bs_write_ue(bs, (uint32_t)intra_mode) != 0)
                return -1;
        }
    } else {
        if (entropy_mode == N148_ENTROPY_CABAC) {
            if (n148_cabac_write_ref_idx(cabac_session, bs, (uint32_t)ref_idx) != 0)
                return -1;
            if (n148_cabac_write_mv(cabac_session, bs, mvx_q4, mvy_q4) != 0)
                return -1;
        } else {
            if (n148_bs_write_ue(bs, (uint32_t)ref_idx) != 0)
                return -1;
            if (n148_entropy_cavlc_write_mv(bs, mvx_q4, mvy_q4) != 0)
                return -1;
        }
    }

    if (final_mode == 0) {
        memcpy(recon_u8, pred, sizeof(recon_u8));
        store_block(recon_plane, stride, width, height,
                    bx, by, sample_stride, sample_offset, recon_u8);
        return 0;
    }

    for (i = 0; i < 16; i++)
        residual[i] = (int16_t)((int)src[i] - (int)pred[i]);

    n148_fdct_4x4(residual, coeff);
    coeff_count = n148_quantize_4x4_tuned(coeff, qzigzag, qp, final_mode == 2, sample_stride != 1 || sample_offset != 0);

    if (coeff_count <= 0) {
        if (entropy_mode == N148_ENTROPY_CABAC) {
            if (n148_cabac_write_has_residual(cabac_session, bs, 0u) != 0)
                return -1;
        } else {
            if (n148_bs_write_bits(bs, 1, 0) != 0)
                return -1;
        }

        memcpy(recon_u8, pred, sizeof(recon_u8));
    } else {
        if (entropy_mode == N148_ENTROPY_CABAC) {
            if (n148_cabac_write_has_residual(cabac_session, bs, 1u) != 0)
                return -1;
            if (n148_cabac_write_qp_delta(cabac_session, bs, 0) != 0)
                return -1;
            if (n148_cabac_write_block(cabac_session, bs, qzigzag, coeff_count) != 0)
                return -1;
        } else {
            if (n148_bs_write_bits(bs, 1, 1) != 0)
                return -1;
            if (n148_bs_write_se(bs, 0) != 0)
                return -1;
            if (n148_entropy_cavlc_write_block(bs, qzigzag, coeff_count) != 0)
                return -1;
        }

        memset(qnatural, 0, sizeof(qnatural));
        n148_quant_unscan_zigzag_4x4(qzigzag, qnatural);
        n148_dequant_4x4(qnatural, dequant, qp);
        n148_idct_4x4(dequant, spatial);

        for (i = 0; i < 16; i++)
            recon_u8[i] = clip_u8((int)pred[i] + (int)spatial[i]);
    }

    store_block(recon_plane, stride, width, height,
                bx, by, sample_stride, sample_offset, recon_u8);

    return 0;
}

static int encode_frame_slice(N148EncoderCtx* ctx,
                              const uint8_t* nv12,
                              int frame_type,
                              uint8_t* out_buf, int out_cap, int* out_size)
{
    N148BsWriter bs;
    const uint8_t* src_y = nv12;
    const uint8_t* src_uv = nv12 + ctx->width * ctx->height;
    const uint8_t* ref_y[4] = { 0 };
    const uint8_t* ref_uv[4] = { 0 };
    uint8_t* recon_y = NULL;
    uint8_t* recon_uv = NULL;
    int y_size = ctx->width * ctx->height;
    int uv_size = ctx->width * ctx->height / 2;
    int mb_cols = (ctx->width + 15) / 16;
    int mb_rows = (ctx->height + 15) / 16;
    int mb_x, mb_y, blk_x, blk_y, ch;

    {
        int ri;
        for (ri = 0; ri < ctx->ref_count && ri < ctx->max_refs; ri++) {
            ref_y[ri] = ctx->ref_y[ri];
            ref_uv[ri] = ctx->ref_uv[ri];
        }
    }

    *out_size = 0;

    recon_y = (uint8_t*)malloc((size_t)y_size);
    recon_uv = (uint8_t*)malloc((size_t)uv_size);
    if (!recon_y || !recon_uv) {
        free(recon_y);
        free(recon_uv);
        return -1;
    }

    memset(recon_y, 128, (size_t)y_size);
    memset(recon_uv, 128, (size_t)uv_size);

    N148CabacSession cabac_sess;
    int cabac_active = (ctx->entropy_mode == N148_ENTROPY_CABAC);

   
    if (ctx->use_advanced_rc) {
        ctx->qp = n148_rc_get_frame_qp(&ctx->rc_adv, frame_type);
    }

    n148_bs_writer_init(&bs, out_buf, out_cap);

    if (n148_bs_write_u16be(&bs, (uint16_t)ctx->width) != 0) goto fail;
    if (n148_bs_write_u16be(&bs, (uint16_t)ctx->height) != 0) goto fail;
    if (n148_bs_write_u8(&bs, (uint8_t)frame_type) != 0) goto fail;

    if (cabac_active) {
        if (n148_cabac_session_init_enc(&cabac_sess, frame_type, ctx->qp, 0) != 0)
            goto fail;
    }

    n148_inter_ctx_set_mv_field(&ctx->inter_ctx, &ctx->mv_field_cur);
    n148_inter_ctx_set_prev_mv_field(&ctx->inter_ctx, &ctx->mv_field_prev);

    for (mb_y = 0; mb_y < mb_rows; mb_y++) {
        for (mb_x = 0; mb_x < mb_cols; mb_x++) {
            n148_inter_ctx_set_mb_pos(&ctx->inter_ctx, mb_x, mb_y);

            for (blk_y = 0; blk_y < 4; blk_y++) {
                for (blk_x = 0; blk_x < 4; blk_x++) {
                    int bx = mb_x * 16 + blk_x * 4;
                    int by = mb_y * 16 + blk_y * 4;

                    if (bx >= ctx->width || by >= ctx->height)
                        continue;

                        if (encode_one_block(ctx,
                                             &bs,
                                             cabac_active ? &cabac_sess : NULL,
                                             src_y,
                                             recon_y,
                                             ref_y,
                                             ctx->ref_count,
                                             ctx->width,
                                             ctx->width, ctx->height,
                                             bx, by, 1, 0,
                                             ctx->qp,
                                             ctx->search_range,
                                             ((frame_type == N148_FRAME_P || frame_type == N148_FRAME_B) && ctx->ref_count > 0),
                                             ctx->entropy_mode) != 0)
                        goto fail;
                }
            }

            for (ch = 0; ch < 2; ch++) {
                for (blk_y = 0; blk_y < 2; blk_y++) {
                    for (blk_x = 0; blk_x < 2; blk_x++) {
                        int bx = mb_x * 8 + blk_x * 4;
                        int by = mb_y * 8 + blk_y * 4;
                        int cw = ctx->width / 2;
                        int chh = ctx->height / 2;

                        if (bx >= cw || by >= chh)
                            continue;

                            if (encode_one_block(ctx,
                                                 &bs,
                                                 cabac_active ? &cabac_sess : NULL,
                                                 src_uv,
                                                 recon_uv,
                                                 ref_uv,
                                                 ctx->ref_count,
                                                 ctx->width,
                                                 cw, chh,
                                                 bx, by, 2, ch,
                                                 ctx->qp,
                                                 ctx->search_range,
                                                 ((frame_type == N148_FRAME_P || frame_type == N148_FRAME_B) && ctx->ref_count > 0),
                                                 ctx->entropy_mode) != 0)
                            goto fail;
                    }
                }
            }
        }
    }

    if (cabac_active) {
        if (n148_cabac_session_finish_enc(&cabac_sess, &bs) != 0)
            goto fail;
    }

    if (n148_bs_flush(&bs) != 0)
        goto fail;

    *out_size = n148_bs_writer_bytes_written(&bs);

    if (ensure_ref_buffers(ctx, y_size, uv_size) != 0)
        goto fail;

    if (frame_type != N148_FRAME_B) {
        push_reference_frame(ctx, recon_y, recon_uv, y_size, uv_size);

        /* Swap MV fields for temporal prediction */
        {
            N148MVField temp = ctx->mv_field_prev;
            ctx->mv_field_prev = ctx->mv_field_cur;
            ctx->mv_field_cur = temp;
            n148_mv_field_clear(&ctx->mv_field_cur);
        }
    }

    free(recon_y);
    free(recon_uv);
    return 0;

fail:
    free(recon_y);
    free(recon_uv);
    return -1;
}

static int build_frame_header_payload(const N148EncoderCtx* ctx,
                                      uint32_t frame_number,
                                      int frame_type,
                                      int64_t pts_hns,
                                      int64_t dts_hns,
                                      int frame_data_size,
                                      uint8_t* out, int out_cap, int* out_size)
{
    N148BsWriter bs;

    n148_bs_writer_init(&bs, out, out_cap);

    if (n148_bs_write_u8(&bs, (uint8_t)frame_type) != 0) return -1;
    if (n148_bs_write_u32be(&bs, frame_number) != 0) return -1;
    if (n148_bs_write_i64be(&bs, pts_hns) != 0) return -1;
    if (n148_bs_write_i64be(&bs, dts_hns) != 0) return -1;
    if (n148_bs_write_u8(&bs, (uint8_t)ctx->qp) != 0) return -1;
    if (n148_bs_write_u16be(&bs, 1) != 0) return -1;
    if (n148_bs_write_u8(&bs, (uint8_t)ctx->ref_count) != 0) return -1;
    if (n148_bs_write_u32be(&bs, (uint32_t)frame_data_size) != 0) return -1;
    if (n148_bs_flush(&bs) != 0) return -1;

    *out_size = n148_bs_writer_bytes_written(&bs);
    return 0;
}

static int append_raw_nal(uint8_t* out, int out_cap, int* pos,
                          int nal_type,
                          const uint8_t* payload, int payload_size)
{
    int i, zeros = 0;
    int worst = 4 + payload_size + payload_size / 2 + 4;

    if (*pos + worst > out_cap)
        return -1;

    out[(*pos)++] = 0x00;
    out[(*pos)++] = 0x00;
    out[(*pos)++] = 0x01;
    out[(*pos)++] = (uint8_t)(nal_type << 4);

   
    zeros = 0;
    for (i = 0; i < payload_size; i++) {
        if (zeros == 2 && payload[i] <= 3) {
            if (*pos >= out_cap) return -1;
            out[(*pos)++] = 0x03;
            zeros = 0;
        }
        if (*pos >= out_cap) return -1;
        out[(*pos)++] = payload[i];
        if (payload[i] == 0)
            zeros++;
        else
            zeros = 0;
    }

    return 0;
}

static int n148_encode_frame_packet(N148EncoderCtx* ctx,
                                    const N148ReorderFrame* in_frame)
{

    int frame_type;
    int64_t pts_hns;
    int64_t dts_hns;
    const uint8_t* nv12;
    int nv12_size;

    if (!ctx || !in_frame)
        return -1;

    nv12 = in_frame->nv12;
    nv12_size = in_frame->nv12_size;

    if (in_frame->planned_frame_type == N148_REORDER_FRAME_I) {
        frame_type = N148_FRAME_I;
    } else if (in_frame->planned_frame_type == N148_REORDER_FRAME_B) {
        frame_type = N148_FRAME_B;
    } else if (in_frame->planned_frame_type == N148_REORDER_FRAME_P) {
        frame_type = N148_FRAME_P;
    } else {
        frame_type = ((in_frame->display_index % (uint32_t)ctx->keyint) == 0 ||
                      in_frame->force_keyframe ||
                      !(ctx->ref_count > 0))
            ? N148_FRAME_I
            : N148_FRAME_P;
    }

    pts_hns = in_frame->pts_hns;
    dts_hns = ctx->next_dts_hns;

    if (frame_type == N148_FRAME_B && ctx->max_bframes <= 0)
        frame_type = N148_FRAME_P;

    uint8_t* slice_payload = NULL;
    uint8_t* frame_hdr = NULL;
    uint8_t* raw_bs = NULL;
    uint8_t* pkt = NULL;
    int slice_cap;
    int slice_size = 0;
    int frame_hdr_size = 0;
    int raw_cap;
    int raw_size = 0;
    int pkt_cap;
    int pkt_size = 0;
    int ref_count_for_hdr;

    if (nv12_size != ctx->width * ctx->height * 3 / 2)
        return -1;

    slice_cap = ctx->width * ctx->height * 12 + 1024;
    raw_cap = slice_cap * 2 + 512 + N148_SEQ_HEADER_SIZE;
    pkt_cap = raw_cap + 64;

    slice_payload = (uint8_t*)malloc((size_t)slice_cap);
    frame_hdr = (uint8_t*)malloc(128);
    raw_bs = (uint8_t*)malloc((size_t)raw_cap);
    pkt = (uint8_t*)malloc((size_t)pkt_cap);

    if (!slice_payload || !frame_hdr || !raw_bs || !pkt)
        goto fail;

    ref_count_for_hdr = ctx->ref_count;

    if (encode_frame_slice(ctx, nv12, frame_type, slice_payload, slice_cap, &slice_size) != 0)
        goto fail;

    {
        int saved = ctx->ref_count;
        ctx->ref_count = ref_count_for_hdr;
        if (build_frame_header_payload(ctx, in_frame->display_index,
                                       frame_type,
                                       pts_hns,
                                       dts_hns,
                                       slice_size,
                                       frame_hdr, 128, &frame_hdr_size) != 0) {
            ctx->ref_count = saved;
            goto fail;
        }
        ctx->ref_count = saved;
    }

    if (ctx->frame_number == 0) {
        if (append_raw_nal(raw_bs, raw_cap, &raw_size,
                           N148_NAL_SEQ_HDR,
                           ctx->codec_private, ctx->codec_private_size) != 0)
            goto fail;
    }

    if (append_raw_nal(raw_bs, raw_cap, &raw_size,
                       N148_NAL_FRM_HDR, frame_hdr, frame_hdr_size) != 0)
        goto fail;

    if (append_raw_nal(raw_bs, raw_cap, &raw_size,
                (frame_type == N148_FRAME_I) ? N148_NAL_IDR : N148_NAL_SLICE,
                slice_payload, slice_size) != 0)
        goto fail;

    if (n148_packetize(raw_bs, raw_size, pkt, pkt_cap, &pkt_size) != 0 || pkt_size <= 0)
        goto fail;

        if (n148_queue_push_packet(ctx,
                               pkt,
                               pkt_size,
                               (frame_type == N148_FRAME_I),
                               pts_hns,
                               dts_hns,
                               ctx->frame_duration_hns) != 0)
        goto fail;

    pkt = NULL;

   
    if (ctx->use_advanced_rc) {
        n148_rc_update(&ctx->rc_adv, frame_type,
                       pkt_size * 8, (double)(slice_size));
    }

    ctx->next_dts_hns += in_frame->duration_hns;
    ctx->frame_number++;

    free(slice_payload);
    free(frame_hdr);
    free(raw_bs);
    return 0;

fail:
    free(slice_payload);
    free(frame_hdr);
    free(raw_bs);
    free(pkt);
    return -1;
}

static int n148_emit_pending(N148EncoderCtx* ctx, ness_packet_callback cb, void* userdata)
{
    NessEncodedPacket pkt;
    int pop_ret;
    int ret;

    if (!ctx || !cb)
        return -1;

    pop_ret = n148_queue_pop_packet(ctx, &pkt);
    if (pop_ret == 1)
        return 0;
    if (pop_ret != 0)
        return -1;

    ret = cb(userdata, &pkt);

    free(pkt.data);
    pkt.data = NULL;

    return ret;
}

static int n148_process_available(N148EncoderCtx* ctx,
                                  int flushing,
                                  ness_packet_callback cb,
                                  void* userdata)
{
    int ret;

    if (!ctx || !cb)
        return -1;

    for (;;) {
        while (ctx->out_queue_count > 0) {
            ret = n148_emit_pending(ctx, cb, userdata);
            if (ret != 0)
                return ret;
        }

        {
            N148ReorderFrame frame;
            int pop_ret;

            memset(&frame, 0, sizeof(frame));

            pop_ret = flushing
                ? n148_reorder_flush_one(&ctx->reorder_queue, &frame)
                : n148_reorder_pop_ready(&ctx->reorder_queue, &frame);

            if (pop_ret == 1)
                return 0;
            if (pop_ret != 0)
                return -1;

            ret = n148_encode_frame_packet(ctx, &frame);
            n148_reorder_release_frame(&frame);

            if (ret != 0)
                return -1;
        }
    }
}

static int n148_create_wrapper(void** out, int width, int height, int fps, int bitrate_kbps)
{
    N148EncoderCtx* ctx;
    N148SeqHeader hdr;
    N148RateControlBasic rc;

    if (!out || width <= 0 || height <= 0 || fps <= 0 || bitrate_kbps <= 0)
        return -1;

    ctx = (N148EncoderCtx*)calloc(1, sizeof(N148EncoderCtx));
    if (!ctx)
        return -1;

    ctx->width = width;
    ctx->height = height;
    ctx->fps = fps;
    ctx->bitrate_kbps = bitrate_kbps;
    ctx->frame_duration_hns = 10000000LL / fps;
    ctx->next_pts_hns = 0;
    ctx->next_dts_hns = 0;
    ctx->frame_number = 0;

    if (n148_rc_basic_init(&rc, width, height, fps, bitrate_kbps) != 0) {
        free(ctx);
        return -1;
    }

    ctx->qp = n148_rc_basic_get_qp(&rc);

    ctx->profile = N148_PROFILE_MAIN;
    ctx->entropy_mode = N148_ENTROPY_CAVLC;

    ctx->keyint = N148_GOP_KEYINT_DEFAULT;
    ctx->search_range = 8;
    ctx->ref_count = 0;
    ctx->max_refs = 1;
    ctx->enable_qpel = 1;

    ctx->max_bframes = 0;
    ctx->reorder_delay = 0;

    N148_ENC_LOG("create: %dx%d fps=%d bitrate=%dk profile=%d entropy=%d",
                width, height, fps, bitrate_kbps,
                ctx->profile, ctx->entropy_mode);

    n148_seq_header_defaults(&hdr, width, height, fps,
                            ctx->profile, ctx->entropy_mode);
    hdr.max_ref_frames = (uint8_t)ctx->max_refs;
    hdr.max_reorder_depth = (uint8_t)ctx->reorder_delay;

    if (n148_seq_header_serialize(&hdr, ctx->codec_private, sizeof(ctx->codec_private)) != N148_SEQ_HEADER_SIZE) {
        free(ctx);
        return -1;
    }

    ctx->codec_private_size = N148_SEQ_HEADER_SIZE;

    ctx->out_queue_head = 0;
    ctx->out_queue_tail = 0;
    ctx->out_queue_count = 0;

    n148_reorder_init(&ctx->reorder_queue);
    n148_reorder_set_max_bframes(&ctx->reorder_queue, ctx->max_bframes);

    memset(ctx->ref_y, 0, sizeof(ctx->ref_y));
    memset(ctx->ref_uv, 0, sizeof(ctx->ref_uv));

   
    ctx->use_advanced_rc = 0;
    if (bitrate_kbps > 0) {
        n148_rc_init(&ctx->rc_adv, N148_RC_ABR,
                     width, height, fps, bitrate_kbps, ctx->qp);
        ctx->use_advanced_rc = 1;
        N148_ENC_LOG("advanced RC enabled: ABR target=%dk qp_base=%d",
                     bitrate_kbps, ctx->qp);
    }

   
    {
        char errbuf[256] = {0};
        int prof_id = (ctx->entropy_mode == N148_ENTROPY_CABAC)
            ? N148_PROF_EPIC : N148_PROF_MAIN;
        if (n148_profile_validate(prof_id,
                ctx->max_bframes > 0,
                ctx->entropy_mode == N148_ENTROPY_CABAC,
                ctx->max_refs,
                16,
                ctx->enable_qpel,
                0, 
                ctx->reorder_delay,
                errbuf, sizeof(errbuf)) != 0) {
            N148_ENC_LOG("WARN: profile violation at create: %s", errbuf);
        }
    }

    ctx->use_enhanced_me = 1;
    {
        int mb_w = (width + 15) / 16;
        int mb_h = (height + 15) / 16;
        n148_mv_field_alloc(&ctx->mv_field_cur, mb_w, mb_h);
        n148_mv_field_alloc(&ctx->mv_field_prev, mb_w, mb_h);
        n148_inter_ctx_init(&ctx->inter_ctx);
        ctx->inter_ctx.me_config.search_range = ctx->search_range;
    }

    *out = ctx;
    return 0;
}

static int n148_submit_frame_wrapper(void* enc, const uint8_t* nv12, int nv12_size)
{
    N148EncoderCtx* ctx = (N148EncoderCtx*)enc;

    if (!ctx || !nv12)
        return -1;

    if (ctx->out_queue_count >= (int)(sizeof(ctx->out_queue) / sizeof(ctx->out_queue[0])))
        return -1;

    if (n148_reorder_push_copy(&ctx->reorder_queue,
                               nv12,
                               nv12_size,
                               ctx->next_pts_hns,
                               ctx->frame_duration_hns,
                               0) != 0)
        return -1;

    ctx->next_pts_hns += ctx->frame_duration_hns;
    return 0;
}

static int n148_receive_packets_wrapper(void* enc, ness_packet_callback cb, void* userdata)
{
    return n148_process_available((N148EncoderCtx*)enc, 0, cb, userdata);
}

static int n148_drain_wrapper(void* enc, ness_packet_callback cb, void* userdata)
{
    return n148_process_available((N148EncoderCtx*)enc, 1, cb, userdata);
}

static int n148_get_codec_private_wrapper(void* enc, uint8_t** out, int* out_size)
{
    N148EncoderCtx* ctx = (N148EncoderCtx*)enc;
    uint8_t* cp;

    if (!ctx || !out || !out_size || ctx->codec_private_size <= 0)
        return -1;

    cp = (uint8_t*)malloc((size_t)ctx->codec_private_size);
    if (!cp)
        return -1;

    memcpy(cp, ctx->codec_private, (size_t)ctx->codec_private_size);
    *out = cp;
    *out_size = ctx->codec_private_size;
    return 0;
}

int n148_encoder_set_profile_entropy_for_tests(void* enc, int profile, int entropy_mode)
{
    N148EncoderCtx* ctx = (N148EncoderCtx*)enc;
    N148SeqHeader hdr;

    if (!ctx)
        return -1;

    if (profile != N148_PROFILE_MAIN && profile != N148_PROFILE_EPIC)
        return -1;

    if (entropy_mode != N148_ENTROPY_CAVLC && entropy_mode != N148_ENTROPY_CABAC)
        return -1;

    ctx->profile = profile;
    ctx->entropy_mode = entropy_mode;

    N148_ENC_LOG("set_profile_entropy_for_tests: profile=%d entropy=%d",
                 ctx->profile, ctx->entropy_mode);

    n148_seq_header_defaults(&hdr, ctx->width, ctx->height, ctx->fps,
                             ctx->profile, ctx->entropy_mode);
    hdr.max_ref_frames = (uint8_t)ctx->max_refs;
    hdr.max_reorder_depth = (uint8_t)ctx->reorder_delay;

    if (n148_seq_header_serialize(&hdr,
                                  ctx->codec_private,
                                  sizeof(ctx->codec_private)) != N148_SEQ_HEADER_SIZE)
        return -1;

    ctx->codec_private_size = N148_SEQ_HEADER_SIZE;

    N148_ENC_LOG("codec_private rebuilt: size=%d profile=%d entropy=%d refs=%d reorder=%d",
                 ctx->codec_private_size,
                 ctx->profile,
                 ctx->entropy_mode,
                 ctx->max_refs,
                 ctx->reorder_delay);

   
    {
        char errbuf[256] = {0};
        int prof_id = (ctx->entropy_mode == N148_ENTROPY_CABAC)
            ? N148_PROF_EPIC : N148_PROF_MAIN;
        if (n148_profile_validate(prof_id,
                ctx->max_bframes > 0,
                ctx->entropy_mode == N148_ENTROPY_CABAC,
                ctx->max_refs,
                16,
                ctx->enable_qpel,
                0,
                ctx->reorder_delay,
                errbuf, sizeof(errbuf)) != 0) {
            N148_ENC_LOG("WARN: profile violation after set_profile: %s", errbuf);
        }
    }

    return 0;
}

static void n148_destroy_wrapper(void* enc)
{
    N148EncoderCtx* ctx = (N148EncoderCtx*)enc;
    int i;

    if (!ctx) return;

    for (i = 0; i < (int)(sizeof(ctx->out_queue) / sizeof(ctx->out_queue[0])); i++) {
        free(ctx->out_queue[i].data);
        ctx->out_queue[i].data = NULL;
    }

    n148_reorder_free(&ctx->reorder_queue);

    for (i = 0; i < ctx->max_refs; i++) {
        free(ctx->ref_y[i]);
        free(ctx->ref_uv[i]);
    }

    n148_mv_field_free(&ctx->mv_field_cur);
    n148_mv_field_free(&ctx->mv_field_prev);

    free(ctx);
}

const NessEncoderVtable g_n148_encoder_vtable = {
    "N148",
    N148_CODEC_ID,
    0,
    n148_create_wrapper,
    n148_submit_frame_wrapper,
    n148_receive_packets_wrapper,
    n148_drain_wrapper,
    n148_get_codec_private_wrapper,
    n148_destroy_wrapper
};