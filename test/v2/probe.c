#include "ness_muxer_v2.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
_Static_assert(sizeof(void *) == 8, "Qualification requires x64");
_Static_assert(sizeof(NessMuxerConfig) == 40, "Legacy ABI size");
_Static_assert(offsetof(NessMuxerConfig, output_path) == 0, "Legacy path offset");
_Static_assert(offsetof(NessMuxerConfig, width) == 8, "Legacy width offset");
_Static_assert(offsetof(NessMuxerConfig, entropy_mode) == 32, "Legacy tail offset");
_Static_assert(sizeof(ness_bytes_v2) == 16, "bytes ABI");
_Static_assert(sizeof(ness_error_v2) == 280, "error ABI");
_Static_assert(sizeof(ness_encoder_config_v2) == 64, "encoder config ABI");
_Static_assert(sizeof(ness_encoder_info_v2) == 112, "encoder info ABI");
_Static_assert(sizeof(ness_cpu_plane_v2) == 24, "plane ABI");
_Static_assert(sizeof(ness_cpu_frame_v2) == 96, "CPU frame ABI");
_Static_assert(sizeof(ness_gpu_frame_v2) == 80, "GPU frame ABI");
_Static_assert(sizeof(ness_completion_v2) == 16, "completion ABI");
_Static_assert(sizeof(ness_packet_view_v2) == 72, "packet ABI");
_Static_assert(sizeof(ness_codec_view_v2) == 48, "codec ABI");
_Static_assert(sizeof(ness_muxer_config_v2) == 40, "muxer config ABI");
_Static_assert(sizeof(ness_encoder_stats_v2) == 72, "encoder stats ABI");
_Static_assert(sizeof(ness_muxer_stats_v2) == 64, "muxer stats ABI");
_Static_assert(sizeof(ness_runtime_stats_v2) == 48, "runtime stats ABI");
_Static_assert(offsetof(ness_packet_view_v2, frame_id) == 24, "packet identity offset");
_Static_assert(offsetof(ness_packet_view_v2, payload) == 56, "packet bytes offset");
int main(void) {
    ness_encoder_config_v2 c = {0};
    ness_encoder_info_v2 info = {0};
    ness_error_v2 e = {0};
    ness_runtime_stats_v2 stats = {0};
    c.size = sizeof(c);
    c.version = 2;
    c.backend = 1;
    c.width = 320;
    c.height = 240;
    c.fps_num = 30000;
    c.fps_den = 1001;
    c.bitrate_bps = 800000;
    c.profile = 66;
    c.color = 1;
    info.size = sizeof(info);
    info.version = 2;
    e.size = sizeof(e);
    e.version = 2;
    int result = ness_encoder_probe_v2(&c, &info, &e);
    stats.size = sizeof(stats);
    stats.version = 2;
    ness_runtime_stats_get_v2(&stats, NULL);
    printf("{\"result\":%d,\"message\":\"%s\",\"native\":%d,\"software\":%u,\"fps_num\":%u,\"fps_den\":%u,"
           "\"live_encoders\":%llu,\"live_packets\":%llu}\n",
           result, e.message_utf8, e.native_code, info.software, info.fps_num, info.fps_den,
           (unsigned long long)stats.encoders, (unsigned long long)stats.packets);
    return result < 0 ? 1 : 0;
}
