#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/encoder/n148/n148_encoder.h"
#include "../src/decoder/n148/n148_decoder.h"
#include "../src/codec/n148/n148_codec_private.h"
#include "../src/codec/n148/n148_spec.h"
#include "../src/common/entropy/n148_cabac_syntax.h"
#include "../src/mkv_muxer.h"

#define TEST_W 64
#define TEST_H 64
#define TEST_FPS 30
#define TEST_BR 1000
#define TEST_FRAMES 4
#define MAX_PKTS 32

typedef struct PacketList {
    uint8_t* data[MAX_PKTS];
    int      size[MAX_PKTS];
    int64_t  pts[MAX_PKTS];
    int64_t  dts[MAX_PKTS];
    int      key[MAX_PKTS];
    int      count;
    int      total_bytes;
} PacketList;

typedef struct CaseResult {
    int total_bytes;
    int packet_count;
    int decoded_frames;
} CaseResult;

static void packet_list_reset(PacketList* pl)
{
    int i;

    if (!pl)
        return;

    for (i = 0; i < MAX_PKTS; i++) {
        if (pl->data[i]) {
            free(pl->data[i]);
            pl->data[i] = NULL;
        }
        pl->size[i] = 0;
        pl->pts[i] = 0;
        pl->dts[i] = 0;
        pl->key[i] = 0;
    }

    pl->count = 0;
    pl->total_bytes = 0;
}

static int collect_packet_list(void* userdata, const NessEncodedPacket* pkt)
{
    PacketList* pl = (PacketList*)userdata;

    if (!pl || !pkt || !pkt->data || pkt->size <= 0)
        return -1;

    if (pl->count >= MAX_PKTS)
        return -1;

    pl->data[pl->count] = (uint8_t*)malloc((size_t)pkt->size);
    if (!pl->data[pl->count])
        return -1;

    memcpy(pl->data[pl->count], pkt->data, (size_t)pkt->size);
    pl->size[pl->count] = pkt->size;
    pl->pts[pl->count] = pkt->pts_hns;
    pl->dts[pl->count] = pkt->dts_hns;
    pl->key[pl->count] = pkt->is_keyframe;
    pl->total_bytes += pkt->size;
    pl->count++;

    printf("    [LOG] pkt #%d size=%d key=%d pts=%lld dts=%lld\n",
           pl->count,
           pkt->size,
           pkt->is_keyframe,
           (long long)pkt->pts_hns,
           (long long)pkt->dts_hns);

    return 0;
}

static void fill_test_frame(uint8_t* nv12, int width, int height, int frame_idx)
{
    uint8_t* y_plane = nv12;
    uint8_t* uv_plane = nv12 + width * height;
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int v = ((x * 5 + y * 3 + frame_idx * 17) ^
                     ((x >> 2) * 11) ^
                     ((y >> 2) * 7)) & 0xFF;
            y_plane[y * width + x] = (uint8_t)v;
        }
    }

   
    {
        int bx = 8 + frame_idx * 6;
        int by = 10 + frame_idx * 3;
        int yy, xx;

        for (yy = 0; yy < 16; yy++) {
            for (xx = 0; xx < 16; xx++) {
                int px = bx + xx;
                int py = by + yy;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    y_plane[py * width + px] = 220;
                }
            }
        }
    }

   
    {
        int bx = 36 - frame_idx * 4;
        int by = 28 + frame_idx * 2;
        int yy, xx;

        for (yy = 0; yy < 12; yy++) {
            for (xx = 0; xx < 12; xx++) {
                int px = bx + xx;
                int py = by + yy;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    y_plane[py * width + px] = 24;
                }
            }
        }
    }

    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width; x += 2) {
            int u = 128 + (((x / 2) + frame_idx * 3 + y) % 21) - 10;
            int v = 128 + (((y * 2) + frame_idx * 5 + x) % 17) - 8;

            if (u < 0) u = 0;
            if (u > 255) u = 255;
            if (v < 0) v = 0;
            if (v > 255) v = 255;

            uv_plane[y * width + x + 0] = (uint8_t)u;
            uv_plane[y * width + x + 1] = (uint8_t)v;
        }
    }
}

