// =============================================================================
// seurat-rtmp — top-level client glue (Phase B: publish negotiation).
//
// This file implements the lifecycle + URL parsing + high-level publish flow
// orchestration. The actual wire-level work (handshake, AMF0, chunking, TLS)
// lives in sibling files (rtmp_socket.cpp / rtmp_handshake.cpp /
// rtmp_chunk.cpp / rtmp_amf0.cpp).
//
// Publish negotiation sequence implemented by seurat_rtmp_connect():
//
//   1. TCP connect              (rtmp_socket.cpp)
//   2. RTMP 1.0 simple handshake (rtmp_handshake.cpp)
//   3. SetChunkSize(4096)        — protocol control, cs_id=2
//   4. AMF0 "connect" (tx=1)
//      wait for _result(tx=1) with NetConnection.Connect.Success
//   5. AMF0 "releaseStream" (tx=2, fire-and-forget)
//      AMF0 "FCPublish"     (tx=3, fire-and-forget)
//      AMF0 "createStream"  (tx=4)
//      wait for _result(tx=4) → extract numeric stream id
//   6. AMF0 "publish"  on that stream id (tx=5, args: null, key, "live")
//      wait for onStatus with code NetStream.Publish.Start
//
// License: MIT.
// =============================================================================

#include "seurat/rtmp.h"
#include "rtmp_internal.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace srt = seurat::rtmp;

namespace {

constexpr const char* kDefaultFlashVer = "libseurat/0.2 (FMLE-compatible)";

// -------- config copy helpers

void copy_config(srt::Client* c, const seurat_rtmp_config_t* cfg) {
    if (cfg->url)        c->cfg_url        = cfg->url;
    if (cfg->stream_key) c->cfg_stream_key = cfg->stream_key;
    if (cfg->flash_ver)  c->cfg_flash_ver  = cfg->flash_ver;
    if (cfg->tc_url)     c->cfg_tc_url     = cfg->tc_url;

    // Rewrite the public cfg's char* fields so they point into the stable
    // std::string storage; that way the user's original buffers can go out
    // of scope immediately after seurat_rtmp_create() returns.
    c->cfg = *cfg;
    c->cfg.url        = c->cfg_url.empty()        ? nullptr : c->cfg_url.c_str();
    c->cfg.stream_key = c->cfg_stream_key.empty() ? nullptr : c->cfg_stream_key.c_str();
    c->cfg.flash_ver  = c->cfg_flash_ver.empty()  ? nullptr : c->cfg_flash_ver.c_str();
    c->cfg.tc_url     = c->cfg_tc_url.empty()     ? nullptr : c->cfg_tc_url.c_str();
}

}  // namespace

// ========================================================= URL parsing

namespace seurat::rtmp {

// Minimal but permissive parser. Accepts:
//   rtmp://host[:port]/app[/key-part[/key-part...]]
//   rtmps://host[:port]/app[/key-part[/key-part...]]
int parse_url(const char* url, ParsedUrl* out) {
    if (!url || !out) return SEURAT_RTMP_E_INVALID_ARG;

    std::string s(url);
    constexpr const char* kRtmp  = "rtmp://";
    constexpr const char* kRtmps = "rtmps://";

    if (s.rfind(kRtmps, 0) == 0) {
        out->tls = true;
        out->port = 443;
        s.erase(0, std::strlen(kRtmps));
    } else if (s.rfind(kRtmp, 0) == 0) {
        out->tls = false;
        out->port = 1935;
        s.erase(0, std::strlen(kRtmp));
    } else {
        return SEURAT_RTMP_E_URL;
    }

    // Split authority and path.
    const auto slash = s.find('/');
    const std::string authority = (slash == std::string::npos) ? s : s.substr(0, slash);
    const std::string path      = (slash == std::string::npos) ? "" : s.substr(slash + 1);

    if (authority.empty()) return SEURAT_RTMP_E_URL;

    // host[:port]
    const auto colon = authority.find(':');
    if (colon == std::string::npos) {
        out->host = authority;
    } else {
        out->host = authority.substr(0, colon);
        const std::string p = authority.substr(colon + 1);
        if (p.empty()) return SEURAT_RTMP_E_URL;
        for (char ch : p) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return SEURAT_RTMP_E_URL;
            }
        }
        const long v = std::strtol(p.c_str(), nullptr, 10);
        if (v <= 0 || v > 65535) return SEURAT_RTMP_E_URL;
        out->port = static_cast<uint16_t>(v);
    }
    if (out->host.empty()) return SEURAT_RTMP_E_URL;

    // path -> app / stream_key
    if (!path.empty()) {
        const auto first_slash = path.find('/');
        if (first_slash == std::string::npos) {
            out->app = path;
            out->stream_key.clear();
        } else {
            out->app = path.substr(0, first_slash);
            out->stream_key = path.substr(first_slash + 1);
        }
    }

    // Reconstruct tcUrl. Default port -> omit, explicit port -> include.
    const bool default_port = (out->tls && out->port == 443) ||
                               (!out->tls && out->port == 1935);
    out->tc_url  = out->tls ? "rtmps://" : "rtmp://";
    out->tc_url += out->host;
    if (!default_port) {
        out->tc_url += ":";
        out->tc_url += std::to_string(out->port);
    }
    if (!out->app.empty()) {
        out->tc_url += "/";
        out->tc_url += out->app;
    }
    return SEURAT_RTMP_OK;
}

}  // namespace seurat::rtmp

