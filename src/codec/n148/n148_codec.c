#include "n148_codec.h"
#include <stddef.h>

static int n148_build_cp_stub(void* ctx, uint8_t** out, int* out_size)
{
    (void)ctx; (void)out; (void)out_size;
    return -1;
}

static int n148_packetize_stub(const uint8_t* raw, int raw_size,
                                uint8_t* out, int out_capacity, int* out_size)
{
    (void)raw; (void)raw_size; (void)out; (void)out_capacity; (void)out_size;
    return -1;
}

static int n148_is_keyframe_stub(const uint8_t* data, int size)
{
    (void)data; (void)size;
    return 0;
}

const NessCodecDesc g_n148_codec_desc = {
    N148_CODEC_ID,
    n148_build_cp_stub,
    n148_packetize_stub,
    n148_is_keyframe_stub,
    0
};