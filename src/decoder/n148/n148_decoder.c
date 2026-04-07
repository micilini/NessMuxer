#include "n148_decoder.h"
#include "n148_parser.h"
#include "n148_frame_recon.h"
#include "../../codec/n148/n148_spec.h"
#include "../../codec/n148/n148_codec_private.h"
#include "../../codec/n148/n148_bitstream.h"

#include <stdlib.h>
#include <string.h>

#define N148_MAX_NALS 64

struct N148Decoder {
    N148SeqHeader   seq_hdr;
    int             seq_hdr_valid;
    int             width, height;
    int             stride;

   
    uint8_t*        frame_buf;
    int             frame_buf_size;
};

int n148_decoder_create(N148Decoder** out)
{
    N148Decoder* dec;

    if (!out) return -1;
    *out = NULL;

    dec = (N148Decoder*)calloc(1, sizeof(N148Decoder));
    if (!dec) return -1;

    *out = dec;
    return 0;
}

int n148_decoder_init_from_seq_header(N148Decoder* dec, const uint8_t* data, int size)
{
    if (!dec || !data) return -1;

    if (n148_seq_header_parse(data, size, &dec->seq_hdr) != 0)
        return -1;

    dec->width  = dec->seq_hdr.width;
    dec->height = dec->seq_hdr.height;
    dec->stride = dec->width;
    dec->seq_hdr_valid = 1;

   
    dec->frame_buf_size = dec->width * dec->height * 3 / 2;
    dec->frame_buf = (uint8_t*)malloc(dec->frame_buf_size);
    if (!dec->frame_buf) return -1;

    return 0;
}

int n148_decoder_decode(N148Decoder* dec, const uint8_t* data, int size,
                        N148DecodedFrame* out_frame)
{
    N148NalUnit nals[N148_MAX_NALS];
    int nal_count = 0;
    N148FrameHeader frm_hdr;
    int frm_hdr_found = 0;
    int slice_found = 0;
    int i;

    if (!dec || !data || size <= 0 || !out_frame)
        return -1;

    memset(out_frame, 0, sizeof(*out_frame));

   
    n148_find_nal_units(data, size, nals, N148_MAX_NALS, &nal_count);

    if (nal_count == 0)
        return -1;

   
    for (i = 0; i < nal_count; i++) {
        switch (nals[i].nal_type) {
        case N148_NAL_SEQ_HDR:
            if (!dec->seq_hdr_valid) {
                if (n148_decoder_init_from_seq_header(dec, nals[i].payload,
                                                      nals[i].payload_size) != 0)
                    return -1;
            }
            break;

        case N148_NAL_FRM_HDR:
            if (n148_parse_frame_header(nals[i].payload, nals[i].payload_size, &frm_hdr) != 0)
                return -1;
            frm_hdr_found = 1;
            break;

        case N148_NAL_IDR:
        case N148_NAL_SLICE: {
            N148BsReader bs;

            if (!dec->seq_hdr_valid || !frm_hdr_found)
                return -1;

           
            memset(dec->frame_buf, 0, dec->frame_buf_size);

            n148_bs_reader_init(&bs, nals[i].payload, nals[i].payload_size);

            if (frm_hdr.frame_type == N148_FRAME_I) {
               
                {
                    uint32_t dummy16;
                    if (n148_bs_read_bits(&bs, 16, &dummy16) != 0) return -1;
                    if (n148_bs_read_bits(&bs, 16, &dummy16) != 0) return -1;
                }

                if (n148_reconstruct_iframe(
                        dec->frame_buf,
                        dec->frame_buf + dec->width * dec->height,
                        dec->stride, dec->width, dec->height,
                        frm_hdr.qp_base, &bs) != 0)
                    return -1;
            } else {
               
                return -1;
            }

            slice_found = 1;
            break;
        }

        default:
            break; 
        }
    }

    if (!slice_found)
        return -1;

   
    out_frame->planes[0]  = dec->frame_buf;
    out_frame->planes[1]  = dec->frame_buf + dec->width * dec->height;
    out_frame->planes[2]  = NULL;
    out_frame->strides[0] = dec->stride;
    out_frame->strides[1] = dec->stride;
    out_frame->strides[2] = 0;
    out_frame->width      = dec->width;
    out_frame->height     = dec->height;
    out_frame->pts        = frm_hdr.pts;
    out_frame->frame_type = frm_hdr.frame_type;

    return 0;
}

int n148_decoder_flush(N148Decoder* dec, N148DecodedFrame* out_frame)
{
   
    (void)dec;
    if (out_frame) memset(out_frame, 0, sizeof(*out_frame));
    return -1; 
}

void n148_decoder_destroy(N148Decoder* dec)
{
    if (!dec) return;
    if (dec->frame_buf) free(dec->frame_buf);
    free(dec);
}