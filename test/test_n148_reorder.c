#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/encoder/n148/n148_reorder.h"

int main(void)
{
    N148ReorderQueue q;
    N148ReorderFrame out;
    uint8_t frame_a[64];
    uint8_t frame_b[64];
    int ret;

    memset(frame_a, 0x11, sizeof(frame_a));
    memset(frame_b, 0x22, sizeof(frame_b));
    memset(&out, 0, sizeof(out));

    n148_reorder_init(&q);
    n148_reorder_set_max_bframes(&q, 1);

    if (n148_reorder_push_copy(&q, frame_a, sizeof(frame_a), 0, 333333, 0) != 0)
        return 1;

    ret = n148_reorder_pop_ready(&q, &out);
    if (ret != 1)
        return 2;

    if (n148_reorder_push_copy(&q, frame_b, sizeof(frame_b), 333333, 333333, 0) != 0)
        return 3;

    ret = n148_reorder_pop_ready(&q, &out);
    if (ret != 0)
        return 4;

    if (!out.nv12 || out.nv12_size != (int)sizeof(frame_a))
        return 5;

    if (out.display_index != 0 || out.pts_hns != 0)
        return 6;

    n148_reorder_release_frame(&out);
    n148_reorder_free(&q);

    printf("=== N.148 Reorder Queue Test (Fase 6.0-B) ===\n");
    printf("  [PASS] fila segura com copy-owning\n");
    printf("  [PASS] lookahead minimo respeitado\n");
    printf("  [PASS] display_index/pts preservados\n");
    return 0;
}