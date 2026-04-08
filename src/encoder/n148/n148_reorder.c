#include "n148_reorder.h"

#include <stdlib.h>
#include <string.h>

static void n148_reorder_zero_frame(N148ReorderFrame* f)
{
    if (!f)
        return;

    f->nv12 = NULL;
    f->nv12_size = 0;
    f->pts_hns = 0;
    f->duration_hns = 0;
    f->display_index = 0;
    f->coding_index = 0;
    f->anchor_index = 0;
    f->force_keyframe = 0;
    f->planned_frame_type = N148_REORDER_FRAME_AUTO;
    f->is_reference = 0;
}

void n148_reorder_init(N148ReorderQueue* q)
{
    int i;

    if (!q)
        return;

    memset(q, 0, sizeof(*q));
    for (i = 0; i < N148_REORDER_QUEUE_CAP; i++)
        n148_reorder_zero_frame(&q->slots[i]);

    q->count = 0;
    q->max_bframes = 0;
    q->next_display_index = 0;
}

void n148_reorder_release_frame(N148ReorderFrame* f)
{
    if (!f)
        return;

    free(f->nv12);
    n148_reorder_zero_frame(f);
}

void n148_reorder_free(N148ReorderQueue* q)
{
    int i;

    if (!q)
        return;

    for (i = 0; i < N148_REORDER_QUEUE_CAP; i++)
        n148_reorder_release_frame(&q->slots[i]);

    q->count = 0;
    q->next_display_index = 0;
}

void n148_reorder_set_max_bframes(N148ReorderQueue* q, int max_bframes)
{
    if (!q)
        return;

    if (max_bframes < 0)
        max_bframes = 0;
    if (max_bframes > 2)
        max_bframes = 2;

    q->max_bframes = max_bframes;
}

int n148_reorder_push_copy(N148ReorderQueue* q,
                           const uint8_t* nv12,
                           int nv12_size,
                           int64_t pts_hns,
                           int64_t duration_hns,
                           int force_keyframe)
{
    N148ReorderFrame* dst;

    if (!q || !nv12 || nv12_size <= 0)
        return -1;

    if (q->count >= N148_REORDER_QUEUE_CAP)
        return -1;

    dst = &q->slots[q->count];
    n148_reorder_zero_frame(dst);

    dst->nv12 = (uint8_t*)malloc((size_t)nv12_size);
    if (!dst->nv12)
        return -1;

    memcpy(dst->nv12, nv12, (size_t)nv12_size);
    dst->nv12_size = nv12_size;
    dst->pts_hns = pts_hns;
    dst->duration_hns = duration_hns;
    dst->display_index = q->next_display_index++;
    dst->force_keyframe = force_keyframe ? 1 : 0;

    dst->coding_index = 0;
    dst->anchor_index = 0;
    dst->planned_frame_type = N148_REORDER_FRAME_AUTO;
    dst->is_reference = 0;

    q->count++;
    return 0;
}

static void n148_reorder_plan_fifo(N148ReorderQueue* q)
{
    int i;

    for (i = 0; i < q->count; i++) {
        q->slots[i].coding_index = q->slots[i].display_index;

        if (q->slots[i].force_keyframe || q->slots[i].display_index == 0) {
            q->slots[i].planned_frame_type = N148_REORDER_FRAME_I;
            q->slots[i].is_reference = 1;
            q->slots[i].anchor_index = q->slots[i].display_index;
        } else {
            q->slots[i].planned_frame_type = N148_REORDER_FRAME_P;
            q->slots[i].is_reference = 1;
            q->slots[i].anchor_index = q->slots[i].display_index;
        }
    }
}

static void n148_reorder_plan_ipb1(N148ReorderQueue* q)
{
    int i;

    for (i = 0; i < q->count; i++) {
        N148ReorderFrame* f = &q->slots[i];

        f->coding_index = f->display_index;

        if (f->force_keyframe || f->display_index == 0) {
            f->planned_frame_type = N148_REORDER_FRAME_I;
            f->is_reference = 1;
            f->anchor_index = f->display_index;
        } else if ((f->display_index % 2) == 1) {
            f->planned_frame_type = N148_REORDER_FRAME_B;
            f->is_reference = 0;
            f->anchor_index = f->display_index + 1;
        } else {
            f->planned_frame_type = N148_REORDER_FRAME_P;
            f->is_reference = 1;
            f->anchor_index = f->display_index;
        }
    }
}

static void n148_reorder_plan(N148ReorderQueue* q)
{
    if (!q)
        return;

    if (q->max_bframes <= 0)
        n148_reorder_plan_fifo(q);
    else
        n148_reorder_plan_ipb1(q);
}

static int n148_reorder_pick_next_index(const N148ReorderQueue* q, int flushing)
{
    int i;

    if (!q || q->count <= 0)
        return -1;

    if (q->max_bframes <= 0) {
        if (!flushing && q->count <= 0)
            return -1;
        return 0;
    }

    for (i = 0; i < q->count; i++) {
        const N148ReorderFrame* f = &q->slots[i];

        if (f->planned_frame_type == N148_REORDER_FRAME_I ||
            f->planned_frame_type == N148_REORDER_FRAME_P) {
            return i;
        }
    }

    if (flushing)
        return 0;

    return -1;
}

static int n148_reorder_pop_internal(N148ReorderQueue* q, N148ReorderFrame* out, int flushing)
{
    int i;
    int selected;
    int required_lookahead;

    if (!q || !out)
        return -1;

    if (q->count <= 0)
        return 1;

    required_lookahead = q->max_bframes;

    if (!flushing && q->count <= required_lookahead)
        return 1;

    n148_reorder_plan(q);

    selected = n148_reorder_pick_next_index(q, flushing);
    if (selected < 0)
        return 1;

    *out = q->slots[selected];
    n148_reorder_zero_frame(&q->slots[selected]);

    for (i = selected + 1; i < q->count; i++) {
        q->slots[i - 1] = q->slots[i];
        n148_reorder_zero_frame(&q->slots[i]);
    }

    q->count--;
    return 0;
}

int n148_reorder_pop_ready(N148ReorderQueue* q, N148ReorderFrame* out)
{
    return n148_reorder_pop_internal(q, out, 0);
}

int n148_reorder_flush_one(N148ReorderQueue* q, N148ReorderFrame* out)
{
    return n148_reorder_pop_internal(q, out, 1);
}