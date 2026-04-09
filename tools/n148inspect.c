
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "codec/n148/n148_spec.h"
#include "codec/n148/n148_codec_private.h"
#include "codec/n148/n148_codec.h"
#include "codec/n148/n148_bitstream.h"
#include "decoder/n148/n148_parser.h"
#include "common/entropy/n148_cavlc.h"
#include "common/entropy/n148_cabac_syntax.h"





typedef struct {
    int total_blocks;
    int intra_blocks;
    int inter_blocks;
    int skip_blocks;
    int total_nz_coeffs;
    int total_residual_blocks;
    int frame_type;
    int qp;
    uint32_t frame_number;
    int64_t pts;
    int slice_bytes;
} FrameInspectStats;

static const char* mode_name(int m)
{
    switch (m) {
        case 0: return "skip";
        case 1: return "inter";
        case 2: return "intra";
        default: return "???";
    }
}

static const char* frame_type_name(int ft)
{
    switch (ft) {
        case 0x01: return "I";
        case 0x02: return "P";
        case 0x03: return "B";
        default:   return "?";
    }
}

static const char* intra_mode_name(int m)
{
    switch (m) {
        case 0: return "DC";
        case 1: return "H";
        case 2: return "V";
        case 3: return "DL";
        case 4: return "DR";
        case 5: return "Planar";
        default: return "?";
    }
}

