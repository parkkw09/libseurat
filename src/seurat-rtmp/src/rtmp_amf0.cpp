// =============================================================================
// seurat-rtmp — AMF0 encoder (Phase A, minimal subset).
//
// AMF0 reference: Adobe "Action Message Format — AMF0" spec (Dec 2007).
//
// Markers we emit for the publish negotiation:
//   0x00 Number          (double BE)
//   0x01 Boolean         (u8)
//   0x02 String          (u16 len + utf-8)
//   0x03 Object          (key-value pairs + object-end-marker)
//   0x05 Null            (no body)
//   0x06 Undefined       (no body) — used occasionally by servers
//   0x09 Object-end      (0x00 0x00 0x09 — key length 0 + marker)
//
// The RTMP publish negotiation uses Number / String / Boolean / Object / Null;
// we don't need parsing (reply messages are consumed via a lightweight
// state-machine in the chunk reader, added in Phase B).
//
// License: MIT.
// =============================================================================

#include "rtmp_internal.h"

#include <cstring>

namespace seurat::rtmp {

namespace {

inline void put_u8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }

inline void put_u16be(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

inline void put_f64be(std::vector<uint8_t>& v, double value) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(value),
                  "IEEE-754 64-bit double assumed");
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 7; i >= 0; --i) {
        v.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
}

inline void put_string_raw(std::vector<uint8_t>& v,
                            const char* s, size_t len) {
    put_u16be(v, static_cast<uint16_t>(len));
    v.insert(v.end(), reinterpret_cast<const uint8_t*>(s),
                      reinterpret_cast<const uint8_t*>(s) + len);
}

}  // namespace

// ---------------------------------------------------------- top-level values

void Amf0Writer::put_string(const char* s) {
    const size_t len = std::strlen(s);
    put_u8(buf_, 0x02);
    put_string_raw(buf_, s, len);
}

void Amf0Writer::put_number(double v) {
    put_u8(buf_, 0x00);
    put_f64be(buf_, v);
}

void Amf0Writer::put_boolean(bool v) {
    put_u8(buf_, 0x01);
    put_u8(buf_, v ? 1 : 0);
}

void Amf0Writer::put_null() {
    put_u8(buf_, 0x05);
}

// -------------------------------------------------------------- object

void Amf0Writer::begin_object() {
    put_u8(buf_, 0x03);
}

void Amf0Writer::begin_ecma_array(uint32_t approximate_count) {
    // 0x08 marker + 4-byte BE "associative count" (informational hint,
    // decoders MUST tolerate mismatches and read until the 0x09 terminator).
    put_u8(buf_, 0x08);
    put_u8(buf_, static_cast<uint8_t>(approximate_count >> 24));
    put_u8(buf_, static_cast<uint8_t>(approximate_count >> 16));
    put_u8(buf_, static_cast<uint8_t>(approximate_count >>  8));
    put_u8(buf_, static_cast<uint8_t>(approximate_count));
}

void Amf0Writer::put_obj_prop_string(const char* k, const char* v) {
    put_string_raw(buf_, k, std::strlen(k));
    put_u8(buf_, 0x02);
    put_string_raw(buf_, v, std::strlen(v));
}

void Amf0Writer::put_obj_prop_number(const char* k, double v) {
    put_string_raw(buf_, k, std::strlen(k));
    put_u8(buf_, 0x00);
    put_f64be(buf_, v);
}

void Amf0Writer::put_obj_prop_boolean(const char* k, bool v) {
    put_string_raw(buf_, k, std::strlen(k));
    put_u8(buf_, 0x01);
    put_u8(buf_, v ? 1 : 0);
}

void Amf0Writer::end_object() {
    // Empty key (u16=0) then the object-end marker (0x09).
    put_u16be(buf_, 0);
    put_u8(buf_, 0x09);
}

// ============================================================== Amf0Reader

namespace {

constexpr uint8_t AMF0_NUMBER      = 0x00;
constexpr uint8_t AMF0_BOOLEAN     = 0x01;
constexpr uint8_t AMF0_STRING      = 0x02;
constexpr uint8_t AMF0_OBJECT      = 0x03;
constexpr uint8_t AMF0_NULL        = 0x05;
constexpr uint8_t AMF0_UNDEFINED   = 0x06;
constexpr uint8_t AMF0_ECMA_ARRAY  = 0x08;
constexpr uint8_t AMF0_OBJECT_END  = 0x09;
constexpr uint8_t AMF0_STRICT_ARR  = 0x0A;
constexpr uint8_t AMF0_DATE        = 0x0B;
constexpr uint8_t AMF0_LONG_STRING = 0x0C;

inline uint16_t read_u16be(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t read_u32be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}
inline double read_f64be(const uint8_t* p) {
    uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) bits = (bits << 8) | p[i];
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

}  // namespace

