#ifndef NESS_N148_SPEC_H
#define NESS_N148_SPEC_H

#include <stdint.h>


#define N148_MAGIC_0  0x4E 
#define N148_MAGIC_1  0x31 
#define N148_MAGIC_2  0x34 
#define N148_MAGIC_3  0x38 


#define N148_NAL_SLICE     0
#define N148_NAL_IDR       1
#define N148_NAL_SEQ_HDR   2
#define N148_NAL_FRM_HDR   3
#define N148_NAL_SEI       4


#define N148_FRAME_I  0x01
#define N148_FRAME_P  0x02
#define N148_FRAME_B  0x03


#define N148_PROFILE_MAIN  0x01
#define N148_PROFILE_EPIC  0x02


#define N148_ENTROPY_CAVLC 0x01
#define N148_ENTROPY_CABAC 0x02


#define N148_CHROMA_420    0x01


#define N148_BLOCK_4X4     (1 << 0)
#define N148_BLOCK_8X8     (1 << 1)
#define N148_BLOCK_16X16   (1 << 2)
#define N148_BLOCK_32X32   (1 << 3)


#define N148_INTRA_DC            0
#define N148_INTRA_HORIZONTAL    1
#define N148_INTRA_VERTICAL      2
#define N148_INTRA_DIAG_DL       3
#define N148_INTRA_DIAG_DR       4
#define N148_INTRA_PLANAR        5
#define N148_INTRA_MODE_COUNT    6


#define N148_SEQ_HEADER_SIZE 32

typedef struct {
    uint8_t  magic[4];
    uint8_t  version;
    uint8_t  profile;
    uint8_t  level;
    uint8_t  chroma_format;
    uint8_t  bit_depth;
    uint16_t width;
    uint16_t height;
    uint32_t timescale;
    uint16_t fps_num;
    uint16_t fps_den;
    uint16_t gop_length;
    uint8_t  entropy_mode;
    uint8_t  max_ref_frames;
    uint8_t  max_reorder_depth;
    uint8_t  block_size_flags;
    uint16_t feature_flags;
    uint8_t  reserved[3];
} N148SeqHeader;


typedef struct {
    uint8_t  frame_type;
    uint32_t frame_number;
    int64_t  pts;
    int64_t  dts;
    uint8_t  qp_base;
    uint16_t slice_count;
    uint8_t  num_ref_frames;
    uint8_t  ref_indices[16];  
    uint32_t frame_data_size;
} N148FrameHeader;


typedef struct {
    int      nal_type;
    const uint8_t* payload;
    int      payload_size;
} N148NalUnit;

#endif