#ifndef NESS_MKV_MUXER_H
#define NESS_MKV_MUXER_H

#include <stdint.h>

typedef struct MkvMuxer MkvMuxer;

typedef struct {
    const uint8_t* data;
    int            size;
    int64_t        pts_hns;
    int64_t        dts_hns;
    int            is_keyframe;
} MkvPacket;

int  mkv_muxer_open(MkvMuxer** out_mux, const char* path,
                     int width, int height, int fps, int bitrate_kbps,
                     const uint8_t* codec_private, int codec_private_size);
int  mkv_muxer_write_packet(MkvMuxer* mux, const MkvPacket* pkt);
int  mkv_muxer_close(MkvMuxer* mux);
void mkv_muxer_destroy(MkvMuxer* mux);

int64_t mkv_muxer_get_frame_count(const MkvMuxer* mux);
int64_t mkv_muxer_get_last_pts_ms(const MkvMuxer* mux);

#endif
