#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/encoder/n148/n148_encoder.h"
#include "../src/decoder/n148/n148_decoder.h"

static uint8_t* g_pkt = NULL;
static int g_pkt_size = 0;
static int g_pkt_key = 0;

static int collect_packet(void* userdata, const NessEncodedPacket* pkt)
{
    (void)userdata;

    g_pkt = (uint8_t*)malloc((size_t)pkt->size);
    if (!g_pkt)
        return -1;

    memcpy(g_pkt, pkt->data, (size_t)pkt->size);
    g_pkt_size = pkt->size;
    g_pkt_key = pkt->is_keyframe;
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

int main(void)
{
    void* enc = NULL;
    uint8_t* cp = NULL;
    int cp_size = 0;
    uint8_t* nv12 = NULL;
    uint8_t* raw_bs = NULL;
    int raw_bs_size = 0;
    int frame_size = 64 * 64 * 3 / 2;
    N148Decoder* dec = NULL;
    N148DecodedFrame frame;
    int i;

    memset(&frame, 0, sizeof(frame));

    if (g_n148_encoder_vtable.create(&enc, 64, 64, 30, 1000) != 0 || !enc)
        return 1;

    nv12 = (uint8_t*)malloc((size_t)frame_size);
    if (!nv12)
        return 2;

    memset(nv12, 128, (size_t)frame_size);

    if (g_n148_encoder_vtable.get_codec_private(enc, &cp, &cp_size) != 0 || cp_size <= 0)
        return 3;

    if (g_n148_encoder_vtable.submit_frame(enc, nv12, frame_size) != 0)
        return 4;

    if (g_n148_encoder_vtable.receive_packets(enc, collect_packet, NULL) != 0)
        return 5;

    if (!g_pkt || g_pkt_size <= 0 || !g_pkt_key)
        return 6;

    if (depacketize_to_startcode(g_pkt, g_pkt_size, &raw_bs, &raw_bs_size) != 0)
        return 7;

    if (n148_decoder_create(&dec) != 0 || !dec)
        return 8;

    if (n148_decoder_init_from_seq_header(dec, cp, cp_size) != 0)
        return 9;

    if (n148_decoder_decode(dec, raw_bs, raw_bs_size, &frame) != 0)
        return 10;

    if (frame.width != 64 || frame.height != 64 || frame.frame_type != 1)
        return 11;

    for (i = 0; i < 64 * 64; i++) {
        if (frame.planes[0][i] != 128)
            return 12;
    }

    for (i = 0; i < 64 * 64 / 2; i++) {
        if (frame.planes[1][i] != 128)
            return 13;
    }

    if (g_pkt_size >= frame_size)
        return 14;

    printf("=== N.148 Encoder Test (Fase 4) ===\n");
    printf("  [PASS] packet gerado pelo encoder N.148\n");
    printf("  [PASS] roundtrip encoder -> decoder OK\n");
    printf("  [PASS] tamanho comprimido: %d bytes (raw=%d)\n", g_pkt_size, frame_size);

    free(nv12);
    free(cp);
    free(raw_bs);
    free(g_pkt);
    n148_decoder_destroy(dec);
    g_n148_encoder_vtable.destroy(enc);
    return 0;
}