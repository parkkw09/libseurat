#include "seurat/aac_native.h"
#include <stdlib.h>
#include <string.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

struct seurat_aac_encoder {
    AMediaCodec* codec;
    AMediaFormat* format;
    int channels;
};

seurat_aac_encoder_t* seurat_aac_create(const seurat_aac_config_t* cfg) {
    if (!cfg) return NULL;

    seurat_aac_encoder_t* enc = (seurat_aac_encoder_t*)calloc(1, sizeof(seurat_aac_encoder_t));
    if (!enc) return NULL;

    enc->channels = cfg->channels;
    enc->codec = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (!enc->codec) {
        free(enc);
        return NULL;
    }

    enc->format = AMediaFormat_new();
    AMediaFormat_setString(enc->format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_SAMPLE_RATE, cfg->sample_rate);
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, cfg->channels);
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_BIT_RATE, cfg->bitrate);
    // 2 = AAC LC
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_AAC_PROFILE, 2); 
    AMediaFormat_setInt32(enc->format, AMEDIAFORMAT_KEY_IS_ADTS, 1);

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

int seurat_aac_encode(seurat_aac_encoder_t* enc,
                      const int16_t* pcm, int samples_per_channel,
                      uint8_t* adts_out, int out_capacity) {
    if (!enc || !enc->codec) return -1;

    // 1. Queue input PCM
    ssize_t in_buf_idx = AMediaCodec_dequeueInputBuffer(enc->codec, 2000); // 2ms timeout
    if (in_buf_idx >= 0) {
        size_t buf_size = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(enc->codec, in_buf_idx, &buf_size);
        
        int input_bytes = samples_per_channel * 2 * enc->channels;
        if (buf && input_bytes <= buf_size) {
            memcpy(buf, pcm, input_bytes);
            AMediaCodec_queueInputBuffer(enc->codec, in_buf_idx, 0, input_bytes, 0, 0); // No pts needed for raw audio usually
        }
    }
    
    // 2. Drain output ADTS
    int out_size = 0;
    AMediaCodecBufferInfo info;
    ssize_t out_buf_idx = AMediaCodec_dequeueOutputBuffer(enc->codec, &info, 2000);
    
    while (out_buf_idx >= 0) {
        size_t buf_size = 0;
        uint8_t* buf = AMediaCodec_getOutputBuffer(enc->codec, out_buf_idx, &buf_size);
        
        if (buf && info.size > 0) {
            if (out_size + info.size <= out_capacity) {
                memcpy(adts_out + out_size, buf + info.offset, info.size);
                out_size += info.size;
            }
        }
        
        AMediaCodec_releaseOutputBuffer(enc->codec, out_buf_idx, false);
        out_buf_idx = AMediaCodec_dequeueOutputBuffer(enc->codec, &info, 0);
    }
    
    return out_size;
}

void seurat_aac_destroy(seurat_aac_encoder_t* enc) {
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
