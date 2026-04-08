#ifndef NESS_N148_DPB_H
#define NESS_N148_DPB_H

#include <stdint.h>

#define N148_MAX_REF_FRAMES 4

typedef struct {
    uint8_t* y[N148_MAX_REF_FRAMES];
    uint8_t* uv[N148_MAX_REF_FRAMES];
    int width;
    int height;
    int count;
} N148ReferenceFrames;

int  n148_refs_init(N148ReferenceFrames* refs, int width, int height);
void n148_refs_free(N148ReferenceFrames* refs);
int  n148_refs_store_nv12(N148ReferenceFrames* refs,
                          const uint8_t* y, const uint8_t* uv,
                          int width, int height);
int  n148_refs_get_planes(const N148ReferenceFrames* refs, int ref_idx,
                          const uint8_t** out_y, const uint8_t** out_uv);

#endif