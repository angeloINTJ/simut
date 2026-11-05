/**
 * @file    test/native_stubs/AlphaFrameCheck.h
 * @brief   What the host tests of the alpha LCD share: a pass of the loop on the fake LCD, and the checks of its glass.
 * @details A pass is what DisplayManager::loopCore1( ) does between two sleeps (display/AlphaFrame.h): the link's
 *          glyph into CGRAM slot 7, one screen into the framebuffer, the blit. AlphaPass runs that on a FakeHd44780,
 *          starting where the loop starts (begin( ) and the CGRAM load), and the checks read the glass after the blit:
 *          a row is exactly sixteen cells, and a cell that holds a CGRAM glyph is written as one of the GL_ pieces
 *          below, so an expected row reads like the screen it describes.
 *          Used by test_validators (the radio's screens) and test_lan (the cable's differences).
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "display/AlphaFrame.h"
#include "FakeHd44780.h"

/* The CGRAM glyphs as a screen writes them: slots 0-6 are the big-digit pieces, slot 7 the link's glyph. The numbers
 * are the contract between BigFont_HD44780::begin( ) and every screen, so they are written here and not read from
 * BigFontSlot: a slot that moved must break a test, not follow itself. One literal each, never inside another, because
 * an octal escape followed by an octal digit would swallow it. */
#define GL_LT   "\000"
#define GL_UB   "\001"
#define GL_RT   "\002"
#define GL_LL   "\003"
#define GL_LB   "\004"
#define GL_LR   "\005"
#define GL_UMB  "\006"
#define GL_LINK "\007"

/* The banner of the boot screens, as the loop builds it from SIMUT_VERSION (the figures fix a version of their own). */
#define ALPHA_TEST_BANNER "SIMUT 2.13.0"

struct AlphaPass {
	FakeHd44780     lcd;
	BigFont_HD44780 big;

	/* The loop's start: begin( ), then the CGRAM load. */
	AlphaPass( ) { lcd.begin( ); big.begin(lcd); }

	/* The previous frame: a framebuffer holds what the last pass wrote, and a screen that does not clear leaves
	 * what it does not write. Every cell holds `c`, so a cell that survives a screen shows as a `c` on the glass. */
	void previousFrame(char c = 'x') {
		memset(lcd.line, c, sizeof(lcd.line));
		lcd.line[0][16] = lcd.line[1][16] = '\0';
		lcd.cursorCol = lcd.cursorRow = 0;
	}

	/* One pass of the loop. */
	void pass(int32_t rssi, const AlphaFrameIn &in) {
		alphaLoadLinkGlyph(lcd, big, rssi);
		alphaDrawFrame(lcd, big, in);
		lcd.blit( );
	}

	/* A screen drawn outside a pass, as the splash is (before the loop's first one): no link glyph. */
	void draw(const AlphaFrameIn &in) {
		alphaDrawFrame(lcd, big, in);
		lcd.blit( );
	}
};

/* The cells as text, for a failure message: printable ASCII as it is, anything else as <n> (the CGRAM glyphs 0-7). */
inline void alphaShowCells(const uint8_t* c, char* out, size_t cap) {
	size_t n = 0;
	out[0] = '\0';
	for (int i = 0; i < 16 && n + 6 < cap; i++) {
		if (c[i] >= 0x20 && c[i] < 0x7F) out[n++] = (char)c[i];
		else n += (size_t)snprintf(out + n, cap - n, "<%u>", (unsigned)c[i]);
	}
	out[n] = '\0';
}

inline void alphaExpectRow(const FakeHd44780 &lcd, int row, const char* want16, const char* what) {
	if (memcmp(lcd.ddram[row], want16, 16) == 0) return;
	char w[160], g[160], msg[480];
	alphaShowCells((const uint8_t*)want16, w, sizeof(w));
	alphaShowCells(lcd.ddram[row], g, sizeof(g));
	snprintf(msg, sizeof(msg), "%s: row %d of the glass expected |%s| and has |%s|", what, row, w, g);
	TEST_FAIL_MESSAGE(msg);
}

/* Both rows of the glass after the last blit( ). A row that is not exactly sixteen cells does not compile. */
template <size_t N0, size_t N1>
inline void alphaExpectGlass(const FakeHd44780 &lcd, const char (&row0)[N0], const char (&row1)[N1], const char* what) {
	static_assert(N0 == 17 && N1 == 17, "a row of the LCD is exactly sixteen cells");
	alphaExpectRow(lcd, 0, row0, what);
	alphaExpectRow(lcd, 1, row1, what);
}

/* A CGRAM slot holds this glyph. */
inline void alphaExpectGlyph(const FakeHd44780 &lcd, uint8_t slot, const uint8_t* want, const char* what) {
	if (memcmp(lcd.cgram[slot], want, 8) == 0) return;
	char msg[240];
	snprintf(msg, sizeof(msg), "%s: CGRAM slot %u holds %02x %02x %02x %02x %02x %02x %02x %02x, not the glyph expected",
	         what, (unsigned)slot, lcd.cgram[slot][0], lcd.cgram[slot][1], lcd.cgram[slot][2], lcd.cgram[slot][3],
	         lcd.cgram[slot][4], lcd.cgram[slot][5], lcd.cgram[slot][6], lcd.cgram[slot][7]);
	TEST_FAIL_MESSAGE(msg);
}
