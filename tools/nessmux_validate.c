#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "buffered_io.h"
#include "mkv_defs.h"

typedef struct
{
    int ebml_version;
    int doctype_version;
    int doctype_read_version;
    char doctype[64];

    int64_t segment_offset;

    int seekhead_entries;

    uint64_t timestamp_scale;
    double duration_ms;

    int track_count;
    int video_width;
    int video_height;
    char codec_id[128];

    int codec_private_size;
    int avcc_version;
    int avcc_profile;
    int avcc_level;

    int cluster_count;
    int block_count;
    int keyframe_count;
    int cue_count;
} ValidatorStats;

static int64_t get_file_size(NessIO* io)
{
    int64_t cur;
    int64_t end;

    cur = nio_tell(io);
    if (cur < 0)
        return -1;

    if (nio_seek(io, 0, SEEK_END) != 0)
        return -1;

    end = nio_tell(io);

    if (nio_seek(io, cur, SEEK_SET) != 0)
        return -1;

    return end;
}

static int skip_bytes(NessIO* io, uint64_t size)
{
    int64_t cur = nio_tell(io);
    if (cur < 0)
        return -1;
    return nio_seek(io, cur + (int64_t)size, SEEK_SET);
}

static int ebml_vint_len_from_first(uint8_t first)
{
    int len;

    for (len = 1; len <= 8; len++) {
        if (first & (1U << (8 - len)))
            return len;
    }

    return -1;
}

static int ebml_read_id(NessIO* io, uint32_t* out_id, int* out_len)
{
    int first;
    int len;
    int i;
    uint32_t value;

    first = nio_r8(io);
    if (first < 0)
        return -1;

    len = ebml_vint_len_from_first((uint8_t)first);
    if (len < 1 || len > 4)
        return -1;

    value = (uint32_t)(uint8_t)first;

    for (i = 1; i < len; i++) {
        int b = nio_r8(io);
        if (b < 0)
            return -1;
        value = (value << 8) | (uint32_t)(b & 0xFF);
    }

    if (out_id)
        *out_id = value;
    if (out_len)
        *out_len = len;

    return 0;
}

static int ebml_read_size(NessIO* io, uint64_t* out_size, int* out_len, int* out_unknown)
{
    int first;
    int len;
    int i;
    uint64_t value;
    uint64_t mask;
    int unknown = 1;

    first = nio_r8(io);
    if (first < 0)
        return -1;

    len = ebml_vint_len_from_first((uint8_t)first);
    if (len < 1 || len > 8)
        return -1;

    mask = (uint64_t)(1U << (8 - len));
    value = (uint64_t)((uint8_t)first & (uint8_t)(mask - 1U));

    if (value != (mask - 1U))
        unknown = 0;

    for (i = 1; i < len; i++) {
        int b = nio_r8(io);
        if (b < 0)
            return -1;
        value = (value << 8) | (uint64_t)(b & 0xFF);
        if ((b & 0xFF) != 0xFF)
            unknown = 0;
    }

    if (out_size)
        *out_size = value;
    if (out_len)
        *out_len = len;
    if (out_unknown)
        *out_unknown = unknown;

    return 0;
}

static int read_uint_payload(NessIO* io, uint64_t size, uint64_t* out_value)
{
    uint64_t value = 0;
    uint64_t i;

    if (size == 0 || size > 8)
        return -1;

    for (i = 0; i < size; i++) {
        int b = nio_r8(io);
        if (b < 0)
            return -1;
        value = (value << 8) | (uint64_t)(b & 0xFF);
    }

    *out_value = value;
    return 0;
}

