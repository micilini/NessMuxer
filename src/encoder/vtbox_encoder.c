

#include "encoder.h"
#include "../avc_utils.h"

#ifdef NESS_HAVE_VIDEOTOOLBOX

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <VideoToolbox/VideoToolbox.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <CoreFoundation/CoreFoundation.h>





typedef struct
{
    VTCompressionSessionRef session;

    int width;
    int height;
    int fps;
    int bitrate_kbps;
    int64_t frame_index;

   
    uint8_t* codec_private;
    int      codec_private_size;
    int      codec_private_ready;

   
    uint8_t* pending_packet;
    int      pending_packet_size;
    int64_t  pending_pts_hns;
    int64_t  pending_duration_hns;
    int      pending_keyframe;
    int      pending_ready;
} VtboxEncoder;





static void vtbox_clear_pending(VtboxEncoder* e)
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


static void vtbox_output_callback(
    void* outputCallbackRefCon,
    void* sourceFrameRefCon,
    OSStatus status,
    VTEncodeInfoFlags infoFlags,
    CMSampleBufferRef sampleBuffer)
{
    VtboxEncoder* e = (VtboxEncoder*)outputCallbackRefCon;
    CMBlockBufferRef block;
    size_t total_length = 0;
    size_t offset = 0;
    uint8_t* block_data = NULL;
    uint8_t* annexb = NULL;
    int annexb_size = 0;
    int annexb_capacity = 0;
    CFArrayRef attachments;
    CFDictionaryRef dict;
    CFBooleanRef not_sync;
    int is_keyframe = 0;
    CMTime pts_cm, dur_cm;

    (void)sourceFrameRefCon;
    (void)infoFlags;

    if (!e || status != noErr || !sampleBuffer)
        return;

   
    attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments && CFArrayGetCount(attachments) > 0) {
        dict = (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
       
        is_keyframe = 1;
        if (dict && CFDictionaryGetValueIfPresent(dict,
                kCMSampleAttachmentKey_NotSync, (const void**)&not_sync)) {
            if (CFBooleanGetValue(not_sync))
                is_keyframe = 0;
        }
    }

   
    block = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!block)
        return;

    total_length = CMBlockBufferGetDataLength(block);
    if (total_length == 0)
        return;

    block_data = (uint8_t*)malloc(total_length);
    if (!block_data)
        return;

    if (CMBlockBufferCopyDataBytes(block, 0, total_length, block_data) != kCMBlockBufferNoErr) {
        free(block_data);
        return;
    }

   
    annexb_capacity = (int)total_length;
    annexb = (uint8_t*)malloc((size_t)annexb_capacity);
    if (!annexb) {
        free(block_data);
        return;
    }

    offset = 0;
    annexb_size = 0;
    while (offset + 4 <= total_length) {
        uint32_t nalu_len = ((uint32_t)block_data[offset + 0] << 24) |
                            ((uint32_t)block_data[offset + 1] << 16) |
                            ((uint32_t)block_data[offset + 2] << 8)  |
                            ((uint32_t)block_data[offset + 3]);
        offset += 4;

        if (nalu_len == 0 || offset + nalu_len > total_length)
            break;

       
        if (annexb_size + 4 + (int)nalu_len > annexb_capacity) {
            annexb_capacity = annexb_size + 4 + (int)nalu_len + 1024;
            uint8_t* tmp = (uint8_t*)realloc(annexb, (size_t)annexb_capacity);
            if (!tmp) {
                free(annexb);
                free(block_data);
                return;
            }
            annexb = tmp;
        }

       
        annexb[annexb_size++] = 0x00;
        annexb[annexb_size++] = 0x00;
        annexb[annexb_size++] = 0x00;
        annexb[annexb_size++] = 0x01;

       
        memcpy(annexb + annexb_size, block_data + offset, nalu_len);
        annexb_size += (int)nalu_len;
        offset += nalu_len;
    }

    free(block_data);

    if (annexb_size <= 0) {
        free(annexb);
        return;
    }

   
    vtbox_clear_pending(e);

    e->pending_packet = annexb;
    e->pending_packet_size = annexb_size;

    pts_cm = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    dur_cm = CMSampleBufferGetDuration(sampleBuffer);

   
    if (CMTIME_IS_VALID(pts_cm) && pts_cm.timescale > 0) {
        e->pending_pts_hns = (int64_t)((double)pts_cm.value / (double)pts_cm.timescale * 10000000.0);
    } else {
        e->pending_pts_hns = e->frame_index * (10000000LL / e->fps);
    }

    if (CMTIME_IS_VALID(dur_cm) && dur_cm.timescale > 0) {
        e->pending_duration_hns = (int64_t)((double)dur_cm.value / (double)dur_cm.timescale * 10000000.0);
    } else {
        e->pending_duration_hns = 10000000LL / e->fps;
    }

    e->pending_keyframe = is_keyframe;
    e->pending_ready = 1;
}






