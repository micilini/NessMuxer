#include "n148_profiles.h"
#include <string.h>
#include <stdio.h>

static const N148ProfileDef s_profiles[] = {
    {
        N148_PROF_MAIN, "Main",
        1, 0,
        0,
        1,
        16,
        0, 0,
        0
    },
    {
        N148_PROF_HIGHMOTION, "HighMotion",
        1, 0,
        0,
        4,
        32,
        1, 0,
        0
    },
    {
        N148_PROF_EPIC, "Epic",
        1, 1,
        1,
        8,
        32,
        1, 1,
        4
    }
};

#define PROFILE_COUNT (int)(sizeof(s_profiles)/sizeof(s_profiles[0]))

const N148ProfileDef* n148_profile_get(N148ProfileId id)
{
    int i;
    for (i = 0; i < PROFILE_COUNT; i++) {
        if (s_profiles[i].id == id)
            return &s_profiles[i];
    }
    return &s_profiles[0];
}

int n148_profile_validate(N148ProfileId id,
                          int has_b, int has_cabac,
                          int num_refs, int block_size,
                          int has_subpel, int has_weighted,
                          int reorder_depth,
                          char* errbuf, int errbuf_size)
{
    const N148ProfileDef* p = n148_profile_get(id);

    if (has_b && !p->allow_b) {
        if (errbuf) snprintf(errbuf, errbuf_size,
            "Profile %s does not allow B-frames", p->name);
        return -1;
    }
    if (has_cabac && !p->allow_cabac) {
        if (errbuf) snprintf(errbuf, errbuf_size,
            "Profile %s does not allow CABAC", p->name);
        return -1;
    }
    if (num_refs > p->max_refs) {
        if (errbuf) snprintf(errbuf, errbuf_size,
            "Profile %s allows max %d refs, got %d", p->name, p->max_refs, num_refs);
        return -1;
    }
    if (block_size > p->max_block_size) {
        if (errbuf) snprintf(errbuf, errbuf_size,
            "Profile %s allows max %dx%d blocks, got %d",
            p->name, p->max_block_size, p->max_block_size, block_size);
        return -1;
    }
    if (has_subpel && !p->allow_subpel) {
        if (errbuf) snprintf(errbuf, errbuf_size,
            "Profile %s does not allow sub-pel ME", p->name);
        return -1;
    }
    if (has_weighted && !p->allow_weighted_pred) {
        if (errbuf) snprintf(errbuf, errbuf_size,
            "Profile %s does not allow weighted prediction", p->name);
        return -1;
    }
    if (reorder_depth > p->max_reorder) {
        if (errbuf) snprintf(errbuf, errbuf_size,
            "Profile %s allows max reorder depth %d, got %d",
            p->name, p->max_reorder, reorder_depth);
        return -1;
    }
    return 0;
}