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
    init_one(&set->ctx[N148_CTX_COEFF_SIG],           20, 1);
    init_one(&set->ctx[N148_CTX_COEFF_LAST],          16, 0);
    init_one(&set->ctx[N148_CTX_COEFF_LEVEL_PREFIX],  28, 0);
    init_one(&set->ctx[N148_CTX_COEFF_LEVEL_SUFFIX],  28, 0);

    for (i = 0; i < 16; i++) {
        int st_sig = 30 - i;
        int st_last;
        uint8_t mps_last;

        if (st_sig < 12) st_sig = 12;

        if (i < 4) {
            st_last = 35 - i;
            mps_last = 0;
        } else if (i < 8) {
            st_last = 29 - (i - 4);
            mps_last = 0;
        } else if (i < 12) {
            st_last = 24 - (i - 8);
            mps_last = 0;
        } else {
            st_last = 22 - (i - 12);
            mps_last = 1;
        }

        if (st_last < 10) st_last = 10;

        init_one(&set->ctx[N148_CTX_SIG_BASE + i], (uint8_t)st_sig, 0);
        init_one(&set->ctx[N148_CTX_LAST_BASE + i], (uint8_t)st_last, mps_last);
    }

    for (i = 0; i < 5; i++) {
        init_one(&set->ctx[N148_CTX_LEVEL_GT1_BASE + i], (uint8_t)(24 + i * 2), 0);
    }

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