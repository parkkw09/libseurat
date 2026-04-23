#ifndef SEURAT_AAC_NATIVE_H
#define SEURAT_AAC_NATIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct seurat_aac_encoder seurat_aac_encoder_t;

typedef struct {
    int sample_rate;    // 44100 or 48000
    int channels;       // 1 or 2
    int bitrate;        // bps, e.g. 128000
} seurat_aac_config_t;

seurat_aac_encoder_t* seurat_aac_create(const seurat_aac_config_t* cfg);

int seurat_aac_encode(seurat_aac_encoder_t* enc,
                      const int16_t* pcm, int samples_per_channel,
                      uint8_t* adts_out, int out_capacity);

void seurat_aac_destroy(seurat_aac_encoder_t* enc);

#ifdef __cplusplus
}
#endif

#endif // SEURAT_AAC_NATIVE_H
