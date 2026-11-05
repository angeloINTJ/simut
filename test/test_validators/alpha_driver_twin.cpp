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
 *          The driver also keeps a shadow of the glass for the other core (display/LcdGlass.h), and three more cases hold that to the same
 *          model: after EVERY operation, not only after a blit( ), what a reader of the shadow gets is what the model's controller holds
 *          (so that print( ), clear( ) and setCursor( ) do not reach it, and createChar( ) does at once); the shadow does not move
 *          during the bus writes of a blit( ) or a createChar( ) (it is written from `line` AFTER them: the model checks it on every byte
 *          it receives); and a frame composed and not blitted is not visible to the reader.
 *          It is a unit of its own because it needs the parallel mode of the driver and the build of
 *          test_validators does not otherwise include it; the cases that call it are in test_main.cpp.
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
uint32_t micros( );   /* the driver's readGlass( ) waits with these two */

#include "display/HD44780_16x2.h"

/* ── A model of the controller, as far as the driver speaks to it ────────────────────────────────────────────── */
namespace twin {
	static uint8_t  pins[32];
	static bool     fourBit, haveHigh, highRs, inCgram, increment;
	static uint8_t  high;
	static uint16_t addr;
	static uint8_t  ddram[0x80], cgram[0x40];
	static unsigned badNibbles;
	/* The driver's shadow of the glass, looked at from inside the bus: while the driver is writing, a reader on the other core must still
	 * get what the glass showed before (`frozen`). watchWrites counts the bytes seen, so that a watch which saw none cannot pass. */
	static const Hd44780_16x2* watched = nullptr;
	static LcdGlass            frozen;
	static unsigned            watchWrites, watchMoves;

	static void reset( ) {
		memset(pins, 0, sizeof(pins));
		fourBit = haveHigh = highRs = inCgram = false; increment = true;
		high = 0; addr = 0; badNibbles = 0;
		watched = nullptr; watchWrites = watchMoves = 0;
		memset(ddram, ' ', sizeof(ddram)); memset(cgram, 0, sizeof(cgram));
	}
	static void exec(bool rs, uint8_t b) {
		if (watched) {
			LcdGlass now;
			watchWrites++;
			if (watched->readGlass(now) == LCD_GLASS_BUSY || memcmp(now.ddram, frozen.ddram, sizeof(now.ddram)) != 0 ||
			    memcmp(now.cgram, frozen.cgram, sizeof(now.cgram)) != 0 || now.blitMs != frozen.blitMs || now.seq != frozen.seq)
				watchMoves++;
		}
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
/* A clock that only the driver's waits and the reader's looks move: one microsecond per look, the whole of each delay. */
static uint32_t usNow;
void delayMicroseconds(unsigned us) { usNow += us; }
uint32_t micros( ) { return usNow++; }
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

/* One random operation through both, the same sequence for every case here. `step` also names the instant of a blit( ). */
static const char* randomOp(Hd44780_16x2 &real, FakeHd44780 &fake, unsigned step, bool &blit, bool &glyph) {
	const char* op = "";
	blit = glyph = false;
	switch (rnd( ) % 9) {
	case 0: { uint8_t c = rnd( ) % 24, r = rnd( ) % 4; real.setCursor(c, r); fake.setCursor(c, r); op = "setCursor"; break; }
	case 1: case 2: { uint8_t b = (uint8_t)rnd( ); real.write(b); fake.write(b); op = "write"; break; }
	case 3: {                                           /* a string of any bytes but NUL, up to 40 */
		char s[48]; unsigned n = rnd( ) % 41;
		for (unsigned i = 0; i < n; i++) { unsigned v = rnd( ) % 255 + 1; s[i] = (char)(rnd( ) % 3 ? 32 + (v % 95) : v); }
		s[n] = '\0'; real.print(s); fake.print(s); op = "print"; break; }
	case 4: real.clear( ); fake.clear( ); op = "clear"; break;
	case 5: { uint8_t slot = rnd( ) % 16; uint8_t bm[8]; for (auto &x : bm) x = (uint8_t)rnd( ) & 0x1F;
		real.createChar(slot, bm); fake.createChar(slot, bm); op = "createChar"; glyph = true; break; }
	default: {
		const uint32_t t = 1000u + step * 7u;               /* the driver stamps millis( ), the fake its nowMs */
		set_native_millis(t); fake.nowMs = t;
		real.blit( ); fake.blit( ); op = "blit"; blit = true; break; }
	}
	return op;
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
		bool blit, glyph;
		const char* op = randomOp(real, fake, step, blit, glyph);
		if (blit) sameGlass(fake, step);
		sameFramebuffer(real, fake, step, op);
	}
	real.blit( ); fake.blit( ); sameGlass(fake, 4001);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, twin::badNibbles, "every nibble of the run was a legal one for the model");
}