static int read_float_payload(NessIO* io, uint64_t size, double* out_value)
{
    if (size == 4) {
        union {
            uint32_t u32;
            float    f32;
        } cvt;

        int b0 = nio_r8(io);
        int b1 = nio_r8(io);
        int b2 = nio_r8(io);
        int b3 = nio_r8(io);

        if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0)
            return -1;

        cvt.u32 = ((uint32_t)(b0 & 0xFF) << 24) |
                  ((uint32_t)(b1 & 0xFF) << 16) |
                  ((uint32_t)(b2 & 0xFF) << 8)  |
                  ((uint32_t)(b3 & 0xFF));

        *out_value = (double)cvt.f32;
        return 0;
    }
    else if (size == 8) {
        union {
            uint64_t u64;
            double   f64;
        } cvt;

        int b0 = nio_r8(io);
        int b1 = nio_r8(io);
        int b2 = nio_r8(io);
        int b3 = nio_r8(io);
        int b4 = nio_r8(io);
        int b5 = nio_r8(io);
        int b6 = nio_r8(io);
        int b7 = nio_r8(io);

        if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0 ||
            b4 < 0 || b5 < 0 || b6 < 0 || b7 < 0)
            return -1;

        cvt.u64 = ((uint64_t)(b0 & 0xFF) << 56) |
                  ((uint64_t)(b1 & 0xFF) << 48) |
                  ((uint64_t)(b2 & 0xFF) << 40) |
                  ((uint64_t)(b3 & 0xFF) << 32) |
                  ((uint64_t)(b4 & 0xFF) << 24) |
                  ((uint64_t)(b5 & 0xFF) << 16) |
                  ((uint64_t)(b6 & 0xFF) << 8)  |
                  ((uint64_t)(b7 & 0xFF));

        *out_value = cvt.f64;
        return 0;
    }

    return -1;
}

static int read_string_payload(NessIO* io, uint64_t size, char* out_buf, size_t out_buf_size)
{
    char* tmp;

    if (!out_buf || out_buf_size == 0)
        return -1;

    if (size == 0) {
        out_buf[0] = '\0';
        return 0;
    }

    tmp = (char*)malloc((size_t)size + 1);
    if (!tmp)
        return -1;

    if (nio_read(io, tmp, (size_t)size) != 0) {
        free(tmp);
        return -1;
    }

    tmp[size] = '\0';
    strncpy(out_buf, tmp, out_buf_size - 1);
    out_buf[out_buf_size - 1] = '\0';

    free(tmp);
    return 0;
}

static int parse_simpleblock(NessIO* io, uint64_t size, ValidatorStats* st)
{
    uint64_t track_number = 0;
    int track_len = 0;
    int flags;

    if (size < 4)
        return -1;

    if (ebml_read_size(io, &track_number, &track_len, NULL) != 0)
        return -1;

    if (track_len < 1 || (uint64_t)track_len >= size)
        return -1;

    if (nio_rb16(io) < 0)
        return -1;

    flags = nio_r8(io);
    if (flags < 0)
        return -1;

    st->block_count++;
    if (flags & 0x80)
        st->keyframe_count++;

    return skip_bytes(io, size - (uint64_t)track_len - 3);
}

static int parse_video(NessIO* io, int64_t end_pos, ValidatorStats* st)
{
    while (nio_tell(io) < end_pos) {
        uint32_t id;
        uint64_t size;
        int unknown = 0;

        if (ebml_read_id(io, &id, NULL) != 0)
            return -1;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            return -1;
        if (unknown)
            return -1;

        if (id == MATROSKA_ID_VIDEOPIXELWIDTH) {
            uint64_t v;
            if (read_uint_payload(io, size, &v) != 0)
                return -1;
            st->video_width = (int)v;
        }
        else if (id == MATROSKA_ID_VIDEOPIXELHEIGHT) {
            uint64_t v;
            if (read_uint_payload(io, size, &v) != 0)
                return -1;
            st->video_height = (int)v;
        }
        else {
            if (skip_bytes(io, size) != 0)
                return -1;
        }
    }

    return 0;
}

