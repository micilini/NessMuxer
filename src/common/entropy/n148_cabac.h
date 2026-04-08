#ifndef NESS_N148_CABAC_H
#define NESS_N148_CABAC_H

#include "../../codec/n148/n148_bitstream.h"
#include <stdint.h>

typedef struct {
    uint32_t low;
    uint32_t range;
    uint32_t code;
    int      pending_bits;
} N148CabacCore;

typedef struct {
    uint8_t state;
    uint8_t mps;
} N148CabacContext;

void n148_cabac_core_init_enc(N148CabacCore* c);
void n148_cabac_core_init_dec(N148CabacCore* c, N148BsReader* bs);

int n148_cabac_encode_bin_bypass(N148CabacCore* c, N148BsWriter* bs, uint32_t bin);
int n148_cabac_decode_bin_bypass(N148CabacCore* c, N148BsReader* bs, uint32_t* bin);

int n148_cabac_encode_bin_ctx(N148CabacCore* c, N148BsWriter* bs, N148CabacContext* ctx, uint32_t bin);
int n148_cabac_decode_bin_ctx(N148CabacCore* c, N148BsReader* bs, N148CabacContext* ctx, uint32_t* bin);

int n148_cabac_finish_enc(N148CabacCore* c, N148BsWriter* bs);

void n148_cabac_context_init(N148CabacContext* ctx, uint8_t state, uint8_t mps);

int n148_cabac_write_block(N148BsWriter* bs,
                           const int16_t* qcoeff_zigzag,
                           int coeff_count);

int n148_cabac_read_block(N148BsReader* bs,
                          int16_t* qcoeff_zigzag,
                          int* coeff_count,
                          int max_coeffs);

int n148_cabac_write_mv(N148BsWriter* bs, int mvx, int mvy);
int n148_cabac_read_mv(N148BsReader* bs, int* mvx, int* mvy);

#endif