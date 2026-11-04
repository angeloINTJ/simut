/* Host stand-in: Digest authentication is not what the test exercises. */
#pragma once
#include <Arduino.h>
class MD5Builder {
public:
    void begin() {}
    void add(const uint8_t*, unsigned int) {}
    void calculate() {}
    void getBytes(uint8_t* out) {
        memset(out, 0, 16);
    }
};
