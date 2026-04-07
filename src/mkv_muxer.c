#include "mkv_muxer.h"
#include "buffered_io.h"
#include "ebml_writer.h"
#include "mkv_defs.h"
#include "avc_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MKV_SEEKHEAD_RESERVED   160
#define MKV_CLUSTER_MAX_FRAMES  30
#define MKV_TIMESTAMP_SCALE     1000000
#define MKV_MAX_CUES            8192

typedef struct {
    int64_t pts_ms;
    int64_t cluster_pos;
} MkvCueEntry;

struct MkvMuxer {
    NessIO*     io;

    int         width, height, fps;
    int         bitrate_kbps;

    int64_t     segment_offset;
    int64_t     seekhead_offset;
    int64_t     info_offset;
    int64_t     duration_offset;
    int64_t     tracks_offset;
    int64_t     cues_offset;

    int64_t     cluster_pos;
    int64_t     cluster_pts_ms;
    int         cluster_block_count;
    ebml_master cluster_master;
    int         cluster_open;

    MkvCueEntry* cues;
    int         cue_count;
    int         cue_capacity;

    int64_t     frame_count;
    int64_t     last_pts_ms;

    uint8_t*    codec_private;
    int         codec_private_size;

    uint8_t*    convert_buf;
    int         convert_buf_size;

    int         needs_annexb_conv;
    char        codec_id[64];

    char        error[256];
};

static void set_error(MkvMuxer* mux, const char* msg)
{
    if (mux && msg)
        strncpy(mux->error, msg, sizeof(mux->error) - 1);
}

static void add_cue(MkvMuxer* mux, int64_t pts_ms, int64_t cluster_pos)
{
    if (mux->cue_count >= mux->cue_capacity) {
        int new_cap = mux->cue_capacity ? mux->cue_capacity * 2 : 256;
        if (new_cap > MKV_MAX_CUES) new_cap = MKV_MAX_CUES;
        if (mux->cue_count >= new_cap) return;
        MkvCueEntry* tmp = (MkvCueEntry*)realloc(mux->cues, new_cap * sizeof(MkvCueEntry));
        if (!tmp) return;
        mux->cues = tmp;
        mux->cue_capacity = new_cap;
    }
    mux->cues[mux->cue_count].pts_ms = pts_ms;
    mux->cues[mux->cue_count].cluster_pos = cluster_pos;
    mux->cue_count++;
}

static void write_ebml_header(NessIO* io)
{
    ebml_master header = ebml_start_master(io, EBML_ID_HEADER, 0);
    ebml_put_uint(io, EBML_ID_EBMLVERSION, 1);
    ebml_put_uint(io, EBML_ID_EBMLREADVERSION, 1);
    ebml_put_uint(io, EBML_ID_EBMLMAXIDLENGTH, 4);
    ebml_put_uint(io, EBML_ID_EBMLMAXSIZELENGTH, 8);
    ebml_put_string(io, EBML_ID_DOCTYPE, "matroska");
    ebml_put_uint(io, EBML_ID_DOCTYPEVERSION, 4);
    ebml_put_uint(io, EBML_ID_DOCTYPEREADVERSION, 2);
    ebml_end_master(io, header);
}

static void write_info(MkvMuxer* mux)
{
    ebml_master info;
    mux->info_offset = nio_tell(mux->io) - mux->segment_offset;

    info = ebml_start_master(mux->io, MATROSKA_ID_INFO, 0);
    ebml_put_uint(mux->io, MATROSKA_ID_TIMESTAMPSCALE, MKV_TIMESTAMP_SCALE);
    ebml_put_string(mux->io, MATROSKA_ID_MUXINGAPP, "NessMuxer");
    ebml_put_string(mux->io, MATROSKA_ID_WRITINGAPP, "NessStudio");

    mux->duration_offset = nio_tell(mux->io);
    ebml_put_float(mux->io, MATROSKA_ID_DURATION, 0.0);

    ebml_end_master(mux->io, info);
}

