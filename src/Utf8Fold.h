/**
 * @file Utf8Fold.h
 * @brief UTF-8 folded to what the classic 5x7 font can draw.
 *
 * @details The License screen, the boot lines and the CLI print with a 7-bit
 * font, and the text they print is UTF-8: language packs, the opening of the
 * License screen, the copyright holder's name. A Latin letter loses its accent
 * ("ção" -> "cao"), typographic punctuation becomes its ASCII cousin, the
 * Spanish opening marks disappear (the closing mark already says what the
 * sentence is), and anything else is one '?' per character.
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