// ========================================================= public API

seurat_rtmp_client_t* seurat_rtmp_create(const seurat_rtmp_config_t* cfg) {
    if (!cfg || !cfg->url) return nullptr;

    auto* c = new (std::nothrow) seurat_rtmp_client();
    if (!c) return nullptr;

    copy_config(c, cfg);

    if (c->cfg_flash_ver.empty()) {
        c->cfg_flash_ver = kDefaultFlashVer;
        c->cfg.flash_ver = c->cfg_flash_ver.c_str();
    }

    if (srt::parse_url(c->cfg.url, &c->parsed) != SEURAT_RTMP_OK) {
        delete c;
        return nullptr;
    }
    // Optional stream_key override wins.
    if (!c->cfg_stream_key.empty()) {
        c->parsed.stream_key = c->cfg_stream_key;
    }
    // Optional tcUrl override wins.
    if (!c->cfg_tc_url.empty()) {
        c->parsed.tc_url = c->cfg_tc_url;
    }

    // Pick plain-TCP or TLS transport based on the parsed scheme. When the
    // build is compiled without OpenSSL (SEURAT_CRYPTO=OFF) and rtmps://
    // was requested, make_transport returns nullptr and we reject early —
    // the caller can't distinguish "alloc failed" from "TLS not built" at
    // the create boundary, so we surface a null client either way.
    c->transport = srt::make_transport(c->parsed.tls);
    if (!c->transport) {
        delete c;
        return nullptr;
    }
    return c;
}

void seurat_rtmp_destroy(seurat_rtmp_client_t* c) {
    if (!c) return;
    if (c->transport) {
        c->transport->close();
        delete c->transport;
    }
    delete c;
}

const char* seurat_rtmp_strerror(int code) {
    switch (code) {
        case SEURAT_RTMP_OK:              return "ok";
        case SEURAT_RTMP_E_INVALID_ARG:   return "invalid argument";
        case SEURAT_RTMP_E_STATE:         return "invalid state / call order";
        case SEURAT_RTMP_E_ALLOC:         return "allocation failed";
        case SEURAT_RTMP_E_NETWORK:       return "network I/O error";
        case SEURAT_RTMP_E_PROTOCOL:      return "peer violated RTMP protocol";
        case SEURAT_RTMP_E_REJECTED:      return "server rejected publish";
        case SEURAT_RTMP_E_TIMEOUT:       return "timed out";
        case SEURAT_RTMP_E_UNSUPPORTED:   return "feature not yet implemented";
        case SEURAT_RTMP_E_TLS:           return "RTMPS requested but build has SEURAT_CRYPTO=OFF";
        case SEURAT_RTMP_E_URL:           return "malformed rtmp(s):// url";
        default:                          return "unknown error";
    }
}

// =============================================================================
// Publish-flow helpers (internal).
// =============================================================================

namespace seurat::rtmp {

namespace {

// Big-endian uint32 writer.
inline void put_u32be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8);
    p[3] = static_cast<uint8_t>(v);
}

constexpr uint8_t  MSG_SET_CHUNK_SIZE = 1;
constexpr uint8_t  MSG_AMF0_COMMAND    = 20;
constexpr uint8_t  MSG_AMF0_DATA       = 18;
constexpr uint32_t CS_PROTOCOL         = 2;
constexpr uint32_t CS_INVOKE           = 3;

constexpr uint32_t kNegotiatedChunkSize = 4096;

}  // namespace

