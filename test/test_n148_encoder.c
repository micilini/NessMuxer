#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/encoder/n148/n148_encoder.h"
#include "../src/decoder/n148/n148_decoder.h"
#include "../src/codec/n148/n148_spec.h"
#include "../src/codec/n148/n148_codec_private.h"

static uint8_t* g_pkt = NULL;
static int g_pkt_size = 0;
static int g_pkt_key = 0;

static void reset_packet_capture(void)
{
    if (g_pkt) {
        free(g_pkt);
        g_pkt = NULL;
    }

    g_pkt_size = 0;
    g_pkt_key = 0;
}

static int collect_packet(void* userdata, const NessEncodedPacket* pkt)
{
    (void)userdata;

    reset_packet_capture();

    g_pkt = (uint8_t*)malloc((size_t)pkt->size);
    if (!g_pkt)
        return -1;

    memcpy(g_pkt, pkt->data, (size_t)pkt->size);
    g_pkt_size = pkt->size;
    g_pkt_key = pkt->is_keyframe;

    printf("    [LOG] packet capturado: size=%d key=%d pts=%lld dts=%lld\n",
           pkt->size,
           pkt->is_keyframe,
           (long long)pkt->pts_hns,
           (long long)pkt->dts_hns);

    return 0;
}

static int depacketize_to_startcode(const uint8_t* in, int in_size, uint8_t** out, int* out_size)
{
    int pos = 0;
    int cap = in_size + 64;
    int wr = 0;
    uint8_t* buf = (uint8_t*)malloc((size_t)cap);

    if (!buf)
        return -1;

    while (pos + 4 <= in_size) {
        int nal_size = ((int)in[pos] << 24) |
                       ((int)in[pos + 1] << 16) |
                       ((int)in[pos + 2] << 8) |
                       (int)in[pos + 3];
        pos += 4;

        if (nal_size <= 0 || pos + nal_size > in_size) {
            free(buf);
            return -1;
        }

        buf[wr++] = 0x00;
        buf[wr++] = 0x00;
        buf[wr++] = 0x01;
        memcpy(buf + wr, in + pos, (size_t)nal_size);
        wr += nal_size;
        pos += nal_size;
    }

    *out = buf;
    *out_size = wr;
    return 0;
}

static int run_one_case(const char* label,
                        int profile,
                        int entropy_mode,
                        int* out_pkt_size)
{
    void* enc = NULL;
    uint8_t* cp = NULL;
    int cp_size = 0;
    uint8_t* nv12 = NULL;
    int frame_size = 64 * 64 * 3 / 2;
    N148Decoder* dec = NULL;
    N148DecodedFrame frame;
    N148SeqHeader parsed;
    int i;

    memset(&frame, 0, sizeof(frame));
    memset(&parsed, 0, sizeof(parsed));

    printf("  [CASE] %s\n", label);

    if (g_n148_encoder_vtable.create(&enc, 64, 64, 30, 1000) != 0 || !enc)
        return 101;

    if (n148_encoder_set_profile_entropy_for_tests(enc, profile, entropy_mode) != 0)
        return 102;

    nv12 = (uint8_t*)malloc((size_t)frame_size);
    if (!nv12)
        return 103;

    memset(nv12, 128, (size_t)frame_size);

    for (i = 0; i < 64 * 64; i += 17)
        nv12[i] = (uint8_t)(64 + (i % 127));

    if (g_n148_encoder_vtable.get_codec_private(enc, &cp, &cp_size) != 0 || cp_size <= 0)
        return 104;

    if (n148_seq_header_parse(cp, cp_size, &parsed) != 0)
        return 105;

    printf("    [LOG] codec private: profile=%d entropy=%d width=%d height=%d\n",
           parsed.profile,
           parsed.entropy_mode,
           parsed.width,
           parsed.height);

    if (parsed.profile != profile)
        return 106;

    if (parsed.entropy_mode != entropy_mode)
        return 107;

    if (g_n148_encoder_vtable.submit_frame(enc, nv12, frame_size) != 0)
        return 108;

    reset_packet_capture();

    if (g_n148_encoder_vtable.drain(enc, collect_packet, NULL) != 0)
        return 109;

    if (!g_pkt || g_pkt_size <= 0 || !g_pkt_key)
        return 110;

    if (n148_decoder_create(&dec) != 0 || !dec)
        return 111;

    if (n148_decoder_init_from_seq_header(dec, cp, cp_size) != 0)
        return 112;

    if (n148_decoder_decode(dec, g_pkt, g_pkt_size, &frame) != 0)
        return 113;

    if (frame.width != 64 || frame.height != 64 || frame.frame_type != N148_FRAME_I)
        return 114;

    for (i = 0; i < 64 * 64; i++) {
        if (frame.planes[0][i] > 255)
            return 115;
    }

    for (i = 0; i < 64 * 64 / 2; i++) {
        if (frame.planes[1][i] > 255)
            return 116;
    }

    if (out_pkt_size)
        *out_pkt_size = g_pkt_size;

    printf("    [PASS] roundtrip %s OK (packet=%d bytes)\n", label, g_pkt_size);

    free(nv12);
    free(cp);
    reset_packet_capture();
    n148_decoder_destroy(dec);
    g_n148_encoder_vtable.destroy(enc);
    return 0;
}

int main(void)
{
    int cavlc_size = 0;
    int cabac_size = 0;
    int rc;

    printf("=== N.148 Encoder Test (Fase 4 + Fase 7.1) ===\n");

    rc = run_one_case("MAIN/CAVLC", N148_PROFILE_MAIN, N148_ENTROPY_CAVLC, &cavlc_size);
    if (rc != 0) {
        printf("  [FAIL] MAIN/CAVLC rc=%d\n", rc);
        return rc;
    }

    rc = run_one_case("EPIC/CABAC", N148_PROFILE_EPIC, N148_ENTROPY_CABAC, &cabac_size);
    if (rc != 0) {
        printf("  [FAIL] EPIC/CABAC rc=%d\n", rc);
        return rc;
    }

    printf("  [PASS] CAVLC e CABAC decodificaram corretamente\n");
    printf("  [INFO] tamanho CAVLC = %d bytes\n", cavlc_size);
    printf("  [INFO] tamanho CABAC = %d bytes\n", cabac_size);

    if (cabac_size < cavlc_size) {
        printf("  [PASS] CABAC ficou menor que CAVLC (%d < %d)\n", cabac_size, cavlc_size);
    } else {
        printf("  [WARN] CABAC nao ficou menor nesta amostra (%d >= %d)\n", cabac_size, cavlc_size);
        printf("  [WARN] Isso NAO invalida o decode; apenas indica que ainda nao provamos ganho de compressao consistente.\n");
    }

    return 0;
}