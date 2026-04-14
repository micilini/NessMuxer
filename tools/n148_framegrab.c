#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "n148dec.h"

typedef struct VideoTrackInfo {
    int track_number;
    uint8_t* codec_private;
    int codec_private_size;
    int width;
    int height;
    int found;
} VideoTrackInfo;

static int ebml_vint_len_from_first(uint8_t first)
{
    int len;
    for (len = 1; len <= 8; len++) {
        if (first & (1U << (8 - len)))
            return len;
    }
    return -1;
}

static int read_exact(FILE* f, void* dst, size_t size)
{
    return fread(dst, 1, size, f) == size ? 0 : -1;
}

static long file_size_of(FILE* f)
{
    long cur = ftell(f);
    long end;
    if (cur < 0)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0)
        return -1;
    end = ftell(f);
    if (fseek(f, cur, SEEK_SET) != 0)
        return -1;
    return end;
}

static int ebml_read_id(FILE* f, uint32_t* out_id, int* out_len)
{
    uint8_t first;
    uint32_t value;
    int len;
    int i;

    if (read_exact(f, &first, 1) != 0)
        return -1;

    len = ebml_vint_len_from_first(first);
    if (len < 1 || len > 4)
        return -1;

    value = first;
    for (i = 1; i < len; i++) {
        uint8_t b;
        if (read_exact(f, &b, 1) != 0)
            return -1;
        value = (value << 8) | b;
    }

    if (out_id) *out_id = value;
    if (out_len) *out_len = len;
    return 0;
}

static int ebml_read_size(FILE* f, uint64_t* out_size, int* out_len, int* out_unknown)
{
    uint8_t first;
    int len;
    int i;
    uint64_t value;
    uint64_t mask;
    int unknown = 1;

    if (read_exact(f, &first, 1) != 0)
        return -1;

    len = ebml_vint_len_from_first(first);
    if (len < 1 || len > 8)
        return -1;

    mask = (uint64_t)(1U << (8 - len));
    value = (uint64_t)(first & (uint8_t)(mask - 1U));
    if (value != (mask - 1U))
        unknown = 0;

    for (i = 1; i < len; i++) {
        uint8_t b;
        if (read_exact(f, &b, 1) != 0)
            return -1;
        value = (value << 8) | (uint64_t)b;
        if (b != 0xFF)
            unknown = 0;
    }

    if (out_size) *out_size = value;
    if (out_len) *out_len = len;
    if (out_unknown) *out_unknown = unknown;
    return 0;
}

static int skip_bytes(FILE* f, uint64_t size)
{
    long cur = ftell(f);
    if (cur < 0)
        return -1;
    return fseek(f, cur + (long)size, SEEK_SET);
}

static int read_uint_payload(FILE* f, uint64_t size, uint64_t* out_value)
{
    uint64_t value = 0;
    uint64_t i;

    if (size == 0 || size > 8)
        return -1;

    for (i = 0; i < size; i++) {
        uint8_t b;
        if (read_exact(f, &b, 1) != 0)
            return -1;
        value = (value << 8) | (uint64_t)b;
    }

    *out_value = value;
    return 0;
}

static int read_string_payload(FILE* f, uint64_t size, char* out_buf, size_t out_buf_size)
{
    uint8_t* tmp;
    size_t copy_len;

    if (!out_buf || out_buf_size == 0)
        return -1;
    if (size > (1024U * 1024U))
        return -1;

    tmp = (uint8_t*)malloc((size_t)size + 1);
    if (!tmp)
        return -1;

    if (size > 0 && read_exact(f, tmp, (size_t)size) != 0) {
        free(tmp);
        return -1;
    }
    tmp[size] = 0;

    copy_len = (size_t)size;
    if (copy_len >= out_buf_size)
        copy_len = out_buf_size - 1;

    memcpy(out_buf, tmp, copy_len);
    out_buf[copy_len] = 0;
    free(tmp);
    return 0;
}

