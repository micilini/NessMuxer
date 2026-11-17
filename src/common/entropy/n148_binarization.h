#ifndef NESS_N148_BINARIZATION_COMPAT_H
#define NESS_N148_BINARIZATION_COMPAT_H

#include "n148_cabac_binarization.h"

#ifdef __cplusplus
extern "C" {
#endif

int n148_bin_write_unary(N148BsWriter* bs, uint32_t value);
int n148_bin_read_unary(N148BsReader* bs, uint32_t* out);
int n148_bin_write_signed_mag(N148BsWriter* bs, int32_t value);
int n148_bin_read_signed_mag(N148BsReader* bs, int32_t* out);

#ifdef __cplusplus
}
#endif

#endif
