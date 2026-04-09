#include "n148_cabac_contexts.h"

static void init_one(N148CabacContext* ctx, uint8_t state, uint8_t mps)
{
    n148_cabac_context_init(ctx, state, mps);
}

void n148_cabac_context_set_init_default(N148CabacContextSet* set)
{
    int i;

    if (!set)
        return;

    for (i = 0; i < N148_CTX_MAX; i++)
        init_one(&set->ctx[i], N148_CABAC_STATE_NEUTRAL, N148_CABAC_MPS_ZERO);

    init_one(&set->ctx[N148_CTX_HAS_RESIDUAL],        22, 0);
    init_one(&set->ctx[N148_CTX_COEFF_SIG],           20, 0);
    init_one(&set->ctx[N148_CTX_COEFF_LAST],          26, 0);
    init_one(&set->ctx[N148_CTX_COEFF_LEVEL_PREFIX],  36, 1);
    init_one(&set->ctx[N148_CTX_COEFF_LEVEL_SUFFIX],  28, 0);

   
    init_one(&set->ctx[N148_CTX_MVD_X_ZERO],  24, 0);
    init_one(&set->ctx[N148_CTX_MVD_X_GT1],   30, 0);
    init_one(&set->ctx[N148_CTX_MVD_X_GT2],   34, 0);
    init_one(&set->ctx[N148_CTX_MVD_X_GT3P],  37, 0);

    init_one(&set->ctx[N148_CTX_MVD_Y_ZERO],  24, 0);
    init_one(&set->ctx[N148_CTX_MVD_Y_GT1],   30, 0);
    init_one(&set->ctx[N148_CTX_MVD_Y_GT2],   34, 0);
    init_one(&set->ctx[N148_CTX_MVD_Y_GT3P],  37, 0);
}

void n148_cabac_context_set_init_for_slice(N148CabacContextSet* set, int frame_type, int qp, int slice_id)
{
    (void)qp;
    (void)slice_id;

    n148_cabac_context_set_init_default(set);

    if (!set)
        return;

    if (frame_type == 1) {
        init_one(&set->ctx[N148_CTX_BLOCK_MODE], 30, 0);
        init_one(&set->ctx[N148_CTX_REF_IDX], 18, 0);
        init_one(&set->ctx[N148_CTX_HAS_RESIDUAL], 20, 0);
    } else {
        init_one(&set->ctx[N148_CTX_BLOCK_MODE], 38, 1);
        init_one(&set->ctx[N148_CTX_REF_IDX], 30, 0);
        init_one(&set->ctx[N148_CTX_HAS_RESIDUAL], 24, 0);
    }
}

N148CabacContext* n148_cabac_context_get(N148CabacContextSet* set, N148CabacCtxId id)
{
    if (!set || id < 0 || id >= N148_CTX_MAX)
        return 0;
    return &set->ctx[id];
}