static int read_bytes_payload(FILE* f, uint64_t size, uint8_t** out_data, int* out_size)
{
    uint8_t* data;
    if (!out_data || !out_size || size > 0x7fffffffULL)
        return -1;
    data = (uint8_t*)malloc((size_t)size);
    if (!data)
        return -1;
    if (size > 0 && read_exact(f, data, (size_t)size) != 0) {
        free(data);
        return -1;
    }
    *out_data = data;
    *out_size = (int)size;
    return 0;
}

static int parse_video_element(FILE* f, long video_end, VideoTrackInfo* track)
{
    while (ftell(f) < video_end) {
        uint32_t id;
        uint64_t size;
        int id_len = 0, sz_len = 0, unknown = 0;
        long element_data_start;
        long element_end;

        if (ebml_read_id(f, &id, &id_len) != 0) return -1;
        if (ebml_read_size(f, &size, &sz_len, &unknown) != 0) return -1;
        element_data_start = ftell(f);
        element_end = unknown ? video_end : (element_data_start + (long)size);

        if (id == 0xB0) {
            uint64_t v = 0;
            if (read_uint_payload(f, size, &v) != 0) return -1;
            track->width = (int)v;
        } else if (id == 0xBA) {
            uint64_t v = 0;
            if (read_uint_payload(f, size, &v) != 0) return -1;
            track->height = (int)v;
        } else {
            if (unknown) return -1;
            if (skip_bytes(f, size) != 0) return -1;
        }

        if (ftell(f) < element_end && fseek(f, element_end, SEEK_SET) != 0)
            return -1;
    }
    return 0;
}

static int parse_track_entry(FILE* f, long track_end, VideoTrackInfo* out_track)
{
    int track_number = 0;
    int track_type = 0;
    int width = 0;
    int height = 0;
    int codec_private_size = 0;
    uint8_t* codec_private = NULL;
    char codec_id[128];

    codec_id[0] = 0;

    while (ftell(f) < track_end) {
        uint32_t id;
        uint64_t size;
        int id_len = 0, sz_len = 0, unknown = 0;
        long element_data_start;
        long element_end;

        if (ebml_read_id(f, &id, &id_len) != 0) goto fail;
        if (ebml_read_size(f, &size, &sz_len, &unknown) != 0) goto fail;
        element_data_start = ftell(f);
        element_end = unknown ? track_end : (element_data_start + (long)size);

        switch (id) {
            case 0xD7: {
                uint64_t v = 0;
                if (read_uint_payload(f, size, &v) != 0) goto fail;
                track_number = (int)v;
                break;
            }
            case 0x83: {
                uint64_t v = 0;
                if (read_uint_payload(f, size, &v) != 0) goto fail;
                track_type = (int)v;
                break;
            }
            case 0x86:
                if (read_string_payload(f, size, codec_id, sizeof(codec_id)) != 0) goto fail;
                break;
            case 0x63A2:
                if (codec_private) {
                    free(codec_private);
                    codec_private = NULL;
                    codec_private_size = 0;
                }
                if (read_bytes_payload(f, size, &codec_private, &codec_private_size) != 0) goto fail;
                break;
            case 0xE0: {
                VideoTrackInfo tmp;
                memset(&tmp, 0, sizeof(tmp));
                if (parse_video_element(f, element_end, &tmp) != 0) goto fail;
                width = tmp.width;
                height = tmp.height;
                break;
            }
            default:
                if (unknown) goto fail;
                if (skip_bytes(f, size) != 0) goto fail;
                break;
        }

        if (ftell(f) < element_end && fseek(f, element_end, SEEK_SET) != 0)
            goto fail;
    }

    if (track_type == 1 && strcmp(codec_id, "V_NESS/N148") == 0 && track_number > 0 && codec_private && codec_private_size > 0) {
        if (out_track->codec_private)
            free(out_track->codec_private);
        out_track->track_number = track_number;
        out_track->codec_private = codec_private;
        out_track->codec_private_size = codec_private_size;
        out_track->width = width;
        out_track->height = height;
        out_track->found = 1;
        return 0;
    }

    if (codec_private)
        free(codec_private);
    return 0;

fail:
    if (codec_private)
        free(codec_private);
    return -1;
}

