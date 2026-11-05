/**
 * @file display/LcdGlass.h
 * @brief What the alpha's 16x2 glass shows, kept so that the other core can read it without tearing.
 *
 * @details Core 1 draws the LCD and is the only one that knows what is on it: the two rows of codes (the DDRAM, as of
 * the last blit( )) and the eight glyphs the codes 0-7 draw (the CGRAM, as createChar( ) left it). The controller
 * answers no read (the driver never reads the busy flag and RW is tied low), so the driver keeps a shadow of what it
 * wrote, and Core 0 (the handler of GET /api/lcd, WebManager::handleApiLcd( )) reads the shadow. Core 0 must get ONE state of the glass: the
 * frame of one blit with the glyphs that were loaded at that moment, never half of this blit and half of the last.
 *
 * The shadow is a seqlock, the pattern of the Ângulo's frame record (ui/angulo/DisplayManager_UiA.cpp, s_seq and
 * uaEstado( )): the writer counts to an odd number, writes, counts to the next even one; the reader copies and
 * trusts the copy only if it saw the same even number before and after. The writer takes no lock, no heap and no log
 * (docs/CONCURRENCY.md, invariant 2): a mutex that Core 1 holds when Core 0 starts a flash write would freeze Core 1
 * with the lock held (invariant 3), and the reader is on the side that can afford to wait.
 *
 * Three things the Ângulo's record does not need and this one does:
 *   - A publish that dies in the middle. Core 0 relaunches Core 1 when its heartbeat is 10 s old (AppManager_Loop.cpp,
 *     "Core 1 dead >10s"), and the relaunch keeps the driver (begin( ) returns at once when `initialized`), so a count
 *     left odd would stay odd. A writer that adds one blindly (the Ângulo's frame record does, in uaPintar( )) then turns
 *     the parity around for good: the count is odd between publishes, when every read says "busy", and even in the
 *     middle of one, when a read is trusted that should not be. open( ) makes the next count odd whatever it found, in
 *     ONE store (an even count written first would let a reader trust data a dead writer left half-copied), and drops
 *     `blitted`: the dead publish may have been a frame, whose cells are then half of two frames, and the reader says
 *     "no frame" until the next blit( ) rewrites all thirty-two of them.
 *   - One snapshot for the cells and the glyphs: a glyph is replaced on its own (the link's, slot 7, at the top of
 *     every pass of the loop) and a frame is blitted on its own, and the glass shows each as soon as it is written, so
 *     the shadow publishes each as soon as it is written, with the same counter. A reader gets a frame with the
 *     glyphs the glass had with it.
 *   - Nothing before the blit. The framebuffer (`line`) is not the glass: print( ) and clear( ) compose in it and the
 *     blit( ) pushes it. The shadow is written from `line` AFTER the bus writes of the blit, so it never shows a frame
 *     the controller does not have yet (the twin test holds the shadow still during every bus write).
 *
 * Memory order. The counter is volatile, the data are plain, and the barrier between them is the fence the SDK's
 * __dmb( ) is: the writer fences after the odd count and before the even one, the reader after its first look at the
 * count and before its second. __atomic_thread_fence( ) is used here, and not the SDK's, because this header is also
 * compiled on the host by the fake LCD (test/native_stubs/FakeHd44780.h), which holds it to the same tests. It is
 * `dmb ish` on the Cortex-M0+ (read from the disassembly of the pico_wire image, 2026-11-04) and, with the same GCC
 * 14.3, on the M33 of the RP2350 (compiled on a copy of the writer: the alpha images are RP2040 builds). A host test cannot see a fence (it
 * is a no-op there: all the native cases pass with any one of the four taken out), so tools/check_glass_seqlock.py holds where the four stand,
 * in the text of this file; its --selftest names the rule that a change to the order breaks.
 *
 * The probes. Every function that stores or copies takes an optional probe, and calls storeDone( ) after each store
 * the other core could observe and copyStep( ) after each byte the reader copies. The default does nothing and
 * compiles away (the image is the same with and without it); the host test passes one that runs the other side at
 * that very point, which is how it stops the writer after each of its stores and lands a whole publish in the
 * middle of the reader's copy. A seqlock cannot be tested by reading while another thread writes: that finds a tear
 * only when it happens to occur.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once
#include <stdint.h>

/** One reading of the glass: a consistent copy, made by LcdGlassShadow::read( ). */
struct LcdGlass {
	uint8_t  ddram[2][16];   /**< the codes of the two rows, as of the last blit( ): a code 0-7 is the glyph in that CGRAM slot, the rest is the character ROM's */
	uint8_t  cgram[8][8];    /**< the eight glyphs as createChar( ) left them: byte 0 is the TOP row, bit 4 the LEFT column (test/native_stubs/FakeHd44780.h) */
	uint32_t seq;            /**< the counter's value the copy was made under (even): it moves with every publish, a blit or a glyph, so a reader can tell that something new was drawn */
	uint32_t blitMs;         /**< millis( ) at the end of the last blit( ); 0 until the first one */
};

