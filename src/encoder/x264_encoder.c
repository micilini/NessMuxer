#include <stdio.h>

#include "encoder.h"
#include "../avc_utils.h"

#ifdef NESS_HAVE_X264

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <x264.h>

#ifdef _WIN32
typedef x264_t* (__cdecl *PFN_x264_encoder_open)(x264_param_t*);
typedef void    (__cdecl *PFN_x264_encoder_close)(x264_t*);
typedef int     (__cdecl *PFN_x264_encoder_encode)(x264_t*, x264_nal_t**, int*, x264_picture_t*, x264_picture_t*);
typedef int     (__cdecl *PFN_x264_encoder_headers)(x264_t*, x264_nal_t**, int*);
typedef int     (__cdecl *PFN_x264_encoder_delayed_frames)(x264_t*);
typedef int     (__cdecl *PFN_x264_param_default_preset)(x264_param_t*, const char*, const char*);
typedef int     (__cdecl *PFN_x264_param_apply_profile)(x264_param_t*, const char*);
typedef void    (__cdecl *PFN_x264_picture_init)(x264_picture_t*);
#endif

typedef struct
{
#ifdef _WIN32
    HMODULE dll;
    PFN_x264_encoder_open            p_x264_encoder_open;
    PFN_x264_encoder_close           p_x264_encoder_close;
    PFN_x264_encoder_encode          p_x264_encoder_encode;
    PFN_x264_encoder_headers         p_x264_encoder_headers;
    PFN_x264_encoder_delayed_frames  p_x264_encoder_delayed_frames;
    PFN_x264_param_default_preset    p_x264_param_default_preset;
    PFN_x264_param_apply_profile     p_x264_param_apply_profile;
    PFN_x264_picture_init            p_x264_picture_init;
#endif

    x264_t*       enc;
    x264_param_t  param;

    int           width;
    int           height;
    int           fps;
    int           bitrate_kbps;
    int64_t       frame_index;

    uint8_t*      codec_private;
    int           codec_private_size;
    int           codec_private_ready;

    uint8_t*      pending_packet;
    int           pending_packet_size;
    int64_t       pending_pts_hns;
    int64_t       pending_duration_hns;
    int           pending_keyframe;
    int           pending_ready;
} X264Encoder;

static void x264_clear_pending(X264Encoder* e)
{
    if (!e)
        return;

    if (e->pending_packet) {
        free(e->pending_packet);
        e->pending_packet = NULL;
    }

    e->pending_packet_size = 0;
    e->pending_pts_hns = 0;
    e->pending_duration_hns = 0;
    e->pending_keyframe = 0;
    e->pending_ready = 0;
}

#ifdef _WIN32
static FARPROC x264_get_symbol(HMODULE dll, const char* name)
{
    if (!dll || !name)
        return NULL;
    return GetProcAddress(dll, name);
}
#endif

