#ifndef NESS_CODEC_H
#define NESS_CODEC_H

#include <stdint.h>

typedef struct NessCodecDesc {
    const char*    codec_id;

    int            (*build_codec_private)(void* encoder_ctx,
                                          uint8_t** out, int* out_size);

    int            (*packetize)(const uint8_t* raw_bitstream, int raw_size,
                                uint8_t* out, int out_capacity, int* out_size);

    int            (*is_keyframe)(const uint8_t* data, int size);

    int            needs_annexb_conversion;
} NessCodecDesc;

#endif