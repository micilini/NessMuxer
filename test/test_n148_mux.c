#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/mkv_muxer.h"
#include "../src/codec/n148/n148_codec.h"
#include "../src/codec/n148/n148_codec_private.h"

int main(void)
{
    N148SeqHeader hdr;
    uint8_t cp[32];
    uint8_t raw[64];
    uint8_t pktbuf[64];
    NessVideoTrackDesc desc;
    MkvMuxer* mux = NULL;
    MkvPacket pkt;
    FILE* f;
    int cp_size;
    int pkt_size = 0;

    n148_seq_header_defaults(&hdr, 320, 240, 30, N148_PROFILE_MAIN, N148_ENTROPY_CAVLC);
    cp_size = n148_seq_header_serialize(&hdr, cp, sizeof(cp));
    if (cp_size != 32) return 1;

    raw[0]  = 0x00; raw[1]  = 0x00; raw[2]  = 0x01;
    raw[3]  = (uint8_t)(N148_NAL_IDR << 4);
    raw[4]  = 0xAA; raw[5]  = 0xBB;

    raw[6]  = 0x00; raw[7]  = 0x00; raw[8]  = 0x01;
    raw[9]  = (uint8_t)(N148_NAL_FRM_HDR << 4);
    raw[10] = N148_FRAME_I;
    raw[11] = 0x01;

    if (n148_packetize(raw, 12, pktbuf, sizeof(pktbuf), &pkt_size) != 0 || pkt_size <= 0)
        return 2;

    memset(&desc, 0, sizeof(desc));
    desc.codec_id = N148_CODEC_ID;
    desc.codec_private = cp;
    desc.codec_private_size = cp_size;
    desc.width = 320;
    desc.height = 240;
    desc.fps = 30;
    desc.bitrate_kbps = 1000;
    desc.needs_annexb_conv = 0;

    if (mkv_muxer_open_desc(&mux, "test_n148_mux.mkv", &desc) != 0)
        return 3;

    pkt.data = pktbuf;
    pkt.size = pkt_size;
    pkt.pts_hns = 0;
    pkt.dts_hns = 0;
    pkt.is_keyframe = 1;

    if (mkv_muxer_write_packet(mux, &pkt) != 0)
        return 4;

    if (mkv_muxer_close(mux) != 0)
        return 5;

    mkv_muxer_destroy(mux);

    f = fopen("test_n148_mux.mkv", "rb");
    if (!f) return 6;
    fseek(f, 0, SEEK_END);
    if (ftell(f) <= 0) {
        fclose(f);
        return 7;
    }
    fclose(f);

    printf("=== N.148 MKV Mux Test ===\n");
    printf("  [PASS] MKV com track %s gerado com sucesso\n", N148_CODEC_ID);
    printf("  Valide com: nessmux_validate test_n148_mux.mkv\n");

    return 0;
}