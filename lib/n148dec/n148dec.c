#include "n148dec.h"

#include "decoder/n148/n148_decoder.h"

#include <stdlib.h>
#include <string.h>

struct N148DecHandle {
    N148Decoder* dec;
};

static int n148dec_fill_output(const N148DecodedFrame* frame, N148DecOutput* out)
{
    if (!frame || !frame->planes[0] || !out)
        return -1;

    memset(out, 0, sizeof(*out));
    out->planes[0] = frame->planes[0];
    out->planes[1] = frame->planes[1];
    out->planes[2] = frame->planes[2];
    out->strides[0] = frame->strides[0];
    out->strides[1] = frame->strides[1];
    out->strides[2] = frame->strides[2];
    out->width = frame->width;
    out->height = frame->height;
    out->pts = frame->pts;
    out->frame_type = frame->frame_type;
    out->pixel_format = N148DEC_PIXFMT_NV12;
    return 0;
}

int n148dec_create(N148DecHandle** handle)
{
    N148DecHandle* h = NULL;

    if (!handle)
        return -1;

    *handle = NULL;

    h = (N148DecHandle*)calloc(1, sizeof(N148DecHandle));
    if (!h)
        return -1;

    if (n148_decoder_create(&h->dec) != 0) {
        free(h);
        return -1;
    }

    *handle = h;
    return 0;
}

int n148dec_init(N148DecHandle* handle, const uint8_t* codec_private, int cp_size)
{
    if (!handle || !handle->dec || !codec_private || cp_size <= 0)
        return -1;

    return n148_decoder_init_from_seq_header(handle->dec, codec_private, cp_size);
}

int n148dec_decode_frame_ex(N148DecHandle* handle,
                            const uint8_t* bitstream,
                            int bs_size,
                            N148DecOutput* out_frame)
{
    N148DecodedFrame frame;
    int rc;

    if (!handle || !handle->dec || !out_frame)
        return -1;

    memset(&frame, 0, sizeof(frame));
    memset(out_frame, 0, sizeof(*out_frame));

    if (!bitstream || bs_size <= 0)
        rc = n148_decoder_flush(handle->dec, &frame);
    else
        rc = n148_decoder_decode(handle->dec, bitstream, bs_size, &frame);

    if (rc != 0)
        return rc;

    return n148dec_fill_output(&frame, out_frame);
}

int n148dec_decode_frame(N148DecHandle* handle,
                         const uint8_t* bitstream,
                         int bs_size,
                         uint8_t** out_yuv,
                         int* out_width,
                         int* out_height)
{
    N148DecOutput out;
    int rc;

    if (!out_yuv || !out_width || !out_height)
        return -1;

    *out_yuv = NULL;
    *out_width = 0;
    *out_height = 0;

    rc = n148dec_decode_frame_ex(handle, bitstream, bs_size, &out);
    if (rc != 0)
        return rc;

    *out_yuv = (uint8_t*)out.planes[0];
    *out_width = out.width;
    *out_height = out.height;
    return 0;
}

void n148dec_destroy(N148DecHandle* handle)
{
    if (!handle)
        return;

    if (handle->dec)
        n148_decoder_destroy(handle->dec);

    free(handle);
}