/**
 * @file B64Decode.h
 * @brief Strict RFC 4648 base64 decoder for the HTTP Basic auth header.
 * @details Pure (no Arduino deps) so `pio test -e native` exercises it — an
 * auth-path parser is exactly the code that must not be validated only by
 * the happy case. Strict on purpose: anything outside the base64 alphabet,
 * bad padding or bad length is a hard -1, never a best-effort partial
 * credential.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Decode base64 `src` (NUL-terminated) into `dst`.
 *
 * @param dst Receives the decoded bytes plus a terminating NUL (credentials
 *            are text; a NUL inside the payload is refused so the string
 *            cannot silently truncate).
 * @param cap Size of dst. Needs decoded length + 1.
 * @return Decoded length, or -1 on any violation: length not a multiple of
 *         4, character outside the alphabet, misplaced '=', overflow, or an
 *         embedded NUL.
 */
inline int b64Decode(const char* src, char* dst, size_t cap) {
	if (!src || !dst || cap == 0) return -1;

	auto val = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+') return 62;
		if (c == '/') return 63;
		return -1;
	};

	size_t len = 0;
	while (src[len]) len++;
	if (len == 0 || (len % 4) != 0) return -1;

	size_t o = 0;
	for (size_t i = 0; i < len; i += 4) {
		int v[4];
		int pads = 0;
		for (int k = 0; k < 4; k++) {
			char c = src[i + k];
			if (c == '=') {
				/* '=' only in the last group's last two positions, and no
				 * data character after the first '='. */
				if (i + 4 < len || k < 2 || (k == 2 && src[i + 3] != '=')) return -1;
				v[k] = 0;
				pads++;
			} else {
				if (pads) return -1;
				v[k] = val(c);
				if (v[k] < 0) return -1;
			}
		}
		uint32_t block = ((uint32_t)v[0] << 18) | ((uint32_t)v[1] << 12)
		               | ((uint32_t)v[2] << 6) | (uint32_t)v[3];
		int outBytes = 3 - pads;
		for (int k = 0; k < outBytes; k++) {
			if (o + 1 >= cap) return -1; /* leave room for the NUL */
			char b = (char)((block >> (16 - 8 * k)) & 0xFF);
			if (b == '\0') return -1;    /* embedded NUL = truncated credential */
			dst[o++] = b;
		}
	}
	dst[o] = '\0';
	return (int)o;
}
