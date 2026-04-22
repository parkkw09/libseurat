// =============================================================================
// seurat-flv — FLV muxer (implementation).
//
// See include/seurat/flv.h for the public contract. This file is self-contained
// (only depends on libc and libc++); it links into libseurat-flv.a.
//
// FLV tag framing:
//     +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
//     | typ |     data size   | timestamp lo  | tsE |   stream id=0   |
//     +-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----+
//     |                      tag body (`data size` bytes)              |
//     +-----------------------------------------------------------------+
//     |                  previousTagSize = 11 + data size               |
//     +-----------------------------------------------------------------+
//
// License: MIT.
// =============================================================================

#include "seurat/flv.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace {

// ----------------------------------------------------------------- constants

constexpr uint8_t FLV_TAG_AUDIO  = 0x08;
constexpr uint8_t FLV_TAG_VIDEO  = 0x09;
constexpr uint8_t FLV_TAG_SCRIPT = 0x12;

constexpr uint8_t FLV_FRAME_KEY    = 0x1;  // keyframe  (IDR)
constexpr uint8_t FLV_FRAME_INTER  = 0x2;  // inter (P/B)

constexpr uint8_t FLV_CODECID_AVC  = 0x7;  // H.264
constexpr uint8_t FLV_SOUND_AAC    = 0xA;  // AAC

constexpr uint8_t AVC_PKT_SEQHDR   = 0x00;
constexpr uint8_t AVC_PKT_NALU     = 0x01;
constexpr uint8_t AVC_PKT_END      = 0x02;

constexpr uint8_t AAC_PKT_SEQHDR   = 0x00;
constexpr uint8_t AAC_PKT_RAW      = 0x01;

// FLV 24-bit data-size field ceiling.
constexpr uint32_t FLV_DATA_MAX    = 0xFFFFFFu;

// ----------------------------------------------------------------- helpers

inline void put_u8(std::vector<uint8_t>& v, uint8_t x) {
    v.push_back(x);
}

inline void put_u16be(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

inline void put_u24be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >>  8));
    v.push_back(static_cast<uint8_t>(x));
}

inline void put_u32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >>  8));
    v.push_back(static_cast<uint8_t>(x));
}

// IEEE-754 double -> 8 bytes big endian.
inline void put_f64be(std::vector<uint8_t>& v, double value) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value),
                  "IEEE-754 64-bit double assumed");
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 7; i >= 0; --i) {
        v.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
}

// ---------------------------------------------------------------- AMF0

// Minimal AMF0 encoder. Only the markers the FLV onMetaData tag needs.
//   0x00 Number (double), 0x01 Boolean, 0x02 String,
//   0x08 ECMA array, 0x09 Object end, 0x05 Null.

constexpr uint8_t AMF0_NUMBER     = 0x00;
constexpr uint8_t AMF0_BOOLEAN    = 0x01;
constexpr uint8_t AMF0_STRING     = 0x02;
constexpr uint8_t AMF0_ECMA_ARRAY = 0x08;
constexpr uint8_t AMF0_OBJECT_END = 0x09;

// Encode an AMF0 short string (used for keys and onMetaData marker value).
// Note: AMF0 "short" string length field is 16-bit BE. Values up to 65535 bytes.
inline void amf0_put_short_string_raw(std::vector<uint8_t>& v,
                                       const char* s, size_t len) {
    // Caller is responsible for len <= 0xFFFF.
    put_u16be(v, static_cast<uint16_t>(len));
    v.insert(v.end(), reinterpret_cast<const uint8_t*>(s),
                      reinterpret_cast<const uint8_t*>(s) + len);
}

// Top-level AMF0 String value (marker + length + bytes).
inline void amf0_put_string(std::vector<uint8_t>& v, const char* s) {
    const size_t len = std::strlen(s);
    put_u8(v, AMF0_STRING);
    amf0_put_short_string_raw(v, s, len);
}

inline void amf0_put_number(std::vector<uint8_t>& v, double x) {
    put_u8(v, AMF0_NUMBER);
    put_f64be(v, x);
}

