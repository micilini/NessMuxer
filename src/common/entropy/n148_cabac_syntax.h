#ifndef NESS_N148_CABAC_SYNTAX_H
#define NESS_N148_CABAC_SYNTAX_H

#include "n148_cabac_binarization.h"

typedef struct {
    N148CabacCore       core;
    N148CabacContextSet contexts;
    int                 frame_type;
    int                 qp;
    int                 slice_id;
} N148CabacSession;

int  n148_cabac_session_init_enc(N148CabacSession* s, int frame_type, int qp, int slice_id);
int  n148_cabac_session_init_dec(N148CabacSession* s, N148BsReader* bs, int frame_type, int qp, int slice_id);
int  n148_cabac_session_finish_enc(N148CabacSession* s, N148BsWriter* bs);

int n148_cabac_write_block_mode(N148CabacSession* s, N148BsWriter* bs, uint32_t block_mode);
int n148_cabac_read_block_mode(N148CabacSession* s, N148BsReader* bs, uint32_t* block_mode);

int n148_cabac_write_ref_idx(N148CabacSession* s, N148BsWriter* bs, uint32_t ref_idx);
int n148_cabac_read_ref_idx(N148CabacSession* s, N148BsReader* bs, uint32_t* ref_idx);

int n148_cabac_write_intra_mode(N148CabacSession* s, N148BsWriter* bs, uint32_t intra_mode);
int n148_cabac_read_intra_mode(N148CabacSession* s, N148BsReader* bs, uint32_t* intra_mode);

int n148_cabac_write_has_residual(N148CabacSession* s, N148BsWriter* bs, uint32_t has_residual);
int n148_cabac_read_has_residual(N148CabacSession* s, N148BsReader* bs, uint32_t* has_residual);

int n148_cabac_write_qp_delta(N148CabacSession* s, N148BsWriter* bs, int32_t qp_delta);
int n148_cabac_read_qp_delta(N148CabacSession* s, N148BsReader* bs, int32_t* qp_delta);

int n148_cabac_write_mv(N148CabacSession* s, N148BsWriter* bs, int mvx, int mvy);
int n148_cabac_read_mv(N148CabacSession* s, N148BsReader* bs, int* mvx, int* mvy);

int n148_cabac_write_block(N148CabacSession* s, N148BsWriter* bs, const int16_t* qcoeff_zigzag, int coeff_count);
int n148_cabac_read_block(N148CabacSession* s, N148BsReader* bs, int16_t* qcoeff_zigzag, int* coeff_count, int max_coeffs);

#endif