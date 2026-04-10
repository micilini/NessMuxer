#include "n148_cabac_engine.h"
#include "n148_cabac_tables.h"
#include <stdio.h>

#define N148_CABAC_TOP        0xFFFFFFFFu
#define N148_CABAC_HALF       0x80000000u
#define N148_CABAC_FIRST_QTR  0x40000000u
#define N148_CABAC_THIRD_QTR  0xC0000000u
#define N148_CABAC_BOOT_BITS  32

#ifndef N148_CABAC_TRACE
#define N148_CABAC_TRACE 0
#endif

#if N148_CABAC_TRACE
#define CABAC_LOG(fmt, ...) \
    fprintf(stderr, "[N148 CABAC][ENGINE] " fmt "\n", ##__VA_ARGS__)
#else
#define CABAC_LOG(fmt, ...) do { } while (0)
#endif

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

static uint32_t n148_cabac_prob_lps(uint8_t state)
{
    uint32_t p = (uint32_t)n148_cabac_estimate_lps_range(state, 256u);

    if (p == 0u)
        p = 1u;
    if (p >= 256u)
        p = 255u;

    return p;
}

static int n148_cabac_renorm_enc(N148CabacCore* c, N148BsWriter* bs)
{
    if (!c || !bs)
        return -1;

    for (;;) {
        if (c->range < N148_CABAC_HALF) {
            CABAC_LOG("renorm_enc E1 low=%u high=%u pending=%d", c->low, c->range, c->pending_bits);
            if (n148_cabac_put_bit_plus_pending(bs, 0u, &c->pending_bits) != 0)
                return -1;
        } else if (c->low >= N148_CABAC_HALF) {
            CABAC_LOG("renorm_enc E2 low=%u high=%u pending=%d", c->low, c->range, c->pending_bits);
            if (n148_cabac_put_bit_plus_pending(bs, 1u, &c->pending_bits) != 0)
                return -1;
            c->low -= N148_CABAC_HALF;
            c->range -= N148_CABAC_HALF;
        } else if (c->low >= N148_CABAC_FIRST_QTR && c->range < N148_CABAC_THIRD_QTR) {
            CABAC_LOG("renorm_enc E3 low=%u high=%u pending=%d", c->low, c->range, c->pending_bits);
            c->pending_bits++;
            c->low -= N148_CABAC_FIRST_QTR;
            c->range -= N148_CABAC_FIRST_QTR;
        } else {
            break;
        }

        c->low <<= 1;
        c->range = (c->range << 1) | 1u;
        CABAC_LOG("renorm_enc post low=%u high=%u pending=%d", c->low, c->range, c->pending_bits);
    }

    return 0;
}

static int n148_cabac_renorm_dec(N148CabacCore* c, N148BsReader* bs)
{
    uint32_t bit = 0;

    if (!c || !bs)
        return -1;

    for (;;) {
        if (c->range < N148_CABAC_HALF) {
            CABAC_LOG("renorm_dec E1 low=%u high=%u code=%u", c->low, c->range, c->code);
        } else if (c->low >= N148_CABAC_HALF) {
            CABAC_LOG("renorm_dec E2 low=%u high=%u code=%u", c->low, c->range, c->code);
            c->low -= N148_CABAC_HALF;
            c->range -= N148_CABAC_HALF;
            c->code -= N148_CABAC_HALF;
        } else if (c->low >= N148_CABAC_FIRST_QTR && c->range < N148_CABAC_THIRD_QTR) {
            CABAC_LOG("renorm_dec E3 low=%u high=%u code=%u", c->low, c->range, c->code);
            c->low -= N148_CABAC_FIRST_QTR;
            c->range -= N148_CABAC_FIRST_QTR;
            c->code -= N148_CABAC_FIRST_QTR;
        } else {
            break;
        }

        c->low <<= 1;
        c->range = (c->range << 1) | 1u;
        c->code <<= 1;

        if (!n148_bs_eof(bs)) {
            if (n148_bs_read_bits(bs, 1, &bit) != 0)
                return -1;
            c->code |= (bit & 1u);
        }

        CABAC_LOG("renorm_dec post low=%u high=%u code=%u bit=%u", c->low, c->range, c->code, bit & 1u);
    }

    return 0;
}

static int n148_cabac_encode_with_prob(N148CabacCore* c, N148BsWriter* bs, uint32_t bin, uint32_t p1)
{
    uint64_t span;
    uint64_t split;
    uint32_t low_before;
    uint32_t high_before;

    if (!c || !bs)
        return -1;

    low_before = c->low;
    high_before = c->range;

    span = (uint64_t)c->range - (uint64_t)c->low + 1u;
    split = (uint64_t)c->low + ((span * (256u - p1)) >> 8) - 1u;

    if (split < c->low)
        split = c->low;
    if (split >= c->range)
        split = c->range - 1u;

    if (bin & 1u)
        c->low = (uint32_t)(split + 1u);
    else
        c->range = (uint32_t)split;

    CABAC_LOG("enc_prob bin=%u p1=%u low:%u->%u high:%u->%u split=%u",
              bin & 1u, p1, low_before, c->low, high_before, c->range, (unsigned)split);

    return n148_cabac_renorm_enc(c, bs);
}

static int n148_cabac_decode_with_prob(N148CabacCore* c, N148BsReader* bs, uint32_t* bin, uint32_t p1)
{
    uint64_t span;
    uint64_t split;
    uint32_t low_before;
    uint32_t high_before;
    uint32_t code_before;

    if (!c || !bs || !bin)
        return -1;

    low_before = c->low;
    high_before = c->range;
    code_before = c->code;

    span = (uint64_t)c->range - (uint64_t)c->low + 1u;
    split = (uint64_t)c->low + ((span * (256u - p1)) >> 8) - 1u;

    if (split < c->low)
        split = c->low;
    if (split >= c->range)
        split = c->range - 1u;

    if ((uint64_t)c->code <= split) {
        *bin = 0u;
        c->range = (uint32_t)split;
    } else {
        *bin = 1u;
        c->low = (uint32_t)(split + 1u);
    }

    CABAC_LOG("dec_prob out=%u p1=%u low:%u->%u high:%u->%u code:%u split=%u",
              *bin, p1, low_before, c->low, high_before, c->range, code_before, (unsigned)split);

    return n148_cabac_renorm_dec(c, bs);
}

void n148_cabac_core_init_enc(N148CabacCore* c)
{
    if (!c)
        return;

    c->low = 0u;
    c->range = N148_CABAC_TOP;
    c->code = 0u;
    c->pending_bits = 0;
    c->terminated = 0;

    CABAC_LOG("init_enc low=%u high=%u", c->low, c->range);
}

int n148_cabac_core_init_dec(N148CabacCore* c, N148BsReader* bs)
{
    uint32_t bit = 0;
    int i;

    if (!c || !bs)
        return -1;

    c->low = 0u;
    c->range = N148_CABAC_TOP;
    c->code = 0u;
    c->pending_bits = 0;
    c->terminated = 0;

    for (i = 0; i < N148_CABAC_BOOT_BITS; i++) {
        c->code <<= 1;
        if (!n148_bs_eof(bs)) {
            if (n148_bs_read_bits(bs, 1, &bit) != 0)
                return -1;
            c->code |= (bit & 1u);
        }
    }

    CABAC_LOG("init_dec low=%u high=%u code=%u", c->low, c->range, c->code);
    return 0;
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
    return n148_cabac_encode_with_prob(c, bs, bin & 1u, 128u);
}

int n148_cabac_decode_bin_bypass(N148CabacCore* c, N148BsReader* bs, uint32_t* bin)
{
    return n148_cabac_decode_with_prob(c, bs, bin, 128u);
}

int n148_cabac_encode_bin_ctx(N148CabacCore* c, N148BsWriter* bs, N148CabacContext* ctx, uint32_t bin)
{
    uint32_t p_lps;
    uint32_t p1;
    uint8_t state_before;
    uint8_t mps_before;

    if (!c || !bs || !ctx)
        return -1;

    state_before = ctx->state;
    mps_before = ctx->mps;
    p_lps = n148_cabac_prob_lps(ctx->state);
    p1 = ctx->mps ? (256u - p_lps) : p_lps;

    if (n148_cabac_encode_with_prob(c, bs, bin & 1u, p1) != 0)
        return -1;

    if ((bin & 1u) == ctx->mps) {
        if (ctx->state < 62u)
            ctx->state++;
    } else {
        if (ctx->state == 0u)
            ctx->mps ^= 1u;
        else
            ctx->state--;
    }

    CABAC_LOG("enc_ctx bin=%u state:%u->%u mps:%u->%u p1=%u",
              bin & 1u, state_before, ctx->state, mps_before, ctx->mps, p1);

    return 0;
}

int n148_cabac_decode_bin_ctx(N148CabacCore* c, N148BsReader* bs, N148CabacContext* ctx, uint32_t* bin)
{
    uint32_t p_lps;
    uint32_t p1;
    uint8_t state_before;
    uint8_t mps_before;

    if (!c || !bs || !ctx || !bin)
        return -1;

    state_before = ctx->state;
    mps_before = ctx->mps;
    p_lps = n148_cabac_prob_lps(ctx->state);
    p1 = ctx->mps ? (256u - p_lps) : p_lps;

    if (n148_cabac_decode_with_prob(c, bs, bin, p1) != 0)
        return -1;

    if ((*bin) == ctx->mps) {
        if (ctx->state < 62u)
            ctx->state++;
    } else {
        if (ctx->state == 0u)
            ctx->mps ^= 1u;
        else
            ctx->state--;
    }

    CABAC_LOG("dec_ctx out=%u state:%u->%u mps:%u->%u p1=%u",
              *bin, state_before, ctx->state, mps_before, ctx->mps, p1);

    return 0;
}

int n148_cabac_encode_terminate(N148CabacCore* c, N148BsWriter* bs, uint32_t bin)
{
    int rc;

    if (!c || !bs)
        return -1;

    rc = n148_cabac_encode_with_prob(c, bs, bin ? 1u : 0u, 128u);
    if (rc == 0 && bin)
        c->terminated = 1;

    CABAC_LOG("enc_term bin=%u term=%d", bin & 1u, c->terminated);
    return rc;
}

int n148_cabac_decode_terminate(N148CabacCore* c, N148BsReader* bs, uint32_t* bin)
{
    int rc;

    if (!c || !bs || !bin)
        return -1;

    rc = n148_cabac_decode_with_prob(c, bs, bin, 128u);
    if (rc == 0 && *bin)
        c->terminated = 1;

    CABAC_LOG("dec_term out=%u term=%d", *bin, c->terminated);
    return rc;
}

int n148_cabac_finish_enc(N148CabacCore* c, N148BsWriter* bs)
{
    int i;

    if (!c || !bs)
        return -1;

    CABAC_LOG("finish_enc begin low=%u high=%u pending=%d term=%d",
              c->low, c->range, c->pending_bits, c->terminated);

    if (!c->terminated) {
        if (n148_cabac_encode_terminate(c, bs, 1u) != 0)
            return -1;
    }

    c->pending_bits++;
    if (c->low < N148_CABAC_FIRST_QTR) {
        if (n148_cabac_put_bit_plus_pending(bs, 0u, &c->pending_bits) != 0)
            return -1;
    } else {
        if (n148_cabac_put_bit_plus_pending(bs, 1u, &c->pending_bits) != 0)
            return -1;
    }

    for (i = 0; i < 31; i++) {
        uint32_t bit = (c->low & N148_CABAC_HALF) ? 1u : 0u;
        if (n148_bs_write_bits(bs, 1, bit) != 0)
            return -1;
        CABAC_LOG("finish_enc tail[%d]=%u low=%u high=%u", i, bit, c->low, c->range);
        c->low <<= 1;
    }

    CABAC_LOG("finish_enc end low=%u high=%u pending=%d", c->low, c->range, c->pending_bits);
    return 0;
}