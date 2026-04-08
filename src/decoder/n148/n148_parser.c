#include "n148_parser.h"
#include "../../codec/n148/n148_bitstream.h"
#include <string.h>

int n148_parse_nal_header(uint8_t header_byte, int* nal_type)
{
    if (header_byte & 0x80) 
        return -1;

    *nal_type = (header_byte >> 4) & 0x07;
    return 0;
}

int n148_find_nal_units(const uint8_t* data, int size,
                        N148NalUnit* nals, int max_nals, int* nal_count)
{
    int pos = 0;
    int count = 0;

    *nal_count = 0;

    while (pos + 3 < size && count < max_nals) {
       
        if (data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) {
            int nal_start = pos + 3; 
            int nal_type;
            int next_nal;

            if (nal_start >= size)
                break;

            if (n148_parse_nal_header(data[nal_start], &nal_type) != 0) {
                pos++;
                continue;
            }

           
            next_nal = nal_start + 1;
            while (next_nal + 2 < size) {
                if (data[next_nal] == 0x00 && data[next_nal + 1] == 0x00) {
                    if (data[next_nal + 2] == 0x01) {
                        break;
                    }
                    if (data[next_nal + 2] == 0x03 &&
                        next_nal + 3 < size &&
                        data[next_nal + 3] <= 0x03) {
                        next_nal += 3;
                        continue;
                    }
                }
                next_nal++;
            }
            if (next_nal + 2 >= size)
                next_nal = size;

            nals[count].nal_type     = nal_type;
            nals[count].payload      = data + nal_start + 1; 
            nals[count].payload_size = next_nal - nal_start - 1;
            count++;

            pos = next_nal;
        } else {
            pos++;
        }
    }

    *nal_count = count;
    return 0;
}

int n148_parse_frame_header(const uint8_t* payload, int size, N148FrameHeader* out)
{
    N148BsReader bs;
    uint8_t u8;
    int i;

    if (size < 29) return -1;

    memset(out, 0, sizeof(*out));
    n148_bs_reader_init(&bs, payload, size);

    if (n148_bs_read_u8(&bs, &out->frame_type) != 0) return -1;
    if (n148_bs_read_u32be(&bs, &out->frame_number) != 0) return -1;
    if (n148_bs_read_i64be(&bs, &out->pts) != 0) return -1;
    if (n148_bs_read_i64be(&bs, &out->dts) != 0) return -1;
    if (n148_bs_read_u8(&bs, &out->qp_base) != 0) return -1;
    if (n148_bs_read_u16be(&bs, &out->slice_count) != 0) return -1;
    if (n148_bs_read_u8(&bs, &out->num_ref_frames) != 0) return -1;

    if (out->num_ref_frames > 16) return -1;

    if (n148_bs_read_u32be(&bs, &out->frame_data_size) != 0) return -1;

    return 0;
}

int n148_find_nal_units_lp(const uint8_t* data, int size,
                           N148NalUnit* nals, int max_nals, int* nal_count)
{
    int pos = 0;
    int count = 0;

    if (!data || size <= 0 || !nals || !nal_count)
        return -1;

    *nal_count = 0;

    while (pos + 4 < size && count < max_nals) {
        int nal_size = ((int)data[pos] << 24) |
                       ((int)data[pos + 1] << 16) |
                       ((int)data[pos + 2] << 8) |
                       (int)data[pos + 3];
        int nal_type;
        pos += 4;

        if (nal_size <= 0 || pos + nal_size > size)
            break;

        if (n148_parse_nal_header(data[pos], &nal_type) != 0) {
            pos += nal_size;
            continue;
        }

        nals[count].nal_type     = nal_type;
        nals[count].payload      = data + pos + 1;
        nals[count].payload_size = nal_size - 1;
        count++;

        pos += nal_size;
    }

    *nal_count = count;
    return 0;
}