int send_set_chunk_size(Client* c, uint32_t size) {
    if (!c || !c->writer) return SEURAT_RTMP_E_STATE;
    uint8_t buf[4];
    put_u32be(buf, size & 0x7FFFFFFFu);

    // Protocol control messages go out with the *default* chunk size (128)
    // because this is the very message that changes it. Save + restore.
    const uint32_t prev = c->writer->out_chunk_size();
    c->writer->set_out_chunk_size(128);
    int rc = c->writer->send_message(CS_PROTOCOL, MSG_SET_CHUNK_SIZE,
                                      /*stream_id=*/0, /*ts=*/0, buf, 4);
    if (rc == SEURAT_RTMP_OK) {
        c->writer->set_out_chunk_size(size);
    } else {
        c->writer->set_out_chunk_size(prev);
    }
    return rc;
}

int send_amf_command(Client* c, uint32_t cs_id, uint32_t stream_id,
                      const Amf0Writer& w) {
    if (!c || !c->writer) return SEURAT_RTMP_E_STATE;
    const auto& b = w.buf();
    return c->writer->send_message(cs_id, MSG_AMF0_COMMAND, stream_id, 0,
                                     b.data(), b.size());
}

// Read messages until we see an AMF0 command whose first value is
// `_result` / `_error` / `onStatus`, AND the transaction id matches
// `wanted_tx_id` (set wanted_tx_id < 0 to accept any).
//
// On success the caller receives the raw message so it can re-parse the
// payload for fields specific to the command (e.g., stream id from
// createStream's _result).
int recv_until_command(Client* c, double wanted_tx_id, Message* out) {
    if (!c || !c->reader || !out) return SEURAT_RTMP_E_STATE;

    for (;;) {
        int rc = c->reader->read_message(out);
        if (rc != SEURAT_RTMP_OK) return rc;

        if (out->msg_type != MSG_AMF0_COMMAND) continue;

        Amf0Reader r(out->payload.data(), out->payload.size());
        std::string cmd;
        rc = r.read_string(&cmd);
        if (rc != SEURAT_RTMP_OK) continue;

        double tx = 0.0;
        rc = r.read_number(&tx);
        if (rc != SEURAT_RTMP_OK) continue;

        if (wanted_tx_id >= 0.0 && tx != wanted_tx_id) {
            // Unrelated command (possibly onBWDone, _result for an older
            // transaction). Drop and keep reading.
            continue;
        }

        // Rewind payload pointer for the caller by re-using the same Message.
        return SEURAT_RTMP_OK;
    }
}

