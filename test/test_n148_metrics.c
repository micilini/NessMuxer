#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../src/common/n148_metrics.h"

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) do { tests_total++; printf("  [TEST] %-55s", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL (%s)\n", msg); return 1; } while(0)

static int test_psnr_identical(void)
{
    uint8_t buf[64 * 64];
    double psnr;
    int i;

    TEST("PSNR of identical frames is very high");

    for (i = 0; i < 64 * 64; i++) buf[i] = (uint8_t)(i & 0xFF);

    psnr = n148_psnr(buf, buf, 64, 64, 64, 64);
    if (psnr < 90.0) FAIL("PSNR should be ~99 for identical");

    printf(" [%.1fdB]", psnr);
    PASS();
    return 0;
}

static int test_psnr_different(void)
{
    uint8_t orig[64 * 64];
    uint8_t recon[64 * 64];
    double psnr;
    int i;

    TEST("PSNR of noisy frame is finite and reasonable");

    for (i = 0; i < 64 * 64; i++) {
        orig[i] = (uint8_t)(i & 0xFF);
        recon[i] = (uint8_t)((i + 10) & 0xFF);
    }

    psnr = n148_psnr(orig, recon, 64, 64, 64, 64);
    if (psnr < 10.0 || psnr > 60.0) FAIL("PSNR out of expected range");

    printf(" [%.1fdB]", psnr);
    PASS();
    return 0;
}

static int test_ssim_identical(void)
{
    uint8_t buf[64 * 64];
    double ssim;
    int i;

    TEST("SSIM of identical frames is ~1.0");

    for (i = 0; i < 64 * 64; i++) buf[i] = (uint8_t)(i & 0xFF);

    ssim = n148_ssim(buf, buf, 64, 64, 64, 64);
    if (ssim < 0.99) FAIL("SSIM should be ~1.0 for identical");

    printf(" [%.4f]", ssim);
    PASS();
    return 0;
}

static int test_ssim_different(void)
{
    uint8_t orig[64 * 64];
    uint8_t recon[64 * 64];
    double ssim;
    int i;

    TEST("SSIM of noisy frame is < 1.0");

    for (i = 0; i < 64 * 64; i++) {
        orig[i] = (uint8_t)(i & 0xFF);
        recon[i] = (uint8_t)((i + 30) & 0xFF);
    }

    ssim = n148_ssim(orig, recon, 64, 64, 64, 64);
    if (ssim >= 1.0) FAIL("SSIM should be < 1.0");
    if (ssim < 0.0) FAIL("SSIM should be >= 0.0");

    printf(" [%.4f]", ssim);
    PASS();
    return 0;
}

int main(void)
{
    int rc = 0;

    printf("=== N.148 Metrics Test (Fase 8.3) ===\n\n");

    if ((rc = test_psnr_identical()) != 0) return rc;
    if ((rc = test_psnr_different()) != 0) return rc;
    if ((rc = test_ssim_identical()) != 0) return rc;
    if ((rc = test_ssim_different()) != 0) return rc;

    printf("\n=== Resultado: %d/%d testes passaram ===\n",
           tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}