static int parse_tracks(FILE* f, long tracks_end, VideoTrackInfo* track)
{
    while (ftell(f) < tracks_end) {
        uint32_t id;
        uint64_t size;
        int id_len = 0, sz_len = 0, unknown = 0;
        long element_data_start;
        long element_end;

        if (ebml_read_id(f, &id, &id_len) != 0) return -1;
        if (ebml_read_size(f, &size, &sz_len, &unknown) != 0) return -1;
        element_data_start = ftell(f);
        element_end = unknown ? tracks_end : (element_data_start + (long)size);

        if (id == 0xAE) {
            if (parse_track_entry(f, element_end, track) != 0) return -1;
        } else {
            if (unknown) return -1;
            if (skip_bytes(f, size) != 0) return -1;
        }

        if (ftell(f) < element_end && fseek(f, element_end, SEEK_SET) != 0)
            return -1;
    }
    return 0;
}

static int read_vint_value_from_memory(const uint8_t* data, int data_size, int* io_offset, uint64_t* out_value)
{
    int off = *io_offset;
    uint8_t first;
    int len;
    int i;
    uint64_t value;

    if (off >= data_size)
        return -1;

    first = data[off++];
    len = ebml_vint_len_from_first(first);
    if (len < 1 || len > 8)
        return -1;
    if (off + (len - 1) > data_size)
        return -1;

    value = (uint64_t)(first & (uint8_t)(0xFF >> len));
    for (i = 1; i < len; i++)
        value = (value << 8) | data[off++];

    *io_offset = off;
    *out_value = value;
    return 0;
}

static int nv12_to_bgr24(const N148DecOutput* out, uint8_t** bgr, int* stride)
{
    int x, y;
    int width = out->width;
    int height = out->height;
    int row_stride;
    uint8_t* dst;

    if (!out || !out->planes[0] || !out->planes[1] || width <= 0 || height <= 0)
        return -1;

    row_stride = ((width * 3 + 3) / 4) * 4;
    dst = (uint8_t*)malloc((size_t)row_stride * (size_t)height);
    if (!dst)
        return -1;

    for (y = 0; y < height; y++) {
        const uint8_t* y_row = out->planes[0] + (size_t)y * (size_t)out->strides[0];
        const uint8_t* uv_row = out->planes[1] + (size_t)(y / 2) * (size_t)out->strides[1];
        uint8_t* dst_row = dst + (size_t)(height - 1 - y) * (size_t)row_stride;

        for (x = 0; x < width; x++) {
            int Y = y_row[x];
            int U = uv_row[(x & ~1) + 0] - 128;
            int V = uv_row[(x & ~1) + 1] - 128;
            int C = Y - 16;
            int D = U;
            int E = V;
            int R, G, B;

            if (C < 0) C = 0;

            R = (298 * C + 409 * E + 128) >> 8;
            G = (298 * C - 100 * D - 208 * E + 128) >> 8;
            B = (298 * C + 516 * D + 128) >> 8;

            if (R < 0) R = 0; else if (R > 255) R = 255;
            if (G < 0) G = 0; else if (G > 255) G = 255;
            if (B < 0) B = 0; else if (B > 255) B = 255;

            dst_row[x * 3 + 0] = (uint8_t)B;
            dst_row[x * 3 + 1] = (uint8_t)G;
            dst_row[x * 3 + 2] = (uint8_t)R;
        }
    }

    *bgr = dst;
    *stride = row_stride;
    return 0;
}

static int dir_exists_for_path(const char* path)
{
    const char* last_slash = NULL;
    size_t len;
    char* dir;
    int ok = 0;

    if (!path || !*path)
        return 0;

    {
        const char* p = path;
        while (*p) {
            if (*p == '/' || *p == '\\')
                last_slash = p;
            ++p;
        }
    }

    if (!last_slash)
        return 1;

    len = (size_t)(last_slash - path);
    if (len == 0)
        return 1;

    dir = (char*)malloc(len + 1);
    if (!dir)
        return 0;

    memcpy(dir, path, len);
    dir[len] = 0;

#if defined(_WIN32)
    ok = (_access(dir, 0) == 0);
#else
    ok = (access(dir, F_OK) == 0);
#endif

    free(dir);
    return ok;
}

