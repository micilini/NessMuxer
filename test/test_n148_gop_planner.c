#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/encoder/n148/n148_reorder.h"

static void fill_frame(uint8_t* buf, int size, uint8_t v)
{
    int i;
    for (i = 0; i < size; i++)
        buf[i] = v;
}

int main(void)
{
    N148ReorderQueue q;
    N148ReorderFrame out;
    uint8_t a[64], b[64], c[64];
    int ret;

    fill_frame(a, sizeof(a), 0x11);
    fill_frame(b, sizeof(b), 0x22);
    fill_frame(c, sizeof(c), 0x33);

    n148_reorder_init(&q);
    n148_reorder_set_max_bframes(&q, 1);

    if (n148_reorder_push_copy(&q, a, sizeof(a), 0, 333333, 1) != 0)
        return 1;
    if (n148_reorder_push_copy(&q, b, sizeof(b), 333333, 333333, 0) != 0)
        return 2;
    if (n148_reorder_push_copy(&q, c, sizeof(c), 666666, 333333, 0) != 0)
        return 3;

    memset(&out, 0, sizeof(out));
    ret = n148_reorder_pop_ready(&q, &out);
    if (ret != 0)
        return 4;

    if (out.planned_frame_type != N148_REORDER_FRAME_I)
        return 5;
    if (out.display_index != 0)
        return 6;
    n148_reorder_release_frame(&out);

    memset(&out, 0, sizeof(out));
    ret = n148_reorder_pop_ready(&q, &out);
    if (ret != 0)
        return 7;

    if (out.planned_frame_type != N148_REORDER_FRAME_P)
        return 8;
    if (out.display_index != 2)
        return 9;
    n148_reorder_release_frame(&out);

    memset(&out, 0, sizeof(out));
    ret = n148_reorder_flush_one(&q, &out);
    if (ret != 0)
        return 10;

    if (out.planned_frame_type != N148_REORDER_FRAME_B)
        return 11;
    if (out.display_index != 1)
        return 12;
    n148_reorder_release_frame(&out);

    n148_reorder_free(&q);

    printf("=== N.148 GOP Planner Test (Fase C) ===\n");
    printf("  [PASS] I frame sai primeiro\n");
    printf("  [PASS] ancora P sai antes do B intermediario\n");
    printf("  [PASS] flush final libera o B restante\n");
    return 0;
}