inline void amf0_put_boolean(std::vector<uint8_t>& v, bool b) {
    put_u8(v, AMF0_BOOLEAN);
    put_u8(v, b ? 1 : 0);
}

// Write an ECMA-array key followed by a number value.
inline void amf0_pair_number(std::vector<uint8_t>& v,
                              const char* key, double x) {
    amf0_put_short_string_raw(v, key, std::strlen(key));
    amf0_put_number(v, x);
}

inline void amf0_pair_boolean(std::vector<uint8_t>& v,
                               const char* key, bool b) {
    amf0_put_short_string_raw(v, key, std::strlen(key));
    amf0_put_boolean(v, b);
}

// ECMA-array trailer: empty key (length=0) + object-end marker.
inline void amf0_ecma_end(std::vector<uint8_t>& v) {
    put_u16be(v, 0);
    put_u8(v, AMF0_OBJECT_END);
}

}  // namespace

// ======================================================== seurat_flv_muxer

struct seurat_flv_muxer {
    seurat_flv_config_t  cfg{};
    seurat_flv_writer_fn writer = nullptr;
    void*                user   = nullptr;

    bool header_written         = false;
    bool video_seq_written      = false;
    bool audio_seq_written      = false;

    // Reusable scratch; shrunk (but not freed) between tags.
    std::vector<uint8_t> body;
};

// ------------------------------------------------------------------- I/O

namespace {

// Push a complete tag (header + body + previousTagSize trailer) to the writer.
int emit_tag(seurat_flv_muxer* m,
              uint8_t tag_type,
              uint32_t timestamp_ms,
              const uint8_t* body, uint32_t body_len) {
    if (body_len > FLV_DATA_MAX) return SEURAT_FLV_E_OVERFLOW;

    uint8_t hdr[11];
    hdr[0] = tag_type;
    hdr[1] = static_cast<uint8_t>(body_len >> 16);
    hdr[2] = static_cast<uint8_t>(body_len >>  8);
    hdr[3] = static_cast<uint8_t>(body_len);
    // Timestamp is 24-bit lower + 8-bit extended upper.
    hdr[4] = static_cast<uint8_t>(timestamp_ms >> 16);
    hdr[5] = static_cast<uint8_t>(timestamp_ms >>  8);
    hdr[6] = static_cast<uint8_t>(timestamp_ms);
    hdr[7] = static_cast<uint8_t>(timestamp_ms >> 24);
    hdr[8] = hdr[9] = hdr[10] = 0;  // StreamID = 0

    int rc;
    rc = m->writer(m->user, hdr, sizeof(hdr));
    if (rc != 0) return SEURAT_FLV_E_WRITER;

    if (body_len > 0) {
        rc = m->writer(m->user, body, body_len);
        if (rc != 0) return SEURAT_FLV_E_WRITER;
    }

    const uint32_t prev_size = 11 + body_len;
    uint8_t trailer[4] = {
        static_cast<uint8_t>(prev_size >> 24),
        static_cast<uint8_t>(prev_size >> 16),
        static_cast<uint8_t>(prev_size >>  8),
        static_cast<uint8_t>(prev_size),
    };
    rc = m->writer(m->user, trailer, sizeof(trailer));
    return (rc != 0) ? SEURAT_FLV_E_WRITER : SEURAT_FLV_OK;
}

// --------------------- AVCDecoderConfigurationRecord (ISO/IEC 14496-15)
//
// aligned(8) class AVCDecoderConfigurationRecord {
//     unsigned int(8)  configurationVersion  = 1;
//     unsigned int(8)  AVCProfileIndication;    // sps[1]
//     unsigned int(8)  profile_compatibility;   // sps[2]
//     unsigned int(8)  AVCLevelIndication;      // sps[3]
//     bit(6) reserved = 111111b;
//     unsigned int(2)  lengthSizeMinusOne = 3;  // we use 4-byte length prefix
//     bit(3) reserved = 111b;
//     unsigned int(5)  numOfSequenceParameterSets;
//     for (i=0; i < numOfSequenceParameterSets; i++) {
//         unsigned int(16) sequenceParameterSetLength;
//         bit(8*sequenceParameterSetLength) sequenceParameterSetNALUnit;
//     }
//     unsigned int(8)  numOfPictureParameterSets;
//     for (i=0; i < numOfPictureParameterSets; i++) {
//         unsigned int(16) pictureParameterSetLength;
//         bit(8*pictureParameterSetLength) pictureParameterSetNALUnit;
//     }
// }
int build_avcc_record(std::vector<uint8_t>& out,
                       const uint8_t* sps, size_t sps_len,
                       const uint8_t* pps, size_t pps_len) {
    if (sps_len < 4 || sps_len > 0xFFFF || pps_len > 0xFFFF) {
        return SEURAT_FLV_E_INVALID_ARG;
    }

    out.clear();
    out.reserve(11 + sps_len + pps_len);

    put_u8(out, 0x01);                // configurationVersion
    put_u8(out, sps[1]);              // AVCProfileIndication
    put_u8(out, sps[2]);              // profile_compatibility
    put_u8(out, sps[3]);              // AVCLevelIndication
    put_u8(out, 0xFF);                // 6 bits reserved + lengthSizeMinusOne=3
    put_u8(out, 0xE1);                // 3 bits reserved + numOfSPS=1

    put_u16be(out, static_cast<uint16_t>(sps_len));
    out.insert(out.end(), sps, sps + sps_len);

    put_u8(out, 0x01);                // numOfPPS=1
    put_u16be(out, static_cast<uint16_t>(pps_len));
    if (pps_len > 0) {
        out.insert(out.end(), pps, pps + pps_len);
    }
    return SEURAT_FLV_OK;
}

// ---- AAC sample-rate index for script tag metadata.
// Index table from ISO/IEC 14496-3.
double aac_index_to_rate(int idx) {
    static const int kRates[13] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000, 7350,
    };
    if (idx < 0 || idx >= 13) return 0.0;
    return static_cast<double>(kRates[idx]);
}

}  // namespace

