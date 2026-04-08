#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/common/entropy/n148_cabac.h"
#include "../src/common/entropy/n148_binarization.h"
#include "../src/common/entropy/n148_contexts.h"
#include "../src/codec/n148/n148_bitstream.h"

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) do { \
    tests_total++; \
    printf("  [TEST] %-50s", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL (%s)\n", msg); return 1; } while(0)

static int test_signed_mag_roundtrip(void)
{
    uint8_t buf[128] = {0};
    N148BsWriter wr;
    N148BsReader rd;
    int32_t in_vals[]  = {0, 1, -1, 2, -2, 17, -17, 255, -255};
    int32_t out = 0;
    int i;

    TEST("binarization signed_mag roundtrip");

    n148_bs_writer_init(&wr, buf, (int)sizeof(buf));
    for (i = 0; i < (int)(sizeof(in_vals)/sizeof(in_vals[0])); i++) {
        if (n148_bin_write_signed_mag(&wr, in_vals[i]) != 0)
            FAIL("write_signed_mag");
    }
    if (n148_bs_flush(&wr) != 0)
        FAIL("flush");

    n148_bs_reader_init(&rd, buf, n148_bs_writer_bytes_written(&wr));
    for (i = 0; i < (int)(sizeof(in_vals)/sizeof(in_vals[0])); i++) {
        if (n148_bin_read_signed_mag(&rd, &out) != 0)
            FAIL("read_signed_mag");
        if (out != in_vals[i])
            FAIL("signed_mag mismatch");
    }

    PASS();
    return 0;
}

static int test_mv_roundtrip(void)
{
    uint8_t buf[128] = {0};
    N148BsWriter wr;
    N148BsReader rd;
    int mvx = 7;
    int mvy = -3;
    int mvx_out = 0;
    int mvy_out = 0;

    TEST("CABAC public MV roundtrip");

    n148_bs_writer_init(&wr, buf, (int)sizeof(buf));
    if (n148_cabac_write_mv(&wr, mvx, mvy) != 0)
        FAIL("write_mv");
    if (n148_bs_flush(&wr) != 0)
        FAIL("flush");

    n148_bs_reader_init(&rd, buf, n148_bs_writer_bytes_written(&wr));
    if (n148_cabac_read_mv(&rd, &mvx_out, &mvy_out) != 0)
        FAIL("read_mv");
    if (mvx_out != mvx || mvy_out != mvy)
        FAIL("mv mismatch");

    PASS();
    return 0;
}

static int test_block_roundtrip(void)
{
    uint8_t buf[128] = {0};
    N148BsWriter wr;
    N148BsReader rd;
    int16_t in_coeffs[16]  = {12, -4, 0, 3, -1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0};
    int16_t out_coeffs[16] = {0};
    int coeff_count = 0;
    int i;

    TEST("CABAC public block roundtrip");

    n148_bs_writer_init(&wr, buf, (int)sizeof(buf));
    if (n148_cabac_write_block(&wr, in_coeffs, 16) != 0)
        FAIL("write_block");
    if (n148_bs_flush(&wr) != 0)
        FAIL("flush");

    n148_bs_reader_init(&rd, buf, n148_bs_writer_bytes_written(&wr));
    if (n148_cabac_read_block(&rd, out_coeffs, &coeff_count, 16) != 0)
        FAIL("read_block");
    if (coeff_count != 16)
        FAIL("coeff_count");

    for (i = 0; i < 16; i++) {
        if (out_coeffs[i] != in_coeffs[i])
            FAIL("coeff mismatch");
    }

    PASS();
    return 0;
}

static int test_core_smoke(void)
{
    N148CabacCore core;
    N148CabacContext ctx;

    TEST("CABAC core/context smoke init");

    n148_cabac_core_init_enc(&core);
    n148_cabac_context_init(&ctx, N148_CABAC_STATE_NEUTRAL, N148_CABAC_MPS_ZERO);

    if (core.range == 0)
        FAIL("core range");
    if (ctx.state != N148_CABAC_STATE_NEUTRAL)
        FAIL("ctx state");

    PASS();
    return 0;
}

int main(void)
{
    int rc = 0;

    printf("=== NessMuxer - Teste Fase 7 (CABAC/CAVLC dual-mode) ===\n\n");

    if ((rc = test_signed_mag_roundtrip()) != 0) return rc;
    if ((rc = test_mv_roundtrip()) != 0) return 4;
    if ((rc = test_block_roundtrip()) != 0) return 5;
    if ((rc = test_core_smoke()) != 0) return 6;

    printf("\n=== Resultado: %d/%d testes passaram ===\n", tests_passed, tests_total);
    return 0;
}
