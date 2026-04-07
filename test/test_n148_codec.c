#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/codec/n148/n148_spec.h"
#include "../src/codec/n148/n148_codec_private.h"
#include "../src/codec/n148/n148_bitstream.h"
#include "../src/decoder/n148/n148_parser.h"
#include "../src/decoder/n148/n148_decoder.h"
#include "../src/decoder/n148/n148_frame_recon.h"

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) do { \
    tests_total++; \
    printf("  [TEST] %-50s", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL (%s)\n", msg); } while(0)

static void test_seq_header_roundtrip(void)
{
    N148SeqHeader hdr, parsed;
    uint8_t buf[64];
    int ret;

    TEST("seq_header serialize + parse roundtrip");

    n148_seq_header_defaults(&hdr, 1920, 1080, 30, N148_PROFILE_MAIN, N148_ENTROPY_CAVLC);

    ret = n148_seq_header_serialize(&hdr, buf, sizeof(buf));
    if (ret != N148_SEQ_HEADER_SIZE) { FAIL("serialize size"); return; }

    if (buf[0] != 'N' || buf[1] != '1' || buf[2] != '4' || buf[3] != '8') {
        FAIL("magic"); return;
    }

    ret = n148_seq_header_parse(buf, N148_SEQ_HEADER_SIZE, &parsed);
    if (ret != 0) { FAIL("parse failed"); return; }

    if (parsed.width != 1920 || parsed.height != 1080) { FAIL("dimensions"); return; }
    if (parsed.profile != N148_PROFILE_MAIN) { FAIL("profile"); return; }
    if (parsed.entropy_mode != N148_ENTROPY_CAVLC) { FAIL("entropy"); return; }
    if (parsed.fps_num != 30 || parsed.fps_den != 1) { FAIL("fps"); return; }

    PASS();
}

static void test_seq_header_bad_magic(void)
{
    N148SeqHeader parsed;
    uint8_t buf[32] = {0};

    TEST("seq_header rejects bad magic");

    buf[0] = 'X';
    if (n148_seq_header_parse(buf, 32, &parsed) == 0) { FAIL("accepted bad magic"); return; }

    PASS();
}

static void test_bitstream_writer_reader(void)
{
    uint8_t buf[128];
    N148BsWriter wr;
    N148BsReader rd;
    uint32_t val;

    TEST("bitstream write + read bits");

    n148_bs_writer_init(&wr, buf, sizeof(buf));

    n148_bs_write_bits(&wr, 8, 0xAB);
    n148_bs_write_bits(&wr, 4, 0x0C);
    n148_bs_write_bits(&wr, 4, 0x03);
    n148_bs_flush(&wr);

    n148_bs_reader_init(&rd, buf, n148_bs_writer_bytes_written(&wr));

    if (n148_bs_read_bits(&rd, 8, &val) != 0 || val != 0xAB) { FAIL("read 8 bits"); return; }
    if (n148_bs_read_bits(&rd, 4, &val) != 0 || val != 0x0C) { FAIL("read 4 bits"); return; }
    if (n148_bs_read_bits(&rd, 4, &val) != 0 || val != 0x03) { FAIL("read 4 bits(2)"); return; }

    PASS();
}

static void test_exp_golomb_roundtrip(void)
{
    uint8_t buf[256];
    N148BsWriter wr;
    N148BsReader rd;
    uint32_t ue_vals[] = {0, 1, 2, 3, 5, 10, 100, 255, 1000};
    int32_t  se_vals[] = {0, 1, -1, 2, -2, 50, -50, 127, -128};
    int i;

    TEST("exp-golomb ue/se roundtrip");

    n148_bs_writer_init(&wr, buf, sizeof(buf));

    for (i = 0; i < 9; i++)
        if (n148_bs_write_ue(&wr, ue_vals[i]) != 0) { FAIL("write ue"); return; }
    for (i = 0; i < 9; i++)
        if (n148_bs_write_se(&wr, se_vals[i]) != 0) { FAIL("write se"); return; }

    n148_bs_flush(&wr);

    n148_bs_reader_init(&rd, buf, n148_bs_writer_bytes_written(&wr));

    for (i = 0; i < 9; i++) {
        uint32_t v;
        if (n148_bs_read_ue(&rd, &v) != 0 || v != ue_vals[i]) { FAIL("read ue"); return; }
    }
    for (i = 0; i < 9; i++) {
        int32_t v;
        if (n148_bs_read_se(&rd, &v) != 0 || v != se_vals[i]) { FAIL("read se"); return; }
    }

    PASS();
}

static void test_nal_parser(void)
{
   
    uint8_t buf[64];
    N148NalUnit nals[8];
    int count;
    int pos = 0;

    TEST("NAL unit parser");

   
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x01;
    buf[pos++] = 0x20; 
    buf[pos++] = 0xFF; 
    buf[pos++] = 0xFE;

   
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x01;
    buf[pos++] = 0x30; 
    buf[pos++] = 0xAA;
    buf[pos++] = 0xBB;
    buf[pos++] = 0xCC;

    n148_find_nal_units(buf, pos, nals, 8, &count);

    if (count != 2) { FAIL("expected 2 NALs"); return; }
    if (nals[0].nal_type != N148_NAL_SEQ_HDR) { FAIL("NAL 0 type"); return; }
    if (nals[1].nal_type != N148_NAL_FRM_HDR) { FAIL("NAL 1 type"); return; }

    PASS();
}

static void test_intra_pred_dc(void)
{
    uint8_t dst[16];
    uint8_t above[4] = {100, 100, 100, 100};
    uint8_t left[4]  = {200, 200, 200, 200};

    TEST("intra pred DC (avg above+left)");

    n148_intra_pred_4x4(dst, 4, N148_INTRA_DC, above, left, 1, 1);

   
    if (dst[0] != 150 || dst[5] != 150 || dst[15] != 150) {
        FAIL("DC value wrong"); return;
    }

    PASS();
}

static void test_idct_identity(void)
{
    int16_t coeff[16] = {0};
    int16_t out[16];

    TEST("IDCT 4x4 (all zeros → all zeros)");

    n148_idct_4x4(coeff, out);

    {
        int i;
        for (i = 0; i < 16; i++) {
            if (out[i] != 0) { FAIL("non-zero output"); return; }
        }
    }

    PASS();
}

static void test_decoder_create_destroy(void)
{
    N148Decoder* dec = NULL;

    TEST("decoder create + destroy");

    if (n148_decoder_create(&dec) != 0 || !dec) { FAIL("create"); return; }
    n148_decoder_destroy(dec);

    PASS();
}

static void test_decoder_init_from_header(void)
{
    N148Decoder* dec = NULL;
    N148SeqHeader hdr;
    uint8_t buf[32];

    TEST("decoder init from seq header");

    n148_seq_header_defaults(&hdr, 320, 240, 30, N148_PROFILE_MAIN, N148_ENTROPY_CAVLC);
    n148_seq_header_serialize(&hdr, buf, sizeof(buf));

    n148_decoder_create(&dec);
    if (n148_decoder_init_from_seq_header(dec, buf, 32) != 0) {
        FAIL("init failed"); n148_decoder_destroy(dec); return;
    }

    n148_decoder_destroy(dec);
    PASS();
}

int main(void)
{
    printf("=== N.148 Codec Tests (Fase 1 + 2) ===\n\n");

    test_seq_header_roundtrip();
    test_seq_header_bad_magic();
    test_bitstream_writer_reader();
    test_exp_golomb_roundtrip();
    test_nal_parser();
    test_intra_pred_dc();
    test_idct_identity();
    test_decoder_create_destroy();
    test_decoder_init_from_header();

    printf("\n=== Resultado: %d/%d testes passaram ===\n", tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}