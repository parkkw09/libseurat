#include "seurat/aac_native.h"
#include <stdlib.h>
#include <string.h>
#include <AudioToolbox/AudioToolbox.h>

struct seurat_aac_encoder {
    AudioConverterRef converter;
    
    // Input state for the callback
    const uint8_t* pcm_data;
    UInt32 pcm_bytes_left;
    int channels;
};

static OSStatus input_data_proc(AudioConverterRef inAudioConverter,
                                UInt32 *ioNumberDataPackets,
                                AudioBufferList *ioData,
                                AudioStreamPacketDescription **outDataPacketDescription,
                                void *inUserData) {
    seurat_aac_encoder_t* enc = (seurat_aac_encoder_t*)inUserData;
    
    if (enc->pcm_bytes_left == 0) {
        *ioNumberDataPackets = 0;
        return -1; // Or some custom EOF code
    }
    
    UInt32 requested_bytes = *ioNumberDataPackets * 2 * enc->channels;
    UInt32 provided_bytes = (requested_bytes < enc->pcm_bytes_left) ? requested_bytes : enc->pcm_bytes_left;
    UInt32 provided_packets = provided_bytes / (2 * enc->channels);
    
    ioData->mBuffers[0].mData = (void*)enc->pcm_data;
    ioData->mBuffers[0].mDataByteSize = provided_bytes;
    ioData->mBuffers[0].mNumberChannels = enc->channels;
    
    enc->pcm_data += provided_bytes;
    enc->pcm_bytes_left -= provided_bytes;
    
    *ioNumberDataPackets = provided_packets;
    
    if (outDataPacketDescription) {
        *outDataPacketDescription = NULL;
    }
    
    return noErr;
}

seurat_aac_encoder_t* seurat_aac_create(const seurat_aac_config_t* cfg) {
    if (!cfg) return NULL;

    seurat_aac_encoder_t* enc = (seurat_aac_encoder_t*)calloc(1, sizeof(seurat_aac_encoder_t));
    if (!enc) return NULL;

    enc->channels = cfg->channels;

    AudioStreamBasicDescription inDesc = {0};
    inDesc.mSampleRate = cfg->sample_rate;
    inDesc.mFormatID = kAudioFormatLinearPCM;
    inDesc.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    inDesc.mFramesPerPacket = 1;
    inDesc.mChannelsPerFrame = cfg->channels;
    inDesc.mBitsPerChannel = 16;
    inDesc.mBytesPerPacket = 2 * cfg->channels;
    inDesc.mBytesPerFrame = 2 * cfg->channels;

    AudioStreamBasicDescription outDesc = {0};
    outDesc.mSampleRate = cfg->sample_rate;
    outDesc.mFormatID = kAudioFormatMPEG4AAC;
    outDesc.mChannelsPerFrame = cfg->channels;
    outDesc.mFramesPerPacket = 1024;

    OSStatus status = AudioConverterNew(&inDesc, &outDesc, &enc->converter);
    if (status != noErr || !enc->converter) {
        free(enc);
        return NULL;
    }

    UInt32 bitrate = cfg->bitrate;
    AudioConverterSetProperty(enc->converter, kAudioConverterEncodeBitRate, sizeof(bitrate), &bitrate);

    return enc;
}

int seurat_aac_encode(seurat_aac_encoder_t* enc,
                      const int16_t* pcm, int samples_per_channel,
                      uint8_t* adts_out, int out_capacity) {
    if (!enc || !enc->converter) return -1;
    
    enc->pcm_data = (const uint8_t*)pcm;
    enc->pcm_bytes_left = samples_per_channel * 2 * enc->channels;
    
    UInt32 ioOutputDataPackets = 1; // AAC generally outputs 1 packet at a time
    AudioBufferList outBufferList;
    outBufferList.mNumberBuffers = 1;
    outBufferList.mBuffers[0].mNumberChannels = enc->channels;
    outBufferList.mBuffers[0].mDataByteSize = out_capacity;
    outBufferList.mBuffers[0].mData = adts_out;
    
    AudioStreamPacketDescription outPacketDesc;
    
    OSStatus status = AudioConverterFillComplexBuffer(enc->converter,
                                                      input_data_proc,
                                                      enc,
                                                      &ioOutputDataPackets,
                                                      &outBufferList,
                                                      &outPacketDesc);
                                                      
    if (status != noErr && status != -1) {
        return -1;
    }
    
    if (ioOutputDataPackets == 0) {
        return 0; // No data output yet
    }
    
    // Note: AudioConverterFillComplexBuffer outputs raw AAC. ADTS header needs to be prepended manually 
    // if the RTMP/FLV muxer requires it (FLV AAC sequence header usually handles this, so raw is often preferred).
    // The user's spec mentions "seurat_flv_write_audio_frame(m, ts, raw, raw_len)" which implies raw AAC is fine.
    
    return outBufferList.mBuffers[0].mDataByteSize;
}

void seurat_aac_destroy(seurat_aac_encoder_t* enc) {
    if (!enc) return;
    if (enc->converter) {
        AudioConverterDispose(enc->converter);
    }
    free(enc);
}
