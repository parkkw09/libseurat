#include "seurat/h264_native.h"
#include <stdlib.h>
#include <string.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

struct seurat_h264_encoder {
    AMediaCodec* codec;
    AMediaFormat* format;
    int width;
    int height;
};

seurat_h264_encoder_t* seurat_h264_create(const seurat_h264_config_t* cfg) {
    if (!cfg) return NULL;

    seurat_h264_encoder_t* enc = (seurat_h264_encoder_t*)calloc(1, sizeof(seurat_h264_encoder_t));
    if (!enc) return NULL;

    enc->width = cfg->width;
    enc->height = cfg->height;

    enc->codec = AMediaCodec_createEncoderByType("video/avc");
    if (!enc->codec) {
        free(enc);
        return NULL;
    }

    enc->format = AMediaFormat_new();
    AMediaFormat_setString(enc->format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_WIDTH, cfg->width);
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_HEIGHT, cfg->height);
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_BIT_RATE, cfg->bitrate_bps);
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_FRAME_RATE, cfg->fps_num / cfg->fps_den);
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, cfg->gop_seconds);
    // 21 = COLOR_FormatYUV420SemiPlanar (NV12), 19 = COLOR_FormatYUV420Planar (I420).
    // Using 19 (I420) since that is our input format from libyuv.
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 19); 

    media_status_t status = AMediaCodec_configure(enc->codec, enc->format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (status != AMEDIA_OK) {
        AMediaFormat_delete(enc->format);
        AMediaCodec_delete(enc->codec);
        free(enc);
        return NULL;
    }

    status = AMediaCodec_start(enc->codec);
    if (status != AMEDIA_OK) {
        AMediaFormat_delete(enc->format);
        AMediaCodec_delete(enc->codec);
        free(enc);
        return NULL;
    }

    return enc;
}

int seurat_h264_encode(seurat_h264_encoder_t* enc,
                       const uint8_t* i420, int stride_y, int stride_uv,
                       int64_t pts_us,
                       uint8_t* nal_out, int out_capacity, int* is_keyframe) {
    if (!enc || !enc->codec) return -1;
    
    // 1. Feed input buffer
    ssize_t in_buf_idx = AMediaCodec_dequeueInputBuffer(enc->codec, 2000); // 2ms timeout
    if (in_buf_idx >= 0) {
        size_t buf_size = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(enc->codec, in_buf_idx, &buf_size);
        if (buf) {
            // Copy I420 into buf
            size_t y_size = enc->width * enc->height;
            size_t uv_size = (enc->width / 2) * (enc->height / 2);
            
            // Assume stride == width for simplicity in this baseline, or do line-by-line copy
            if (stride_y == enc->width && stride_uv == (enc->width / 2)) {
                memcpy(buf, i420, y_size + 2 * uv_size);
            } else {
                uint8_t* dst = buf;
                const uint8_t* src = i420;
                for (int y = 0; y < enc->height; y++) {
                    memcpy(dst, src, enc->width);
                    dst += enc->width; src += stride_y;
                }
                src = i420 + (stride_y * enc->height);
                for (int y = 0; y < enc->height / 2; y++) {
                    memcpy(dst, src, enc->width / 2);
                    dst += (enc->width / 2); src += stride_uv;
                }
                src = i420 + (stride_y * enc->height) + (stride_uv * (enc->height / 2));
                for (int y = 0; y < enc->height / 2; y++) {
                    memcpy(dst, src, enc->width / 2);
                    dst += (enc->width / 2); src += stride_uv;
                }
            }
            
            AMediaCodec_queueInputBuffer(enc->codec, in_buf_idx, 0, y_size + 2 * uv_size, pts_us, 0);
        }
    }
    
    // 2. Drain output buffer
    int out_size = 0;
    *is_keyframe = 0;
    
    AMediaCodecBufferInfo info;
    ssize_t out_buf_idx = AMediaCodec_dequeueOutputBuffer(enc->codec, &info, 2000);
    
    while (out_buf_idx >= 0) {
        size_t buf_size = 0;
        uint8_t* buf = AMediaCodec_getOutputBuffer(enc->codec, out_buf_idx, &buf_size);
        
        if (buf && info.size > 0) {
            if (out_size + info.size <= out_capacity) {
                memcpy(nal_out + out_size, buf + info.offset, info.size);
                out_size += info.size;
                
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                    // SPS/PPS (Annex B)
                    *is_keyframe = 1;
                } else if (info.flags & 1) { // BUFFER_FLAG_SYNC_FRAME = 1
                    *is_keyframe = 1;
                }
            }
        }
        
        AMediaCodec_releaseOutputBuffer(enc->codec, out_buf_idx, false);
        out_buf_idx = AMediaCodec_dequeueOutputBuffer(enc->codec, &info, 0); // Drain completely
    }
    
    return out_size;
}

void seurat_h264_force_keyframe(seurat_h264_encoder_t* enc) {
    if (!enc || !enc->codec) return;
#if __ANDROID_API__ >= 26
    AMediaFormat* params = AMediaFormat_new();
    AMediaFormat_setInt32(params, "request-sync", 1);
    AMediaCodec_setParameters(enc->codec, params);
    AMediaFormat_delete(params);
#else
    // API 24/25 does not support AMediaCodec_setParameters in NDK.
    // Keyframes will only be generated based on the GOP interval.
#endif
}

void seurat_h264_destroy(seurat_h264_encoder_t* enc) {
    if (!enc) return;
    if (enc->codec) {
        AMediaCodec_stop(enc->codec);
        AMediaCodec_delete(enc->codec);
    }
    if (enc->format) {
        AMediaFormat_delete(enc->format);
    }
    free(enc);
}