static int write_case_to_mkv(const char* path,
                             const uint8_t* cp,
                             int cp_size,
                             const PacketList* pl)
{
    MkvMuxer* mux = NULL;
    NessVideoTrackDesc desc;
    MkvPacket pkt;
    FILE* f;

    memset(&desc, 0, sizeof(desc));
    desc.codec_id = "V_NESS/N148";
    desc.codec_private = cp;
    desc.codec_private_size = cp_size;
    desc.width = TEST_W;
    desc.height = TEST_H;
    desc.fps = TEST_FPS;
    desc.bitrate_kbps = TEST_BR;
    desc.needs_annexb_conv = 0;

    if (mkv_muxer_open_desc(&mux, path, &desc) != 0 || !mux)
        return 301;

    for (int i = 0; i < pl->count; i++) {
        pkt.data = pl->data[i];
        pkt.size = pl->size[i];
        pkt.pts_hns = pl->pts[i];
        pkt.dts_hns = pl->dts[i];
        pkt.is_keyframe = pl->key[i];

        if (mkv_muxer_write_packet(mux, &pkt) != 0) {
            mkv_muxer_destroy(mux);
            return 302;
        }
    }

    if (mkv_muxer_close(mux) != 0) {
        mkv_muxer_destroy(mux);
        return 303;
    }

    mkv_muxer_destroy(mux);

    f = fopen(path, "rb");
    if (!f)
        return 304;

    fseek(f, 0, SEEK_END);
    if (ftell(f) <= 0) {
        fclose(f);
        return 305;
    }
    fclose(f);

    printf("    [PASS] MKV salvo: %s\n", path);
    return 0;
}

static int run_case(const char* label,
                    int profile,
                    int entropy_mode,
                    const char* out_mkv_path,
                    CaseResult* out_res)
{
    void* enc = NULL;
    PacketList packets;
    uint8_t* cp = NULL;
    int cp_size = 0;
    N148SeqHeader parsed;
    N148Decoder* dec = NULL;
    N148DecodedFrame out;
    int frame_size = TEST_W * TEST_H * 3 / 2;
    int decoded_frames = 0;
    int i;
    int ret = 0;

    memset(&packets, 0, sizeof(packets));
    memset(&parsed, 0, sizeof(parsed));
    memset(&out, 0, sizeof(out));

    printf("  [CASE] %s\n", label);

    n148_cabac_telemetry_reset();

    if (g_n148_encoder_vtable.create(&enc, TEST_W, TEST_H, TEST_FPS, TEST_BR) != 0 || !enc)
        return 101;

    if (n148_encoder_set_profile_entropy_for_tests(enc, profile, entropy_mode) != 0)
        return 102;

    if (g_n148_encoder_vtable.get_codec_private(enc, &cp, &cp_size) != 0 || !cp || cp_size <= 0)
        return 103;

    if (n148_seq_header_parse(cp, cp_size, &parsed) != 0)
        return 104;

    printf("    [LOG] codec_private: profile=%d entropy=%d width=%d height=%d refs=%d reorder=%d\n",
           parsed.profile,
           parsed.entropy_mode,
           parsed.width,
           parsed.height,
           parsed.max_ref_frames,
           parsed.max_reorder_depth);

    if (parsed.profile != profile)
        return 105;
    if (parsed.entropy_mode != entropy_mode)
        return 106;

    packet_list_reset(&packets);

    for (i = 0; i < TEST_FRAMES; i++) {
        uint8_t* frame = (uint8_t*)malloc((size_t)frame_size);
        if (!frame)
            return 107;

        fill_test_frame(frame, TEST_W, TEST_H, i);

        if (g_n148_encoder_vtable.submit_frame(enc, frame, frame_size) != 0) {
            free(frame);
            return 108;
        }

        if (g_n148_encoder_vtable.receive_packets(enc, collect_packet_list, &packets) != 0) {
            free(frame);
            return 109;
        }

        free(frame);
    }

    if (g_n148_encoder_vtable.drain(enc, collect_packet_list, &packets) != 0)
        return 110;

    if (packets.count <= 0 || packets.total_bytes <= 0)
        return 111;

    printf("    [LOG] total packets=%d total_bytes=%d\n",
           packets.count,
           packets.total_bytes);

    if (entropy_mode == N148_ENTROPY_CABAC) {
        N148CabacTelemetry telem;
        n148_cabac_telemetry_get(&telem);
        printf("    [LOG] cabac telemetry: mv_bits=%llu coeff_coded_block_bits=%llu coeff_sig_bits=%llu coeff_last_bits=%llu coeff_siglast_bits=%llu coeff_level_bits=%llu coeff_sign_bits=%llu mvs=%llu blocks=%llu\n",
               (unsigned long long)telem.mv_bits,
               (unsigned long long)telem.coeff_coded_block_bits,
               (unsigned long long)telem.coeff_sig_bits,
               (unsigned long long)telem.coeff_last_bits,
               (unsigned long long)telem.coeff_siglast_bits,
               (unsigned long long)telem.coeff_level_bits,
               (unsigned long long)telem.coeff_sign_bits,
               (unsigned long long)telem.mvs_coded,
               (unsigned long long)telem.blocks_coded);
    }

    if (n148_decoder_create(&dec) != 0 || !dec)
        return 112;

    if (n148_decoder_init_from_seq_header(dec, cp, cp_size) != 0)
        return 113;

    for (i = 0; i < packets.count; i++) {
        ret = n148_decoder_decode(dec, packets.data[i], packets.size[i], &out);
        if (ret == 0) {
            decoded_frames++;
        } else if (ret == 1) {
           
        } else {
            return 114;
        }
    }

    while (1) {
        ret = n148_decoder_flush(dec, &out);
        if (ret == 0) {
            decoded_frames++;
            continue;
        }
        if (ret == 1)
            break;
        return 115;
    }

    if (decoded_frames != TEST_FRAMES)
        return 116;

    printf("    [PASS] decode completo: %d frames reconstruidos\n", decoded_frames);

    if (out_mkv_path && out_mkv_path[0] != '\0') {
        ret = write_case_to_mkv(out_mkv_path, cp, cp_size, &packets);
        if (ret != 0)
            return ret;
    }

    if (out_res) {
        out_res->total_bytes = packets.total_bytes;
        out_res->packet_count = packets.count;
        out_res->decoded_frames = decoded_frames;
    }

    packet_list_reset(&packets);
    free(cp);
    n148_decoder_destroy(dec);
    g_n148_encoder_vtable.destroy(enc);
    return 0;
}