namespace {

// Build the AMF0 "connect" command payload.
void build_connect(Amf0Writer& w, double tx, const ParsedUrl& url,
                    const std::string& flash_ver) {
    w.put_string("connect");
    w.put_number(tx);
    w.begin_object();
    w.put_obj_prop_string("app", url.app.c_str());
    w.put_obj_prop_string("type", "nonprivate");
    w.put_obj_prop_string("flashVer", flash_ver.c_str());
    w.put_obj_prop_string("tcUrl", url.tc_url.c_str());
    w.put_obj_prop_boolean("fpad", false);
    w.put_obj_prop_number("capabilities", 15.0);
    w.put_obj_prop_number("audioCodecs", 3191.0);    // all AAC + MP3 + Speex
    w.put_obj_prop_number("videoCodecs",  252.0);    // all incl. H.264
    w.put_obj_prop_number("videoFunction", 1.0);     // seek
    w.end_object();
}

void build_simple_cmd(Amf0Writer& w, const char* name, double tx,
                       const std::string& arg) {
    w.put_string(name);
    w.put_number(tx);
    w.put_null();
    w.put_string(arg.c_str());
}

void build_create_stream(Amf0Writer& w, double tx) {
    w.put_string("createStream");
    w.put_number(tx);
    w.put_null();
}

void build_publish(Amf0Writer& w, double tx, const std::string& key,
                    const char* mode = "live") {
    w.put_string("publish");
    w.put_number(tx);
    w.put_null();
    w.put_string(key.c_str());
    w.put_string(mode);
}

// Validate a NetConnection.Connect.Success / NetStream.Publish.Start from
// a command response's info object.
int check_status_code(const Message& m, const char* expected_code_prefix) {
    Amf0Reader r(m.payload.data(), m.payload.size());
    std::string cmd;
    if (r.read_string(&cmd) != SEURAT_RTMP_OK) return SEURAT_RTMP_E_PROTOCOL;

    double tx = 0.0;
    if (r.read_number(&tx) != SEURAT_RTMP_OK) return SEURAT_RTMP_E_PROTOCOL;

    if (cmd == "_error") return SEURAT_RTMP_E_REJECTED;

    // Next value may be: null | object | array — we accept any, and look
    // inside objects for level / code.
    while (!r.eof()) {
        const int mk = r.peek_marker();
        if (mk == 0x05) {  // null
            (void)r.read_null();
            continue;
        }
        if (mk == 0x03) {  // object
            if (r.begin_object() != SEURAT_RTMP_OK) return SEURAT_RTMP_E_PROTOCOL;

            // Iterate and capture level / code.
            std::string level, code;
            for (;;) {
                std::string key;
                bool end_of_obj = false;
                int rc = r.next_property(&key, &end_of_obj);
                if (rc != SEURAT_RTMP_OK) return rc;
                if (end_of_obj) break;

                if (key == "level" && r.peek_marker() == 0x02) {
                    (void)r.read_string(&level);
                } else if (key == "code" && r.peek_marker() == 0x02) {
                    (void)r.read_string(&code);
                } else {
                    rc = r.skip_value();
                    if (rc != SEURAT_RTMP_OK) return rc;
                }
            }
            if (!code.empty()) {
                if (level == "error") return SEURAT_RTMP_E_REJECTED;
                // Accept if prefix matches.
                if (std::strncmp(code.c_str(), expected_code_prefix,
                                  std::strlen(expected_code_prefix)) == 0) {
                    return SEURAT_RTMP_OK;
                }
                // Known non-success for our prefix → reject.
                return SEURAT_RTMP_E_REJECTED;
            }
            // Object without code — keep scanning following values.
            continue;
        }
        // Skip unrecognised top-level value.
        if (r.skip_value() != SEURAT_RTMP_OK) return SEURAT_RTMP_E_PROTOCOL;
    }
    // No code field anywhere — treat as "success" conservatively for _result
    // replies that omit an info object (rare but legal).
    return SEURAT_RTMP_OK;
}

// Extract the stream id from a createStream _result payload:
//   "_result", tx_id, null, stream_id(Number)
int extract_stream_id(const Message& m, uint32_t* out) {
    Amf0Reader r(m.payload.data(), m.payload.size());
    std::string cmd;
    if (r.read_string(&cmd) != SEURAT_RTMP_OK) return SEURAT_RTMP_E_PROTOCOL;
    if (cmd == "_error") return SEURAT_RTMP_E_REJECTED;

    double tx = 0.0;
    if (r.read_number(&tx) != SEURAT_RTMP_OK) return SEURAT_RTMP_E_PROTOCOL;

    // Arguments object (may be null)
    if (r.peek_marker() == 0x05) (void)r.read_null();
    else if (r.skip_value() != SEURAT_RTMP_OK) return SEURAT_RTMP_E_PROTOCOL;

    double sid = 0.0;
    if (r.read_number(&sid) != SEURAT_RTMP_OK) return SEURAT_RTMP_E_PROTOCOL;

    if (sid < 0 || sid > static_cast<double>(UINT32_MAX)) {
        return SEURAT_RTMP_E_PROTOCOL;
    }
    *out = static_cast<uint32_t>(sid);
    return SEURAT_RTMP_OK;
}

// Wait specifically for an onStatus(NetStream.Publish.Start) on the publish
// stream. Also tolerates informational onStatus messages preceding it.
int wait_publish_start(Client* c) {
    if (!c || !c->reader) return SEURAT_RTMP_E_STATE;

    for (;;) {
        Message m;
        int rc = c->reader->read_message(&m);
        if (rc != SEURAT_RTMP_OK) return rc;
        if (m.msg_type != MSG_AMF0_COMMAND) continue;

        Amf0Reader r(m.payload.data(), m.payload.size());
        std::string cmd;
        if (r.read_string(&cmd) != SEURAT_RTMP_OK) continue;
        if (cmd != "onStatus" && cmd != "_error") continue;

        const int ck = check_status_code(m, "NetStream.Publish.Start");
        if (ck == SEURAT_RTMP_OK)       return SEURAT_RTMP_OK;
        if (ck == SEURAT_RTMP_E_REJECTED) return SEURAT_RTMP_E_REJECTED;
        // Otherwise (informational, mismatched level) keep waiting.
    }
}

}  // namespace

}  // namespace seurat::rtmp

// ---------------------------------------------------------------- connect / IO