static int file_exists_on_disk(const char* path)
{
#if defined(_WIN32)
    return _access(path, 0) == 0;
#else
    return access(path, F_OK) == 0;
#endif
}

static int write_bmp24(const char* path, const uint8_t* bgr, int width, int height, int stride)
{
    FILE* f;
    uint32_t file_size;
    uint32_t pixel_offset = 14 + 40;
    uint32_t image_size = (uint32_t)(stride * height);
    uint8_t file_header[14];
    uint8_t info_header[40];

    if (!path || !*path || !bgr || width <= 0 || height <= 0 || stride <= 0) {
        fprintf(stderr, "write_bmp24: invalid arguments.\n");
        return -1;
    }

    if (!dir_exists_for_path(path)) {
        fprintf(stderr, "write_bmp24: output directory does not exist for: %s\n", path);
        return -1;
    }

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "write_bmp24: cannot open output path for writing: %s\n", path);
        return -1;
    }

    file_size = pixel_offset + image_size;

    memset(file_header, 0, sizeof(file_header));
    file_header[0] = 'B';
    file_header[1] = 'M';
    file_header[2] = (uint8_t)(file_size & 0xFF);
    file_header[3] = (uint8_t)((file_size >> 8) & 0xFF);
    file_header[4] = (uint8_t)((file_size >> 16) & 0xFF);
    file_header[5] = (uint8_t)((file_size >> 24) & 0xFF);
    file_header[10] = (uint8_t)(pixel_offset & 0xFF);
    file_header[11] = (uint8_t)((pixel_offset >> 8) & 0xFF);
    file_header[12] = (uint8_t)((pixel_offset >> 16) & 0xFF);
    file_header[13] = (uint8_t)((pixel_offset >> 24) & 0xFF);

    memset(info_header, 0, sizeof(info_header));
    info_header[0] = 40;
    info_header[4] = (uint8_t)(width & 0xFF);
    info_header[5] = (uint8_t)((width >> 8) & 0xFF);
    info_header[6] = (uint8_t)((width >> 16) & 0xFF);
    info_header[7] = (uint8_t)((width >> 24) & 0xFF);
    info_header[8] = (uint8_t)(height & 0xFF);
    info_header[9] = (uint8_t)((height >> 8) & 0xFF);
    info_header[10] = (uint8_t)((height >> 16) & 0xFF);
    info_header[11] = (uint8_t)((height >> 24) & 0xFF);
    info_header[12] = 1;
    info_header[14] = 24;
    info_header[20] = (uint8_t)(image_size & 0xFF);
    info_header[21] = (uint8_t)((image_size >> 8) & 0xFF);
    info_header[22] = (uint8_t)((image_size >> 16) & 0xFF);
    info_header[23] = (uint8_t)((image_size >> 24) & 0xFF);

    if (fwrite(file_header, 1, sizeof(file_header), f) != sizeof(file_header) ||
        fwrite(info_header, 1, sizeof(info_header), f) != sizeof(info_header) ||
        fwrite(bgr, 1, image_size, f) != image_size) {
        fprintf(stderr, "write_bmp24: fwrite failed for: %s\n", path);
        fclose(f);
        return -1;
    }

    if (fflush(f) != 0) {
        fprintf(stderr, "write_bmp24: fflush failed for: %s\n", path);
        fclose(f);
        return -1;
    }

    fclose(f);

    if (!file_exists_on_disk(path)) {
        fprintf(stderr, "write_bmp24: file still not visible after close: %s\n", path);
        return -1;
    }

    return 0;
}

static int decode_and_maybe_save(N148DecHandle* dec,
                                 const uint8_t* payload,
                                 int payload_size,
                                 int target_output_index,
                                 int* io_decoded_count,
                                 const char* output_bmp)
{
    N148DecOutput out;
    int rc;

    memset(&out, 0, sizeof(out));
    rc = n148dec_decode_frame_ex(dec, payload, payload_size, &out);
    if (rc == 1)
        return 0;
    if (rc != 0)
        return -1;

    if (out.planes[0] && out.width > 0 && out.height > 0) {
        (*io_decoded_count)++;
        if (*io_decoded_count == target_output_index) {
            uint8_t* bgr = NULL;
            int stride = 0;
            int save_rc;
            if (nv12_to_bgr24(&out, &bgr, &stride) != 0)
                return -1;
            save_rc = write_bmp24(output_bmp, bgr, out.width, out.height, stride);
            free(bgr);
            return save_rc == 0 ? 1 : -1;
        }
    }

    return 0;
}

