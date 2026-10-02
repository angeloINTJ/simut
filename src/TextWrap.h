/**
 * @file TextWrap.h
 * @brief How the License screen lays text out: words, columns and lines.
 *
 * @details The screen draws a long text with the classic 5x7 font, 50 columns
 * by 17 lines a page, word-wrapped. Since 2026-10-02 the text is UTF-8 and
 * comes in two parts — the opening in the pack's language, composed in RAM,
 * and the licence text with the third-party list, on flash — so the layout:
 *  - counts a column per character, not per byte, so "ção" is three columns;
 *  - carries its position from one part to the next, so they read as one text;
 *  - indents a line that a newline began by its leading spaces (the holders
 *    in the third-party list), while a line that a wrap began skips them, as
 *    it always did;
 *  - splits a word wider than the screen at the width, never inside a
 *    character.
 * For ASCII without leading spaces it is the layout the screen always had,
 * and a native case compares the two word for word.
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

struct TextWrap {
	int line = 0;            /* 0-based line of the next word */
	int col = 0;
	bool bol = true;         /* at the start of a line a newline began */

	int lines( ) const { return line + 1; }

	/** Calls emit(line, col, word, bytes) for each word of text, a word being
	 *  at most maxCols characters with no space or newline in it. Stops and
	 *  returns false as soon as emit returns false. */
	template <class Emit>
	bool walk(const char* text, int maxCols, Emit&& emit) {
		const unsigned char* p = (const unsigned char*)text;
		while (p && *p) {
			if (*p == '\n') { line++; col = 0; bol = true; p++; continue; }
			if (*p == ' ') {
				if (bol) { if (col < maxCols - 1) col++; }
				else if (col > 0 && col < maxCols) col++;
				p++;
				continue;
			}
			const unsigned char* w = p;
			int wcols = 0;
			while (*p && *p != ' ' && *p != '\n') {
				if ((*p & 0xC0) != 0x80) {          /* a character starts here */
					if (wcols == maxCols) break;
					wcols++;
				}
				p++;
			}
			if (col > 0 && col + wcols > maxCols) { line++; col = 0; }
			if (!emit(line, col, (const char*)w, (size_t)(p - w))) return false;
			col += wcols;
			bol = false;
		}
		return true;
	}
};