int seurat_rtmp_connect(seurat_rtmp_client_t* c) {
    if (!c) return SEURAT_RTMP_E_INVALID_ARG;
    if (c->connected) return SEURAT_RTMP_E_STATE;
    if (!c->transport) return SEURAT_RTMP_E_ALLOC;

    const uint32_t connect_timeout =
        c->cfg.connect_timeout_ms ? c->cfg.connect_timeout_ms : 10'000u;

    // 1. TCP (+ TLS handshake when rtmps://) connect
    int rc = c->transport->connect_host(c->parsed.host, c->parsed.port,
                                          connect_timeout);
    if (rc != SEURAT_RTMP_OK) return rc;

    // 2. RTMP 1.0 handshake
    rc = srt::run_handshake(c->transport);
    if (rc != SEURAT_RTMP_OK) {
        c->transport->close();
        return rc;
    }

    // 3. Chunk writer/reader set-up
    c->writer.reset(new (std::nothrow) srt::ChunkWriter(c->transport));
    c->reader.reset(new (std::nothrow) srt::ChunkReader(c->transport));
    if (!c->writer || !c->reader) {
        c->transport->close();
        return SEURAT_RTMP_E_ALLOC;
    }

    // 4. SetChunkSize (both directions). We proactively enlarge our sending
    //    chunk size; the server tells us its own size through a
    //    protocol-control message consumed transparently by ChunkReader.
    rc = srt::send_set_chunk_size(c, srt::kNegotiatedChunkSize);
    if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }

    // 5. connect command (tx=1)
    srt::Amf0Writer w;
    const double tx_connect = c->next_tx_id++;
    srt::build_connect(w, tx_connect, c->parsed, c->cfg_flash_ver);
    rc = srt::send_amf_command(c, srt::CS_INVOKE, /*stream_id=*/0, w);
    if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }

    {
        srt::Message resp;
        rc = srt::recv_until_command(c, tx_connect, &resp);
        if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }
        rc = srt::check_status_code(resp, "NetConnection.Connect.Success");
        if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }
    }

    // 6. releaseStream + FCPublish + createStream
    const std::string& key = c->parsed.stream_key;

    {
        srt::Amf0Writer rs;
        const double tx = c->next_tx_id++;
        srt::build_simple_cmd(rs, "releaseStream", tx, key);
        rc = srt::send_amf_command(c, srt::CS_INVOKE, 0, rs);
        if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }
    }
    {
        srt::Amf0Writer fc;
        const double tx = c->next_tx_id++;
        srt::build_simple_cmd(fc, "FCPublish", tx, key);
        rc = srt::send_amf_command(c, srt::CS_INVOKE, 0, fc);
        if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }
    }

    const double tx_create = c->next_tx_id++;
    {
        srt::Amf0Writer cs;
        srt::build_create_stream(cs, tx_create);
        rc = srt::send_amf_command(c, srt::CS_INVOKE, 0, cs);
        if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }
    }

    {
        srt::Message resp;
        rc = srt::recv_until_command(c, tx_create, &resp);
        if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }
        uint32_t sid = 0;
        rc = srt::extract_stream_id(resp, &sid);
        if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }
        c->publish_stream_id = sid;
    }

    // 7. publish
    {
        srt::Amf0Writer pu;
        const double tx = c->next_tx_id++;
        srt::build_publish(pu, tx, key, "live");
        rc = srt::send_amf_command(c, srt::CS_INVOKE, c->publish_stream_id, pu);
        if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }
    }

    rc = srt::wait_publish_start(c);
    if (rc != SEURAT_RTMP_OK) { c->transport->close(); return rc; }

    c->connected = true;
    return SEURAT_RTMP_OK;
}

int seurat_rtmp_disconnect(seurat_rtmp_client_t* c) {
    if (!c) return SEURAT_RTMP_E_INVALID_ARG;
    if (!c->connected) return SEURAT_RTMP_OK;

    // Best-effort FCUnpublish + deleteStream. We ignore errors since the
    // close below will tear the socket down regardless.
    if (c->writer && c->publish_stream_id != 0) {
        srt::Amf0Writer w;
        const double tx = c->next_tx_id++;
        srt::build_simple_cmd(w, "FCUnpublish", tx, c->parsed.stream_key);
        (void)srt::send_amf_command(c, srt::CS_INVOKE, 0, w);
    }
    if (c->writer && c->publish_stream_id != 0) {
        srt::Amf0Writer w;
        w.put_string("deleteStream");
        w.put_number(c->next_tx_id++);
        w.put_null();
        w.put_number(static_cast<double>(c->publish_stream_id));
        (void)srt::send_amf_command(c, srt::CS_INVOKE, 0, w);
    }

    if (c->transport) c->transport->close();
    c->connected = false;
    c->publish_stream_id = 0;
    return SEURAT_RTMP_OK;
}

