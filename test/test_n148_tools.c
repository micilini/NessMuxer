
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "encoder/encoder.h"
#include "encoder/n148/n148_encoder.h"
#include "codec/n148/n148_spec.h"
#include "codec/n148/n148_codec.h"
#include "codec/n148/n148_codec_private.h"
#include "decoder/n148/n148_parser.h"
#include "common/n148_metrics.h"

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) do { tests_total++; printf("  [TEST] %-55s", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL (%s)\n", msg); return 1; } while(0)



static uint8_t s_cap_data[256 * 1024];
static int s_cap_size = 0;
static int s_cap_got = 0;

static int capture_first_packet(void* ud, const NessEncodedPacket* p)
{
    (void)ud;
    if (!s_cap_got && p && p->data && p->size > 0) {
        if (p->size <= (int)sizeof(s_cap_data)) {
            memcpy(s_cap_data, p->data, (size_t)p->size);
            s_cap_size = p->size;
            s_cap_got = 1;
        }
    }
    return 0;
}


static int encode_one_frame(int width, int height, uint8_t** pkt_out, int* pkt_size_out)
{
    const NessEncoderVtable* vt = &g_n148_encoder_vtable;
    void* enc = NULL;
    uint8_t* nv12 = NULL;
    int frame_size = width * height * 3 / 2;
    int y, x;

    *pkt_out = NULL;
    *pkt_size_out = 0;

    nv12 = (uint8_t*)malloc((size_t)frame_size);
    if (!nv12) return -1;

   
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            nv12[y * width + x] = (uint8_t)((x + y * 3) & 0xFF);
    memset(nv12 + width * height, 128, (size_t)(frame_size - width * height));

    if (vt->create(&enc, width, height, 30, 2000) != 0) {
        free(nv12);
        return -1;
    }

    if (vt->submit_frame(enc, nv12, frame_size) != 0) {
        vt->destroy(enc);
        free(nv12);
        return -1;
    }

    s_cap_size = 0;
    s_cap_got = 0;

    vt->drain(enc, capture_first_packet, NULL);

    if (s_cap_got && s_cap_size > 0) {
        *pkt_out = (uint8_t*)malloc((size_t)s_cap_size);
        if (*pkt_out) {
            memcpy(*pkt_out, s_cap_data, (size_t)s_cap_size);
            *pkt_size_out = s_cap_size;
        }
    }

    vt->destroy(enc);
    free(nv12);
    return (*pkt_out) ? 0 : -1;
}

static int test_nal_parsing(void)
{
    uint8_t* pkt = NULL;
    int pkt_size = 0;
    N148NalUnit nals[32];
    int nal_count = 0;
    int has_seq = 0, has_frm = 0, has_slice = 0;
    int i;

    TEST("NAL parsing of encoded packet");

    if (encode_one_frame(64, 64, &pkt, &pkt_size) != 0)
        FAIL("encode_one_frame");

    if (n148_find_nal_units_lp(pkt, pkt_size, nals, 32, &nal_count) != 0) {
        free(pkt);
        FAIL("find_nal_units_lp");
    }

    if (nal_count < 2) {
        free(pkt);
        FAIL("expected at least 2 NALs");
    }

    for (i = 0; i < nal_count; i++) {
        if (nals[i].nal_type == N148_NAL_SEQ_HDR) has_seq = 1;
        if (nals[i].nal_type == N148_NAL_FRM_HDR) has_frm = 1;
        if (nals[i].nal_type == N148_NAL_IDR || nals[i].nal_type == N148_NAL_SLICE) has_slice = 1;
    }

    free(pkt);

    if (!has_seq) FAIL("missing SEQ_HEADER NAL");
    if (!has_frm) FAIL("missing FRM_HEADER NAL");
    if (!has_slice) FAIL("missing SLICE/IDR NAL");

    printf(" [%d NALs]", nal_count);
    PASS();
    return 0;
}

static int test_frame_header_parse(void)
{
    uint8_t* pkt = NULL;
    int pkt_size = 0;
    N148NalUnit nals[32];
    int nal_count = 0;
    int i;

    TEST("Frame header parsing from encoded packet");

    if (encode_one_frame(64, 64, &pkt, &pkt_size) != 0)
        FAIL("encode_one_frame");

    n148_find_nal_units_lp(pkt, pkt_size, nals, 32, &nal_count);

    for (i = 0; i < nal_count; i++) {
        if (nals[i].nal_type == N148_NAL_FRM_HDR) {
            N148FrameHeader fh;
            if (n148_parse_frame_header(nals[i].payload, nals[i].payload_size, &fh) != 0) {
                free(pkt);
                FAIL("parse_frame_header");
            }
            if (fh.frame_type != N148_FRAME_I) {
                free(pkt);
                FAIL("expected I-frame");
            }
            printf(" [I-frame qp=%u]", fh.qp_base);
            free(pkt);
            PASS();
            return 0;
        }
    }

    free(pkt);
    FAIL("no FRM_HDR found");
}

static int test_seq_header_parse(void)
{
    uint8_t* pkt = NULL;
    int pkt_size = 0;
    N148NalUnit nals[32];
    int nal_count = 0;
    int i;

    TEST("Seq header parsing from encoded packet");

    if (encode_one_frame(64, 64, &pkt, &pkt_size) != 0)
        FAIL("encode_one_frame");

    n148_find_nal_units_lp(pkt, pkt_size, nals, 32, &nal_count);

    for (i = 0; i < nal_count; i++) {
        if (nals[i].nal_type == N148_NAL_SEQ_HDR) {
            N148SeqHeader sh;
            if (n148_seq_header_parse(nals[i].payload, nals[i].payload_size, &sh) != 0) {
                free(pkt);
                FAIL("seq_header_parse");
            }
            if (sh.width != 64 || sh.height != 64) {
                free(pkt);
                FAIL("wrong dimensions");
            }
            printf(" [%ux%u]", sh.width, sh.height);
            free(pkt);
            PASS();
            return 0;
        }
    }

    free(pkt);
    FAIL("no SEQ_HDR found");
}

static int test_metrics_on_identical(void)
{
    uint8_t buf[64 * 64];
    double psnr, ssim;
    int i;

    TEST("PSNR/SSIM identical -> perfect scores");

    for (i = 0; i < 64 * 64; i++) buf[i] = (uint8_t)(i & 0xFF);

    psnr = n148_psnr(buf, buf, 64, 64, 64, 64);
    ssim = n148_ssim(buf, buf, 64, 64, 64, 64);

    if (psnr < 90.0) FAIL("PSNR too low");
    if (ssim < 0.99) FAIL("SSIM too low");

    PASS();
    return 0;
}

static int test_metrics_on_different(void)
{
    uint8_t orig[64 * 64];
    uint8_t recon[64 * 64];
    double psnr, ssim;
    int i;

    TEST("PSNR/SSIM different -> sensible values");

    for (i = 0; i < 64 * 64; i++) {
        orig[i] = (uint8_t)(i & 0xFF);
        recon[i] = (uint8_t)((i + 5) & 0xFF);
    }

    psnr = n148_psnr(orig, recon, 64, 64, 64, 64);
    ssim = n148_ssim(orig, recon, 64, 64, 64, 64);

    if (psnr < 5.0 || psnr > 60.0) FAIL("PSNR out of range");
    if (ssim < 0.0 || ssim >= 1.0) FAIL("SSIM out of range");

    printf(" [PSNR=%.1f SSIM=%.3f]", psnr, ssim);
    PASS();
    return 0;
}

int main(void)
{
    int rc = 0;

    printf("=== N.148 Tools Integration Test (Fase 9) ===\n\n");

    if ((rc = test_nal_parsing()) != 0) return rc;
    if ((rc = test_frame_header_parse()) != 0) return rc;
    if ((rc = test_seq_header_parse()) != 0) return rc;
    if ((rc = test_metrics_on_identical()) != 0) return rc;
    if ((rc = test_metrics_on_different()) != 0) return rc;

    printf("\n=== Resultado: %d/%d testes passaram ===\n",
           tests_passed, tests_total);
    return (tests_passed == tests_total) ? 0 : 1;
}