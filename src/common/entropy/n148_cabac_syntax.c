#include "n148_cabac_syntax.h"
#include <string.h>
#include <limits.h>
#include <stdio.h>

#ifndef N148_CABAC_TRACE
#define N148_CABAC_TRACE 0
#endif

#if N148_CABAC_TRACE
#define MVD_LOG(fmt, ...) \
    fprintf(stderr, "[N148 CABAC][MVD] " fmt "\n", ##__VA_ARGS__)
#define BLK_LOG(fmt, ...) \
    fprintf(stderr, "[N148 CABAC][BLK] " fmt "\n", ##__VA_ARGS__)
#else
#define MVD_LOG(fmt, ...) do { } while (0)
#define BLK_LOG(fmt, ...) do { } while (0)
#endif

static int n148_cabac_write_mvd_component(N148CabacCore* core,
                                          N148BsWriter* bs,
                                          N148CabacContext* ctx_zero,
                                          N148CabacContext* ctx_gt1,
                                          N148CabacContext* ctx_gt2,
                                          N148CabacContext* ctx_gt3p,
                                          int value)
{
    uint32_t mag;
    uint32_t suffix;
    uint32_t bit;
    int k;
    int prefix_count = 0;

    if (!core || !bs || !ctx_zero || !ctx_gt1 || !ctx_gt2 || !ctx_gt3p)
        return -1;

    if (value == INT_MIN)
        return -1;

    mag = (uint32_t)(value < 0 ? -value : value);

    MVD_LOG("ENC begin value=%d mag=%u", value, mag);

    if (n148_cabac_encode_bin_ctx(core, bs, ctx_zero, mag ? 1u : 0u) != 0)
        return -1;
    MVD_LOG("ENC zero/nonzero=%u", mag ? 1u : 0u);

    if (mag == 0u) {
        MVD_LOG("ENC end value=0");
        return 0;
    }

    if (n148_cabac_encode_bin_ctx(core, bs, ctx_gt1, (mag > 1u) ? 1u : 0u) != 0)
        return -1;
    MVD_LOG("ENC gt1=%u", (mag > 1u) ? 1u : 0u);
    if (mag == 1u)
        goto write_sign;

    if (n148_cabac_encode_bin_ctx(core, bs, ctx_gt2, (mag > 2u) ? 1u : 0u) != 0)
        return -1;
    MVD_LOG("ENC gt2=%u", (mag > 2u) ? 1u : 0u);
    if (mag == 2u)
        goto write_sign;

    if (n148_cabac_encode_bin_ctx(core, bs, ctx_gt3p, (mag > 3u) ? 1u : 0u) != 0)
        return -1;
    MVD_LOG("ENC gt3p=%u", (mag > 3u) ? 1u : 0u);
    if (mag == 3u)
        goto write_sign;

    suffix = mag - 4u;
    k = 0;

    while (suffix >= (1u << k)) {
        if (n148_cabac_encode_bin_bypass(core, bs, 1u) != 0)
            return -1;
        MVD_LOG("ENC tail_prefix[%d]=1 suffix_before=%u", prefix_count, suffix);
        suffix -= (1u << k);
        prefix_count++;
        k++;
        if (k > 24)
            return -1;
    }

    if (n148_cabac_encode_bin_bypass(core, bs, 0u) != 0)
        return -1;
    MVD_LOG("ENC tail_stop=0 prefix_count=%d residual_suffix=%u", prefix_count, suffix);

    while (k-- > 0) {
        bit = (suffix >> k) & 1u;
        if (n148_cabac_encode_bin_bypass(core, bs, bit) != 0)
            return -1;
        MVD_LOG("ENC tail_bit[%d]=%u", k, bit);
    }

write_sign:
    MVD_LOG("ENC sign=%u", value < 0 ? 1u : 0u);
    return n148_cabac_encode_bin_bypass(core, bs, value < 0 ? 1u : 0u);
}

static int n148_cabac_read_mvd_component(N148CabacCore* core,
                                         N148BsReader* bs,
                                         N148CabacContext* ctx_zero,
                                         N148CabacContext* ctx_gt1,
                                         N148CabacContext* ctx_gt2,
                                         N148CabacContext* ctx_gt3p,
                                         int* out)
{
    uint32_t bin = 0;
    uint32_t sign = 0;
    uint32_t mag = 0;
    uint32_t suffix = 0;
    uint32_t bit = 0;
    int k = 0;
    int prefix_count = 0;

    if (!core || !bs || !ctx_zero || !ctx_gt1 || !ctx_gt2 || !ctx_gt3p || !out)
        return -1;

    if (n148_cabac_decode_bin_ctx(core, bs, ctx_zero, &bin) != 0)
        return -1;
    MVD_LOG("DEC zero/nonzero=%u", bin);
    if (bin == 0u) {
        *out = 0;
        MVD_LOG("DEC end value=0");
        return 0;
    }

    mag = 1u;

    if (n148_cabac_decode_bin_ctx(core, bs, ctx_gt1, &bin) != 0)
        return -1;
    MVD_LOG("DEC gt1=%u", bin);
    if (bin == 0u)
        goto read_sign;

    mag = 2u;

    if (n148_cabac_decode_bin_ctx(core, bs, ctx_gt2, &bin) != 0)
        return -1;
    MVD_LOG("DEC gt2=%u", bin);
    if (bin == 0u)
        goto read_sign;

    mag = 3u;

    if (n148_cabac_decode_bin_ctx(core, bs, ctx_gt3p, &bin) != 0)
        return -1;
    MVD_LOG("DEC gt3p=%u", bin);
    if (bin == 0u)
        goto read_sign;

    mag = 4u;

    while (1) {
        if (n148_cabac_decode_bin_bypass(core, bs, &bit) != 0)
            return -1;
        MVD_LOG("DEC tail_prefix[%d]=%u", prefix_count, bit);
        if (bit == 0u)
            break;
        mag += (1u << k);
        prefix_count++;
        k++;
        if (k > 24)
            return -1;
    }

    while (k-- > 0) {
        if (n148_cabac_decode_bin_bypass(core, bs, &bit) != 0)
            return -1;
        suffix = (suffix << 1) | (bit & 1u);
        MVD_LOG("DEC tail_bit[%d]=%u suffix_now=%u", k, bit, suffix);
    }

    mag += suffix;
    MVD_LOG("DEC mag_after_tail=%u", mag);

read_sign:
    if (n148_cabac_decode_bin_bypass(core, bs, &sign) != 0)
        return -1;

    *out = (int)mag;
    if (sign)
        *out = -*out;

    MVD_LOG("DEC sign=%u value=%d", sign, *out);
    return 0;
}

int n148_cabac_session_init_enc(N148CabacSession* s, int frame_type, int qp, int slice_id)
{
    if (!s)
        return -1;

    memset(s, 0, sizeof(*s));
    s->frame_type = frame_type;
    s->qp = qp;
    s->slice_id = slice_id;

    n148_cabac_core_init_enc(&s->core);
    n148_cabac_context_set_init_for_slice(&s->contexts, frame_type, qp, slice_id);
    return 0;
}

int n148_cabac_session_init_dec(N148CabacSession* s, N148BsReader* bs, int frame_type, int qp, int slice_id)
{
    if (!s || !bs)
        return -1;

    memset(s, 0, sizeof(*s));
    s->frame_type = frame_type;
    s->qp = qp;
    s->slice_id = slice_id;

    n148_cabac_context_set_init_for_slice(&s->contexts, frame_type, qp, slice_id);
    return n148_cabac_core_init_dec(&s->core, bs);
}

int n148_cabac_session_finish_enc(N148CabacSession* s, N148BsWriter* bs)
{
    if (!s || !bs)
        return -1;
    return n148_cabac_finish_enc(&s->core, bs);
}

int n148_cabac_write_block_mode(N148CabacSession* s, N148BsWriter* bs, uint32_t block_mode)
{
    return n148_cabac_write_trunc_unary_ctx(&s->core, bs,
        n148_cabac_context_get(&s->contexts, N148_CTX_BLOCK_MODE), block_mode, 2u);
}

int n148_cabac_read_block_mode(N148CabacSession* s, N148BsReader* bs, uint32_t* block_mode)
{
    return n148_cabac_read_trunc_unary_ctx(&s->core, bs,
        n148_cabac_context_get(&s->contexts, N148_CTX_BLOCK_MODE), block_mode, 2u);
}

int n148_cabac_write_ref_idx(N148CabacSession* s, N148BsWriter* bs, uint32_t ref_idx)
{
    N148CabacContext* ctx;

    if (!s || !bs || ref_idx > 3u)
        return -1;

    ctx = n148_cabac_context_get(&s->contexts, N148_CTX_REF_IDX);
    if (!ctx)
        return -1;

    if (n148_cabac_encode_bin_ctx(&s->core, bs, ctx, ref_idx ? 1u : 0u) != 0)
        return -1;

    if (ref_idx == 0u)
        return 0;

    return n148_cabac_write_trunc_unary_ctx(&s->core, bs, ctx, ref_idx - 1u, 2u);
}

int n148_cabac_read_ref_idx(N148CabacSession* s, N148BsReader* bs, uint32_t* ref_idx)
{
    N148CabacContext* ctx;
    uint32_t nonzero = 0;
    uint32_t rem = 0;

    if (!s || !bs || !ref_idx)
        return -1;

    ctx = n148_cabac_context_get(&s->contexts, N148_CTX_REF_IDX);
    if (!ctx)
        return -1;

    if (n148_cabac_decode_bin_ctx(&s->core, bs, ctx, &nonzero) != 0)
        return -1;

    if (!nonzero) {
        *ref_idx = 0u;
        return 0;
    }

    if (n148_cabac_read_trunc_unary_ctx(&s->core, bs, ctx, &rem, 2u) != 0)
        return -1;

    *ref_idx = rem + 1u;
    return 0;
}

int n148_cabac_write_intra_mode(N148CabacSession* s, N148BsWriter* bs, uint32_t intra_mode)
{
    return n148_cabac_write_fixed_bypass(&s->core, bs, intra_mode & 7u, 3);
}

int n148_cabac_read_intra_mode(N148CabacSession* s, N148BsReader* bs, uint32_t* intra_mode)
{
    return n148_cabac_read_fixed_bypass(&s->core, bs, intra_mode, 3);
}

int n148_cabac_write_has_residual(N148CabacSession* s, N148BsWriter* bs, uint32_t has_residual)
{
    return n148_cabac_encode_bin_ctx(&s->core, bs,
        n148_cabac_context_get(&s->contexts, N148_CTX_HAS_RESIDUAL), has_residual ? 1u : 0u);
}

int n148_cabac_read_has_residual(N148CabacSession* s, N148BsReader* bs, uint32_t* has_residual)
{
    return n148_cabac_decode_bin_ctx(&s->core, bs,
        n148_cabac_context_get(&s->contexts, N148_CTX_HAS_RESIDUAL), has_residual);
}

int n148_cabac_write_qp_delta(N148CabacSession* s, N148BsWriter* bs, int32_t qp_delta)
{
    return n148_cabac_write_signed_mag_ctx(&s->core, bs,
        n148_cabac_context_get(&s->contexts, N148_CTX_QP_DELTA),
        n148_cabac_context_get(&s->contexts, N148_CTX_QP_DELTA),
        n148_cabac_context_get(&s->contexts, N148_CTX_QP_DELTA), qp_delta);
}

int n148_cabac_read_qp_delta(N148CabacSession* s, N148BsReader* bs, int32_t* qp_delta)
{
    return n148_cabac_read_signed_mag_ctx(&s->core, bs,
        n148_cabac_context_get(&s->contexts, N148_CTX_QP_DELTA),
        n148_cabac_context_get(&s->contexts, N148_CTX_QP_DELTA),
        n148_cabac_context_get(&s->contexts, N148_CTX_QP_DELTA), qp_delta);
}

int n148_cabac_write_mv(N148CabacSession* s, N148BsWriter* bs, int mvx, int mvy)
{
    if (!s || !bs)
        return -1;

    MVD_LOG("WRITE MV begin mvx=%d mvy=%d", mvx, mvy);

    if (n148_cabac_write_mvd_component(
            &s->core,
            bs,
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_X_ZERO),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_X_GT1),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_X_GT2),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_X_GT3P),
            mvx) != 0)
        return -1;

    if (n148_cabac_write_mvd_component(
            &s->core,
            bs,
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_Y_ZERO),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_Y_GT1),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_Y_GT2),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_Y_GT3P),
            mvy) != 0)
        return -1;

    MVD_LOG("WRITE MV end mvx=%d mvy=%d", mvx, mvy);
    return 0;
}

