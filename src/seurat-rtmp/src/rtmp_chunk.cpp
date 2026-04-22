// =============================================================================
// seurat-rtmp — RTMP 1.0 chunk stream (writer + reader).
//
// Reference: RTMP 1.0 Specification §6.1.
//
// Writer strategy (Phase B):
//   - Always emit fmt=0 (full 11-byte message header) for the first chunk of
//     each message and fmt=3 (continuation, header-less) for subsequent
//     chunks. This avoids per-cs_id state bookkeeping at the cost of a couple
//     dozen extra bytes per message — negligible for RTMP publishing.
//
// Reader strategy:
//   - Per-cs_id state (last-seen message-length / type-id / stream-id /
//     timestamp) is required because servers compress follow-up messages with
//     fmt=1/2/3.
//   - Messages are buffered until complete, at which point read_message()
//     returns them.
//
// License: MIT.
// =============================================================================

#include "rtmp_internal.h"

#include <cstring>

namespace seurat::rtmp {

namespace {

constexpr uint32_t kExtTsSentinel = 0xFFFFFF;

// ---- 3-byte big-endian writer (timestamp / message length fields)
inline void put3(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 16);
    p[1] = static_cast<uint8_t>(v >>  8);
    p[2] = static_cast<uint8_t>(v);
}

// ---- 4-byte big-endian writer (extended timestamp)
inline void put4be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8);
    p[3] = static_cast<uint8_t>(v);
}

// ---- 4-byte LITTLE-endian writer (RTMP §6.1.1 quirk: msg_stream_id only).
inline void put4le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >>  8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline uint32_t get3(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) <<  8) |
            static_cast<uint32_t>(p[2]);
}
inline uint32_t get4be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}
inline uint32_t get4le(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])        |
           (static_cast<uint32_t>(p[1]) <<  8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Serialise the basic header (fmt, cs_id) into `out`. Returns bytes written.
size_t write_basic_header(uint8_t* out, uint8_t fmt, uint32_t cs_id) {
    const uint8_t f = static_cast<uint8_t>((fmt & 0x3) << 6);

    if (cs_id >= 2 && cs_id <= 63) {
        out[0] = f | static_cast<uint8_t>(cs_id);
        return 1;
    }
    if (cs_id >= 64 && cs_id <= 319) {
        out[0] = f;                                   // marker = 0
        out[1] = static_cast<uint8_t>(cs_id - 64);
        return 2;
    }
    if (cs_id >= 64 && cs_id <= 65599) {
        out[0] = f | 0x01;                            // marker = 1
        const uint32_t v = cs_id - 64;
        out[1] = static_cast<uint8_t>(v);             // LE
        out[2] = static_cast<uint8_t>(v >> 8);
        return 3;
    }
    // Unsupported cs_id — caller's responsibility to pick a valid one.
    out[0] = f;
    return 1;
}

}  // namespace

// ========================================================= ChunkWriter

ChunkWriter::ChunkWriter(Transport* tr) : tr_(tr) {}

int ChunkWriter::send_message(uint32_t cs_id,
                               uint8_t  msg_type,
                               uint32_t stream_id,
                               uint32_t timestamp_ms,
                               const uint8_t* payload,
                               size_t payload_len) {
    if (!tr_) return SEURAT_RTMP_E_STATE;
    if (payload_len > 0xFFFFFFu) return SEURAT_RTMP_E_INVALID_ARG;  // 24-bit
    if (out_chunk_size_ == 0)     return SEURAT_RTMP_E_STATE;

    uint8_t hdr[18];  // worst case: 3 (basic) + 11 (fmt0 msg) + 4 (ext ts)
    size_t  hdr_len = 0;

    hdr_len += write_basic_header(hdr, /*fmt=*/0, cs_id);

    const bool     ext_ts = (timestamp_ms >= kExtTsSentinel);
    const uint32_t ts24   = ext_ts ? kExtTsSentinel : timestamp_ms;

    // Message header (fmt=0, full 11 bytes):
    //     [3] timestamp (24-bit BE, 0xFFFFFF => extended)
    //     [3] message length (24-bit BE)
    //     [1] message type id
    //     [4] message stream id (LITTLE endian!)
    put3(&hdr[hdr_len], ts24);                 hdr_len += 3;
    put3(&hdr[hdr_len], static_cast<uint32_t>(payload_len)); hdr_len += 3;
    hdr[hdr_len++] = msg_type;
    put4le(&hdr[hdr_len], stream_id);          hdr_len += 4;

    if (ext_ts) {
        put4be(&hdr[hdr_len], timestamp_ms);   hdr_len += 4;
    }

    int rc = tr_->send_all(hdr, hdr_len);
    if (rc != SEURAT_RTMP_OK) return rc;

    size_t off = 0;
    bool   first = true;
    while (off < payload_len) {
        if (!first) {
            // Continuation chunk: basic header only, fmt=3.
            uint8_t cont[4];
            size_t  cont_len = write_basic_header(cont, /*fmt=*/3, cs_id);
            if (ext_ts) {
                // Per §6.1.3, the extended timestamp is repeated on every
                // chunk that requires it, including fmt=3 continuations.
                put4be(&cont[cont_len], timestamp_ms);
                cont_len += 4;
            }
            rc = tr_->send_all(cont, cont_len);
            if (rc != SEURAT_RTMP_OK) return rc;
        }

        const size_t remain = payload_len - off;
        const size_t n      = (remain < out_chunk_size_) ? remain
                                                          : out_chunk_size_;
        rc = tr_->send_all(payload + off, n);
        if (rc != SEURAT_RTMP_OK) return rc;

        off  += n;
        first = false;
    }
    return SEURAT_RTMP_OK;
}

// ========================================================= ChunkReader

