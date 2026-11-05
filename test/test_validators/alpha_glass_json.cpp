/**
 * @file    test/test_validators/alpha_glass_json.cpp
 * @brief   The alpha's glass as JSON (display/LcdGlassJson.h): the one formatter GET /api/lcd and the host tools share.
 * @details lcdGlassJson( ) writes {"linhas":["<32 hex>","<32 hex>"],"cgram":["<16 hex>" x 8],"seq":N,"age_ms":M}, the frame
 *          tools/lcd_alpha/lcd.py --quadro reads (lowercase hex, pinned here). The device writes it into a buffer on the stack, so
 *          what is held is the bound: a buffer of exactly the text and its NUL fits, one byte less gets nothing written (not one byte,
 *          the canary after `cap` and every byte before it stay as they were), and a null buffer or a zero cap is a refusal and not a
 *          crash. The exact text is written out by hand below (a formatter checked against itself proves nothing); a second
 *          implementation, snprintf, is held to it on random glasses and at every digit count of the two numbers.
 *          `age_ms` is `nowMs - blitMs` in 32 bits, so the clock's wrap (every 49.7 days) is a small age and not a huge one.
 *
 * @project SIMUT
 * @license MIT License
 */
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "display/LcdGlassJson.h"
#include "FakeHd44780.h"

namespace {

constexpr uint8_t CANARY = 0xA5;

/* A glass whose every byte is different, and whose hex digits include every letter from a to f. */
LcdGlass knownGlass( ) {
	LcdGlass g; memset(&g, 0, sizeof(g));
	for (int c = 0; c < 16; c++) { g.ddram[0][c] = (uint8_t)c; g.ddram[1][c] = (uint8_t)(0xF0 + c); }
	for (int s = 0; s < 8; s++) for (int i = 0; i < 8; i++) g.cgram[s][i] = (uint8_t)(s * 8 + i);
	g.seq = 4242; g.blitMs = 1000;
	return g;
}

/* knownGlass( ) at nowMs = 1250, written by hand. */
const char KNOWN[] =
	"{\"linhas\":[\"000102030405060708090a0b0c0d0e0f\",\"f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff\"],"
	"\"cgram\":[\"0001020304050607\",\"08090a0b0c0d0e0f\",\"1011121314151617\",\"18191a1b1c1d1e1f\","
	"\"2021222324252627\",\"28292a2b2c2d2e2f\",\"3031323334353637\",\"38393a3b3c3d3e3f\"],"
	"\"seq\":4242,\"age_ms\":250}";

/* The same text by a second implementation, for the cases that cannot be written out by hand. */
size_t reference(const LcdGlass &g, uint32_t nowMs, char *out, size_t cap) {
	size_t n = 0;
	auto put = [&](const char *fmt, unsigned v) { n += (size_t)snprintf(out + n, cap - n, fmt, v); };
	n += (size_t)snprintf(out + n, cap - n, "{\"linhas\":[\"");
	for (int r = 0; r < 2; r++) {
		for (int c = 0; c < 16; c++) put("%02x", g.ddram[r][c]);
		n += (size_t)snprintf(out + n, cap - n, r ? "\"]," : "\",\"");
	}
	n += (size_t)snprintf(out + n, cap - n, "\"cgram\":[");
	for (int s = 0; s < 8; s++) {
		n += (size_t)snprintf(out + n, cap - n, s ? ",\"" : "\"");
		for (int i = 0; i < 8; i++) put("%02x", g.cgram[s][i]);
		n += (size_t)snprintf(out + n, cap - n, "\"");
	}
	n += (size_t)snprintf(out + n, cap - n, "],\"seq\":");
	put("%u", g.seq);
	n += (size_t)snprintf(out + n, cap - n, ",\"age_ms\":");
	put("%u", nowMs - g.blitMs);
	n += (size_t)snprintf(out + n, cap - n, "}");
	return n;
}

}  // namespace

void test_alpha_glass_json_exact_text_for_a_known_glass(void) {
	const LcdGlass g = knownGlass( );
	char buf[LCD_GLASS_JSON_MAX];
	memset(buf, CANARY, sizeof(buf));
	const size_t n = lcdGlassJson(g, 1250, buf, sizeof(buf));
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(strlen(KNOWN), n, "the length returned is the text's, without the NUL");
	TEST_ASSERT_EQUAL_STRING(KNOWN, buf);
	TEST_ASSERT_EQUAL_UINT8_MESSAGE('\0', (uint8_t)buf[n], "the text ends with a NUL");
	TEST_ASSERT_TRUE_MESSAGE(strchr(buf, 'A') == nullptr && strchr(buf, 'F') == nullptr, "the hex is lowercase: pinned, though lcd.py reads both");
}

