#ifndef SEURAT_H264_NATIVE_H
#define SEURAT_H264_NATIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct seurat_h264_encoder seurat_h264_encoder_t;

typedef struct {
    int      width, height;
    int      fps_num, fps_den;     // e.g. 60000 / 1001
    int      bitrate_bps;          // CBR
    int      gop_seconds;          // 2 권장 (YouTube Live)
    int      profile;              // 0=Main, 1=High
    int      allow_hw_only;        // 1이면 HW 인코더 미지원 시 즉시 실패
} seurat_h264_config_t;

seurat_h264_encoder_t* seurat_h264_create(const seurat_h264_config_t* cfg);

int seurat_h264_encode(seurat_h264_encoder_t* enc,
                       const uint8_t* i420, int stride_y, int stride_uv,
                       int64_t pts_us,
                       uint8_t* nal_out, int out_capacity, int* is_keyframe);

void seurat_h264_force_keyframe(seurat_h264_encoder_t* enc);

void seurat_h264_destroy(seurat_h264_encoder_t* enc);

#ifdef __cplusplus
}
#endif

#endif // SEURAT_H264_NATIVE_H