static void write_tracks(MkvMuxer* mux)
{
    ebml_master tracks, track_entry, video;
    int64_t default_duration_ns = 1000000000LL / mux->fps;

    mux->tracks_offset = nio_tell(mux->io) - mux->segment_offset;

    tracks = ebml_start_master(mux->io, MATROSKA_ID_TRACKS, 0);

    track_entry = ebml_start_master(mux->io, MATROSKA_ID_TRACKENTRY, 0);
    ebml_put_uint(mux->io, MATROSKA_ID_TRACKNUMBER, 1);
    ebml_put_uint(mux->io, MATROSKA_ID_TRACKUID, 1);
    ebml_put_uint(mux->io, MATROSKA_ID_TRACKTYPE, MATROSKA_TRACK_TYPE_VIDEO);
    ebml_put_uint(mux->io, MATROSKA_ID_FLAGLACING, 0);
    ebml_put_uint(mux->io, MATROSKA_ID_TRACKDEFAULTDURATION, (uint64_t)default_duration_ns);
    ebml_put_string(mux->io, MATROSKA_ID_CODECID, mux->codec_id);
    ebml_put_binary(mux->io, MATROSKA_ID_CODECPRIVATE,
                    mux->codec_private, mux->codec_private_size);

    video = ebml_start_master(mux->io, MATROSKA_ID_TRACKVIDEO, 0);
    ebml_put_uint(mux->io, MATROSKA_ID_VIDEOPIXELWIDTH, (uint64_t)mux->width);
    ebml_put_uint(mux->io, MATROSKA_ID_VIDEOPIXELHEIGHT, (uint64_t)mux->height);
    ebml_put_uint(mux->io, MATROSKA_ID_VIDEODISPLAYWIDTH, (uint64_t)mux->width);
    ebml_put_uint(mux->io, MATROSKA_ID_VIDEODISPLAYHEIGHT, (uint64_t)mux->height);
    ebml_end_master(mux->io, video);

    ebml_end_master(mux->io, track_entry);
    ebml_end_master(mux->io, tracks);
}

static void close_cluster(MkvMuxer* mux)
{
    if (!mux->cluster_open) return;
    ebml_end_master(mux->io, mux->cluster_master);
    mux->cluster_open = 0;
}

static void open_cluster(MkvMuxer* mux, int64_t pts_ms)
{
    if (mux->cluster_open)
        close_cluster(mux);

    mux->cluster_pos = nio_tell(mux->io) - mux->segment_offset;
    mux->cluster_master = ebml_start_master(mux->io, MATROSKA_ID_CLUSTER, 0);
    ebml_put_uint(mux->io, MATROSKA_ID_CLUSTERTIMESTAMP, (uint64_t)pts_ms);
    mux->cluster_pts_ms = pts_ms;
    mux->cluster_block_count = 0;
    mux->cluster_open = 1;
}


int mkv_muxer_open_desc(MkvMuxer** out_mux, const char* path,
                        const NessVideoTrackDesc* desc)
{
    MkvMuxer* mux;

    *out_mux = NULL;

    if (!path || !desc || desc->width <= 0 || desc->height <= 0 || desc->fps <= 0)
        return -1;
    if (!desc->codec_private || desc->codec_private_size <= 0)
        return -1;
    if (!desc->codec_id || desc->codec_id[0] == '\0')
        return -1;

    mux = (MkvMuxer*)calloc(1, sizeof(MkvMuxer));
    if (!mux) return -1;

    mux->width = desc->width;
    mux->height = desc->height;
    mux->fps = desc->fps;
    mux->bitrate_kbps = desc->bitrate_kbps;
    mux->cluster_open = 0;
    mux->last_pts_ms = 0;
    mux->needs_annexb_conv = desc->needs_annexb_conv;

    strncpy(mux->codec_id, desc->codec_id, sizeof(mux->codec_id) - 1);
    mux->codec_id[sizeof(mux->codec_id) - 1] = '\0';

    mux->codec_private = (uint8_t*)malloc(desc->codec_private_size);
    if (!mux->codec_private) { free(mux); return -1; }
    memcpy(mux->codec_private, desc->codec_private, desc->codec_private_size);
    mux->codec_private_size = desc->codec_private_size;

    mux->convert_buf_size = desc->width * desc->height * 2;
    mux->convert_buf = (uint8_t*)malloc(mux->convert_buf_size);
    if (!mux->convert_buf) { free(mux->codec_private); free(mux); return -1; }

    mux->io = nio_open(path);
    if (!mux->io) {
        set_error(mux, "Failed to open output file");
        free(mux->convert_buf);
        free(mux->codec_private);
        free(mux);
        return -1;
    }

    write_ebml_header(mux->io);

    ebml_put_id(mux->io, MATROSKA_ID_SEGMENT);
    ebml_put_size_unknown(mux->io, 8);
    mux->segment_offset = nio_tell(mux->io);

    mux->seekhead_offset = nio_tell(mux->io) - mux->segment_offset;
    ebml_put_void(mux->io, MKV_SEEKHEAD_RESERVED);

    write_info(mux);
    write_tracks(mux);

    *out_mux = mux;
    return 0;
}

