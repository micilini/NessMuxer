
#ifndef NESS_MUXER_H
#define NESS_MUXER_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
  #ifdef NESSMUXER_EXPORTS
    #define NESS_API __declspec(dllexport)
  #else
    #define NESS_API __declspec(dllimport)
  #endif
#else
  #define NESS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NessMuxer NessMuxer;

typedef enum {
    NESS_ENCODER_AUTO = 0,
    NESS_ENCODER_MEDIA_FOUNDATION = 1,
    NESS_ENCODER_X264 = 2,
    NESS_ENCODER_NVENC = 3,
    NESS_ENCODER_VIDEOTOOLBOX = 4,
    NESS_ENCODER_V4L2 = 5
} NessEncoderType;

typedef struct {
    const char* output_path;    
    int         width;          
    int         height;         
    int         fps;            
    int         bitrate_kbps;   
    int         encoder_type;   
} NessMuxerConfig;


#define NESS_OK              0
#define NESS_ERROR          -1
#define NESS_ERROR_IO       -2
#define NESS_ERROR_PARAM    -3
#define NESS_ERROR_STATE    -4
#define NESS_ERROR_ENCODER  -5
#define NESS_ERROR_ALLOC    -6


NESS_API int ness_muxer_open(NessMuxer** out_muxer, const NessMuxerConfig* config);


NESS_API int ness_muxer_write_frame(NessMuxer* muxer,
                                     const uint8_t* nv12_data,
                                     int nv12_size);


NESS_API int ness_muxer_close(NessMuxer* muxer);


NESS_API const char* ness_muxer_error(const NessMuxer* muxer);
NESS_API int64_t     ness_muxer_frame_count(const NessMuxer* muxer);
NESS_API int64_t     ness_muxer_encoded_count(const NessMuxer* muxer);

#ifdef __cplusplus
}
#endif

#endif
