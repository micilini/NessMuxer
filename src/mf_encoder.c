


#define COBJMACROS
#define WIN32_LEAN_AND_MEAN


#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#include "mf_encoder.h"
#include "avc_utils.h"

#include <windows.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mfidl.h>
#include <mferror.h>
#include <codecapi.h>
#include <initguid.h>
#include <wmcodecdsp.h>   

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


DEFINE_GUID(LOCAL_IID_ICodecAPI, 0x901db4c7, 0x31ce, 0x41a2, 0x85,0xdc,0x8f,0xa0,0xbf,0x41,0xb8,0xda);




#ifndef MF_MT_MAJOR_TYPE
DEFINE_GUID(MF_MT_MAJOR_TYPE,       0x48eba18e, 0xf8c9, 0x4687, 0xbf,0x11,0x0a,0x74,0xc9,0xf9,0x6a,0x8f);
#endif
#ifndef MF_MT_SUBTYPE
DEFINE_GUID(MF_MT_SUBTYPE,          0xf7e34c9a, 0x42e8, 0x4714, 0xb7,0x4b,0xcb,0x29,0xd7,0x2c,0x35,0xe5);
#endif
#ifndef MF_MT_AVG_BITRATE
DEFINE_GUID(MF_MT_AVG_BITRATE,      0x20332624, 0xfb0d, 0x4d9e, 0xbd,0x0d,0xcb,0xf6,0x78,0x6c,0x10,0x2e);
#endif
#ifndef MF_MT_INTERLACE_MODE
DEFINE_GUID(MF_MT_INTERLACE_MODE,   0xe2724bb8, 0xe676, 0x4806, 0xb4,0xb2,0xa8,0xd6,0xef,0xb4,0x4c,0xcd);
#endif
#ifndef MF_MT_FRAME_SIZE
DEFINE_GUID(MF_MT_FRAME_SIZE,       0x1652c33d, 0xd6b2, 0x4012, 0xb8,0x34,0x72,0x03,0x08,0x49,0xa3,0x7d);
#endif
#ifndef MF_MT_FRAME_RATE
DEFINE_GUID(MF_MT_FRAME_RATE,       0xc459a2e8, 0x3d2c, 0x4e44, 0xb1,0x32,0xfe,0xe5,0x15,0x6c,0x7b,0xb0);
#endif
#ifndef MF_MT_MPEG2_PROFILE
DEFINE_GUID(MF_MT_MPEG2_PROFILE,    0xad76a80b, 0x2d5c, 0x4e0b, 0xb3,0x75,0x64,0xe5,0x20,0x13,0x70,0x36);
#endif


