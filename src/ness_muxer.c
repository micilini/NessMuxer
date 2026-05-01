#include "../include/ness_muxer.h"
#include "encoder/encoder.h"
#include "mkv_muxer.h"

#ifdef NESS_HAVE_N148
#include "encoder/n148/n148_encoder.h"
#include "codec/n148/n148_spec.h"
#endif

#include <stdlib.h>
#include <string.h>

struct NessMuxer {
    void*                   encoder_ctx;
    const NessEncoderVtable* encoder_vtable;
    MkvMuxer*               muxer;
    int                     width, height, fps;
    int                     bitrate_kbps;
    int64_t                 frames_submitted;
    int64_t                 packets_written;
    int                     muxer_opened;
    NessMuxerConfig         config;
    char                    output_path[1024];
    char                    error[256];
    int64_t                 pts_override_hns;
    int                     use_pts_override;
};

static void set_error(NessMuxer* m, const char* msg)
{
    if (m && msg) {
        strncpy(m->error, msg, sizeof(m->error) - 1);
        m->error[sizeof(m->error) - 1] = '\0';
    }
}

static int on_encoded_packet(void* userdata, const NessEncodedPacket* pkt)
{
    NessMuxer* m = (NessMuxer*)userdata;

    if (!m || !pkt)
        return -1;

    if (!m->muxer_opened && pkt->is_keyframe) {
        uint8_t* cp = NULL;
        int cp_size = 0;

        if (!m->encoder_vtable || !m->encoder_vtable->get_codec_private) {
            set_error(m, "Encoder backend does not provide codec private");
            return -1;
        }

        if (m->encoder_vtable->get_codec_private(m->encoder_ctx, &cp, &cp_size) != 0 || cp_size <= 0) {
            set_error(m, "Failed to get codec private from encoder");
            return -1;
        }

        {
            NessVideoTrackDesc desc;
            memset(&desc, 0, sizeof(desc));
            desc.codec_id = m->encoder_vtable->codec_id
                            ? m->encoder_vtable->codec_id
                            : "V_MPEG4/ISO/AVC";
            desc.codec_private = cp;
            desc.codec_private_size = cp_size;
            desc.width = m->width;
            desc.height = m->height;
            desc.fps = m->fps;
            desc.bitrate_kbps = m->bitrate_kbps;
            desc.needs_annexb_conv = m->encoder_vtable->needs_annexb_conversion;

            if (mkv_muxer_open_desc(&m->muxer, m->output_path, &desc) != 0) {
                set_error(m, "mkv_muxer_open failed");
                return -1;
            }
        }

        m->muxer_opened = 1;
    }

    if (m->muxer_opened) {
        MkvPacket mkv_pkt;

        mkv_pkt.data = pkt->data;
        mkv_pkt.size = pkt->size;
        mkv_pkt.pts_hns = m->use_pts_override ? m->pts_override_hns : pkt->pts_hns;
        mkv_pkt.dts_hns = m->use_pts_override ? m->pts_override_hns : pkt->dts_hns;
        mkv_pkt.is_keyframe = pkt->is_keyframe;

        if (mkv_muxer_write_packet(m->muxer, &mkv_pkt) != 0) {
            set_error(m, "mkv_muxer_write_packet failed");
            return -1;
        }

        m->packets_written++;
    }

    return 0;
}

NESS_API int ness_muxer_open(NessMuxer** out_muxer, const NessMuxerConfig* config)
{
    NessMuxer* m;
    NessEncoderType enc_type;

    if (!out_muxer)
        return NESS_ERROR_PARAM;

    *out_muxer = NULL;

    if (!config || !config->output_path)
        return NESS_ERROR_PARAM;
    if (config->width <= 0 || config->height <= 0)
        return NESS_ERROR_PARAM;
    if (config->fps <= 0 || config->bitrate_kbps <= 0)
        return NESS_ERROR_PARAM;
    if ((config->width % 2) != 0 || (config->height % 2) != 0)
        return NESS_ERROR_PARAM;

    m = (NessMuxer*)calloc(1, sizeof(NessMuxer));
    if (!m)
        return NESS_ERROR_ALLOC;

    m->width = config->width;
    m->height = config->height;
    m->fps = config->fps;
    m->bitrate_kbps = config->bitrate_kbps;
    m->config = *config;

    strncpy(m->output_path, config->output_path, sizeof(m->output_path) - 1);
    m->output_path[sizeof(m->output_path) - 1] = '\0';
    m->config.output_path = m->output_path;

    enc_type = (NessEncoderType)config->encoder_type;

    if (config->codec_type == NESS_CODEC_N148)
        enc_type = NESS_ENCODER_N148;

    if (enc_type == NESS_ENCODER_AUTO)
        m->encoder_vtable = ness_encoder_get_best();
    else
        m->encoder_vtable = ness_encoder_get(enc_type);

    if (!m->encoder_vtable) {
        set_error(m, "No compatible encoder backend found");
        free(m);
        return NESS_ERROR_ENCODER;
    }

    if (m->encoder_vtable->create(&m->encoder_ctx,
                                  m->width,
                                  m->height,
                                  m->fps,
                                  m->bitrate_kbps) != 0) {
        set_error(m, "encoder create failed");
        free(m);
        return NESS_ERROR_ENCODER;
    }

#ifdef NESS_HAVE_N148
    if (enc_type == NESS_ENCODER_N148 && config->entropy_mode != 0) {
        int profile = (config->entropy_mode == NESS_ENTROPY_CABAC) 
                      ? N148_PROFILE_EPIC : N148_PROFILE_MAIN;
        int entropy = (config->entropy_mode == NESS_ENTROPY_CABAC)
                      ? N148_ENTROPY_CABAC : N148_ENTROPY_CAVLC;
        if (n148_encoder_set_profile_entropy_for_tests(m->encoder_ctx, profile, entropy) != 0) {
            set_error(m, "failed to set entropy mode");
            m->encoder_vtable->destroy(m->encoder_ctx);
            free(m);
            return NESS_ERROR_ENCODER;
        }
    }
#endif

    *out_muxer = m;
    return NESS_OK;
}

