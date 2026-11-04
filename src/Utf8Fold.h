/**
 * @file Utf8Fold.h
 * @brief UTF-8 folded to what the classic 5x7 font can draw.
 *
 * @details The License screen, the boot lines and the CLI print with a 7-bit
 * font, and the text they print is UTF-8: language packs, the opening of the
 * License screen, the copyright holder's name. A Latin letter loses its accent
 * ("ção" -> "cao"), typographic punctuation becomes its ASCII cousin, the
 * Spanish opening marks disappear (the closing mark already says what the
 * sentence is), and anything else is one '?' per character. The one text that
 * is not UTF-8 is the key of a boot line: tr( ) returns Latin-1, so
 * latin1FoldNext( ) and bootLineAscii( ) at the end of this file fold that part
 * by its own encoding (the suffix of the same line is UTF-8).
 *
 * Until 2026-10-02 this lived twice, as DisplayManager::unaccent( ) in
 * DisplayManager_LangParser.cpp and in DisplayManager_Alpha.cpp, and both
 * printed one '?' per BYTE of a sequence outside the two Latin-1 rows: the
 * dash in "versão original — é ela" read "???". Both also stepped two bytes
 * past a lead byte without looking, so a string that ended in one was read
 * past its terminator. One character in, at most one character out, so the
 * output is never longer than the input and a column count of characters is
 * also a column count of what gets drawn.
 *
 * Pure and header-only: `pio test -e native` covers it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/** Folds the character at p and moves p past it, never past a terminator.
 *  Returns what to print, or 0 when the character prints as nothing. */
inline char utf8FoldNext(const unsigned char*& p) {
	const unsigned char c = p[0];
	if (c < 0x80) { p++; return (char)c; }
	const size_t want = (c >= 0xC2 && c <= 0xDF) ? 2
	                  : (c >= 0xE0 && c <= 0xEF) ? 3
	                  : (c >= 0xF0 && c <= 0xF4) ? 4 : 1;
	size_t len = 1;
	while (len < want && (p[len] & 0xC0) == 0x80) len++;   /* a NUL is not a continuation */
	const unsigned char c2 = p[1];
	const unsigned char c3 = (len > 2) ? p[2] : 0;
	p += len;
	if (len != want || want == 1) return '?';
	if (c == 0xC3) {
		switch (c2) {
		case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: return 'A';
		case 0x87: return 'C';
		case 0x88: case 0x89: case 0x8A: case 0x8B: return 'E';
		case 0x8C: case 0x8D: case 0x8E: case 0x8F: return 'I';
		case 0x91: return 'N';
		case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x98: return 'O';
		case 0x99: case 0x9A: case 0x9B: case 0x9C: return 'U';
		case 0x9D: return 'Y';
		case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: case 0xA6: return 'a';
		case 0xA7: return 'c';
		case 0xA8: case 0xA9: case 0xAA: case 0xAB: return 'e';
		case 0xAC: case 0xAD: case 0xAE: case 0xAF: return 'i';
		case 0xB1: return 'n';
		case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB8: return 'o';
		case 0xB9: case 0xBA: case 0xBB: case 0xBC: return 'u';
		case 0xBD: case 0xBF: return 'y';
		default: return '?';
		}
	}
	if (c == 0xC2) {
		switch (c2) {
		case 0xA1: case 0xBF: return 0;        /* the Spanish opening marks */
		case 0xA0: return ' ';                 /* no-break space */
		case 0xA9: return 'C';                 /* copyright */
		case 0xAE: return 'R';                 /* registered */
		case 0xAB: case 0xBB: return '"';      /* guillemets */
		case 0xB0: return 'o';                 /* degree */
		case 0xB1: return '+';                 /* plus-minus */
		case 0xB2: return '2';
		case 0xB3: return '3';
		case 0xB7: return '.';                 /* middle dot */
		default: return '?';
		}
	}
	if (c == 0xE2 && c2 == 0x80) {
		switch (c3) {
		case 0x93: case 0x94: return '-';      /* en and em dash */
		case 0x98: case 0x99: return '\'';     /* single quotes */
		case 0x9C: case 0x9D: return '"';      /* double quotes */
		case 0xA2: return '*';                 /* bullet */
		case 0xA6: return '.';                 /* ellipsis, one column */
		default: return '?';
		}
	}
	return '?';
}

/** The whole string, into out (always terminated when outSize > 0). */
inline void utf8FoldAscii(const char* utf8, char* out, size_t outSize) {
	if (!out || outSize == 0) return;
	size_t o = 0;
	const unsigned char* p = (const unsigned char*)utf8;
	while (p && *p && o + 1 < outSize) {
		const char ch = utf8FoldNext(p);
		if (ch) out[o++] = ch;
	}
	out[o] = '\0';
}

