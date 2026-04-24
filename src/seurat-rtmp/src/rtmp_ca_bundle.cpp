// =============================================================================
// seurat-rtmp — platform CA bundle extractor (Phase E.3).
//
// Exposes `seurat_rtmp_platform_ca_bundle_pem()` to the public C API. The
// extraction is best-effort and intentionally conservative:
//
//   * On macOS we pull the system trust anchors via SecTrustCopyAnchorCertificates,
//     base64-encode each DER cert, and wrap it in PEM markers.
//   * On Android we scan the known filesystem CA directories and concatenate
//     the PEM blocks we find, skipping any file that doesn't contain a
//     well-formed certificate block (Android's "hash.0" files can carry
//     trailing metadata past the END CERTIFICATE marker).
//   * On iOS, Windows, and everything else we return NULL and document in the
//     public header that the caller must provide their own bundle.
//
// The result is cached in a file-scope std::string guarded by std::call_once,
// so repeated calls are cheap and thread-safe.
//
// No dependency on OpenSSL's base64 helpers — we use a tiny hand-rolled
// RFC 4648 encoder so this TU stays buildable even when SEURAT_CRYPTO=OFF
// (in which case the whole function is still compiled, but never yields
// a useful bundle because the platform extractors are still behind their
// respective `#if`s).
//
// License: MIT.
// =============================================================================

#include "seurat/rtmp.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#if defined(__APPLE__)
  #include <TargetConditionals.h>
  #include <CoreFoundation/CoreFoundation.h>
  #include <Security/Security.h>
#endif

#if defined(__ANDROID__)
  #include <cstdio>
  #include <dirent.h>
  #include <sys/stat.h>
#endif

namespace {

// --------------------------------------------------------------- base64

// Standard RFC 4648 alphabet with '+' / '/'. PEM uses 64-char line wrapping.
constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode_pem(const uint8_t* data, size_t len, std::string& out) {
    // First produce one long unwrapped base64 string, then line-wrap at 64
    // chars. We could emit lines as we go, but the two-pass approach keeps
    // the state machine trivial and the allocations predictable.
    std::string raw;
    raw.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        raw.push_back(kBase64Alphabet[(n >> 18) & 0x3F]);
        raw.push_back(kBase64Alphabet[(n >> 12) & 0x3F]);
        raw.push_back((i + 1 < len) ? kBase64Alphabet[(n >> 6) & 0x3F] : '=');
        raw.push_back((i + 2 < len) ? kBase64Alphabet[n & 0x3F]       : '=');
    }

    constexpr size_t kLineWidth = 64;
    for (size_t i = 0; i < raw.size(); i += kLineWidth) {
        out.append(raw, i, kLineWidth);
        out.push_back('\n');
    }
}

// --------------------------------------------------------------- macOS

#if defined(__APPLE__) && defined(TARGET_OS_OSX) && TARGET_OS_OSX

// macOS has a real system trust store that SecTrustCopyAnchorCertificates
// returns. Each anchor is a SecCertificateRef — we DER-encode it and wrap
// it in PEM markers.
//
// We only emit a cert when SecCertificateCopyData succeeds; any individual
// failure is silently skipped so a single broken anchor doesn't void the
// whole bundle.
void extract_macos(std::string& out) {
    CFArrayRef anchors = nullptr;
    const OSStatus st = ::SecTrustCopyAnchorCertificates(&anchors);
    if (st != errSecSuccess || anchors == nullptr) return;

    const CFIndex count = ::CFArrayGetCount(anchors);
    for (CFIndex i = 0; i < count; ++i) {
        SecCertificateRef cert = reinterpret_cast<SecCertificateRef>(
            const_cast<void*>(::CFArrayGetValueAtIndex(anchors, i)));
        if (!cert) continue;

        CFDataRef der = ::SecCertificateCopyData(cert);
        if (!der) continue;

        const uint8_t* p = ::CFDataGetBytePtr(der);
        const CFIndex  n = ::CFDataGetLength(der);
        if (p && n > 0) {
            out.append("-----BEGIN CERTIFICATE-----\n");
            base64_encode_pem(p, static_cast<size_t>(n), out);
            out.append("-----END CERTIFICATE-----\n");
        }
        ::CFRelease(der);
    }
    ::CFRelease(anchors);
}

#endif  // __APPLE__ && TARGET_OS_OSX

// --------------------------------------------------------------- Android

#if defined(__ANDROID__)

// Android stores each system CA as "<hash>.0" (legacy) or as PEM files
// under the APEX path on Android 14+. The file body is PEM but may carry
// trailing bookkeeping data after the END CERTIFICATE line; we extract
// only the PEM block itself.
//
// Extraction stops at the first path that yields any certs: Android 14's
// APEX store is preferred when available, otherwise we fall back to the
// /system copy (which is normally a bind-mount of the same anchors).
void extract_android(std::string& out) {
    static constexpr const char* kPaths[] = {
        "/apex/com.android.conscrypt/cacerts",
        "/system/etc/security/cacerts",
    };

    for (const char* path : kPaths) {
        DIR* d = ::opendir(path);
        if (!d) continue;

        size_t before = out.size();

        for (struct dirent* ent = ::readdir(d); ent != nullptr;
               ent = ::readdir(d)) {
            if (ent->d_name[0] == '.') continue;

            std::string full = path;
            full.push_back('/');
            full.append(ent->d_name);

            FILE* f = std::fopen(full.c_str(), "rb");
            if (!f) continue;

            std::string body;
            char buf[4096];
            for (;;) {
                const size_t r = std::fread(buf, 1, sizeof(buf), f);
                if (r == 0) break;
                body.append(buf, r);
            }
            std::fclose(f);

            const std::string::size_type begin =
                body.find("-----BEGIN CERTIFICATE-----");
            const std::string::size_type end =
                body.find("-----END CERTIFICATE-----");
            if (begin == std::string::npos || end == std::string::npos
                || end < begin) {
                continue;
            }
            const size_t end_tail = end + std::strlen("-----END CERTIFICATE-----");
            out.append(body, begin, end_tail - begin);
            if (out.empty() || out.back() != '\n') out.push_back('\n');
        }

        ::closedir(d);
        if (out.size() > before) break;  // first populated path wins
    }
}

#endif  // __ANDROID__

// --------------------------------------------------------------- dispatch

std::string        g_cached;
std::once_flag     g_once;

void extract_once() {
#if defined(__APPLE__) && defined(TARGET_OS_OSX) && TARGET_OS_OSX
    extract_macos(g_cached);
#elif defined(__ANDROID__)
    extract_android(g_cached);
#else
    // iOS / Windows / other — no native extractor.
    // Public header documents that callers must supply their own bundle.
#endif
}

}  // namespace

extern "C" const char* seurat_rtmp_platform_ca_bundle_pem(void) {
    std::call_once(g_once, extract_once);
    return g_cached.empty() ? nullptr : g_cached.c_str();
}
