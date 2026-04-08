#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/encoder/n148/n148_encoder.h"
#include "../src/decoder/n148/n148_decoder.h"

static uint8_t* g_packets[8];
static int g_packet_sizes[8];
static int g_packet_count = 0;

static int collect_packet(void* userdata, const NessEncodedPacket* pkt)
{
    (void)userdata;

    if (g_packet_count >= 8)
        return -1;

    g_packets[g_packet_count] = (uint8_t*)malloc((size_t)pkt->size);
    if (!g_packets[g_packet_count])
        return -1;

    memcpy(g_packets[g_packet_count], pkt->data, (size_t)pkt->size);
    g_packet_sizes[g_packet_count] = pkt->size;
    g_packet_count++;
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
    N148Decoder* dec = NULL;
    uint8_t* cp = NULL;
    int cp_size = 0;
    int frame_size = 64 * 64 * 3 / 2;
    uint8_t* frame0 = NULL;
    uint8_t* frame1 = NULL;
    uint8_t* raw0 = NULL;
    uint8_t* raw1 = NULL;
    int raw0_size = 0;
    int raw1_size = 0;
    N148DecodedFrame out;
    int i;

    memset(&out, 0, sizeof(out));

    if (g_n148_encoder_vtable.create(&enc, 64, 64, 30, 1000) != 0)
        return 1;

    frame0 = (uint8_t*)malloc((size_t)frame_size);
    frame1 = (uint8_t*)malloc((size_t)frame_size);
    if (!frame0 || !frame1)
        return 2;

    memset(frame0, 128, (size_t)frame_size);
    memset(frame1, 128, (size_t)frame_size);

    for (i = 0; i < 16; i++)
        frame1[10 * 64 + 10 + i] = 180;

    if (g_n148_encoder_vtable.get_codec_private(enc, &cp, &cp_size) != 0)
        return 3;

    if (g_n148_encoder_vtable.submit_frame(enc, frame0, frame_size) != 0)
        return 4;
    if (g_n148_encoder_vtable.receive_packets(enc, collect_packet, NULL) != 0)
        return 5;

    if (g_n148_encoder_vtable.submit_frame(enc, frame1, frame_size) != 0)
        return 6;
    if (g_n148_encoder_vtable.receive_packets(enc, collect_packet, NULL) != 0)
        return 7;

    if (g_packet_count != 2)
        return 8;

    if (n148_decoder_create(&dec) != 0)
        return 11;
    if (n148_decoder_init_from_seq_header(dec, cp, cp_size) != 0)
        return 12;
    if (n148_decoder_decode(dec, g_packets[0], g_packet_sizes[0], &out) != 0)
        return 13;
    if (n148_decoder_decode(dec, g_packets[1], g_packet_sizes[1], &out) != 0)
        return 14;

    if (g_packet_sizes[1] >= g_packet_sizes[0])
        return 15;

    printf("=== N.148 Roundtrip P-frame Test (Fase 5) ===\n");
    printf("  [PASS] frame I codificado\n");
    printf("  [PASS] frame P codificado e decodificado\n");
    printf("  [PASS] P-frame menor que I-frame (%d < %d)\n", g_packet_sizes[1], g_packet_sizes[0]);

    free(frame0);
    free(frame1);
    free(cp);
    if (g_packets[0]) free(g_packets[0]);
    if (g_packets[1]) free(g_packets[1]);
    n148_decoder_destroy(dec);
    g_n148_encoder_vtable.destroy(enc);
    return 0;
}