static int parse_track_entry(NessIO* io, int64_t end_pos, ValidatorStats* st)
{
    while (nio_tell(io) < end_pos) {
        uint32_t id;
        uint64_t size;
        int unknown = 0;

        if (ebml_read_id(io, &id, NULL) != 0)
            return -1;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            return -1;
        if (unknown)
            return -1;

        if (id == MATROSKA_ID_CODECID) {
            if (read_string_payload(io, size, st->codec_id, sizeof(st->codec_id)) != 0)
                return -1;
        }
        else if (id == MATROSKA_ID_CODECPRIVATE) {
            unsigned char header[4];

            st->codec_private_size = (int)size;
            if (size >= 4) {
                if (nio_read(io, header, 4) != 0)
                    return -1;

                st->avcc_version = header[0];
                st->avcc_profile = header[1];
                st->avcc_level = header[3];

                if (size > 4 && skip_bytes(io, size - 4) != 0)
                    return -1;
            } else {
                if (skip_bytes(io, size) != 0)
                    return -1;
            }
        }
        else if (id == MATROSKA_ID_TRACKVIDEO) {
            int64_t sub_end = nio_tell(io) + (int64_t)size;
            if (parse_video(io, sub_end, st) != 0)
                return -1;
            if (nio_seek(io, sub_end, SEEK_SET) != 0)
                return -1;
        }
        else {
            if (skip_bytes(io, size) != 0)
                return -1;
        }
    }

    return 0;
}

static int parse_tracks(NessIO* io, int64_t end_pos, ValidatorStats* st)
{
    while (nio_tell(io) < end_pos) {
        uint32_t id;
        uint64_t size;
        int unknown = 0;

        if (ebml_read_id(io, &id, NULL) != 0)
            return -1;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            return -1;
        if (unknown)
            return -1;

        if (id == MATROSKA_ID_TRACKENTRY) {
            int64_t sub_end = nio_tell(io) + (int64_t)size;
            st->track_count++;
            if (parse_track_entry(io, sub_end, st) != 0)
                return -1;
            if (nio_seek(io, sub_end, SEEK_SET) != 0)
                return -1;
        }
        else {
            if (skip_bytes(io, size) != 0)
                return -1;
        }
    }

    return 0;
}

static int parse_info(NessIO* io, int64_t end_pos, ValidatorStats* st)
{
    while (nio_tell(io) < end_pos) {
        uint32_t id;
        uint64_t size;
        int unknown = 0;

        if (ebml_read_id(io, &id, NULL) != 0)
            return -1;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            return -1;
        if (unknown)
            return -1;

        if (id == MATROSKA_ID_TIMESTAMPSCALE) {
            uint64_t v;
            if (read_uint_payload(io, size, &v) != 0)
                return -1;
            st->timestamp_scale = v;
        }
        else if (id == MATROSKA_ID_DURATION) {
            double d;
            if (read_float_payload(io, size, &d) != 0)
                return -1;
            st->duration_ms = d;
        }
        else {
            if (skip_bytes(io, size) != 0)
                return -1;
        }
    }

    return 0;
}

static int parse_seekhead(NessIO* io, int64_t end_pos, ValidatorStats* st)
{
    while (nio_tell(io) < end_pos) {
        uint32_t id;
        uint64_t size;
        int unknown = 0;

        if (ebml_read_id(io, &id, NULL) != 0)
            return -1;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            return -1;
        if (unknown)
            return -1;

        if (id == MATROSKA_ID_SEEKENTRY) {
            st->seekhead_entries++;
        }

        if (skip_bytes(io, size) != 0)
            return -1;
    }

    return 0;
}

static int parse_cluster(NessIO* io, int64_t end_pos, ValidatorStats* st)
{
    st->cluster_count++;

    while (nio_tell(io) < end_pos) {
        uint32_t id;
        uint64_t size;
        int unknown = 0;

        if (ebml_read_id(io, &id, NULL) != 0)
            return -1;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            return -1;
        if (unknown)
            return -1;

        if (id == MATROSKA_ID_SIMPLEBLOCK) {
            if (parse_simpleblock(io, size, st) != 0)
                return -1;
        }
        else {
            if (skip_bytes(io, size) != 0)
                return -1;
        }
    }

    return 0;
}

