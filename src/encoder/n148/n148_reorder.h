#ifndef N148_REORDER_H
#define N148_REORDER_H

#include <stdint.h>

#define N148_REORDER_QUEUE_CAP 8

#define N148_REORDER_FRAME_AUTO 0
#define N148_REORDER_FRAME_I    1
#define N148_REORDER_FRAME_P    2
#define N148_REORDER_FRAME_B    3

typedef struct {
    uint8_t* nv12;
    int      nv12_size;
    int64_t  pts_hns;
    int64_t  duration_hns;
    uint32_t display_index;
    uint32_t coding_index;
    uint32_t anchor_index;
    int      force_keyframe;
    int      planned_frame_type;
    int      is_reference;
} N148ReorderFrame;

typedef struct {
    N148ReorderFrame slots[N148_REORDER_QUEUE_CAP];
    int              count;
    int              max_bframes;
    uint32_t         next_display_index;
} N148ReorderQueue;

void n148_reorder_init(N148ReorderQueue* q);
void n148_reorder_free(N148ReorderQueue* q);
void n148_reorder_release_frame(N148ReorderFrame* f);
void n148_reorder_set_max_bframes(N148ReorderQueue* q, int max_bframes);

int n148_reorder_push_copy(N148ReorderQueue* q,
                           const uint8_t* nv12,
                           int nv12_size,
                           int64_t pts_hns,
                           int64_t duration_hns,
                           int force_keyframe);

int n148_reorder_pop_ready(N148ReorderQueue* q, N148ReorderFrame* out);
int n148_reorder_flush_one(N148ReorderQueue* q, N148ReorderFrame* out);

#endif