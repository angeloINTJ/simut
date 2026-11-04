/* Host stand-in: Basic authentication is not what the test exercises. */
#pragma once
static inline int base64_encode_expected_len(int n) {
    return ((n + 2) / 3) * 4;
}
static inline int base64_encode_chars(const char*, int, char* out) {
    out[0] = '\0';
    return 0;
}
