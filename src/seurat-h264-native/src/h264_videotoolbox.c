#include "seurat/h264_native.h"
#include <stdlib.h>
#include <string.h>
#include <VideoToolbox/VideoToolbox.h>
#include <CoreVideo/CoreVideo.h>
#include <CoreFoundation/CoreFoundation.h>

struct seurat_h264_encoder {
    VTCompressionSessionRef session;
    CVPixelBufferPoolRef pool;
    int width;
    int height;
    
    // Callback output
    uint8_t* out_buffer;
    int out_capacity;
    int out_size;
    int is_keyframe;
};

static void vt_callback(void *outputCallbackRefCon, void *sourceFrameRefCon, OSStatus status, VTEncodeInfoFlags infoFlags, CMSampleBufferRef sampleBuffer) {
    if (status != noErr || !sampleBuffer) return;
    
    seurat_h264_encoder_t* enc = (seurat_h264_encoder_t*)outputCallbackRefCon;
    
    // Check if keyframe
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments && CFArrayGetCount(attachments) > 0) {
        CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
        enc->is_keyframe = !CFDictionaryContainsKey(dict, kCMSampleAttachmentKey_NotSync);
    } else {
        enc->is_keyframe = 1;
    }

    // Output Annex-B: first write SPS/PPS if keyframe
    enc->out_size = 0;
    if (enc->is_keyframe) {
        CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sampleBuffer);
        size_t sps_size = 0, pps_size = 0;
        const uint8_t *sps = NULL, *pps = NULL;
        
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, &sps, &sps_size, NULL, NULL);
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 1, &pps, &pps_size, NULL, NULL);
        
        if (sps && pps && enc->out_capacity >= (sps_size + pps_size + 8)) {
            const uint8_t start_code[] = {0, 0, 0, 1};
            memcpy(enc->out_buffer + enc->out_size, start_code, 4);
            memcpy(enc->out_buffer + enc->out_size + 4, sps, sps_size);
            enc->out_size += 4 + sps_size;
            
            memcpy(enc->out_buffer + enc->out_size, start_code, 4);
            memcpy(enc->out_buffer + enc->out_size + 4, pps, pps_size);
            enc->out_size += 4 + pps_size;
        }
    }

    // Write NAL units (convert AVCC to Annex B)
    CMBlockBufferRef dataBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    size_t length = 0;
    char *dataPointer = NULL;
    CMBlockBufferGetDataPointer(dataBuffer, 0, NULL, &length, &dataPointer);
    
    size_t offset = 0;
    while (offset < length) {
        uint32_t nal_length;
        memcpy(&nal_length, dataPointer + offset, 4);
        nal_length = CFSwapInt32BigToHost(nal_length);
        
        if (enc->out_size + 4 + nal_length <= enc->out_capacity) {
            const uint8_t start_code[] = {0, 0, 0, 1};
            memcpy(enc->out_buffer + enc->out_size, start_code, 4);
            memcpy(enc->out_buffer + enc->out_size + 4, dataPointer + offset + 4, nal_length);
            enc->out_size += 4 + nal_length;
        }
        offset += 4 + nal_length;
    }
}

