// =============================================================================
// seurat-rtmp internal header.
//
// Defines the private client struct and cross-module declarations. Not shipped
// to downstream users.
// =============================================================================

#ifndef SEURAT_RTMP_INTERNAL_H_
#define SEURAT_RTMP_INTERNAL_H_

#include "seurat/rtmp.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace seurat::rtmp {

// ---- URL parse result

struct ParsedUrl {
    bool        tls = false;     // rtmp vs rtmps
    std::string host;
    uint16_t    port = 0;        // 1935 for rtmp, 443 for rtmps by convention
    std::string app;             // first path component
    std::string stream_key;      // remaining path components joined by '/'
    std::string tc_url;          // reconstructed scheme://host[:port]/app
};

// Returns SEURAT_RTMP_OK on success; SEURAT_RTMP_E_URL on parse failure.
int parse_url(const char* url, ParsedUrl* out);

// ---- Socket / TLS transport abstraction.
//
// `Transport` is the common interface used by the RTMP handshake, chunk
// stream, and publish-flow layers. Two concrete implementations exist:
//
//   - PosixTransport    plain TCP (always available).
//   - TlsTransport      OpenSSL BIO/SSL wrapper around a PosixTransport,
//                        compiled only when SEURAT_RTMP_WITH_TLS=1.
//
// The transport type is chosen at client-create time based on the URL
// scheme (`rtmp://` vs `rtmps://`); see `make_transport()` below.

// TLS knobs passed through to TlsTransport at construction time. Mirrors
// the relevant subset of seurat_rtmp_config_t, but uses std::string so the
// owning Client's lifetime dictates the backing storage.
struct TlsOptions {
    std::string ca_bundle_pem;   // empty → default OpenSSL paths only
    bool        insecure = false;  // true → SSL_VERIFY_NONE, no host check
};

class Transport {
public:
    virtual ~Transport() = default;

    virtual int connect_host(const std::string& host, uint16_t port,
                              uint32_t timeout_ms) = 0;
    virtual int send_all(const uint8_t* data, size_t len) = 0;
    virtual int recv_exact(uint8_t* out, size_t len) = 0;
    virtual void close() = 0;

    // Cumulative counters surfaced in seurat_rtmp_stats_t. For TlsTransport
    // these track plaintext (application-level) bytes, not on-the-wire ones.
    uint64_t bytes_sent = 0;
    uint64_t bytes_recv = 0;
};

// Plain TCP transport. Exposed here (not in the anonymous namespace of
// rtmp_socket.cpp) so TlsTransport can compose one internally to perform
// the TCP dial before the TLS handshake.
class PosixTransport : public Transport {
public:
    PosixTransport();
    ~PosixTransport() override;

    int connect_host(const std::string& host, uint16_t port,
                      uint32_t timeout_ms) override;
    int send_all(const uint8_t* data, size_t len) override;
    int recv_exact(uint8_t* out, size_t len) override;
    void close() override;

    // Underlying socket fd (>= 0 once connected, -1 otherwise). Used by
    // TlsTransport to attach OpenSSL's BIO to the already-dialled socket.
    int fd() const { return fd_; }

private:
    int fd_ = -1;
};

// Returns a new heap-allocated transport. When `use_tls` is true and the
// build has SEURAT_RTMP_WITH_TLS=1, yields a TlsTransport configured with
// `tls_opts`; otherwise a PosixTransport (and `tls_opts` is ignored).
// Returns nullptr if TLS was requested but the build was compiled without
// OpenSSL support (caller should surface E_TLS).
Transport* make_transport(bool use_tls, const TlsOptions& tls_opts);

#if defined(SEURAT_RTMP_WITH_TLS) && SEURAT_RTMP_WITH_TLS
// Implemented in rtmp_tls.cpp. Kept separate so plain-RTMP builds (without
// OpenSSL) never pull in TLS code paths.
Transport* make_tls_transport(const TlsOptions& tls_opts);
#endif

// ---- RTMP handshake (Phase B).

int run_handshake(Transport* tr);

// ---- AMF0 encoder (Phase B).

class Amf0Writer {
public:
    std::vector<uint8_t>& buf() { return buf_; }
    const std::vector<uint8_t>& buf() const { return buf_; }

    void put_string(const char* s);
    void put_number(double v);
    void put_boolean(bool v);
    void put_null();
    void begin_object();                              // marker 0x03
    void begin_ecma_array(uint32_t approximate_count);// marker 0x08 + u32be count
    void put_obj_prop_string(const char* k, const char* v);
    void put_obj_prop_number(const char* k, double v);
    void put_obj_prop_boolean(const char* k, bool v);
    void end_object();                                // 00 00 09

private:
    std::vector<uint8_t> buf_;
};

// ---- AMF0 parser (Phase B). Minimal: walks values without materialising a
//      full tree. Used to extract specific fields from server replies.

class Amf0Reader {
public:
    Amf0Reader(const uint8_t* data, size_t len) : p_(data), end_(data + len) {}

