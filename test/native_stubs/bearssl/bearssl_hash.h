/**
 * Native stub — NOT SHA-256.
 *
 * NetworkManager derives the setup access point's WPA2 key by hashing the
 * board id (finding V-05), and the state machine under test has to link. What
 * it does NOT have here is a real digest: this is a trivial mixing function
 * that produces a deterministic 32 bytes and nothing more.
 *
 * That distinction matters enough to state loudly, because the shape invites
 * a test that "checks the AP key". Any such test would be measuring this
 * function, not the firmware's, and would pass while the real derivation was
 * broken. If the key derivation is ever worth testing, it needs the real
 * BearSSL on the host — not this.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t acc; size_t len; } br_sha256_context;

static inline void br_sha256_init(br_sha256_context* c) { c->acc = 0x9E3779B9u; c->len = 0; }
static inline void br_sha256_update(br_sha256_context* c, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) { c->acc = (c->acc * 31u) + p[i]; c->len++; }
}
static inline void br_sha256_out(const br_sha256_context* c, void* dst) {
    uint8_t* o = (uint8_t*)dst;
    uint32_t a = c->acc;
    for (int i = 0; i < 32; i++) { a = (a * 1103515245u) + 12345u; o[i] = (uint8_t)(a >> 24); }
}
