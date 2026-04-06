

#include "ebml_writer.h"
#include "mkv_defs.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>


int ebml_id_size(uint32_t id)
{
    int bytes = 0;
    uint32_t tmp = id;
    while (tmp > 0) {
        bytes++;
        tmp >>= 8;
    }
    return bytes > 0 ? bytes : 1;
}


void ebml_put_id(NessIO* io, uint32_t id)
{
    int i = ebml_id_size(id);
    while (i > 0) {
        i--;
        nio_w8(io, (uint8_t)(id >> (i * 8)));
    }
}


int ebml_num_size(uint64_t num)
{
    int bytes = 0;
    do {
        bytes++;
    } while (num >>= 7);
    return bytes;
}


int ebml_length_size(uint64_t length)
{
    return ebml_num_size(length + 1);
}


static void ebml_put_num(NessIO* io, uint64_t num, int bytes)
{
    int i;
    num |= 1ULL << (bytes * 7);
    for (i = bytes - 1; i >= 0; i--)
        nio_w8(io, (uint8_t)(num >> (i * 8)));
}


void ebml_put_size(NessIO* io, uint64_t size, int bytes)
{
    int needed = ebml_length_size(size);
    if (bytes == 0)
        bytes = needed;
    assert(bytes >= needed);
    ebml_put_num(io, size, bytes);
}


void ebml_put_size_unknown(NessIO* io, int bytes)
{
    assert(bytes >= 1 && bytes <= 8);
   
    nio_w8(io, (uint8_t)(0x1FF >> bytes));
    if (bytes > 1)
        nio_fill(io, 0xFF, bytes - 1);
}


void ebml_put_uint(NessIO* io, uint32_t element_id, uint64_t val)
{
    int i, bytes = 1;
    uint64_t tmp = val;

    while (tmp >>= 8)
        bytes++;

    ebml_put_id(io, element_id);
    ebml_put_size(io, bytes, 0);

    for (i = bytes - 1; i >= 0; i--)
        nio_w8(io, (uint8_t)(val >> (i * 8)));
}


void ebml_put_float(NessIO* io, uint32_t element_id, double val)
{
    union { double d; uint64_t u; } conv;
    conv.d = val;

    ebml_put_id(io, element_id);
    ebml_put_size(io, 8, 0);
    nio_wb64(io, conv.u);
}


void ebml_put_string(NessIO* io, uint32_t element_id, const char* str)
{
    size_t len = strlen(str);
    ebml_put_id(io, element_id);
    ebml_put_size(io, (uint64_t)len, 0);
    nio_write(io, str, len);
}


void ebml_put_binary(NessIO* io, uint32_t element_id, const void* data, int size)
{
    ebml_put_id(io, element_id);
    ebml_put_size(io, (uint64_t)size, 0);
    nio_write(io, data, (size_t)size);
}


void ebml_put_void(NessIO* io, int size)
{
    assert(size >= 2);

    ebml_put_id(io, EBML_ID_VOID);

    if (size < 10) {
        size -= 2;
        ebml_put_size(io, (uint64_t)size, 0);
    } else {
        size -= 9;
        ebml_put_size(io, (uint64_t)size, 8);
    }

    nio_fill(io, 0, size);
}


ebml_master ebml_start_master(NessIO* io, uint32_t element_id, uint64_t expected_size)
{
    ebml_master m;
    int bytes = expected_size ? ebml_length_size(expected_size) : 8;

    ebml_put_id(io, element_id);
    ebml_put_size_unknown(io, bytes);

    m.pos = nio_tell(io);
    m.sizebytes = bytes;
    return m;
}


void ebml_end_master(NessIO* io, ebml_master master)
{
    int64_t pos = nio_tell(io);
    int64_t size = pos - master.pos;

   
    nio_seek(io, master.pos - master.sizebytes, SEEK_SET);

   
    ebml_put_size(io, (uint64_t)size, master.sizebytes);

   
    nio_seek(io, pos, SEEK_SET);
}