static int vtbox_prepare_codec_private(VtboxEncoder* e)
{
    CMFormatDescriptionRef fmt;
    const uint8_t* sps = NULL;
    const uint8_t* pps = NULL;
    size_t sps_len = 0;
    size_t pps_len = 0;
    size_t param_count = 0;
    OSStatus st;

    if (!e || !e->session)
        return -1;

    if (e->codec_private_ready)
        return 0;

   

   
    VTCompressionSessionCompleteFrames(e->session, kCMTimeInvalid);

   

   
    if (e->pending_ready && e->pending_packet && e->pending_packet_size > 0) {
        const uint8_t* out_sps = NULL;
        const uint8_t* out_pps = NULL;
        int out_sps_len = 0;
        int out_pps_len = 0;

        if (avc_extract_sps_pps(e->pending_packet, e->pending_packet_size,
                                &out_sps, &out_sps_len,
                                &out_pps, &out_pps_len) == 0) {
            if (avc_build_codec_private(out_sps, out_sps_len,
                                        out_pps, out_pps_len,
                                        &e->codec_private,
                                        &e->codec_private_size) == 0) {
                e->codec_private_ready = 1;
                return 0;
            }
        }
    }

   
    printf("[vtbox] WARNING: could not extract SPS/PPS from first keyframe\n");
    return -1;
}





