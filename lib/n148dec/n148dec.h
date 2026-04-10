#ifndef N148DEC_H
#define N148DEC_H

#include <stdint.h>

#ifdef _WIN32
  #ifdef N148DEC_EXPORTS
    #define N148DEC_API __declspec(dllexport)
  #else
    #define N148DEC_API __declspec(dllimport)
  #endif
#else
  #define N148DEC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct N148DecHandle N148DecHandle;

enum {
    N148DEC_PIXFMT_UNKNOWN = 0,
    N148DEC_PIXFMT_NV12    = 1
};

typedef struct N148DecOutput {
    const uint8_t* planes[3];
    int            strides[3];
    int            width;
    int            height;
    int64_t        pts;
    int            frame_type;
    int            pixel_format;
} N148DecOutput;

N148DEC_API int n148dec_create(N148DecHandle** handle);

N148DEC_API int n148dec_init(N148DecHandle* handle,
                             const uint8_t* codec_private,
                             int cp_size);


N148DEC_API int n148dec_decode_frame(N148DecHandle* handle,
                                     const uint8_t* bitstream,
                                     int bs_size,
                                     uint8_t** out_yuv,
                                     int* out_width,
                                     int* out_height);


N148DEC_API int n148dec_decode_frame_ex(N148DecHandle* handle,
                                        const uint8_t* bitstream,
                                        int bs_size,
                                        N148DecOutput* out_frame);

N148DEC_API void n148dec_destroy(N148DecHandle* handle);

#ifdef __cplusplus
}
#endif

#endif