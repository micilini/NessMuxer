#include "n148_binarization.h"

int n148_bin_write_unary(N148BsWriter* bs, uint32_t value)
{
    uint32_t i;
    if (!bs) return -1;

    for (i = 0; i < value; i++) {
        if (n148_bs_write_bits(bs, 1, 0) != 0)
            return -1;
    }

    return n148_bs_write_bits(bs, 1, 1);
}

int n148_bin_read_unary(N148BsReader* bs, uint32_t* out)
{
    uint32_t bit = 0;
    uint32_t value = 0;

    if (!bs || !out) return -1;

    for (;;) {
        if (n148_bs_read_bits(bs, 1, &bit) != 0)
            return -1;
        if (bit)
            break;
        value++;
        if (value > 1024)
            return -1;
    }

    *out = value;
    return 0;
}

int n148_bin_write_signed_mag(N148BsWriter* bs, int32_t value)
{
    uint32_t mag;

    if (!bs) return -1;

    if (value == 0)
        return n148_bs_write_bits(bs, 1, 0);

    if (n148_bs_write_bits(bs, 1, 1) != 0)
        return -1;
    if (n148_bs_write_bits(bs, 1, value < 0 ? 1 : 0) != 0)
        return -1;

    mag = (uint32_t)((value < 0) ? -value : value);
    return n148_bs_write_ue(bs, mag - 1);
}

int n148_bin_read_signed_mag(N148BsReader* bs, int32_t* out)
{
    uint32_t nonzero = 0;
    uint32_t sign = 0;
    uint32_t magm1 = 0;

    if (!bs || !out) return -1;

    if (n148_bs_read_bits(bs, 1, &nonzero) != 0)
        return -1;

    if (!nonzero) {
        *out = 0;
        return 0;
    }

    if (n148_bs_read_bits(bs, 1, &sign) != 0)
        return -1;
    if (n148_bs_read_ue(bs, &magm1) != 0)
        return -1;

    *out = (int32_t)(magm1 + 1);
    if (sign)
        *out = -*out;

    return 0;
}