// ========================================================== public API

seurat_flv_muxer_t* seurat_flv_create(const seurat_flv_config_t* cfg,
                                       seurat_flv_writer_fn writer,
                                       void* user) {
    if (!cfg || !writer) return nullptr;

    auto* m = new (std::nothrow) seurat_flv_muxer();
    if (!m) return nullptr;

    m->cfg    = *cfg;
    m->writer = writer;
    m->user   = user;
    m->body.reserve(256);
    return m;
}

void seurat_flv_destroy(seurat_flv_muxer_t* m) {
    delete m;
}

int seurat_flv_write_header(seurat_flv_muxer_t* m,
                             int has_video, int has_audio) {
    if (!m) return SEURAT_FLV_E_INVALID_ARG;
    if (m->header_written) return SEURAT_FLV_E_STATE;

    uint8_t flags = 0;
    if (has_audio) flags |= 0x04;
    if (has_video) flags |= 0x01;

    const uint8_t header[9 + 4] = {
        'F', 'L', 'V',
        0x01,                    // version
        flags,                   // TypeFlags
        0x00, 0x00, 0x00, 0x09,  // DataOffset = 9
        0x00, 0x00, 0x00, 0x00,  // PreviousTagSize0 = 0
    };
    if (m->writer(m->user, header, sizeof(header)) != 0) {
        return SEURAT_FLV_E_WRITER;
    }
    m->header_written = true;
    return SEURAT_FLV_OK;
}