// =============================================================================
// Phase C — AV payload helpers.
//
// RTMP payloads for audio (type 8), video (type 9) and data (type 18) are the
// exact same byte layout as FLV tag bodies. We build the tag body bytes here
// rather than routing through seurat-flv (which prepends/appends a FLV tag
// envelope that's redundant once we hand the buffer to a RTMP chunk). The
// framing is ~15 lines per payload kind, so inline production is simpler and
// has lower overhead than tunneling through a writer callback.
//
// References:
//   - Adobe FLV File Format Specification v10.1 (2008-11), §E.4.2.1 (video),
//     §E.4.2.1 (audio), §E.4.3 (SCRIPTDATA).
//   - ISO/IEC 14496-15 §5.2.4.1.1 (AVCDecoderConfigurationRecord).
// =============================================================================

namespace seurat::rtmp {

namespace {

constexpr uint8_t  MSG_AUDIO = 8;
constexpr uint8_t  MSG_VIDEO = 9;
constexpr uint32_t CS_AUDIO  = 4;
constexpr uint32_t CS_DATA   = 5;
constexpr uint32_t CS_VIDEO  = 6;

// ---- 3-byte big-endian helper (FLV composition time field).
inline void put3be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >>  8));
    v.push_back(static_cast<uint8_t>(x));
}

inline void put2be(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

// ---- Build an AVCDecoderConfigurationRecord from a single SPS and PPS pair.
//
// Layout (ISO 14496-15 §5.2.4.1.1):
//
//     configurationVersion   = 1              (1 byte)
//     AVCProfileIndication   = SPS[1]         (1 byte)
//     profile_compatibility  = SPS[2]         (1 byte)
//     AVCLevelIndication     = SPS[3]         (1 byte)
//     0xFC | lengthSizeMinusOne (=3)          (1 byte = 0xFF)
//     0xE0 | numOfSPS (=1)                    (1 byte = 0xE1)
//     spsLength (u16be) + sps                 (2 + N bytes)
//     numOfPPS (=1)                           (1 byte)
//     ppsLength (u16be) + pps                 (2 + M bytes)
//
// Returns SEURAT_RTMP_OK on success; E_INVALID_ARG on short SPS.
int build_avcc_record(const uint8_t* sps, size_t sps_len,
                       const uint8_t* pps, size_t pps_len,
                       std::vector<uint8_t>* out) {
    if (!sps || sps_len < 4 || !pps || pps_len == 0 ||
        sps_len > 0xFFFFu || pps_len > 0xFFFFu) {
        return SEURAT_RTMP_E_INVALID_ARG;
    }
    out->reserve(11 + sps_len + pps_len);
    out->push_back(0x01);          // configurationVersion
    out->push_back(sps[1]);        // profile
    out->push_back(sps[2]);        // profile_compatibility
    out->push_back(sps[3]);        // level
    out->push_back(0xFF);          // lengthSizeMinusOne = 3
    out->push_back(0xE1);          // 1 SPS
    put2be(*out, static_cast<uint16_t>(sps_len));
    out->insert(out->end(), sps, sps + sps_len);
    out->push_back(0x01);          // 1 PPS
    put2be(*out, static_cast<uint16_t>(pps_len));
    out->insert(out->end(), pps, pps + pps_len);
    return SEURAT_RTMP_OK;
}

}  // namespace

}  // namespace seurat::rtmp

// ---------------------------------------------------------------- metadata

