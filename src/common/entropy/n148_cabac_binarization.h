#ifndef NESS_N148_CABAC_BINARIZATION_H
#define NESS_N148_CABAC_BINARIZATION_H

#include "n148_cabac_contexts.h"
#include <stdint.h>

int n148_cabac_write_unary_ctx(N148CabacCore* c, N148BsWriter* bs, N148CabacContext* ctx, uint32_t value);
int n148_cabac_read_unary_ctx(N148CabacCore* c, N148BsReader* bs, N148CabacContext* ctx, uint32_t* value);

int n148_cabac_write_trunc_unary_ctx(N148CabacCore* c, N148BsWriter* bs, N148CabacContext* ctx, uint32_t value, uint32_t max_value);
int n148_cabac_read_trunc_unary_ctx(N148CabacCore* c, N148BsReader* bs, N148CabacContext* ctx, uint32_t* value, uint32_t max_value);

int n148_cabac_write_fixed_bypass(N148CabacCore* c, N148BsWriter* bs, uint32_t value, int num_bits);
int n148_cabac_read_fixed_bypass(N148CabacCore* c, N148BsReader* bs, uint32_t* value, int num_bits);

int n148_cabac_write_signed_mag_ctx(N148CabacCore* c,
                                    N148BsWriter* bs,
                                    N148CabacContext* sig_ctx,
                                    N148CabacContext* sign_ctx,
                                    N148CabacContext* mag_ctx,
                                    int32_t value);

int n148_cabac_read_signed_mag_ctx(N148CabacCore* c,
                                   N148BsReader* bs,
                                   N148CabacContext* sig_ctx,
                                   N148CabacContext* sign_ctx,
                                   N148CabacContext* mag_ctx,
                                   int32_t* out);

#endif