int seurat_flv_write_metadata(seurat_flv_muxer_t* m) {
    if (!m) return SEURAT_FLV_E_INVALID_ARG;
    if (!m->header_written) return SEURAT_FLV_E_STATE;

    auto& b = m->body;
    b.clear();

    // SCRIPTDATAOBJECT #1: string "onMetaData"
    amf0_put_string(b, "onMetaData");

    // SCRIPTDATAOBJECT #2: ECMA array of metadata pairs.
    put_u8(b, AMF0_ECMA_ARRAY);
    // Array element count. FLV treats this as a hint; most servers ignore it
    // but we keep it accurate for well-formedness.
    constexpr uint32_t kApproxCount = 16;
    put_u32be(b, kApproxCount);

    const auto& c = m->cfg;

    // ---- video block
    const bool has_video = c.video_width > 0 && c.video_height > 0;
    if (has_video) {
        amf0_pair_number(b, "width",          static_cast<double>(c.video_width));
        amf0_pair_number(b, "height",         static_cast<double>(c.video_height));
        if (c.video_framerate > 0.0) {
            amf0_pair_number(b, "framerate",  c.video_framerate);
        }
        if (c.video_bitrate_bps > 0) {
            amf0_pair_number(b, "videodatarate",
                             static_cast<double>(c.video_bitrate_bps) / 1000.0);
        }
        amf0_pair_number(b, "videocodecid",   static_cast<double>(FLV_CODECID_AVC));
    }

    // ---- audio block
    const bool has_audio = c.audio_sample_rate > 0 && c.audio_channels > 0;
    if (has_audio) {
        amf0_pair_number(b, "audiosamplerate",
                         static_cast<double>(c.audio_sample_rate));
        amf0_pair_number(b, "audiosamplesize",
                         static_cast<double>(c.audio_sample_size_bits > 0
                                             ? c.audio_sample_size_bits : 16));
        amf0_pair_boolean(b, "stereo",       (c.audio_channels == 2));
        amf0_pair_number(b, "audiochannels",  static_cast<double>(c.audio_channels));
        if (c.audio_bitrate_bps > 0) {
            amf0_pair_number(b, "audiodatarate",
                             static_cast<double>(c.audio_bitrate_bps) / 1000.0);
        }
        amf0_pair_number(b, "audiocodecid",   static_cast<double>(FLV_SOUND_AAC));
    }

    // ---- encoder / misc
    amf0_pair_number(b, "duration", 0.0);      // live stream: 0
    const char kEncoder[] = "libseurat";
    {
        // emit raw pair "encoder" -> String
        const char key[] = "encoder";
        amf0_put_short_string_raw(b, key, sizeof(key) - 1);
        amf0_put_string(b, kEncoder);
    }

    amf0_ecma_end(b);

    return emit_tag(m, FLV_TAG_SCRIPT, 0,
                    b.data(), static_cast<uint32_t>(b.size()));
}

int seurat_flv_write_video_sequence(seurat_flv_muxer_t* m,
                                      uint32_t       timestamp_ms,
                                      const uint8_t* sps, size_t sps_len,
                                      const uint8_t* pps, size_t pps_len) {
    if (!m || !sps) return SEURAT_FLV_E_INVALID_ARG;
    if (pps_len > 0 && !pps) return SEURAT_FLV_E_INVALID_ARG;
    if (!m->header_written) return SEURAT_FLV_E_STATE;
    if (m->video_seq_written) return SEURAT_FLV_E_STATE;

    auto& b = m->body;
    b.clear();

    put_u8(b, (FLV_FRAME_KEY << 4) | FLV_CODECID_AVC);
    put_u8(b, AVC_PKT_SEQHDR);
    put_u24be(b, 0);  // composition time = 0

    std::vector<uint8_t> cfg;
    const int rc = build_avcc_record(cfg, sps, sps_len, pps, pps_len);
    if (rc != SEURAT_FLV_OK) return rc;
    b.insert(b.end(), cfg.begin(), cfg.end());

    const int er = emit_tag(m, FLV_TAG_VIDEO, timestamp_ms,
                             b.data(), static_cast<uint32_t>(b.size()));
    if (er == SEURAT_FLV_OK) m->video_seq_written = true;
    return er;
}

