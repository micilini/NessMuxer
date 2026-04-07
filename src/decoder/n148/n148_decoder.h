#ifndef NESS_N148_DECODER_H
#define NESS_N148_DECODER_H

#include <stdint.h>

typedef struct N148Decoder N148Decoder;

typedef struct {
    uint8_t* planes[3];  
    int      strides[3];
    int      width, height;
    int64_t  pts;
    int      frame_type; 
} N148DecodedFrame;

int  n148_decoder_create(N148Decoder** out);
int  n148_decoder_init_from_seq_header(N148Decoder* dec, const uint8_t* data, int size);
int  n148_decoder_decode(N148Decoder* dec, const uint8_t* data, int size,
                         N148DecodedFrame* out_frame);
int  n148_decoder_flush(N148Decoder* dec, N148DecodedFrame* out_frame);
void n148_decoder_destroy(N148Decoder* dec);

#endif