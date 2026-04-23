#include "seurat/h264_native.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mfplay.h>
#include <mferror.h>

// Link libraries
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

struct seurat_h264_encoder {
    IMFTransform* pTransform;
    // Buffer management...
};

seurat_h264_encoder_t* seurat_h264_create(const seurat_h264_config_t* cfg) {
    if (!cfg) return NULL;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return NULL;

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return NULL;

    seurat_h264_encoder_t* enc = new seurat_h264_encoder_t();
    enc->pTransform = NULL;

    // TODO: CoCreateInstance(CLSID_CMSH264EncoderMFT), set input/output media types
    
    return enc;
}

int seurat_h264_encode(seurat_h264_encoder_t* enc,
                       const uint8_t* i420, int stride_y, int stride_uv,
                       int64_t pts_us,
                       uint8_t* nal_out, int out_capacity, int* is_keyframe) {
    if (!enc || !enc->pTransform) return -1;
    // TODO: ProcessInput, ProcessOutput
    return 0;
}

void seurat_h264_force_keyframe(seurat_h264_encoder_t* enc) {
    if (!enc || !enc->pTransform) return;
    // TODO: Send MFT_MESSAGE_COMMAND_MARKER
}

void seurat_h264_destroy(seurat_h264_encoder_t* enc) {
    if (!enc) return;
    if (enc->pTransform) {
        enc->pTransform->Release();
    }
    delete enc;
    MFShutdown();
    CoUninitialize();
}

#else

// Dummy fallback if compiled not on Windows but included
seurat_h264_encoder_t* seurat_h264_create(const seurat_h264_config_t* cfg) { return NULL; }
int seurat_h264_encode(seurat_h264_encoder_t* enc, const uint8_t* i420, int stride_y, int stride_uv, int64_t pts_us, uint8_t* nal_out, int out_capacity, int* is_keyframe) { return -1; }
void seurat_h264_force_keyframe(seurat_h264_encoder_t* enc) {}
void seurat_h264_destroy(seurat_h264_encoder_t* enc) {}

#endif
