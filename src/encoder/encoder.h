#ifndef NESS_ENCODER_H
#define NESS_ENCODER_H

#include <stdint.h>
#include "../../include/ness_muxer.h"

typedef struct NessEncodedPacket {
    uint8_t* data;
    int      size;
    int64_t  pts_hns;
    int64_t  duration_hns;
    int      is_keyframe;
} NessEncodedPacket;

typedef int (*ness_packet_callback)(void* userdata, const NessEncodedPacket* pkt);

typedef struct NessEncoderVtable {
    const char* name;
    const char* codec_id;
    int         needs_annexb_conversion;
    int  (*create)(void** ctx, int width, int height, int fps, int bitrate_kbps);
    int  (*submit_frame)(void* ctx, const uint8_t* nv12, int nv12_size);
    int  (*receive_packets)(void* ctx, ness_packet_callback cb, void* userdata);
    int  (*drain)(void* ctx, ness_packet_callback cb, void* userdata);
    int  (*get_codec_private)(void* ctx, uint8_t** out, int* out_size);
    void (*destroy)(void* ctx);
} NessEncoderVtable;

const NessEncoderVtable* ness_encoder_get(NessEncoderType type);
const NessEncoderVtable* ness_encoder_get_best(void);

#endif