/** What read( ) and DisplayManager::lcdGlass( ) return: the three numbers DisplayManager::uaEstado( ) returns, with the same meaning. */
enum LcdGlassRead : int8_t {
	LCD_GLASS_BUSY = -1,   /**< Core 1 kept the shadow under update for the whole wait (2 ms): `out` is not a reading; the caller answers "busy, try again" and not "none" */
	LCD_GLASS_NONE = 0,    /**< `out` is a consistent copy, but no frame has been blitted yet (or the writer died in a publish and has not blitted since): the glass shows nothing the caller should draw */
	LCD_GLASS_OK   = 1,    /**< `out` is a consistent copy of the glass */
};

/** The probe a call takes when it is not being tested: both hooks empty. */
struct LcdGlassNoProbe {
	void storeDone( ) {}   /**< the writer has just made a store that a reader on the other core could observe */
	void copyStep( ) {}    /**< the reader has just copied one byte (or one field) of the shadow */
};

struct LcdGlassShadow {
	/** The wait of read( ): it looks again every kReadStepUs and gives up after kReadLimitUs. The Ângulo's reader waits 2 ms with a 20 us step (uaEstado( )): a publish is tens of microseconds, so 2 ms outlasts it unless Core 1 is held, and Core 0's loop does not stand a longer one. */
	static constexpr uint32_t kReadLimitUs = 2000;
	static constexpr uint32_t kReadStepUs  = 20;

	/* No initializers, on purpose: the state at the start is all zero, which a static gets from the loader. The driver's `_lcd` is a static,
	 * and `DisplayManager::_driver`, an alpha driver nothing reads, is a member of a class that is built once: an initializer here made
	 * both construct the shadow, +160 B in DisplayManager::DisplayManager( ) and +48 B in the static initializer of the unit (the pico_wire
	 * image, 2026-11-04). An instance that is not static is value-initialized by whoever makes it: `LcdGlassShadow s{}`. The one exception
	 * is `DisplayManager::_driver`: the DisplayManager is made on the heap (`make_unique` in AppManager) by a user-provided constructor, so
	 * its copy is neither zeroed nor used. Nothing publishes into it or reads it in the alpha, and it costs 108 B of heap (the size
	 * AppManager passes to operator new went from 2,600 to 2,708 B in the pico_wire image; the review of 2026-11-04). */
	volatile uint32_t seq;   /**< even: the shadow is whole; odd: Core 1 is writing it (or died writing it) */
	uint8_t  ddram[2][16];
	uint8_t  cgram[8][8];
	uint32_t blitMs;         /**< millis( ) of the last blit( ) */
	bool     blitted;        /**< a frame has been blitted since the start, or since the last publish that died (open( ) clears it) */

	/* ── Core 1: the writer ─────────────────────────────────────────── */

