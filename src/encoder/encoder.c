#include "encoder.h"
#include <stddef.h>

#ifdef _WIN32
extern const NessEncoderVtable g_mf_encoder_vtable;
#endif

#ifdef NESS_HAVE_X264
extern const NessEncoderVtable g_x264_encoder_vtable;
#endif

#ifdef NESS_HAVE_NVENC
extern const NessEncoderVtable g_nvenc_encoder_vtable;
#endif

#ifdef NESS_HAVE_VIDEOTOOLBOX
extern const NessEncoderVtable g_vtbox_encoder_vtable;
#endif

#ifdef NESS_HAVE_V4L2
extern const NessEncoderVtable g_v4l2_encoder_vtable;
#endif

#ifdef NESS_HAVE_N148
extern const NessEncoderVtable g_n148_encoder_vtable;
#endif

static int encoder_probe(const NessEncoderVtable* vt)
{
    void* ctx = NULL;
    if (!vt || !vt->create || !vt->destroy)
        return 0;
    if (vt->create(&ctx, 160, 120, 30, 500) != 0)
        return 0;
    vt->destroy(ctx);
    return 1;
}



const NessEncoderVtable* ness_encoder_get(NessEncoderType type)
{
    switch (type) {
        case NESS_ENCODER_AUTO:
            return ness_encoder_get_best();

        case NESS_ENCODER_MEDIA_FOUNDATION:
#ifdef _WIN32
            return &g_mf_encoder_vtable;
#else
            return NULL;
#endif

        case NESS_ENCODER_X264:
#ifdef NESS_HAVE_X264
            return &g_x264_encoder_vtable;
#else
            return NULL;
#endif

        case NESS_ENCODER_NVENC:
#ifdef NESS_HAVE_NVENC
            return &g_nvenc_encoder_vtable;
#else
            return NULL;
#endif

        case NESS_ENCODER_VIDEOTOOLBOX:
#ifdef NESS_HAVE_VIDEOTOOLBOX
            return &g_vtbox_encoder_vtable;
#else
            return NULL;
#endif

case NESS_ENCODER_V4L2:
#ifdef NESS_HAVE_V4L2
            return &g_v4l2_encoder_vtable;
#else
            return NULL;
#endif

        case NESS_ENCODER_N148:
#ifdef NESS_HAVE_N148
            return &g_n148_encoder_vtable;
#else
            return NULL;
#endif

        default:
            return NULL;
    }
}


const NessEncoderVtable* ness_encoder_get_best(void)
{
    const NessEncoderVtable* candidates[] = {
#ifdef NESS_HAVE_NVENC
        &g_nvenc_encoder_vtable,
#endif
#ifdef NESS_HAVE_VIDEOTOOLBOX
        &g_vtbox_encoder_vtable,
#endif
#ifdef _WIN32
        &g_mf_encoder_vtable,
#endif
#ifdef NESS_HAVE_V4L2
        &g_v4l2_encoder_vtable,
#endif
#ifdef NESS_HAVE_X264
        &g_x264_encoder_vtable,
#endif
        NULL
    };

    int i;
    for (i = 0; candidates[i] != NULL; i++) {
        if (encoder_probe(candidates[i]))
            return candidates[i];
    }

    return NULL;
}