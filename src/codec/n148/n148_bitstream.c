#include "n148_bitstream.h"
#include <string.h>



void n148_bs_reader_init(N148BsReader* bs, const uint8_t* data, int size)
{
    bs->buf      = data;
    bs->size     = size;
    bs->byte_pos = 0;
    bs->bit_pos  = 7;
}

int n148_bs_eof(const N148BsReader* bs)
{
    return bs->byte_pos >= bs->size;
}

int n148_bs_bytes_remaining(const N148BsReader* bs)
{
    int rem = bs->size - bs->byte_pos;
    return rem > 0 ? rem : 0;
}

int n148_bs_align(N148BsReader* bs)
{
    if (bs->bit_pos != 7) {
        bs->byte_pos++;
        bs->bit_pos = 7;
    }
    return 0;
}

int n148_bs_read_bits(N148BsReader* bs, int n, uint32_t* out)
{
    uint32_t val = 0;
    int i;

    for (i = 0; i < n; i++) {
        if (bs->byte_pos >= bs->size)
            return -1;

        val <<= 1;
        val |= (bs->buf[bs->byte_pos] >> bs->bit_pos) & 1;

        if (bs->bit_pos == 0) {
            bs->bit_pos = 7;
            bs->byte_pos++;
        } else {
            bs->bit_pos--;
        }
    }

    *out = val;
    return 0;
}

int n148_bs_read_u8(N148BsReader* bs, uint8_t* out)
{
    uint32_t v;
    if (n148_bs_read_bits(bs, 8, &v) != 0) return -1;
    *out = (uint8_t)v;
    return 0;
}

int n148_bs_read_u16be(N148BsReader* bs, uint16_t* out)
{
    uint32_t v;
    if (n148_bs_read_bits(bs, 16, &v) != 0) return -1;
    *out = (uint16_t)v;
    return 0;
}

int n148_bs_read_u32be(N148BsReader* bs, uint32_t* out)
{
    uint32_t hi, lo;
    if (n148_bs_read_bits(bs, 16, &hi) != 0) return -1;
    if (n148_bs_read_bits(bs, 16, &lo) != 0) return -1;
    *out = (hi << 16) | lo;
    return 0;
}

int n148_bs_read_i64be(N148BsReader* bs, int64_t* out)
{
    uint32_t hi, lo;
    if (n148_bs_read_u32be(bs, &hi) != 0) return -1;
    if (n148_bs_read_u32be(bs, &lo) != 0) return -1;
    *out = ((int64_t)hi << 32) | (int64_t)lo;
    return 0;
}

int n148_bs_read_ue(N148BsReader* bs, uint32_t* out)
{
    int leading_zeros = 0;
    uint32_t bit;

   
    while (1) {
        if (n148_bs_read_bits(bs, 1, &bit) != 0) return -1;
        if (bit) break;
        leading_zeros++;
        if (leading_zeros > 31) return -1;
    }

    if (leading_zeros == 0) {
        *out = 0;
        return 0;
    }

    {
        uint32_t suffix;
        if (n148_bs_read_bits(bs, leading_zeros, &suffix) != 0) return -1;
        *out = (1u << leading_zeros) - 1 + suffix;
    }
    return 0;
}

int n148_bs_read_se(N148BsReader* bs, int32_t* out)
{
    uint32_t ue;
    if (n148_bs_read_ue(bs, &ue) != 0) return -1;

    if (ue & 1)
        *out = (int32_t)((ue + 1) >> 1);
    else
        *out = -(int32_t)(ue >> 1);
    return 0;
}



void n148_bs_writer_init(N148BsWriter* bs, uint8_t* buf, int capacity)
{
    bs->buf       = buf;
    bs->capacity  = capacity;
    bs->byte_pos  = 0;
    bs->bit_buf   = 0;
    bs->bits_left = 8;
}

int n148_bs_write_bits(N148BsWriter* bs, int n, uint32_t val)
{
    int i;
    for (i = n - 1; i >= 0; i--) {
        if (bs->byte_pos >= bs->capacity)
            return -1;

        bs->bit_buf <<= 1;
        bs->bit_buf |= (val >> i) & 1;
        bs->bits_left--;

        if (bs->bits_left == 0) {
            bs->buf[bs->byte_pos++] = (uint8_t)bs->bit_buf;
            bs->bit_buf = 0;
            bs->bits_left = 8;
        }
    }
    return 0;
}

int n148_bs_write_u8(N148BsWriter* bs, uint8_t val)
{
    return n148_bs_write_bits(bs, 8, val);
}

int n148_bs_write_u16be(N148BsWriter* bs, uint16_t val)
{
    return n148_bs_write_bits(bs, 16, val);
}

int n148_bs_write_u32be(N148BsWriter* bs, uint32_t val)
{
    if (n148_bs_write_bits(bs, 16, val >> 16) != 0) return -1;
    return n148_bs_write_bits(bs, 16, val & 0xFFFF);
}

int n148_bs_write_i64be(N148BsWriter* bs, int64_t val)
{
    uint64_t u = (uint64_t)val;
    if (n148_bs_write_u32be(bs, (uint32_t)(u >> 32)) != 0) return -1;
    return n148_bs_write_u32be(bs, (uint32_t)(u & 0xFFFFFFFF));
}

int n148_bs_write_ue(N148BsWriter* bs, uint32_t val)
{
    uint32_t code = val + 1;
    int bits = 0;
    uint32_t tmp = code;

   
    while (tmp > 0) {
        bits++;
        tmp >>= 1;
    }

   
    {
        int leading = bits - 1;
        int total = 2 * leading + 1;
        return n148_bs_write_bits(bs, total, code);
    }
}

int n148_bs_write_se(N148BsWriter* bs, int32_t val)
{
    uint32_t ue;
    if (val > 0)
        ue = (uint32_t)(2 * val - 1);
    else if (val < 0)
        ue = (uint32_t)(-2 * val);
    else
        ue = 0;
    return n148_bs_write_ue(bs, ue);
}

int n148_bs_flush(N148BsWriter* bs)
{
    if (bs->bits_left < 8) {
        bs->bit_buf <<= bs->bits_left;
        if (bs->byte_pos >= bs->capacity) return -1;
        bs->buf[bs->byte_pos++] = (uint8_t)bs->bit_buf;
        bs->bit_buf = 0;
        bs->bits_left = 8;
    }
    return 0;
}

int n148_bs_writer_bytes_written(const N148BsWriter* bs)
{
    return bs->byte_pos + (bs->bits_left < 8 ? 1 : 0);
}