/* ── The shadow of the glass, held to the same model ──────────────────────────────────────────────────────────── */

/* What a reader of the shadow gets, against the controller's DDRAM and CGRAM (the model) and against the fake's, which is the other twin.
 * `publishes` is how many times the writer has published (begin( ), each createChar( ), each blit( )): the count moves by two for each, and
 * by nothing for the operations that only compose. */
static void sameShadow(const Hd44780_16x2 &real, const FakeHd44780 &fake, unsigned step, const char* op, bool framed, uint32_t blitMs, unsigned publishes) {
	char msg[200];
	LcdGlass r, f;
	memset(&r, 0xEE, sizeof(r)); memset(&f, 0xEE, sizeof(f));   /* a field the reader leaves alone shows */
	const int8_t want = framed ? LCD_GLASS_OK : LCD_GLASS_NONE;
	snprintf(msg, sizeof(msg), "step %u after %s: the real driver's reader answered otherwise (1 a frame was blitted, 0 none yet)", step, op);
	TEST_ASSERT_EQUAL_INT8_MESSAGE(want, real.readGlass(r), msg);
	snprintf(msg, sizeof(msg), "step %u after %s: the fake's reader answered otherwise", step, op);
	TEST_ASSERT_EQUAL_INT8_MESSAGE(want, fake.readGlass(f), msg);
	for (int row = 0; row < 2; row++) for (int col = 0; col < 16; col++) {
		snprintf(msg, sizeof(msg), "step %u after %s: shadow row %d column %d against the controller's DDRAM", step, op, row, col);
		TEST_ASSERT_EQUAL_UINT8_MESSAGE(twin::cell(row, col), r.ddram[row][col], msg);
		snprintf(msg, sizeof(msg), "step %u after %s: the fake's shadow row %d column %d against the fake's glass", step, op, row, col);
		TEST_ASSERT_EQUAL_UINT8_MESSAGE(fake.ddram[row][col], f.ddram[row][col], msg);
	}
	for (int sl = 0; sl < 8; sl++) {
		snprintf(msg, sizeof(msg), "step %u after %s: shadow CGRAM slot %d against the controller's", step, op, sl);
		TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&twin::cgram[sl * 8], r.cgram[sl], 8, msg);
		snprintf(msg, sizeof(msg), "step %u after %s: the fake's shadow CGRAM slot %d against the fake's glass", step, op, sl);
		TEST_ASSERT_EQUAL_MEMORY_MESSAGE(fake.cgram[sl], f.cgram[sl], 8, msg);
	}
	snprintf(msg, sizeof(msg), "step %u after %s: the time of the last blit", step, op);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(blitMs, r.blitMs, msg);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(blitMs, f.blitMs, msg);
	snprintf(msg, sizeof(msg), "step %u after %s: the count moves by two for each publish and for nothing else", step, op);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u * publishes, r.seq, msg);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u * publishes, f.seq, msg);
}

void test_alpha_real_driver_shadow_is_the_glass_after_every_operation(void) {
	twin::reset( );
	Hd44780_16x2 real;
	FakeHd44780 fake;
	set_native_millis(0);
	real.begin( ); fake.begin( );
	unsigned publishes = 1;                    /* begin( ): the controller was cleared */
	bool framed = false;
	uint32_t blitMs = 0;
	sameShadow(real, fake, 0, "begin", framed, blitMs, publishes);
	rngState = 20261104u;                      /* the sequence of the case above */
	for (unsigned step = 1; step <= 4000; step++) {
		bool blit, glyph;
		const char* op = randomOp(real, fake, step, blit, glyph);
		if (blit) { framed = true; blitMs = 1000u + step * 7u; publishes++; }
		if (glyph) publishes++;
		sameShadow(real, fake, step, op, framed, blitMs, publishes);
	}
	TEST_ASSERT_TRUE_MESSAGE(framed && publishes > 600, "the run blitted and loaded glyphs often enough to mean something");
}

