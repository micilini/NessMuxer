#ifndef NESS_N148_DPB_H
#define NESS_N148_DPB_H

#include <stdint.h>

typedef struct N148Dpb N148Dpb;

int  n148_dpb_create(N148Dpb** out, int max_refs, int frame_size);
int  n148_dpb_store(N148Dpb* dpb, const uint8_t* frame_data, int frame_size, int is_ref);
const uint8_t* n148_dpb_get_ref(N148Dpb* dpb, int index);
void n148_dpb_destroy(N148Dpb* dpb);

#endif