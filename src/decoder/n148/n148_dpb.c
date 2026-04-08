#include "n148_dpb.h"

#include <stdlib.h>
#include <string.h>

int n148_refs_init(N148ReferenceFrames* refs, int width, int height)
{
    int i;
    int y_size, uv_size;

    if (!refs || width <= 0 || height <= 0)
        return -1;

    memset(refs, 0, sizeof(*refs));
    refs->width = width;
    refs->height = height;

    y_size = width * height;
    uv_size = width * height / 2;

    for (i = 0; i < N148_MAX_REF_FRAMES; i++) {
        refs->y[i] = (uint8_t*)malloc((size_t)y_size);
        refs->uv[i] = (uint8_t*)malloc((size_t)uv_size);
        if (!refs->y[i] || !refs->uv[i]) {
            n148_refs_free(refs);
            return -1;
        }
    }

    return 0;
}

void n148_refs_free(N148ReferenceFrames* refs)
{
    int i;
    if (!refs) return;

    for (i = 0; i < N148_MAX_REF_FRAMES; i++) {
        free(refs->y[i]);
        free(refs->uv[i]);
        refs->y[i] = NULL;
        refs->uv[i] = NULL;
    }

    refs->count = 0;
    refs->width = 0;
    refs->height = 0;
}

int n148_refs_store_nv12(N148ReferenceFrames* refs,
                         const uint8_t* y, const uint8_t* uv,
                         int width, int height)
{
    int i;
    int y_size, uv_size;

    if (!refs || !y || !uv)
        return -1;

    if (refs->width != width || refs->height != height)
        return -1;

    y_size = width * height;
    uv_size = width * height / 2;

    for (i = N148_MAX_REF_FRAMES - 1; i > 0; i--) {
        memcpy(refs->y[i], refs->y[i - 1], (size_t)y_size);
        memcpy(refs->uv[i], refs->uv[i - 1], (size_t)uv_size);
    }

    memcpy(refs->y[0], y, (size_t)y_size);
    memcpy(refs->uv[0], uv, (size_t)uv_size);

    if (refs->count < N148_MAX_REF_FRAMES)
        refs->count++;

    return 0;
}

int n148_refs_get_planes(const N148ReferenceFrames* refs, int ref_idx,
                         const uint8_t** out_y, const uint8_t** out_uv)
{
    if (!refs || !out_y || !out_uv)
        return -1;

    if (ref_idx < 0 || ref_idx >= refs->count)
        return -1;

    *out_y = refs->y[ref_idx];
    *out_uv = refs->uv[ref_idx];
    return 0;
}