static int vtbox_create(void** ctx, int width, int height, int fps, int bitrate_kbps)
{
    VtboxEncoder* e;
    OSStatus st;
    CFMutableDictionaryRef encoder_spec;
    CFMutableDictionaryRef pixel_buf_attrs;
    CFNumberRef cf_width, cf_height, cf_pixfmt;
    CFNumberRef cf_bitrate, cf_max_keyframe;
    int32_t pixel_format = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
    int32_t bitrate_bps;
    int32_t max_keyframe_interval;

    if (!ctx || width <= 0 || height <= 0 || fps <= 0 || bitrate_kbps <= 0)
        return -1;
    if ((width % 2) != 0 || (height % 2) != 0)
        return -1;

    e = (VtboxEncoder*)calloc(1, sizeof(VtboxEncoder));
    if (!e)
        return -1;

    e->width = width;
    e->height = height;
    e->fps = fps;
    e->bitrate_kbps = bitrate_kbps;

    printf("[vtbox] create: width=%d height=%d fps=%d bitrate=%d\n",
           width, height, fps, bitrate_kbps);

   
    pixel_buf_attrs = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    if (!pixel_buf_attrs) {
        free(e);
        return -1;
    }

    cf_width  = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &width);
    cf_height = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &height);
    cf_pixfmt = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixel_format);

    CFDictionarySetValue(pixel_buf_attrs, kCVPixelBufferWidthKey, cf_width);
    CFDictionarySetValue(pixel_buf_attrs, kCVPixelBufferHeightKey, cf_height);
    CFDictionarySetValue(pixel_buf_attrs, kCVPixelBufferPixelFormatTypeKey, cf_pixfmt);

    CFRelease(cf_width);
    CFRelease(cf_height);
    CFRelease(cf_pixfmt);

   
    st = VTCompressionSessionCreate(
        kCFAllocatorDefault,
        width,
        height,
        kCMVideoCodecType_H264,
        NULL,              
        pixel_buf_attrs,   
        NULL,              
        vtbox_output_callback,
        e,                 
        &e->session
    );

    CFRelease(pixel_buf_attrs);

    if (st != noErr) {
        printf("[vtbox] ERROR: VTCompressionSessionCreate failed (status=%d)\n", (int)st);
        free(e);
        return -1;
    }

   

   
    VTSessionSetProperty(e->session,
        kVTCompressionPropertyKey_RealTime,
        kCFBooleanFalse);

   
    VTSessionSetProperty(e->session,
        kVTCompressionPropertyKey_ProfileLevel,
        kVTProfileLevel_H264_Baseline_AutoLevel);

   
    bitrate_bps = bitrate_kbps * 1000;
    cf_bitrate = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &bitrate_bps);
    VTSessionSetProperty(e->session,
        kVTCompressionPropertyKey_AverageBitRate,
        cf_bitrate);
    CFRelease(cf_bitrate);

   
    max_keyframe_interval = fps * 2;
    cf_max_keyframe = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &max_keyframe_interval);
    VTSessionSetProperty(e->session,
        kVTCompressionPropertyKey_MaxKeyFrameInterval,
        cf_max_keyframe);
    CFRelease(cf_max_keyframe);

   
    VTSessionSetProperty(e->session,
        kVTCompressionPropertyKey_AllowFrameReordering,
        kCFBooleanFalse);

   
    st = VTCompressionSessionPrepareToEncodeFrames(e->session);
    if (st != noErr) {
        printf("[vtbox] ERROR: VTCompressionSessionPrepareToEncodeFrames failed (status=%d)\n", (int)st);
        VTCompressionSessionInvalidate(e->session);
        CFRelease(e->session);
        free(e);
        return -1;
    }

    printf("[vtbox] session created and prepared\n");

    *ctx = e;
    return 0;
}

static int vtbox_submit_frame(void* ctx, const uint8_t* nv12, int nv12_size)
{
    VtboxEncoder* e = (VtboxEncoder*)ctx;
    CVPixelBufferRef pixel_buf = NULL;
    CVReturn cvret;
    CMTime pts, dur;
    OSStatus st;
    int expected_size;
    void* y_base;
    void* uv_base;
    size_t y_stride, uv_stride;
    int row;

    if (!e || !e->session || !nv12)
        return -1;

    expected_size = e->width * e->height * 3 / 2;
    if (nv12_size != expected_size)
        return -1;

   
    if (e->pending_ready)
        return -1;

   
    cvret = CVPixelBufferCreate(
        kCFAllocatorDefault,
        e->width,
        e->height,
        kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
        NULL,
        &pixel_buf
    );

    if (cvret != kCVReturnSuccess || !pixel_buf) {
        printf("[vtbox] ERROR: CVPixelBufferCreate failed (cvret=%d)\n", (int)cvret);
        return -1;
    }

    CVPixelBufferLockBaseAddress(pixel_buf, 0);

   
    y_base   = CVPixelBufferGetBaseAddressOfPlane(pixel_buf, 0);
    y_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel_buf, 0);

    for (row = 0; row < e->height; row++) {
        memcpy(
            (uint8_t*)y_base + row * y_stride,
            nv12 + row * e->width,
            (size_t)e->width
        );
    }

   
    uv_base   = CVPixelBufferGetBaseAddressOfPlane(pixel_buf, 1);
    uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel_buf, 1);

    for (row = 0; row < e->height / 2; row++) {
        memcpy(
            (uint8_t*)uv_base + row * uv_stride,
            nv12 + (e->width * e->height) + row * e->width,
            (size_t)e->width
        );
    }

    CVPixelBufferUnlockBaseAddress(pixel_buf, 0);

   
    pts = CMTimeMake(e->frame_index, e->fps);
    dur = CMTimeMake(1, e->fps);

   
    st = VTCompressionSessionEncodeFrame(
        e->session,
        pixel_buf,
        pts,
        dur,
        NULL,  
        NULL,  
        NULL   
    );

    CVPixelBufferRelease(pixel_buf);

    if (st != noErr) {
        printf("[vtbox] ERROR: VTCompressionSessionEncodeFrame failed (status=%d) at frame %lld\n",
               (int)st, (long long)e->frame_index);
        return -1;
    }

   
    st = VTCompressionSessionCompleteFrames(e->session, pts);
    if (st != noErr) {
        printf("[vtbox] ERROR: VTCompressionSessionCompleteFrames failed (status=%d)\n", (int)st);
        return -1;
    }

    e->frame_index++;

   
    if (!e->codec_private_ready && e->pending_ready && e->pending_keyframe) {
        vtbox_prepare_codec_private(e);
    }

    return 0;
}

