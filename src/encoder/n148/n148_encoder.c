#include "n148_encoder.h"
#include "n148_intra.h"
#include "n148_transform.h"
#include "n148_quant.h"
#include "n148_entropy_enc_cavlc.h"
#include "n148_ratecontrol_basic.h"

#include "../../codec/n148/n148_spec.h"
#include "../../codec/n148/n148_bitstream.h"
#include "../../codec/n148/n148_codec.h"
#include "../../codec/n148/n148_codec_private.h"
#include "../../decoder/n148/n148_frame_recon.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int width;
    int height;
    int fps;
    int bitrate_kbps;

    int qp;
    int64_t frame_duration_hns;
    int64_t next_pts_hns;
    uint32_t frame_number;

    uint8_t codec_private[N148_SEQ_HEADER_SIZE];
    int codec_private_size;

    uint8_t* pending_data;
    int pending_size;
    int pending_is_keyframe;
    int64_t pending_pts_hns;
    int64_t pending_duration_hns;
} N148EncoderCtx;

static uint8_t clip_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
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

static int encode_one_block(N148BsWriter* bs,
                            const uint8_t* src_plane,
                            uint8_t* recon_plane,
                            int stride,
                            int width, int height,
                            int bx, int by,
                            int sample_stride, int sample_offset,
                            int qp)
{
    uint8_t src[16];
    uint8_t pred[16];
    uint8_t recon_u8[16];
    int16_t residual[16];
    int16_t coeff[16];
    int16_t qzigzag[16];
    int16_t qnatural[16];
    int16_t dequant[16];
    int16_t spatial[16];
    int mode;
    int coeff_count;
    int i;

    load_block(src_plane, stride, width, height,
               bx, by, sample_stride, sample_offset, src);

    mode = n148_intra_choose_mode(src_plane, recon_plane, stride,
                                  width, height,
                                  bx, by,
                                  sample_stride, sample_offset,
                                  pred);

    for (i = 0; i < 16; i++)
        residual[i] = (int16_t)((int)src[i] - (int)pred[i]);

    n148_fdct_4x4(residual, coeff);
    coeff_count = n148_quantize_4x4(coeff, qzigzag, qp);

    if (n148_bs_write_ue(bs, (uint32_t)mode) != 0)
        return -1;

    if (coeff_count <= 0) {
        if (n148_bs_write_bits(bs, 1, 0) != 0)
            return -1;

        memcpy(recon_u8, pred, sizeof(recon_u8));
    } else {
        if (n148_bs_write_bits(bs, 1, 1) != 0)
            return -1;
        if (n148_bs_write_se(bs, 0) != 0)
            return -1;
        if (n148_cavlc_write_block(bs, qzigzag, coeff_count) != 0)
            return -1;

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

static int encode_iframe_slice(N148EncoderCtx* ctx,
                               const uint8_t* nv12,
                               uint8_t* out_buf, int out_cap, int* out_size)
{
    N148BsWriter bs;
    const uint8_t* src_y = nv12;
    const uint8_t* src_uv = nv12 + ctx->width * ctx->height;
    uint8_t* recon_y = NULL;
    uint8_t* recon_uv = NULL;
    int y_size = ctx->width * ctx->height;
    int uv_size = ctx->width * ctx->height / 2;
    int mb_cols = (ctx->width + 15) / 16;
    int mb_rows = (ctx->height + 15) / 16;
    int mb_x, mb_y, blk_x, blk_y, ch;

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

    n148_bs_writer_init(&bs, out_buf, out_cap);

    if (n148_bs_write_u16be(&bs, (uint16_t)ctx->width) != 0) goto fail;
    if (n148_bs_write_u16be(&bs, (uint16_t)ctx->height) != 0) goto fail;

    for (mb_y = 0; mb_y < mb_rows; mb_y++) {
        for (mb_x = 0; mb_x < mb_cols; mb_x++) {
            for (blk_y = 0; blk_y < 4; blk_y++) {
                for (blk_x = 0; blk_x < 4; blk_x++) {
                    int bx = mb_x * 16 + blk_x * 4;
                    int by = mb_y * 16 + blk_y * 4;

                    if (bx >= ctx->width || by >= ctx->height)
                        continue;

                    if (encode_one_block(&bs, src_y, recon_y, ctx->width,
                                         ctx->width, ctx->height,
                                         bx, by, 1, 0, ctx->qp) != 0)
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

                        if (encode_one_block(&bs, src_uv, recon_uv, ctx->width,
                                             cw, chh,
                                             bx, by, 2, ch, ctx->qp) != 0)
                            goto fail;
                    }
                }
            }
        }
    }

    if (n148_bs_flush(&bs) != 0)
        goto fail;

    *out_size = n148_bs_writer_bytes_written(&bs);
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
                                      int64_t pts_hns,
                                      int frame_data_size,
                                      uint8_t* out, int out_cap, int* out_size)
{
    N148BsWriter bs;

    n148_bs_writer_init(&bs, out, out_cap);

    if (n148_bs_write_u8(&bs, N148_FRAME_I) != 0) return -1;
    if (n148_bs_write_u32be(&bs, frame_number) != 0) return -1;
    if (n148_bs_write_i64be(&bs, pts_hns) != 0) return -1;
    if (n148_bs_write_i64be(&bs, pts_hns) != 0) return -1;
    if (n148_bs_write_u8(&bs, (uint8_t)ctx->qp) != 0) return -1;
    if (n148_bs_write_u16be(&bs, 1) != 0) return -1;
    if (n148_bs_write_u8(&bs, 0) != 0) return -1;
    if (n148_bs_write_u32be(&bs, (uint32_t)frame_data_size) != 0) return -1;
    if (n148_bs_flush(&bs) != 0) return -1;

    *out_size = n148_bs_writer_bytes_written(&bs);
    return 0;
}

static int append_raw_nal(uint8_t* out, int out_cap, int* pos,
                          int nal_type,
                          const uint8_t* payload, int payload_size)
{
    if (*pos + 4 + payload_size > out_cap)
        return -1;

    out[(*pos)++] = 0x00;
    out[(*pos)++] = 0x00;
    out[(*pos)++] = 0x01;
    out[(*pos)++] = (uint8_t)(nal_type << 4);

    memcpy(out + *pos, payload, (size_t)payload_size);
    *pos += payload_size;
    return 0;
}

static int n148_encode_frame_packet(N148EncoderCtx* ctx,
                                    const uint8_t* nv12, int nv12_size)
{
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

    if (nv12_size != ctx->width * ctx->height * 3 / 2)
        return -1;

    slice_cap = ctx->width * ctx->height * 12 + 1024;
    raw_cap = slice_cap + 512 + N148_SEQ_HEADER_SIZE;
    pkt_cap = raw_cap + 64;

    slice_payload = (uint8_t*)malloc((size_t)slice_cap);
    frame_hdr = (uint8_t*)malloc(128);
    raw_bs = (uint8_t*)malloc((size_t)raw_cap);
    pkt = (uint8_t*)malloc((size_t)pkt_cap);

    if (!slice_payload || !frame_hdr || !raw_bs || !pkt)
        goto fail;

    if (encode_iframe_slice(ctx, nv12, slice_payload, slice_cap, &slice_size) != 0)
        goto fail;

    if (build_frame_header_payload(ctx, ctx->frame_number,
                                   ctx->next_pts_hns,
                                   slice_size,
                                   frame_hdr, 128, &frame_hdr_size) != 0)
        goto fail;

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
                       N148_NAL_IDR, slice_payload, slice_size) != 0)
        goto fail;

    if (n148_packetize(raw_bs, raw_size, pkt, pkt_cap, &pkt_size) != 0 || pkt_size <= 0)
        goto fail;

    free(ctx->pending_data);
    ctx->pending_data = pkt;
    ctx->pending_size = pkt_size;
    ctx->pending_is_keyframe = 1;
    ctx->pending_pts_hns = ctx->next_pts_hns;
    ctx->pending_duration_hns = ctx->frame_duration_hns;

    ctx->next_pts_hns += ctx->frame_duration_hns;
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
    int ret;

    if (!ctx || !cb)
        return -1;

    if (!ctx->pending_data || ctx->pending_size <= 0)
        return 0;

    memset(&pkt, 0, sizeof(pkt));
    pkt.data = ctx->pending_data;
    pkt.size = ctx->pending_size;
    pkt.pts_hns = ctx->pending_pts_hns;
    pkt.duration_hns = ctx->pending_duration_hns;
    pkt.is_keyframe = ctx->pending_is_keyframe;

    ret = cb(userdata, &pkt);

    free(ctx->pending_data);
    ctx->pending_data = NULL;
    ctx->pending_size = 0;
    ctx->pending_is_keyframe = 0;
    ctx->pending_pts_hns = 0;
    ctx->pending_duration_hns = 0;

    return ret;
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
    ctx->frame_number = 0;

    if (n148_rc_basic_init(&rc, width, height, fps, bitrate_kbps) != 0) {
        free(ctx);
        return -1;
    }

    ctx->qp = n148_rc_basic_get_qp(&rc);

    n148_seq_header_defaults(&hdr, width, height, fps,
                             N148_PROFILE_MAIN, N148_ENTROPY_CAVLC);

    if (n148_seq_header_serialize(&hdr, ctx->codec_private, sizeof(ctx->codec_private)) != N148_SEQ_HEADER_SIZE) {
        free(ctx);
        return -1;
    }

    ctx->codec_private_size = N148_SEQ_HEADER_SIZE;
    *out = ctx;
    return 0;
}

static int n148_submit_frame_wrapper(void* enc, const uint8_t* nv12, int nv12_size)
{
    N148EncoderCtx* ctx = (N148EncoderCtx*)enc;

    if (!ctx || !nv12)
        return -1;

    if (ctx->pending_data)
        return -1;

    return n148_encode_frame_packet(ctx, nv12, nv12_size);
}

static int n148_receive_packets_wrapper(void* enc, ness_packet_callback cb, void* userdata)
{
    return n148_emit_pending((N148EncoderCtx*)enc, cb, userdata);
}

static int n148_drain_wrapper(void* enc, ness_packet_callback cb, void* userdata)
{
    return n148_emit_pending((N148EncoderCtx*)enc, cb, userdata);
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

static void n148_destroy_wrapper(void* enc)
{
    N148EncoderCtx* ctx = (N148EncoderCtx*)enc;
    if (!ctx) return;

    free(ctx->pending_data);
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