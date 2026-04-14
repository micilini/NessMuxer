#ifndef N148_PIXEL_SSE2_H
#define N148_PIXEL_SSE2_H

#include <stdint.h>
#include <string.h>

#if (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && (_M_IX86_FP >= 2))) && \
    (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
#define N148_HAVE_SSE2 1
#include <emmintrin.h>

static inline int n148_sse2_hsum_sad(__m128i v)
{
    uint64_t sums[2];
    _mm_storeu_si128((__m128i*)sums, v);
    return (int)(sums[0] + sums[1]);
}


static inline __m128i n148_sse2_abs_epi16(__m128i v)
{
    __m128i mask = _mm_srai_epi16(v, 15);
    return _mm_sub_epi16(_mm_xor_si128(v, mask), mask);
}

static inline void n148_sse2_hadamard4_epi16(__m128i r0, __m128i r1, __m128i r2, __m128i r3,
                                              __m128i* o0, __m128i* o1, __m128i* o2, __m128i* o3)
{
    __m128i t0 = _mm_add_epi16(r0, r3);
    __m128i t1 = _mm_add_epi16(r1, r2);
    __m128i t2 = _mm_sub_epi16(r1, r2);
    __m128i t3 = _mm_sub_epi16(r0, r3);
    *o0 = _mm_add_epi16(t0, t1);
    *o1 = _mm_add_epi16(t3, t2);
    *o2 = _mm_sub_epi16(t0, t1);
    *o3 = _mm_sub_epi16(t3, t2);
}

static inline int n148_sse2_hsum_epi32(__m128i v)
{
    uint32_t sums[4];
    _mm_storeu_si128((__m128i*)sums, v);
    return (int)(sums[0] + sums[1] + sums[2] + sums[3]);
}

static inline int n148_sse2_satd_4x4(const uint8_t* cur, int cur_stride,
                                      const uint8_t* ref, int ref_stride)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i ones = _mm_set1_epi16(1);
    const __m128i low4_mask = _mm_set_epi16(0, 0, 0, 0, -1, -1, -1, -1);
    __m128i r0, r1, r2, r3;
    __m128i c0, c1, c2, c3;
    __m128i t01, t23, u0, u1;
    __m128i tr0, tr1, tr2, tr3;
    __m128i h0, h1, h2, h3;
    __m128i sum32;
    uint32_t a = 0, b = 0;

    memcpy(&a, cur + 0 * cur_stride, sizeof(uint32_t));
    memcpy(&b, ref + 0 * ref_stride, sizeof(uint32_t));
    r0 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)a), zero),
                       _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)b), zero));

    memcpy(&a, cur + 1 * cur_stride, sizeof(uint32_t));
    memcpy(&b, ref + 1 * ref_stride, sizeof(uint32_t));
    r1 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)a), zero),
                       _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)b), zero));

    memcpy(&a, cur + 2 * cur_stride, sizeof(uint32_t));
    memcpy(&b, ref + 2 * ref_stride, sizeof(uint32_t));
    r2 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)a), zero),
                       _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)b), zero));

    memcpy(&a, cur + 3 * cur_stride, sizeof(uint32_t));
    memcpy(&b, ref + 3 * ref_stride, sizeof(uint32_t));
    r3 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)a), zero),
                       _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)b), zero));

    n148_sse2_hadamard4_epi16(r0, r1, r2, r3, &c0, &c1, &c2, &c3);

    t01 = _mm_unpacklo_epi16(c0, c1);
    t23 = _mm_unpacklo_epi16(c2, c3);
    u0 = _mm_unpacklo_epi32(t01, t23);
    u1 = _mm_unpackhi_epi32(t01, t23);
    tr0 = _mm_and_si128(u0, low4_mask);
    tr1 = _mm_and_si128(_mm_srli_si128(u0, 8), low4_mask);
    tr2 = _mm_and_si128(u1, low4_mask);
    tr3 = _mm_and_si128(_mm_srli_si128(u1, 8), low4_mask);

    n148_sse2_hadamard4_epi16(tr0, tr1, tr2, tr3, &h0, &h1, &h2, &h3);

    sum32 = _mm_setzero_si128();
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(h0), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(h1), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(h2), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(h3), ones));

    return (n148_sse2_hsum_epi32(sum32) + 1) >> 1;
}

static inline int n148_sse2_satd_flat16(const uint8_t a[16], const uint8_t b[16])
{
    return n148_sse2_satd_4x4(a, 4, b, 4);
}


