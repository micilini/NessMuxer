#ifndef NESS_N148_BINARIZATION_H
#define NESS_N148_BINARIZATION_H

#include "../../codec/n148/n148_bitstream.h"
#include <stdint.h>

int n148_bin_write_unary(N148BsWriter* bs, uint32_t value);
int n148_bin_read_unary(N148BsReader* bs, uint32_t* out);

int n148_bin_write_signed_mag(N148BsWriter* bs, int32_t value);
int n148_bin_read_signed_mag(N148BsReader* bs, int32_t* out);

#endif