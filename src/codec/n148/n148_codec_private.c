#include "n148_codec_private.h"
#include <string.h>

void n148_seq_header_defaults(N148SeqHeader* hdr,
                              int width, int height, int fps,
                              int profile, int entropy_mode)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic[0] = N148_MAGIC_0;
    hdr->magic[1] = N148_MAGIC_1;
    hdr->magic[2] = N148_MAGIC_2;
    hdr->magic[3] = N148_MAGIC_3;
    hdr->version  = 1;
    hdr->profile  = (uint8_t)profile;
    hdr->level    = 1;
    hdr->chroma_format = N148_CHROMA_420;
    hdr->bit_depth     = 8;
    hdr->width         = (uint16_t)width;
    hdr->height        = (uint16_t)height;
    hdr->timescale     = 1000000;  
    hdr->fps_num       = (uint16_t)fps;
    hdr->fps_den       = 1;
    hdr->gop_length    = (uint16_t)(fps > 0 ? fps : 30);
    hdr->entropy_mode  = (uint8_t)entropy_mode;
    hdr->max_ref_frames    = 1;
    hdr->max_reorder_depth = 0;
    hdr->block_size_flags  = N148_BLOCK_4X4 | N148_BLOCK_8X8 | N148_BLOCK_16X16;
    hdr->feature_flags     = 0;
}

int n148_seq_header_serialize(const N148SeqHeader* hdr, uint8_t* out, int out_capacity)
{
    if (out_capacity < N148_SEQ_HEADER_SIZE)
        return -1;

    out[0]  = hdr->magic[0];
    out[1]  = hdr->magic[1];
    out[2]  = hdr->magic[2];
    out[3]  = hdr->magic[3];
    out[4]  = hdr->version;
    out[5]  = hdr->profile;
    out[6]  = hdr->level;
    out[7]  = hdr->chroma_format;
    out[8]  = hdr->bit_depth;
    out[9]  = (uint8_t)(hdr->width >> 8);
    out[10] = (uint8_t)(hdr->width & 0xFF);
    out[11] = (uint8_t)(hdr->height >> 8);
    out[12] = (uint8_t)(hdr->height & 0xFF);
    out[13] = (uint8_t)(hdr->timescale >> 24);
    out[14] = (uint8_t)(hdr->timescale >> 16);
    out[15] = (uint8_t)(hdr->timescale >> 8);
    out[16] = (uint8_t)(hdr->timescale & 0xFF);
    out[17] = (uint8_t)(hdr->fps_num >> 8);
    out[18] = (uint8_t)(hdr->fps_num & 0xFF);
    out[19] = (uint8_t)(hdr->fps_den >> 8);
    out[20] = (uint8_t)(hdr->fps_den & 0xFF);
    out[21] = (uint8_t)(hdr->gop_length >> 8);
    out[22] = (uint8_t)(hdr->gop_length & 0xFF);
    out[23] = hdr->entropy_mode;
    out[24] = hdr->max_ref_frames;
    out[25] = hdr->max_reorder_depth;
    out[26] = hdr->block_size_flags;
    out[27] = (uint8_t)(hdr->feature_flags >> 8);
    out[28] = (uint8_t)(hdr->feature_flags & 0xFF);
    out[29] = 0;
    out[30] = 0;
    out[31] = 0;

    return N148_SEQ_HEADER_SIZE;
}

int n148_seq_header_parse(const uint8_t* data, int size, N148SeqHeader* out)
{
    if (size < N148_SEQ_HEADER_SIZE)
        return -1;

    if (data[0] != N148_MAGIC_0 || data[1] != N148_MAGIC_1 ||
        data[2] != N148_MAGIC_2 || data[3] != N148_MAGIC_3)
        return -1;

    memset(out, 0, sizeof(*out));
    out->magic[0] = data[0];
    out->magic[1] = data[1];
    out->magic[2] = data[2];
    out->magic[3] = data[3];
    out->version        = data[4];
    out->profile        = data[5];
    out->level          = data[6];
    out->chroma_format  = data[7];
    out->bit_depth      = data[8];
    out->width          = (uint16_t)((data[9] << 8) | data[10]);
    out->height         = (uint16_t)((data[11] << 8) | data[12]);
    out->timescale      = ((uint32_t)data[13] << 24) | ((uint32_t)data[14] << 16) |
                          ((uint32_t)data[15] << 8) | data[16];
    out->fps_num        = (uint16_t)((data[17] << 8) | data[18]);
    out->fps_den        = (uint16_t)((data[19] << 8) | data[20]);
    out->gop_length     = (uint16_t)((data[21] << 8) | data[22]);
    out->entropy_mode   = data[23];
    out->max_ref_frames     = data[24];
    out->max_reorder_depth  = data[25];
    out->block_size_flags   = data[26];
    out->feature_flags      = (uint16_t)((data[27] << 8) | data[28]);

    return 0;
}