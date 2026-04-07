#include "n148_transform.h"

static void fdct_1d_4(const int16_t* in, int16_t* out)
{
    int x0 = in[0] + in[3];
    int x1 = in[1] + in[2];
    int x2 = in[1] - in[2];
    int x3 = in[0] - in[3];

    out[0] = (int16_t)(x0 + x1);
    out[1] = (int16_t)(x3 + x2);
    out[2] = (int16_t)(x0 - x1);
    out[3] = (int16_t)(x3 - x2);
}

void n148_fdct_4x4(const int16_t* in, int16_t* out)
{
    int16_t tmp[16];
    int16_t col_in[4];
    int16_t col_out[4];
    int i;

    for (i = 0; i < 4; i++)
        fdct_1d_4(in + i * 4, tmp + i * 4);

    for (i = 0; i < 4; i++) {
        col_in[0] = tmp[0 * 4 + i];
        col_in[1] = tmp[1 * 4 + i];
        col_in[2] = tmp[2 * 4 + i];
        col_in[3] = tmp[3 * 4 + i];

        fdct_1d_4(col_in, col_out);

        out[0 * 4 + i] = col_out[0];
        out[1 * 4 + i] = col_out[1];
        out[2 * 4 + i] = col_out[2];
        out[3 * 4 + i] = col_out[3];
    }
}