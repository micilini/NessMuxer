

#include "libavcodec/avcodec.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"

#include "n148dec.h"

#ifndef AVERROR_INVALIDDATA
#define AVERROR_INVALIDDATA AVERROR(EINVAL)
#endif

typedef struct N148FFDecoderContext {
    N148DecHandle* dec;
    int            initialized;
} N148FFDecoderContext;

static enum AVPictureType n148_ff_map_picture_type(int frame_type)
{
    switch (frame_type) {
    case 1: return AV_PICTURE_TYPE_I;
    case 2: return AV_PICTURE_TYPE_P;
    case 3: return AV_PICTURE_TYPE_B;
    default: return AV_PICTURE_TYPE_NONE;
    }
}

static int n148_ff_copy_nv12_to_avframe(AVCodecContext* avctx,
                                        AVFrame* frame,
                                        const N148DecOutput* out)
{
    int y;
    int ret;
    const uint8_t* src_y;
    const uint8_t* src_uv;

    if (!avctx || !frame || !out || !out->planes[0] || !out->planes[1])
        return AVERROR(EINVAL);

    if (out->pixel_format != N148DEC_PIXFMT_NV12)
        return AVERROR_INVALIDDATA;

    frame->format = AV_PIX_FMT_NV12;
    frame->width  = out->width;
    frame->height = out->height;

    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0)
        return ret;

    ret = av_frame_make_writable(frame);
    if (ret < 0)
        return ret;

    src_y  = out->planes[0];
    src_uv = out->planes[1];

    for (y = 0; y < out->height; y++) {
        memcpy(frame->data[0] + y * frame->linesize[0],
               src_y + y * out->strides[0],
               (size_t)out->width);
    }

    for (y = 0; y < out->height / 2; y++) {
        memcpy(frame->data[1] + y * frame->linesize[1],
               src_uv + y * out->strides[1],
               (size_t)out->width);
    }

    frame->pts       = out->pts;
    frame->pict_type = n148_ff_map_picture_type(out->frame_type);
    frame->key_frame = (out->frame_type == 1);

    avctx->pix_fmt      = AV_PIX_FMT_NV12;
    avctx->width        = out->width;
    avctx->height       = out->height;
    avctx->has_b_frames = 1;

    return 0;
}

int n148_ff_decoder_init(AVCodecContext* avctx)
{
    N148FFDecoderContext* ctx = (N148FFDecoderContext*)avctx->priv_data;

    if (!ctx)
        return AVERROR(EINVAL);

    memset(ctx, 0, sizeof(*ctx));

    if (n148dec_create(&ctx->dec) != 0)
        return AVERROR(ENOMEM);

    if (avctx->extradata && avctx->extradata_size > 0) {
        if (n148dec_init(ctx->dec, avctx->extradata, avctx->extradata_size) != 0)
            return AVERROR_INVALIDDATA;
        ctx->initialized = 1;
    }

    avctx->pix_fmt = AV_PIX_FMT_NV12;
    return 0;
}

int n148_ff_decoder_close(AVCodecContext* avctx)
{
    N148FFDecoderContext* ctx = (N148FFDecoderContext*)avctx->priv_data;

    if (ctx && ctx->dec) {
        n148dec_destroy(ctx->dec);
        ctx->dec = NULL;
    }

    if (ctx)
        ctx->initialized = 0;

    return 0;
}

void n148_ff_decoder_flush(AVCodecContext* avctx)
{
    N148FFDecoderContext* ctx = (N148FFDecoderContext*)avctx->priv_data;

    if (!ctx || !ctx->dec)
        return;

    n148dec_destroy(ctx->dec);
    ctx->dec = NULL;
    ctx->initialized = 0;

    if (n148dec_create(&ctx->dec) != 0)
        return;

    if (avctx->extradata && avctx->extradata_size > 0) {
        if (n148dec_init(ctx->dec, avctx->extradata, avctx->extradata_size) == 0)
            ctx->initialized = 1;
    }
}

int n148_ff_decoder_decode(AVCodecContext* avctx,
                           AVFrame* frame,
                           int* got_frame,
                           AVPacket* pkt)
{
    N148FFDecoderContext* ctx = (N148FFDecoderContext*)avctx->priv_data;
    N148DecOutput out;
    int rc;

    if (!ctx || !ctx->dec || !frame || !got_frame)
        return AVERROR(EINVAL);

    *got_frame = 0;
    memset(&out, 0, sizeof(out));

    if (!ctx->initialized && avctx->extradata && avctx->extradata_size > 0) {
        if (n148dec_init(ctx->dec, avctx->extradata, avctx->extradata_size) != 0)
            return AVERROR_INVALIDDATA;
        ctx->initialized = 1;
    }

    if (pkt && pkt->size > 0)
        rc = n148dec_decode_frame_ex(ctx->dec, pkt->data, pkt->size, &out);
    else
        rc = n148dec_decode_frame_ex(ctx->dec, NULL, 0, &out);

    if (rc == 1)
        return pkt ? pkt->size : 0;

    if (rc < 0)
        return AVERROR_INVALIDDATA;

    rc = n148_ff_copy_nv12_to_avframe(avctx, frame, &out);
    if (rc < 0)
        return rc;

    *got_frame = 1;
    return pkt ? pkt->size : 0;
}