static int parse_cues(NessIO* io, int64_t end_pos, ValidatorStats* st)
{
    while (nio_tell(io) < end_pos) {
        uint32_t id;
        uint64_t size;
        int unknown = 0;

        if (ebml_read_id(io, &id, NULL) != 0)
            return -1;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            return -1;
        if (unknown)
            return -1;

        if (id == MATROSKA_ID_CUEPOINT)
            st->cue_count++;

        if (skip_bytes(io, size) != 0)
            return -1;
    }

    return 0;
}

static int parse_ebml_header(NessIO* io, int64_t end_pos, ValidatorStats* st)
{
    while (nio_tell(io) < end_pos) {
        uint32_t id;
        uint64_t size;
        int unknown = 0;

        if (ebml_read_id(io, &id, NULL) != 0)
            return -1;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            return -1;
        if (unknown)
            return -1;

        if (id == EBML_ID_EBMLVERSION) {
            uint64_t v;
            if (read_uint_payload(io, size, &v) != 0)
                return -1;
            st->ebml_version = (int)v;
        }
        else if (id == EBML_ID_DOCTYPE) {
            if (read_string_payload(io, size, st->doctype, sizeof(st->doctype)) != 0)
                return -1;
        }
        else if (id == EBML_ID_DOCTYPEVERSION) {
            uint64_t v;
            if (read_uint_payload(io, size, &v) != 0)
                return -1;
            st->doctype_version = (int)v;
        }
        else if (id == EBML_ID_DOCTYPEREADVERSION) {
            uint64_t v;
            if (read_uint_payload(io, size, &v) != 0)
                return -1;
            st->doctype_read_version = (int)v;
        }
        else {
            if (skip_bytes(io, size) != 0)
                return -1;
        }
    }

    return 0;
}