int Amf0Reader::read_short_string(std::string* out) {
    if (end_ - p_ < 2) return SEURAT_RTMP_E_PROTOCOL;
    const uint16_t len = read_u16be(p_);
    p_ += 2;
    if (static_cast<size_t>(end_ - p_) < len) return SEURAT_RTMP_E_PROTOCOL;
    out->assign(reinterpret_cast<const char*>(p_), len);
    p_ += len;
    return SEURAT_RTMP_OK;
}

int Amf0Reader::read_string(std::string* out) {
    if (eof()) return SEURAT_RTMP_E_PROTOCOL;
    const uint8_t m = *p_++;
    if (m == AMF0_STRING) {
        return read_short_string(out);
    }
    if (m == AMF0_LONG_STRING) {
        if (end_ - p_ < 4) return SEURAT_RTMP_E_PROTOCOL;
        const uint32_t len = read_u32be(p_);
        p_ += 4;
        if (static_cast<size_t>(end_ - p_) < len) return SEURAT_RTMP_E_PROTOCOL;
        out->assign(reinterpret_cast<const char*>(p_), len);
        p_ += len;
        return SEURAT_RTMP_OK;
    }
    return SEURAT_RTMP_E_PROTOCOL;
}

int Amf0Reader::read_number(double* out) {
    if (eof() || *p_ != AMF0_NUMBER) return SEURAT_RTMP_E_PROTOCOL;
    ++p_;
    if (end_ - p_ < 8) return SEURAT_RTMP_E_PROTOCOL;
    *out = read_f64be(p_);
    p_ += 8;
    return SEURAT_RTMP_OK;
}

int Amf0Reader::read_boolean(bool* out) {
    if (eof() || *p_ != AMF0_BOOLEAN) return SEURAT_RTMP_E_PROTOCOL;
    ++p_;
    if (eof()) return SEURAT_RTMP_E_PROTOCOL;
    *out = (*p_++ != 0);
    return SEURAT_RTMP_OK;
}

int Amf0Reader::read_null() {
    if (eof() || *p_ != AMF0_NULL) return SEURAT_RTMP_E_PROTOCOL;
    ++p_;
    return SEURAT_RTMP_OK;
}

int Amf0Reader::read_undefined() {
    if (eof() || *p_ != AMF0_UNDEFINED) return SEURAT_RTMP_E_PROTOCOL;
    ++p_;
    return SEURAT_RTMP_OK;
}

