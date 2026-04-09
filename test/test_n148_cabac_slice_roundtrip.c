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
    return 0;
}

static void fill_frame(uint8_t* nv12, int width, int height, int seed)
{
    uint8_t* y = nv12;
    uint8_t* uv = nv12 + width * height;
    int x, yy;

    for (yy = 0; yy < height; yy++) {
        for (x = 0; x < width; x++) {
            y[yy * width + x] = (uint8_t)((x * 3 + yy * 5 + seed * 17) & 0xFF);
        }
    }

    for (yy = 0; yy < height / 2; yy++) {
        for (x = 0; x < width; x += 2) {
            uv[yy * width + x + 0] = (uint8_t)(120 + ((seed + x + yy) % 13));
            uv[yy * width + x + 1] = (uint8_t)(128 + ((seed + x + yy) % 11));
        }
    }
}

int main(void)
{
    void* enc = NULL;
    N148Decoder* dec = NULL;
    N148DecodedFrame frame;
    uint8_t* cp = NULL;
    int cp_size = 0;
    uint8_t* nv12 = NULL;
    int frame_size = 64 * 64 * 3 / 2;
    int rc;

    memset(&frame, 0, sizeof(frame));

    printf("=== N.148 CABAC Slice Roundtrip Test (7R.9) ===\n");

    if (g_n148_encoder_vtable.create(&enc, 64, 64, 30, 1000) != 0 || !enc) {
        printf("  [FAIL] create encoder\n");
        return 101;
    }

    if (n148_encoder_set_profile_entropy_for_tests(enc, N148_PROFILE_EPIC, N148_ENTROPY_CABAC) != 0) {
        printf("  [FAIL] set epic/cabac\n");
        return 102;
    }

    nv12 = (uint8_t*)malloc((size_t)frame_size);
    if (!nv12) {
        printf("  [FAIL] alloc frame\n");
        return 103;
    }

    fill_frame(nv12, 64, 64, 1);

    if (g_n148_encoder_vtable.get_codec_private(enc, &cp, &cp_size) != 0 || !cp || cp_size <= 0) {
        printf("  [FAIL] get codec private\n");
        return 104;
    }

    if (g_n148_encoder_vtable.submit_frame(enc, nv12, frame_size) != 0) {
        printf("  [FAIL] submit frame\n");
        return 105;
    }

    reset_packet_capture();

    if (g_n148_encoder_vtable.drain(enc, collect_packet, NULL) != 0) {
        printf("  [FAIL] drain\n");
        return 106;
    }

    if (!g_pkt || g_pkt_size <= 0 || !g_pkt_key) {
        printf("  [FAIL] no packet/keyframe\n");
        return 107;
    }

    if (n148_decoder_create(&dec) != 0 || !dec) {
        printf("  [FAIL] create decoder\n");
        return 108;
    }

    if (n148_decoder_init_from_seq_header(dec, cp, cp_size) != 0) {
        printf("  [FAIL] init decoder from seq header\n");
        return 109;
    }

    rc = n148_decoder_decode(dec, g_pkt, g_pkt_size, &frame);
    if (rc != 0) {
        printf("  [FAIL] decode rc=%d\n", rc);
        return 110;
    }

    if (frame.width != 64 || frame.height != 64 || frame.frame_type != N148_FRAME_I) {
        printf("  [FAIL] bad decoded frame metadata\n");
        return 111;
    }

    printf("  [PASS] CABAC slice roundtrip OK\n");
    printf("  [INFO] packet=%d bytes\n", g_pkt_size);

    free(nv12);
    free(cp);
    reset_packet_capture();
    n148_decoder_destroy(dec);
    g_n148_encoder_vtable.destroy(enc);
    return 0;
}