int mkv_muxer_open(MkvMuxer** out_mux, const char* path,
                    int width, int height, int fps, int bitrate_kbps,
                    const uint8_t* codec_private, int codec_private_size)
{
    NessVideoTrackDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.codec_id = "V_MPEG4/ISO/AVC";
    desc.codec_private = codec_private;
    desc.codec_private_size = codec_private_size;
    desc.width = width;
    desc.height = height;
    desc.fps = fps;
    desc.bitrate_kbps = bitrate_kbps;
    desc.needs_annexb_conv = 1;
    return mkv_muxer_open_desc(out_mux, path, &desc);
}

int mkv_muxer_write_packet(MkvMuxer* mux, const MkvPacket* pkt)
{
    int64_t pts_ms;
    int16_t rel_ts;
    int mp4_size = 0;
    int block_data_size;
    int need_new_cluster;

    if (!mux || !mux->io || !pkt || !pkt->data || pkt->size <= 0)
        return -1;

    pts_ms = pkt->pts_hns / 10000;

    need_new_cluster = !mux->cluster_open ||
                       mux->cluster_block_count >= MKV_CLUSTER_MAX_FRAMES ||
                       (pkt->is_keyframe && mux->cluster_block_count > 0);

    if (need_new_cluster) {
        open_cluster(mux, pts_ms);
        if (pkt->is_keyframe)
            add_cue(mux, pts_ms, mux->cluster_pos);
    }

    if (mux->needs_annexb_conv) {
        if (avc_annexb_to_mp4(pkt->data, pkt->size,
                               mux->convert_buf, mux->convert_buf_size,
                               &mp4_size) != 0 || mp4_size <= 0) {
            set_error(mux, "avc_annexb_to_mp4 failed");
            return -1;
        }
    }

    rel_ts = (int16_t)(pts_ms - mux->cluster_pts_ms);

    if (mux->needs_annexb_conv) {
        block_data_size = 1 + 2 + 1 + mp4_size;
    } else {
        block_data_size = 1 + 2 + 1 + pkt->size;
    }

    ebml_put_id(mux->io, MATROSKA_ID_SIMPLEBLOCK);
    ebml_put_size(mux->io, (uint64_t)block_data_size, 0);

    nio_w8(mux->io, 0x81);
    nio_wb16(mux->io, (uint16_t)rel_ts);
    nio_w8(mux->io, pkt->is_keyframe ? 0x80 : 0x00);

    if (mux->needs_annexb_conv) {
        nio_write(mux->io, mux->convert_buf, mp4_size);
    } else {
        nio_write(mux->io, pkt->data, pkt->size);
    }

    mux->cluster_block_count++;
    mux->frame_count++;
    if (pts_ms > mux->last_pts_ms)
        mux->last_pts_ms = pts_ms;

    return 0;
}

