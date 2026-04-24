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
// Security model (Phase E):
//   Peer certificate verification is ON by default. The trust store is
//   populated from three sources in priority order:
//
//     1. User-provided in-memory PEM bundle (`TlsOptions::ca_bundle_pem`).
//        Each PEM-encoded certificate is parsed and added to the CTX's
//        X509_STORE. This is the recommended path for iOS/Android where
//        OpenSSL's default paths resolve to nothing useful.
//     2. OpenSSL's compiled-in default verify paths (useful on typical
//        Linux / macOS dev hosts; empty on iOS/Android/Windows).
//
//   Hostname verification uses `X509_VERIFY_PARAM_set1_host` on the SSL
//   object (not the CTX), which validates the SNI host against SAN/CN
//   per RFC 6125. `X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS` is set so wildcard
//   certs only match at the leftmost label.
//
//   An opt-in escape hatch `TlsOptions::insecure` downgrades back to
//   SSL_VERIFY_NONE + no hostname check. Intended only for bring-up
//   against self-signed test servers (e.g. local mediamtx). A release
//   build should never ship with that set.
//
//   Phase E.3 will add platform-native CA bundle extraction helpers
//   (iOS SecTrust / Android cacerts / Windows CertOpenStore) that produce
//   a PEM blob the caller can feed into `ca_bundle_pem`.
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
#include <utility>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

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
    explicit TlsTransport(TlsOptions opts) : opts_(std::move(opts)) {}
    ~TlsTransport() override { close(); }

    int connect_host(const std::string& host, uint16_t port,
                      uint32_t timeout_ms) override {
        ensure_openssl_inited();

        int rc = posix_.connect_host(host, port, timeout_ms);
        if (rc != SEURAT_RTMP_OK) return rc;

        const int fd = posix_.fd();
        if (fd < 0) return SEURAT_RTMP_E_STATE;

        ctx_ = ::SSL_CTX_new(::TLS_client_method());
        if (!ctx_) return fail_tls();

        ::SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
        ::SSL_CTX_set_mode(ctx_,
            SSL_MODE_AUTO_RETRY |
            SSL_MODE_ENABLE_PARTIAL_WRITE |
            SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

        // -------- trust store + verify mode
        if (opts_.insecure) {
            // Opt-in development mode. Accepts any cert, skips hostname
            // check. Keep this path behaviourally identical to the Phase
            // D baseline so legacy tests against self-signed servers
            // continue to work.
            ::SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
        } else {
            // Default: verify on. Caller must have supplied CA anchors
            // somewhere — user-provided PEM wins, OpenSSL's default paths
            // act as a best-effort fallback (empty on iOS/Android).
            if (!opts_.ca_bundle_pem.empty()) {
                const int added = load_ca_bundle_pem(ctx_, opts_.ca_bundle_pem);
                if (added <= 0) return fail_tls();
            }
            // Default paths are additive, not exclusive of the PEM above.
            // On iOS/Android this is a no-op; on macOS/Linux it may pick
            // up a Homebrew / distro CA store.
            ::SSL_CTX_set_default_verify_paths(ctx_);
            ::SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
        }

        ssl_ = ::SSL_new(ctx_);
        if (!ssl_) return fail_tls();

        // Hostname verification (MITM defense). Must be set on the SSL
        // object, not the CTX, because `host` is per-connection. OpenSSL
        // enforces this during SSL_connect — handshake fails if the leaf
        // cert's SAN / CN doesn't match.
        if (!opts_.insecure) {
            ::X509_VERIFY_PARAM* param = ::SSL_get0_param(ssl_);
            ::X509_VERIFY_PARAM_set_hostflags(
                param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            if (::X509_VERIFY_PARAM_set1_host(param, host.c_str(),
                                                host.size()) != 1) {
                return fail_tls();
            }
        }

        // BIO_NOCLOSE: OpenSSL must NOT call ::close(fd) on SSL_free; the
        // fd is owned by our inner PosixTransport which does the teardown.
        ::BIO* bio = ::BIO_new_socket(fd, BIO_NOCLOSE);
        if (!bio) return fail_tls();
        ::SSL_set_bio(ssl_, bio, bio);

        // SNI is mandatory for all public RTMPS endpoints we target (they
        // share infrastructure across many stream tenants). Without it the
        // server terminates with `unrecognized_name` during handshake.
        ::SSL_set_tlsext_host_name(ssl_, host.c_str());

        if (::SSL_connect(ssl_) != 1) return fail_tls();
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

    // Parse one or more concatenated PEM certificates from `pem` and
    // push them into the CTX's trust store. Returns the number of certs
    // successfully added (>0 on success), -1 on hard allocation error.
    // Malformed entries are skipped silently — callers expecting strict
    // validation should audit the `added > 0` return before SSL_connect.
    static int load_ca_bundle_pem(::SSL_CTX* ctx, const std::string& pem) {
        ::BIO* bio = ::BIO_new_mem_buf(pem.data(),
                                         static_cast<int>(pem.size()));
        if (!bio) return -1;

        ::X509_STORE* store = ::SSL_CTX_get_cert_store(ctx);
        if (!store) { ::BIO_free(bio); return -1; }

        int added = 0;
        while (true) {
            ::X509* cert =
                ::PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            if (!cert) break;
            if (::X509_STORE_add_cert(store, cert) == 1) ++added;
            ::X509_free(cert);
        }
        // Drain any "no start line" EOF errors left in the queue by
        // PEM_read_bio_X509 so they don't confuse subsequent diagnostics.
        while (::ERR_get_error() != 0) {}
        ::BIO_free(bio);
        return added;
    }

    // Drain the OpenSSL error queue so subsequent ERR_* observers don't
    // trip over stale entries from a failed connect, then tear everything
    // down and surface a TLS-specific error code to the caller. Used for
    // all setup + handshake + verification failures.
    int fail_tls() {
        while (::ERR_get_error() != 0) {}
        close();
        return SEURAT_RTMP_E_TLS;
    }

    TlsOptions     opts_;
    PosixTransport posix_;
    ::SSL_CTX*     ctx_ = nullptr;
    ::SSL*         ssl_ = nullptr;
};

}  // namespace

Transport* make_tls_transport(const TlsOptions& tls_opts) {
    return new (std::nothrow) TlsTransport(tls_opts);
}

}  // namespace seurat::rtmp

#endif  // SEURAT_RTMP_WITH_TLS
