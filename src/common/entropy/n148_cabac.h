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

/* ---- Sessão CABAC por-slice: deve ser criada antes do primeiro bloco e finalizada depois do último ---- */
typedef struct {
    N148CabacCore   core;
    N148CabacContext ctx_coeff_sig;   /* coeff != 0 ? */
    N148CabacContext ctx_coeff_sign;  /* sinal       */
    N148CabacContext ctx_coeff_mag;   /* magnitude   */
    N148CabacContext ctx_mv_sig;      /* mv != 0 ?   */
    N148CabacContext ctx_mv_sign;     /* mv sinal    */
    N148CabacContext ctx_mv_mag;      /* mv mag      */
    N148CabacContext ctx_count;       /* coeff_count bins */
} N148CabacSession;

void n148_cabac_session_init_enc(N148CabacSession* s);
void n148_cabac_session_init_dec(N148CabacSession* s, N148BsReader* bs);
int  n148_cabac_session_finish_enc(N148CabacSession* s, N148BsWriter* bs);

int n148_cabac_write_block(N148CabacSession* s,
                           N148BsWriter* bs,
                           const int16_t* qcoeff_zigzag,
                           int coeff_count);

int n148_cabac_read_block(N148CabacSession* s,
                          N148BsReader* bs,
                          int16_t* qcoeff_zigzag,
                          int* coeff_count,
                          int max_coeffs);

int n148_cabac_write_mv(N148CabacSession* s, N148BsWriter* bs, int mvx, int mvy);
int n148_cabac_read_mv(N148CabacSession* s, N148BsReader* bs, int* mvx, int* mvy);

#endif