int main(void)
{
    CaseResult cavlc;
    CaseResult cabac;
    int rc;

    memset(&cavlc, 0, sizeof(cavlc));
    memset(&cabac, 0, sizeof(cabac));

    printf("=== N.148 Phase 7 Epic Test (CABAC real + MKV) ===\n");

    rc = run_case("MAIN/CAVLC",
                  N148_PROFILE_MAIN,
                  N148_ENTROPY_CAVLC,
                  NULL,
                  &cavlc);
    if (rc != 0) {
        printf("  [FAIL] MAIN/CAVLC rc=%d\n", rc);
        return rc;
    }

    rc = run_case("EPIC/CABAC",
                  N148_PROFILE_EPIC,
                  N148_ENTROPY_CABAC,
                  "test_n148_mux_cabac.mkv",
                  &cabac);
    if (rc != 0) {
        printf("  [FAIL] EPIC/CABAC rc=%d\n", rc);
        return rc;
    }

    printf("  [INFO] total CAVLC = %d bytes\n", cavlc.total_bytes);
    printf("  [INFO] total CABAC = %d bytes\n", cabac.total_bytes);

    if (cabac.total_bytes < cavlc.total_bytes) {
        printf("  [PASS] CABAC ficou menor que CAVLC (%d < %d)\n",
               cabac.total_bytes, cavlc.total_bytes);
    } else {
        printf("  [WARN] CABAC nao ficou menor nesta amostra (%d >= %d)\n",
               cabac.total_bytes, cavlc.total_bytes);
        printf("  [WARN] O decode e o MKV CABAC ficaram provados; o ganho de compressao ainda precisa de iteracao.\n");
    }

    printf("  [PASS] MKV CABAC gerado: test_n148_mux_cabac.mkv\n");
    return 0;
}