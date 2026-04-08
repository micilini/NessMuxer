#ifndef NESS_N148_ENTROPY_ENC_CAVLC_H
#define NESS_N148_ENTROPY_ENC_CAVLC_H

#include "../../codec/n148/n148_bitstream.h"
#include <stdint.h>

int n148_cavlc_write_block(N148BsWriter* bs,
                           const int16_t* qcoeff_zigzag,
                           int coeff_count);

int n148_cavlc_write_mv(N148BsWriter* bs, int mvx, int mvy);

#endif