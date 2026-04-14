#ifndef N148_SLICE_PLAN_H
#define N148_SLICE_PLAN_H

#include <stdint.h>

#define N148_MAX_SLICE_PLAN_SLICES 16

typedef struct {
    int slice_id;
    int first_mb_row;
    int mb_row_count;
    int mb_count;
    int start_mb_index;
    int end_mb_index;

    uint32_t luma_blocks;
    uint32_t chroma_blocks;
    uint32_t skip_blocks;
    uint32_t inter_blocks;
    uint32_t intra_blocks;
    uint32_t nonzero_blocks;
    uint32_t coeff_symbols;
} N148SliceInfo;

typedef struct {
    int mb_cols;
    int mb_rows;
    int requested_slice_count;
    int slice_count;
    int rows_per_slice;
    N148SliceInfo slices[N148_MAX_SLICE_PLAN_SLICES];
} N148SlicePlan;

void n148_slice_plan_init(N148SlicePlan* plan,
                          int mb_cols,
                          int mb_rows,
                          int requested_slice_count);

int n148_slice_plan_slice_for_mb_row(const N148SlicePlan* plan, int mb_row);

N148SliceInfo* n148_slice_plan_get_mutable(N148SlicePlan* plan, int slice_id);
const N148SliceInfo* n148_slice_plan_get(const N148SlicePlan* plan, int slice_id);

void n148_slice_plan_note_block(N148SlicePlan* plan,
                                int slice_id,
                                int is_chroma,
                                int final_mode,
                                int coeff_count);

#endif