static int inspect_slice_cavlc(const uint8_t* data, int size,
                                int width, int height,
                                int frame_type, int target_frame,
                                int verbose, FrameInspectStats* stats)
{
    N148BsReader bs;
    uint16_t w, h;
    uint8_t ft;
    int mb_cols, mb_rows;
    int mb_x, mb_y, blk_x, blk_y, ch;
    int block_idx = 0;
    int allow_inter;

    n148_bs_reader_init(&bs, data, size);

    if (n148_bs_read_u16be(&bs, &w) != 0) return -1;
    if (n148_bs_read_u16be(&bs, &h) != 0) return -1;
    if (n148_bs_read_u8(&bs, &ft) != 0) return -1;

    mb_cols = (w + 15) / 16;
    mb_rows = (h + 15) / 16;
    allow_inter = (ft == N148_FRAME_P || ft == N148_FRAME_B);

    for (mb_y = 0; mb_y < mb_rows; mb_y++) {
        for (mb_x = 0; mb_x < mb_cols; mb_x++) {
            for (blk_y = 0; blk_y < 4; blk_y++) {
                for (blk_x = 0; blk_x < 4; blk_x++) {
                    uint32_t block_mode = 0;
                    int bx = mb_x * 16 + blk_x * 4;
                    int by = mb_y * 16 + blk_y * 4;

                    if (bx >= w || by >= h) continue;

                    if (n148_bs_read_ue(&bs, &block_mode) != 0) return -1;

                    stats->total_blocks++;

                    if (block_mode == 2) {
                        uint32_t intra_m = 0;
                        n148_bs_read_ue(&bs, &intra_m);
                        stats->intra_blocks++;

                        if (verbose)
                            printf("    blk(%3d,%3d) intra mode=%s", bx, by, intra_mode_name(intra_m));
                    } else if (block_mode == 0) {
                        stats->skip_blocks++;
                        if (verbose)
                            printf("    blk(%3d,%3d) skip", bx, by);
                    } else {
                        uint32_t ref_idx = 0;
                        int32_t mvx = 0, mvy = 0;
                        n148_bs_read_ue(&bs, &ref_idx);
                        n148_entropy_cavlc_read_mv(&bs, (int*)&mvx, (int*)&mvy);
                        stats->inter_blocks++;

                        if (verbose)
                            printf("    blk(%3d,%3d) inter ref=%u mv=(%d,%d)", bx, by, ref_idx, mvx, mvy);
                    }

                   
                    if (block_mode != 0) {
                        uint32_t has_res = 0;
                        n148_bs_read_bits(&bs, 1, &has_res);
                        if (has_res) {
                            int32_t qp_delta = 0;
                            int16_t qcoeffs[16] = {0};
                            int coeff_count = 0;
                            n148_bs_read_se(&bs, &qp_delta);
                            n148_entropy_cavlc_read_block(&bs, qcoeffs, &coeff_count, 16);
                            stats->total_nz_coeffs += coeff_count;
                            stats->total_residual_blocks++;
                            if (verbose)
                                printf(" res=%d coeffs", coeff_count);
                        } else {
                            if (verbose) printf(" res=0");
                        }
                    }

                    if (verbose) printf("\n");
                    block_idx++;
                }
            }

           
            for (ch = 0; ch < 2; ch++) {
                for (blk_y = 0; blk_y < 2; blk_y++) {
                    for (blk_x = 0; blk_x < 2; blk_x++) {
                        int bx = mb_x * 8 + blk_x * 4;
                        int by = mb_y * 8 + blk_y * 4;
                        int cw = w / 2;
                        int chh = h / 2;
                        uint32_t bm = 0;

                        if (bx >= cw || by >= chh) continue;

                        if (n148_bs_read_ue(&bs, &bm) != 0) return -1;

                        if (bm == 2) {
                            uint32_t im = 0;
                            n148_bs_read_ue(&bs, &im);
                        } else if (bm == 1) {
                            uint32_t ri = 0;
                            int mx2 = 0, my2 = 0;
                            n148_bs_read_ue(&bs, &ri);
                            n148_entropy_cavlc_read_mv(&bs, &mx2, &my2);
                        }

                        if (bm != 0) {
                            uint32_t hr = 0;
                            n148_bs_read_bits(&bs, 1, &hr);
                            if (hr) {
                                int32_t qd = 0;
                                int16_t qc[16] = {0};
                                int cc = 0;
                                n148_bs_read_se(&bs, &qd);
                                n148_entropy_cavlc_read_block(&bs, qc, &cc, 16);
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
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

int main(int argc, char* argv[])
{
    const char* input = NULL;
    int target_frame = -1;
    int summary_only = 0;
    int i;
    FILE* f;
    long file_size;
    int frame_index = 0;
    N148SeqHeader seq;
    int have_seq = 0;
    int entropy_mode = N148_ENTROPY_CAVLC;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            target_frame = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--summary") == 0) {
            summary_only = 1;
        } else if (!input) {
            input = argv[i];
        }
    }

    if (!input) {
        fprintf(stderr, "Usage: n148inspect <input.mkv> [--frame N] [--summary]\n");
        return 1;
    }

    f = fopen(input, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", input); return 1; }

    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    printf("=== n148inspect: %s ===\n\n", input);

    while (ftell(f) < file_size - 2) {
        int id_len = 0, sz_len = 0;
        int64_t elem_id, elem_size;

        elem_id = read_ebml_id(f, &id_len);
        if (elem_id < 0) break;
        elem_size = read_ebml_vint(f, &sz_len);
        if (elem_size < 0) break;

        if (elem_id == 0xA3 && elem_size > 4) {
            uint8_t* block_data = (uint8_t*)malloc((size_t)elem_size);
            int track_len;
            int payload_off;
            N148NalUnit nals[64];
            int nal_count = 0;
            int ni;

            if (!block_data) break;
            if (fread(block_data, 1, (size_t)elem_size, f) != (size_t)elem_size) {
                free(block_data); break;
            }

            track_len = ebml_vint_len(block_data[0]);
            payload_off = track_len + 3;

            if (payload_off < (int)elem_size) {
                n148_find_nal_units_lp(block_data + payload_off,
                                       (int)elem_size - payload_off,
                                       nals, 64, &nal_count);

                for (ni = 0; ni < nal_count; ni++) {
                    if (nals[ni].nal_type == N148_NAL_SEQ_HDR) {
                        if (n148_seq_header_parse(nals[ni].payload,
                                nals[ni].payload_size, &seq) == 0) {
                            have_seq = 1;
                            entropy_mode = seq.entropy_mode;
                        }
                    }

                    if (nals[ni].nal_type == N148_NAL_FRM_HDR) {
                        N148FrameHeader fh;
                        if (n148_parse_frame_header(nals[ni].payload,
                                nals[ni].payload_size, &fh) == 0) {
                           
                            int si;
                            for (si = ni + 1; si < nal_count; si++) {
                                if (nals[si].nal_type == N148_NAL_SLICE ||
                                    nals[si].nal_type == N148_NAL_IDR) {
                                    int show_verbose = !summary_only &&
                                        (target_frame < 0 || target_frame == frame_index);

                                    FrameInspectStats stats;
                                    memset(&stats, 0, sizeof(stats));
                                    stats.frame_type = fh.frame_type;
                                    stats.qp = fh.qp_base;
                                    stats.frame_number = fh.frame_number;
                                    stats.pts = fh.pts;
                                    stats.slice_bytes = nals[si].payload_size;

                                    if (target_frame < 0 || target_frame == frame_index) {
                                        printf("  Frame #%d: %s-frame  size=%d  qp=%u  pts=%lld\n",
                                                    frame_index,
                                                    frame_type_name(fh.frame_type),
                                                    nals[si].payload_size,
                                                    fh.qp_base,
                                                    (long long)fh.pts);
                                    }

                                   
                                    if (entropy_mode == N148_ENTROPY_CAVLC && have_seq) {
                                        inspect_slice_cavlc(nals[si].payload,
                                                            nals[si].payload_size,
                                                            seq.width, seq.height,
                                                            fh.frame_type,
                                                            frame_index,
                                                            show_verbose,
                                                            &stats);
                                    }

                                    if (target_frame < 0 || target_frame == frame_index) {
                                        printf("    summary: %d blocks (intra=%d inter=%d skip=%d) "
                                               "residual=%d nz_coeffs=%d\n\n",
                                               stats.total_blocks,
                                               stats.intra_blocks,
                                               stats.inter_blocks,
                                               stats.skip_blocks,
                                               stats.total_residual_blocks,
                                               stats.total_nz_coeffs);
                                    }

                                    frame_index++;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            free(block_data);
            continue;
        }

        if (elem_id == 0x18538067 || elem_id == 0x1F43B675) continue;

        if (elem_size > 0 && ftell(f) + elem_size <= file_size)
            fseek(f, (long)elem_size, SEEK_CUR);
    }

    printf("  Total frames inspected: %d\n", frame_index);
    fclose(f);
    return 0;
}