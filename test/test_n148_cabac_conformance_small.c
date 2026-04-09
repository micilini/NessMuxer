#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/encoder/n148/n148_encoder.h"
#include "../src/decoder/n148/n148_decoder.h"
#include "../src/codec/n148/n148_spec.h"
#include "../src/codec/n148/n148_codec_private.h"

#define FRAME_W 64
#define FRAME_H 64
#define FRAME_COUNT 4
#define MAX_PKTS 16

static uint8_t* g_pkt[MAX_PKTS];
static int g_pkt_size[MAX_PKTS];
static int g_pkt_count = 0;

static void reset_packets(void)
{
    int i;
    for (i = 0; i < MAX_PKTS; i++) {
        if (g_pkt[i]) {
            free(g_pkt[i]);
            g_pkt[i] = NULL;
        }
        g_pkt_size[i] = 0;
    }
    g_pkt_count = 0;
}

static int collect_packet(void* userdata, const NessEncodedPacket* pkt)
{
    (void)userdata;

    if (g_pkt_count >= MAX_PKTS)
        return -1;

    g_pkt[g_pkt_count] = (uint8_t*)malloc((size_t)pkt->size);
    if (!g_pkt[g_pkt_count])
        return -1;

    memcpy(g_pkt[g_pkt_count], pkt->data, (size_t)pkt->size);
    g_pkt_size[g_pkt_count] = pkt->size;
    g_pkt_count++;
    return 0;
}

static void fill_frame(uint8_t* nv12, int width, int height, int seed)
{
    uint8_t* y = nv12;
    uint8_t* uv = nv12 + width * height;
    int x, yy;

    for (yy = 0; yy < height; yy++) {
        for (x = 0; x < width; x++) {
            y[yy * width + x] = (uint8_t)((x * 9 + yy * 7 + seed * 13) & 0xFF);
        }
    }

    for (yy = 0; yy < height / 2; yy++) {
        for (x = 0; x < width; x += 2) {
            uv[yy * width + x + 0] = (uint8_t)(110 + ((seed + x + yy) % 21));
            uv[yy * width + x + 1] = (uint8_t)(130 + ((seed + x + yy) % 17));
        }
    }
}

int main(void)
{
    void* enc = NULL;
    N148Decoder* dec = NULL;
    N148DecodedFrame out;
    uint8_t* cp = NULL;
    int cp_size = 0;
    uint8_t* nv12 = NULL;
    int frame_size = FRAME_W * FRAME_H * 3 / 2;
    int i;
    int decoded = 0;

    memset(&out, 0, sizeof(out));

    printf("=== N.148 CABAC Conformance Small Test (7R.9) ===\n");

    if (g_n148_encoder_vtable.create(&enc, FRAME_W, FRAME_H, 30, 1000) != 0 || !enc) {
        printf("  [FAIL] create encoder\n");
        return 201;
    }

    if (n148_encoder_set_profile_entropy_for_tests(enc, N148_PROFILE_EPIC, N148_ENTROPY_CABAC) != 0) {
        printf("  [FAIL] set epic/cabac\n");
        return 202;
    }

    nv12 = (uint8_t*)malloc((size_t)frame_size);
    if (!nv12) {
        printf("  [FAIL] alloc frame\n");
        return 203;
    }

    if (g_n148_encoder_vtable.get_codec_private(enc, &cp, &cp_size) != 0 || !cp || cp_size <= 0) {
        printf("  [FAIL] get codec private\n");
        return 204;
    }

    reset_packets();

    for (i = 0; i < FRAME_COUNT; i++) {
        fill_frame(nv12, FRAME_W, FRAME_H, i + 1);
        if (g_n148_encoder_vtable.submit_frame(enc, nv12, frame_size) != 0) {
            printf("  [FAIL] submit frame %d\n", i);
            return 205;
        }
    }

    if (g_n148_encoder_vtable.drain(enc, collect_packet, NULL) != 0) {
        printf("  [FAIL] drain\n");
        return 206;
    }

    if (g_pkt_count <= 0) {
        printf("  [FAIL] no packets\n");
        return 207;
    }

    if (n148_decoder_create(&dec) != 0 || !dec) {
        printf("  [FAIL] create decoder\n");
        return 208;
    }

    if (n148_decoder_init_from_seq_header(dec, cp, cp_size) != 0) {
        printf("  [FAIL] init decoder from seq header\n");
        return 209;
    }

    for (i = 0; i < g_pkt_count; i++) {
        int rc = n148_decoder_decode(dec, g_pkt[i], g_pkt_size[i], &out);
        if (rc == 0) {
            decoded++;
        } else if (rc != 1) {
            printf("  [FAIL] decode packet %d rc=%d\n", i, rc);
            return 210;
        }
    }

    while (n148_decoder_flush(dec, &out) == 0)
        decoded++;

    if (decoded != FRAME_COUNT) {
        printf("  [FAIL] decoded=%d expected=%d\n", decoded, FRAME_COUNT);
        return 211;
    }

    printf("  [PASS] decoded %d/%d frames\n", decoded, FRAME_COUNT);
    printf("  [INFO] packets=%d\n", g_pkt_count);

    free(nv12);
    free(cp);
    reset_packets();
    n148_decoder_destroy(dec);
    g_n148_encoder_vtable.destroy(enc);
    return 0;
}