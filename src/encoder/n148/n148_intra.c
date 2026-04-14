#include "n148_intra.h"
#include "n148_transform.h"
#include "n148_quant.h"
#include "../../decoder/n148/n148_frame_recon.h"
#include "../../codec/n148/n148_spec.h"

#include <limits.h>
#include <string.h>

static uint8_t read_sample(const uint8_t* plane, int stride,
                           int x, int y,
                           int sample_stride, int sample_offset)
{
    return plane[y * stride + x * sample_stride + sample_offset];
}

static void load_source_block(const uint8_t* plane, int stride,
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
                out[y * 4 + x] = read_sample(plane, stride, px, py, sample_stride, sample_offset);
            else
                out[y * 4 + x] = 128;
        }
    }
}

static int abs_val(int v)
{
    return (v < 0) ? -v : v;
}

static int estimate_satd_4x4(const uint8_t src[16], const uint8_t pred[16])
{
    int satd = 0;
    int y, x;

    for (y = 0; y < 4; y++) {
        int d0 = (int)src[y * 4 + 0] - (int)pred[y * 4 + 0];
        int d1 = (int)src[y * 4 + 1] - (int)pred[y * 4 + 1];
        int d2 = (int)src[y * 4 + 2] - (int)pred[y * 4 + 2];
        int d3 = (int)src[y * 4 + 3] - (int)pred[y * 4 + 3];

        int a0 = d0 + d3;
        int a1 = d1 + d2;
        int a2 = d1 - d2;
        int a3 = d0 - d3;

        satd += abs_val(a0 + a1);
        satd += abs_val(a3 + a2);
        satd += abs_val(a0 - a1);
        satd += abs_val(a3 - a2);
    }

    for (x = 0; x < 4; x++) {
        int d0 = (int)src[0 * 4 + x] - (int)pred[0 * 4 + x];
        int d1 = (int)src[1 * 4 + x] - (int)pred[1 * 4 + x];
        int d2 = (int)src[2 * 4 + x] - (int)pred[2 * 4 + x];
        int d3 = (int)src[3 * 4 + x] - (int)pred[3 * 4 + x];

        int a0 = d0 + d3;
        int a1 = d1 + d2;
        int a2 = d1 - d2;
        int a3 = d0 - d3;

        satd += abs_val(a0 + a1);
        satd += abs_val(a3 + a2);
        satd += abs_val(a0 - a1);
        satd += abs_val(a3 - a2);
    }

    return satd >> 1;
}

static int estimate_coeff_count_4x4(const uint8_t src[16], const uint8_t pred[16], int qp, int is_chroma)
{
    int16_t residual[16];
    int16_t coeff[16];
    int16_t qzigzag[16];
    int i;

    for (i = 0; i < 16; i++)
        residual[i] = (int16_t)((int)src[i] - (int)pred[i]);

    n148_fdct_4x4(residual, coeff);
    return n148_quantize_4x4_tuned(coeff, qzigzag, qp, 1, is_chroma);
}

void n148_intra_build_prediction(const uint8_t* ref_plane, int stride,
                                 int width, int height,
                                 int bx, int by,
                                 int sample_stride, int sample_offset,
                                 int mode,
                                 uint8_t pred[16])
{
    uint8_t above[4] = {128, 128, 128, 128};
    uint8_t left[4]  = {128, 128, 128, 128};
    int has_above = (by > 0);
    int has_left  = (bx > 0);
    int i;

    if (has_above) {
        for (i = 0; i < 4; i++) {
            int px = bx + i;
            if (px < width)
                above[i] = read_sample(ref_plane, stride, px, by - 1, sample_stride, sample_offset);
            else if (i > 0)
                above[i] = above[i - 1];
        }
    }

    if (has_left) {
        for (i = 0; i < 4; i++) {
            int py = by + i;
            if (py < height)
                left[i] = read_sample(ref_plane, stride, bx - 1, py, sample_stride, sample_offset);
            else if (i > 0)
                left[i] = left[i - 1];
        }
    }

    n148_intra_pred_4x4(pred, 4, mode, above, left, has_above, has_left);
}

int n148_intra_choose_mode(const uint8_t* src_plane,
                           const uint8_t* ref_plane,
                           int stride,
                           int width, int height,
                           int bx, int by,
                           int sample_stride, int sample_offset,
                           int qp,
                           uint8_t best_pred[16])
{
    uint8_t src[16];
    uint8_t pred[16];
    uint8_t pred_best[16] = {0};
    uint8_t pred_second[16] = {0};
    int best_mode = N148_INTRA_DC;
    int second_mode = N148_INTRA_DC;
    int best_sad = INT_MAX;
    int second_sad = INT_MAX;
    int best_refine_cost = INT_MAX;
    int mode, x, y;

    load_source_block(src_plane, stride, width, height,
                      bx, by, sample_stride, sample_offset, src);

    for (mode = 0; mode < N148_INTRA_MODE_COUNT; mode++) {
        int sad = 0;

        n148_intra_build_prediction(ref_plane, stride, width, height,
                                    bx, by, sample_stride, sample_offset,
                                    mode, pred);

        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                int px = bx + x;
                int py = by + y;
                if (px < width && py < height) {
                    int d = (int)src[y * 4 + x] - (int)pred[y * 4 + x];
                    sad += abs_val(d);
                }
            }
        }

        if (sad < best_sad) {
            second_sad = best_sad;
            second_mode = best_mode;
            memcpy(pred_second, pred_best, 16);

            best_sad = sad;
            best_mode = mode;
            memcpy(pred_best, pred, 16);
        } else if (sad < second_sad) {
            second_sad = sad;
            second_mode = mode;
            memcpy(pred_second, pred, 16);
        }
    }

    {
        int candidate_modes[2];
        const uint8_t* candidate_preds[2];
        int candidate_count = 0;
        int idx;

        candidate_modes[candidate_count] = best_mode;
        candidate_preds[candidate_count] = pred_best;
        candidate_count++;

        if (second_sad < INT_MAX && second_mode != best_mode && second_sad <= best_sad + 8) {
            candidate_modes[candidate_count] = second_mode;
            candidate_preds[candidate_count] = pred_second;
            candidate_count++;
        }

        for (idx = 0; idx < candidate_count; idx++) {
            int satd = estimate_satd_4x4(src, candidate_preds[idx]);
            int coeff_count = estimate_coeff_count_4x4(src, candidate_preds[idx], qp, sample_stride != 1 || sample_offset != 0);
            int refine_cost = satd + coeff_count * 6;

            if (candidate_modes[idx] == N148_INTRA_PLANAR)
                refine_cost += 2;

            if (refine_cost < best_refine_cost) {
                best_refine_cost = refine_cost;
                best_mode = candidate_modes[idx];
                memcpy(best_pred, candidate_preds[idx], 16);
            }
        }
    }

    return best_mode;
}