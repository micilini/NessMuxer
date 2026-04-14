#ifndef NESS_N148_PROFILES_H
#define NESS_N148_PROFILES_H

#include <stdint.h>

typedef int N148ProfileId;
#define N148_PROF_MAIN        1
#define N148_PROF_HIGHMOTION  2
#define N148_PROF_EPIC        3

typedef int N148EncodeMode;
#define N148_ENC_MODE_FAST    1
#define N148_ENC_MODE_SLOW    2

typedef struct {
    N148ProfileId id;
    const char*   name;

   
    int allow_p;
    int allow_b;

   
    int allow_cabac;

   
    int max_refs;

   
    int max_block_size;  

   
    int allow_subpel;
    int allow_weighted_pred;

   
    int max_reorder;
} N148ProfileDef;

const N148ProfileDef* n148_profile_get(N148ProfileId id);


int n148_profile_validate(N148ProfileId id,
                          int has_b, int has_cabac,
                          int num_refs, int block_size,
                          int has_subpel, int has_weighted,
                          int reorder_depth,
                          char* errbuf, int errbuf_size);

#endif