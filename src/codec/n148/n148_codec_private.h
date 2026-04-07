#ifndef NESS_N148_CODEC_PRIVATE_H
#define NESS_N148_CODEC_PRIVATE_H

#include "n148_spec.h"


int n148_seq_header_serialize(const N148SeqHeader* hdr, uint8_t* out, int out_capacity);


int n148_seq_header_parse(const uint8_t* data, int size, N148SeqHeader* out);


void n148_seq_header_defaults(N148SeqHeader* hdr,
                              int width, int height, int fps,
                              int profile, int entropy_mode);

#endif