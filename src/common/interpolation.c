#include "interpolation.h"

static int floor_div4(int v)
{
    if (v >= 0) return v / 4;
    return -(((-v) + 3) / 4);
}

static int mod4_pos(int v)
{
    int r = v % 4;
    return (r < 0) ? (r + 4) : r;
}

static uint8_t sample_or_default(const uint8_t* plane, int stride,
                                 int width, int height,
                                 int x, int y,
                                 int sample_stride, int sample_offset)
{
    if (x < 0 || y < 0 || x >= width || y >= height)
        return 128;

    return plane[y * stride + x * sample_stride + sample_offset];
}

uint8_t n148_interp_sample_qpel(const uint8_t* plane, int stride,
                                int width, int height,
                                int x_q4, int y_q4,
                                int sample_stride, int sample_offset)
{
    int x0 = floor_div4(x_q4);
    int y0 = floor_div4(y_q4);
    int fx = mod4_pos(x_q4);
    int fy = mod4_pos(y_q4);

    int p00 = sample_or_default(plane, stride, width, height, x0,     y0,     sample_stride, sample_offset);
    int p10 = sample_or_default(plane, stride, width, height, x0 + 1, y0,     sample_stride, sample_offset);
    int p01 = sample_or_default(plane, stride, width, height, x0,     y0 + 1, sample_stride, sample_offset);
    int p11 = sample_or_default(plane, stride, width, height, x0 + 1, y0 + 1, sample_stride, sample_offset);

    int w00 = (4 - fx) * (4 - fy);
    int w10 = fx * (4 - fy);
    int w01 = (4 - fx) * fy;
    int w11 = fx * fy;
    int sum = p00 * w00 + p10 * w10 + p01 * w01 + p11 * w11;

    return (uint8_t)((sum + 8) / 16);
}

void n148_interp_block_4x4_qpel(uint8_t out[16],
                                const uint8_t* plane, int stride,
                                int width, int height,
                                int bx, int by,
                                int mvx_q4, int mvy_q4,
                                int sample_stride, int sample_offset)
{
    int y, x;

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int sx_q4 = (bx + x) * 4 + mvx_q4;
            int sy_q4 = (by + y) * 4 + mvy_q4;

            out[y * 4 + x] = n148_interp_sample_qpel(
                plane, stride,
                width, height,
                sx_q4, sy_q4,
                sample_stride, sample_offset
            );
        }
    }
}