int ChunkReader::read_message(Message* msg) {
    if (!tr_ || !msg) return SEURAT_RTMP_E_INVALID_ARG;

    msg->payload.clear();

    while (true) {
        // ---- basic header
        uint8_t bh0;
        int rc = tr_->recv_exact(&bh0, 1);
        if (rc != SEURAT_RTMP_OK) return rc;
        const uint8_t  fmt   = static_cast<uint8_t>(bh0 >> 6);
        const uint32_t marker = static_cast<uint32_t>(bh0 & 0x3F);

        uint32_t cs_id;
        if (marker == 0) {
            uint8_t b1;
            rc = tr_->recv_exact(&b1, 1);
            if (rc != SEURAT_RTMP_OK) return rc;
            cs_id = 64u + b1;
        } else if (marker == 1) {
            uint8_t b[2];
            rc = tr_->recv_exact(b, 2);
            if (rc != SEURAT_RTMP_OK) return rc;
            cs_id = 64u + static_cast<uint32_t>(b[0]) +
                          (static_cast<uint32_t>(b[1]) << 8);
        } else {
            cs_id = marker;
        }

        CsState& s = state_[cs_id];

        // ---- message header
        uint32_t msg_len     = s.msg_length;
        uint8_t  msg_type    = s.msg_type_id;
        uint32_t stream_id   = s.msg_stream_id;
        uint32_t ts_val      = 0;  // either absolute ts or delta, before ext
        bool     ext_ts      = false;

        if (fmt == 0) {
            uint8_t h[11];
            rc = tr_->recv_exact(h, 11);
            if (rc != SEURAT_RTMP_OK) return rc;
            ts_val    = get3(&h[0]);
            msg_len   = get3(&h[3]);
            msg_type  = h[6];
            stream_id = get4le(&h[7]);
        } else if (fmt == 1) {
            uint8_t h[7];
            rc = tr_->recv_exact(h, 7);
            if (rc != SEURAT_RTMP_OK) return rc;
            ts_val   = get3(&h[0]);
            msg_len  = get3(&h[3]);
            msg_type = h[6];
        } else if (fmt == 2) {
            uint8_t h[3];
            rc = tr_->recv_exact(h, 3);
            if (rc != SEURAT_RTMP_OK) return rc;
            ts_val = get3(h);
        } else {
            // fmt == 3: no message header. But if previous chunk used an
            // extended timestamp, its 4-byte ext-ts block is repeated here.
            ext_ts = s.has_ext_ts;
        }

        // ---- extended timestamp (present when ts field == 0xFFFFFF on fmt
        //      0/1/2, or when inheriting ext-ts from prior state on fmt 3).
        uint32_t abs_ts = s.abs_timestamp;
        if (fmt != 3) {
            if (ts_val == kExtTsSentinel) {
                uint8_t e[4];
                rc = tr_->recv_exact(e, 4);
                if (rc != SEURAT_RTMP_OK) return rc;
                ts_val = get4be(e);
                ext_ts = true;
            }
            if (fmt == 0) abs_ts  = ts_val;
            else          abs_ts += ts_val;
        } else if (ext_ts) {
            uint8_t e[4];
            rc = tr_->recv_exact(e, 4);
            if (rc != SEURAT_RTMP_OK) return rc;
            // Some servers re-send the full extended ts here; others don't.
            // We accept either: trust the parent (s.abs_timestamp) if the
            // received value equals what we expect; otherwise update.
            abs_ts = get4be(e);
        }

        s.msg_length    = msg_len;
        s.msg_type_id   = msg_type;
        s.msg_stream_id = stream_id;
        s.abs_timestamp = abs_ts;
        s.has_ext_ts    = ext_ts;

        // ---- payload chunk
        if (s.partial.size() < msg_len) {
            const size_t want = msg_len - s.partial.size();
            const size_t n    = (want < in_chunk_size_) ? want : in_chunk_size_;
            const size_t off  = s.partial.size();
            s.partial.resize(off + n);
            rc = tr_->recv_exact(s.partial.data() + off, n);
            if (rc != SEURAT_RTMP_OK) return rc;
        }

        if (s.partial.size() >= msg_len) {
            msg->msg_type     = msg_type;
            msg->stream_id    = stream_id;
            msg->timestamp_ms = abs_ts;
            msg->payload      = std::move(s.partial);
            s.partial.clear();

            // Protocol control messages on cs_id 2 are handled transparently.
            if (cs_id == 2) {
                if (handle_control(*msg) == SEURAT_RTMP_OK) {
                    // Swallow protocol-control messages, keep looping until
                    // we surface something application-level.
                    msg->payload.clear();
                    continue;
                }
            }
            return SEURAT_RTMP_OK;
        }
        // Not complete yet — next iteration reads the next chunk.
    }
}

int ChunkReader::handle_control(const Message& msg) {
    // Protocol control messages (spec §5.4). We only act on the ones that
    // affect our framing; the rest are accepted & ignored.
    switch (msg.msg_type) {
        case 1:  // Set Chunk Size
            if (msg.payload.size() >= 4) {
                const uint32_t v = get4be(msg.payload.data()) & 0x7FFFFFFFu;
                if (v > 0) in_chunk_size_ = v;
            }
            return SEURAT_RTMP_OK;
        case 3:  // Acknowledgement
        case 5:  // Window Ack Size
        case 6:  // Set Peer Bandwidth
            return SEURAT_RTMP_OK;
        case 2:  // Abort — ignore
            return SEURAT_RTMP_OK;
        case 4:  // User Control Message (§6.2) — ignore for publish
            return SEURAT_RTMP_OK;
        default:
            // Not recognized as a control message: surface to caller.
            return SEURAT_RTMP_E_PROTOCOL;
    }
}

}  // namespace seurat::rtmp