static int extract_frame_to_bmp(const char* input_mkv, int target_output_index, const char* output_bmp)
{
    if (!dir_exists_for_path(output_bmp)) {
        fprintf(stderr, "Output directory does not exist for: %s\n", output_bmp);
        return 1;
    }
    FILE* f;
    long total_file_size;
    VideoTrackInfo track;
    N148DecHandle* dec = NULL;
    int decoder_ready = 0;
    int decoded_count = 0;

    memset(&track, 0, sizeof(track));

    f = fopen(input_mkv, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open input: %s\n", input_mkv);
        return 1;
    }

    total_file_size = file_size_of(f);
    if (total_file_size <= 0) {
        fclose(f);
        fprintf(stderr, "Cannot determine file size.\n");
        return 1;
    }

    {
        uint32_t id;
        uint64_t size;
        int id_len = 0, sz_len = 0, unknown = 0;
        if (ebml_read_id(f, &id, &id_len) != 0 || id != 0x1A45DFA3) {
            fprintf(stderr, "Invalid EBML header.\n");
            goto fail;
        }
        if (ebml_read_size(f, &size, &sz_len, &unknown) != 0) {
            fprintf(stderr, "Invalid EBML header size.\n");
            goto fail;
        }
        if (unknown || skip_bytes(f, size) != 0) {
            fprintf(stderr, "Failed to skip EBML header.\n");
            goto fail;
        }
    }

    {
        uint32_t id;
        uint64_t size;
        int id_len = 0, sz_len = 0, unknown = 0;
        long segment_data_start;
        long segment_end;

        if (ebml_read_id(f, &id, &id_len) != 0 || id != 0x18538067) {
            fprintf(stderr, "Segment element not found.\n");
            goto fail;
        }
        if (ebml_read_size(f, &size, &sz_len, &unknown) != 0) {
            fprintf(stderr, "Invalid Segment size.\n");
            goto fail;
        }

        segment_data_start = ftell(f);
        segment_end = unknown ? total_file_size : (segment_data_start + (long)size);

        while (ftell(f) < segment_end) {
            uint32_t id2;
            uint64_t size2;
            int id_len2 = 0, sz_len2 = 0, unknown2 = 0;
            long element_data_start;
            long element_end;

            if (ebml_read_id(f, &id2, &id_len2) != 0)
                break;
            if (ebml_read_size(f, &size2, &sz_len2, &unknown2) != 0)
                break;

            element_data_start = ftell(f);
            element_end = unknown2 ? segment_end : (element_data_start + (long)size2);

            if (id2 == 0x1654AE6B) {
                if (parse_tracks(f, element_end, &track) != 0) {
                    fprintf(stderr, "Failed to parse Tracks.\n");
                    goto fail;
                }
            } else if (id2 == 0x1F43B675) {
                long cluster_end = element_end;
                while (ftell(f) < cluster_end) {
                    uint32_t cid;
                    uint64_t csize;
                    int cid_len = 0, csz_len = 0, cunknown = 0;
                    long cdata_start;
                    long cend;

                    if (ebml_read_id(f, &cid, &cid_len) != 0) break;
                    if (ebml_read_size(f, &csize, &csz_len, &cunknown) != 0) break;
                    cdata_start = ftell(f);
                    cend = cunknown ? cluster_end : (cdata_start + (long)csize);

                    if (cid == 0xA3) {
                        uint8_t* block = NULL;
                        int block_size = 0;
                        int off = 0;
                        uint64_t track_number = 0;

                        if (!track.found) {
                            fprintf(stderr, "N.148 track not found in Tracks metadata.\n");
                            goto fail;
                        }

                        if (read_bytes_payload(f, csize, &block, &block_size) != 0) {
                            fprintf(stderr, "Failed to read SimpleBlock.\n");
                            goto fail;
                        }

                        if (read_vint_value_from_memory(block, block_size, &off, &track_number) != 0 || off + 3 > block_size) {
                            free(block);
                            fprintf(stderr, "Invalid SimpleBlock.\n");
                            goto fail;
                        }

                        off += 2; /* timecode */
                        {
                            uint8_t flags = block[off++];
                            int lacing = (flags >> 1) & 0x03;
                            if (lacing != 0) {
                                free(block);
                                fprintf(stderr, "SimpleBlock with lacing not supported.\n");
                                goto fail;
                            }
                        }

                        if ((int)track_number == track.track_number) {
                            const uint8_t* payload = block + off;
                            int payload_size = block_size - off;
                            int result;

                            if (!decoder_ready) {
                                if (n148dec_create(&dec) != 0) {
                                    free(block);
                                    fprintf(stderr, "n148dec_create failed.\n");
                                    goto fail;
                                }
                                if (n148dec_init(dec, track.codec_private, track.codec_private_size) != 0) {
                                    free(block);
                                    fprintf(stderr, "n148dec_init failed.\n");
                                    goto fail;
                                }
                                decoder_ready = 1;
                            }

                            result = decode_and_maybe_save(dec, payload, payload_size,
                                                           target_output_index, &decoded_count,
                                                           output_bmp);
                            free(block);
                            if (result < 0) {
                                fprintf(stderr, "Decode or save failed around decoded frame %d.\n", decoded_count + 1);
                                goto fail;
                            }
                            if (result > 0) {
                                if (dec) n148dec_destroy(dec);
                                if (track.codec_private) free(track.codec_private);
                                fprintf(stderr, "[OK] Wrote decoded frame %d to %s\n", target_output_index, output_bmp);
                                fclose(f);
                                return 0;
                            }
                            continue;
                        }

                        free(block);
                    } else {
                        if (cunknown || skip_bytes(f, csize) != 0)
                            goto fail;
                    }

                    if (ftell(f) < cend && fseek(f, cend, SEEK_SET) != 0)
                        goto fail;
                }
            } else {
                if (unknown2 || skip_bytes(f, size2) != 0)
                    goto fail;
            }

            if (ftell(f) < element_end && fseek(f, element_end, SEEK_SET) != 0)
                goto fail;
        }
    }

    if (dec) {
        for (;;) {
            N148DecOutput out;
            int rc;
            uint8_t* bgr = NULL;
            int stride = 0;
            memset(&out, 0, sizeof(out));
            rc = n148dec_decode_frame_ex(dec, NULL, 0, &out);
            if (rc != 0)
                break;
            if (out.planes[0] && out.width > 0 && out.height > 0) {
                decoded_count++;
                if (decoded_count == target_output_index) {
                    if (nv12_to_bgr24(&out, &bgr, &stride) != 0)
                        goto fail;
                    if (write_bmp24(output_bmp, bgr, out.width, out.height, stride) != 0) {
                        free(bgr);
                        goto fail;
                    }
                    free(bgr);
                    if (dec) n148dec_destroy(dec);
                    if (track.codec_private) free(track.codec_private);
                    fprintf(stderr, "[OK] Wrote decoded frame %d to %s\n", target_output_index, output_bmp);
                    fclose(f);
                    return 0;
                }
            }
        }
    }

    fprintf(stderr, "Requested decoded frame #%d was not found. Total decoded frames=%d\n",
            target_output_index, decoded_count);

fail:
    if (dec) n148dec_destroy(dec);
    if (track.codec_private) free(track.codec_private);
    fclose(f);
    return 1;
}

static void print_usage(const char* exe)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s <input.mkv> <decoded_frame_index_1based> <output.bmp>\n\n"
            "Example:\n"
            "  %s video_n148_recal.mkv 5 frame5.bmp\n",
            exe, exe);
}

int main(int argc, char** argv)
{
    int frame_index;
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }

    frame_index = atoi(argv[2]);
    if (frame_index <= 0) {
        fprintf(stderr, "Invalid frame index: %s\n", argv[2]);
        return 1;
    }

    return extract_frame_to_bmp(argv[1], frame_index, argv[3]);
}