int seurat_rtmp_send_metadata(seurat_rtmp_client_t* c,
                               int video_width, int video_height,
                               double framerate, int video_bitrate_bps,
                               int audio_sample_rate, int audio_channels,
                               int audio_bitrate_bps) {
    if (!c || !c->writer) return SEURAT_RTMP_E_INVALID_ARG;
    if (!c->connected)    return SEURAT_RTMP_E_STATE;

    // RTMP publish uses the "@setDataFrame" convention so the server can
    // distinguish publisher-supplied metadata from its own onMetaData.
    srt::Amf0Writer w;
    w.put_string("@setDataFrame");
    w.put_string("onMetaData");

    // Count properties up-front for the ECMA-array hint (decoders tolerate
    // mismatches but an honest hint keeps logs clean).
    uint32_t props = 0;
    if (video_width > 0 && video_height > 0) props += 3;  // w/h/codecid
    if (framerate > 0.0)                     props += 1;
    if (video_bitrate_bps > 0)               props += 1;
    if (audio_sample_rate > 0)               props += 1;
    if (audio_channels > 0)                  props += 2;  // channels + stereo
    if (audio_bitrate_bps > 0)               props += 1;
    props += 1;  // audiocodecid (AAC constant)
    props += 1;  // encoder identity string

    w.begin_ecma_array(props);

    if (video_width > 0 && video_height > 0) {
        w.put_obj_prop_number("width",        static_cast<double>(video_width));
        w.put_obj_prop_number("height",       static_cast<double>(video_height));
        w.put_obj_prop_number("videocodecid", 7.0);  // H.264
    }
    if (framerate > 0.0) {
        w.put_obj_prop_number("framerate",    framerate);
    }
    if (video_bitrate_bps > 0) {
        // FLV convention: kilobits per second.
        w.put_obj_prop_number("videodatarate",
                                 static_cast<double>(video_bitrate_bps) / 1000.0);
    }
    if (audio_sample_rate > 0) {
        w.put_obj_prop_number("audiosamplerate",
                                 static_cast<double>(audio_sample_rate));
    }
    if (audio_channels > 0) {
        w.put_obj_prop_number("audiochannels",
                                 static_cast<double>(audio_channels));
        w.put_obj_prop_boolean("stereo", audio_channels >= 2);
    }
    if (audio_bitrate_bps > 0) {
        w.put_obj_prop_number("audiodatarate",
                                 static_cast<double>(audio_bitrate_bps) / 1000.0);
    }
    w.put_obj_prop_number("audiocodecid", 10.0);  // AAC
    w.put_obj_prop_string("encoder",
                             c->cfg_flash_ver.empty()
                                 ? "libseurat"
                                 : c->cfg_flash_ver.c_str());

    w.end_object();  // same trailer shape as ECMA array (0x00 0x00 0x09)

    const auto& b = w.buf();
    const int rc = c->writer->send_message(srt::CS_DATA, srt::MSG_AMF0_DATA,
                                              c->publish_stream_id, /*ts=*/0,
                                              b.data(), b.size());
    if (rc == SEURAT_RTMP_OK) {
        c->metadata_sent = true;
        c->stats.rtmp_messages_sent++;
    }
    return rc;
}

// ---------------------------------------------------------------- video

int seurat_rtmp_send_video_sequence(seurat_rtmp_client_t* c,
                                      uint32_t ts, const uint8_t* sps, size_t sps_len,
                                      const uint8_t* pps, size_t pps_len) {
    if (!c || !c->writer) return SEURAT_RTMP_E_INVALID_ARG;
    if (!c->connected)    return SEURAT_RTMP_E_STATE;
    if (c->video_seq_sent) return SEURAT_RTMP_E_STATE;

    std::vector<uint8_t> cfg;
    int rc = srt::build_avcc_record(sps, sps_len, pps, pps_len, &cfg);
    if (rc != SEURAT_RTMP_OK) return rc;

    // FLV video tag body (AVC sequence header):
    //     0x17        = FrameType(keyframe=1) | CodecID(AVC=7)
    //     0x00        = AVCPacketType(sequenceHeader=0)
    //     00 00 00    = composition time (0)
    //     <cfg>       = AVCDecoderConfigurationRecord
    std::vector<uint8_t> body;
    body.reserve(5 + cfg.size());
    body.push_back(0x17);
    body.push_back(0x00);
    srt::put3be(body, 0);
    body.insert(body.end(), cfg.begin(), cfg.end());

    rc = c->writer->send_message(srt::CS_VIDEO, srt::MSG_VIDEO,
                                    c->publish_stream_id, ts,
                                    body.data(), body.size());
    if (rc == SEURAT_RTMP_OK) {
        c->video_seq_sent = true;
        c->stats.rtmp_messages_sent++;
    }
    return rc;
}

