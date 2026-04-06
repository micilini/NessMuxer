#include "encoder.h"
#include "../avc_utils.h"

#ifdef NESS_HAVE_NVENC

#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nvEncodeAPI.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

typedef NVENCSTATUS (NVENCAPI *PFN_NvEncodeAPICreateInstance)(NV_ENCODE_API_FUNCTION_LIST*);

typedef struct
{
    HMODULE dll;
    PFN_NvEncodeAPICreateInstance pNvEncodeAPICreateInstance;
    NV_ENCODE_API_FUNCTION_LIST api;

    ID3D11Device* device;
    ID3D11DeviceContext* context;
    void* encoder;

    int width;
    int height;
    int fps;
    int bitrate_kbps;
    int64_t frame_index;

    NV_ENC_BUFFER_FORMAT buffer_format;
    GUID codec_guid;
    GUID preset_guid;

    NV_ENC_INPUT_PTR  input_buffer;
    NV_ENC_OUTPUT_PTR output_buffer;

    uint8_t* codec_private;
    int      codec_private_size;
    int      codec_private_ready;

    int      last_submit_had_output;
    int64_t  last_submit_pts_hns;

    uint8_t* pending_packet;
    int      pending_packet_size;
    int64_t  pending_pts_hns;
    int64_t  pending_duration_hns;
    int      pending_keyframe;
    int      pending_ready;
} NvencEncoder;

static void nvenc_clear_pending(NvencEncoder* e)
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

static NVENCSTATUS nvenc_fail(const char* where, NVENCSTATUS st)
{
    printf("[nvenc] ERROR: %s failed (status=%d)\n", where, (int)st);
    return st;
}

static FARPROC nvenc_get_symbol(HMODULE dll, const char* name)
{
    if (!dll || !name)
        return NULL;
    return GetProcAddress(dll, name);
}

static int nvenc_load_api(NvencEncoder* e)
{
    if (!e)
        return -1;

    e->dll = LoadLibraryA("nvEncodeAPI64.dll");
    if (!e->dll) {
        printf("[nvenc] ERROR: failed to load nvEncodeAPI64.dll (GetLastError=%lu)\n",
               (unsigned long)GetLastError());
        return -1;
    }

    e->pNvEncodeAPICreateInstance =
        (PFN_NvEncodeAPICreateInstance)nvenc_get_symbol(e->dll, "NvEncodeAPICreateInstance");

    if (!e->pNvEncodeAPICreateInstance) {
        printf("[nvenc] ERROR: NvEncodeAPICreateInstance not found\n");
        FreeLibrary(e->dll);
        e->dll = NULL;
        return -1;
    }

    memset(&e->api, 0, sizeof(e->api));
    e->api.version = NV_ENCODE_API_FUNCTION_LIST_VER;

    if (e->pNvEncodeAPICreateInstance(&e->api) != NV_ENC_SUCCESS) {
        printf("[nvenc] ERROR: NvEncodeAPICreateInstance call failed\n");
        FreeLibrary(e->dll);
        e->dll = NULL;
        return -1;
    }

    return 0;
}

static void nvenc_unload_api(NvencEncoder* e)
{
    if (!e)
        return;

    if (e->dll) {
        FreeLibrary(e->dll);
        e->dll = NULL;
    }

    memset(&e->api, 0, sizeof(e->api));
    e->pNvEncodeAPICreateInstance = NULL;
}

static int nvenc_create_d3d11_device(NvencEncoder* e)
{
    D3D_FEATURE_LEVEL fl_out = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    HRESULT hr;

    if (!e)
        return -1;

    hr = D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        levels,
        (UINT)(sizeof(levels) / sizeof(levels[0])),
        D3D11_SDK_VERSION,
        &e->device,
        &fl_out,
        &e->context
    );

    if (FAILED(hr)) {
        printf("[nvenc] ERROR: D3D11CreateDevice failed (hr=0x%08X)\n", (unsigned int)hr);
        return -1;
    }

    return 0;
}

static int nvenc_open_session(NvencEncoder* e)
{
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params;
    NVENCSTATUS st;

    memset(&params, 0, sizeof(params));
    params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.device = e->device;
    params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    params.apiVersion = NVENCAPI_VERSION;

    st = e->api.nvEncOpenEncodeSessionEx(&params, &e->encoder);
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncOpenEncodeSessionEx", st);
        return -1;
    }

    return 0;
}

