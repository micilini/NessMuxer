#include "n148_codec.h"
#include "n148_codec_private.h"
#include "../../decoder/n148/n148_parser.h"

#include <stdlib.h>
#include <string.h>

int n148_build_codec_private(void* encoder_ctx, uint8_t** out, int* out_size)
{
    N148SeqHeader hdr;
    uint8_t* buf;
    int size;

    (void)encoder_ctx;

    if (!out || !out_size)
        return -1;

    buf = (uint8_t*)malloc(N148_SEQ_HEADER_SIZE);
    if (!buf)
        return -1;

    n148_seq_header_defaults(&hdr, 320, 240, 30, N148_PROFILE_MAIN, N148_ENTROPY_CAVLC);

    size = n148_seq_header_serialize(&hdr, buf, N148_SEQ_HEADER_SIZE);
    if (size != N148_SEQ_HEADER_SIZE) {
        free(buf);
        return -1;
    }

    *out = buf;
    *out_size = size;
    return 0;
}

int n148_packetize(const uint8_t* raw_bitstream, int raw_size,
                   uint8_t* out, int out_capacity, int* out_size)
{
    int pos = 0;
    int written = 0;

    if (!raw_bitstream || raw_size <= 0 || !out || out_capacity <= 0 || !out_size)
        return -1;

    while (pos + 3 < raw_size) {
        int nal_start;
        int nal_end;
        int payload_size;

        if (!(raw_bitstream[pos] == 0x00 &&
              raw_bitstream[pos + 1] == 0x00 &&
              raw_bitstream[pos + 2] == 0x01)) {
            pos++;
            continue;
        }

        nal_start = pos + 3;
        nal_end = nal_start;

        while (nal_end + 2 < raw_size) {
            if (raw_bitstream[nal_end] == 0x00 &&
                raw_bitstream[nal_end + 1] == 0x00 &&
                raw_bitstream[nal_end + 2] == 0x01) {
                break;
            }
            nal_end++;
        }

        if (nal_end + 2 >= raw_size)
            nal_end = raw_size;

        payload_size = nal_end - nal_start;

        if (payload_size > 0) {
            if (written + 4 + payload_size > out_capacity)
                return -1;

            out[written + 0] = (uint8_t)((payload_size >> 24) & 0xFF);
            out[written + 1] = (uint8_t)((payload_size >> 16) & 0xFF);
            out[written + 2] = (uint8_t)((payload_size >> 8) & 0xFF);
            out[written + 3] = (uint8_t)(payload_size & 0xFF);

            memcpy(out + written + 4, raw_bitstream + nal_start, (size_t)payload_size);
            written += 4 + payload_size;
        }

        pos = nal_end;
    }

    *out_size = written;
    return written > 0 ? 0 : -1;
}

int n148_is_keyframe(const uint8_t* data, int size)
{
    N148NalUnit nals[16];
    int count = 0;
    int i;

    if (!data || size <= 0)
        return 0;

    if (n148_find_nal_units(data, size, nals, 16, &count) != 0)
        return 0;

    for (i = 0; i < count; i++) {
        if (nals[i].nal_type == N148_NAL_IDR)
            return 1;

        if (nals[i].nal_type == N148_NAL_FRM_HDR &&
            nals[i].payload_size > 0 &&
            nals[i].payload[0] == N148_FRAME_I) {
            return 1;
        }
    }

    return 0;
}

const NessCodecDesc g_n148_codec_desc = {
    N148_CODEC_ID,
    n148_build_codec_private,
    n148_packetize,
    n148_is_keyframe,
    0
};