int Amf0Reader::skip_value() {
    if (eof()) return SEURAT_RTMP_E_PROTOCOL;
    const uint8_t m = *p_++;
    switch (m) {
        case AMF0_NUMBER:
            if (end_ - p_ < 8) return SEURAT_RTMP_E_PROTOCOL;
            p_ += 8;
            return SEURAT_RTMP_OK;
        case AMF0_BOOLEAN:
            if (eof()) return SEURAT_RTMP_E_PROTOCOL;
            ++p_;
            return SEURAT_RTMP_OK;
        case AMF0_STRING: {
            if (end_ - p_ < 2) return SEURAT_RTMP_E_PROTOCOL;
            const uint16_t len = read_u16be(p_);
            p_ += 2;
            if (static_cast<size_t>(end_ - p_) < len) return SEURAT_RTMP_E_PROTOCOL;
            p_ += len;
            return SEURAT_RTMP_OK;
        }
        case AMF0_LONG_STRING: {
            if (end_ - p_ < 4) return SEURAT_RTMP_E_PROTOCOL;
            const uint32_t len = read_u32be(p_);
            p_ += 4;
            if (static_cast<size_t>(end_ - p_) < len) return SEURAT_RTMP_E_PROTOCOL;
            p_ += len;
            return SEURAT_RTMP_OK;
        }
        case AMF0_NULL:
        case AMF0_UNDEFINED:
            return SEURAT_RTMP_OK;
        case AMF0_OBJECT: {
            while (!eof()) {
                if (end_ - p_ < 2) return SEURAT_RTMP_E_PROTOCOL;
                const uint16_t klen = read_u16be(p_);
                if (klen == 0) {
                    // Expect object-end marker (0x09).
                    p_ += 2;
                    if (eof() || *p_++ != AMF0_OBJECT_END) return SEURAT_RTMP_E_PROTOCOL;
                    return SEURAT_RTMP_OK;
                }
                p_ += 2;
                if (static_cast<size_t>(end_ - p_) < klen) return SEURAT_RTMP_E_PROTOCOL;
                p_ += klen;
                int rc = skip_value();
                if (rc != SEURAT_RTMP_OK) return rc;
            }
            return SEURAT_RTMP_E_PROTOCOL;
        }
        case AMF0_ECMA_ARRAY: {
            if (end_ - p_ < 4) return SEURAT_RTMP_E_PROTOCOL;
            p_ += 4;  // associative count (hint)
            // then object-style pairs terminated by empty-key + 0x09
            while (!eof()) {
                if (end_ - p_ < 2) return SEURAT_RTMP_E_PROTOCOL;
                const uint16_t klen = read_u16be(p_);
                if (klen == 0) {
                    p_ += 2;
                    if (eof() || *p_++ != AMF0_OBJECT_END) return SEURAT_RTMP_E_PROTOCOL;
                    return SEURAT_RTMP_OK;
                }
                p_ += 2;
                if (static_cast<size_t>(end_ - p_) < klen) return SEURAT_RTMP_E_PROTOCOL;
                p_ += klen;
                int rc = skip_value();
                if (rc != SEURAT_RTMP_OK) return rc;
            }
            return SEURAT_RTMP_E_PROTOCOL;
        }
        case AMF0_STRICT_ARR: {
            if (end_ - p_ < 4) return SEURAT_RTMP_E_PROTOCOL;
            const uint32_t n = read_u32be(p_);
            p_ += 4;
            for (uint32_t i = 0; i < n; ++i) {
                int rc = skip_value();
                if (rc != SEURAT_RTMP_OK) return rc;
            }
            return SEURAT_RTMP_OK;
        }
        case AMF0_DATE: {
            if (end_ - p_ < 10) return SEURAT_RTMP_E_PROTOCOL;
            p_ += 10;  // 8-byte double + 2-byte tz
            return SEURAT_RTMP_OK;
        }
        default:
            return SEURAT_RTMP_E_PROTOCOL;  // unsupported marker
    }
}

int Amf0Reader::begin_object() {
    if (eof()) return SEURAT_RTMP_E_PROTOCOL;
    const uint8_t m = *p_++;
    if (m != AMF0_OBJECT) return SEURAT_RTMP_E_PROTOCOL;
    return SEURAT_RTMP_OK;
}

int Amf0Reader::next_property(std::string* key, bool* end_of_object) {
    *end_of_object = false;
    if (end_ - p_ < 2) return SEURAT_RTMP_E_PROTOCOL;
    const uint16_t klen = read_u16be(p_);
    if (klen == 0) {
        p_ += 2;
        if (eof() || *p_++ != AMF0_OBJECT_END) return SEURAT_RTMP_E_PROTOCOL;
        *end_of_object = true;
        return SEURAT_RTMP_OK;
    }
    p_ += 2;
    if (static_cast<size_t>(end_ - p_) < klen) return SEURAT_RTMP_E_PROTOCOL;
    key->assign(reinterpret_cast<const char*>(p_), klen);
    p_ += klen;
    return SEURAT_RTMP_OK;
}

int Amf0Reader::find_object_string(const char* key, std::string* out) {
    // Caller must have already consumed the object marker via begin_object().
    while (!eof()) {
        std::string k;
        bool end_of_obj = false;
        int rc = next_property(&k, &end_of_obj);
        if (rc != SEURAT_RTMP_OK) return rc;
        if (end_of_obj) return SEURAT_RTMP_E_PROTOCOL;  // not found

        if (k == key) {
            // Value must be a string.
            if (eof() || *p_ != AMF0_STRING) return SEURAT_RTMP_E_PROTOCOL;
            return read_string(out);
        }
        // Not the target key: skip the value.
        int sv = skip_value();
        if (sv != SEURAT_RTMP_OK) return sv;
    }
    return SEURAT_RTMP_E_PROTOCOL;
}

}  // namespace seurat::rtmp
