

#include "avc_utils.h"
#include <stdlib.h>
#include <string.h>


const uint8_t* avc_find_startcode(const uint8_t* p, const uint8_t* end)
{
    const uint8_t* out = end;

    if (p + 3 > end)
        return end;

   
    for (; p + 2 < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
            out = p;
           
            if (out > p && out > (const uint8_t*)0 && out[-1] == 0) {
               
            }
            break;
        }
    }

    return out;
}


static const uint8_t* find_startcode_begin(const uint8_t* p, const uint8_t* end)
{
    const uint8_t* sc;

    for (sc = p; sc + 2 < end; sc++) {
        if (sc[0] == 0 && sc[1] == 0) {
            if (sc[2] == 1)
                return sc;
            if (sc + 3 < end && sc[2] == 0 && sc[3] == 1)
                return sc;
        }
    }

    return end;
}


static const uint8_t* skip_startcode(const uint8_t* p, const uint8_t* end, int* sc_size)
{
    if (p + 3 < end && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
        *sc_size = 4;
        return p + 4;
    }
    if (p + 2 < end && p[0] == 0 && p[1] == 0 && p[2] == 1) {
        *sc_size = 3;
        return p + 3;
    }
    *sc_size = 0;
    return p;
}


int avc_extract_sps_pps(const uint8_t* data, int size,
                        const uint8_t** out_sps, int* out_sps_len,
                        const uint8_t** out_pps, int* out_pps_len)
{
    const uint8_t* p = data;
    const uint8_t* end = data + size;
    const uint8_t* nalu_start;
    const uint8_t* next_sc;
    int sc_size;
    uint8_t nalu_type;

    *out_sps = NULL;
    *out_sps_len = 0;
    *out_pps = NULL;
    *out_pps_len = 0;

    if (!data || size <= 0)
        return -1;

    while (p < end) {
       
        const uint8_t* sc = find_startcode_begin(p, end);
        if (sc >= end)
            break;

       
        nalu_start = skip_startcode(sc, end, &sc_size);
        if (sc_size == 0 || nalu_start >= end)
            break;

       
        next_sc = find_startcode_begin(nalu_start, end);

       
        nalu_type = nalu_start[0] & 0x1F;

        if (nalu_type == AVC_NALU_TYPE_SPS && *out_sps == NULL) {
            *out_sps = nalu_start;
            *out_sps_len = (int)(next_sc - nalu_start);
           
            while (*out_sps_len > 1 && (*out_sps)[*out_sps_len - 1] == 0)
                (*out_sps_len)--;
        } else if (nalu_type == AVC_NALU_TYPE_PPS && *out_pps == NULL) {
            *out_pps = nalu_start;
            *out_pps_len = (int)(next_sc - nalu_start);
           
            while (*out_pps_len > 1 && (*out_pps)[*out_pps_len - 1] == 0)
                (*out_pps_len)--;
        }

       
        if (*out_sps && *out_pps)
            return 0;

        p = nalu_start + 1;
    }

   
    if (!*out_sps || !*out_pps)
        return -1;

    return 0;
}


int avc_build_codec_private(const uint8_t* sps, int sps_len,
                            const uint8_t* pps, int pps_len,
                            uint8_t** out, int* out_len)
{
    int total;
    uint8_t* buf;
    int pos = 0;

    if (!sps || sps_len < 4 || !pps || pps_len < 1)
        return -1;

   
    total = 6 + 2 + sps_len + 1 + 2 + pps_len;

    buf = (uint8_t*)malloc(total);
    if (!buf)
        return -1;

   
    buf[pos++] = 1;            
    buf[pos++] = sps[1];       
    buf[pos++] = sps[2];       
    buf[pos++] = sps[3];       
    buf[pos++] = 0xFF;         
    buf[pos++] = 0xE1;         

   
    buf[pos++] = (uint8_t)(sps_len >> 8);
    buf[pos++] = (uint8_t)(sps_len);
    memcpy(buf + pos, sps, sps_len);
    pos += sps_len;

   
    buf[pos++] = 1;            
    buf[pos++] = (uint8_t)(pps_len >> 8);
    buf[pos++] = (uint8_t)(pps_len);
    memcpy(buf + pos, pps, pps_len);
    pos += pps_len;

    *out = buf;
    *out_len = pos;
    return 0;
}


int avc_annexb_to_mp4(const uint8_t* src, int src_size,
                      uint8_t* dst, int dst_capacity,
                      int* dst_size)
{
    const uint8_t* p = src;
    const uint8_t* end = src + src_size;
    const uint8_t* nalu_start;
    const uint8_t* next_sc;
    int sc_size;
    int nalu_len;
    uint8_t nalu_type;
    int out_pos = 0;

    *dst_size = 0;

    while (p < end) {
       
        const uint8_t* sc = find_startcode_begin(p, end);
        if (sc >= end)
            break;

        nalu_start = skip_startcode(sc, end, &sc_size);
        if (sc_size == 0 || nalu_start >= end)
            break;

       
        next_sc = find_startcode_begin(nalu_start, end);
        nalu_len = (int)(next_sc - nalu_start);

       
        while (nalu_len > 1 && nalu_start[nalu_len - 1] == 0)
            nalu_len--;

        if (nalu_len <= 0) {
            p = nalu_start + 1;
            continue;
        }

       
        nalu_type = nalu_start[0] & 0x1F;

       
        if (nalu_type == AVC_NALU_TYPE_SPS ||
            nalu_type == AVC_NALU_TYPE_PPS ||
            nalu_type == 9) {
            p = next_sc;
            continue;
        }

       
        if (out_pos + 4 + nalu_len > dst_capacity)
            return -1;

       
        dst[out_pos++] = (uint8_t)(nalu_len >> 24);
        dst[out_pos++] = (uint8_t)(nalu_len >> 16);
        dst[out_pos++] = (uint8_t)(nalu_len >> 8);
        dst[out_pos++] = (uint8_t)(nalu_len);

       
        memcpy(dst + out_pos, nalu_start, nalu_len);
        out_pos += nalu_len;

        p = next_sc;
    }

    *dst_size = out_pos;
    return 0;
}


int avc_is_keyframe(const uint8_t* data, int size)
{
    const uint8_t* p = data;
    const uint8_t* end = data + size;

    while (p < end) {
        const uint8_t* sc = find_startcode_begin(p, end);
        int sc_size;
        const uint8_t* nalu_start;
        uint8_t nalu_type;

        if (sc >= end)
            break;

        nalu_start = skip_startcode(sc, end, &sc_size);
        if (sc_size == 0 || nalu_start >= end)
            break;

        nalu_type = nalu_start[0] & 0x1F;
        if (nalu_type == AVC_NALU_TYPE_IDR)
            return 1;

        p = nalu_start + 1;
    }

    return 0;
}