static inline int n148_sse2_intra_estimate_satd_flat16(const uint8_t src[16], const uint8_t pred[16])
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i ones = _mm_set1_epi16(1);
    const __m128i low4_mask = _mm_set_epi16(0, 0, 0, 0, -1, -1, -1, -1);
    __m128i r0, r1, r2, r3;
    __m128i v0, v1, v2, v3;
    __m128i t01, t23, u0, u1;
    __m128i tr0, tr1, tr2, tr3;
    __m128i h0, h1, h2, h3;
    __m128i sum32 = _mm_setzero_si128();
    uint32_t a = 0, b = 0;

    memcpy(&a, src + 0, sizeof(uint32_t));
    memcpy(&b, pred + 0, sizeof(uint32_t));
    r0 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)a), zero),
                       _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)b), zero));

    memcpy(&a, src + 4, sizeof(uint32_t));
    memcpy(&b, pred + 4, sizeof(uint32_t));
    r1 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)a), zero),
                       _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)b), zero));

    memcpy(&a, src + 8, sizeof(uint32_t));
    memcpy(&b, pred + 8, sizeof(uint32_t));
    r2 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)a), zero),
                       _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)b), zero));

    memcpy(&a, src + 12, sizeof(uint32_t));
    memcpy(&b, pred + 12, sizeof(uint32_t));
    r3 = _mm_sub_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128((int)a), zero),
                       _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)b), zero));

    n148_sse2_hadamard4_epi16(r0, r1, r2, r3, &v0, &v1, &v2, &v3);
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(v0), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(v1), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(v2), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(v3), ones));

    t01 = _mm_unpacklo_epi16(r0, r1);
    t23 = _mm_unpacklo_epi16(r2, r3);
    u0 = _mm_unpacklo_epi32(t01, t23);
    u1 = _mm_unpackhi_epi32(t01, t23);
    tr0 = _mm_and_si128(u0, low4_mask);
    tr1 = _mm_and_si128(_mm_srli_si128(u0, 8), low4_mask);
    tr2 = _mm_and_si128(u1, low4_mask);
    tr3 = _mm_and_si128(_mm_srli_si128(u1, 8), low4_mask);

    n148_sse2_hadamard4_epi16(tr0, tr1, tr2, tr3, &h0, &h1, &h2, &h3);
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(h0), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(h1), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(h2), ones));
    sum32 = _mm_add_epi32(sum32, _mm_madd_epi16(n148_sse2_abs_epi16(h3), ones));

    return n148_sse2_hsum_epi32(sum32) >> 1;
}

static inline int n148_sse2_sad_flat16(const uint8_t a[16], const uint8_t b[16])
{
    __m128i va = _mm_loadu_si128((const __m128i*)a);
    __m128i vb = _mm_loadu_si128((const __m128i*)b);
    return n148_sse2_hsum_sad(_mm_sad_epu8(va, vb));
}

static inline int n148_sse2_sad_4x4(const uint8_t* cur, int cur_stride,
                                     const uint8_t* ref, int ref_stride)
{
    __m128i sum = _mm_setzero_si128();
    int y;
    for (y = 0; y < 4; y++) {
        uint32_t ca = 0;
        uint32_t cb = 0;
        memcpy(&ca, cur + y * cur_stride, sizeof(uint32_t));
        memcpy(&cb, ref + y * ref_stride, sizeof(uint32_t));
        sum = _mm_add_epi64(sum,
                            _mm_sad_epu8(_mm_cvtsi32_si128((int)ca),
                                         _mm_cvtsi32_si128((int)cb)));
    }
    return n148_sse2_hsum_sad(sum);
}

static inline int n148_sse2_sad_8x8(const uint8_t* cur, int cur_stride,
                                     const uint8_t* ref, int ref_stride)
{
    __m128i sum = _mm_setzero_si128();
    int y;
    for (y = 0; y < 8; y++) {
        __m128i va = _mm_loadl_epi64((const __m128i*)(cur + y * cur_stride));
        __m128i vb = _mm_loadl_epi64((const __m128i*)(ref + y * ref_stride));
        sum = _mm_add_epi64(sum, _mm_sad_epu8(va, vb));
    }
    return n148_sse2_hsum_sad(sum);
}


static inline void n148_sse2_interp_block_4x4_qpel_luma_inbounds(uint8_t out[16],
                                                                  const uint8_t* plane,
                                                                  int stride,
                                                                  int x_base,
                                                                  int y_base,
                                                                  int fx,
                                                                  int fy)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i wx0 = _mm_set1_epi16((short)(4 - fx));
    const __m128i wx1 = _mm_set1_epi16((short)fx);
    const __m128i wy0 = _mm_set1_epi16((short)(4 - fy));
    const __m128i wy1 = _mm_set1_epi16((short)fy);
    const __m128i bias = _mm_set1_epi16(8);
    int y;

    for (y = 0; y < 4; y++) {
        const uint8_t* row0 = plane + (y_base + y) * stride + x_base;
        const uint8_t* row1 = row0 + stride;
        uint32_t r00 = 0, r01 = 0, r10 = 0, r11 = 0;
        __m128i vr00, vr01, vr10, vr11;
        __m128i top, bot, sum, packed;

        memcpy(&r00, row0, sizeof(uint32_t));
        memcpy(&r01, row0 + 1, sizeof(uint32_t));
        memcpy(&r10, row1, sizeof(uint32_t));
        memcpy(&r11, row1 + 1, sizeof(uint32_t));

        vr00 = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)r00), zero);
        vr01 = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)r01), zero);
        vr10 = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)r10), zero);
        vr11 = _mm_unpacklo_epi8(_mm_cvtsi32_si128((int)r11), zero);

        top = _mm_add_epi16(_mm_mullo_epi16(vr00, wx0), _mm_mullo_epi16(vr01, wx1));
        bot = _mm_add_epi16(_mm_mullo_epi16(vr10, wx0), _mm_mullo_epi16(vr11, wx1));
        sum = _mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(top, wy0), _mm_mullo_epi16(bot, wy1)), bias);
        sum = _mm_srli_epi16(sum, 4);
        packed = _mm_packus_epi16(sum, sum);
        memcpy(out + y * 4, &packed, sizeof(uint32_t));
    }
}

static inline int n148_sse2_sad_16x16(const uint8_t* cur, int cur_stride,
                                       const uint8_t* ref, int ref_stride)
{
    __m128i sum = _mm_setzero_si128();
    int y;
    for (y = 0; y < 16; y++) {
        __m128i va = _mm_loadu_si128((const __m128i*)(cur + y * cur_stride));
        __m128i vb = _mm_loadu_si128((const __m128i*)(ref + y * ref_stride));
        sum = _mm_add_epi64(sum, _mm_sad_epu8(va, vb));
    }
    return n148_sse2_hsum_sad(sum);
}

#else
#define N148_HAVE_SSE2 0
#endif

#endif
