#include <stdio.h>
#include <stdlib.h>

#include "../src/encoder/n148/n148_slice_plan.h"

#define FAIL(msg) do { printf("[FAIL] %s\n", msg); return 1; } while (0)
#define TEST(msg) do { printf("[TEST] %s... ", msg); fflush(stdout); } while (0)
#define PASS() do { printf("OK\n"); } while (0)

int main(void)
{
    N148SlicePlan plan;

    TEST("slice plan splits rows consistently");
    n148_slice_plan_init(&plan, 50, 10, 3);
    if (plan.slice_count != 3) FAIL("unexpected slice count");
    if (plan.slices[0].first_mb_row != 0 || plan.slices[0].mb_row_count != 4) FAIL("slice 0 rows");
    if (plan.slices[1].first_mb_row != 4 || plan.slices[1].mb_row_count != 4) FAIL("slice 1 rows");
    if (plan.slices[2].first_mb_row != 8 || plan.slices[2].mb_row_count != 2) FAIL("slice 2 rows");
    if (n148_slice_plan_slice_for_mb_row(&plan, 0) != 0) FAIL("row 0 slice id");
    if (n148_slice_plan_slice_for_mb_row(&plan, 4) != 1) FAIL("row 4 slice id");
    if (n148_slice_plan_slice_for_mb_row(&plan, 9) != 2) FAIL("row 9 slice id");
    PASS();

    TEST("slice plan accumulates per-slice block stats");
    n148_slice_plan_note_block(&plan, 1, 0, 0, 0);
    n148_slice_plan_note_block(&plan, 1, 0, 1, 2);
    n148_slice_plan_note_block(&plan, 1, 1, 2, 3);
    if (plan.slices[1].luma_blocks != 2) FAIL("luma block count");
    if (plan.slices[1].chroma_blocks != 1) FAIL("chroma block count");
    if (plan.slices[1].skip_blocks != 1) FAIL("skip count");
    if (plan.slices[1].inter_blocks != 1) FAIL("inter count");
    if (plan.slices[1].intra_blocks != 1) FAIL("intra count");
    if (plan.slices[1].nonzero_blocks != 2) FAIL("nonzero block count");
    if (plan.slices[1].coeff_symbols != 5) FAIL("coeff symbol count");
    PASS();

    printf("[PASS] test_n148_slice_plan\n");
    return 0;
}
