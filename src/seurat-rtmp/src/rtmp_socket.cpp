// =============================================================================
// seurat-rtmp — POSIX TCP transport.
//
// Features:
//   - getaddrinfo() name resolution (IPv4 + IPv6).
//   - Non-blocking connect() with poll() timeout.
//   - Blocking send / recv with SO_SNDTIMEO / SO_RCVTIMEO.
//   - send_all / recv_exact loops (EINTR retry, short-read handling).
//
// `PosixTransport` is exposed via rtmp_internal.h so `TlsTransport`
// (rtmp_tls.cpp) can compose one to do the TCP dial before attaching
// OpenSSL. The factory `make_transport()` dispatches between plain and
// TLS flavours based on the requested URL scheme.
//
// License: MIT.
// =============================================================================

#include "rtmp_internal.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace seurat::rtmp {

namespace {

constexpr uint32_t kDefaultConnectTimeoutMs = 10'000;
constexpr uint32_t kDefaultIoTimeoutMs      = 15'000;

int set_nonblocking(int fd, bool on) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    const int newflags = on ? (flags |  O_NONBLOCK)
                             : (flags & ~O_NONBLOCK);
    return ::fcntl(fd, F_SETFL, newflags);
}

int apply_io_timeout(int fd, uint32_t ms) {
    struct timeval tv;
    tv.tv_sec  = static_cast<time_t>(ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) return -1;
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) return -1;
    return 0;
}

}  // namespace

PosixTransport::PosixTransport() = default;

PosixTransport::~PosixTransport() { close(); }

int PosixTransport::connect_host(const std::string& host, uint16_t port,
                                  uint32_t timeout_ms) {
    if (timeout_ms == 0) timeout_ms = kDefaultConnectTimeoutMs;

    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u",
                   static_cast<unsigned>(port));

    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = nullptr;
    const int gai = ::getaddrinfo(host.c_str(), port_str, &hints, &res);
    if (gai != 0 || !res) {
        return SEURAT_RTMP_E_NETWORK;
    }

    int rc = SEURAT_RTMP_E_NETWORK;
    for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        const int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0) continue;

        // TCP_NODELAY — RTMP is latency-sensitive, disable Nagle.
        int one = 1;
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (set_nonblocking(s, true) != 0) {
            ::close(s);
            continue;
        }

        const int cr = ::connect(s, ai->ai_addr, ai->ai_addrlen);
        if (cr == 0) {
            // immediately connected
        } else if (errno == EINPROGRESS || errno == EINTR) {
            struct pollfd p;
            p.fd      = s;
            p.events  = POLLOUT;
            p.revents = 0;
            const int pr = ::poll(&p, 1, static_cast<int>(timeout_ms));
            if (pr == 0) {
                ::close(s);
                rc = SEURAT_RTMP_E_TIMEOUT;
                continue;
            }
            if (pr < 0) {
                ::close(s);
                continue;
            }
            int so_err = 0;
            socklen_t len = sizeof(so_err);
            if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &so_err, &len) < 0 ||
                so_err != 0) {
                ::close(s);
                continue;
            }
        } else {
            ::close(s);
            continue;
        }

        // Connected. Switch back to blocking + apply send/recv timeouts.
        // (TlsTransport relies on blocking fd semantics so OpenSSL's
        // SSL_connect/SSL_read/SSL_write inherit the same timeouts.)
        if (set_nonblocking(s, false) != 0) {
            ::close(s);
            continue;
        }
        (void)apply_io_timeout(s, kDefaultIoTimeoutMs);

        fd_ = s;
        rc  = SEURAT_RTMP_OK;
        break;
    }

    ::freeaddrinfo(res);
    return rc;
}

int PosixTransport::send_all(const uint8_t* data, size_t len) {
    if (fd_ < 0) return SEURAT_RTMP_E_STATE;

    size_t off = 0;
    while (off < len) {
        const ssize_t n = ::send(fd_, data + off, len - off, 0);
        if (n > 0) {
            off        += static_cast<size_t>(n);
            bytes_sent += static_cast<uint64_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return SEURAT_RTMP_E_TIMEOUT;
        }
        return SEURAT_RTMP_E_NETWORK;
    }
    return SEURAT_RTMP_OK;
}

int PosixTransport::recv_exact(uint8_t* out, size_t len) {
    if (fd_ < 0) return SEURAT_RTMP_E_STATE;

    size_t off = 0;
    while (off < len) {
        const ssize_t n = ::recv(fd_, out + off, len - off, 0);
        if (n > 0) {
            off        += static_cast<size_t>(n);
            bytes_recv += static_cast<uint64_t>(n);
            continue;
        }
        if (n == 0) return SEURAT_RTMP_E_NETWORK;  // EOF mid-message
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return SEURAT_RTMP_E_TIMEOUT;
        }
        return SEURAT_RTMP_E_NETWORK;
    }
    return SEURAT_RTMP_OK;
}

void PosixTransport::close() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

// Factory. Dispatches to the TLS transport when requested and available,
// otherwise returns a plain TCP transport. Returning nullptr for a TLS
// request means the build was compiled without OpenSSL support
// (LEONARDO_CRYPTO=OFF); the caller translates that to E_TLS.
Transport* make_transport(bool use_tls) {
#if defined(SEURAT_RTMP_WITH_TLS) && SEURAT_RTMP_WITH_TLS
    if (use_tls) return make_tls_transport();
#else
    if (use_tls) return nullptr;
#endif
    return new (std::nothrow) PosixTransport();
}

}  // namespace seurat::rtmp
