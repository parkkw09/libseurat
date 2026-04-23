#include "seurat/aac_native.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mfplay.h>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

struct seurat_aac_encoder {
    IMFTransform* pTransform;
};

seurat_aac_encoder_t* seurat_aac_create(const seurat_aac_config_t* cfg) {
    if (!cfg) return NULL;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return NULL;

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return NULL;

    seurat_aac_encoder_t* enc = new seurat_aac_encoder_t();
    enc->pTransform = NULL;

    // TODO: CoCreateInstance(CLSID_AACMFTEncoder), set input/output media types
    
    return enc;
}

int seurat_aac_encode(seurat_aac_encoder_t* enc,
                      const int16_t* pcm, int samples_per_channel,
                      uint8_t* adts_out, int out_capacity) {
    if (!enc || !enc->pTransform) return -1;
    // TODO: ProcessInput, ProcessOutput
    return 0;
}

void seurat_aac_destroy(seurat_aac_encoder_t* enc) {
    if (!enc) return;
    if (enc->pTransform) {
        enc->pTransform->Release();
    }
    delete enc;
    MFShutdown();
    CoUninitialize();
}

#else

// Dummy fallback
seurat_aac_encoder_t* seurat_aac_create(const seurat_aac_config_t* cfg) { return NULL; }
int seurat_aac_encode(seurat_aac_encoder_t* enc, const int16_t* pcm, int samples_per_channel, uint8_t* adts_out, int out_capacity) { return -1; }
void seurat_aac_destroy(seurat_aac_encoder_t* enc) {}

#endif
