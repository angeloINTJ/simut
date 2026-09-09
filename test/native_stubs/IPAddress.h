/* Native stub — enough IPAddress for the network state machine to compile and
 * for a test to say "the interface has an address" or "it does not". */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

class IPAddress {
public:
    IPAddress( ) : _v(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : _v(((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d) {}
    explicit IPAddress(uint32_t v) : _v(v) {}

    bool fromString(const char* s) {
        unsigned a, b, c, d;
        if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
        _v = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
        return true;
    }
    bool isSet( ) const { return _v != 0; }
    operator uint32_t( ) const { return _v; }
    uint8_t operator[](int i) const { return (uint8_t)((_v >> (8 * (3 - i))) & 0xFF); }

    class String toString( ) const;
private:
    uint32_t _v;
};
