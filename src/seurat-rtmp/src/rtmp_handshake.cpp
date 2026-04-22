// =============================================================================
// seurat-rtmp — RTMP 1.0 simple handshake (Phase B).
//
// Sequence:
//     C0 = { version=0x03 }                      (1 byte)
//     C1 = { time, zero, random[1528] }          (1536 bytes)
//     ---- send -->
//     <-- S0 = { version=0x03 }                  (1 byte)
//     <-- S1 = { time, zero, random[1528] }      (1536 bytes)
//     C2 = S1 echo                               (1536 bytes)
//     ---- send -->
//     <-- S2 = C1 echo                           (1536 bytes)
//
// For publish clients the "simple" form is sufficient; YouTube / Twitch / SOOP
// all accept it. The "complex" digest-signed handshake that Flash Media Server
// negotiated is only required by a few legacy deployments and is skipped here.
//
// We don't strictly validate S2 == C1 payload — some broker-style servers
// rewrite the echo. We only check that S0 carries version 0x03.
//
// License: MIT.
// =============================================================================

#include "rtmp_internal.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace seurat::rtmp {

namespace {

constexpr uint8_t kRtmpVersion = 0x03;
constexpr size_t  kHsPayload   = 1536;

void fill_random(uint8_t* buf, size_t len) {
    // Deterministic seed based on time is good enough here: the C1 payload is
    // just a nonce that the server mirrors back. We don't need CSPRNG quality.
    std::mt19937_64 rng(
        static_cast<uint64_t>(std::random_device{}()) ^
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf)));
    for (size_t i = 0; i + 8 <= len; i += 8) {
        const uint64_t v = rng();
        std::memcpy(buf + i, &v, 8);
    }
    for (size_t i = (len / 8) * 8; i < len; ++i) {
        buf[i] = static_cast<uint8_t>(rng() & 0xFF);
    }
}

}  // namespace

int run_handshake(Transport* tr) {
    if (!tr) return SEURAT_RTMP_E_INVALID_ARG;

    uint8_t c0 = kRtmpVersion;
    uint8_t c1[kHsPayload];
    uint8_t c2[kHsPayload];
    uint8_t s0 = 0;
    uint8_t s1[kHsPayload];
    uint8_t s2[kHsPayload];

    // C1 layout per spec: time(4) zero(4) random(1528)
    std::memset(c1, 0, 8);
    fill_random(c1 + 8, kHsPayload - 8);

    // -------- send C0 + C1
    int rc = tr->send_all(&c0, 1);
    if (rc != SEURAT_RTMP_OK) return rc;
    rc = tr->send_all(c1, kHsPayload);
    if (rc != SEURAT_RTMP_OK) return rc;

    // -------- read S0 + S1
    rc = tr->recv_exact(&s0, 1);
    if (rc != SEURAT_RTMP_OK) return rc;
    if (s0 != kRtmpVersion) return SEURAT_RTMP_E_PROTOCOL;

    rc = tr->recv_exact(s1, kHsPayload);
    if (rc != SEURAT_RTMP_OK) return rc;

    // -------- send C2 (echo of S1)
    std::memcpy(c2, s1, kHsPayload);
    rc = tr->send_all(c2, kHsPayload);
    if (rc != SEURAT_RTMP_OK) return rc;

    // -------- read S2 (ignored payload, only length matters)
    rc = tr->recv_exact(s2, kHsPayload);
    if (rc != SEURAT_RTMP_OK) return rc;

    return SEURAT_RTMP_OK;
}

}  // namespace seurat::rtmp
