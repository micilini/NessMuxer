

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/buffered_io.h"
#include "../src/ebml_writer.h"
#include "../src/mkv_defs.h"

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


static void test_buffered_io(void)
{
    NessIO* io;
    int64_t pos;

    TEST("nio_open + nio_write + nio_tell");

    io = nio_open("test_io.bin");
    assert(io != NULL);

    nio_w8(io, 0xAB);
    nio_wb16(io, 0xCDEF);
    nio_wb32(io, 0x12345678);
    nio_wb64(io, 0x0102030405060708ULL);

    pos = nio_tell(io);
    assert(pos == 1 + 2 + 4 + 8);

    nio_close(io);

   
    {
        FILE* fp = fopen("test_io.bin", "rb");
        unsigned char buf[15];
        size_t n;

        assert(fp != NULL);
        n = fread(buf, 1, 15, fp);
        fclose(fp);
        assert(n == 15);

        assert(buf[0] == 0xAB);
        assert(buf[1] == 0xCD && buf[2] == 0xEF);
        assert(buf[3] == 0x12 && buf[4] == 0x34 && buf[5] == 0x56 && buf[6] == 0x78);
        assert(buf[7] == 0x01 && buf[8] == 0x02 && buf[9] == 0x03 && buf[10] == 0x04);
        assert(buf[11] == 0x05 && buf[12] == 0x06 && buf[13] == 0x07 && buf[14] == 0x08);
    }

    remove("test_io.bin");
    PASS();
}


static void test_nio_fill(void)
{
    NessIO* io;

    TEST("nio_fill");

    io = nio_open("test_fill.bin");
    assert(io != NULL);
    nio_fill(io, 0xFF, 5);
    nio_fill(io, 0x00, 3);
    nio_close(io);

    {
        FILE* fp = fopen("test_fill.bin", "rb");
        unsigned char buf[8];
        size_t n;
        int i;

        assert(fp != NULL);
        n = fread(buf, 1, 8, fp);
        fclose(fp);
        assert(n == 8);

        for (i = 0; i < 5; i++) assert(buf[i] == 0xFF);
        for (i = 5; i < 8; i++) assert(buf[i] == 0x00);
    }

    remove("test_fill.bin");
    PASS();
}


static void test_nio_seek(void)
{
    NessIO* io;

    TEST("nio_seek + overwrite");

    io = nio_open("test_seek.bin");
    assert(io != NULL);
    nio_wb32(io, 0xAAAAAAAA);
    nio_wb32(io, 0xBBBBBBBB);

   
    nio_seek(io, 0, SEEK_SET);
    nio_wb32(io, 0xCCCCCCCC);
    nio_seek(io, 0, SEEK_END);
    nio_close(io);

    {
        FILE* fp = fopen("test_seek.bin", "rb");
        unsigned char buf[8];

        assert(fp != NULL);
        fread(buf, 1, 8, fp);
        fclose(fp);

       
        assert(buf[0] == 0xCC && buf[1] == 0xCC);
        assert(buf[4] == 0xBB && buf[5] == 0xBB);
    }

    remove("test_seek.bin");
    PASS();
}


static void test_ebml_id_size(void)
{
    TEST("ebml_id_size");

   
    assert(ebml_id_size(0xEC) == 1);
    assert(ebml_id_size(0xA3) == 1); 

   
    assert(ebml_id_size(0xD7) == 1);
    assert(ebml_id_size(0x4286) == 2); 

   
    assert(ebml_id_size(0x23E383) == 3); 

   
    assert(ebml_id_size(0x1A45DFA3) == 4); 
    assert(ebml_id_size(0x18538067) == 4); 
    assert(ebml_id_size(0x1F43B675) == 4); 

    PASS();
}


static void test_ebml_sizes(void)
{
    TEST("ebml_num_size + ebml_length_size");

   
    assert(ebml_num_size(0) == 1);
    assert(ebml_num_size(1) == 1);
    assert(ebml_num_size(126) == 1);
    assert(ebml_num_size(127) == 1);
    assert(ebml_num_size(128) == 2);

   
    assert(ebml_length_size(0) == 1);
    assert(ebml_length_size(126) == 1);  
    assert(ebml_length_size(127) == 2);  

    PASS();
}


static void test_ebml_header(void)
{
    NessIO* io;
    ebml_master header;

    TEST("EBML Header completo");

    io = nio_open("test_ebml.bin");
    assert(io != NULL);

   
    header = ebml_start_master(io, EBML_ID_HEADER, 0);
    ebml_put_uint(io, EBML_ID_EBMLVERSION, 1);
    ebml_put_uint(io, EBML_ID_EBMLREADVERSION, 1);
    ebml_put_uint(io, EBML_ID_EBMLMAXIDLENGTH, 4);
    ebml_put_uint(io, EBML_ID_EBMLMAXSIZELENGTH, 8);
    ebml_put_string(io, EBML_ID_DOCTYPE, "matroska");
    ebml_put_uint(io, EBML_ID_DOCTYPEVERSION, 4);
    ebml_put_uint(io, EBML_ID_DOCTYPEREADVERSION, 2);
    ebml_end_master(io, header);

    {
        int64_t pos = nio_tell(io);
       
        assert(pos > 20 && pos < 100);
    }

    nio_close(io);

   
    {
        FILE* fp = fopen("test_ebml.bin", "rb");
        unsigned char buf[4];
        size_t n;

        assert(fp != NULL);
        n = fread(buf, 1, 4, fp);
        fclose(fp);
        assert(n == 4);

        assert(buf[0] == 0x1A);
        assert(buf[1] == 0x45);
        assert(buf[2] == 0xDF);
        assert(buf[3] == 0xA3);
    }

    PASS();
}


