#include "n148_cabac_binarization.h"

int n148_cabac_write_unary_ctx(N148CabacCore* c, N148BsWriter* bs, N148CabacContext* ctx, uint32_t value)
{
    uint32_t i;
    for (i = 0; i < value; i++) {
        if (n148_cabac_encode_bin_ctx(c, bs, ctx, 1u) != 0)
            return -1;
    }
    return n148_cabac_encode_bin_ctx(c, bs, ctx, 0u);
}

int n148_cabac_read_unary_ctx(N148CabacCore* c, N148BsReader* bs, N148CabacContext* ctx, uint32_t* value)
{
    uint32_t v = 0;
    uint32_t bin = 0;

    if (!value)
        return -1;

    for (;;) {
        if (n148_cabac_decode_bin_ctx(c, bs, ctx, &bin) != 0)
            return -1;
        if (bin == 0u)
            break;
        v++;
        if (v > 4096u)
            return -1;
    }

    *value = v;
    return 0;
}

int n148_cabac_write_trunc_unary_ctx(N148CabacCore* c, N148BsWriter* bs, N148CabacContext* ctx, uint32_t value, uint32_t max_value)
{
    uint32_t i;
    if (value > max_value)
        return -1;
    for (i = 0; i < value; i++) {
        if (n148_cabac_encode_bin_ctx(c, bs, ctx, 1u) != 0)
            return -1;
    }
    if (value < max_value)
        return n148_cabac_encode_bin_ctx(c, bs, ctx, 0u);
    return 0;
}

int n148_cabac_read_trunc_unary_ctx(N148CabacCore* c, N148BsReader* bs, N148CabacContext* ctx, uint32_t* value, uint32_t max_value)
{
    uint32_t v = 0;
    uint32_t bin = 0;

    if (!value)
        return -1;

    while (v < max_value) {
        if (n148_cabac_decode_bin_ctx(c, bs, ctx, &bin) != 0)
            return -1;
        if (bin == 0u)
            break;
        v++;
    }

    *value = v;
    return 0;
}

int n148_cabac_write_fixed_bypass(N148CabacCore* c, N148BsWriter* bs, uint32_t value, int num_bits)
{
    int i;
    for (i = num_bits - 1; i >= 0; i--) {
        if (n148_cabac_encode_bin_bypass(c, bs, (value >> i) & 1u) != 0)
            return -1;
    }
    return 0;
}

int n148_cabac_read_fixed_bypass(N148CabacCore* c, N148BsReader* bs, uint32_t* value, int num_bits)
{
    uint32_t out = 0;
    uint32_t bit = 0;
    int i;

    if (!value)
        return -1;

    for (i = 0; i < num_bits; i++) {
        if (n148_cabac_decode_bin_bypass(c, bs, &bit) != 0)
            return -1;
        out = (out << 1) | (bit & 1u);
    }

    *value = out;
    return 0;
}

int n148_cabac_write_signed_mag_ctx(N148CabacCore* c,
                                    N148BsWriter* bs,
                                    N148CabacContext* sig_ctx,
                                    N148CabacContext* sign_ctx,
                                    N148CabacContext* mag_ctx,
                                    int32_t value)
{
    uint32_t mag;

    if (value == 0)
        return n148_cabac_encode_bin_ctx(c, bs, sig_ctx, 0u);

    if (n148_cabac_encode_bin_ctx(c, bs, sig_ctx, 1u) != 0)
        return -1;
    if (n148_cabac_encode_bin_ctx(c, bs, sign_ctx, value < 0 ? 1u : 0u) != 0)
        return -1;

    mag = (uint32_t)(value < 0 ? -value : value);
    return n148_cabac_write_unary_ctx(c, bs, mag_ctx, mag - 1u);
}

int n148_cabac_read_signed_mag_ctx(N148CabacCore* c,
                                   N148BsReader* bs,
                                   N148CabacContext* sig_ctx,
                                   N148CabacContext* sign_ctx,
                                   N148CabacContext* mag_ctx,
                                   int32_t* out)
{
    uint32_t sig = 0, sign = 0, magm1 = 0;

    if (!out)
        return -1;

    if (n148_cabac_decode_bin_ctx(c, bs, sig_ctx, &sig) != 0)
        return -1;
    if (!sig) {
        *out = 0;
        return 0;
    }

    if (n148_cabac_decode_bin_ctx(c, bs, sign_ctx, &sign) != 0)
        return -1;
    if (n148_cabac_read_unary_ctx(c, bs, mag_ctx, &magm1) != 0)
        return -1;

    *out = (int32_t)(magm1 + 1u);
    if (sign)
        *out = -*out;
    return 0;
}