#include <stdio.h>
#include <string.h>

#include "../src/encoder/n148/n148_profiles.h"

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) do { tests_total++; printf("  [TEST] %-55s", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL (%s)\n", msg); return 1; } while(0)

static int test_main_profile_limits(void)
{
    char errbuf[256];

    TEST("Main profile rejects B-frames");
    if (n148_profile_validate(N148_PROF_MAIN,
            1, 0, 1, 16, 0, 0, 0, errbuf, sizeof(errbuf)) == 0)
        FAIL("should reject B-frames");
    PASS();
    return 0;
}

static int test_main_profile_rejects_cabac(void)
{
    char errbuf[256];

    TEST("Main profile rejects CABAC");
    if (n148_profile_validate(N148_PROF_MAIN,
            0, 1, 1, 16, 0, 0, 0, errbuf, sizeof(errbuf)) == 0)
        FAIL("should reject CABAC");
    PASS();
    return 0;
}

static int test_main_profile_accepts_valid(void)
{
    char errbuf[256];

    TEST("Main profile accepts valid I+P CAVLC config");
    if (n148_profile_validate(N148_PROF_MAIN,
            0, 0, 1, 16, 0, 0, 0, errbuf, sizeof(errbuf)) != 0)
        FAIL(errbuf);
    PASS();
    return 0;
}

static int test_epic_accepts_full(void)
{
    char errbuf[256];

    TEST("Epic profile accepts B+CABAC+multi-ref+subpel");
    if (n148_profile_validate(N148_PROF_EPIC,
            1, 1, 4, 32, 1, 1, 2, errbuf, sizeof(errbuf)) != 0)
        FAIL(errbuf);
    PASS();
    return 0;
}

static int test_epic_rejects_too_many_refs(void)
{
    char errbuf[256];

    TEST("Epic profile rejects > 8 refs");
    if (n148_profile_validate(N148_PROF_EPIC,
            1, 1, 12, 32, 1, 0, 2, errbuf, sizeof(errbuf)) == 0)
        FAIL("should reject 12 refs");
    PASS();
    return 0;
}

static int test_highmotion_profile(void)
{
    char errbuf[256];

    TEST("HighMotion profile: 4 refs OK, B-frames rejected");
    if (n148_profile_validate(N148_PROF_HIGHMOTION,
            0, 0, 4, 32, 1, 0, 0, errbuf, sizeof(errbuf)) != 0)
        FAIL(errbuf);
    if (n148_profile_validate(N148_PROF_HIGHMOTION,
            1, 0, 2, 16, 0, 0, 0, errbuf, sizeof(errbuf)) == 0)
        FAIL("should reject B-frames");
    PASS();
    return 0;
}

int main(void)
{
    int rc = 0;

    printf("=== N.148 Profiles Test (Fase 8.2) ===\n\n");

    if ((rc = test_main_profile_limits()) != 0) return rc;
    if ((rc = test_main_profile_rejects_cabac()) != 0) return rc;
    if ((rc = test_main_profile_accepts_valid()) != 0) return rc;
    if ((rc = test_epic_accepts_full()) != 0) return rc;
    if ((rc = test_epic_rejects_too_many_refs()) != 0) return rc;
    if ((rc = test_highmotion_profile()) != 0) return rc;

    printf("\n=== Resultado: %d/%d testes passaram ===\n",
           tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}