static void test_master_element(void)
{
    NessIO* io;
    ebml_master master;
    int64_t content_start, content_end;

    TEST("start_master + end_master (size patch)");

    io = nio_open("test_master.bin");
    assert(io != NULL);

   
    master = ebml_start_master(io, MATROSKA_ID_INFO, 0);
    content_start = nio_tell(io);

   
    ebml_put_uint(io, MATROSKA_ID_TIMESTAMPSCALE, 1000000);
    ebml_put_string(io, MATROSKA_ID_MUXINGAPP, "NessMuxer");
    ebml_put_string(io, MATROSKA_ID_WRITINGAPP, "NessStudio");

    content_end = nio_tell(io);

   
    ebml_end_master(io, master);

   
    assert(nio_tell(io) == content_end);

    nio_close(io);

   
    {
        FILE* fp = fopen("test_master.bin", "rb");
        unsigned char buf[4];

        assert(fp != NULL);
        fread(buf, 1, 4, fp);
        fclose(fp);

        assert(buf[0] == 0x15);
        assert(buf[1] == 0x49);
        assert(buf[2] == 0xA9);
        assert(buf[3] == 0x66);
    }

    remove("test_master.bin");
    PASS();
}


static void test_void_element(void)
{
    NessIO* io;
    int64_t pos;

    TEST("ebml_put_void (reserva espaco)");

    io = nio_open("test_void.bin");
    assert(io != NULL);

   
    ebml_put_void(io, 120);
    pos = nio_tell(io);
    assert(pos == 120);

   
    ebml_put_void(io, 2);
    pos = nio_tell(io);
    assert(pos == 122);

   
    ebml_put_void(io, 9);
    pos = nio_tell(io);
    assert(pos == 131);

   
    ebml_put_void(io, 10);
    pos = nio_tell(io);
    assert(pos == 141);

    nio_close(io);

   
    {
        FILE* fp = fopen("test_void.bin", "rb");
        unsigned char buf[1];

        assert(fp != NULL);
        fread(buf, 1, 1, fp);
        fclose(fp);
        assert(buf[0] == 0xEC);
    }

    remove("test_void.bin");
    PASS();
}


static void test_float_element(void)
{
    NessIO* io;

    TEST("ebml_put_float");

    io = nio_open("test_float.bin");
    assert(io != NULL);

    ebml_put_float(io, MATROSKA_ID_DURATION, 3000.0);

   
   
    assert(nio_tell(io) == 11);

    nio_close(io);
    remove("test_float.bin");
    PASS();
}


static void test_segment_structure(void)
{
    NessIO* io;
    ebml_master header, segment;
    int64_t segment_offset, seekhead_offset;

    TEST("Segment + SeekHead placeholder");

    io = nio_open("test_segment.bin");
    assert(io != NULL);

   
    header = ebml_start_master(io, EBML_ID_HEADER, 0);
    ebml_put_uint(io, EBML_ID_EBMLVERSION, 1);
    ebml_put_uint(io, EBML_ID_EBMLREADVERSION, 1);
    ebml_put_uint(io, EBML_ID_EBMLMAXIDLENGTH, 4);
    ebml_put_uint(io, EBML_ID_EBMLMAXSIZELENGTH, 8);
    ebml_put_string(io, EBML_ID_DOCTYPE, "matroska");
    ebml_put_uint(io, EBML_ID_DOCTYPEVERSION, 4);
    ebml_put_uint(io, EBML_ID_DOCTYPEREADVERSION, 2);
    ebml_end_master(io, header);

   
    segment = ebml_start_master(io, MATROSKA_ID_SEGMENT, 0);
    segment_offset = nio_tell(io);

   
    seekhead_offset = nio_tell(io);
    ebml_put_void(io, 120);

   
    assert(nio_tell(io) == seekhead_offset + 120);

    nio_close(io);

   
    {
        FILE* fp = fopen("test_segment.bin", "rb");
        unsigned char buf[4];

        assert(fp != NULL);
        fread(buf, 1, 4, fp);
        fclose(fp);
        assert(buf[0] == 0x1A && buf[1] == 0x45);
    }

    remove("test_segment.bin");
    PASS();
}

int main(void)
{
    printf("\n=== NessMuxer - Testes Fase 1 (EBML + I/O) ===\n\n");

    test_buffered_io();
    test_nio_fill();
    test_nio_seek();
    test_ebml_id_size();
    test_ebml_sizes();
    test_ebml_header();
    test_master_element();
    test_void_element();
    test_float_element();
    test_segment_structure();

    printf("\n=== Resultado: %d/%d testes passaram ===\n\n",
           tests_passed, tests_total);

    return (tests_passed == tests_total) ? 0 : 1;
}