static int nvenc_create_io_buffers(NvencEncoder* e)
{
    NV_ENC_CREATE_INPUT_BUFFER inbuf;
    NV_ENC_CREATE_BITSTREAM_BUFFER outbuf;
    NVENCSTATUS st;

    memset(&inbuf, 0, sizeof(inbuf));
    inbuf.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    inbuf.width = e->width;
    inbuf.height = e->height;
    inbuf.memoryHeap = NV_ENC_MEMORY_HEAP_AUTOSELECT;
    inbuf.bufferFmt = e->buffer_format;

    st = e->api.nvEncCreateInputBuffer(e->encoder, &inbuf);
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncCreateInputBuffer", st);
        return -1;
    }

    e->input_buffer = inbuf.inputBuffer;

    memset(&outbuf, 0, sizeof(outbuf));
    outbuf.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    outbuf.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;

    st = e->api.nvEncCreateBitstreamBuffer(e->encoder, &outbuf);
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncCreateBitstreamBuffer", st);
        return -1;
    }

    e->output_buffer = outbuf.bitstreamBuffer;
    return 0;
}

static int nvenc_prepare_codec_private(NvencEncoder* e)
{
    NV_ENC_SEQUENCE_PARAM_PAYLOAD seq;
    uint8_t seq_buf[4096];
    uint32_t payload_size = 0;
    const uint8_t* sps = NULL;
    const uint8_t* pps = NULL;
    int sps_len = 0;
    int pps_len = 0;
    NVENCSTATUS st;

    memset(&seq, 0, sizeof(seq));
    seq.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
    seq.inBufferSize = (uint32_t)sizeof(seq_buf);
    seq.spsppsBuffer = seq_buf;
    seq.outSPSPPSPayloadSize = &payload_size;

    st = e->api.nvEncGetSequenceParams(e->encoder, &seq);
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncGetSequenceParams", st);
        return -1;
    }

    if (payload_size == 0 || payload_size > (uint32_t)sizeof(seq_buf)) {
        printf("[nvenc] ERROR: invalid SPS/PPS payload size: %u\n", (unsigned int)payload_size);
        return -1;
    }

    if (avc_extract_sps_pps(seq_buf, (int)payload_size, &sps, &sps_len, &pps, &pps_len) != 0) {
        printf("[nvenc] ERROR: failed to extract SPS/PPS from sequence params\n");
        return -1;
    }

    if (avc_build_codec_private(sps, sps_len, pps, pps_len,
                                &e->codec_private, &e->codec_private_size) != 0) {
        printf("[nvenc] ERROR: failed to build codec private\n");
        return -1;
    }

    e->codec_private_ready = 1;
    return 0;
}

static int nvenc_initialize_encoder(NvencEncoder* e)
{
    NV_ENC_INITIALIZE_PARAMS init_params;
    NV_ENC_PRESET_CONFIG preset_config;
    NV_ENC_CONFIG* config;
    NVENCSTATUS st;

    memset(&preset_config, 0, sizeof(preset_config));
    preset_config.version = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

    st = e->api.nvEncGetEncodePresetConfigEx(
        e->encoder,
        e->codec_guid,
        e->preset_guid,
        NV_ENC_TUNING_INFO_HIGH_QUALITY,
        &preset_config
    );
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncGetEncodePresetConfigEx", st);
        return -1;
    }

    config = &preset_config.presetCfg;

    config->profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
    config->gopLength = e->fps * 2;
    config->frameIntervalP = 1;
    config->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    config->rcParams.averageBitRate = (uint32_t)(e->bitrate_kbps * 1000);
    config->rcParams.maxBitRate = (uint32_t)(e->bitrate_kbps * 1000);
    config->encodeCodecConfig.h264Config.idrPeriod = config->gopLength;
    config->encodeCodecConfig.h264Config.repeatSPSPPS = 0;

    memset(&init_params, 0, sizeof(init_params));
    init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
    init_params.encodeGUID = e->codec_guid;
    init_params.presetGUID = e->preset_guid;
    init_params.tuningInfo = NV_ENC_TUNING_INFO_HIGH_QUALITY;
    init_params.encodeWidth = (uint32_t)e->width;
    init_params.encodeHeight = (uint32_t)e->height;
    init_params.darWidth = (uint32_t)e->width;
    init_params.darHeight = (uint32_t)e->height;
    init_params.maxEncodeWidth = (uint32_t)e->width;
    init_params.maxEncodeHeight = (uint32_t)e->height;
    init_params.frameRateNum = (uint32_t)e->fps;
    init_params.frameRateDen = 1;
    init_params.enablePTD = 1;
    init_params.reportSliceOffsets = 0;
    init_params.enableSubFrameWrite = 0;
    init_params.enableEncodeAsync = 0;
    init_params.enableWeightedPrediction = 0;
    init_params.encodeConfig = config;

    st = e->api.nvEncInitializeEncoder(e->encoder, &init_params);
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncInitializeEncoder", st);
        return -1;
    }

    return 0;
}

