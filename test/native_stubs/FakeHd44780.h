/**
 * @file    test/native_stubs/FakeHd44780.h
 * @brief   A stand-in for Hd44780_16x2 (display/HD44780_16x2.h) that keeps what the glass would show.
 * @details The alpha screens are written by templates on the LCD type (display/AlphaFrame.h, BigFont_HD44780.h), so
 *          a host test passes this where the firmware passes the driver. It has the driver's framebuffer, and what
 *          the driver does with it on the bus is recorded instead of sent:
 *            - blit( ) copies the framebuffer into `ddram`, the 2x16 cells the glass shows after it (the driver
 *              writes row 0 at address 0x00 and row 1 at 0x40, one data byte per cell);
 *            - createChar( ) copies the 8 rows of a glyph into `cgram[slot & 7]` (the driver writes them at
 *              0x40 + slot * 8). Byte 0 of a glyph is the TOP row and bit 4 of a byte its LEFT column; the driver's
 *              comment on createChar( ), "LSB = top row", leaves that ambiguous.
 *          The framebuffer methods are the driver's, line for line, because the expected rows of a test are read from
 *          the original code and what that code does with a cursor past the edge, a control character or a clear is
 *          part of it. test_validators/alpha_driver_twin.cpp holds them equal to HD44780_16x2.h's: it runs the real driver on the
 *          host against a model of the controller and the same random operations through this class, and compares the glass.
 *          It keeps what the other core would read, too: the driver's shadow of the glass (display/LcdGlass.h, the same code
 *          the driver runs) is published at each blit( ) and each createChar( ), and readGlass( ) is the driver's reader with a
 *          clock of its own, so that a test of the route that serves the glass, and of the formatter behind it, runs on the
 *          host without a board. `nowMs` is the time blit( ) stamps (the driver stamps millis( ), which a test of this class
 *          does not need to move); `clock` counts the microseconds the reader waited, and `glass` is public so that a test
 *          can put the shadow in the states the other core can find it in (a count left odd: the reader says busy).
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "display/LcdGlass.h"

struct FakeHd44780 {
	bool initialized = false;

	/* The framebuffer, as in the driver. */
	char    line[2][17];
	uint8_t cursorCol = 0, cursorRow = 0;

	/* The glass: the DDRAM after the last blit( ), and the CGRAM as createChar( ) left it. */
	uint8_t  ddram[2][16];
	uint8_t  cgram[8][8];
	unsigned blits = 0;
	unsigned glyphLoads[8];   /**< createChar( ) calls per slot: a glyph is not reloaded when it did not change */

	/* What the other core reads: the shadow, the time blit( ) stamps into it, and the reader's clock. */
	LcdGlassShadow glass{};
	uint32_t nowMs = 0;
	struct Clock {
		uint32_t t = 0;        /**< microseconds: one per look at the clock, and the whole of each wait */
		uint32_t waited = 0;   /**< what the reader spent in pause( ), for the test that holds it to its limit */
		uint32_t now( ) { return t++; }
		void pause(uint32_t us) { t += us; waited += us; }
	};
	mutable Clock clock;

	FakeHd44780( ) {
		memset(line, ' ', sizeof(line));
		memset(ddram, ' ', sizeof(ddram));
		memset(cgram, 0, sizeof(cgram));
		memset(glyphLoads, 0, sizeof(glyphLoads));
	}

	/* Lifecycle */
	void begin( ) {
		if (initialized) return;
		memset(line[0], ' ', 16); line[0][16] = '\0';
		memset(line[1], ' ', 16); line[1][16] = '\0';
		glass.putBlank( );
		initialized = true;
	}

	void blit( ) {
		if (!initialized) return;
		for (uint8_t row = 0; row < 2; row++)
			for (uint8_t col = 0; col < 16; col++)
				ddram[row][col] = (uint8_t)line[row][col];
		blits++;
		glass.putFrame(line, nowMs);
	}

	/* Character output: the driver's, verbatim. */
	void setCursor(uint8_t col, uint8_t row) {
		cursorCol = (col < 16) ? col : 15;
		cursorRow = (row < 2)  ? row : 1;
	}

	void clear( ) {
		memset(line[0], ' ', 16);
		memset(line[1], ' ', 16);
		cursorCol = 0; cursorRow = 0;
	}

	size_t write(uint8_t c) {
		if (c == '\n') { cursorRow = 1; cursorCol = 0; return 1; }
		if (c < 32 && c > 7) return 1;  /* allow CGRAM chars 0-7 */
		if (cursorCol >= 16) { cursorCol = 0; cursorRow = 1; }
		if (cursorRow >= 2)  { cursorRow = 1; cursorCol = 0; }
		line[cursorRow][cursorCol++] = (char)c;
		return 1;
	}

	size_t print(const char* s) {
		if (!s) return 0;
		size_t n = 0;
		while (*s) { write((uint8_t)*s++); n++; }
		return n;
	}

	void createChar(uint8_t num, const uint8_t* bitmap) {
		if (!initialized) return;
		memcpy(cgram[num & 0x07], bitmap, 8);
		glyphLoads[num & 0x07]++;
		glass.putGlyph(num, bitmap);
	}

	/* The driver's readGlass( ): one consistent copy of the shadow, LCD_GLASS_OK, LCD_GLASS_NONE or LCD_GLASS_BUSY. */
	int8_t readGlass(LcdGlass &out) const { return glass.read(out, clock); }
};