static int validate_file(const char* path)
{
    NessIO* io = NULL;
    int64_t file_size;
    uint32_t id;
    uint64_t size;
    int unknown = 0;
    ValidatorStats st;
    int64_t segment_end;

    memset(&st, 0, sizeof(st));

    io = nio_open_read(path);
    if (!io) {
        printf("[ERR] Failed to open file: %s\n", path);
        return 1;
    }

    file_size = get_file_size(io);
    if (file_size <= 0) {
        printf("[ERR] Failed to get file size\n");
        nio_close(io);
        return 1;
    }

    if (nio_seek(io, 0, SEEK_SET) != 0) {
        printf("[ERR] Failed to seek to start\n");
        nio_close(io);
        return 1;
    }

    if (ebml_read_id(io, &id, NULL) != 0 || id != EBML_ID_HEADER) {
        printf("[ERR] Missing EBML header\n");
        nio_close(io);
        return 1;
    }

    if (ebml_read_size(io, &size, NULL, &unknown) != 0 || unknown) {
        printf("[ERR] Invalid EBML header size\n");
        nio_close(io);
        return 1;
    }

    if (parse_ebml_header(io, nio_tell(io) + (int64_t)size, &st) != 0) {
        printf("[ERR] Failed to parse EBML header\n");
        nio_close(io);
        return 1;
    }

    printf("[OK] EBML Header: version=%d, doctype=%s, doctypeversion=%d\n",
           st.ebml_version,
           st.doctype[0] ? st.doctype : "(unknown)",
           st.doctype_version);

    if (ebml_read_id(io, &id, NULL) != 0 || id != MATROSKA_ID_SEGMENT) {
        printf("[ERR] Segment not found after EBML header\n");
        nio_close(io);
        return 1;
    }

    if (ebml_read_size(io, &size, NULL, &unknown) != 0) {
        printf("[ERR] Failed to read segment size\n");
        nio_close(io);
        return 1;
    }

    st.segment_offset = nio_tell(io);
    segment_end = unknown ? file_size : (st.segment_offset + (int64_t)size);

    printf("[OK] Segment found at offset %lld\n", (long long)st.segment_offset);

    while (nio_tell(io) < segment_end && !nio_eof(io)) {
        int64_t element_start = nio_tell(io);
        int64_t element_end;

        if (ebml_read_id(io, &id, NULL) != 0)
            break;
        if (ebml_read_size(io, &size, NULL, &unknown) != 0)
            break;

        if (unknown) {
            printf("[ERR] Unsupported unknown-size element inside Segment (id=0x%X)\n", id);
            nio_close(io);
            return 1;
        }

        element_end = nio_tell(io) + (int64_t)size;

        if (id == EBML_ID_VOID) {
            if (skip_bytes(io, size) != 0)
                break;
        }
        else if (id == MATROSKA_ID_SEEKHEAD) {
            if (parse_seekhead(io, element_end, &st) != 0) {
                printf("[ERR] Failed parsing SeekHead\n");
                nio_close(io);
                return 1;
            }
        }
        else if (id == MATROSKA_ID_INFO) {
            if (parse_info(io, element_end, &st) != 0) {
                printf("[ERR] Failed parsing Info\n");
                nio_close(io);
                return 1;
            }
        }
        else if (id == MATROSKA_ID_TRACKS) {
            if (parse_tracks(io, element_end, &st) != 0) {
                printf("[ERR] Failed parsing Tracks\n");
                nio_close(io);
                return 1;
            }
        }
        else if (id == MATROSKA_ID_CLUSTER) {
            if (parse_cluster(io, element_end, &st) != 0) {
                printf("[ERR] Failed parsing Cluster at offset %lld\n", (long long)element_start);
                nio_close(io);
                return 1;
            }
        }
        else if (id == MATROSKA_ID_CUES) {
            if (parse_cues(io, element_end, &st) != 0) {
                printf("[ERR] Failed parsing Cues\n");
                nio_close(io);
                return 1;
            }
        }
        else {
            if (skip_bytes(io, size) != 0)
                break;
        }

        if (nio_tell(io) != element_end) {
            if (nio_seek(io, element_end, SEEK_SET) != 0) {
                printf("[ERR] Failed to realign parser\n");
                nio_close(io);
                return 1;
            }
        }
    }

    printf("[OK] SeekHead: %d entries\n", st.seekhead_entries);
    printf("[OK] Info: TimestampScale=%llu, Duration=%.2fms\n",
           (unsigned long long)st.timestamp_scale,
           st.duration_ms);
    printf("[OK] Tracks: %d track(s)\n", st.track_count);
    printf("     Track 1: video, %s, %dx%d\n",
           st.codec_id[0] ? st.codec_id : "(unknown)",
           st.video_width,
           st.video_height);
    printf("     CodecPrivate: %d bytes (AVCC v%d, profile=%d, level=%d)\n",
           st.codec_private_size,
           st.avcc_version,
           st.avcc_profile,
           st.avcc_level);
    printf("[OK] Clusters: %d clusters, %d SimpleBlocks\n",
           st.cluster_count,
           st.block_count);
    printf("[OK] Cues: %d entries\n", st.cue_count);
    printf("[OK] VALID — %d frames, %.2fs, %d keyframes\n",
           st.block_count,
           st.duration_ms / 1000.0,
           st.keyframe_count);

    nio_close(io);
    return 0;
}

static void print_usage(void)
{
    printf("NessMuxer MKV Validator\n");
    printf("\n");
    printf("Usage:\n");
    printf("  nessmux_validate <file.mkv>\n");
    printf("\n");
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        print_usage();
        return 1;
    }

    return validate_file(argv[1]);
}