int n148_cabac_read_mv(N148CabacSession* s, N148BsReader* bs, int* mvx, int* mvy)
{
    int x = 0;
    int y = 0;

    if (!s || !bs || !mvx || !mvy)
        return -1;

    if (n148_cabac_read_mvd_component(
            &s->core,
            bs,
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_X_ZERO),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_X_GT1),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_X_GT2),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_X_GT3P),
            &x) != 0)
        return -1;

    if (n148_cabac_read_mvd_component(
            &s->core,
            bs,
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_Y_ZERO),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_Y_GT1),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_Y_GT2),
            n148_cabac_context_get(&s->contexts, N148_CTX_MVD_Y_GT3P),
            &y) != 0)
        return -1;

    *mvx = x;
    *mvy = y;

    MVD_LOG("READ MV end mvx=%d mvy=%d", x, y);
    return 0;
}

int n148_cabac_write_block(N148CabacSession* s, N148BsWriter* bs, const int16_t* qcoeff_zigzag, int coeff_count)
{
    int i;
    int last_nz = -1;
    int nz_count = 0;
    int16_t levels[16];
    int nz_idx = 0;
    uint32_t mag;
    int prefix;

    if (!s || !bs || !qcoeff_zigzag || coeff_count < 0 || coeff_count > 16)
        return -1;

   
    for (i = coeff_count - 1; i >= 0; i--) {
        if (qcoeff_zigzag[i] != 0) {
            last_nz = i;
            break;
        }
    }

    BLK_LOG("ENC block coeff_count=%d last_nz=%d", coeff_count, last_nz);

   
    if (last_nz < 0) {
        return n148_cabac_encode_bin_ctx(&s->core, bs,
            n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_SIG), 0u);
    }

    if (n148_cabac_encode_bin_ctx(&s->core, bs,
        n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_SIG), 1u) != 0)
        return -1;

   
    for (i = 0; i <= last_nz; i++) {
        int sig = (qcoeff_zigzag[i] != 0) ? 1 : 0;

        if (n148_cabac_encode_bin_ctx(&s->core, bs,
            n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_LAST), (uint32_t)sig) != 0)
            return -1;

        if (sig) {
            levels[nz_count++] = qcoeff_zigzag[i];
            if (i < last_nz) {
               
                if (n148_cabac_encode_bin_ctx(&s->core, bs,
                    n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_COUNT), 0u) != 0)
                    return -1;
            } else {
               
                if (n148_cabac_encode_bin_ctx(&s->core, bs,
                    n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_COUNT), 1u) != 0)
                    return -1;
            }
        }
    }

    BLK_LOG("ENC nz_count=%d", nz_count);

   
    for (nz_idx = nz_count - 1; nz_idx >= 0; nz_idx--) {
        int16_t level = levels[nz_idx];
        uint32_t sign_bit = (level < 0) ? 1u : 0u;
        mag = (uint32_t)(level < 0 ? -level : level);

        BLK_LOG("ENC level[%d]=%d mag=%u", nz_idx, level, mag);

       
        prefix = (int)(mag - 1u);
        if (prefix < 14) {
            for (i = 0; i < prefix; i++) {
                if (n148_cabac_encode_bin_ctx(&s->core, bs,
                    n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_LEVEL_PREFIX), 1u) != 0)
                    return -1;
            }
            if (n148_cabac_encode_bin_ctx(&s->core, bs,
                n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_LEVEL_PREFIX), 0u) != 0)
                return -1;
        } else {
           
            for (i = 0; i < 14; i++) {
                if (n148_cabac_encode_bin_ctx(&s->core, bs,
                    n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_LEVEL_PREFIX), 1u) != 0)
                    return -1;
            }
           
            {
                uint32_t suffix_val = mag - 15u;
                uint32_t tmp = suffix_val + 1u;
                int num_bits = 0;
                uint32_t t2 = tmp;
                while (t2 > 1u) { num_bits++; t2 >>= 1; }
               
                for (i = 0; i < num_bits; i++) {
                    if (n148_cabac_encode_bin_bypass(&s->core, bs, 1u) != 0)
                        return -1;
                }
                if (n148_cabac_encode_bin_bypass(&s->core, bs, 0u) != 0)
                    return -1;
               
                for (i = num_bits - 1; i >= 0; i--) {
                    if (n148_cabac_encode_bin_bypass(&s->core, bs, (tmp >> i) & 1u) != 0)
                        return -1;
                }
            }
        }

       
        if (n148_cabac_encode_bin_bypass(&s->core, bs, sign_bit) != 0)
            return -1;
    }

    return 0;
}

