#include "n148_cabac.h"
#include "n148_binarization.h"
#include "n148_contexts.h"

#include <stdio.h>
#include <string.h>

#define N148_CABAC_TRACE(fmt, ...) \
    fprintf(stderr, "[N148 CABAC] " fmt "\n", ##__VA_ARGS__)

#define N148_CABAC_TOP      0x01u
#define N148_CABAC_INIT_LOW 0x00000000u
#define N148_CABAC_INIT_RNG 0x1FEu

static int n148_cabac_put_bit_plus_pending(N148BsWriter* bs, uint32_t bit, int* pending_bits)
{
    int i;

    if (!bs || !pending_bits)
        return -1;

    if (n148_bs_write_bits(bs, 1, bit ? 1u : 0u) != 0)
        return -1;

    for (i = 0; i < *pending_bits; i++) {
        if (n148_bs_write_bits(bs, 1, bit ? 0u : 1u) != 0)
            return -1;
    }

    *pending_bits = 0;
    return 0;
}

static int n148_cabac_renorm_enc(N148CabacCore* c, N148BsWriter* bs)
{
    if (!c || !bs)
        return -1;

    while (c->range < 256u) {
        if (c->low < 256u) {
            if (n148_cabac_put_bit_plus_pending(bs, 0u, &c->pending_bits) != 0)
                return -1;
        } else if (c->low >= 512u) {
            c->low -= 512u;
            if (n148_cabac_put_bit_plus_pending(bs, 1u, &c->pending_bits) != 0)
                return -1;
        } else {
            c->low -= 256u;
            c->pending_bits++;
        }
        c->range <<= 1;
        c->low <<= 1;
    }

    return 0;
}

static int n148_cabac_renorm_dec(N148CabacCore* c, N148BsReader* bs)
{
    uint32_t bit = 0;

    if (!c || !bs)
        return -1;

    while (c->range < 256u) {
        c->range <<= 1;
        c->code <<= 1;
        if (n148_bs_read_bits(bs, 1, &bit) != 0)
            return -1;
        c->code |= (bit & 1u);
    }

    return 0;
}

void n148_cabac_core_init_enc(N148CabacCore* c)
{
    if (!c)
        return;

    c->low = N148_CABAC_INIT_LOW;
    c->range = N148_CABAC_INIT_RNG;
    c->code = 0;
    c->pending_bits = 0;
}

void n148_cabac_core_init_dec(N148CabacCore* c, N148BsReader* bs)
{
    uint32_t bit = 0;
    int i;

    if (!c || !bs)
        return;

    c->low = N148_CABAC_INIT_LOW;
    c->range = N148_CABAC_INIT_RNG;
    c->code = 0;
    c->pending_bits = 0;

    for (i = 0; i < 9; i++) {
        c->code <<= 1;
        if (n148_bs_read_bits(bs, 1, &bit) != 0)
            break;
        c->code |= (bit & 1u);
    }
}

void n148_cabac_context_init(N148CabacContext* ctx, uint8_t state, uint8_t mps)
{
    if (!ctx)
        return;

    ctx->state = state;
    ctx->mps = mps ? 1u : 0u;
}

int n148_cabac_encode_bin_bypass(N148CabacCore* c, N148BsWriter* bs, uint32_t bin)
{
    if (!c || !bs)
        return -1;

    c->low <<= 1;
    if (bin)
        c->low += c->range;

    if (c->low < 256u) {
        if (n148_cabac_put_bit_plus_pending(bs, 0u, &c->pending_bits) != 0)
            return -1;
    } else if (c->low >= 512u) {
        c->low -= 512u;
        if (n148_cabac_put_bit_plus_pending(bs, 1u, &c->pending_bits) != 0)
            return -1;
    } else {
        c->low -= 256u;
        c->pending_bits++;
    }

    return 0;
}

int n148_cabac_decode_bin_bypass(N148CabacCore* c, N148BsReader* bs, uint32_t* bin)
{
    uint32_t bit = 0;

    if (!c || !bs || !bin)
        return -1;

    c->code <<= 1;
    if (n148_bs_read_bits(bs, 1, &bit) != 0)
        return -1;
    c->code |= (bit & 1u);

    if (c->code >= c->range) {
        *bin = 1u;
        c->code -= c->range;
    } else {
        *bin = 0u;
    }

    return 0;
}

int n148_cabac_encode_bin_ctx(N148CabacCore* c, N148BsWriter* bs, N148CabacContext* ctx, uint32_t bin)
{
    uint32_t lps_range;

    if (!c || !bs || !ctx)
        return -1;

    lps_range = 2u + ((uint32_t)(63 - (ctx->state & 63u)) >> 2);
    if (lps_range >= c->range)
        lps_range = c->range >> 1;

    if (bin == ctx->mps) {
        c->range -= lps_range;
        if (ctx->state < 62u)
            ctx->state++;
    } else {
        c->low += (c->range - lps_range);
        c->range = lps_range;

        if (ctx->state == 0u) {
            ctx->mps ^= 1u;
        } else {
            ctx->state--;
        }
    }

    return n148_cabac_renorm_enc(c, bs);
}

int n148_cabac_decode_bin_ctx(N148CabacCore* c, N148BsReader* bs, N148CabacContext* ctx, uint32_t* bin)
{
    uint32_t lps_range;
    uint32_t split;

    if (!c || !bs || !ctx || !bin)
        return -1;

    lps_range = 2u + ((uint32_t)(63 - (ctx->state & 63u)) >> 2);
    if (lps_range >= c->range)
        lps_range = c->range >> 1;

    split = c->range - lps_range;

    if (c->code < split) {
        *bin = ctx->mps;
        c->range = split;
        if (ctx->state < 62u)
            ctx->state++;
    } else {
        *bin = ctx->mps ^ 1u;
        c->code -= split;
        c->range = lps_range;

        if (ctx->state == 0u) {
            ctx->mps ^= 1u;
        } else {
            ctx->state--;
        }
    }

    return n148_cabac_renorm_dec(c, bs);
}

int n148_cabac_finish_enc(N148CabacCore* c, N148BsWriter* bs)
{
    int i;

    if (!c || !bs)
        return -1;

    for (i = 0; i < 10; i++) {
        if (c->low < 256u) {
            if (n148_cabac_put_bit_plus_pending(bs, 0u, &c->pending_bits) != 0)
                return -1;
        } else if (c->low >= 512u) {
            c->low -= 512u;
            if (n148_cabac_put_bit_plus_pending(bs, 1u, &c->pending_bits) != 0)
                return -1;
        } else {
            c->low -= 256u;
            c->pending_bits++;
        }
        c->low <<= 1;
    }

    if (n148_cabac_put_bit_plus_pending(bs, 0u, &c->pending_bits) != 0)
        return -1;

    return 0;
}

static int n148_cabac_write_unary_ctx(N148CabacCore* c, N148BsWriter* bs, uint32_t value, int ctx_id)
{
    N148CabacContext ctx;
    uint32_t i;

    n148_cabac_context_init(&ctx, 10u + (uint8_t)ctx_id, 1u);

    for (i = 0; i < value; i++) {
        if (n148_cabac_encode_bin_ctx(c, bs, &ctx, 0u) != 0)
            return -1;
    }

    return n148_cabac_encode_bin_ctx(c, bs, &ctx, 1u);
}

static int n148_cabac_read_unary_ctx(N148CabacCore* c, N148BsReader* bs, uint32_t* value, int ctx_id)
{
    N148CabacContext ctx;
    uint32_t bin = 0;
    uint32_t v = 0;

    if (!value)
        return -1;

    n148_cabac_context_init(&ctx, 10u + (uint8_t)ctx_id, 1u);

    for (;;) {
        if (n148_cabac_decode_bin_ctx(c, bs, &ctx, &bin) != 0)
            return -1;
        if (bin)
            break;
        v++;
        if (v > 1024u)
            return -1;
    }

    *value = v;
    return 0;
}

static int n148_cabac_write_signed_mag_core(N148CabacCore* c, N148BsWriter* bs, int32_t value, int ctx_id)
{
    uint32_t mag;
    N148CabacContext sig_ctx;
    N148CabacContext sign_ctx;

    n148_cabac_context_init(&sig_ctx, 20u + (uint8_t)ctx_id, 0u);
    n148_cabac_context_init(&sign_ctx, 30u + (uint8_t)ctx_id, 0u);

    if (value == 0)
        return n148_cabac_encode_bin_ctx(c, bs, &sig_ctx, 0u);

    if (n148_cabac_encode_bin_ctx(c, bs, &sig_ctx, 1u) != 0)
        return -1;
    if (n148_cabac_encode_bin_ctx(c, bs, &sign_ctx, value < 0 ? 1u : 0u) != 0)
        return -1;

    mag = (uint32_t)((value < 0) ? -value : value);
    return n148_cabac_write_unary_ctx(c, bs, mag - 1u, N148_CTX_COEFF_MAG);
}

static int n148_cabac_read_signed_mag_core(N148CabacCore* c, N148BsReader* bs, int32_t* out, int ctx_id)
{
    uint32_t sig = 0;
    uint32_t sign = 0;
    uint32_t magm1 = 0;
    N148CabacContext sig_ctx;
    N148CabacContext sign_ctx;

    if (!out)
        return -1;

    n148_cabac_context_init(&sig_ctx, 20u + (uint8_t)ctx_id, 0u);
    n148_cabac_context_init(&sign_ctx, 30u + (uint8_t)ctx_id, 0u);

    if (n148_cabac_decode_bin_ctx(c, bs, &sig_ctx, &sig) != 0)
        return -1;

    if (!sig) {
        *out = 0;
        return 0;
    }

    if (n148_cabac_decode_bin_ctx(c, bs, &sign_ctx, &sign) != 0)
        return -1;
    if (n148_cabac_read_unary_ctx(c, bs, &magm1, N148_CTX_COEFF_MAG) != 0)
        return -1;

    *out = (int32_t)(magm1 + 1u);
    if (sign)
        *out = -*out;

    return 0;
}

int n148_cabac_write_mv(N148CabacSession* s, N148BsWriter* bs, int mvx, int mvy)
{
    if (!s || !bs)
        return -1;

    N148_CABAC_TRACE("write_mv begin: mvx=%d mvy=%d", mvx, mvy);

    if (n148_cabac_write_signed_mag_core(&s->core, bs, (int32_t)mvx,
                                          N148_CTX_MV_X) != 0)
        return -1;
    if (n148_cabac_write_signed_mag_core(&s->core, bs, (int32_t)mvy,
                                          N148_CTX_MV_Y) != 0)
        return -1;

    return 0;
}

int n148_cabac_read_mv(N148CabacSession* s, N148BsReader* bs, int* mvx, int* mvy)
{
    int32_t x = 0, y = 0;

    if (!s || !bs || !mvx || !mvy)
        return -1;

    if (n148_cabac_read_signed_mag_core(&s->core, bs, &x, N148_CTX_MV_X) != 0)
        return -1;
    if (n148_cabac_read_signed_mag_core(&s->core, bs, &y, N148_CTX_MV_Y) != 0)
        return -1;

    *mvx = (int)x;
    *mvy = (int)y;

    N148_CABAC_TRACE("read_mv: mvx=%d mvy=%d", *mvx, *mvy);
    return 0;
}

int n148_cabac_write_block(N148CabacSession* s,
                           N148BsWriter* bs,
                           const int16_t* qcoeff_zigzag,
                           int coeff_count)
{
    int i;

    if (!s || !bs || !qcoeff_zigzag || coeff_count < 0 || coeff_count > 16)
        return -1;

    N148_CABAC_TRACE("write_block begin: coeff_count=%d", coeff_count);

    /* coeff_count via truncated unary com contexto */
    if (n148_cabac_write_unary_ctx(&s->core, bs, (uint32_t)coeff_count,
                                    N148_CTX_COEFF_CNT) != 0)
        return -1;

    for (i = 0; i < coeff_count; i++) {
        if (n148_cabac_write_signed_mag_core(&s->core, bs,
                                              (int32_t)qcoeff_zigzag[i],
                                              N148_CTX_COEFF_SIG) != 0)
            return -1;
    }

    return 0;
}

int n148_cabac_read_block(N148CabacSession* s,
                          N148BsReader* bs,
                          int16_t* qcoeff_zigzag,
                          int* coeff_count,
                          int max_coeffs)
{
    uint32_t count = 0;
    int i;
    int32_t level = 0;

    if (!s || !bs || !qcoeff_zigzag || !coeff_count || max_coeffs <= 0)
        return -1;

    memset(qcoeff_zigzag, 0, (size_t)max_coeffs * sizeof(qcoeff_zigzag[0]));

    if (n148_cabac_read_unary_ctx(&s->core, bs, &count, N148_CTX_COEFF_CNT) != 0)
        return -1;
    if ((int)count > max_coeffs)
        return -1;

    for (i = 0; i < (int)count; i++) {
        if (n148_cabac_read_signed_mag_core(&s->core, bs, &level,
                                             N148_CTX_COEFF_SIG) != 0)
            return -1;
        qcoeff_zigzag[i] = (int16_t)level;
    }

    *coeff_count = (int)count;
    return 0;
}

/* ---- Session management ---- */

void n148_cabac_session_init_enc(N148CabacSession* s)
{
    if (!s) return;
    n148_cabac_core_init_enc(&s->core);
    n148_cabac_context_init(&s->ctx_coeff_sig,  N148_CABAC_STATE_NEUTRAL, 0);
    n148_cabac_context_init(&s->ctx_coeff_sign, N148_CABAC_STATE_NEUTRAL, 0);
    n148_cabac_context_init(&s->ctx_coeff_mag,  N148_CABAC_STATE_NEUTRAL, 1);
    n148_cabac_context_init(&s->ctx_mv_sig,     N148_CABAC_STATE_NEUTRAL, 0);
    n148_cabac_context_init(&s->ctx_mv_sign,    N148_CABAC_STATE_NEUTRAL, 0);
    n148_cabac_context_init(&s->ctx_mv_mag,     N148_CABAC_STATE_NEUTRAL, 1);
    n148_cabac_context_init(&s->ctx_count,      N148_CABAC_STATE_NEUTRAL, 1);
}

void n148_cabac_session_init_dec(N148CabacSession* s, N148BsReader* bs)
{
    if (!s || !bs) return;
    n148_cabac_core_init_dec(&s->core, bs);
    n148_cabac_context_init(&s->ctx_coeff_sig,  N148_CABAC_STATE_NEUTRAL, 0);
    n148_cabac_context_init(&s->ctx_coeff_sign, N148_CABAC_STATE_NEUTRAL, 0);
    n148_cabac_context_init(&s->ctx_coeff_mag,  N148_CABAC_STATE_NEUTRAL, 1);
    n148_cabac_context_init(&s->ctx_mv_sig,     N148_CABAC_STATE_NEUTRAL, 0);
    n148_cabac_context_init(&s->ctx_mv_sign,    N148_CABAC_STATE_NEUTRAL, 0);
    n148_cabac_context_init(&s->ctx_mv_mag,     N148_CABAC_STATE_NEUTRAL, 1);
    n148_cabac_context_init(&s->ctx_count,      N148_CABAC_STATE_NEUTRAL, 1);
}

int n148_cabac_session_finish_enc(N148CabacSession* s, N148BsWriter* bs)
{
    if (!s || !bs) return -1;
    return n148_cabac_finish_enc(&s->core, bs);
}