static int nvenc_fetch_bitstream(NvencEncoder* e, int64_t pts_hns, ness_packet_callback cb, void* userdata)
{
    NV_ENC_LOCK_BITSTREAM lock_bs;
    NVENCSTATUS st;
    NessEncodedPacket pkt;
    int is_key = 0;

    memset(&lock_bs, 0, sizeof(lock_bs));
    lock_bs.version = NV_ENC_LOCK_BITSTREAM_VER;
    lock_bs.outputBitstream = e->output_buffer;
    lock_bs.doNotWait = 0;

    st = e->api.nvEncLockBitstream(e->encoder, &lock_bs);
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncLockBitstream", st);
        return -1;
    }

    nvenc_clear_pending(e);

    e->pending_packet = (uint8_t*)malloc((size_t)lock_bs.bitstreamSizeInBytes);
    if (!e->pending_packet) {
        e->api.nvEncUnlockBitstream(e->encoder, e->output_buffer);
        return -1;
    }

    memcpy(e->pending_packet, lock_bs.bitstreamBufferPtr, (size_t)lock_bs.bitstreamSizeInBytes);
    e->pending_packet_size = (int)lock_bs.bitstreamSizeInBytes;
    e->pending_pts_hns = pts_hns;
    e->pending_duration_hns = 10000000LL / e->fps;

    is_key = (lock_bs.pictureType == NV_ENC_PIC_TYPE_IDR ||
              lock_bs.pictureType == NV_ENC_PIC_TYPE_I);
    e->pending_keyframe = is_key ? 1 : 0;
    e->pending_ready = 1;

    e->api.nvEncUnlockBitstream(e->encoder, e->output_buffer);

    pkt.data = e->pending_packet;
    pkt.size = e->pending_packet_size;
    pkt.pts_hns = e->pending_pts_hns;
    pkt.duration_hns = e->pending_duration_hns;
    pkt.is_keyframe = e->pending_keyframe;

    if (cb(userdata, &pkt) != 0) {
        nvenc_clear_pending(e);
        return -1;
    }

    nvenc_clear_pending(e);
    return 0;
}

static int nvenc_create(void** ctx, int width, int height, int fps, int bitrate_kbps)
{
    NvencEncoder* e;

    if (!ctx || width <= 0 || height <= 0 || fps <= 0 || bitrate_kbps <= 0)
        return -1;
    if ((width % 2) != 0 || (height % 2) != 0)
        return -1;

    e = (NvencEncoder*)calloc(1, sizeof(NvencEncoder));
    if (!e)
        return -1;

    e->width = width;
    e->height = height;
    e->fps = fps;
    e->bitrate_kbps = bitrate_kbps;
    e->buffer_format = NV_ENC_BUFFER_FORMAT_NV12;
    e->codec_guid = NV_ENC_CODEC_H264_GUID;
    e->preset_guid = NV_ENC_PRESET_P4_GUID;

    printf("[nvenc] create: width=%d height=%d fps=%d bitrate=%d\n",
           width, height, fps, bitrate_kbps);

    if (nvenc_load_api(e) != 0)
        goto fail;
    if (nvenc_create_d3d11_device(e) != 0)
        goto fail;
    if (nvenc_open_session(e) != 0)
        goto fail;
    if (nvenc_initialize_encoder(e) != 0)
        goto fail;
    if (nvenc_create_io_buffers(e) != 0)
        goto fail;
    if (nvenc_prepare_codec_private(e) != 0)
        goto fail;

    *ctx = e;
    return 0;

fail:
    if (e) {
        if (e->output_buffer && e->encoder && e->api.nvEncDestroyBitstreamBuffer)
            e->api.nvEncDestroyBitstreamBuffer(e->encoder, e->output_buffer);
        if (e->input_buffer && e->encoder && e->api.nvEncDestroyInputBuffer)
            e->api.nvEncDestroyInputBuffer(e->encoder, e->input_buffer);
        if (e->encoder && e->api.nvEncDestroyEncoder)
            e->api.nvEncDestroyEncoder(e->encoder);
        if (e->context)
            e->context->lpVtbl->Release(e->context);
        if (e->device)
            e->device->lpVtbl->Release(e->device);
        if (e->codec_private)
            free(e->codec_private);
        nvenc_unload_api(e);
        free(e);
    }
    return -1;
}