/* A frame on the glass, composed from two strings. */
static void composeFrame(Hd44780_16x2 &lcd, const char* top, const char* bottom) {
	lcd.clear( );
	lcd.setCursor(0, 0); lcd.print(top);
	lcd.setCursor(0, 1); lcd.print(bottom);
}

void test_alpha_real_driver_shadow_does_not_move_during_the_bus_writes(void) {
	twin::reset( );
	Hd44780_16x2 real;
	real.begin( );
	static const uint8_t first[8]  = { 1, 2, 3, 4, 5, 6, 7, 8 };
	static const uint8_t second[8] = { 31, 30, 29, 28, 27, 26, 25, 24 };
	real.createChar(3, first);
	composeFrame(real, "frame one", "row two of one");
	set_native_millis(500); real.blit( );
	LcdGlass before, after;
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, real.readGlass(before));

	/* The blit: 2 address commands and 32 cells on the bus. The reader must get frame one on every one of them. */
	composeFrame(real, "frame two", "row two of two");
	twin::frozen = before; twin::watched = &real; twin::watchWrites = twin::watchMoves = 0;
	set_native_millis(900); real.blit( );
	twin::watched = nullptr;
	TEST_ASSERT_EQUAL_UINT_MESSAGE(34, twin::watchWrites, "the watch saw every byte the blit sent (2 addresses and 32 cells)");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, twin::watchMoves, "the shadow stayed frame one for as long as the controller was being written");
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, real.readGlass(after));
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE("frame two       ", after.ddram[0], 16, "and is frame two once the blit is over");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE("row two of two  ", after.ddram[1], 16, "row two");
	TEST_ASSERT_EQUAL_UINT32(900, after.blitMs);
	TEST_ASSERT_EQUAL_UINT32(before.seq + 2, after.seq);

	/* The glyph: a command, 8 rows, a command. */
	twin::frozen = after; twin::watched = &real; twin::watchWrites = twin::watchMoves = 0;
	real.createChar(3, second);
	twin::watched = nullptr;
	TEST_ASSERT_EQUAL_UINT_MESSAGE(10, twin::watchWrites, "the watch saw every byte createChar( ) sent (the CGRAM address, 8 rows, the DDRAM address)");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, twin::watchMoves, "the shadow kept the old glyph for as long as the controller was being written");
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, real.readGlass(before));
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(second, before.cgram[3], 8, "and has the new one after");
	TEST_ASSERT_EQUAL_UINT32(after.seq + 2, before.seq);
}

void test_alpha_real_driver_shows_the_previous_frame_until_the_blit(void) {
	twin::reset( );
	Hd44780_16x2 real;
	real.begin( );
	LcdGlass g;
	/* Composed, never blitted: the glass is blank and the reader says there is no frame. */
	composeFrame(real, "composed only", "not on the glass");
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_NONE, real.readGlass(g), "a frame that was composed and not blitted is no frame");
	TEST_ASSERT_EACH_EQUAL_UINT8_MESSAGE(' ', g.ddram, 32, "and the cells are the controller's after its clear");
	set_native_millis(100); real.blit( );
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, real.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY("composed only   ", g.ddram[0], 16);
	const uint32_t seq1 = g.seq;
	/* Composed over it, not blitted: the reader still gets the first frame, and the count has not moved. */
	composeFrame(real, "second frame", "also composed");
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, real.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE("composed only   ", g.ddram[0], 16, "the previous frame, row 0");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE("not on the glass", g.ddram[1], 16, "the previous frame, row 1");
	TEST_ASSERT_EQUAL_UINT32(100, g.blitMs);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(seq1, g.seq, "nothing was published");
	/* A glyph loaded between two blits is on the glass at once, and the reader gets it with the old frame. */
	static const uint8_t bm[8] = { 0x1F, 0x11, 0x15, 0x11, 0x1F, 0, 0, 0 };
	real.createChar(7, bm);
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, real.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY(bm, g.cgram[7], 8);
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE("composed only   ", g.ddram[0], 16, "with the old frame");
	TEST_ASSERT_EQUAL_UINT32(seq1 + 2, g.seq);
	set_native_millis(250); real.blit( );
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, real.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE("second frame    ", g.ddram[0], 16, "the blit brings the new frame");
	TEST_ASSERT_EQUAL_UINT32(250, g.blitMs);
	TEST_ASSERT_EQUAL_UINT32(seq1 + 4, g.seq);
}