/** One byte of Latin-1 folded as the character it is. Latin-1 is the first 256 code points of Unicode, so the byte is the UTF-8 pair of its
 *  code point (0xC2 and the byte for 0x80..0xBF, 0xC3 and the byte less 0x40 for 0xC0..0xFF) and goes through the table above: one table for
 *  both encodings, and a letter folds the same in either. Returns 0 when the character prints as nothing (the Spanish opening marks). */
inline char latin1FoldNext(unsigned char b) {
	if (b < 0x80) return (char)b;
	const unsigned char pair[3] = { (unsigned char)(b < 0xC0 ? 0xC2 : 0xC3), (unsigned char)(0x80 | (b & 0x3F)), 0 };
	const unsigned char* p = pair;
	return utf8FoldNext(p);
}

/** A line of the boot terminal. Its two parts do not share an encoding: the key's text is what tr( ) returns, Latin-1 (the TFT fonts index glyphs
 *  by Latin-1 code), and the suffix is what the boot formatted in, UTF-8 (a device name, an SSID: showApOnPanel( ) in AppManager_Commands.cpp).
 *  The box draws with the built-in 5x7 font, which is 7-bit, so each part goes through its own table, and the line is padded with spaces to
 *  `cols` columns AFTER the fold, so that a shorter line overwrites a longer one that was there: an accent in the suffix is two bytes in and one
 *  out. Until 2026-11-04 the two were joined and folded as UTF-8, and the key's "Conexão" (43 6F 6E 65 78 E3 6F) read as a lead byte with no
 *  continuation: "Conex?o". */
inline void bootLineAscii(const char* keyLatin1, const char* suffixUtf8, char* out, size_t outSize, size_t cols) {
	if (!out || outSize == 0) return;
	size_t n = 0;
	for (const unsigned char* p = (const unsigned char*)keyLatin1; p && *p && n + 1 < outSize; p++) {
		const char ch = latin1FoldNext(*p);
		if (ch) out[n++] = ch;
	}
	for (const unsigned char* p = (const unsigned char*)suffixUtf8; p && *p && n + 1 < outSize; ) {
		const char ch = utf8FoldNext(p);
		if (ch) out[n++] = ch;
	}
	while (n < cols && n + 1 < outSize) out[n++] = ' ';
	out[n] = '\0';
}

/** A line of the Angulo's boot ring, in UTF-8. It has the two parts of bootLineAscii( ) in the same two encodings: the key's text is what tr( ) returns,
 *  Latin-1, and the suffix is what the boot formatted in, UTF-8 (a device name, an SSID). The kit draws UTF-8, so the key's bytes at 0x80 and above become their
 *  two-byte sequences and the suffix is kept as it is; a line with no key (a raw legacy line, key == TR_KEYS_COUNT) is the suffix alone, which the caller passes with an
 *  empty key. Until 2026-11-04 the ring joined the two and converted the WHOLE line from Latin-1, so the suffix was encoded a second time: the AP's "Estação_SETUP" (the
 *  device's name and "_SETUP") came out as the bytes 45 73 74 61 C3 83 C2 A7 C3 83 C2 A3 6F ..., drawn "EstaÃ§Ã£o_SETUP". The line is cut at a whole character at `cap`,
 *  the terminator included, so no sequence is left half, and a key that does not fit whole leaves the suffix out. A suffix byte that is not UTF-8 is one '?' (as
 *  utf8FoldNext( ) counts it): what the kit gets is always valid UTF-8, as it was when every byte of the joined line was encoded again. */
inline void bootLineUtf8(const char* keyLatin1, const char* suffixUtf8, char* out, size_t cap) {
	if (!out || cap == 0) return;
	size_t n = 0;
	bool full = true;
	for (const unsigned char* p = (const unsigned char*)keyLatin1; p && *p; p++) {
		const size_t w = *p < 0x80 ? 1 : 2;
		if (n + w + 1 > cap) { full = false; break; }                  /* a whole character, and the terminator */
		if (w == 1) out[n++] = (char)*p;
		else { out[n++] = (char)(0xC0 | (*p >> 6)); out[n++] = (char)(0x80 | (*p & 0x3F)); }
	}
	for (const unsigned char* p = (const unsigned char*)suffixUtf8; full && p && *p; ) {
		const unsigned char c = p[0];
		const size_t want = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
		size_t have = 1;
		while (have < want && (p[have] & 0xC0) == 0x80) have++;        /* a NUL is not a continuation */
		const bool whole = want != 0 && have == want;                 /* a character, as it came */
		const size_t w = whole ? want : 1;                            /* anything else is one '?' for the bytes it spans */
		if (n + w + 1 > cap) break;
		if (whole) { for (size_t k = 0; k < want; k++) out[n++] = (char)p[k]; }
		else out[n++] = '?';
		p += whole ? want : have;
	}
	out[n] = '\0';
}
