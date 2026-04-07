#ifndef NESS_N148_BITSTREAM_H
#define NESS_N148_BITSTREAM_H

#include <stdint.h>


typedef struct {
    const uint8_t* buf;
    int            size;
    int            byte_pos;
    int            bit_pos;  
} N148BsReader;

void  n148_bs_reader_init(N148BsReader* bs, const uint8_t* data, int size);
int   n148_bs_read_bits(N148BsReader* bs, int n, uint32_t* out);
int   n148_bs_read_u8(N148BsReader* bs, uint8_t* out);
int   n148_bs_read_u16be(N148BsReader* bs, uint16_t* out);
int   n148_bs_read_u32be(N148BsReader* bs, uint32_t* out);
int   n148_bs_read_i64be(N148BsReader* bs, int64_t* out);
int   n148_bs_read_ue(N148BsReader* bs, uint32_t* out);  
int   n148_bs_read_se(N148BsReader* bs, int32_t* out);   
int   n148_bs_eof(const N148BsReader* bs);
int   n148_bs_align(N148BsReader* bs); 
int   n148_bs_bytes_remaining(const N148BsReader* bs);


typedef struct {
    uint8_t* buf;
    int      capacity;
    int      byte_pos;
    int      bit_buf;
    int      bits_left;  
} N148BsWriter;

void  n148_bs_writer_init(N148BsWriter* bs, uint8_t* buf, int capacity);
int   n148_bs_write_bits(N148BsWriter* bs, int n, uint32_t val);
int   n148_bs_write_u8(N148BsWriter* bs, uint8_t val);
int   n148_bs_write_u16be(N148BsWriter* bs, uint16_t val);
int   n148_bs_write_u32be(N148BsWriter* bs, uint32_t val);
int   n148_bs_write_i64be(N148BsWriter* bs, int64_t val);
int   n148_bs_write_ue(N148BsWriter* bs, uint32_t val);  
int   n148_bs_write_se(N148BsWriter* bs, int32_t val);   
int   n148_bs_flush(N148BsWriter* bs);
int   n148_bs_writer_bytes_written(const N148BsWriter* bs);

#endif