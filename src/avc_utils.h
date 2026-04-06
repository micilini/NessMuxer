

#ifndef NESS_AVC_UTILS_H
#define NESS_AVC_UTILS_H

#include <stdint.h>


#define AVC_NALU_TYPE_SLICE     1
#define AVC_NALU_TYPE_IDR       5
#define AVC_NALU_TYPE_SEI       6
#define AVC_NALU_TYPE_SPS       7
#define AVC_NALU_TYPE_PPS       8


const uint8_t* avc_find_startcode(const uint8_t* p, const uint8_t* end);


int avc_extract_sps_pps(const uint8_t* data, int size,
                        const uint8_t** out_sps, int* out_sps_len,
                        const uint8_t** out_pps, int* out_pps_len);


int avc_build_codec_private(const uint8_t* sps, int sps_len,
                            const uint8_t* pps, int pps_len,
                            uint8_t** out, int* out_len);


int avc_annexb_to_mp4(const uint8_t* src, int src_size,
                      uint8_t* dst, int dst_capacity,
                      int* dst_size);


int avc_is_keyframe(const uint8_t* data, int size);

#endif
