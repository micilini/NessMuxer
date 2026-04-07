#include "encoder.h"
#include "../mf_encoder.h"

typedef struct
{
    ness_packet_callback cb;
    void* userdata;
} MFCallbackBridge;

static int mf_packet_bridge(void* userdata, const MFEncodedPacket* packet)
{
    MFCallbackBridge* bridge = (MFCallbackBridge*)userdata;
    NessEncodedPacket pkt;

    if (!bridge || !bridge->cb || !packet)
        return -1;

    pkt.data = packet->data;
    pkt.size = packet->size;
    pkt.pts_hns = packet->pts_hns;
    pkt.duration_hns = packet->duration_hns;
    pkt.is_keyframe = packet->is_keyframe;

    return bridge->cb(bridge->userdata, &pkt);
}

static int mf_create_wrapper(void** ctx, int width, int height, int fps, int bitrate_kbps)
{
    return mf_encoder_create((MFEncoder**)ctx, width, height, fps, bitrate_kbps);
}

static int mf_submit_frame_wrapper(void* ctx, const uint8_t* nv12, int nv12_size)
{
    return mf_encoder_submit_frame((MFEncoder*)ctx, nv12, nv12_size);
}

static int mf_receive_packets_wrapper(void* ctx, ness_packet_callback cb, void* userdata)
{
    MFCallbackBridge bridge;

    bridge.cb = cb;
    bridge.userdata = userdata;

    return mf_encoder_receive_packets((MFEncoder*)ctx, mf_packet_bridge, &bridge);
}

static int mf_drain_wrapper(void* ctx, ness_packet_callback cb, void* userdata)
{
    MFCallbackBridge bridge;

    bridge.cb = cb;
    bridge.userdata = userdata;

    return mf_encoder_drain((MFEncoder*)ctx, mf_packet_bridge, &bridge);
}

static int mf_get_codec_private_wrapper(void* ctx, uint8_t** out, int* out_size)
{
    return mf_encoder_get_codec_private((MFEncoder*)ctx, out, out_size);
}

static void mf_destroy_wrapper(void* ctx)
{
    mf_encoder_destroy((MFEncoder*)ctx);
}

const NessEncoderVtable g_mf_encoder_vtable = {
    "MediaFoundation",
    "V_MPEG4/ISO/AVC",
    1,
    mf_create_wrapper,
    mf_submit_frame_wrapper,
    mf_receive_packets_wrapper,
    mf_drain_wrapper,
    mf_get_codec_private_wrapper,
    mf_destroy_wrapper
};