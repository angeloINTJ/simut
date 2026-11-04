/**
 * @file    test/test_validators/alpha_driver_twin.cpp
 * @brief   The fake LCD against the real driver: the same operations, the same glass.
 * @details FakeHd44780 (test/native_stubs) stands in for Hd44780_16x2 in every alpha-frame case, and its framebuffer
 *          methods are copies of the driver's. A copy can drift, and then every expected row would be read from a
 *          display that does not exist. This unit compiles the REAL driver (display/HD44780_16x2.h, parallel mode) on
 *          the host, with pinMode( ), digitalWrite( ) and delayMicroseconds( ) answered by a model of the controller
 *          that decodes the 4-bit protocol the driver speaks (the initialisation, the DDRAM and CGRAM address
 *          commands, the data writes, the entry mode), and runs the same random sequence of operations through the
 *          driver and through the fake: setCursor( ) past both edges, write( ) of every byte value including the
 *          control characters and '\n', print( ), clear( ), createChar( ) with a slot number past 7, blit( ). After
 *          each operation the framebuffer and the cursor are equal; after each blit( ) the cells the model's DDRAM
 *          holds at the two rows are equal to the fake's glass, and the model's CGRAM to the fake's.
 *          It is a unit of its own because it needs the parallel mode of the driver and the build of
 *          test_validators does not otherwise include it; the case that calls it is in test_main.cpp.
 *
 * @project SIMUT
 * @license MIT License
 */

#define HD44780_MODE_PARALLEL 1   /* before anything that brings simut_config.h: the driver's parallel mode and its pins */
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "FakeHd44780.h"

#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#define pgm_read_byte(p) (*(const uint8_t*)(p))

/* What the driver calls, declared before it is read; defined below, once its pin macros exist. */
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
void delayMicroseconds(unsigned us);

#include "display/HD44780_16x2.h"

/* ── A model of the controller, as far as the driver speaks to it ────────────────────────────────────────────── */
namespace twin {
	static uint8_t  pins[32];
	static bool     fourBit, haveHigh, highRs, inCgram, increment;
	static uint8_t  high;
	static uint16_t addr;
	static uint8_t  ddram[0x80], cgram[0x40];
	static unsigned badNibbles;

	static void reset( ) {
		memset(pins, 0, sizeof(pins));
		fourBit = haveHigh = highRs = inCgram = false; increment = true;
		high = 0; addr = 0; badNibbles = 0;
		memset(ddram, ' ', sizeof(ddram)); memset(cgram, 0, sizeof(cgram));
	}
	static void exec(bool rs, uint8_t b) {
		if (!rs) {
			if      (b & 0x80) { addr = b & 0x7F; inCgram = false; }          /* set DDRAM address */
			else if (b & 0x40) { addr = b & 0x3F; inCgram = true;  }          /* set CGRAM address */
			else if (b == 0x01) { memset(ddram, ' ', sizeof(ddram)); addr = 0; inCgram = false; }   /* clear */
			else if ((b & 0xFC) == 0x04) increment = (b & 0x02) != 0;         /* entry mode: I/D */
			/* function set, display on/off, cursor shift: nothing the glass keeps */
		} else if (inCgram) {
			cgram[addr & 0x3F] = b; addr = (addr + (increment ? 1 : -1)) & 0x3F;
		} else {
			ddram[addr & 0x7F] = b; addr = (addr + (increment ? 1 : -1)) & 0x7F;
		}
	}
	static void nibble(bool rs, uint8_t n) {
		if (!fourBit) {                                  /* still in the 8-bit power-up: 0x3 three times, then 0x2 switches */
			if (!rs && n == 0x2) { fourBit = true; haveHigh = false; }
			else if (!(!rs && n == 0x3)) badNibbles++;
			return;
		}
		if (!haveHigh) { high = n; highRs = rs; haveHigh = true; return; }
		if (rs != highRs) badNibbles++;                  /* both halves of a byte carry the same RS */
		haveHigh = false;
		exec(rs, (uint8_t)((high << 4) | n));
	}
	static uint8_t cell(int row, int col) { return ddram[(row ? 0x40 : 0x00) + col]; }
}
void pinMode(uint8_t, uint8_t) {}
void delayMicroseconds(unsigned) {}
void digitalWrite(uint8_t pin, uint8_t value) {
	const bool enWas = twin::pins[HD44780_EN] != 0;
	twin::pins[pin] = value ? 1 : 0;
	if (pin == HD44780_EN && enWas && !value) {          /* the controller latches on the falling edge of E */
		const uint8_t n = (uint8_t)((twin::pins[HD44780_D7] << 3) | (twin::pins[HD44780_D6] << 2) | (twin::pins[HD44780_D5] << 1) | twin::pins[HD44780_D4]);
		twin::nibble(twin::pins[HD44780_RS] != 0, n);
	}
}

