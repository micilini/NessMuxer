#ifndef NESS_N148_CABAC_ENGINE_H
#define NESS_N148_CABAC_ENGINE_H

#include "../../codec/n148/n148_bitstream.h"
#include <stdint.h>

typedef struct {
    uint32_t low;
    uint32_t range;
    uint32_t code;
    int      pending_bits;
    int      terminated;
} N148CabacCore;

typedef struct {
    uint8_t state;
    uint8_t mps;
} N148CabacContext;

void n148_cabac_core_init_enc(N148CabacCore* c);
int  n148_cabac_core_init_dec(N148CabacCore* c, N148BsReader* bs);

int n148_cabac_encode_bin_bypass(N148CabacCore* c, N148BsWriter* bs, uint32_t bin);
int n148_cabac_decode_bin_bypass(N148CabacCore* c, N148BsReader* bs, uint32_t* bin);

int n148_cabac_encode_bin_ctx(N148CabacCore* c, N148BsWriter* bs, N148CabacContext* ctx, uint32_t bin);
int n148_cabac_decode_bin_ctx(N148CabacCore* c, N148BsReader* bs, N148CabacContext* ctx, uint32_t* bin);

int n148_cabac_encode_terminate(N148CabacCore* c, N148BsWriter* bs, uint32_t bin);
int n148_cabac_decode_terminate(N148CabacCore* c, N148BsReader* bs, uint32_t* bin);

int n148_cabac_finish_enc(N148CabacCore* c, N148BsWriter* bs);
void n148_cabac_context_init(N148CabacContext* ctx, uint8_t state, uint8_t mps);

#endif