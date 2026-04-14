#include "n148_slice_plan.h"

#include <string.h>

static int clamp_slice_count(int requested_slice_count, int mb_rows)
{
    int slice_count = requested_slice_count;

    if (mb_rows <= 0)
        return 1;

    if (slice_count <= 0)
        slice_count = 1;
    if (slice_count > mb_rows)
        slice_count = mb_rows;
    if (slice_count > N148_MAX_SLICE_PLAN_SLICES)
        slice_count = N148_MAX_SLICE_PLAN_SLICES;
    if (slice_count < 1)
        slice_count = 1;

    return slice_count;
}

void n148_slice_plan_init(N148SlicePlan* plan,
                          int mb_cols,
                          int mb_rows,
                          int requested_slice_count)
{
    int slice_count;
    int rows_per_slice;
    int row_cursor;
    int i;

    if (!plan)
        return;

    memset(plan, 0, sizeof(*plan));

    plan->mb_cols = mb_cols;
    plan->mb_rows = mb_rows;
    plan->requested_slice_count = requested_slice_count;

    slice_count = clamp_slice_count(requested_slice_count, mb_rows);
    rows_per_slice = (mb_rows + slice_count - 1) / slice_count;
    if (rows_per_slice < 1)
        rows_per_slice = 1;

    plan->slice_count = slice_count;
    plan->rows_per_slice = rows_per_slice;

    row_cursor = 0;
    for (i = 0; i < slice_count; i++) {
        int rows_left = mb_rows - row_cursor;
        int this_rows = rows_left < rows_per_slice ? rows_left : rows_per_slice;
        N148SliceInfo* info = &plan->slices[i];

        if (this_rows < 0)
            this_rows = 0;

        info->slice_id = i;
        info->first_mb_row = row_cursor;
        info->mb_row_count = this_rows;
        info->mb_count = this_rows * mb_cols;
        info->start_mb_index = row_cursor * mb_cols;
        info->end_mb_index = info->start_mb_index + info->mb_count;

        row_cursor += this_rows;
    }
}

int n148_slice_plan_slice_for_mb_row(const N148SlicePlan* plan, int mb_row)
{
    int slice_id;

    if (!plan || plan->slice_count <= 0)
        return 0;

    if (mb_row <= 0)
        return 0;

    slice_id = mb_row / (plan->rows_per_slice > 0 ? plan->rows_per_slice : 1);
    if (slice_id >= plan->slice_count)
        slice_id = plan->slice_count - 1;
    if (slice_id < 0)
        slice_id = 0;

    return slice_id;
}

N148SliceInfo* n148_slice_plan_get_mutable(N148SlicePlan* plan, int slice_id)
{
    if (!plan || slice_id < 0 || slice_id >= plan->slice_count)
        return NULL;
    return &plan->slices[slice_id];
}

const N148SliceInfo* n148_slice_plan_get(const N148SlicePlan* plan, int slice_id)
{
    if (!plan || slice_id < 0 || slice_id >= plan->slice_count)
        return NULL;
    return &plan->slices[slice_id];
}

void n148_slice_plan_note_block(N148SlicePlan* plan,
                                int slice_id,
                                int is_chroma,
                                int final_mode,
                                int coeff_count)
{
    N148SliceInfo* info = n148_slice_plan_get_mutable(plan, slice_id);
    if (!info)
        return;

    if (is_chroma)
        info->chroma_blocks++;
    else
        info->luma_blocks++;

    if (final_mode == 0)
        info->skip_blocks++;
    else if (final_mode == 2)
        info->intra_blocks++;
    else
        info->inter_blocks++;

    if (coeff_count > 0) {
        info->nonzero_blocks++;
        info->coeff_symbols += (uint32_t)coeff_count;
    }
}