int n148_cabac_read_block(N148CabacSession* s, N148BsReader* bs, int16_t* qcoeff_zigzag, int* coeff_count, int max_coeffs)
{
    uint32_t coded_block = 0;
    uint32_t sig = 0;
    uint32_t last_flag = 0;
    int16_t levels[16];
    int positions[16];
    int nz_count = 0;
    int i;
    int nz_idx;

    if (!s || !bs || !qcoeff_zigzag || !coeff_count || max_coeffs <= 0)
        return -1;

    memset(qcoeff_zigzag, 0, (size_t)max_coeffs * sizeof(qcoeff_zigzag[0]));

   
    if (n148_cabac_decode_bin_ctx(&s->core, bs,
        n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_SIG), &coded_block) != 0)
        return -1;

    if (!coded_block) {
        *coeff_count = 0;
        return 0;
    }

   
    for (i = 0; i < max_coeffs; i++) {
        if (n148_cabac_decode_bin_ctx(&s->core, bs,
            n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_LAST), &sig) != 0)
            return -1;

        if (sig) {
            positions[nz_count] = i;
            nz_count++;

            if (n148_cabac_decode_bin_ctx(&s->core, bs,
                n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_COUNT), &last_flag) != 0)
                return -1;

            if (last_flag)
                break;
        }
    }

    BLK_LOG("DEC nz_count=%d", nz_count);

   
    for (nz_idx = nz_count - 1; nz_idx >= 0; nz_idx--) {
        uint32_t bin = 0;
        uint32_t mag = 1;
        uint32_t sign_bit = 0;
        int prefix_count = 0;

       
        for (prefix_count = 0; prefix_count < 14; prefix_count++) {
            if (n148_cabac_decode_bin_ctx(&s->core, bs,
                n148_cabac_context_get(&s->contexts, N148_CTX_COEFF_LEVEL_PREFIX), &bin) != 0)
                return -1;
            if (!bin)
                break;
        }

        mag = (uint32_t)prefix_count + 1u;

        if (prefix_count == 14) {
           
            uint32_t leading_ones = 0;
            uint32_t suffix_val;
            while (1) {
                if (n148_cabac_decode_bin_bypass(&s->core, bs, &bin) != 0)
                    return -1;
                if (!bin)
                    break;
                leading_ones++;
                if (leading_ones > 24)
                    return -1;
            }
            suffix_val = 1u;
            for (i = 0; i < (int)leading_ones; i++) {
                if (n148_cabac_decode_bin_bypass(&s->core, bs, &bin) != 0)
                    return -1;
                suffix_val = (suffix_val << 1) | (bin & 1u);
            }
            suffix_val -= 1u;
            mag = suffix_val + 15u;
        }

       
        if (n148_cabac_decode_bin_bypass(&s->core, bs, &sign_bit) != 0)
            return -1;

        levels[nz_idx] = (int16_t)mag;
        if (sign_bit)
            levels[nz_idx] = (int16_t)(-(int16_t)mag);

        BLK_LOG("DEC level[%d]=%d pos=%d", nz_idx, levels[nz_idx], positions[nz_idx]);
    }

   
    for (nz_idx = 0; nz_idx < nz_count; nz_idx++) {
        qcoeff_zigzag[positions[nz_idx]] = levels[nz_idx];
    }

    *coeff_count = nz_count;
    return 0;
}