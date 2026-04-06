

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/avc_utils.h"

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name) do { \
    tests_total++; \
    printf("  [TEST] %-40s ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while(0)


static const uint8_t sample_sps[] = {
    0x67,                  
    0x42, 0xC0, 0x1E,     
    0xD9, 0x00, 0xA0, 0x47, 0xFE, 0xC8 
};
static const int sample_sps_len = sizeof(sample_sps);

static const uint8_t sample_pps[] = {
    0x68,                  
    0xCE, 0x38, 0x80       
};
static const int sample_pps_len = sizeof(sample_pps);


static uint8_t* build_annexb_stream(int* out_size)
{
   
    uint8_t idr_fake[] = { 0x65, 0x88, 0x80, 0x40, 0x00, 0xFF, 0xFF };
    int idr_len = sizeof(idr_fake);

    int total = 4 + sample_sps_len + 4 + sample_pps_len + 4 + idr_len;
    uint8_t* buf = (uint8_t*)malloc(total);
    int pos = 0;

   
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x01;
    memcpy(buf + pos, sample_sps, sample_sps_len); pos += sample_sps_len;

   
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x01;
    memcpy(buf + pos, sample_pps, sample_pps_len); pos += sample_pps_len;

   
    buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x01;
    memcpy(buf + pos, idr_fake, idr_len); pos += idr_len;

    *out_size = pos;
    return buf;
}


static void test_find_startcode(void)
{
   
    uint8_t buf[] = {
        0x00, 0x00, 0x00, 0x01, 
        0x67, 0x42, 0xC0,       
        0x00, 0x00, 0x01,       
        0x68, 0xCE              
    };
    int size = sizeof(buf);
    const uint8_t* sc;

    TEST("avc_find_startcode");

   
    sc = avc_find_startcode(buf, buf + size);
    assert(sc == buf);

   
    sc = avc_find_startcode(buf + 4, buf + size);
    assert(sc != buf + size);
    assert(sc[0] == 0x00 && sc[1] == 0x00 && sc[2] == 0x01);

    PASS();
}


static void test_extract_sps_pps(void)
{
    uint8_t* stream;
    int stream_size;
    const uint8_t* sps;
    const uint8_t* pps;
    int sps_len, pps_len;
    int ret;

    TEST("avc_extract_sps_pps");

    stream = build_annexb_stream(&stream_size);

    ret = avc_extract_sps_pps(stream, stream_size,
                               &sps, &sps_len,
                               &pps, &pps_len);

    assert(ret == 0);
    assert(sps != NULL);
    assert(pps != NULL);
    assert(sps_len == sample_sps_len);
    assert(pps_len == sample_pps_len);

   
    assert(sps[0] == 0x67); 
    assert(pps[0] == 0x68); 

   
    assert(sps[1] == 0x42); 
    assert(sps[3] == 0x1E); 

    free(stream);
    PASS();
}


static void test_build_codec_private(void)
{
    uint8_t* avcc = NULL;
    int avcc_len = 0;
    int ret;
    int offset;

    TEST("avc_build_codec_private (AVCC)");

    ret = avc_build_codec_private(sample_sps, sample_sps_len,
                                   sample_pps, sample_pps_len,
                                   &avcc, &avcc_len);
    assert(ret == 0);
    assert(avcc != NULL);

   
    assert(avcc[0] == 1);      
    assert(avcc[1] == 0x42);   
    assert(avcc[2] == 0xC0);   
    assert(avcc[3] == 0x1E);   
    assert(avcc[4] == 0xFF);   
    assert(avcc[5] == 0xE1);   

   
    {
        int sps_len_read = (avcc[6] << 8) | avcc[7];
        assert(sps_len_read == sample_sps_len);
    }

   
    assert(avcc[8] == 0x67);

   
    offset = 8 + sample_sps_len;
    assert(avcc[offset] == 1); 

   
    {
        int pps_len_read = (avcc[offset + 1] << 8) | avcc[offset + 2];
        assert(pps_len_read == sample_pps_len);
    }

   
    assert(avcc[offset + 3] == 0x68);

   
    {
        int expected = 6 + 2 + sample_sps_len + 1 + 2 + sample_pps_len;
        assert(avcc_len == expected);
    }

    free(avcc);
    PASS();
}


static void test_annexb_to_mp4(void)
{
    uint8_t* stream;
    int stream_size;
    uint8_t dst[512];
    int dst_size = 0;
    int ret;

    TEST("avc_annexb_to_mp4 (length-prefixed)");

    stream = build_annexb_stream(&stream_size);

    ret = avc_annexb_to_mp4(stream, stream_size,
                             dst, sizeof(dst),
                             &dst_size);
    assert(ret == 0);
    assert(dst_size > 0);

   
    {
       
        int nalu_len = (dst[0] << 24) | (dst[1] << 16) | (dst[2] << 8) | dst[3];
        uint8_t nalu_type;
        assert(nalu_len > 0);
        assert(nalu_len + 4 == dst_size);

       
        nalu_type = dst[4] & 0x1F;
        assert(nalu_type == 5); 
    }

    free(stream);
    PASS();
}


static void test_is_keyframe(void)
{
    uint8_t* stream;
    int stream_size;

    TEST("avc_is_keyframe");

   
    stream = build_annexb_stream(&stream_size);
    assert(avc_is_keyframe(stream, stream_size) == 1);
    free(stream);

   
    {
        uint8_t non_idr[] = {
            0x00, 0x00, 0x00, 0x01,
            0x41, 0x9A, 0x00, 0x04, 0xFF 
        };
        assert(avc_is_keyframe(non_idr, sizeof(non_idr)) == 0);
    }

    PASS();
}


static void test_3byte_startcodes(void)
{
   
    uint8_t buf[] = {
        0x00, 0x00, 0x01,          
        0x67, 0x42, 0xC0, 0x1E,   
        0xD9, 0x00, 0xA0, 0x47, 0xFE, 0xC8,
        0x00, 0x00, 0x01,          
        0x68, 0xCE, 0x38, 0x80    
    };
    const uint8_t* sps = NULL;
    const uint8_t* pps = NULL;
    int sps_len = 0, pps_len = 0;
    int ret;

    TEST("avc_extract com 3-byte start codes");

    ret = avc_extract_sps_pps(buf, sizeof(buf),
                               &sps, &sps_len,
                               &pps, &pps_len);

    assert(ret == 0);
    assert(sps != NULL && sps[0] == 0x67);
    assert(pps != NULL && pps[0] == 0x68);

    PASS();
}


static void test_codec_private_roundtrip(void)
{
    uint8_t* stream = NULL;
    int stream_size = 0;
    const uint8_t* sps = NULL;
    const uint8_t* pps = NULL;
    int sps_len = 0, pps_len = 0;
    uint8_t* avcc = NULL;
    int avcc_len = 0;
    int ret;

    TEST("roundtrip: Annex-B -> extract -> AVCC");

   
    stream = build_annexb_stream(&stream_size);
    assert(stream != NULL);

   
    ret = avc_extract_sps_pps(stream, stream_size,
                               &sps, &sps_len,
                               &pps, &pps_len);
    assert(ret == 0);
    assert(sps != NULL);
    assert(pps != NULL);

   
    ret = avc_build_codec_private(sps, sps_len, pps, pps_len,
                                   &avcc, &avcc_len);
    assert(ret == 0);
    assert(avcc != NULL);
    assert(avcc_len > 0);

   
    assert(avcc[0] == 1);      
    assert(avcc[1] == sps[1]); 
    assert(avcc[3] == sps[3]); 

    free(avcc);
    free(stream);
    PASS();
}


static void test_edge_cases(void)
{
    const uint8_t* sps = NULL;
    const uint8_t* pps = NULL;
    int sps_len = 0, pps_len = 0;
    int ret;

    TEST("edge cases (buffer vazio, sem SPS)");

   
    {
        uint8_t empty = 0;
        ret = avc_extract_sps_pps(&empty, 0, &sps, &sps_len, &pps, &pps_len);
        assert(ret == -1); 
    }

   
    {
        uint8_t garbage[] = { 0x01, 0x02, 0x03, 0x04 };
        ret = avc_extract_sps_pps(garbage, sizeof(garbage),
                                   &sps, &sps_len, &pps, &pps_len);
        assert(ret == -1); 
    }

   
    {
        uint8_t only_sps[] = {
            0x00, 0x00, 0x00, 0x01,
            0x67, 0x42, 0xC0, 0x1E, 0xD9
        };
        ret = avc_extract_sps_pps(only_sps, sizeof(only_sps),
                                   &sps, &sps_len, &pps, &pps_len);
        assert(ret == -1); 
    }

    PASS();
}

int main(void)
{
    printf("\n=== NessMuxer - Testes Fase 2 (AVC Utils) ===\n\n");

    test_find_startcode();
    test_extract_sps_pps();
    test_build_codec_private();
    test_annexb_to_mp4();
    test_is_keyframe();
    test_3byte_startcodes();
    test_codec_private_roundtrip();
    test_edge_cases();

    printf("\n=== Resultado: %d/%d testes passaram ===\n\n",
           tests_passed, tests_total);

    return (tests_passed == tests_total) ? 0 : 1;
}
