/* Host stand-in for <FS.h>: a File the test fills from memory, for
 * streamFile( ), and an FS for the signatures of serveStatic( ). */
#pragma once
#include <Arduino.h>
#include <string>

namespace fs {
class File {
public:
    File() {}
    File(const std::string& data, const char* name) : _data(data), _name(name), _valid(true) {}
    explicit operator bool() const {
        return _valid;
    }
    bool isDirectory() const {
        return false;
    }
    size_t size() const {
        return _data.size();
    }
    const char* name() const {
        return _name.c_str();
    }
    int available() {
        return static_cast<int>(_data.size() - _pos);
    }
    size_t read(uint8_t* b, size_t n) {
        const size_t left = _data.size() - _pos;
        const size_t k = n < left ? n : left;
        memcpy(b, _data.data() + _pos, k);
        _pos += k;
        return k;
    }
private:
    std::string _data;
    std::string _name;
    size_t _pos = 0;
    bool _valid = false;
};

class FS {
public:
    File open(const String&, const char*) {
        return File();
    }
    bool exists(const String&) {
        return false;
    }
};
}  // namespace fs
using fs::File;
using fs::FS;
