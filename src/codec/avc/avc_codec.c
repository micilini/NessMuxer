#include "avc_codec.h"
#include "../../avc_utils.h"

static int avc_build_cp_wrapper(void* encoder_ctx, uint8_t** out, int* out_size)
{
    (void)encoder_ctx;
    (void)out;
    (void)out_size;
    return -1;
}

static int avc_packetize_wrapper(const uint8_t* raw, int raw_size,
                                  uint8_t* out, int out_capacity, int* out_size)
{
    return avc_annexb_to_mp4(raw, raw_size, out, out_capacity, out_size);
}

static int avc_is_keyframe_wrapper(const uint8_t* data, int size)
{
    return avc_is_keyframe(data, size);
}

const NessCodecDesc g_avc_codec_desc = {
    AVC_CODEC_ID,
    avc_build_cp_wrapper,
    avc_packetize_wrapper,
    avc_is_keyframe_wrapper,
    1
};