seurat_h264_encoder_t* seurat_h264_create(const seurat_h264_config_t* cfg) {
    if (!cfg) return NULL;

    seurat_h264_encoder_t* enc = (seurat_h264_encoder_t*)calloc(1, sizeof(seurat_h264_encoder_t));
    if (!enc) return NULL;

    enc->width = cfg->width;
    enc->height = cfg->height;

    OSStatus status = VTCompressionSessionCreate(kCFAllocatorDefault,
                                                 cfg->width, cfg->height,
                                                 kCMVideoCodecType_H264,
                                                 NULL, NULL, NULL,
                                                 vt_callback,
                                                 enc,
                                                 &enc->session);
    if (status != noErr || !enc->session) {
        free(enc);
        return NULL;
    }

    // Configure session properties
    int32_t bps = cfg->bitrate_bps;
    CFNumberRef bitrateNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &bps);
    VTSessionSetProperty(enc->session, kVTCompressionPropertyKey_AverageBitRate, bitrateNum);
    CFRelease(bitrateNum);

    int32_t fps = cfg->fps_num / cfg->fps_den;
    CFNumberRef fpsNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &fps);
    VTSessionSetProperty(enc->session, kVTCompressionPropertyKey_ExpectedFrameRate, fpsNum);
    CFRelease(fpsNum);

    int32_t gop = cfg->gop_seconds * fps;
    CFNumberRef gopNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &gop);
    VTSessionSetProperty(enc->session, kVTCompressionPropertyKey_MaxKeyFrameInterval, gopNum);
    CFRelease(gopNum);

    VTSessionSetProperty(enc->session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_Main_AutoLevel);
    VTSessionSetProperty(enc->session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);

    VTCompressionSessionPrepareToEncodeFrames(enc->session);
    enc->pool = VTCompressionSessionGetPixelBufferPool(enc->session);
    if (enc->pool) {
        CVPixelBufferPoolRetain(enc->pool);
    }

    return enc;
}

int seurat_h264_encode(seurat_h264_encoder_t* enc,
                       const uint8_t* i420, int stride_y, int stride_uv,
                       int64_t pts_us,
                       uint8_t* nal_out, int out_capacity, int* is_keyframe) {
    if (!enc || !enc->session) return -1;
    
    enc->out_buffer = nal_out;
    enc->out_capacity = out_capacity;
    enc->out_size = 0;
    enc->is_keyframe = 0;
    
    CVPixelBufferRef pixelBuffer = NULL;
    if (enc->pool) {
        CVPixelBufferPoolCreatePixelBuffer(NULL, enc->pool, &pixelBuffer);
    } else {
        CVPixelBufferCreate(NULL, enc->width, enc->height, kCVPixelFormatType_420YpCbCr8Planar, NULL, &pixelBuffer);
    }
    
    if (!pixelBuffer) return -1;
    
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    uint8_t* y_dest = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
    uint8_t* u_dest = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
    uint8_t* v_dest = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 2);
    
    size_t dest_stride_y = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
    size_t dest_stride_u = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
    size_t dest_stride_v = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 2);
    
    const uint8_t* src_y = i420;
    const uint8_t* src_u = i420 + (stride_y * enc->height);
    const uint8_t* src_v = src_u + (stride_uv * (enc->height / 2));
    
    for (int y = 0; y < enc->height; y++) {
        memcpy(y_dest + y * dest_stride_y, src_y + y * stride_y, enc->width);
    }
    for (int y = 0; y < enc->height / 2; y++) {
        memcpy(u_dest + y * dest_stride_u, src_u + y * stride_uv, enc->width / 2);
        memcpy(v_dest + y * dest_stride_v, src_v + y * stride_uv, enc->width / 2);
    }
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    
    CMTime pts = CMTimeMake(pts_us, 1000000);
    OSStatus status = VTCompressionSessionEncodeFrame(enc->session, pixelBuffer, pts, kCMTimeInvalid, NULL, NULL, NULL);
    
    CVPixelBufferRelease(pixelBuffer);
    
    if (status != noErr) return -1;
    
    *is_keyframe = enc->is_keyframe;
    return enc->out_size;
}

void seurat_h264_force_keyframe(seurat_h264_encoder_t* enc) {
    if (!enc || !enc->session) return;
    CFDictionaryRef properties = NULL;
    const void *keys[1] = { kVTEncodeFrameOptionKey_ForceKeyFrame };
    const void *values[1] = { kCFBooleanTrue };
    properties = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (properties) {
        VTSessionSetProperties(enc->session, properties);
        CFRelease(properties);
    }
}

void seurat_h264_destroy(seurat_h264_encoder_t* enc) {
    if (!enc) return;
    if (enc->session) {
        VTCompressionSessionCompleteFrames(enc->session, kCMTimeInvalid);
        VTCompressionSessionInvalidate(enc->session);
        CFRelease(enc->session);
    }
    if (enc->pool) {
        CVPixelBufferPoolRelease(enc->pool);
    }
    free(enc);
}
