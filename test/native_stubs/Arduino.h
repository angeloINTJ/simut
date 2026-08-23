/**
 * @file    test/native_stubs/Arduino.h
 * @brief   Minimal Arduino.h stub for native (host) unit testing.
 * @details Extended for CommandParser tests — String subset used by parseCliCommand.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <algorithm>
#include <cctype>

namespace simut_native {
    extern uint32_t fake_millis_value;
}
inline uint32_t millis() { return simut_native::fake_millis_value; }
inline void set_native_millis(uint32_t v) { simut_native::fake_millis_value = v; }

/* strlcpy — BSD, ausente na glibc; o firmware tem via Arduino core.
 * Mantida em sync com a semântica do alvo (dest sempre terminado em NUL). */
inline size_t strlcpy(char* dst, const char* src, size_t cap) {
    if (!dst || cap == 0) return src ? strlen(src) : 0;
    if (!src) { dst[0] = '\0'; return 0; }
    size_t slen = strlen(src);
    size_t n = (slen < cap - 1) ? slen : (cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
    return slen;
}

class String {
public:
    String() : data_() {}
    String(const char* s) : data_(s ? s : "") {}
    String(const std::string& s) : data_(s) {}

    size_t length() const { return data_.length(); }
    char operator[](size_t i) const { return i < data_.size() ? data_[i] : '\0'; }
    const char* c_str() const { return data_.c_str(); }

    void reserve(size_t) { /* no-op on host */ }

    String& operator+=(char c) { data_ += c; return *this; }
    String& operator+=(const char* s) { if (s) data_ += s; return *this; }

    /* toInt/toFloat mirror the TARGET, not the host libc. On the ferro
     * (ArduinoCore-API) toInt() is atol() — newlib strtol with a 32-bit
     * long, which SATURATES at ±2^31-1/-2^31 on overflow — and toFloat() is
     * float(atof()), which overflows to ±inf. The old std::stol/std::stof
     * threw on overflow and the catch turned that into 0/0.0f, so the host
     * diverged from the hardware on exactly the inputs a fuzzer finds. */
    long toInt() const {
        char* end = nullptr;
        const long long v = strtoll(data_.c_str(), &end, 10);
        if (end == data_.c_str()) return 0;          /* no digits → atol gives 0 */
        if (v > 2147483647LL) return 2147483647L;    /* 32-bit strtol saturation */
        if (v < -2147483648LL) return (long)-2147483648LL;
        return (long)v;
    }

    float toFloat() const {
        return (float)strtod(data_.c_str(), nullptr); /* huge input → ±inf, like atof */
    }

    void trim() {
        size_t start = 0;
        while (start < data_.size() && std::isspace((unsigned char)data_[start])) start++;
        size_t end = data_.size();
        while (end > start && std::isspace((unsigned char)data_[end - 1])) end--;
        data_ = data_.substr(start, end - start);
    }

    int indexOf(char ch, int from = 0) const {
        if (from < 0) from = 0;
        size_t pos = data_.find(ch, (size_t)from);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    int indexOf(const char* s, int from = 0) const {
        if (!s || from < 0) return -1;
        size_t pos = data_.find(s, (size_t)from);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    int lastIndexOf(char ch) const {
        size_t pos = data_.rfind(ch);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    String substring(size_t begin, size_t end = (size_t)-1) const {
        if (begin >= data_.size()) return String("");
        if (end == (size_t)-1 || end > data_.size()) end = data_.size();
        if (end <= begin) return String("");
        return String(data_.substr(begin, end - begin));
    }

    bool startsWith(const char* prefix) const {
        if (!prefix) return false;
        size_t n = strlen(prefix);
        return data_.size() >= n && data_.compare(0, n, prefix) == 0;
    }

    bool endsWith(const char* suffix) const {
        if (!suffix) return false;
        size_t n = strlen(suffix);
        if (data_.size() < n) return false;
        return data_.compare(data_.size() - n, n, suffix) == 0;
    }

    void toLowerCase() {
        std::transform(data_.begin(), data_.end(), data_.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
    }

    void replace(const char* from, const char* to) {
        if (!from || !to) return;
        std::string f(from), t(to);
        size_t pos = 0;
        while ((pos = data_.find(f, pos)) != std::string::npos) {
            data_.replace(pos, f.length(), t);
            pos += t.length();
        }
    }

    void remove(size_t idx, size_t count = 1) {
        if (idx < data_.size()) {
            data_.erase(idx, std::min(count, data_.size() - idx));
        }
    }

    bool operator==(const char* s) const { return s && data_ == s; }
    bool operator==(const String& o) const { return data_ == o.data_; }

private:
    std::string data_;
};

/* ============================================================================
 *  Minimal File stub — host-side tests only.
 *  The encode/decode unit tests don't use File I/O; this stub exists so the
 *  header compiles. Real File I/O is tested on-device.
 * ============================================================================ */

#include <cstdio>
#include <vector>

class File {
public:
    File() : pos_(0), mode_(0) {}

    size_t write(const uint8_t *buf, size_t len) {
        if (mode_ != 'w' && mode_ != 'a') return 0;
        data_.insert(data_.end(), buf, buf + len);
        pos_ = data_.size();
        return len;
    }
    size_t read(uint8_t *buf, size_t len) {
        if (mode_ != 'r' && mode_ != 'w') return 0;
        size_t avail = data_.size() - pos_;
        if (len > avail) len = avail;
        if (avail > 0) memcpy(buf, data_.data() + pos_, len);
        pos_ += len;
        return len;
    }
    void seek(size_t pos) {
        if (pos <= data_.size()) pos_ = pos;
    }
    size_t position() const { return pos_; }
    int available() const { return (int)(data_.size() - pos_); }
    size_t size() const { return data_.size(); }
    void close() { mode_ = 0; }

    /* Test helpers */
    void openForWrite() { data_.clear(); pos_ = 0; mode_ = 'w'; }
    void openForRead(const uint8_t *buf, size_t len) {
        data_.assign(buf, buf + len); pos_ = 0; mode_ = 'r';
    }

private:
    std::vector<uint8_t> data_;
    size_t pos_;
    char mode_;
};