NESS_API int ness_muxer_write_frame(NessMuxer* muxer,
                                     const uint8_t* nv12_data,
                                     int nv12_size)
{
    int ret;

    if (!muxer || !nv12_data || nv12_size <= 0)
        return NESS_ERROR_PARAM;

    if (!muxer->encoder_vtable || !muxer->encoder_vtable->submit_frame || !muxer->encoder_vtable->receive_packets)
        return NESS_ERROR_STATE;

    ret = muxer->encoder_vtable->submit_frame(muxer->encoder_ctx, nv12_data, nv12_size);
    if (ret != 0) {
        set_error(muxer, "encoder submit_frame failed");
        return NESS_ERROR_ENCODER;
    }

    muxer->frames_submitted++;

    ret = muxer->encoder_vtable->receive_packets(muxer->encoder_ctx, on_encoded_packet, muxer);
    if (ret < 0) {
        if (muxer->error[0] == '\0')
            set_error(muxer, "encoder receive_packets failed");
        return NESS_ERROR;
    }

    return NESS_OK;
}

int ness_muxer_write_frame_pts(NessMuxer* muxer, const uint8_t* nv12_data, int nv12_size, int64_t pts_hns)
{
    int ret;
    if (!muxer || !nv12_data || nv12_size <= 0)
        return NESS_ERROR_PARAM;

    if (!muxer->encoder_vtable || !muxer->encoder_vtable->submit_frame || !muxer->encoder_vtable->receive_packets)
        return NESS_ERROR_STATE;

    muxer->pts_override_hns = pts_hns;
    muxer->use_pts_override = 1;

    ret = muxer->encoder_vtable->submit_frame(muxer->encoder_ctx, nv12_data, nv12_size);
    if (ret != 0) {
        muxer->use_pts_override = 0;
        set_error(muxer, "encoder submit_frame failed");
        return NESS_ERROR_ENCODER;
    }

    ret = muxer->encoder_vtable->receive_packets(muxer->encoder_ctx, on_encoded_packet, muxer);
    muxer->use_pts_override = 0;

    if (ret < 0) {
        if (!muxer->error[0])
            set_error(muxer, "encoder receive_packets failed");
        return NESS_ERROR_ENCODER;
    }

    muxer->frames_submitted++;
    return NESS_OK;
}

NESS_API int ness_muxer_close(NessMuxer* muxer)
{
    int ret;

    if (!muxer)
        return NESS_ERROR_PARAM;

    if (muxer->encoder_ctx && muxer->encoder_vtable) {
        ret = muxer->encoder_vtable->drain(muxer->encoder_ctx, on_encoded_packet, muxer);
        if (ret < 0 && muxer->error[0] == '\0')
            set_error(muxer, "encoder drain failed");

        if (muxer->encoder_vtable->destroy)
            muxer->encoder_vtable->destroy(muxer->encoder_ctx);

        muxer->encoder_ctx = NULL;
    }

    if (muxer->muxer) {
        mkv_muxer_close(muxer->muxer);
        mkv_muxer_destroy(muxer->muxer);
        muxer->muxer = NULL;
    }

    return NESS_OK;
}

NESS_API const char* ness_muxer_error(const NessMuxer* muxer)
{
    if (!muxer) return "null muxer";
    return muxer->error;
}

NESS_API int64_t ness_muxer_frame_count(const NessMuxer* muxer)
{
    return muxer ? muxer->frames_submitted : 0;
}

NESS_API int64_t ness_muxer_encoded_count(const NessMuxer* muxer)
{
    return muxer ? muxer->packets_written : 0;
}