#ifdef _WIN32
static int x264_load_dynamic_api(X264Encoder* e)
{
    static const char* dll_names[] = {
        "libx264.dll",
        "libx264-164.dll",
        "libx264-148.dll",
        "x264.dll"
    };
    static const char* open_names[] = {
        "x264_encoder_open_165",
        "x264_encoder_open_164",
        "x264_encoder_open_163",
        "x264_encoder_open_161",
        "x264_encoder_open_160",
        "x264_encoder_open_159",
        "x264_encoder_open_158",
        "x264_encoder_open_157",
        "x264_encoder_open_155",
        "x264_encoder_open_148",
        "x264_encoder_open"
    };

    size_t i;
    DWORD last_error = 0;

    if (!e)
        return -1;

    for (i = 0; i < sizeof(dll_names) / sizeof(dll_names[0]); i++) {
        printf("[x264] trying DLL: %s\n", dll_names[i]);
        e->dll = LoadLibraryA(dll_names[i]);
        if (e->dll) {
            printf("[x264] loaded DLL: %s\n", dll_names[i]);
            break;
        }

        last_error = GetLastError();
        printf("[x264] LoadLibrary failed for %s (GetLastError=%lu)\n",
               dll_names[i],
               (unsigned long)last_error);
    }

    if (!e->dll)
        return -1;

    e->p_x264_encoder_open = NULL;
    for (i = 0; i < sizeof(open_names) / sizeof(open_names[0]); i++) {
        e->p_x264_encoder_open = (PFN_x264_encoder_open)x264_get_symbol(e->dll, open_names[i]);
        if (e->p_x264_encoder_open) {
            printf("[x264] found open symbol: %s\n", open_names[i]);
            break;
        }
    }

    e->p_x264_encoder_close = (PFN_x264_encoder_close)x264_get_symbol(e->dll, "x264_encoder_close");
    e->p_x264_encoder_encode = (PFN_x264_encoder_encode)x264_get_symbol(e->dll, "x264_encoder_encode");
    e->p_x264_encoder_headers = (PFN_x264_encoder_headers)x264_get_symbol(e->dll, "x264_encoder_headers");
    e->p_x264_encoder_delayed_frames = (PFN_x264_encoder_delayed_frames)x264_get_symbol(e->dll, "x264_encoder_delayed_frames");
    e->p_x264_param_default_preset = (PFN_x264_param_default_preset)x264_get_symbol(e->dll, "x264_param_default_preset");
    e->p_x264_param_apply_profile = (PFN_x264_param_apply_profile)x264_get_symbol(e->dll, "x264_param_apply_profile");
    e->p_x264_picture_init = (PFN_x264_picture_init)x264_get_symbol(e->dll, "x264_picture_init");

    if (!e->p_x264_encoder_open)
        printf("[x264] missing symbol: x264_encoder_open_*\n");
    if (!e->p_x264_encoder_close)
        printf("[x264] missing symbol: x264_encoder_close\n");
    if (!e->p_x264_encoder_encode)
        printf("[x264] missing symbol: x264_encoder_encode\n");
    if (!e->p_x264_encoder_headers)
        printf("[x264] missing symbol: x264_encoder_headers\n");
    if (!e->p_x264_encoder_delayed_frames)
        printf("[x264] missing symbol: x264_encoder_delayed_frames\n");
    if (!e->p_x264_param_default_preset)
        printf("[x264] missing symbol: x264_param_default_preset\n");
    if (!e->p_x264_param_apply_profile)
        printf("[x264] missing symbol: x264_param_apply_profile\n");
    if (!e->p_x264_picture_init)
        printf("[x264] missing symbol: x264_picture_init\n");

    if (!e->p_x264_encoder_open ||
        !e->p_x264_encoder_close ||
        !e->p_x264_encoder_encode ||
        !e->p_x264_encoder_headers ||
        !e->p_x264_encoder_delayed_frames ||
        !e->p_x264_param_default_preset ||
        !e->p_x264_param_apply_profile ||
        !e->p_x264_picture_init) {
        FreeLibrary(e->dll);
        e->dll = NULL;
        return -1;
    }

    return 0;
}

static void x264_unload_dynamic_api(X264Encoder* e)
{
    if (!e)
        return;

    if (e->dll) {
        FreeLibrary(e->dll);
        e->dll = NULL;
    }

    e->p_x264_encoder_open = NULL;
    e->p_x264_encoder_close = NULL;
    e->p_x264_encoder_encode = NULL;
    e->p_x264_encoder_headers = NULL;
    e->p_x264_encoder_delayed_frames = NULL;
    e->p_x264_param_default_preset = NULL;
    e->p_x264_param_apply_profile = NULL;
    e->p_x264_picture_init = NULL;
}
#endif

static int x264_prepare_codec_private(X264Encoder* e)
{
    x264_nal_t* nals = NULL;
    int num_nals = 0;
    const uint8_t* sps = NULL;
    const uint8_t* pps = NULL;
    int sps_len = 0;
    int pps_len = 0;
    int total = 0;
    int i;
    uint8_t* annexb = NULL;
    int ret = -1;

    if (!e || !e->enc)
        return -1;

    if (e->codec_private_ready)
        return 0;

    #ifdef _WIN32
        if (e->p_x264_encoder_headers(e->enc, &nals, &num_nals) < 0)
    #else
        if (x264_encoder_headers(e->enc, &nals, &num_nals) < 0)
    #endif
        return -1;

    for (i = 0; i < num_nals; i++)
        total += nals[i].i_payload;

    if (total <= 0)
        return -1;

    annexb = (uint8_t*)malloc((size_t)total);
    if (!annexb)
        return -1;

    total = 0;
    for (i = 0; i < num_nals; i++) {
        memcpy(annexb + total, nals[i].p_payload, (size_t)nals[i].i_payload);
        total += nals[i].i_payload;
    }

    if (avc_extract_sps_pps(annexb, total, &sps, &sps_len, &pps, &pps_len) != 0)
        goto cleanup;

    if (avc_build_codec_private(sps, sps_len, pps, pps_len,
                                &e->codec_private, &e->codec_private_size) != 0)
        goto cleanup;

    e->codec_private_ready = 1;
    ret = 0;

cleanup:
    if (annexb)
        free(annexb);
    return ret;
}

