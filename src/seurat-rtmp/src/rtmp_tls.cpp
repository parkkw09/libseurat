// =============================================================================
// seurat-rtmp — OpenSSL-based TLS transport (Phase D / RTMPS).
//
// Wraps a `PosixTransport` (plain TCP dial) with an OpenSSL `SSL_CTX` +
// `SSL` pair so the RTMP chunk stream runs over TLS. Flow:
//
//   1. Dial TCP via the inner PosixTransport.
//   2. Attach the resulting fd to a BIO with BIO_NOCLOSE — the underlying
//      PosixTransport retains ownership of close().
//   3. Configure SSL_CTX with TLS 1.2 minimum, SNI (required by YouTube,
//      Twitch, SOOP and friends), then run SSL_connect() to finish the
//      TLS handshake. The fd is blocking with SO_SNDTIMEO/SO_RCVTIMEO
//      already applied, so SSL_connect inherits that timeout budget.
//   4. `send_all` / `recv_exact` loop over SSL_write / SSL_read, handling
//      SSL_ERROR_WANT_READ/WRITE (OpenSSL can ask to retry after a
//      renegotiation or partial record), EINTR, and timeout conditions.
//   5. `close()` issues a best-effort SSL_shutdown, frees the SSL/CTX,
//      then delegates the fd close to PosixTransport.
//
// Security note (Phase D baseline):
//   Peer certificate verification is disabled by default in this first
//   cut. OpenSSL's `SSL_CTX_set_default_verify_paths` does not find a
//   usable CA store on iOS/Android without additional platform glue
//   (Security framework / NDK-provided anchors), which would balloon the
//   scope of this change. A future iteration will add a pluggable CA
//   bundle + full hostname verification — see docs/DESIGN.md §8.6.1 and
//   the "Phase E follow-up" roadmap entry.
//
// License: MIT.
// =============================================================================

#include "rtmp_internal.h"

#if defined(SEURAT_RTMP_WITH_TLS) && SEURAT_RTMP_WITH_TLS

#include <cerrno>
#include <climits>
#include <cstring>
#include <new>
#include <string>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace seurat::rtmp {

namespace {

void ensure_openssl_inited() {
    // OpenSSL 3.x: OPENSSL_init_ssl is idempotent and thread-safe. Wrapping
    // it in a function-local static guarantees exactly-once initialisation
    // across all TlsTransport instances and threads.
    static const bool inited = [] {
        ::OPENSSL_init_ssl(
            OPENSSL_INIT_LOAD_SSL_STRINGS |
            OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
            nullptr);
        return true;
    }();
    (void)inited;
}

class TlsTransport : public Transport {
public:
    TlsTransport() = default;
    ~TlsTransport() override { close(); }

    int connect_host(const std::string& host, uint16_t port,
                      uint32_t timeout_ms) override {
        ensure_openssl_inited();

        int rc = posix_.connect_host(host, port, timeout_ms);
        if (rc != SEURAT_RTMP_OK) return rc;

        const int fd = posix_.fd();
        if (fd < 0) return SEURAT_RTMP_E_STATE;

        ctx_ = ::SSL_CTX_new(::TLS_client_method());
        if (!ctx_) return fail();

        ::SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
        ::SSL_CTX_set_mode(ctx_,
            SSL_MODE_AUTO_RETRY |
            SSL_MODE_ENABLE_PARTIAL_WRITE |
            SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

        // Phase D: accept any peer cert. Phase E will plug in a CA bundle
        // + hostname verification via X509_VERIFY_PARAM_set1_host.
        ::SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);

        ssl_ = ::SSL_new(ctx_);
        if (!ssl_) return fail();

        // BIO_NOCLOSE: OpenSSL must NOT call ::close(fd) on SSL_free; the
        // fd is owned by our inner PosixTransport which does the teardown.
        ::BIO* bio = ::BIO_new_socket(fd, BIO_NOCLOSE);
        if (!bio) return fail();
        ::SSL_set_bio(ssl_, bio, bio);

        // SNI is mandatory for all public RTMPS endpoints we target (they
        // share infrastructure across many stream tenants). Without it the
        // server terminates with `unrecognized_name` during handshake.
        ::SSL_set_tlsext_host_name(ssl_, host.c_str());

        if (::SSL_connect(ssl_) != 1) return fail();
        return SEURAT_RTMP_OK;
    }

    int send_all(const uint8_t* data, size_t len) override {
        if (!ssl_) return SEURAT_RTMP_E_STATE;
        size_t off = 0;
        while (off < len) {
            const size_t chunk = (len - off > static_cast<size_t>(INT_MAX))
                                   ? static_cast<size_t>(INT_MAX)
                                   : (len - off);
            const int n = ::SSL_write(ssl_, data + off, static_cast<int>(chunk));
            if (n > 0) {
                off        += static_cast<size_t>(n);
                bytes_sent += static_cast<uint64_t>(n);
                continue;
            }
            const int rc = translate_ssl_error(n);
            if (rc == kRetry) continue;
            return rc;
        }
        return SEURAT_RTMP_OK;
    }

    int recv_exact(uint8_t* out, size_t len) override {
        if (!ssl_) return SEURAT_RTMP_E_STATE;
        size_t off = 0;
        while (off < len) {
            const size_t chunk = (len - off > static_cast<size_t>(INT_MAX))
                                   ? static_cast<size_t>(INT_MAX)
                                   : (len - off);
            const int n = ::SSL_read(ssl_, out + off, static_cast<int>(chunk));
            if (n > 0) {
                off        += static_cast<size_t>(n);
                bytes_recv += static_cast<uint64_t>(n);
                continue;
            }
            const int rc = translate_ssl_error(n);
            if (rc == kRetry) continue;
            return rc;
        }
        return SEURAT_RTMP_OK;
    }

    void close() override {
        if (ssl_) {
            // Best-effort graceful shutdown: we don't wait for peer's
            // close_notify to avoid hanging on misbehaving RTMPS proxies.
            (void)::SSL_shutdown(ssl_);
            ::SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (ctx_) {
            ::SSL_CTX_free(ctx_);
            ctx_ = nullptr;
        }
        posix_.close();
    }

private:
    // Sentinel returned by translate_ssl_error when the caller should
    // simply retry the SSL_read/SSL_write without reporting an error.
    static constexpr int kRetry = 1;

    int translate_ssl_error(int n) {
        const int err = ::SSL_get_error(ssl_, n);
        switch (err) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return kRetry;
            case SSL_ERROR_ZERO_RETURN:
                return SEURAT_RTMP_E_NETWORK;   // peer sent close_notify
            case SSL_ERROR_SYSCALL:
                if (errno == EINTR) return kRetry;
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return SEURAT_RTMP_E_TIMEOUT;
                }
                return SEURAT_RTMP_E_NETWORK;
            default:
                return SEURAT_RTMP_E_NETWORK;
        }
    }

    // Drain the OpenSSL error queue so subsequent ERR_* observers don't
    // trip over stale entries from a failed connect, then tear everything
    // down and return a network-error code to the caller.
    int fail() {
        while (::ERR_get_error() != 0) {}
        close();
        return SEURAT_RTMP_E_NETWORK;
    }

    PosixTransport posix_;
    ::SSL_CTX*     ctx_ = nullptr;
    ::SSL*         ssl_ = nullptr;
};

}  // namespace

Transport* make_tls_transport() {
    return new (std::nothrow) TlsTransport();
}

}  // namespace seurat::rtmp

#endif  // SEURAT_RTMP_WITH_TLS