int seurat_rtmp_send_video_frame(seurat_rtmp_client_t* c,
                                   uint32_t ts, int keyframe, int32_t cts,
                                   const uint8_t* avcc, size_t len) {
    if (!c || !c->writer || !avcc) return SEURAT_RTMP_E_INVALID_ARG;
    if (!c->connected)              return SEURAT_RTMP_E_STATE;
    if (!c->video_seq_sent)         return SEURAT_RTMP_E_STATE;

    // FLV video tag body (AVC NALU):
    //     0x17 / 0x27 = FrameType(key=1, inter=2) | CodecID(AVC=7)
    //     0x01        = AVCPacketType(NALU=1)
    //     [3 bytes]   = composition time (signed 24-bit, ms)
    //     <avcc>      = AVCC-framed NAL units (each NAL has 4-byte BE length)
    std::vector<uint8_t> body;
    body.reserve(5 + len);
    body.push_back(keyframe ? 0x17 : 0x27);
    body.push_back(0x01);
    // signed → two's complement low 24 bits
    srt::put3be(body, static_cast<uint32_t>(cts) & 0xFFFFFFu);
    body.insert(body.end(), avcc, avcc + len);

    const int rc = c->writer->send_message(srt::CS_VIDEO, srt::MSG_VIDEO,
                                              c->publish_stream_id, ts,
                                              body.data(), body.size());
    if (rc == SEURAT_RTMP_OK) {
        c->stats.rtmp_messages_sent++;
        c->stats.video_frames_sent++;
    }
    return rc;
}

// ---------------------------------------------------------------- audio

// FLV audio-tag "header byte" for AAC. Per FLV spec §E.4.2.1, when
// SoundFormat == AAC (10) the rate/size/type fields are effectively ignored
// and MUST be 3 (44 kHz) / 1 (16-bit) / 1 (stereo), yielding 0xAF.
static constexpr uint8_t kAacAudioHdr = 0xAF;

int seurat_rtmp_send_audio_sequence(seurat_rtmp_client_t* c,
                                      uint32_t ts, const uint8_t* asc, size_t len) {
    if (!c || !c->writer || !asc || len == 0) return SEURAT_RTMP_E_INVALID_ARG;
    if (!c->connected)                         return SEURAT_RTMP_E_STATE;
    if (c->audio_seq_sent)                     return SEURAT_RTMP_E_STATE;

    // FLV audio tag body (AAC sequence header):
    //     0xAF        = AAC / 44 / 16-bit / stereo
    //     0x00        = AACPacketType(sequenceHeader=0)
    //     <asc>       = AudioSpecificConfig
    std::vector<uint8_t> body;
    body.reserve(2 + len);
    body.push_back(kAacAudioHdr);
    body.push_back(0x00);
    body.insert(body.end(), asc, asc + len);

    const int rc = c->writer->send_message(srt::CS_AUDIO, srt::MSG_AUDIO,
                                              c->publish_stream_id, ts,
                                              body.data(), body.size());
    if (rc == SEURAT_RTMP_OK) {
        c->audio_seq_sent = true;
        c->stats.rtmp_messages_sent++;
    }
    return rc;
}

int seurat_rtmp_send_audio_frame(seurat_rtmp_client_t* c,
                                   uint32_t ts, const uint8_t* raw, size_t len) {
    if (!c || !c->writer || !raw || len == 0) return SEURAT_RTMP_E_INVALID_ARG;
    if (!c->connected)                         return SEURAT_RTMP_E_STATE;
    if (!c->audio_seq_sent)                    return SEURAT_RTMP_E_STATE;

    // FLV audio tag body (AAC raw):
    //     0xAF
    //     0x01        = AACPacketType(raw=1)
    //     <raw>       = ADTS-less AAC frame
    std::vector<uint8_t> body;
    body.reserve(2 + len);
    body.push_back(kAacAudioHdr);
    body.push_back(0x01);
    body.insert(body.end(), raw, raw + len);

    const int rc = c->writer->send_message(srt::CS_AUDIO, srt::MSG_AUDIO,
                                              c->publish_stream_id, ts,
                                              body.data(), body.size());
    if (rc == SEURAT_RTMP_OK) {
        c->stats.rtmp_messages_sent++;
        c->stats.audio_frames_sent++;
    }
    return rc;
}

// ---------------------------------------------------------------- stats

int seurat_rtmp_get_stats(seurat_rtmp_client_t* c, seurat_rtmp_stats_t* out) {
    if (!c || !out) return SEURAT_RTMP_E_INVALID_ARG;
    if (c->transport) {
        c->stats.bytes_sent_total = c->transport->bytes_sent;
        c->stats.bytes_recv_total = c->transport->bytes_recv;
    }
    *out = c->stats;
    return SEURAT_RTMP_OK;
}
