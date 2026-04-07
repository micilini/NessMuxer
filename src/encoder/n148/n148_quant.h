#ifndef NESS_N148_QUANT_H
#define NESS_N148_QUANT_H

#include <stdint.h>

int n148_quant_qstep_from_qp(int qp);
int n148_quantize_4x4(const int16_t* coeff, int16_t* out_zigzag, int qp);
void n148_quant_unscan_zigzag_4x4(const int16_t* zigzag, int16_t* natural);

#endif