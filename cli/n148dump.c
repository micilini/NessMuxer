
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "codec/n148/n148_spec.h"
#include "codec/n148/n148_codec_private.h"
#include "codec/n148/n148_codec.h"
#include "codec/n148/n148_bitstream.h"
#include "decoder/n148/n148_parser.h"
#include "buffered_io.h"
#include "mkv_defs.h"





static const char* nal_type_name(int t)
{
    switch (t) {
        case N148_NAL_SLICE:   return "SLICE";
        case N148_NAL_IDR:     return "IDR";
        case N148_NAL_SEQ_HDR: return "SEQ_HEADER";
        case N148_NAL_FRM_HDR: return "FRM_HEADER";
        case N148_NAL_SEI:     return "SEI";
        default:               return "UNKNOWN";
    }
}

static const char* ft_name(int ft)
{
    switch (ft) {
        case N148_FRAME_I: return "I";
        case N148_FRAME_P: return "P";
        case N148_FRAME_B: return "B";
        default:           return "?";
    }
}

static void dump_seq_header(const uint8_t* payload, int size)
{
    N148SeqHeader hdr;
    if (n148_seq_header_parse(payload, size, &hdr) != 0) {
        printf("           (parse failed)\n");
        return;
    }
    printf("           version=%u profile=%u level=%u\n",
           hdr.version, hdr.profile, hdr.level);
    printf("           %ux%u chroma=%u depth=%u\n",
           hdr.width, hdr.height, hdr.chroma_format, hdr.bit_depth);
    printf("           fps=%u/%u gop=%u entropy=%s\n",
           hdr.fps_num, hdr.fps_den, hdr.gop_length,
           hdr.entropy_mode == N148_ENTROPY_CABAC ? "CABAC" : "CAVLC");
    printf("           max_refs=%u max_reorder=%u blocks=0x%02X\n",
           hdr.max_ref_frames, hdr.max_reorder_depth, hdr.block_size_flags);
}

static void dump_frame_header(const uint8_t* payload, int size)
{
    N148FrameHeader fh;
    if (n148_parse_frame_header(payload, size, &fh) != 0) {
        printf("           (parse failed)\n");
        return;
    }
    printf("           frame_type=%s frame_num=%u qp=%u slices=%u refs=%u\n",
           ft_name(fh.frame_type), fh.frame_number,
           fh.qp_base, fh.slice_count, fh.num_ref_frames);
    printf("           pts=%lld dts=%lld data_size=%u\n",
           (long long)fh.pts, (long long)fh.dts, fh.frame_data_size);
}





static int ebml_vint_len(uint8_t first)
{
    int len;
    for (len = 1; len <= 8; len++) {
        if (first & (1U << (8 - len))) return len;
    }
    return -1;
}

static int64_t read_ebml_vint(FILE* f, int* out_len)
{
    uint8_t buf[8];
    int len, i;
    int64_t val;

    if (fread(buf, 1, 1, f) != 1) return -1;
    len = ebml_vint_len(buf[0]);
    if (len < 1 || len > 8) return -1;
    if (len > 1 && fread(buf + 1, 1, (size_t)(len - 1), f) != (size_t)(len - 1))
        return -1;

    val = buf[0] & ((1 << (8 - len)) - 1);
    for (i = 1; i < len; i++)
        val = (val << 8) | buf[i];

    if (out_len) *out_len = len;
    return val;
}

static int64_t read_ebml_id(FILE* f, int* out_len)
{
    uint8_t buf[4];
    int len, i;
    int64_t val;

    if (fread(buf, 1, 1, f) != 1) return -1;
    len = ebml_vint_len(buf[0]);
    if (len < 1 || len > 4) return -1;
    if (len > 1 && fread(buf + 1, 1, (size_t)(len - 1), f) != (size_t)(len - 1))
        return -1;

    val = buf[0];
    for (i = 1; i < len; i++)
        val = (val << 8) | buf[i];

    if (out_len) *out_len = len;
    return val;
}

