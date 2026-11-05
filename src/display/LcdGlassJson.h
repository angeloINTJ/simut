/**
 * @file display/LcdGlassJson.h
 * @brief The alpha's glass as JSON: the one formatter GET /api/lcd and the host tools share.
 *
 * @details lcdGlassJson( ) writes what DisplayManager::lcdGlass( ) read (display/LcdGlass.h) as
 *
 *     {"linhas":["<32 hex>","<32 hex>"],"cgram":["<16 hex>", ... eight],"seq":N,"age_ms":M}
 *
 * the frame tools/lcd_alpha/lcd.py --quadro reads (`linhas` the two DDRAM rows of 16 codes, `cgram` the eight glyphs of 8 rows; the
 * keys are Portuguese because the reader's are), with the shadow's count and the age of the frame. The hex is LOWERCASE: lcd.py parses
 * both, the host program that draws the manual's figures (tools/lcd_alpha/amostras.cpp) has always printed lowercase, and
 * test/test_validators/alpha_glass_json.cpp pins it. `age_ms` is `nowMs - g.blitMs` in 32 bits, so the wrap of millis( ) every 49.7
 * days gives a small age and not a huge one.
 *
 * The handler (WebManager_History.cpp) formats into a buffer on its stack, so the bound is the contract:
 *   - it returns the length written, without the NUL;
 *   - it returns 0 and WRITES NOTHING when the text and its NUL do not fit in `cap`, or when `out` is null. Not a truncated text, not
 *     a NUL at out[0]: a caller that ignores the 0 sees its own buffer unchanged, and the byte at out[cap] is never touched;
 *   - no heap, no printf, no float: the text is put together from the literals below and two hand-made decimals. Its length is
 *     counted from the same literals (kFixed), so the bound check cannot drift from the writer; the test holds the count to the
 *     text it writes.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "LcdGlass.h"

namespace LcdGlassJsonImpl {

/* The text between the numbers. Each is counted by its own sizeof( ), so the length the bound check uses is the writer's. */
constexpr char kHead[]  = "{\"linhas\":[\"";           /* up to the first hex digit of row 0 */
constexpr char kRows[]  = "\",\"";                      /* between the two rows, and between two glyphs (the closing and the opening quote) */
constexpr char kCgram[] = "\"],\"cgram\":[\"";          /* from row 1 to the first glyph */
constexpr char kSeq[]   = "\"],\"seq\":";               /* from the last glyph to the count */
constexpr char kAge[]   = ",\"age_ms\":";               /* from the count to the age */
constexpr char kTail[]  = "}";

constexpr size_t kRowHex    = 2 * 16;                   /* a row of 16 codes */
constexpr size_t kGlyphHex  = 2 * 8;                    /* a glyph of 8 rows */
constexpr size_t kMaxDigits = 10;                       /* 4294967295 */

/** The characters that do not depend on the glass: every literal, the two rows and the eight glyphs; the two numbers are counted apart. */
constexpr size_t kFixed = (sizeof(kHead) - 1) + kRowHex + (sizeof(kRows) - 1) + kRowHex + (sizeof(kCgram) - 1)
                        + 8 * kGlyphHex + 7 * (sizeof(kRows) - 1) + (sizeof(kSeq) - 1) + (sizeof(kAge) - 1) + (sizeof(kTail) - 1);

inline size_t decimalLength(uint32_t v) {
	size_t n = 1;
	while (v >= 10u) { v /= 10u; n++; }
	return n;
}

inline char *putText(char *p, const char *s) {
	while (*s) *p++ = *s++;
	return p;
}

inline char *putHex(char *p, const uint8_t *b, size_t n) {
	for (size_t i = 0; i < n; i++) {
		const uint8_t hi = (uint8_t)(b[i] >> 4), lo = (uint8_t)(b[i] & 0x0F);
		*p++ = (char)(hi < 10 ? '0' + hi : 'a' + (hi - 10));
		*p++ = (char)(lo < 10 ? '0' + lo : 'a' + (lo - 10));
	}
	return p;
}

inline char *putDecimal(char *p, uint32_t v) {
	char digits[kMaxDigits];
	size_t n = 0;
	do { digits[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
	while (n) *p++ = digits[--n];
	return p;
}

}  // namespace LcdGlassJsonImpl

/** The longest text lcdGlassJson( ) writes, and its NUL: a buffer of this size always fits (282 B on 2026-11-04). */
constexpr size_t LCD_GLASS_JSON_MAX = LcdGlassJsonImpl::kFixed + 2 * LcdGlassJsonImpl::kMaxDigits + 1;

/** The glass `g`, read at `nowMs` (millis( ) of the reader), as the JSON above in out[0..cap). Returns its length without the NUL, or 0 and nothing written. */
inline size_t lcdGlassJson(const LcdGlass &g, uint32_t nowMs, char *out, size_t cap) {
	using namespace LcdGlassJsonImpl;
	const uint32_t age = nowMs - g.blitMs;
	const size_t len = kFixed + decimalLength(g.seq) + decimalLength(age);
	if (!out || cap <= len) return 0;                  /* the NUL needs the byte after the text */
	char *p = out;
	p = putText(p, kHead);
	p = putHex(p, g.ddram[0], 16);
	p = putText(p, kRows);
	p = putHex(p, g.ddram[1], 16);
	p = putText(p, kCgram);
	for (uint8_t s = 0; s < 8; s++) {
		if (s) p = putText(p, kRows);
		p = putHex(p, g.cgram[s], 8);
	}
	p = putText(p, kSeq);
	p = putDecimal(p, g.seq);
	p = putText(p, kAge);
	p = putDecimal(p, age);
	p = putText(p, kTail);
	*p = '\0';
	return (size_t)(p - out);
}