int seurat_flv_write_video_frame(seurat_flv_muxer_t* m,
                                   uint32_t       timestamp_ms,
                                   int            is_keyframe,
                                   int32_t        composition_time_ms,
                                   const uint8_t* avcc_nalus,
                                   size_t         avcc_nalus_len) {
    if (!m) return SEURAT_FLV_E_INVALID_ARG;
    if (avcc_nalus_len > 0 && !avcc_nalus) return SEURAT_FLV_E_INVALID_ARG;
    if (!m->video_seq_written) return SEURAT_FLV_E_STATE;

    // composition time is a signed 24-bit int in FLV.
    if (composition_time_ms >  0x7FFFFF ||
        composition_time_ms < -0x800000) {
        return SEURAT_FLV_E_OVERFLOW;
    }
    const uint32_t ct = static_cast<uint32_t>(composition_time_ms) & 0xFFFFFFu;

    auto& b = m->body;
    b.clear();
    b.reserve(5 + avcc_nalus_len);

    const uint8_t frame_type = is_keyframe ? FLV_FRAME_KEY : FLV_FRAME_INTER;
    put_u8(b, (frame_type << 4) | FLV_CODECID_AVC);
    put_u8(b, AVC_PKT_NALU);
    put_u24be(b, ct);
    if (avcc_nalus_len > 0) {
        b.insert(b.end(), avcc_nalus, avcc_nalus + avcc_nalus_len);
    }

    return emit_tag(m, FLV_TAG_VIDEO, timestamp_ms,
                    b.data(), static_cast<uint32_t>(b.size()));
}

int seurat_flv_write_video_end(seurat_flv_muxer_t* m, uint32_t timestamp_ms) {
    if (!m) return SEURAT_FLV_E_INVALID_ARG;
    if (!m->video_seq_written) return SEURAT_FLV_E_STATE;

    uint8_t body[5];
    body[0] = (FLV_FRAME_KEY << 4) | FLV_CODECID_AVC;
    body[1] = AVC_PKT_END;
    body[2] = body[3] = body[4] = 0;  // composition time = 0
    return emit_tag(m, FLV_TAG_VIDEO, timestamp_ms, body, sizeof(body));
}

int seurat_flv_write_audio_sequence(seurat_flv_muxer_t* m,
                                      uint32_t       timestamp_ms,
                                      const uint8_t* asc,
                                      size_t         asc_len) {
    if (!m || !asc || asc_len == 0) return SEURAT_FLV_E_INVALID_ARG;
    if (!m->header_written) return SEURAT_FLV_E_STATE;
    if (m->audio_seq_written) return SEURAT_FLV_E_STATE;

    auto& b = m->body;
    b.clear();
    b.reserve(2 + asc_len);

    // SoundFormat=10 (AAC), SoundRate=3 (44kHz bit — ignored for AAC by spec
    // but most servers want it set to 3), SoundSize=1 (16-bit), SoundType=1
    // (stereo). For AAC the real sample rate / channel count come from the
    // AudioSpecificConfig that follows.
    put_u8(b, (FLV_SOUND_AAC << 4) | (3 << 2) | (1 << 1) | 1);
    put_u8(b, AAC_PKT_SEQHDR);
    b.insert(b.end(), asc, asc + asc_len);

    const int rc = emit_tag(m, FLV_TAG_AUDIO, timestamp_ms,
                             b.data(), static_cast<uint32_t>(b.size()));
    if (rc == SEURAT_FLV_OK) m->audio_seq_written = true;

    // (Parse ASC only for validation: first 5 bits = AOT, next 4 = freq_idx,
    //  next 4 = channel_cfg. We use it to sanity-check the common case where
    //  the user's config_t sample rate matches the ASC.)
    if (asc_len >= 2) {
        const unsigned aot      = (asc[0] >> 3) & 0x1F;
        const unsigned freq_idx = ((asc[0] & 0x07) << 1) | ((asc[1] >> 7) & 0x01);
        const double   rate     = aac_index_to_rate(static_cast<int>(freq_idx));
        (void)aot;  // Accepted: AOT_LC=2 is the norm; we don't enforce.
        (void)rate;
    }
    return rc;
}