static int x264_store_encoded_access_unit(X264Encoder* e, x264_nal_t* nals, int num_nals, x264_picture_t* pic_out)
{
    int total = 0;
    int i;
    uint8_t* copy;

    if (!e || !nals || num_nals <= 0 || !pic_out)
        return -1;

    x264_clear_pending(e);

    for (i = 0; i < num_nals; i++)
        total += nals[i].i_payload;

    if (total <= 0)
        return 0;

    copy = (uint8_t*)malloc((size_t)total);
    if (!copy)
        return -1;

    total = 0;
    for (i = 0; i < num_nals; i++) {
        memcpy(copy + total, nals[i].p_payload, (size_t)nals[i].i_payload);
        total += nals[i].i_payload;
    }

    e->pending_packet = copy;
    e->pending_packet_size = total;
    e->pending_pts_hns = (int64_t)pic_out->i_pts * (10000000LL / e->fps);
    e->pending_duration_hns = 10000000LL / e->fps;
    e->pending_keyframe = pic_out->b_keyframe ? 1 : 0;
    e->pending_ready = 1;

    return 0;
}

static int x264_create(void** ctx, int width, int height, int fps, int bitrate_kbps)
{
    X264Encoder* e;

    if (!ctx || width <= 0 || height <= 0 || fps <= 0 || bitrate_kbps <= 0)
        return -1;
    if ((width % 2) != 0 || (height % 2) != 0)
        return -1;

    e = (X264Encoder*)calloc(1, sizeof(X264Encoder));
    if (!e)
        return -1;

    e->width = width;
    e->height = height;
    e->fps = fps;
    e->bitrate_kbps = bitrate_kbps;

    printf("[x264] create: width=%d height=%d fps=%d bitrate=%d\n",
        width, height, fps, bitrate_kbps);

    #ifdef _WIN32
    printf("[x264] loading dynamic API...\n");

    if (x264_load_dynamic_api(e) != 0) {
        printf("[x264] ERROR: failed to load dynamic API\n");
        free(e);
        return -1;
    }

    printf("[x264] dynamic API loaded\n");
    #endif

    printf("[x264] applying preset/profile...\n");

    #ifdef _WIN32
        e->p_x264_param_default_preset(&e->param, "veryfast", "zerolatency");
    #else
        x264_param_default_preset(&e->param, "veryfast", "zerolatency");
    #endif
    e->param.i_csp = X264_CSP_NV12;
    e->param.i_width = width;
    e->param.i_height = height;
    e->param.i_fps_num = fps;
    e->param.i_fps_den = 1;
    e->param.i_keyint_max = fps * 2;
    e->param.i_keyint_min = fps;
    e->param.b_intra_refresh = 0;
    e->param.rc.i_rc_method = X264_RC_ABR;
    e->param.rc.i_bitrate = bitrate_kbps;
    e->param.b_repeat_headers = 0;
    e->param.b_annexb = 1;
    e->param.i_threads = 1;
    e->param.b_vfr_input = 0;
    e->param.i_log_level = X264_LOG_NONE;

    #ifdef _WIN32
        e->p_x264_param_apply_profile(&e->param, "baseline");
    #else
        x264_param_apply_profile(&e->param, "baseline");
    #endif

    printf("[x264] opening encoder...\n");

    #ifdef _WIN32
        e->enc = e->p_x264_encoder_open(&e->param);
    #else
        e->enc = x264_encoder_open(&e->param);
    #endif
        if (!e->enc) {
            printf("[x264] ERROR: x264_encoder_open returned NULL\n");
    #ifdef _WIN32
            x264_unload_dynamic_api(e);
    #endif
            free(e);
            return -1;
        }

    printf("[x264] encoder opened successfully\n");

    printf("[x264] preparing codec private...\n");

    if (x264_prepare_codec_private(e) != 0) {
        printf("[x264] ERROR: failed to prepare codec private\n");
        #ifdef _WIN32
                e->p_x264_encoder_close(e->enc);
        #else
                x264_encoder_close(e->enc);
        #endif
                e->enc = NULL;
        #ifdef _WIN32
                x264_unload_dynamic_api(e);
        #endif
                free(e);
                return -1;
    }

    printf("[x264] codec private ready (%d bytes)\n", e->codec_private_size);

    *ctx = e;
    return 0;
}

