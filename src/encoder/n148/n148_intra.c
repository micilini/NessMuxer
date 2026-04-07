#include "n148_intra.h"
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
                           uint8_t best_pred[16])
{
    uint8_t src[16];
    uint8_t pred[16];
    int best_mode = N148_INTRA_DC;
    int best_sad = INT_MAX;
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
                    sad += (d < 0) ? -d : d;
                }
            }
        }

        if (sad < best_sad) {
            best_sad = sad;
            best_mode = mode;
            memcpy(best_pred, pred, 16);
        }
    }

    return best_mode;
}