void test_alpha_glass_json_of_a_fake_lcd_after_a_glyph_and_a_blit(void) {
	/* begin( ) publishes the blank glass (the count goes 0 to 2), createChar( ) a glyph (4), blit( ) the frame (6), which stamps nowMs. */
	FakeHd44780 lcd;
	lcd.begin( );
	const uint8_t x[8] = { 0x1F, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x1F, 0x00 };
	lcd.createChar(7, x);
	lcd.nowMs = 100;
	lcd.setCursor(0, 0); lcd.print("SIMUT 9.8.7");
	lcd.setCursor(0, 1); lcd.print("  Inicializando ");
	lcd.blit( );
	LcdGlass g; memset(&g, 0xEE, sizeof(g));
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, lcd.readGlass(g));
	char buf[LCD_GLASS_JSON_MAX];
	const size_t n = lcdGlassJson(g, 130, buf, sizeof(buf));
	/* the rows and the glyph are the ones Python hexlified from "SIMUT 9.8.7" and "  Inicializando " (2026-11-04), not the formatter's own */
	const char want[] =
		"{\"linhas\":[\"53494d555420392e382e372020202020\",\"2020496e696369616c697a616e646f20\"],"
		"\"cgram\":[\"0000000000000000\",\"0000000000000000\",\"0000000000000000\",\"0000000000000000\","
		"\"0000000000000000\",\"0000000000000000\",\"0000000000000000\",\"1f110a040a111f00\"],"
		"\"seq\":6,\"age_ms\":30}";
	TEST_ASSERT_EQUAL_UINT32(strlen(want), n);
	TEST_ASSERT_EQUAL_STRING(want, buf);
}

void test_alpha_glass_json_cap_of_length_plus_one_fits_and_length_does_not(void) {
	const LcdGlass g = knownGlass( );
	const size_t len = strlen(KNOWN);
	char buf[LCD_GLASS_JSON_MAX + 16];

	/* cap = len + 1: the text and its NUL, and not a byte beyond. */
	memset(buf, CANARY, sizeof(buf));
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(len, lcdGlassJson(g, 1250, buf, len + 1), "cap = length + 1 fits");
	TEST_ASSERT_EQUAL_STRING(KNOWN, buf);
	for (size_t i = len + 1; i < sizeof(buf); i++)
		TEST_ASSERT_EQUAL_UINT8_MESSAGE(CANARY, (uint8_t)buf[i], "cap = length + 1: nothing is written past the NUL");

	/* cap = len, len - 1, ... : refused, and the buffer is as it was, every byte (in particular out[cap]). */
	const size_t caps[] = { len, len - 1, len - 2, len / 2, 2, 1 };
	for (size_t cap : caps) {
		memset(buf, CANARY, sizeof(buf));
		char what[80]; snprintf(what, sizeof(what), "cap = %u (length %u) returns 0", (unsigned)cap, (unsigned)len);
		TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lcdGlassJson(g, 1250, buf, cap), what);
		for (size_t i = 0; i < sizeof(buf); i++) {
			snprintf(what, sizeof(what), "cap = %u: byte %u is untouched", (unsigned)cap, (unsigned)i);
			TEST_ASSERT_EQUAL_UINT8_MESSAGE(CANARY, (uint8_t)buf[i], what);
		}
	}
}

void test_alpha_glass_json_null_buffer_and_zero_cap_write_nothing(void) {
	const LcdGlass g = knownGlass( );
	char buf[LCD_GLASS_JSON_MAX + 16];
	memset(buf, CANARY, sizeof(buf));
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lcdGlassJson(g, 1250, buf, 0), "cap 0 returns 0");
	for (size_t i = 0; i < sizeof(buf); i++) TEST_ASSERT_EQUAL_UINT8_MESSAGE(CANARY, (uint8_t)buf[i], "cap 0: the buffer is untouched");
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lcdGlassJson(g, 1250, nullptr, 1000), "a null buffer with room claimed returns 0, and does not crash");
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lcdGlassJson(g, 1250, nullptr, 0), "a null buffer with no room returns 0");
}