    bool eof() const { return p_ >= end_; }
    const uint8_t* cursor() const { return p_; }

    // Read the next top-level marker byte without consuming. -1 on eof.
    int peek_marker() const { return eof() ? -1 : static_cast<int>(*p_); }

    // Read top-level values. Return SEURAT_RTMP_OK + side effect on args,
    // or E_PROTOCOL on truncated / type-mismatched input.
    int read_string(std::string* out);
    int read_number(double* out);
    int read_boolean(bool* out);
    int read_null();
    int read_undefined();

    // Skip exactly one top-level value of any type. Recurses into objects
    // and arrays. Used to advance past things we don't care about.
    int skip_value();

    // If the next value is an object, iterate properties. Each invocation
    // of next_property() fills `key` + positions the cursor at the value.
    // Returns SEURAT_RTMP_OK while iterating, E_STATE (>0) when object-end
    // has been consumed, and negative on parse error.
    int begin_object();
    int next_property(std::string* key, bool* end_of_object);

    // Convenience: in the current object, find a property by name whose
    // value is a string. The cursor is NOT restored after this call — so
    // don't mix with begin_object / next_property loops.
    //
    // Returns SEURAT_RTMP_OK + fills `out` if found, or E_PROTOCOL if the
    // key is missing / not a string.
    int find_object_string(const char* key, std::string* out);

private:
    int read_short_string(std::string* out);

    const uint8_t* p_;
    const uint8_t* end_;
};

// ---- RTMP chunk stream (Phase B).

class ChunkWriter {
public:
    explicit ChunkWriter(Transport* tr);

    // Send a complete RTMP message. `msg_type`, `stream_id`, `timestamp`
    // follow the RTMP 1.0 message header semantics.
    int send_message(uint32_t cs_id,
                      uint8_t  msg_type,
                      uint32_t stream_id,
                      uint32_t timestamp_ms,
                      const uint8_t* payload, size_t payload_len);

    void set_out_chunk_size(uint32_t s) { out_chunk_size_ = s; }
    uint32_t out_chunk_size() const { return out_chunk_size_; }

private:
    Transport* tr_;
    uint32_t   out_chunk_size_ = 128;  // RTMP default pre-handshake
};

// RTMP message surfaced to the application.
struct Message {
    uint8_t              msg_type     = 0;
    uint32_t             stream_id    = 0;
    uint32_t             timestamp_ms = 0;
    std::vector<uint8_t> payload;
};

class ChunkReader {
public:
    explicit ChunkReader(Transport* tr) : tr_(tr) {}

    // Read the next complete application-level RTMP message. Protocol
    // control messages (cs_id = 2) are consumed transparently.
    int read_message(Message* msg);

    void set_in_chunk_size(uint32_t s) { in_chunk_size_ = s; }
    uint32_t in_chunk_size() const { return in_chunk_size_; }

private:
    int handle_control(const Message& msg);

    struct CsState {
        uint32_t             msg_length    = 0;
        uint8_t              msg_type_id   = 0;
        uint32_t             msg_stream_id = 0;
        uint32_t             abs_timestamp = 0;
        bool                 has_ext_ts    = false;
        std::vector<uint8_t> partial;
    };

    Transport*                             tr_;
    uint32_t                               in_chunk_size_ = 128;
    std::unordered_map<uint32_t, CsState>  state_;
};

// ----------------------------------------------------------- Client struct
//
// Kept opaque to the public API (forward-declared as seurat_rtmp_client).

struct Client {
    seurat_rtmp_config_t cfg{};     // copies of const char* below
    std::string cfg_url;
    std::string cfg_stream_key;
    std::string cfg_flash_ver;
    std::string cfg_tc_url;
    std::string cfg_ca_bundle_pem;  // RTMPS CA trust anchors, opaque PEM blob

    ParsedUrl   parsed;
    Transport*  transport = nullptr;

    std::unique_ptr<ChunkWriter> writer;
    std::unique_ptr<ChunkReader> reader;

    // Assigned by createStream response.
    uint32_t    publish_stream_id = 0;

    // Running AMF0 transaction id. 1 = connect, 2+ for subsequent commands.
    double      next_tx_id = 1.0;

    bool        connected        = false;
    bool        video_seq_sent   = false;
    bool        audio_seq_sent   = false;
    bool        metadata_sent    = false;

    seurat_rtmp_stats_t stats{};
};

// ---- Publish-flow helpers (implemented in rtmp.cpp).

int send_set_chunk_size(Client* c, uint32_t size);
int send_amf_command(Client* c, uint32_t cs_id, uint32_t stream_id,
                      const Amf0Writer& w);
int recv_until_command(Client* c, double wanted_tx_id, Message* out);

}  // namespace seurat::rtmp

// The opaque struct the public API speaks to is a thin alias.
struct seurat_rtmp_client : ::seurat::rtmp::Client {};

#endif  // SEURAT_RTMP_INTERNAL_H_