	/** The controller was just cleared (begin( ) ends with the display's clear): every cell is a space, no frame yet. */
	template <typename P = LcdGlassNoProbe>
	void putBlank(P &&probe = P( )) {
		open(probe);
		for (uint8_t r = 0; r < 2; r++)
			for (uint8_t c = 0; c < 16; c++) { ddram[r][c] = ' '; probe.storeDone( ); }
		blitted = false; probe.storeDone( );
		close(probe);
	}

	/** A blit( ) has just ended: the framebuffer is now on the glass. */
	template <typename P = LcdGlassNoProbe>
	void putFrame(const char (&line)[2][17], uint32_t nowMs, P &&probe = P( )) {
		open(probe);
		for (uint8_t r = 0; r < 2; r++)
			for (uint8_t c = 0; c < 16; c++) { ddram[r][c] = (uint8_t)line[r][c]; probe.storeDone( ); }
		blitMs = nowMs; probe.storeDone( );
		blitted = true; probe.storeDone( );
		close(probe);
	}

	/** A createChar( ) has just ended: the glyph is in the controller's CGRAM. `slot` is masked as the driver masks it on the bus. */
	template <typename P = LcdGlassNoProbe>
	void putGlyph(uint8_t slot, const uint8_t* bitmap, P &&probe = P( )) {
		open(probe);
		uint8_t *g = cgram[slot & 0x07];
		for (uint8_t i = 0; i < 8; i++) { g[i] = bitmap[i]; probe.storeDone( ); }
		close(probe);
	}

	/* ── Core 0: the reader ─────────────────────────────────────────── */

	/** One consistent copy of the shadow, or LCD_GLASS_BUSY if Core 1 kept it under update for kReadLimitUs.
	 *  `Clock` has now( ) (microseconds, free-running: the difference is taken modulo 2^32) and pause(us); the
	 *  firmware's is micros( ) and delayMicroseconds( ), the host's counts. The reader waits only for an odd counter: a
	 *  copy that was overtaken by a publish is tried again at once, as uaEstado( ) does, and the time it takes counts. */
	template <typename Clock, typename P = LcdGlassNoProbe>
	int8_t read(LcdGlass &out, Clock &clock, P &&probe = P( )) const {
		const uint32_t t0 = clock.now( );
		do {
			const uint32_t before = seq;
			if (before & 1u) { clock.pause(kReadStepUs); continue; }
			fence( );
			for (uint8_t r = 0; r < 2; r++)
				for (uint8_t c = 0; c < 16; c++) { out.ddram[r][c] = ddram[r][c]; probe.copyStep( ); }
			for (uint8_t s = 0; s < 8; s++)
				for (uint8_t i = 0; i < 8; i++) { out.cgram[s][i] = cgram[s][i]; probe.copyStep( ); }
			out.blitMs = blitMs; probe.copyStep( );
			const bool framed = blitted; probe.copyStep( );
			fence( );
			if (seq == before) {
				out.seq = before;
				return framed ? LCD_GLASS_OK : LCD_GLASS_NONE;
			}
		} while (clock.now( ) - t0 < kReadLimitUs);
		return LCD_GLASS_BUSY;
	}

private:
	/** A full barrier: `dmb ish` on the RP2040 and the RP2350, a fence on the host. Inlined: out of line, at -Os, GCC made it a 6 B function that every publish called (the pico_wire image, 2026-11-04). */
	static inline __attribute__((always_inline)) void fence( ) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

	/** The count goes odd and stays odd until close( ). From an odd count (the previous writer died inside a publish) it goes
	 *  to the odd number after the next even one, in the same store; the shadow it left may be half of two frames. */
	template <typename P>
	void open(P &probe) {
		const uint32_t s = seq;
		seq = (s + 1u) | 1u; probe.storeDone( );
		fence( );
		if (s & 1u) { blitted = false; probe.storeDone( ); }
	}

	template <typename P>
	void close(P &probe) {
		fence( );
		seq = seq + 1u; probe.storeDone( );
	}
};
