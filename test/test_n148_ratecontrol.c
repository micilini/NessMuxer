#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../src/encoder/n148/n148_ratecontrol.h"

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) do { tests_total++; printf("  [TEST] %-55s", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL (%s)\n", msg); return 1; } while(0)

static int test_cqp_mode(void)
{
    N148RateControl rc;
    int qp_i, qp_p, qp_b;

    TEST("CQP mode returns stable QP per frame type");

    n148_rc_init(&rc, N148_RC_CQP, 320, 240, 30, 2000, 22);
    qp_i = n148_rc_get_frame_qp(&rc, 1);
    qp_p = n148_rc_get_frame_qp(&rc, 2);
    qp_b = n148_rc_get_frame_qp(&rc, 3);

    if (qp_i != 22) FAIL("CQP I-frame QP wrong");
    if (qp_p != 24) FAIL("CQP P-frame QP wrong");
    if (qp_b != 26) FAIL("CQP B-frame QP wrong");

    PASS();
    return 0;
}

static int test_abr_adapts(void)
{
    N148RateControl rc;
    int qp_before, qp_after;
    int i;

    TEST("ABR adapts QP when overshooting bitrate");

    n148_rc_init(&rc, N148_RC_ABR, 320, 240, 30, 1000, 22);

    qp_before = n148_rc_get_frame_qp(&rc, 2);

   
    for (i = 0; i < 30; i++) {
        n148_rc_get_frame_qp(&rc, 2);
        n148_rc_update(&rc, 2,
            (int)(rc.bits_per_frame_target * 2.0), 100.0);
    }

    qp_after = n148_rc_get_frame_qp(&rc, 2);

    if (qp_after <= qp_before) FAIL("QP should increase after overshoot");

    printf(" [qp %d->%d]", qp_before, qp_after);
    PASS();
    return 0;
}

static int test_abr_undershoot(void)
{
    N148RateControl rc;
    int qp_before, qp_after;
    int i;

    TEST("ABR lowers QP when undershooting bitrate");

    n148_rc_init(&rc, N148_RC_ABR, 320, 240, 30, 4000, 28);

    qp_before = n148_rc_get_frame_qp(&rc, 2);

   
    for (i = 0; i < 30; i++) {
        n148_rc_get_frame_qp(&rc, 2);
        n148_rc_update(&rc, 2,
            (int)(rc.bits_per_frame_target * 0.3), 50.0);
    }

    qp_after = n148_rc_get_frame_qp(&rc, 2);

    if (qp_after >= qp_before) FAIL("QP should decrease after undershoot");

    printf(" [qp %d->%d]", qp_before, qp_after);
    PASS();
    return 0;
}

static int test_vbv_buffer(void)
{
    N148RateControl rc;
    double fill;

    TEST("VBV buffer tracks fullness correctly");

    n148_rc_init(&rc, N148_RC_ABR, 320, 240, 30, 2000, 22);
    n148_rc_set_vbv(&rc, 2000, 3000);

    fill = n148_rc_get_buffer_fullness(&rc);
    if (fill < 0.45 || fill > 0.55) FAIL("initial fill should be ~50%");

   
    n148_rc_update(&rc, 2, (int)(rc.bits_per_frame_target * 3.0), 100.0);
    fill = n148_rc_get_buffer_fullness(&rc);
    if (fill >= 0.5) FAIL("buffer should drain on overshoot");

    PASS();
    return 0;
}

static int test_qp_clamp(void)
{
    N148RateControl rc;
    int qp;

    TEST("QP stays within min/max bounds");

    n148_rc_init(&rc, N148_RC_CQP, 320, 240, 30, 2000, 5);
    n148_rc_set_qp_range(&rc, 16, 40);

    qp = n148_rc_get_frame_qp(&rc, 1);
    if (qp < 16) FAIL("QP below min");

    rc.base_qp = 50;
    qp = n148_rc_get_frame_qp(&rc, 1);
    if (qp > 40) FAIL("QP above max");

    PASS();
    return 0;
}

int main(void)
{
    int rc = 0;

    printf("=== N.148 Rate Control Test (Fase 8.1) ===\n\n");

    if ((rc = test_cqp_mode()) != 0) return rc;
    if ((rc = test_abr_adapts()) != 0) return rc;
    if ((rc = test_abr_undershoot()) != 0) return rc;
    if ((rc = test_vbv_buffer()) != 0) return rc;
    if ((rc = test_qp_clamp()) != 0) return rc;

    printf("\n=== Resultado: %d/%d testes passaram ===\n",
           tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}