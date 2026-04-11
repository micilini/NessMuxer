#ifndef NESS_N148_CABAC_CONTEXTS_H
#define NESS_N148_CABAC_CONTEXTS_H

#include "n148_cabac_engine.h"
#include <stdint.h>

typedef enum {
    N148_CTX_BLOCK_MODE = 0,
    N148_CTX_REF_IDX,
    N148_CTX_INTRA_MODE,
    N148_CTX_HAS_RESIDUAL,
    N148_CTX_QP_DELTA,
    N148_CTX_COEFF_COUNT,
    N148_CTX_COEFF_SIG,
    N148_CTX_COEFF_LAST,
    N148_CTX_COEFF_LEVEL_PREFIX,
    N148_CTX_COEFF_LEVEL_SUFFIX,

    N148_CTX_SIG_BASE,
    N148_CTX_LAST_BASE = N148_CTX_SIG_BASE + 16,
    N148_CTX_LEVEL_GT1_BASE = N148_CTX_LAST_BASE + 16,

    N148_CTX_MVD_X_ZERO = N148_CTX_LEVEL_GT1_BASE + 5,
    N148_CTX_MVD_X_GT1,
    N148_CTX_MVD_X_GT2,
    N148_CTX_MVD_X_GT3P,

    N148_CTX_MVD_Y_ZERO,
    N148_CTX_MVD_Y_GT1,
    N148_CTX_MVD_Y_GT2,
    N148_CTX_MVD_Y_GT3P,

    N148_CTX_MAX
} N148CabacCtxId;

#define N148_CABAC_STATE_NEUTRAL 32
#define N148_CABAC_MPS_ZERO      0
#define N148_CABAC_MPS_ONE       1

typedef struct {
    N148CabacContext ctx[N148_CTX_MAX];
} N148CabacContextSet;

void n148_cabac_context_set_init_default(N148CabacContextSet* set);
void n148_cabac_context_set_init_for_slice(N148CabacContextSet* set, int frame_type, int qp, int slice_id);
N148CabacContext* n148_cabac_context_get(N148CabacContextSet* set, N148CabacCtxId id);

#endif