static void write_cues(MkvMuxer* mux)
{
    int i;
    ebml_master cues_master;

    mux->cues_offset = nio_tell(mux->io) - mux->segment_offset;

    cues_master = ebml_start_master(mux->io, MATROSKA_ID_CUES, 0);

    for (i = 0; i < mux->cue_count; i++) {
        ebml_master cue_point = ebml_start_master(mux->io, MATROSKA_ID_CUEPOINT, 0);
        ebml_put_uint(mux->io, MATROSKA_ID_CUETIME, (uint64_t)mux->cues[i].pts_ms);

        {
            ebml_master cue_track = ebml_start_master(mux->io, MATROSKA_ID_CUETRACKPOSITIONS, 0);
            ebml_put_uint(mux->io, MATROSKA_ID_CUETRACK, 1);
            ebml_put_uint(mux->io, MATROSKA_ID_CUECLUSTERPOSITION,
                          (uint64_t)mux->cues[i].cluster_pos);
            ebml_end_master(mux->io, cue_track);
        }

        ebml_end_master(mux->io, cue_point);
    }

    ebml_end_master(mux->io, cues_master);
}

static void write_seekhead(MkvMuxer* mux)
{
    int64_t seekhead_abs = mux->segment_offset + mux->seekhead_offset;
    int64_t current_pos;
    int64_t remaining;
    ebml_master seekhead_master;

    nio_seek(mux->io, seekhead_abs, SEEK_SET);

    seekhead_master = ebml_start_master(mux->io, MATROSKA_ID_SEEKHEAD, 0);

    {
        ebml_master entry = ebml_start_master(mux->io, MATROSKA_ID_SEEKENTRY, 21);
        ebml_put_binary(mux->io, MATROSKA_ID_SEEKID, "\x15\x49\xA9\x66", 4);
        ebml_put_uint(mux->io, MATROSKA_ID_SEEKPOSITION, (uint64_t)mux->info_offset);
        ebml_end_master(mux->io, entry);
    }

    {
        ebml_master entry = ebml_start_master(mux->io, MATROSKA_ID_SEEKENTRY, 21);
        ebml_put_binary(mux->io, MATROSKA_ID_SEEKID, "\x16\x54\xAE\x6B", 4);
        ebml_put_uint(mux->io, MATROSKA_ID_SEEKPOSITION, (uint64_t)mux->tracks_offset);
        ebml_end_master(mux->io, entry);
    }

    if (mux->cues_offset > 0) {
        ebml_master entry = ebml_start_master(mux->io, MATROSKA_ID_SEEKENTRY, 21);
        ebml_put_binary(mux->io, MATROSKA_ID_SEEKID, "\x1C\x53\xBB\x6B", 4);
        ebml_put_uint(mux->io, MATROSKA_ID_SEEKPOSITION, (uint64_t)mux->cues_offset);
        ebml_end_master(mux->io, entry);
    }

    ebml_end_master(mux->io, seekhead_master);

    current_pos = nio_tell(mux->io);
    remaining = (seekhead_abs + MKV_SEEKHEAD_RESERVED) - current_pos;
    if (remaining >= 2)
        ebml_put_void(mux->io, (int)remaining);
}

static void patch_duration(MkvMuxer* mux)
{
    double duration_ms = (double)mux->last_pts_ms + (1000.0 / mux->fps);
    nio_seek(mux->io, mux->duration_offset, SEEK_SET);
    ebml_put_float(mux->io, MATROSKA_ID_DURATION, duration_ms);
}

int mkv_muxer_close(MkvMuxer* mux)
{
    if (!mux) return -1;

    close_cluster(mux);
    write_cues(mux);
    write_seekhead(mux);
    patch_duration(mux);

    nio_flush(mux->io);
    nio_close(mux->io);
    mux->io = NULL;

    return 0;
}

void mkv_muxer_destroy(MkvMuxer* mux)
{
    if (!mux) return;
    if (mux->io) nio_close(mux->io);
    if (mux->codec_private) free(mux->codec_private);
    if (mux->convert_buf) free(mux->convert_buf);
    if (mux->cues) free(mux->cues);
    free(mux);
}

int64_t mkv_muxer_get_frame_count(const MkvMuxer* mux)
{
    return mux ? mux->frame_count : 0;
}

int64_t mkv_muxer_get_last_pts_ms(const MkvMuxer* mux)
{
    return mux ? mux->last_pts_ms : 0;
}
