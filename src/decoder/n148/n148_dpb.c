#include "n148_dpb.h"
#include <stdlib.h>

struct N148Dpb {
    int dummy;
};

int n148_dpb_create(N148Dpb** out, int max_refs, int frame_size)
{
    (void)max_refs; (void)frame_size;
    *out = (N148Dpb*)calloc(1, sizeof(N148Dpb));
    return *out ? 0 : -1;
}

int n148_dpb_store(N148Dpb* dpb, const uint8_t* frame_data, int frame_size, int is_ref)
{
    (void)dpb; (void)frame_data; (void)frame_size; (void)is_ref;
    return 0;
}

const uint8_t* n148_dpb_get_ref(N148Dpb* dpb, int index)
{
    (void)dpb; (void)index;
    return NULL;
}

void n148_dpb_destroy(N148Dpb* dpb)
{
    if (dpb) free(dpb);
}