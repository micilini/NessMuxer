#include "motion_search.h"

#include <limits.h>

static uint8_t sample_at(const uint8_t* plane, int stride,
                         int x, int y,
                         int sample_stride, int sample_offset)
{
    return plane[y * stride + x * sample_stride + sample_offset];
}

static int block_sad_4x4(const uint8_t* cur_plane, int cur_stride,
                         const uint8_t* ref_plane, int ref_stride,
                         int width, int height,
                         int cur_bx, int cur_by,
                         int ref_bx, int ref_by,
                         int sample_stride, int sample_offset)
{
    int sad = 0;
    int y, x;

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int cx = cur_bx + x;
            int cy = cur_by + y;
            int rx = ref_bx + x;
            int ry = ref_by + y;
            int a, b, d;

            if (cx >= width || cy >= height)
                continue;

            if (rx < 0 || ry < 0 || rx >= width || ry >= height)
                b = 128;
            else
                b = sample_at(ref_plane, ref_stride, rx, ry, sample_stride, sample_offset);

            a = sample_at(cur_plane, cur_stride, cx, cy, sample_stride, sample_offset);
            d = a - b;
            sad += (d < 0) ? -d : d;
        }
    }

    return sad;
}

int n148_sad_4x4(const uint8_t* cur, int cur_stride,
                 const uint8_t* ref, int ref_stride)
{
    int sad = 0;
    int y, x;

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int d = (int)cur[y * cur_stride + x] - (int)ref[y * ref_stride + x];
            sad += (d < 0) ? -d : d;
        }
    }

    return sad;
}

int n148_motion_search_diamond_4x4(const uint8_t* cur_plane, int cur_stride,
                                   const uint8_t* ref_plane, int ref_stride,
                                   int width, int height,
                                   int bx, int by,
                                   int sample_stride, int sample_offset,
                                   int search_range,
                                   N148MotionResult* out)
{
    static const int dirs[4][2] = {
        {  0, -1 },
        { -1,  0 },
        {  1,  0 },
        {  0,  1 }
    };

    int best_mvx = 0;
    int best_mvy = 0;
    int best_sad;
    int improved = 1;

    if (!cur_plane || !ref_plane || !out)
        return -1;

    best_sad = block_sad_4x4(cur_plane, cur_stride,
                             ref_plane, ref_stride,
                             width, height,
                             bx, by, bx, by,
                             sample_stride, sample_offset);

    while (improved) {
        int i;
        improved = 0;

        for (i = 0; i < 4; i++) {
            int cand_mvx = best_mvx + dirs[i][0];
            int cand_mvy = best_mvy + dirs[i][1];
            int cand_sad;

            if (cand_mvx < -search_range || cand_mvx > search_range ||
                cand_mvy < -search_range || cand_mvy > search_range)
                continue;

            cand_sad = block_sad_4x4(cur_plane, cur_stride,
                                     ref_plane, ref_stride,
                                     width, height,
                                     bx, by,
                                     bx + cand_mvx, by + cand_mvy,
                                     sample_stride, sample_offset);

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
    return 0;
}