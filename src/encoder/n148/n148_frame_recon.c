#include "n148_frame_recon.h"
#include <string.h>
#include <stdlib.h>
#include "../../common/x86/n148_pixel_sse2.h"



static const int g_zigzag_4x4[16] = {
    0,  1,  4,  8,
    5,  2,  3,  6,
    9, 12, 13, 10,
    7, 11, 14, 15
};




static const int g_quant_scale[6] = { 10, 11, 13, 14, 16, 18 };

static int qp_to_qstep(int qp)
{
    int rem = qp % 6;
    int div = qp / 6;
    return g_quant_scale[rem] << div;
}

void n148_dequant_4x4(const int16_t* levels, int16_t* coeff, int qp)
{
    int qstep = qp_to_qstep(qp);
#if N148_HAVE_SSE2
    n148_sse2_dequant_4x4_i16(levels, coeff, qstep);
#else
    int i;
    for (i = 0; i < 16; i++)
        coeff[i] = levels[i] * (int16_t)qstep;
#endif
}



static void idct_1d_4(const int16_t* in, int16_t* out)
{
    int s0 = in[0] + in[2];
    int s1 = in[0] - in[2];
    int s2 = (in[1] >> 1) - in[3];
    int s3 = in[1] + (in[3] >> 1);

    out[0] = (int16_t)(s0 + s3);
    out[1] = (int16_t)(s1 + s2);
    out[2] = (int16_t)(s1 - s2);
    out[3] = (int16_t)(s0 - s3);
}

void n148_idct_4x4(const int16_t* coeff, int16_t* out)
{
    int16_t tmp[16];
    int i;

   
    for (i = 0; i < 4; i++)
        idct_1d_4(coeff + i * 4, tmp + i * 4);

   
    {
        int16_t col[4], res[4];
        int j;
        for (j = 0; j < 4; j++) {
            col[0] = tmp[0 * 4 + j];
            col[1] = tmp[1 * 4 + j];
            col[2] = tmp[2 * 4 + j];
            col[3] = tmp[3 * 4 + j];
            idct_1d_4(col, res);
            out[0 * 4 + j] = (int16_t)((res[0] + 32) >> 6);
            out[1 * 4 + j] = (int16_t)((res[1] + 32) >> 6);
            out[2 * 4 + j] = (int16_t)((res[2] + 32) >> 6);
            out[3 * 4 + j] = (int16_t)((res[3] + 32) >> 6);
        }
    }
}



static uint8_t clip_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

void n148_intra_pred_4x4(uint8_t* dst, int stride, int mode,
                          const uint8_t* above, const uint8_t* left,
                          int has_above, int has_left)
{
    int x, y;

    switch (mode) {
    case N148_INTRA_DC: {
        int sum = 0, count = 0;
        if (has_above) { for (x = 0; x < 4; x++) { sum += above[x]; } count += 4; }
        if (has_left)  { for (y = 0; y < 4; y++) { sum += left[y]; }  count += 4; }
        {
            uint8_t dc = count > 0 ? (uint8_t)((sum + count / 2) / count) : 128;
#if N148_HAVE_SSE2
            if (stride == 4) {
                _mm_storeu_si128((__m128i*)dst, _mm_set1_epi8((char)dc));
                break;
            }
#endif
            for (y = 0; y < 4; y++)
                for (x = 0; x < 4; x++)
                    dst[y * stride + x] = dc;
        }
        break;
    }

    case N148_INTRA_HORIZONTAL:
#if N148_HAVE_SSE2
        if (stride == 4) {
            uint32_t row0 = (has_left ? left[0] : 128) * 0x01010101u;
            uint32_t row1 = (has_left ? left[1] : 128) * 0x01010101u;
            uint32_t row2 = (has_left ? left[2] : 128) * 0x01010101u;
            uint32_t row3 = (has_left ? left[3] : 128) * 0x01010101u;
            __m128i block = _mm_set_epi32((int)row3, (int)row2, (int)row1, (int)row0);
            _mm_storeu_si128((__m128i*)dst, block);
            break;
        }
#endif
        for (y = 0; y < 4; y++) {
            uint8_t val = has_left ? left[y] : 128;
            for (x = 0; x < 4; x++)
                dst[y * stride + x] = val;
        }
        break;

    case N148_INTRA_VERTICAL:
#if N148_HAVE_SSE2
        if (stride == 4) {
            uint32_t row = 0x80808080u;
            __m128i block;
            if (has_above)
                memcpy(&row, above, sizeof(uint32_t));
            block = _mm_shuffle_epi32(_mm_cvtsi32_si128((int)row), 0x00);
            _mm_storeu_si128((__m128i*)dst, block);
            break;
        }
#endif
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++)
                dst[y * stride + x] = has_above ? above[x] : 128;
        break;

    case N148_INTRA_DIAG_DL:
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++) {
                int idx = x + y + 1;
                if (has_above && idx < 8)
                    dst[y * stride + x] = above[idx < 4 ? idx : 3];
                else
                    dst[y * stride + x] = has_above ? above[3] : 128;
            }
        break;

    case N148_INTRA_DIAG_DR:
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++) {
                if (x > y) {
                    int idx = x - y - 1;
                    dst[y * stride + x] = has_above ? above[idx < 4 ? idx : 3] : 128;
                } else if (y > x) {
                    int idx = y - x - 1;
                    dst[y * stride + x] = has_left ? left[idx < 4 ? idx : 3] : 128;
                } else {
                   
                    if (has_above && has_left)
                        dst[y * stride + x] = (uint8_t)((above[0] + left[0] + 1) >> 1);
                    else if (has_above)
                        dst[y * stride + x] = above[0];
                    else if (has_left)
                        dst[y * stride + x] = left[0];
                    else
                        dst[y * stride + x] = 128;
                }
            }
        break;

    case N148_INTRA_PLANAR: {
       
        uint8_t tr = has_above ? above[3] : 128; 
        uint8_t bl = has_left ? left[3] : 128;    
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++) {
                int h = has_above ? above[x] : 128;
                int v = has_left ? left[y] : 128;
                int pred = (h * (3 - y) + bl * (y + 1) +
                            v * (3 - x) + tr * (x + 1) + 4) >> 3;
                dst[y * stride + x] = clip_u8(pred);
            }
        break;
    }

    default:
       
        for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++)
                dst[y * stride + x] = 128;
        break;
    }
}



