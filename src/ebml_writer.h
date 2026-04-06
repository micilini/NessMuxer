

#ifndef NESS_EBML_WRITER_H
#define NESS_EBML_WRITER_H

#include "buffered_io.h"
#include <stdint.h>


typedef struct {
    int64_t pos;       
    int     sizebytes; 
} ebml_master;


int ebml_id_size(uint32_t id);


void ebml_put_id(NessIO* io, uint32_t id);


void ebml_put_size(NessIO* io, uint64_t size, int bytes);


void ebml_put_size_unknown(NessIO* io, int bytes);


int ebml_num_size(uint64_t num);


int ebml_length_size(uint64_t length);


void ebml_put_uint(NessIO* io, uint32_t element_id, uint64_t val);


void ebml_put_float(NessIO* io, uint32_t element_id, double val);


void ebml_put_string(NessIO* io, uint32_t element_id, const char* str);


void ebml_put_binary(NessIO* io, uint32_t element_id, const void* data, int size);


void ebml_put_void(NessIO* io, int size);


ebml_master ebml_start_master(NessIO* io, uint32_t element_id, uint64_t expected_size);


void ebml_end_master(NessIO* io, ebml_master master);

#endif