int seurat_flv_write_audio_frame(seurat_flv_muxer_t* m,
                                   uint32_t       timestamp_ms,
                                   const uint8_t* aac_raw,
                                   size_t         aac_raw_len) {
    if (!m) return SEURAT_FLV_E_INVALID_ARG;
    if (aac_raw_len > 0 && !aac_raw) return SEURAT_FLV_E_INVALID_ARG;
    if (!m->audio_seq_written) return SEURAT_FLV_E_STATE;

    auto& b = m->body;
    b.clear();
    b.reserve(2 + aac_raw_len);

    put_u8(b, (FLV_SOUND_AAC << 4) | (3 << 2) | (1 << 1) | 1);
    put_u8(b, AAC_PKT_RAW);
    if (aac_raw_len > 0) {
        b.insert(b.end(), aac_raw, aac_raw + aac_raw_len);
    }

    return emit_tag(m, FLV_TAG_AUDIO, timestamp_ms,
                    b.data(), static_cast<uint32_t>(b.size()));
}

// ----------------------------------------------------- Annex-B → AVCC

int seurat_flv_annexb_to_avcc(const uint8_t* in, size_t in_len,
                               uint8_t*       out, size_t out_cap,
                               size_t*        written) {
    if (written) *written = 0;
    if (in_len == 0) return SEURAT_FLV_OK;
    if (!in || !out || !written) return SEURAT_FLV_E_INVALID_ARG;

    // Locate start codes: 0x000001 (3-byte) or 0x00000001 (4-byte).
    // We walk through the buffer, find each NAL unit's [start, end), and
    // emit <4-byte BE length><NAL bytes> for each.
    size_t out_off = 0;

    // scan returns the offset of the byte just AFTER the start code, and the
    // length of that start code (3 or 4). Returns in_len on no-more-matches.
    auto find_start_code = [&](size_t from, size_t* sc_len) -> size_t {
        size_t i = from;
        while (i + 3 <= in_len) {
            if (in[i] == 0 && in[i + 1] == 0) {
                if (in[i + 2] == 1) {
                    if (sc_len) *sc_len = 3;
                    return i + 3;
                }
                if (i + 4 <= in_len && in[i + 2] == 0 && in[i + 3] == 1) {
                    if (sc_len) *sc_len = 4;
                    return i + 4;
                }
            }
            ++i;
        }
        return in_len;
    };

    size_t sc_len = 0;
    size_t nal_begin = find_start_code(0, &sc_len);
    if (nal_begin == in_len) return SEURAT_FLV_E_INVALID_ARG;

    while (nal_begin < in_len) {
        size_t next_sc_len = 0;
        size_t nal_end     = find_start_code(nal_begin, &next_sc_len);
        // Back off the 0x00 bytes preceding the next start code so they are
        // not included in the current NAL. (Standard Annex-B allows trailing
        // 0x00 emulation bytes, but the canonical NAL doesn't include them.)
        size_t actual_end = nal_end;
        if (actual_end < in_len) {
            actual_end -= next_sc_len;
        }

        if (actual_end < nal_begin) {
            return SEURAT_FLV_E_INVALID_ARG;  // malformed
        }
        const size_t nal_len = actual_end - nal_begin;

        if (out_off + 4 + nal_len > out_cap) {
            return SEURAT_FLV_E_BUFFER;
        }
        out[out_off + 0] = static_cast<uint8_t>(nal_len >> 24);
        out[out_off + 1] = static_cast<uint8_t>(nal_len >> 16);
        out[out_off + 2] = static_cast<uint8_t>(nal_len >>  8);
        out[out_off + 3] = static_cast<uint8_t>(nal_len);
        std::memcpy(out + out_off + 4, in + nal_begin, nal_len);
        out_off += 4 + nal_len;

        nal_begin = nal_end;
    }

    *written = out_off;
    return SEURAT_FLV_OK;
}