static int decode_block_coeffs_4x4(N148BsReader* bs, int16_t levels[16])
{
    uint32_t total_coeff;
    int i;

    memset(levels, 0, 16 * sizeof(int16_t));

    if (n148_bs_read_ue(bs, &total_coeff) != 0) return -1;
    if (total_coeff > 16) return -1;
    if (total_coeff == 0) return 0;

    for (i = 0; i < (int)total_coeff; i++) {
        int32_t level;
        if (n148_bs_read_se(bs, &level) != 0) return -1;
        levels[i] = (int16_t)level;
    }

    return (int)total_coeff;
}



int n148_reconstruct_iframe(uint8_t* y_plane, uint8_t* uv_plane,
                            int stride, int width, int height,
                            int qp, N148BsReader* bs)
{
    int mb_cols = (width + 15) / 16;
    int mb_rows = (height + 15) / 16;
    int mb_x, mb_y, blk_y, blk_x;

   
    for (mb_y = 0; mb_y < mb_rows; mb_y++) {
        for (mb_x = 0; mb_x < mb_cols; mb_x++) {

           
            for (blk_y = 0; blk_y < 4; blk_y++) {
                for (blk_x = 0; blk_x < 4; blk_x++) {
                    int px = mb_x * 16 + blk_x * 4;
                    int py = mb_y * 16 + blk_y * 4;
                    uint32_t intra_mode;
                    uint32_t has_residual;
                    uint8_t above[4], left[4];
                    int has_above, has_left;
                    uint8_t pred_block[4 * 4];
                    int i;

                    if (px >= width || py >= height)
                        continue;

                   
                    if (n148_bs_read_ue(bs, &intra_mode) != 0) return -1;
                    if (intra_mode >= N148_INTRA_MODE_COUNT) intra_mode = N148_INTRA_DC;

                   
                    has_above = (py > 0);
                    has_left  = (px > 0);

                    if (has_above) {
                        int ax;
                        for (ax = 0; ax < 4 && (px + ax) < width; ax++)
                            above[ax] = y_plane[(py - 1) * stride + px + ax];
                        for (; ax < 4; ax++)
                            above[ax] = above[ax - 1];
                    }
                    if (has_left) {
                        int ay;
                        for (ay = 0; ay < 4 && (py + ay) < height; ay++)
                            left[ay] = y_plane[(py + ay) * stride + px - 1];
                        for (; ay < 4; ay++)
                            left[ay] = left[ay - 1];
                    }

                   
                    n148_intra_pred_4x4(pred_block, 4, (int)intra_mode,
                                         above, left, has_above, has_left);

                   
                    if (n148_bs_read_bits(bs, 1, &has_residual) != 0) return -1;

                    if (has_residual) {
                        int16_t levels[16], coeffs[16], residual[16];
                        int32_t qp_delta;

                        if (n148_bs_read_se(bs, &qp_delta) != 0) return -1;

                        if (decode_block_coeffs_4x4(bs, levels) < 0) return -1;

                       
                        {
                            int16_t reordered[16];
                            for (i = 0; i < 16; i++)
                                reordered[g_zigzag_4x4[i]] = levels[i];
                            n148_dequant_4x4(reordered, coeffs, qp + (int)qp_delta);
                        }

                       
                        n148_idct_4x4(coeffs, residual);

                       
                        for (i = 0; i < 16; i++) {
                            int r = i / 4, c = i % 4;
                            if ((py + r) < height && (px + c) < width) {
                                int val = (int)pred_block[r * 4 + c] + (int)residual[r * 4 + c];
                                y_plane[(py + r) * stride + px + c] = clip_u8(val);
                            }
                        }
                    } else {
                       
                        int r, c;
                        for (r = 0; r < 4; r++)
                            for (c = 0; c < 4; c++)
                                if ((py + r) < height && (px + c) < width)
                                    y_plane[(py + r) * stride + px + c] = pred_block[r * 4 + c];
                    }
                }
            }

           
           
            {
                int ch;
                for (ch = 0; ch < 2; ch++) {
                    for (blk_y = 0; blk_y < 2; blk_y++) {
                        for (blk_x = 0; blk_x < 2; blk_x++) {
                            int cx = mb_x * 8 + blk_x * 4;
                            int cy = mb_y * 8 + blk_y * 4;
                            int chroma_w = width / 2;
                            int chroma_h = height / 2;
                            uint32_t intra_mode;
                            uint32_t has_residual;
                            uint8_t above_c[4], left_c[4];
                            int has_above_c, has_left_c;
                            uint8_t pred_block[4 * 4];
                            int ax, ay, i;

                            if (cx >= chroma_w || cy >= chroma_h)
                                continue;

                            if (n148_bs_read_ue(bs, &intra_mode) != 0) return -1;
                            if (intra_mode >= N148_INTRA_MODE_COUNT) intra_mode = N148_INTRA_DC;

                            has_above_c = (cy > 0);
                            has_left_c  = (cx > 0);

                           
                            if (has_above_c) {
                                for (ax = 0; ax < 4 && (cx + ax) < chroma_w; ax++)
                                    above_c[ax] = uv_plane[(cy - 1) * stride + (cx + ax) * 2 + ch];
                                for (; ax < 4; ax++)
                                    above_c[ax] = above_c[ax - 1];
                            }
                            if (has_left_c) {
                                for (ay = 0; ay < 4 && (cy + ay) < chroma_h; ay++)
                                    left_c[ay] = uv_plane[(cy + ay) * stride + (cx - 1) * 2 + ch];
                                for (; ay < 4; ay++)
                                    left_c[ay] = left_c[ay - 1];
                            }

                            n148_intra_pred_4x4(pred_block, 4, (int)intra_mode,
                                                 above_c, left_c, has_above_c, has_left_c);

                            if (n148_bs_read_bits(bs, 1, &has_residual) != 0) return -1;

                            if (has_residual) {
                                int16_t levels[16], coeffs[16], residual[16];
                                int32_t qp_delta;

                                if (n148_bs_read_se(bs, &qp_delta) != 0) return -1;
                                if (decode_block_coeffs_4x4(bs, levels) < 0) return -1;

                                {
                                    int16_t reordered[16];
                                    for (i = 0; i < 16; i++)
                                        reordered[g_zigzag_4x4[i]] = levels[i];
                                    n148_dequant_4x4(reordered, coeffs, qp + (int)qp_delta);
                                }

                                n148_idct_4x4(coeffs, residual);

                                for (i = 0; i < 16; i++) {
                                    int r = i / 4, c = i % 4;
                                    if ((cy + r) < chroma_h && (cx + c) < chroma_w) {
                                        int val = (int)pred_block[r * 4 + c] + (int)residual[r * 4 + c];
                                        uv_plane[(cy + r) * stride + (cx + c) * 2 + ch] = clip_u8(val);
                                    }
                                }
                            } else {
                                int r, c;
                                for (r = 0; r < 4; r++)
                                    for (c = 0; c < 4; c++)
                                        if ((cy + r) < chroma_h && (cx + c) < chroma_w)
                                            uv_plane[(cy + r) * stride + (cx + c) * 2 + ch] = pred_block[r * 4 + c];
                            }
                        }
                    }
                }
            }

        }
    }

    return 0;
}