#ifndef NESS_N148_PARSER_H
#define NESS_N148_PARSER_H

#include "../../codec/n148/n148_spec.h"


int n148_find_nal_units(const uint8_t* data, int size,
                        N148NalUnit* nals, int max_nals, int* nal_count);


int n148_parse_nal_header(uint8_t header_byte, int* nal_type);


int n148_parse_frame_header(const uint8_t* payload, int size, N148FrameHeader* out);

#endif