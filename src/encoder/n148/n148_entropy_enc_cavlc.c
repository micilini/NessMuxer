#include "n148_entropy_enc_cavlc.h"

int n148_cavlc_write_block(N148BsWriter* bs,
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

int n148_cavlc_write_mv(N148BsWriter* bs, int mvx, int mvy)
{
    if (!bs)
        return -1;

    if (n148_bs_write_se(bs, mvx) != 0)
        return -1;
    if (n148_bs_write_se(bs, mvy) != 0)
        return -1;

    return 0;
}