DEFINE_GUID(LOCAL_MFMediaType_Video,     0x73646976, 0x0000, 0x0010, 0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID(LOCAL_MFVideoFormat_H264,    0x34363248, 0x0000, 0x0010, 0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID(LOCAL_MFVideoFormat_NV12,    0x3231564E, 0x0000, 0x0010, 0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);


DEFINE_GUID(LOCAL_MFSampleExtension_CleanPoint, 0x9cdf01d8, 0xa0f0, 0x43ba, 0xb0,0x77,0xea,0xa0,0x6c,0xbd,0x72,0x8a);


DEFINE_GUID(LOCAL_MF_TRANSFORM_ASYNC_UNLOCK, 0xe3ef8e76, 0x0b94, 0x4dc2, 0xa1,0x3c,0x56,0x06,0x6b,0x1d,0x99,0x02);


DEFINE_GUID(LOCAL_MF_LOW_LATENCY, 0x9c27891a, 0xed7a, 0x40e1, 0x88,0xe8,0xb2,0x27,0x27,0xa0,0x24,0xee);


DEFINE_GUID(LOCAL_CODECAPI_AVEncCommonRateControlMode,   0x1c0608e9, 0x370c, 0x4710, 0x8a,0x58,0xcb,0x61,0x81,0xc4,0x24,0x23);
DEFINE_GUID(LOCAL_CODECAPI_AVEncCommonMeanBitRate,        0xf7222374, 0x2144, 0x4815, 0xb5,0x50,0xa3,0x7f,0x8e,0x12,0xee,0x52);
DEFINE_GUID(LOCAL_CODECAPI_AVEncMPVGOPSize,               0x95f31b26, 0x95a4, 0x41aa, 0x93,0x03,0x24,0x6a,0x7f,0xc6,0xee,0xf1);
DEFINE_GUID(LOCAL_CODECAPI_AVEncAdaptiveMode,              0x4419b185, 0xda1f, 0x4f53, 0xbc,0x76,0x09,0x7d,0x0c,0x1e,0xfb,0x1e);
DEFINE_GUID(LOCAL_CODECAPI_AVLowLatencyMode,               0x9c27891a, 0xed7a, 0x40e1, 0x88,0xe8,0xb2,0x27,0x27,0xa0,0x24,0xee);


#define NESS_H264_PROFILE_BASELINE  66
#define NESS_H264_PROFILE_MAIN      77
#define NESS_H264_PROFILE_HIGH      100


#define NESS_RC_CBR  0



struct MFEncoder {
    IMFTransform*   mft;            
    int             width;
    int             height;
    int             fps;
    int             bitrate_bps;    
    int64_t         frame_duration_hns; 
    int64_t         next_pts_hns;   

    DWORD           input_stream_id;
    DWORD           output_stream_id;
    DWORD           output_buffer_size;
    int             encoder_provides_samples;

    int             frames_submitted;
    int             stream_started;
    int             draining;

   
    uint8_t*        codec_private;
    int             codec_private_size;
    int             codec_private_ready;

   
    uint8_t*        output_buf;
    int             output_buf_capacity;

    char            error[256];
};



static void set_error(MFEncoder* enc, const char* msg)
{
    if (enc && msg)
        strncpy(enc->error, msg, sizeof(enc->error) - 1);
}


static HRESULT mf_set_2uint32(IMFAttributes* attrs, REFGUID key, UINT32 hi, UINT32 lo)
{
    return IMFAttributes_SetUINT64(attrs, key, ((UINT64)hi << 32) | lo);
}


static int configure_codec_api(MFEncoder* enc)
{
    ICodecAPI* codec_api = NULL;
    HRESULT hr;
    VARIANT var;

    hr = IMFTransform_QueryInterface(enc->mft, &LOCAL_IID_ICodecAPI, (void**)&codec_api);
    if (FAILED(hr)) {
       
        return 0;
    }

    VariantInit(&var);

   
    var.vt = VT_UI4;
    var.ulVal = NESS_RC_CBR;
    ICodecAPI_SetValue(codec_api, &LOCAL_CODECAPI_AVEncCommonRateControlMode, &var);

   
    var.vt = VT_UI4;
    var.ulVal = (ULONG)enc->bitrate_bps;
    ICodecAPI_SetValue(codec_api, &LOCAL_CODECAPI_AVEncCommonMeanBitRate, &var);

   
    var.vt = VT_UI4;
    var.ulVal = (ULONG)(enc->fps * 2);
    ICodecAPI_SetValue(codec_api, &LOCAL_CODECAPI_AVEncMPVGOPSize, &var);

   
    var.vt = VT_BOOL;
    var.boolVal = VARIANT_TRUE;
    ICodecAPI_SetValue(codec_api, &LOCAL_CODECAPI_AVLowLatencyMode, &var);

    ICodecAPI_Release(codec_api);
    return 0;
}


static void try_extract_codec_private(MFEncoder* enc, const uint8_t* data, int size)
{
    const uint8_t* sps = NULL;
    const uint8_t* pps = NULL;
    int sps_len = 0, pps_len = 0;

    if (enc->codec_private_ready)
        return;

    if (avc_extract_sps_pps(data, size, &sps, &sps_len, &pps, &pps_len) == 0) {
        if (enc->codec_private) {
            free(enc->codec_private);
            enc->codec_private = NULL;
        }
        if (avc_build_codec_private(sps, sps_len, pps, pps_len,
                                     &enc->codec_private,
                                     &enc->codec_private_size) == 0) {
            enc->codec_private_ready = 1;
        }
    }
}


static int receive_one_packet(MFEncoder* enc, mf_packet_callback cb, void* userdata)
{
    HRESULT hr;
    DWORD status = 0;
    MFT_OUTPUT_DATA_BUFFER out_buf;
    IMFSample* out_sample = NULL;
    IMFMediaBuffer* media_buf = NULL;
    BYTE* raw_data = NULL;
    DWORD raw_len = 0;
    MFEncodedPacket pkt;
    LONGLONG sample_time = 0;
    LONGLONG sample_duration = 0;
    UINT32 is_clean_point = 0;
    int ret = 0;

    memset(&out_buf, 0, sizeof(out_buf));
    out_buf.dwStreamID = enc->output_stream_id;
    out_buf.pSample = NULL;

   
    if (!enc->encoder_provides_samples) {
        IMFMediaBuffer* alloc_buf = NULL;

        hr = MFCreateSample(&out_sample);
        if (FAILED(hr)) {
            set_error(enc, "MFCreateSample para output falhou");
            return -1;
        }

        hr = MFCreateMemoryBuffer(enc->output_buffer_size, &alloc_buf);
        if (FAILED(hr)) {
            IMFSample_Release(out_sample);
            set_error(enc, "MFCreateMemoryBuffer para output falhou");
            return -1;
        }

        hr = IMFSample_AddBuffer(out_sample, alloc_buf);
        IMFMediaBuffer_Release(alloc_buf);
        if (FAILED(hr)) {
            IMFSample_Release(out_sample);
            set_error(enc, "AddBuffer para output falhou");
            return -1;
        }

        out_buf.pSample = out_sample;
    }

   
    hr = IMFTransform_ProcessOutput(enc->mft, 0, 1, &out_buf, &status);

    if (out_buf.pEvents) {
        IMFCollection_Release(out_buf.pEvents);
        out_buf.pEvents = NULL;
    }

    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
       
        if (out_sample)
            IMFSample_Release(out_sample);
        return 0;
    }

    if (FAILED(hr)) {
        if (out_sample)
            IMFSample_Release(out_sample);
        set_error(enc, "ProcessOutput falhou");
        return -1;
    }

   
    if (enc->encoder_provides_samples && out_buf.pSample) {
        out_sample = out_buf.pSample;
    }

    if (!out_sample) {
        set_error(enc, "ProcessOutput OK mas sem sample");
        return -1;
    }

   
    hr = IMFSample_ConvertToContiguousBuffer(out_sample, &media_buf);
    if (FAILED(hr)) {
        IMFSample_Release(out_sample);
        set_error(enc, "ConvertToContiguousBuffer falhou");
        return -1;
    }

    hr = IMFMediaBuffer_Lock(media_buf, &raw_data, NULL, &raw_len);
    if (FAILED(hr)) {
        IMFMediaBuffer_Release(media_buf);
        IMFSample_Release(out_sample);
        set_error(enc, "Lock do buffer falhou");
        return -1;
    }

   
    IMFSample_GetSampleTime(out_sample, &sample_time);
    IMFSample_GetSampleDuration(out_sample, &sample_duration);

    hr = IMFAttributes_GetUINT32((IMFAttributes*)out_sample,
                                  &LOCAL_MFSampleExtension_CleanPoint,
                                  &is_clean_point);
    if (FAILED(hr))
        is_clean_point = 0;

   
    memset(&pkt, 0, sizeof(pkt));
    pkt.data = (uint8_t*)raw_data;
    pkt.size = (int)raw_len;
    pkt.pts_hns = sample_time;
    pkt.duration_hns = sample_duration;
    pkt.is_keyframe = is_clean_point ? 1 : 0;

   
    if (pkt.is_keyframe) {
        try_extract_codec_private(enc, pkt.data, pkt.size);
    }

   
    if (cb) {
        ret = cb(userdata, &pkt);
    }

   
    IMFMediaBuffer_Unlock(media_buf);
    IMFMediaBuffer_Release(media_buf);
    IMFSample_Release(out_sample);

    return (ret < 0) ? ret : 1; 
}




int mf_encoder_create(MFEncoder** out_enc, int width, int height,
                       int fps, int bitrate_kbps)
{
    MFEncoder* enc = NULL;
    HRESULT hr;
    IMFMediaType* output_type = NULL;
    IMFMediaType* input_type = NULL;
    MFT_OUTPUT_STREAM_INFO out_info;
    IMFAttributes* attrs = NULL;

    *out_enc = NULL;

   
    if (width <= 0 || height <= 0 || fps <= 0 || bitrate_kbps <= 0)
        return -1;
    if ((width % 2) != 0 || (height % 2) != 0)
        return -1;

   
    enc = (MFEncoder*)calloc(1, sizeof(MFEncoder));
    if (!enc) return -1;

    enc->width = width;
    enc->height = height;
    enc->fps = fps;
    enc->bitrate_bps = bitrate_kbps * 1000;
    enc->frame_duration_hns = 10000000LL / fps; 
    enc->next_pts_hns = 0;

   
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        set_error(enc, "CoInitializeEx falhou");
        goto fail;
    }

    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        set_error(enc, "MFStartup falhou");
        goto fail;
    }

   
    hr = CoCreateInstance(&CLSID_CMSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMFTransform, (void**)&enc->mft);
    if (FAILED(hr)) {
        set_error(enc, "CoCreateInstance CMSH264EncoderMFT falhou");
        goto fail;
    }

   
   
    hr = IMFTransform_GetAttributes(enc->mft, &attrs);
    if (SUCCEEDED(hr)) {
       
        IMFAttributes_SetUINT32(attrs, &LOCAL_MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
       
        IMFAttributes_SetUINT32(attrs, &LOCAL_MF_LOW_LATENCY, TRUE);
        IMFAttributes_Release(attrs);
        attrs = NULL;
    }

   
    configure_codec_api(enc);

   
    hr = IMFTransform_GetStreamIDs(enc->mft, 1, &enc->input_stream_id,
                                    1, &enc->output_stream_id);
    if (hr == E_NOTIMPL) {
       
        enc->input_stream_id = 0;
        enc->output_stream_id = 0;
    } else if (FAILED(hr)) {
        set_error(enc, "GetStreamIDs falhou");
        goto fail;
    }

   
   
    hr = MFCreateMediaType(&output_type);
    if (FAILED(hr)) {
        set_error(enc, "MFCreateMediaType para output falhou");
        goto fail;
    }

    IMFMediaType_SetGUID(output_type, &MF_MT_MAJOR_TYPE, &LOCAL_MFMediaType_Video);
    IMFMediaType_SetGUID(output_type, &MF_MT_SUBTYPE, &LOCAL_MFVideoFormat_H264);
    IMFMediaType_SetUINT32(output_type, &MF_MT_AVG_BITRATE, (UINT32)enc->bitrate_bps);
    IMFMediaType_SetUINT32(output_type, &MF_MT_INTERLACE_MODE, 2);
    IMFMediaType_SetUINT32(output_type, &MF_MT_MPEG2_PROFILE, NESS_H264_PROFILE_BASELINE);

    mf_set_2uint32((IMFAttributes*)output_type, &MF_MT_FRAME_SIZE,
                   (UINT32)width, (UINT32)height);
    mf_set_2uint32((IMFAttributes*)output_type, &MF_MT_FRAME_RATE,
                   (UINT32)fps, 1);

    hr = IMFTransform_SetOutputType(enc->mft, enc->output_stream_id, output_type, 0);
    if (FAILED(hr)) {
        set_error(enc, "SetOutputType H.264 falhou");
        goto fail;
    }

   
   
    hr = MFCreateMediaType(&input_type);
    if (FAILED(hr)) {
        set_error(enc, "MFCreateMediaType para input falhou");
        goto fail;
    }

    IMFMediaType_SetGUID(input_type, &MF_MT_MAJOR_TYPE, &LOCAL_MFMediaType_Video);
    IMFMediaType_SetGUID(input_type, &MF_MT_SUBTYPE, &LOCAL_MFVideoFormat_NV12);
    IMFMediaType_SetUINT32(input_type, &MF_MT_INTERLACE_MODE, 2);

    mf_set_2uint32((IMFAttributes*)input_type, &MF_MT_FRAME_SIZE,
                   (UINT32)width, (UINT32)height);
    mf_set_2uint32((IMFAttributes*)input_type, &MF_MT_FRAME_RATE,
                   (UINT32)fps, 1);

    hr = IMFTransform_SetInputType(enc->mft, enc->input_stream_id, input_type, 0);
    if (FAILED(hr)) {
        set_error(enc, "SetInputType NV12 falhou");
        goto fail;
    }

   
    memset(&out_info, 0, sizeof(out_info));
    hr = IMFTransform_GetOutputStreamInfo(enc->mft, enc->output_stream_id, &out_info);
    if (FAILED(hr)) {
        set_error(enc, "GetOutputStreamInfo falhou");
        goto fail;
    }

    enc->output_buffer_size = out_info.cbSize;
    if (enc->output_buffer_size == 0)
        enc->output_buffer_size = width * height * 2; 

    enc->encoder_provides_samples =
        (out_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) ||
        (out_info.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES);

   
   
    hr = IMFTransform_ProcessMessage(enc->mft, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (FAILED(hr)) {
        set_error(enc, "BEGIN_STREAMING falhou");
        goto fail;
    }

    hr = IMFTransform_ProcessMessage(enc->mft, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (FAILED(hr)) {
        set_error(enc, "START_OF_STREAM falhou");
        goto fail;
    }

    enc->stream_started = 1;

   
    if (output_type) IMFMediaType_Release(output_type);
    if (input_type)  IMFMediaType_Release(input_type);

    *out_enc = enc;
    return 0;

fail:
    if (output_type) IMFMediaType_Release(output_type);
    if (input_type)  IMFMediaType_Release(input_type);
    if (attrs)       IMFAttributes_Release(attrs);
    mf_encoder_destroy(enc);
    return -1;
}


int mf_encoder_submit_frame(MFEncoder* enc, const uint8_t* nv12, int nv12_size)
{
    HRESULT hr;
    IMFMediaBuffer* buffer = NULL;
    IMFSample* sample = NULL;
    BYTE* buf_ptr = NULL;
    int expected_size;

    if (!enc || !enc->mft || !nv12)
        return -1;

    expected_size = enc->width * enc->height * 3 / 2;
    if (nv12_size < expected_size) {
        set_error(enc, "nv12_size menor que esperado");
        return -1;
    }

   
    hr = MFCreateMemoryBuffer((DWORD)expected_size, &buffer);
    if (FAILED(hr)) {
        set_error(enc, "MFCreateMemoryBuffer falhou");
        return -1;
    }

   
    hr = IMFMediaBuffer_Lock(buffer, &buf_ptr, NULL, NULL);
    if (FAILED(hr)) {
        IMFMediaBuffer_Release(buffer);
        set_error(enc, "Lock do input buffer falhou");
        return -1;
    }

    memcpy(buf_ptr, nv12, expected_size);

    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_SetCurrentLength(buffer, (DWORD)expected_size);

   
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) {
        IMFMediaBuffer_Release(buffer);
        set_error(enc, "MFCreateSample falhou");
        return -1;
    }

    hr = IMFSample_AddBuffer(sample, buffer);
    IMFMediaBuffer_Release(buffer);
    if (FAILED(hr)) {
        IMFSample_Release(sample);
        set_error(enc, "AddBuffer falhou");
        return -1;
    }

   
    IMFSample_SetSampleTime(sample, enc->next_pts_hns);
    IMFSample_SetSampleDuration(sample, enc->frame_duration_hns);

   
    if (enc->frames_submitted == 0) {
        IMFAttributes_SetUINT32((IMFAttributes*)sample,
                                 &LOCAL_MFSampleExtension_CleanPoint, TRUE);
    }

   
    hr = IMFTransform_ProcessInput(enc->mft, enc->input_stream_id, sample, 0);
    IMFSample_Release(sample);

    if (FAILED(hr)) {
        if (hr == MF_E_NOTACCEPTING) {
            set_error(enc, "ProcessInput: MF_E_NOTACCEPTING (encoder cheio)");
        } else {
            set_error(enc, "ProcessInput falhou");
        }
        return -1;
    }

    enc->next_pts_hns += enc->frame_duration_hns;
    enc->frames_submitted++;
    return 0;
}


int mf_encoder_receive_packets(MFEncoder* enc, mf_packet_callback cb, void* userdata)
{
    int total = 0;

    if (!enc || !enc->mft)
        return -1;

   
    while (1) {
        int ret = receive_one_packet(enc, cb, userdata);
        if (ret < 0)
            return ret;   
        if (ret == 0)
            break;        
        total++;
    }

    return total;
}


int mf_encoder_drain(MFEncoder* enc, mf_packet_callback cb, void* userdata)
{
    HRESULT hr;
    int total = 0;

    if (!enc || !enc->mft)
        return -1;

    if (enc->draining)
        return 0;

   
    hr = IMFTransform_ProcessMessage(enc->mft, MFT_MESSAGE_COMMAND_DRAIN, 0);
    if (FAILED(hr)) {
        set_error(enc, "COMMAND_DRAIN falhou");
        return -1;
    }

    enc->draining = 1;

   
    while (1) {
        int ret = receive_one_packet(enc, cb, userdata);
        if (ret < 0)
            return ret;
        if (ret == 0)
            break;
        total++;
    }

    return total;
}


int mf_encoder_get_codec_private(MFEncoder* enc, uint8_t** out, int* out_size)
{
    if (!enc || !enc->codec_private_ready) {
        *out = NULL;
        *out_size = 0;
        return -1;
    }

    *out = enc->codec_private;
    *out_size = enc->codec_private_size;
    return 0;
}


void mf_encoder_destroy(MFEncoder* enc)
{
    if (!enc) return;

    if (enc->mft) {
        if (enc->stream_started) {
            IMFTransform_ProcessMessage(enc->mft, MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            IMFTransform_ProcessMessage(enc->mft, MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        }
        IMFTransform_Release(enc->mft);
        enc->mft = NULL;
    }

    if (enc->codec_private) {
        free(enc->codec_private);
        enc->codec_private = NULL;
    }

    if (enc->output_buf) {
        free(enc->output_buf);
        enc->output_buf = NULL;
    }

    MFShutdown();
    CoUninitialize();

    free(enc);
}