static int vtbox_receive_packets(void* ctx, ness_packet_callback cb, void* userdata)
{
    VtboxEncoder* e = (VtboxEncoder*)ctx;
    NessEncodedPacket pkt;
    int ret;

    if (!e || !cb)
        return -1;

    if (!e->pending_ready)
        return 0;

    pkt.data         = e->pending_packet;
    pkt.size         = e->pending_packet_size;
    pkt.pts_hns      = e->pending_pts_hns;
    pkt.duration_hns = e->pending_duration_hns;
    pkt.is_keyframe  = e->pending_keyframe;

    ret = cb(userdata, &pkt);
    vtbox_clear_pending(e);

    return ret;
}

static int vtbox_drain(void* ctx, ness_packet_callback cb, void* userdata)
{
    VtboxEncoder* e = (VtboxEncoder*)ctx;
    OSStatus st;

    if (!e || !cb)
        return -1;

    if (!e->session)
        return 0;

   
    st = VTCompressionSessionCompleteFrames(e->session, kCMTimeInvalid);
    if (st != noErr) {
        printf("[vtbox] ERROR: drain CompleteFrames failed (status=%d)\n", (int)st);
        return -1;
    }

   
    if (e->pending_ready) {
        NessEncodedPacket pkt;

        pkt.data         = e->pending_packet;
        pkt.size         = e->pending_packet_size;
        pkt.pts_hns      = e->pending_pts_hns;
        pkt.duration_hns = e->pending_duration_hns;
        pkt.is_keyframe  = e->pending_keyframe;

        cb(userdata, &pkt);
        vtbox_clear_pending(e);
    }

    return 0;
}

static int vtbox_get_codec_private(void* ctx, uint8_t** out, int* out_size)
{
    VtboxEncoder* e = (VtboxEncoder*)ctx;

    if (!e || !out || !out_size || !e->codec_private_ready)
        return -1;

    *out = e->codec_private;
    *out_size = e->codec_private_size;
    return 0;
}

static void vtbox_destroy(void* ctx)
{
    VtboxEncoder* e = (VtboxEncoder*)ctx;
    if (!e)
        return;

    vtbox_clear_pending(e);

    if (e->session) {
        VTCompressionSessionInvalidate(e->session);
        CFRelease(e->session);
        e->session = NULL;
    }

    if (e->codec_private) {
        free(e->codec_private);
        e->codec_private = NULL;
    }

    free(e);
}





const NessEncoderVtable g_vtbox_encoder_vtable = {
    "VideoToolbox",
    vtbox_create,
    vtbox_submit_frame,
    vtbox_receive_packets,
    vtbox_drain,
    vtbox_get_codec_private,
    vtbox_destroy
};

#else



const NessEncoderVtable g_vtbox_encoder_vtable = {
    "VideoToolbox",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif