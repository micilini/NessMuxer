#include "n148_motion.h"
#include "../../common/interpolation.h"

void n148_mc_copy_qpel_4x4(uint8_t* dst_plane, int dst_stride,
                           const uint8_t* ref_plane, int ref_stride,
                           int width, int height,
                           int dst_bx, int dst_by,
                           int mvx_q4, int mvy_q4,
                           int sample_stride, int sample_offset)
{
    uint8_t pred[16];
    int y, x;

    n148_interp_block_4x4_qpel(pred,
                               ref_plane, ref_stride,
                               width, height,
                               dst_bx, dst_by,
                               mvx_q4, mvy_q4,
                               sample_stride, sample_offset);

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int dx = dst_bx + x;
            int dy = dst_by + y;
            if (dx < width && dy < height)
                dst_plane[dy * dst_stride + dx * sample_stride + sample_offset] = pred[y * 4 + x];
        }
    }
}