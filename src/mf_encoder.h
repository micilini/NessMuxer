

#ifndef NESS_MF_ENCODER_H
#define NESS_MF_ENCODER_H

#include <stdint.h>

typedef struct MFEncoder MFEncoder;


typedef struct {
    uint8_t* data;          
    int      size;          
    int64_t  pts_hns;       
    int64_t  duration_hns;  
    int      is_keyframe;   
} MFEncodedPacket;


typedef int (*mf_packet_callback)(void* userdata, const MFEncodedPacket* packet);


int mf_encoder_create(MFEncoder** out_enc, int width, int height,
                       int fps, int bitrate_kbps);


int mf_encoder_submit_frame(MFEncoder* enc, const uint8_t* nv12, int nv12_size);


int mf_encoder_receive_packets(MFEncoder* enc, mf_packet_callback cb, void* userdata);


int mf_encoder_drain(MFEncoder* enc, mf_packet_callback cb, void* userdata);


int mf_encoder_get_codec_private(MFEncoder* enc, uint8_t** out, int* out_size);


void mf_encoder_destroy(MFEncoder* enc);

#endif