static int nvenc_submit_frame(void* ctx, const uint8_t* nv12, int nv12_size)
{
    NvencEncoder* e = (NvencEncoder*)ctx;
    int expected_size;
    NV_ENC_LOCK_INPUT_BUFFER lock_in;
    NV_ENC_PIC_PARAMS pic;
    NVENCSTATUS st;
    uint8_t* dst_y;
    uint8_t* dst_uv;
    const uint8_t* src_y;
    const uint8_t* src_uv;
    int y;

    if (!e || !e->encoder || !nv12)
        return -1;

    expected_size = e->width * e->height * 3 / 2;
    if (nv12_size != expected_size)
        return -1;

    e->last_submit_had_output = 0;
    e->last_submit_pts_hns = e->frame_index * (10000000LL / e->fps);

    memset(&lock_in, 0, sizeof(lock_in));
    lock_in.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lock_in.inputBuffer = e->input_buffer;

    st = e->api.nvEncLockInputBuffer(e->encoder, &lock_in);
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncLockInputBuffer", st);
        return -1;
    }

    src_y = nv12;
    src_uv = nv12 + (e->width * e->height);
    dst_y = (uint8_t*)lock_in.bufferDataPtr;
    dst_uv = dst_y + (lock_in.pitch * e->height);

    for (y = 0; y < e->height; y++)
        memcpy(dst_y + y * lock_in.pitch, src_y + y * e->width, (size_t)e->width);

    for (y = 0; y < e->height / 2; y++)
        memcpy(dst_uv + y * lock_in.pitch, src_uv + y * e->width, (size_t)e->width);

    st = e->api.nvEncUnlockInputBuffer(e->encoder, e->input_buffer);
    if (st != NV_ENC_SUCCESS) {
        nvenc_fail("nvEncUnlockInputBuffer", st);
        return -1;
    }

    memset(&pic, 0, sizeof(pic));
    pic.version = NV_ENC_PIC_PARAMS_VER;
    pic.inputBuffer = e->input_buffer;
    pic.bufferFmt = e->buffer_format;
    pic.inputWidth = (uint32_t)e->width;
    pic.inputHeight = (uint32_t)e->height;
    pic.outputBitstream = e->output_buffer;
    pic.inputTimeStamp = (uint64_t)e->frame_index;
    pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

    st = e->api.nvEncEncodePicture(e->encoder, &pic);
    if (st == NV_ENC_SUCCESS) {
        e->last_submit_had_output = 1;
    } else if (st == NV_ENC_ERR_NEED_MORE_INPUT) {
        e->last_submit_had_output = 0;
    } else {
        nvenc_fail("nvEncEncodePicture", st);
        return -1;
    }

    e->frame_index++;
    return 0;
}

static int nvenc_receive_packets(void* ctx, ness_packet_callback cb, void* userdata)
{
    NvencEncoder* e = (NvencEncoder*)ctx;

    if (!e || !cb)
        return -1;

    if (!e->last_submit_had_output)
        return 0;

    e->last_submit_had_output = 0;
    return nvenc_fetch_bitstream(e, e->last_submit_pts_hns, cb, userdata);
}

static int nvenc_drain(void* ctx, ness_packet_callback cb, void* userdata)
{
    NvencEncoder* e = (NvencEncoder*)ctx;
    NV_ENC_PIC_PARAMS pic;
    NVENCSTATUS st;

    (void)cb;
    (void)userdata;

    if (!e)
        return -1;

    memset(&pic, 0, sizeof(pic));
    pic.version = NV_ENC_PIC_PARAMS_VER;
    pic.encodePicFlags = NV_ENC_PIC_FLAG_EOS;

    st = e->api.nvEncEncodePicture(e->encoder, &pic);
    if (st != NV_ENC_SUCCESS && st != NV_ENC_ERR_NEED_MORE_INPUT) {
        nvenc_fail("nvEncEncodePicture(EOS)", st);
        return -1;
    }

    return 0;
}

static int nvenc_get_codec_private(void* ctx, uint8_t** out, int* out_size)
{
    NvencEncoder* e = (NvencEncoder*)ctx;

    if (!e || !out || !out_size || !e->codec_private_ready)
        return -1;

    *out = e->codec_private;
    *out_size = e->codec_private_size;
    return 0;
}

static void nvenc_destroy(void* ctx)
{
    NvencEncoder* e = (NvencEncoder*)ctx;
    if (!e)
        return;

    nvenc_clear_pending(e);

    if (e->output_buffer && e->encoder && e->api.nvEncDestroyBitstreamBuffer)
        e->api.nvEncDestroyBitstreamBuffer(e->encoder, e->output_buffer);
    if (e->input_buffer && e->encoder && e->api.nvEncDestroyInputBuffer)
        e->api.nvEncDestroyInputBuffer(e->encoder, e->input_buffer);
    if (e->encoder && e->api.nvEncDestroyEncoder)
        e->api.nvEncDestroyEncoder(e->encoder);

    if (e->context)
        e->context->lpVtbl->Release(e->context);
    if (e->device)
        e->device->lpVtbl->Release(e->device);

    if (e->codec_private)
        free(e->codec_private);

    nvenc_unload_api(e);
    free(e);
}

const NessEncoderVtable g_nvenc_encoder_vtable = {
    "NVENC",
    nvenc_create,
    nvenc_submit_frame,
    nvenc_receive_packets,
    nvenc_drain,
    nvenc_get_codec_private,
    nvenc_destroy
};

#else

const NessEncoderVtable g_nvenc_encoder_vtable = {
    "NVENC",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif