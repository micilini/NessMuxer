#include "n148_decoder.h"
#include "n148_parser.h"
#include "n148_frame_recon.h"
#include "n148_motion.h"
#include "n148_dpb.h"
#include "../../encoder/n148/n148_quant.h"
#include "../../codec/n148/n148_spec.h"
#include "../../codec/n148/n148_codec_private.h"
#include "../../codec/n148/n148_codec.h"
#include "../../codec/n148/n148_bitstream.h"

#include <stdlib.h>
#include <string.h>

#define N148_MAX_NALS 64

struct N148Decoder {
    N148SeqHeader   seq_hdr;
    int             seq_hdr_valid;
    int             width, height;
    int             stride;

    uint8_t*        frame_buf;
    int             frame_buf_size;

    uint8_t*        pending_anchor_buf;
    int64_t         pending_anchor_pts;
    int             pending_anchor_frame_type;
    int             pending_anchor_valid;

    N148ReferenceFrames refs;
    int ref_ready;
};

static uint8_t clip_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void store_block_direct(uint8_t* plane, int stride,
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
            if (px < width && py < height) {
                plane[py * stride + px * sample_stride + sample_offset] = in[y * 4 + x];
            }
        }
    }
}

static void load_pred_block(uint8_t* plane, int stride,
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

static int reconstruct_inter_or_intra_block(N148BsReader* bs,
                                            uint8_t* dst_plane,
                                            const uint8_t* ref_plane,
                                            int stride,
                                            int width, int height,
                                            int bx, int by,
                                            int sample_stride, int sample_offset,
                                            int qp,
                                            int block_mode)
{
    uint8_t pred[16];
    uint8_t recon_u8[16];
    int16_t qzigzag[16];
    int16_t qnatural[16];
    int16_t dequant[16];
    int16_t spatial[16];
    uint32_t has_residual = 0;
    int i;

    memset(pred, 128, sizeof(pred));
    memset(recon_u8, 128, sizeof(recon_u8));
    memset(qzigzag, 0, sizeof(qzigzag));
    memset(qnatural, 0, sizeof(qnatural));
    memset(dequant, 0, sizeof(dequant));
    memset(spatial, 0, sizeof(spatial));

    if (block_mode == 2) {
        uint32_t intra_mode = 0;
        uint8_t above[4] = {128, 128, 128, 128};
        uint8_t left[4]  = {128, 128, 128, 128};
        int has_above = (by > 0);
        int has_left  = (bx > 0);
        int k;

        if (n148_bs_read_ue(bs, &intra_mode) != 0)
            return -1;

        if (has_above) {
            for (k = 0; k < 4; k++) {
                int px = bx + k;
                if (px < width)
                    above[k] = dst_plane[(by - 1) * stride + px * sample_stride + sample_offset];
                else if (k > 0)
                    above[k] = above[k - 1];
            }
        }

        if (has_left) {
            for (k = 0; k < 4; k++) {
                int py = by + k;
                if (py < height)
                    left[k] = dst_plane[py * stride + (bx - 1) * sample_stride + sample_offset];
                else if (k > 0)
                    left[k] = left[k - 1];
            }
        }

        n148_intra_pred_4x4(pred, 4, (int)intra_mode, above, left, has_above, has_left);
    } else {
        uint32_t ref_idx = 0;
        int32_t mvx_q4 = 0;
        int32_t mvy_q4 = 0;

        if (!ref_plane)
            return -1;

        if (n148_bs_read_ue(bs, &ref_idx) != 0)
            return -1;
        if (n148_bs_read_se(bs, &mvx_q4) != 0)
            return -1;
        if (n148_bs_read_se(bs, &mvy_q4) != 0)
            return -1;

        n148_mc_copy_qpel_4x4(dst_plane, stride,
                              ref_plane, stride,
                              width, height,
                              bx, by,
                              mvx_q4, mvy_q4,
                              sample_stride, sample_offset);

        load_pred_block(dst_plane, stride, width, height,
                        bx, by, sample_stride, sample_offset, pred);

        if (block_mode == 0) {
            store_block_direct(dst_plane, stride, width, height,
                               bx, by, sample_stride, sample_offset, pred);
            return 0;
        }
    }

    if (n148_bs_read_bits(bs, 1, &has_residual) != 0)
        return -1;

    if (!has_residual) {
        memcpy(recon_u8, pred, sizeof(recon_u8));
        store_block_direct(dst_plane, stride, width, height,
                           bx, by, sample_stride, sample_offset, recon_u8);
        return 0;
    }

    {
        int32_t qp_delta = 0;
        uint32_t coeff_count = 0;

        if (n148_bs_read_se(bs, &qp_delta) != 0)
            return -1;
        if (n148_bs_read_ue(bs, &coeff_count) != 0)
            return -1;

        if (coeff_count > 16)
            return -1;

        for (i = 0; i < (int)coeff_count; i++) {
            int32_t level = 0;
            if (n148_bs_read_se(bs, &level) != 0)
                return -1;
            qzigzag[i] = (int16_t)level;
        }
    }

    n148_quant_unscan_zigzag_4x4(qzigzag, qnatural);
    n148_dequant_4x4(qnatural, dequant, qp);
    n148_idct_4x4(dequant, spatial);

    for (i = 0; i < 16; i++)
        recon_u8[i] = clip_u8((int)pred[i] + (int)spatial[i]);

    store_block_direct(dst_plane, stride, width, height,
                       bx, by, sample_stride, sample_offset, recon_u8);

    return 0;
}

int n148_decoder_create(N148Decoder** out)
{
    N148Decoder* dec;

    if (!out) return -1;
    *out = NULL;

    dec = (N148Decoder*)calloc(1, sizeof(N148Decoder));
    if (!dec) return -1;

    dec->ref_ready = 0;
    dec->pending_anchor_buf = NULL;
    dec->pending_anchor_pts = 0;
    dec->pending_anchor_frame_type = 0;
    dec->pending_anchor_valid = 0;
    memset(&dec->refs, 0, sizeof(dec->refs));

    *out = dec;
    return 0;
}

int n148_decoder_init_from_seq_header(N148Decoder* dec, const uint8_t* data, int size)
{
    if (!dec || !data) return -1;

    if (n148_seq_header_parse(data, size, &dec->seq_hdr) != 0)
        return -1;

    dec->width  = dec->seq_hdr.width;
    dec->height = dec->seq_hdr.height;
    dec->stride = dec->width;
    dec->seq_hdr_valid = 1;

    if (dec->frame_buf) {
        free(dec->frame_buf);
        dec->frame_buf = NULL;
    }

    if (dec->pending_anchor_buf) {
        free(dec->pending_anchor_buf);
        dec->pending_anchor_buf = NULL;
    }

    dec->frame_buf_size = dec->width * dec->height * 3 / 2;

    dec->frame_buf = (uint8_t*)malloc((size_t)dec->frame_buf_size);
    if (!dec->frame_buf) return -1;

    dec->pending_anchor_buf = (uint8_t*)malloc((size_t)dec->frame_buf_size);
    if (!dec->pending_anchor_buf) return -1;

    dec->pending_anchor_pts = 0;
    dec->pending_anchor_frame_type = 0;
    dec->pending_anchor_valid = 0;

    if (!dec->refs.y[0] || !dec->refs.uv[0]) {
        if (n148_refs_init(&dec->refs, dec->width, dec->height) != 0)
            return -1;
    }

    return 0;
}

int n148_decoder_decode(N148Decoder* dec, const uint8_t* data, int size,
                        N148DecodedFrame* out_frame)
{
    N148NalUnit nals[N148_MAX_NALS];
    int nal_count = 0;
    N148FrameHeader frm_hdr;
    int frm_hdr_found = 0;
    int slice_found = 0;
    int i;
    uint8_t* clean_data = NULL;
    int clean_size = 0;
    int had_ref_ready_before = 0;

    if (!dec || !data || size <= 0 || !out_frame)
        return -1;

    memset(out_frame, 0, sizeof(*out_frame));
    memset(&frm_hdr, 0, sizeof(frm_hdr));

   
    if (size >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
        clean_data = (uint8_t*)malloc((size_t)size);
        if (!clean_data)
            return -1;
        clean_size = n148_remove_epb(data, size, clean_data);
        n148_find_nal_units(clean_data, clean_size, nals, N148_MAX_NALS, &nal_count);
    } else {
        n148_find_nal_units_lp(data, size, nals, N148_MAX_NALS, &nal_count);
    }

    if (nal_count == 0)
        goto fail;

    for (i = 0; i < nal_count; i++) {
        switch (nals[i].nal_type) {
        case N148_NAL_SEQ_HDR:
            if (!dec->seq_hdr_valid) {
                if (n148_decoder_init_from_seq_header(dec, nals[i].payload,
                                                      nals[i].payload_size) != 0)
                    goto fail;
            }
            break;

        case N148_NAL_FRM_HDR:
            if (n148_parse_frame_header(nals[i].payload, nals[i].payload_size, &frm_hdr) != 0)
                goto fail;
            frm_hdr_found = 1;
            break;

        case N148_NAL_IDR:
        case N148_NAL_SLICE: {
            N148BsReader bs;
            uint16_t coded_width = 0;
            uint16_t coded_height = 0;
            uint8_t slice_frame_type = 0;
            const uint8_t* ref_y_planes[4] = { 0 };
            const uint8_t* ref_uv_planes[4] = { 0 };
            uint8_t* dst_y;
            uint8_t* dst_uv;
            int mb_x, mb_y, blk_x, blk_y, ch;

            if (!dec->seq_hdr_valid || !frm_hdr_found)
                goto fail;

            memset(dec->frame_buf, 0, (size_t)dec->frame_buf_size);

            dst_y = dec->frame_buf;
            dst_uv = dec->frame_buf + dec->width * dec->height;

            n148_bs_reader_init(&bs, nals[i].payload, nals[i].payload_size);

            if (n148_bs_read_u16be(&bs, &coded_width) != 0) goto fail;
            if (n148_bs_read_u16be(&bs, &coded_height) != 0) goto fail;
            if (n148_bs_read_u8(&bs, &slice_frame_type) != 0) goto fail;

            if ((int)coded_width != dec->width || (int)coded_height != dec->height)
                goto fail;

            if (slice_frame_type != frm_hdr.frame_type)
                goto fail;

            if ((slice_frame_type == N148_FRAME_P || slice_frame_type == N148_FRAME_B) && !dec->ref_ready)
                goto fail;

            if (slice_frame_type == N148_FRAME_P || slice_frame_type == N148_FRAME_B) {
                int ri;
                for (ri = 0; ri < dec->refs.count && ri < 4; ri++) {
                    if (n148_refs_get_planes(&dec->refs, ri, &ref_y_planes[ri], &ref_uv_planes[ri]) != 0)
                        goto fail;
                }
            }

            for (mb_y = 0; mb_y < (dec->height + 15) / 16; mb_y++) {
                for (mb_x = 0; mb_x < (dec->width + 15) / 16; mb_x++) {
                    for (blk_y = 0; blk_y < 4; blk_y++) {
                        for (blk_x = 0; blk_x < 4; blk_x++) {
                            int bx = mb_x * 16 + blk_x * 4;
                            int by = mb_y * 16 + blk_y * 4;
                            uint32_t block_mode = 0;

                            if (bx >= dec->width || by >= dec->height)
                                continue;

                            if (n148_bs_read_ue(&bs, &block_mode) != 0)
                                goto fail;

                            if (block_mode > 2)
                                goto fail;

                            if (block_mode <= 1) {
                                uint32_t peek_ref_idx = 0;
                                int save_bitpos = bs.bit_pos;
                                int save_bytepos = bs.byte_pos;

                                if (n148_bs_read_ue(&bs, &peek_ref_idx) != 0)
                                    goto fail;

                                bs.byte_pos = save_bytepos;
                                bs.bit_pos = save_bitpos;

                                if (peek_ref_idx >= (uint32_t)dec->refs.count)
                                    goto fail;

                                if (reconstruct_inter_or_intra_block(
                                        &bs,
                                        dst_y,
                                        ref_y_planes[peek_ref_idx],
                                        dec->stride,
                                        dec->width, dec->height,
                                        bx, by,
                                        1, 0,
                                        frm_hdr.qp_base,
                                        (int)block_mode) != 0)
                                    goto fail;
                            } else {
                                if (reconstruct_inter_or_intra_block(
                                        &bs,
                                        dst_y,
                                        NULL,
                                        dec->stride,
                                        dec->width, dec->height,
                                        bx, by,
                                        1, 0,
                                        frm_hdr.qp_base,
                                        (int)block_mode) != 0)
                                    goto fail;
                            }
                        }
                    }

                    for (ch = 0; ch < 2; ch++) {
                        int cw = dec->width / 2;
                        int chh = dec->height / 2;

                        for (blk_y = 0; blk_y < 2; blk_y++) {
                            for (blk_x = 0; blk_x < 2; blk_x++) {
                                int bx = mb_x * 8 + blk_x * 4;
                                int by = mb_y * 8 + blk_y * 4;
                                uint32_t block_mode = 0;

                                if (bx >= cw || by >= chh)
                                    continue;

                                if (n148_bs_read_ue(&bs, &block_mode) != 0)
                                    goto fail;

                                if (block_mode > 2)
                                    goto fail;

                                if (block_mode <= 1) {
                                    uint32_t peek_ref_idx = 0;
                                    int save_bitpos = bs.bit_pos;
                                    int save_bytepos = bs.byte_pos;

                                    if (n148_bs_read_ue(&bs, &peek_ref_idx) != 0)
                                        goto fail;

                                    bs.byte_pos = save_bytepos;
                                    bs.bit_pos = save_bitpos;

                                    if (peek_ref_idx >= (uint32_t)dec->refs.count)
                                        goto fail;

                                    if (reconstruct_inter_or_intra_block(
                                            &bs,
                                            dst_uv,
                                            ref_uv_planes[peek_ref_idx],
                                            dec->stride,
                                            cw, chh,
                                            bx, by,
                                            2, ch,
                                            frm_hdr.qp_base,
                                            (int)block_mode) != 0)
                                        goto fail;
                                } else {
                                    if (reconstruct_inter_or_intra_block(
                                            &bs,
                                            dst_uv,
                                            NULL,
                                            dec->stride,
                                            cw, chh,
                                            bx, by,
                                            2, ch,
                                            frm_hdr.qp_base,
                                            (int)block_mode) != 0)
                                        goto fail;
                                }
                            }
                        }
                    }
                }
            }

            slice_found = 1;
            break;
        }

        default:
            break;
        }
    }

    had_ref_ready_before = dec->ref_ready;

    if (!slice_found)
        goto fail;

        if (frm_hdr.frame_type != N148_FRAME_B) {
        if (n148_refs_store_nv12(&dec->refs,
                                 dec->frame_buf,
                                 dec->frame_buf + dec->width * dec->height,
                                 dec->width, dec->height) != 0)
            goto fail;

        dec->ref_ready = 1;
    }

    if (frm_hdr.frame_type == N148_FRAME_B) {
        out_frame->planes[0]  = dec->frame_buf;
        out_frame->planes[1]  = dec->frame_buf + dec->width * dec->height;
        out_frame->planes[2]  = NULL;
        out_frame->strides[0] = dec->stride;
        out_frame->strides[1] = dec->stride;
        out_frame->strides[2] = 0;
        out_frame->width      = dec->width;
        out_frame->height     = dec->height;
        out_frame->pts        = frm_hdr.pts;
        out_frame->frame_type = frm_hdr.frame_type;

        free(clean_data);
        return 0;
    }

    if (frm_hdr.frame_type == N148_FRAME_I && !had_ref_ready_before && !dec->pending_anchor_valid) {
        out_frame->planes[0]  = dec->frame_buf;
        out_frame->planes[1]  = dec->frame_buf + dec->width * dec->height;
        out_frame->planes[2]  = NULL;
        out_frame->strides[0] = dec->stride;
        out_frame->strides[1] = dec->stride;
        out_frame->strides[2] = 0;
        out_frame->width      = dec->width;
        out_frame->height     = dec->height;
        out_frame->pts        = frm_hdr.pts;
        out_frame->frame_type = frm_hdr.frame_type;

        free(clean_data);
        return 0;
    }

    if (dec->pending_anchor_valid) {
        out_frame->planes[0]  = dec->pending_anchor_buf;
        out_frame->planes[1]  = dec->pending_anchor_buf + dec->width * dec->height;
        out_frame->planes[2]  = NULL;
        out_frame->strides[0] = dec->stride;
        out_frame->strides[1] = dec->stride;
        out_frame->strides[2] = 0;
        out_frame->width      = dec->width;
        out_frame->height     = dec->height;
        out_frame->pts        = dec->pending_anchor_pts;
        out_frame->frame_type = dec->pending_anchor_frame_type;
    } else {
        memset(out_frame, 0, sizeof(*out_frame));
    }

    memcpy(dec->pending_anchor_buf, dec->frame_buf, (size_t)dec->frame_buf_size);
    dec->pending_anchor_pts = frm_hdr.pts;
    dec->pending_anchor_frame_type = frm_hdr.frame_type;

    if (dec->pending_anchor_valid) {
        free(clean_data);
        return 0;
    }

    dec->pending_anchor_valid = 1;
    free(clean_data);
    return 1;

fail:
    free(clean_data);
    return -1;
}

int n148_decoder_flush(N148Decoder* dec, N148DecodedFrame* out_frame)
{
    if (!dec || !out_frame)
        return -1;

    memset(out_frame, 0, sizeof(*out_frame));

    if (!dec->pending_anchor_valid)
        return 1;

    out_frame->planes[0]  = dec->pending_anchor_buf;
    out_frame->planes[1]  = dec->pending_anchor_buf + dec->width * dec->height;
    out_frame->planes[2]  = NULL;
    out_frame->strides[0] = dec->stride;
    out_frame->strides[1] = dec->stride;
    out_frame->strides[2] = 0;
    out_frame->width      = dec->width;
    out_frame->height     = dec->height;
    out_frame->pts        = dec->pending_anchor_pts;
    out_frame->frame_type = dec->pending_anchor_frame_type;

    dec->pending_anchor_valid = 0;
    dec->pending_anchor_pts = 0;
    dec->pending_anchor_frame_type = 0;

    return 0;
}

void n148_decoder_destroy(N148Decoder* dec)
{
    if (!dec) return;
    if (dec->frame_buf) free(dec->frame_buf);
    if (dec->pending_anchor_buf) free(dec->pending_anchor_buf);
    n148_refs_free(&dec->refs);
    free(dec);
}