void test_alpha_glass_json_age_is_unsigned_across_the_wrap(void) {
	struct Row { uint32_t blitMs, nowMs; const char *tail; } rows[] = {
		{ 0xFFFFFF00u, 0x00000100u, "\"age_ms\":512}" },          /* millis( ) wrapped between the blit and the read: 0x100 - 0xFFFFFF00 mod 2^32 = 512 */
		{ 5, 5, "\"age_ms\":0}" },                                 /* read in the millisecond of the blit */
		{ 0, 0xFFFFFFFFu, "\"age_ms\":4294967295}" },              /* the longest age: 10 digits */
		{ 0xFFFFFFFFu, 0, "\"age_ms\":1}" },                       /* one millisecond across the wrap */
		{ 1, 0, "\"age_ms\":4294967295}" },                        /* a clock behind the stamp is not a negative age: it is read as unsigned, and the caller sees 49 days */
	};
	for (const Row &r : rows) {
		LcdGlass g = knownGlass( );
		g.blitMs = r.blitMs;
		char buf[LCD_GLASS_JSON_MAX];
		const size_t n = lcdGlassJson(g, r.nowMs, buf, sizeof(buf));
		char what[120]; snprintf(what, sizeof(what), "blit %u, now %u: the text ends with %s", (unsigned)r.blitMs, (unsigned)r.nowMs, r.tail);
		TEST_ASSERT_TRUE_MESSAGE(n > strlen(r.tail) && strcmp(buf + n - strlen(r.tail), r.tail) == 0, what);
	}
	/* The worst case is what the handler's buffer is sized by: 10 digits for the count and 10 for the age, plus the NUL. */
	LcdGlass g = knownGlass( ); g.seq = 4294967294u; g.blitMs = 0;
	char buf[LCD_GLASS_JSON_MAX];
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(LCD_GLASS_JSON_MAX - 1, lcdGlassJson(g, 0xFFFFFFFFu, buf, sizeof(buf)), "LCD_GLASS_JSON_MAX is the longest text and its NUL");
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, lcdGlassJson(g, 0xFFFFFFFFu, buf, LCD_GLASS_JSON_MAX - 1), "one byte less does not fit the longest text");
}

void test_alpha_glass_json_equals_a_reference_at_every_digit_count_and_on_random_glasses(void) {
	/* The digit counts of the two numbers: 1 to 10, at both sides of each power of ten. */
	uint32_t edges[40]; int ne = 0;
	uint32_t p = 1;
	for (int d = 0; d < 10; d++, p *= 10u) { edges[ne++] = p - 1u; edges[ne++] = p; edges[ne++] = p + 1u; }
	edges[ne++] = 0xFFFFFFFFu;
	for (int i = 0; i < ne; i++) {
		LcdGlass g = knownGlass( );
		g.seq = edges[i];
		g.blitMs = 0;
		char got[LCD_GLASS_JSON_MAX], want[LCD_GLASS_JSON_MAX + 8];
		const size_t n = lcdGlassJson(g, edges[i], got, sizeof(got)), m = reference(g, edges[i], want, sizeof(want));
		char what[80]; snprintf(what, sizeof(what), "seq and age %u", (unsigned)edges[i]);
		TEST_ASSERT_EQUAL_UINT32_MESSAGE(m, n, what);
		TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, what);
	}
	/* Random glasses, from a fixed seed. */
	uint32_t s = 0x2F6E2B1u;
	auto next = [&]( ) { s = s * 1664525u + 1013904223u; return s; };
	for (int it = 0; it < 300; it++) {
		LcdGlass g; memset(&g, 0, sizeof(g));
		for (int r = 0; r < 2; r++) for (int c = 0; c < 16; c++) g.ddram[r][c] = (uint8_t)(next( ) >> 24);
		for (int k = 0; k < 8; k++) for (int i = 0; i < 8; i++) g.cgram[k][i] = (uint8_t)(next( ) >> 24);
		g.seq = next( ) >> (next( ) >> 27);        /* 0 to 10 digits */
		g.blitMs = next( );
		const uint32_t now = (it & 1) ? g.blitMs + (next( ) >> (next( ) >> 27)) : next( );
		char got[LCD_GLASS_JSON_MAX], want[LCD_GLASS_JSON_MAX + 8];
		const size_t n = lcdGlassJson(g, now, got, sizeof(got)), m = reference(g, now, want, sizeof(want));
		char what[60]; snprintf(what, sizeof(what), "random glass %d", it);
		TEST_ASSERT_EQUAL_UINT32_MESSAGE(m, n, what);
		TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, what);
	}
}
