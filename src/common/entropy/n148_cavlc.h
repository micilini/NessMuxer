#ifndef NESS_N148_CAVLC_SHARED_H
#define NESS_N148_CAVLC_SHARED_H

#include "../../codec/n148/n148_bitstream.h"
#include <stdint.h>

int n148_entropy_cavlc_write_block(N148BsWriter* bs,
                                   const int16_t* qcoeff_zigzag,
                                   int coeff_count);

int n148_entropy_cavlc_read_block(N148BsReader* bs,
                                  int16_t* qcoeff_zigzag,
                                  int* coeff_count,
                                  int max_coeffs);

int n148_entropy_cavlc_write_mv(N148BsWriter* bs, int mvx, int mvy);
int n148_entropy_cavlc_read_mv(N148BsReader* bs, int* mvx, int* mvy);

#endif