/* Versioned C API. Default x64 alignment 8. Exact size/version=2 on all envelopes.
 * CPU input is copied before Submit returns. Packet/codec views are borrowed until
 * their owned handle is released. Encoder operations are creator-thread affine.
 * GPU descriptors negotiate ownership/completion; hardware input uses a tracked-sample completion lease.
 * Normal completion is Drain -> Receive until END -> mux Finalize. Discard is not
 * completion. Release accepts NULL and never implicitly finalizes an output. */
#ifndef NESS_MUXER_V2_H
#define NESS_MUXER_V2_H
#include "ness_muxer.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NESS_V2_VERSION 2u
#define NESS_V2_OK 0
#define NESS_V2_AGAIN 1
#define NESS_V2_END 2
#define NESS_V2_E_IO (-2)
#define NESS_V2_E_ARGUMENT (-3)
#define NESS_V2_E_STATE (-4)
#define NESS_V2_E_ENCODER (-5)
#define NESS_V2_E_ALLOC (-6)
#define NESS_V2_E_VERSION (-10)
#define NESS_V2_E_UNAVAILABLE (-11)
#define NESS_V2_E_BACKPRESSURE (-12)
#define NESS_V2_E_THREAD (-13)
#define NESS_V2_E_INTERNAL (-14)
#define NESS_V2_STAGE_CONFIG 1u
#define NESS_V2_STAGE_ENCODER 2u
#define NESS_V2_STAGE_PACKET 3u
#define NESS_V2_STAGE_OUTPUT 4u
#define NESS_V2_STAGE_FINALIZE 5u
#define NESS_V2_BACKEND_MF_SOFTWARE 1u
#define NESS_V2_BACKEND_X264 2u
#define NESS_V2_BACKEND_MF_D3D11 3u
#define NESS_V2_BACKEND_QSV_D3D11 4u
#define NESS_V2_BACKEND_NVENC_D3D11 5u
#define NESS_V2_BACKEND_AMF_D3D11 6u
#define NESS_V2_POLICY_SOFTWARE 0u
#define NESS_V2_POLICY_REQUIRE_GPU 1u
#define NESS_V2_NV12 1u
#define NESS_V2_D3D11_TEXTURE2D 1u
#define NESS_V2_AVCC4 1u
#define NESS_V2_ANNEX_B 2u
#define NESS_V2_KEYFRAME 1u
#define NESS_V2_DTS_VALID 2u
#define NESS_V2_PROFILE_BASELINE 66u
#define NESS_V2_PROFILE_MAIN 77u
#define NESS_V2_PROFILE_HIGH 100u
#define NESS_V2_CBR 0u
#define NESS_V2_BT709_LIMITED 1u

typedef struct ness_encoder_v2 ness_encoder_v2;
typedef struct ness_packet_v2 ness_packet_v2;
typedef struct ness_codec_v2 ness_codec_v2;
typedef struct ness_mkv_v2 ness_mkv_v2;
typedef struct ness_input_lease_v2 ness_input_lease_v2;
typedef struct ness_bytes_v2 {
    const uint8_t *data;
    uint64_t bytes;
} ness_bytes_v2;
typedef struct ness_error_v2 {
    uint32_t size, version;
    int32_t code;
    uint32_t stage;
    int32_t native_code;
    uint32_t reserved;
    uint8_t message_utf8[256];
} ness_error_v2;
typedef struct ness_encoder_config_v2 {
    uint32_t size, version, backend, policy, width, height, fps_num, fps_den;
    uint32_t bitrate_bps, rate_control, profile, color, max_b_frames, max_pending_frames;
    uint64_t max_packet_bytes;
} ness_encoder_config_v2;
typedef struct ness_encoder_info_v2 {
    uint32_t size, version, backend, software, gpu_input, width, height, fps_num, fps_den, profile, color,
        reserved;
    uint8_t implementation_utf8[64];
} ness_encoder_info_v2;
typedef struct ness_cpu_plane_v2 {
    const uint8_t *data;
    uint64_t bytes;
    uint32_t stride, reserved;
} ness_cpu_plane_v2;
typedef struct ness_cpu_frame_v2 {
    uint32_t size, version, format, width, height, reserved;
    uint64_t frame_id;
    int64_t pts_hns, duration_hns;
    ness_cpu_plane_v2 planes[2];
} ness_cpu_frame_v2;
typedef struct ness_gpu_frame_v2 {
    uint32_t size, version, resource_type, format, width, height, subresource, reserved;
    void *texture;
    void *device;
    uint64_t adapter_luid, frame_id;
    int64_t pts_hns, duration_hns;
} ness_gpu_frame_v2;
/* Additive envelopes keep the existing V2 structure sizes unchanged. */
typedef struct ness_gpu_device_v2 {
    uint32_t size, version;
    void *device;
    uint64_t adapter_luid;
} ness_gpu_device_v2;
typedef struct ness_gpu_info_v2 {
    uint32_t size, version;
    uint64_t adapter_luid;
    uint32_t asynchronous, d3d11_aware;
    uint8_t implementation_utf8[192], hardware_url_utf8[128], clsid_utf8[64];
} ness_gpu_info_v2;