static int x264_submit_frame(void* ctx, const uint8_t* nv12, int nv12_size)
{
    X264Encoder* e = (X264Encoder*)ctx;
    int expected_size;
    x264_picture_t pic_in;
    x264_picture_t pic_out;
    x264_nal_t* nals = NULL;
    int num_nals = 0;
    int frame_size;

    if (!e || !e->enc || !nv12)
        return -1;

    if (e->frame_index == 0) {
        printf("[x264] submit first frame: nv12_size=%d expected=%d\n",
               nv12_size,
               e->width * e->height * 3 / 2);
    }

    expected_size = e->width * e->height * 3 / 2;
    if (nv12_size != expected_size)
        return -1;

    if (e->pending_ready)
        return -1;

    #ifdef _WIN32
        e->p_x264_picture_init(&pic_in);
    #else
        x264_picture_init(&pic_in);
    #endif
    memset(&pic_out, 0, sizeof(pic_out));

    pic_in.img.i_csp = X264_CSP_NV12;
    pic_in.img.i_plane = 2;
    pic_in.img.plane[0] = (uint8_t*)nv12;
    pic_in.img.plane[1] = (uint8_t*)(nv12 + (e->width * e->height));
    pic_in.img.i_stride[0] = e->width;
    pic_in.img.i_stride[1] = e->width;
    pic_in.i_pts = e->frame_index++;

    if (e->frame_index == 1)
        printf("[x264] calling x264_encoder_encode on first frame...\n");

    #ifdef _WIN32
        frame_size = e->p_x264_encoder_encode(e->enc, &nals, &num_nals, &pic_in, &pic_out);
    #else
        frame_size = x264_encoder_encode(e->enc, &nals, &num_nals, &pic_in, &pic_out);
    #endif
    if (frame_size < 0) {
        printf("[x264] ERROR: x264_encoder_encode returned %d on frame %lld\n",
               frame_size, (long long)(e->frame_index - 1));
        return -1;
    }

    if (frame_size == 0 || num_nals <= 0)
        return 0;

    if (e->frame_index == 1) {
        printf("[x264] first frame encoded: frame_size=%d num_nals=%d keyframe=%d pts=%lld\n",
               frame_size,
               num_nals,
               pic_out.b_keyframe ? 1 : 0,
               (long long)pic_out.i_pts);
    }

    return x264_store_encoded_access_unit(e, nals, num_nals, &pic_out);
}

static int x264_receive_packets(void* ctx, ness_packet_callback cb, void* userdata)
{
    X264Encoder* e = (X264Encoder*)ctx;
    NessEncodedPacket pkt;
    int ret;

    if (!e || !cb)
        return -1;

    if (!e->pending_ready)
        return 0;

    pkt.data = e->pending_packet;
    pkt.size = e->pending_packet_size;
    pkt.pts_hns = e->pending_pts_hns;
    pkt.duration_hns = e->pending_duration_hns;
    pkt.is_keyframe = e->pending_keyframe;

    ret = cb(userdata, &pkt);
    x264_clear_pending(e);

    return ret;
}

static int x264_drain(void* ctx, ness_packet_callback cb, void* userdata)
{
    X264Encoder* e = (X264Encoder*)ctx;

    if (!e || !cb)
        return -1;

    #ifdef _WIN32
        while (e->p_x264_encoder_delayed_frames(e->enc) > 0) {
    #else
        while (x264_encoder_delayed_frames(e->enc) > 0) {
    #endif
        x264_picture_t pic_out;
        x264_nal_t* nals = NULL;
        int num_nals = 0;
        int frame_size;

        memset(&pic_out, 0, sizeof(pic_out));

        #ifdef _WIN32
                frame_size = e->p_x264_encoder_encode(e->enc, &nals, &num_nals, NULL, &pic_out);
        #else
                frame_size = x264_encoder_encode(e->enc, &nals, &num_nals, NULL, &pic_out);
        #endif
        if (frame_size < 0)
            return -1;

        if (frame_size == 0 || num_nals <= 0)
            continue;

        if (x264_store_encoded_access_unit(e, nals, num_nals, &pic_out) != 0)
            return -1;

        if (x264_receive_packets(e, cb, userdata) != 0)
            return -1;
    }

    return 0;
}

static int x264_get_codec_private(void* ctx, uint8_t** out, int* out_size)
{
    X264Encoder* e = (X264Encoder*)ctx;

    if (!e || !out || !out_size || !e->codec_private_ready)
        return -1;

    *out = e->codec_private;
    *out_size = e->codec_private_size;
    return 0;
}

static void x264_destroy(void* ctx)
{
    X264Encoder* e = (X264Encoder*)ctx;
    if (!e)
        return;

    x264_clear_pending(e);

    if (e->enc) {
    #ifdef _WIN32
        e->p_x264_encoder_close(e->enc);
    #else
        x264_encoder_close(e->enc);
    #endif
    }

    if (e->codec_private)
        free(e->codec_private);

    #ifdef _WIN32
        x264_unload_dynamic_api(e);
    #endif
    free(e);
}

const NessEncoderVtable g_x264_encoder_vtable = {
    "x264",
    "V_MPEG4/ISO/AVC",
    1,
    x264_create,
    x264_submit_frame,
    x264_receive_packets,
    x264_drain,
    x264_get_codec_private,
    x264_destroy
};

#else

const NessEncoderVtable g_x264_encoder_vtable = {
    "x264",
    "V_MPEG4/ISO/AVC",
    1,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif