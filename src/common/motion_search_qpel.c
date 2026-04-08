#include "motion_search_qpel.h"
#include "motion_search.h"
#include "interpolation.h"

#include <limits.h>

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
        sad += (d < 0) ? -d : d;
    }

    return sad;
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
    int r;

    if (!cur_plane || !ref_planes || ref_count <= 0 || !out)
        return -1;

    load_cur_block_4x4(cur_plane, cur_stride,
                       width, height,
                       bx, by,
                       sample_stride, sample_offset,
                       cur);

    for (r = 0; r < ref_count; r++) {
        N148MotionResult base;
        int dx, dy;

        if (!ref_planes[r])
            continue;

        if (n148_motion_search_diamond_4x4(cur_plane, cur_stride,
                                           ref_planes[r], ref_stride,
                                           width, height,
                                           bx, by,
                                           sample_stride, sample_offset,
                                           search_range,
                                           &base) != 0)
            continue;

        for (dy = -2; dy <= 2; dy++) {
            for (dx = -2; dx <= 2; dx++) {
                int cand_mvx_q4 = base.mvx * 4 + dx;
                int cand_mvy_q4 = base.mvy * 4 + dy;
                int cand_sad = block_sad_qpel_4x4(cur,
                                                  ref_planes[r], ref_stride,
                                                  width, height,
                                                  bx, by,
                                                  cand_mvx_q4, cand_mvy_q4,
                                                  sample_stride, sample_offset);

                if (cand_sad < best_sad) {
                    best_sad = cand_sad;
                    best_ref = r;
                    best_mvx_q4 = cand_mvx_q4;
                    best_mvy_q4 = cand_mvy_q4;
                }
            }
        }
    }

    out->ref_idx = best_ref;
    out->mvx_q4 = best_mvx_q4;
    out->mvy_q4 = best_mvy_q4;
    out->sad = best_sad;
    return 0;
}