typedef struct ness_completion_v2 {
    uint32_t size, version, ready, reserved;
} ness_completion_v2;
typedef struct ness_packet_view_v2 {
    uint32_t size, version, track_id, flags, bitstream, reserved;
    uint64_t frame_id;
    int64_t pts_hns, dts_hns, duration_hns;
    ness_bytes_v2 payload;
} ness_packet_view_v2;
typedef struct ness_codec_view_v2 {
    uint32_t size, version, width, height, fps_num, fps_den, profile, color;
    ness_bytes_v2 avc_decoder_config;
} ness_codec_view_v2;
typedef struct ness_muxer_config_v2 {
    uint32_t size, version, track_id, max_buffered_packets;
    uint64_t max_buffered_bytes;
    ness_bytes_v2 output_path_utf8;
} ness_muxer_config_v2;
typedef struct ness_encoder_stats_v2 {
    uint32_t size, version;
    uint64_t submitted, emitted, pending, queued_packets, queued_bytes, discarded;
    uint32_t draining, drained, failed, reserved;
} ness_encoder_stats_v2;
typedef struct ness_muxer_stats_v2 {
    uint32_t size, version;
    uint64_t packets, buffered_packets, buffered_bytes, clusters, bytes_written;
    int64_t duration_hns;
    uint32_t finalized, failed;
} ness_muxer_stats_v2;
typedef struct ness_runtime_stats_v2 {
    uint32_t size, version;
    uint64_t encoders, muxers, packets, codecs, packet_bytes;
} ness_runtime_stats_v2;
typedef struct ness_gpu_runtime_stats_v2 {
    uint32_t size, version;
    uint64_t completion_states, input_leases;
} ness_gpu_runtime_stats_v2;

NESS_API int32_t ness_encoder_create_v2(const ness_encoder_config_v2 *, ness_encoder_v2 **, ness_error_v2 *);
NESS_API int32_t ness_encoder_create_gpu_v2(const ness_encoder_config_v2 *, const ness_gpu_device_v2 *,
                                           ness_encoder_v2 **, ness_error_v2 *);
NESS_API int32_t ness_encoder_gpu_info_get_v2(ness_encoder_v2 *, ness_gpu_info_v2 *, ness_error_v2 *);
NESS_API int32_t ness_encoder_probe_v2(const ness_encoder_config_v2 *, ness_encoder_info_v2 *,
                                       ness_error_v2 *);
NESS_API int32_t ness_encoder_info_get_v2(ness_encoder_v2 *, ness_encoder_info_v2 *, ness_error_v2 *);
NESS_API int32_t ness_encoder_submit_cpu_v2(ness_encoder_v2 *, const ness_cpu_frame_v2 *, ness_error_v2 *);
NESS_API int32_t ness_encoder_submit_gpu_v2(ness_encoder_v2 *, const ness_gpu_frame_v2 *,
                                            ness_input_lease_v2 **, ness_error_v2 *);
NESS_API int32_t ness_input_completion_get_v2(ness_input_lease_v2 *, ness_completion_v2 *, ness_error_v2 *);
NESS_API void ness_input_release_v2(ness_input_lease_v2 *);
NESS_API int32_t ness_encoder_receive_v2(ness_encoder_v2 *, ness_packet_v2 **, ness_error_v2 *);
NESS_API int32_t ness_encoder_description_v2(ness_encoder_v2 *, ness_codec_v2 **, ness_error_v2 *);
NESS_API int32_t ness_encoder_drain_v2(ness_encoder_v2 *, ness_error_v2 *);
NESS_API int32_t ness_encoder_discard_v2(ness_encoder_v2 *, ness_error_v2 *);
NESS_API int32_t ness_encoder_stats_get_v2(ness_encoder_v2 *, ness_encoder_stats_v2 *, ness_error_v2 *);
NESS_API void ness_encoder_release_v2(ness_encoder_v2 *);
NESS_API int32_t ness_packet_copy_v2(const ness_packet_view_v2 *, ness_packet_v2 **, ness_error_v2 *);
NESS_API int32_t ness_packet_view_get_v2(const ness_packet_v2 *, ness_packet_view_v2 *, ness_error_v2 *);
NESS_API void ness_packet_release_v2(ness_packet_v2 *);
NESS_API int32_t ness_codec_copy_v2(const ness_codec_view_v2 *, ness_codec_v2 **, ness_error_v2 *);
NESS_API int32_t ness_codec_view_get_v2(const ness_codec_v2 *, ness_codec_view_v2 *, ness_error_v2 *);
NESS_API void ness_codec_release_v2(ness_codec_v2 *);
NESS_API int32_t ness_mkv_open_v2(const ness_muxer_config_v2 *, const ness_codec_v2 *, ness_mkv_v2 **,
                                  ness_error_v2 *);
NESS_API int32_t ness_mkv_set_codec_v2(ness_mkv_v2 *, const ness_codec_v2 *, ness_error_v2 *);
NESS_API int32_t ness_mkv_write_packet_v2(ness_mkv_v2 *, const ness_packet_v2 *, ness_error_v2 *);
NESS_API int32_t ness_mkv_finalize_v2(ness_mkv_v2 *, ness_error_v2 *);
NESS_API int32_t ness_mkv_discard_v2(ness_mkv_v2 *, ness_error_v2 *);
NESS_API int32_t ness_mkv_stats_get_v2(ness_mkv_v2 *, ness_muxer_stats_v2 *, ness_error_v2 *);
NESS_API void ness_mkv_release_v2(ness_mkv_v2 *);
NESS_API int32_t ness_runtime_stats_get_v2(ness_runtime_stats_v2 *, ness_error_v2 *);
NESS_API int32_t ness_gpu_runtime_stats_get_v2(ness_gpu_runtime_stats_v2 *, ness_error_v2 *);
#ifdef __cplusplus
}
#endif
#endif
