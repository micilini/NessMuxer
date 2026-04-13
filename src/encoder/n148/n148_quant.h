#ifndef N148_QUANT_H
#define N148_QUANT_H

#include <stdint.h>

int n148_quant_qstep_from_qp(int qp);
int n148_quantize_4x4(const int16_t* coeff, int16_t* out_zigzag, int qp);
int n148_quantize_4x4_tuned(const int16_t* coeff, int16_t* out_zigzag, int qp, int is_intra, int is_chroma);
void n148_quant_unscan_zigzag_4x4(const int16_t* zigzag, int16_t* natural);
void n148_dequant_4x4(const int16_t* qcoeff, int16_t* out_coeff, int qp);

#endif