/* ── The same operations through both ─────────────────────────────────────────────────────────────────────────── */
static unsigned rngState;
static unsigned rnd( ) { rngState = rngState * 1664525u + 1013904223u; return rngState >> 8; }

static void sameFramebuffer(const Hd44780_16x2 &real, const FakeHd44780 &fake, unsigned step, const char* op) {
	char msg[160];
	snprintf(msg, sizeof(msg), "step %u after %s: framebuffer or cursor differs", step, op);
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(real.line[0], fake.line[0], 16, msg);
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(real.line[1], fake.line[1], 16, msg);
	TEST_ASSERT_EQUAL_UINT8_MESSAGE(real.cursorCol, fake.cursorCol, msg);
	TEST_ASSERT_EQUAL_UINT8_MESSAGE(real.cursorRow, fake.cursorRow, msg);
}
static void sameGlass(const FakeHd44780 &fake, unsigned step) {
	char msg[160];
	for (int r = 0; r < 2; r++) for (int c = 0; c < 16; c++) {
		snprintf(msg, sizeof(msg), "step %u: glass row %d column %d (the controller's DDRAM against the fake's)", step, r, c);
		TEST_ASSERT_EQUAL_UINT8_MESSAGE(twin::cell(r, c), fake.ddram[r][c], msg);
	}
	for (int s = 0; s < 8; s++) {
		snprintf(msg, sizeof(msg), "step %u: CGRAM slot %d (the controller's against the fake's)", step, s);
		TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&twin::cgram[s * 8], fake.cgram[s], 8, msg);
	}
}

void test_alpha_fake_lcd_leaves_the_glass_the_real_driver_leaves(void) {
	twin::reset( );
	Hd44780_16x2 real;
	FakeHd44780 fake;
	real.begin( ); fake.begin( );
	TEST_ASSERT_TRUE_MESSAGE(twin::fourBit, "the driver's initialisation reached 4-bit mode on the model");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, twin::badNibbles, "every nibble the driver sent was a legal one for the model");
	rngState = 20261104u;
	for (unsigned step = 1; step <= 4000; step++) {
		const char* op = "";
		switch (rnd( ) % 9) {
		case 0: { uint8_t c = rnd( ) % 24, r = rnd( ) % 4; real.setCursor(c, r); fake.setCursor(c, r); op = "setCursor"; break; }
		case 1: case 2: { uint8_t b = (uint8_t)rnd( ); real.write(b); fake.write(b); op = "write"; break; }
		case 3: {                                           /* a string of any bytes but NUL, up to 40 */
			char s[48]; unsigned n = rnd( ) % 41;
			for (unsigned i = 0; i < n; i++) { unsigned v = rnd( ) % 255 + 1; s[i] = (char)(rnd( ) % 3 ? 32 + (v % 95) : v); }
			s[n] = '\0'; real.print(s); fake.print(s); op = "print"; break; }
		case 4: real.clear( ); fake.clear( ); op = "clear"; break;
		case 5: { uint8_t slot = rnd( ) % 16; uint8_t bm[8]; for (auto &x : bm) x = (uint8_t)rnd( ) & 0x1F;
			real.createChar(slot, bm); fake.createChar(slot, bm); op = "createChar"; break; }
		default: real.blit( ); fake.blit( ); sameGlass(fake, step); op = "blit"; break;
		}
		sameFramebuffer(real, fake, step, op);
	}
	real.blit( ); fake.blit( ); sameGlass(fake, 4001);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, twin::badNibbles, "every nibble of the run was a legal one for the model");
}
