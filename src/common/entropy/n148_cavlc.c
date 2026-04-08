#include "n148_cavlc.h"
#include <string.h>

int n148_entropy_cavlc_write_block(N148BsWriter* bs,
                                   const int16_t* qcoeff_zigzag,
                                   int coeff_count)
{
    int i;

    if (!bs || !qcoeff_zigzag || coeff_count < 0 || coeff_count > 16)
        return -1;

    if (n148_bs_write_ue(bs, (uint32_t)coeff_count) != 0)
        return -1;

    for (i = 0; i < coeff_count; i++) {
        if (n148_bs_write_se(bs, qcoeff_zigzag[i]) != 0)
            return -1;
    }

    return 0;
}

int n148_entropy_cavlc_read_block(N148BsReader* bs,
                                  int16_t* qcoeff_zigzag,
                                  int* coeff_count,
                                  int max_coeffs)
{
    uint32_t count = 0;
    int i;
    int32_t level = 0;

    if (!bs || !qcoeff_zigzag || !coeff_count || max_coeffs <= 0)
        return -1;

    memset(qcoeff_zigzag, 0, (size_t)max_coeffs * sizeof(qcoeff_zigzag[0]));

    if (n148_bs_read_ue(bs, &count) != 0)
        return -1;
    if ((int)count > max_coeffs)
        return -1;

    for (i = 0; i < (int)count; i++) {
        if (n148_bs_read_se(bs, &level) != 0)
            return -1;
        qcoeff_zigzag[i] = (int16_t)level;
    }

    *coeff_count = (int)count;
    return 0;
}

int n148_entropy_cavlc_write_mv(N148BsWriter* bs, int mvx, int mvy)
{
    if (!bs) return -1;
    if (n148_bs_write_se(bs, mvx) != 0) return -1;
    if (n148_bs_write_se(bs, mvy) != 0) return -1;
    return 0;
}

int n148_entropy_cavlc_read_mv(N148BsReader* bs, int* mvx, int* mvy)
{
    int32_t x = 0;
    int32_t y = 0;

    if (!bs || !mvx || !mvy) return -1;

    if (n148_bs_read_se(bs, &x) != 0) return -1;
    if (n148_bs_read_se(bs, &y) != 0) return -1;

    *mvx = (int)x;
    *mvy = (int)y;
    return 0;
}