#ifndef NESS_N148_CONTEXTS_H
#define NESS_N148_CONTEXTS_H

enum {
    N148_CTX_BLOCK_MODE = 0,
    N148_CTX_MV_X       = 1,
    N148_CTX_MV_Y       = 2,
    N148_CTX_QP_DELTA   = 3,
    N148_CTX_COEFF_CNT  = 4,
    N148_CTX_COEFF_SIG  = 5,
    N148_CTX_COEFF_MAG  = 6,
    N148_CTX_MAX        = 7
};

#define N148_CABAC_STATE_NEUTRAL 32
#define N148_CABAC_MPS_ZERO      0
#define N148_CABAC_MPS_ONE       1

#endif