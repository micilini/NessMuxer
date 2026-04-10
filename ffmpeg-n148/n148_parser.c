

#include "libavcodec/avcodec.h"
#include "parser.h"

typedef struct N148ParseContext {
    int dummy;
} N148ParseContext;

static int n148_ff_is_keyframe_packet(const uint8_t* buf, int buf_size)
{
    int pos = 0;

    if (!buf || buf_size <= 0)
        return 0;

   
    while (pos + 5 <= buf_size) {
        int nal_size = ((int)buf[pos] << 24) |
                       ((int)buf[pos + 1] << 16) |
                       ((int)buf[pos + 2] << 8) |
                       (int)buf[pos + 3];
        int nal_type;

        pos += 4;
        if (nal_size <= 0 || pos + nal_size > buf_size)
            break;

        nal_type = (buf[pos] >> 4) & 0x07;
        if (nal_type == 1)
            return 1;

        pos += nal_size;
    }

    return 0;
}

static int n148_ff_parser_parse(AVCodecParserContext* s,
                                AVCodecContext* avctx,
                                const uint8_t** poutbuf,
                                int* poutbuf_size,
                                const uint8_t* buf,
                                int buf_size)
{
    (void)avctx;

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    if (!buf || buf_size <= 0)
        return 0;

    s->key_frame = n148_ff_is_keyframe_packet(buf, buf_size);
    return buf_size;
}

AVCodecParser ff_n148_parser = {
    .codec_ids      = { AV_CODEC_ID_NONE },
    .priv_data_size = sizeof(N148ParseContext),
    .parser_parse   = n148_ff_parser_parse,
    .parser_close   = ff_parse_close,
};