static int dump_mkv_file(const char* path)
{
    FILE* f;
    int64_t file_size;
    int nal_index = 0;
    int total_bytes = 0;

    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", path); return 1; }

    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    printf("=== n148dump: %s (%lld bytes) ===\n\n", path, (long long)file_size);

   
    while (ftell(f) < file_size - 2) {
        int id_len = 0, sz_len = 0;
        int64_t elem_id, elem_size;
        long pos_before = ftell(f);

        elem_id = read_ebml_id(f, &id_len);
        if (elem_id < 0) break;

        elem_size = read_ebml_vint(f, &sz_len);
        if (elem_size < 0) break;

       
        if (elem_id == 0xA3 && elem_size > 4) {
            uint8_t* block_data;
            int track_len = 0;
            int64_t track_num;
            int payload_offset;
            N148NalUnit nals[64];
            int nal_count = 0;
            int ni;

            block_data = (uint8_t*)malloc((size_t)elem_size);
            if (!block_data) break;

            if (fread(block_data, 1, (size_t)elem_size, f) != (size_t)elem_size) {
                free(block_data);
                break;
            }

           
            track_num = block_data[0] & ((1 << (8 - ebml_vint_len(block_data[0]))) - 1);
            track_len = ebml_vint_len(block_data[0]);

           
            payload_offset = track_len + 3;

            if (payload_offset < (int)elem_size) {
                uint8_t* pkt = block_data + payload_offset;
                int pkt_size = (int)elem_size - payload_offset;

               
                n148_find_nal_units_lp(pkt, pkt_size, nals, 64, &nal_count);

                for (ni = 0; ni < nal_count; ni++) {
                    printf("  NAL #%-3d  type=%-11s  size=%-7d  offset=%ld\n",
                           nal_index, nal_type_name(nals[ni].nal_type),
                           nals[ni].payload_size, pos_before);

                    if (nals[ni].nal_type == N148_NAL_SEQ_HDR)
                        dump_seq_header(nals[ni].payload, nals[ni].payload_size);
                    else if (nals[ni].nal_type == N148_NAL_FRM_HDR)
                        dump_frame_header(nals[ni].payload, nals[ni].payload_size);

                    total_bytes += nals[ni].payload_size;
                    nal_index++;
                }
            }

            free(block_data);
            continue;
        }

       
        if (elem_id == 0x18538067 ||
            elem_id == 0x1F43B675)  
        {
            continue;
        }

       
        if (elem_size > 0 && ftell(f) + elem_size <= file_size) {
            fseek(f, (long)elem_size, SEEK_CUR);
        }
    }

    printf("\n  Total: %d NAL units, %d payload bytes\n", nal_index, total_bytes);
    fclose(f);
    return 0;
}





static int dump_raw_file(const char* path)
{
    FILE* f;
    uint8_t* data;
    long file_size;
    N148NalUnit nals[4096];
    int nal_count = 0;
    int i;

    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", path); return 1; }

    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (uint8_t*)malloc((size_t)file_size);
    if (!data) { fclose(f); return 1; }

    if ((long)fread(data, 1, (size_t)file_size, f) != file_size) {
        free(data); fclose(f); return 1;
    }
    fclose(f);

    printf("=== n148dump (raw): %s (%ld bytes) ===\n\n", path, file_size);

    n148_find_nal_units(data, (int)file_size, nals, 4096, &nal_count);

    for (i = 0; i < nal_count; i++) {
        printf("  NAL #%-3d  type=%-11s  size=%d\n",
               i, nal_type_name(nals[i].nal_type), nals[i].payload_size);

        if (nals[i].nal_type == N148_NAL_SEQ_HDR)
            dump_seq_header(nals[i].payload, nals[i].payload_size);
        else if (nals[i].nal_type == N148_NAL_FRM_HDR)
            dump_frame_header(nals[i].payload, nals[i].payload_size);
    }

    printf("\n  Total: %d NAL units\n", nal_count);
    free(data);
    return 0;
}





int main(int argc, char* argv[])
{
    int raw_mode = 0;
    const char* input = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--raw") == 0)
            raw_mode = 1;
        else if (!input)
            input = argv[i];
    }

    if (!input) {
        fprintf(stderr, "Usage: n148dump <input.mkv|input.n148> [--raw]\n");
        return 1;
    }

    if (raw_mode)
        return dump_raw_file(input);
    else
        return dump_mkv_file(input);
}