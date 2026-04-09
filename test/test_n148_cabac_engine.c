#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/common/entropy/n148_cabac_engine.h"
#include "../src/codec/n148/n148_bitstream.h"

static int test_ctx_roundtrip(void)
{
    uint8_t buf[128] = {0};
    N148BsWriter wr;
    N148BsReader rd;
    N148CabacCore enc, dec;
    N148CabacContext enc_ctx, dec_ctx;
    uint32_t bits_in[]  = {1,0,1,1,0,0,1,0,1,1,1,0};
    uint32_t bit_out = 0;
    int i;

    n148_bs_writer_init(&wr, buf, (int)sizeof(buf));
    n148_cabac_core_init_enc(&enc);
    n148_cabac_context_init(&enc_ctx, 32, 0);

    for (i = 0; i < (int)(sizeof(bits_in)/sizeof(bits_in[0])); i++) {
        if (n148_cabac_encode_bin_ctx(&enc, &wr, &enc_ctx, bits_in[i]) != 0)
            return 1;
    }

    if (n148_cabac_finish_enc(&enc, &wr) != 0)
        return 2;
    if (n148_bs_flush(&wr) != 0)
        return 3;

    n148_bs_reader_init(&rd, buf, n148_bs_writer_bytes_written(&wr));
    if (n148_cabac_core_init_dec(&dec, &rd) != 0)
        return 4;
    n148_cabac_context_init(&dec_ctx, 32, 0);

    for (i = 0; i < (int)(sizeof(bits_in)/sizeof(bits_in[0])); i++) {
        if (n148_cabac_decode_bin_ctx(&dec, &rd, &dec_ctx, &bit_out) != 0)
            return 5;
        if (bit_out != bits_in[i])
            return 6;
    }

    return 0;
}

static int test_bypass_roundtrip(void)
{
    uint8_t buf[128] = {0};
    N148BsWriter wr;
    N148BsReader rd;
    N148CabacCore enc, dec;
    uint32_t bits_in[]  = {1,1,0,1,0,0,1,1,0,1,1,0,0,1};
    uint32_t bit_out = 0;
    int i;

    n148_bs_writer_init(&wr, buf, (int)sizeof(buf));
    n148_cabac_core_init_enc(&enc);

    for (i = 0; i < (int)(sizeof(bits_in)/sizeof(bits_in[0])); i++) {
        if (n148_cabac_encode_bin_bypass(&enc, &wr, bits_in[i]) != 0)
            return 10;
    }

    if (n148_cabac_finish_enc(&enc, &wr) != 0)
        return 11;
    if (n148_bs_flush(&wr) != 0)
        return 12;

    n148_bs_reader_init(&rd, buf, n148_bs_writer_bytes_written(&wr));
    if (n148_cabac_core_init_dec(&dec, &rd) != 0)
        return 13;

    for (i = 0; i < (int)(sizeof(bits_in)/sizeof(bits_in[0])); i++) {
        if (n148_cabac_decode_bin_bypass(&dec, &rd, &bit_out) != 0)
            return 14;
        if (bit_out != bits_in[i])
            return 15;
    }

    return 0;
}

static int test_mixed_roundtrip(void)
{
    uint8_t buf[128] = {0};
    N148BsWriter wr;
    N148BsReader rd;
    N148CabacCore enc, dec;
    N148CabacContext enc_ctx, dec_ctx;
    uint32_t ctx_bits[] = {1, 0, 1, 1, 0};
    uint32_t bypass_bits[] = {1, 0, 1, 1, 0, 0};
    uint32_t bit_out = 0;
    uint32_t term = 0;
    int i;

    n148_bs_writer_init(&wr, buf, (int)sizeof(buf));
    n148_cabac_core_init_enc(&enc);
    n148_cabac_context_init(&enc_ctx, 32, 0);

    for (i = 0; i < 3; i++) {
        if (n148_cabac_encode_bin_ctx(&enc, &wr, &enc_ctx, ctx_bits[i]) != 0)
            return 20;
    }

    for (i = 0; i < 5; i++) {
        if (n148_cabac_encode_bin_bypass(&enc, &wr, bypass_bits[i]) != 0)
            return 21;
    }

    for (i = 3; i < 5; i++) {
        if (n148_cabac_encode_bin_ctx(&enc, &wr, &enc_ctx, ctx_bits[i]) != 0)
            return 22;
    }

    if (n148_cabac_finish_enc(&enc, &wr) != 0)
        return 23;
    if (n148_bs_flush(&wr) != 0)
        return 24;

    n148_bs_reader_init(&rd, buf, n148_bs_writer_bytes_written(&wr));
    if (n148_cabac_core_init_dec(&dec, &rd) != 0)
        return 25;
    n148_cabac_context_init(&dec_ctx, 32, 0);

    for (i = 0; i < 3; i++) {
        if (n148_cabac_decode_bin_ctx(&dec, &rd, &dec_ctx, &bit_out) != 0)
            return 26;
        if (bit_out != ctx_bits[i])
            return 27;
    }

    for (i = 0; i < 5; i++) {
        if (n148_cabac_decode_bin_bypass(&dec, &rd, &bit_out) != 0)
            return 28;
        if (bit_out != bypass_bits[i])
            return 29;
    }

    for (i = 3; i < 5; i++) {
        if (n148_cabac_decode_bin_ctx(&dec, &rd, &dec_ctx, &bit_out) != 0)
            return 30;
        if (bit_out != ctx_bits[i])
            return 31;
    }

    if (n148_cabac_decode_terminate(&dec, &rd, &term) != 0)
        return 32;
    if (term != 1u)
        return 33;

    return 0;
}

int main(void)
{
    printf("=== N.148 CABAC Engine Test (7R.3 / B2) ===\n");

    if (test_ctx_roundtrip() != 0) {
        printf("  [FAIL] CABAC engine ctx roundtrip\n");
        return 1;
    }

    if (test_bypass_roundtrip() != 0) {
        printf("  [FAIL] CABAC engine bypass roundtrip\n");
        return 2;
    }

    if (test_mixed_roundtrip() != 0) {
        printf("  [FAIL] CABAC engine mixed roundtrip\n");
        return 3;
    }

    printf("  [PASS] CABAC engine ctx roundtrip\n");
    printf("  [PASS] CABAC engine bypass roundtrip\n");
    printf("  [PASS] CABAC engine mixed roundtrip\n");
    return 0;
}