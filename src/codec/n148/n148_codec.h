#ifndef NESS_N148_CODEC_H
#define NESS_N148_CODEC_H

#include "../codec.h"

extern const NessCodecDesc g_n148_codec_desc;

#define N148_CODEC_ID "V_NESS/N148"

int n148_build_codec_private(void* encoder_ctx, uint8_t** out, int* out_size);

int n148_packetize(const uint8_t* raw_bitstream, int raw_size,
                   uint8_t* out, int out_capacity, int* out_size);

int n148_is_keyframe(const uint8_t* data, int size);

#endif