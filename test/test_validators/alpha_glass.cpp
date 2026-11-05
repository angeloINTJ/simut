/**
 * @file    test/test_validators/alpha_glass.cpp
 * @brief   The shadow of the alpha's glass (display/LcdGlass.h): a reader on the other core gets ONE state of it, or "busy".
 * @details Core 1 writes the shadow and Core 0 reads it, and the one thing that can go wrong between them is a reader that gets half of
 *          one publish and half of the previous: a frame whose first row is this blit's and whose second is the last one's, or a
 *          frame with a glyph the glass did not have while it showed it. Reading while another thread writes finds that only when the
 *          timing happens to produce it, so these cases do not use a thread. The writer and the reader take a probe (LcdGlassNoProbe
 *          by default, compiled away) and call it after each store and after each byte copied, and the case runs the OTHER side at
 *          that very point: the reader after every store of a publish (the writer "stopped" there), and a whole publish in the middle
 *          of the reader's copy, at every byte of it. Frames and glyphs here differ in every byte from the ones they replace, so a
 *          copy that is part of each cannot be mistaken for either.
 *          The instrument is held first: the same harnesses run on a writer without the odd count and on two readers that skip one
 *          of their two looks at the count, and each must report a mix. A harness that finds none in the real code means something
 *          only because it finds one in these.
 *          Also here: an odd count at the start of a publish (a writer that died inside one, which the relaunch of Core 1 does not
 *          reset) heals in one publish, however the writer died; the reader gives up within its limit, with the count stuck odd and
 *          with a publish landing in every attempt; and the fake LCD, which runs the same code, publishes at the blit and not at the
 *          composition. The real driver against the model of the controller is test_validators/alpha_driver_twin.cpp.
 *
 * @project SIMUT
 * @license MIT License
 */
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FakeHd44780.h"

namespace {

constexpr uint32_t LIMIT = LcdGlassShadow::kReadLimitUs;
constexpr uint32_t STEP  = LcdGlassShadow::kReadStepUs;
constexpr unsigned COPY_STEPS = 32 + 64 + 1 + 1;   /* what one attempt of the reader copies: 32 cells, 64 glyph rows, the time, the flag */

/* The reader's clock: a microsecond for each look, and the whole of each wait. */
struct Clock {
	uint32_t t = 0, waited = 0;
	uint32_t now( ) { return t++; }
	void pause(uint32_t us) { t += us; waited += us; }
};

/* Frames and glyphs that differ in every byte when their seeds differ by something other than a multiple of 256. */
void fillLine(char (&line)[2][17], uint8_t seed) {
	for (int r = 0; r < 2; r++) {
		for (int c = 0; c < 16; c++) line[r][c] = (char)(uint8_t)(seed + r * 16 + c);
		line[r][16] = '\0';
	}
}
void fillGlyph(uint8_t (&g)[8], uint8_t seed) { for (int i = 0; i < 8; i++) g[i] = (uint8_t)(seed + i); }

/* What a reader must get is the cells, the glyphs and the time. The count differs from one state to the next by definition. */
bool sameGlass(const LcdGlass &a, const LcdGlass &b) {
	return memcmp(a.ddram, b.ddram, sizeof(a.ddram)) == 0 && memcmp(a.cgram, b.cgram, sizeof(a.cgram)) == 0 && a.blitMs == b.blitMs;
}

/* A start-up: cleared, the eight glyphs of the big digits loaded, the first frame blitted at 100 ms. 10 publishes, so the count is 20. */
void makeOld(LcdGlassShadow &s) {
	s.putBlank( );
	for (uint8_t slot = 0; slot < 8; slot++) { uint8_t g[8]; fillGlyph(g, (uint8_t)(0x10 + slot * 8)); s.putGlyph(slot, g); }
	char a[2][17]; fillLine(a, 0x30); s.putFrame(a, 100);
}

/* The glass a reader gets from a whole shadow (a failed assertion if it is not readable). */
LcdGlass readWhole(const LcdGlassShadow &s, int8_t want, const char* what) {
	LcdGlass g; memset(&g, 0xEE, sizeof(g));
	Clock c;
	char msg[120]; snprintf(msg, sizeof(msg), "%s: a whole shadow reads (1 a frame, 0 none)", what);
	TEST_ASSERT_EQUAL_INT8_MESSAGE(want, s.read(g, c), msg);
	return g;
}

/* ── The publishes under test: what each does, and the glass it must leave ────────────────────────────────────── */

struct PubFrame {   /* a blit */
	char line[2][17];
	PubFrame( ) { fillLine(line, 0x90); }
	const char* name( ) const { return "a frame"; }
	template <typename P> void operator( )(LcdGlassShadow &w, P &&p) const { w.putFrame(line, 200, p); }
	void expect(LcdGlass &g, int8_t &code) const { for (int r = 0; r < 2; r++) for (int c = 0; c < 16; c++) g.ddram[r][c] = (uint8_t)line[r][c]; g.blitMs = 200; code = LCD_GLASS_OK; }
};
struct PubGlyph {   /* a createChar( ) of slot 5 */
	uint8_t bm[8];
	PubGlyph( ) { fillGlyph(bm, 0xE0); }
	const char* name( ) const { return "a glyph"; }
	template <typename P> void operator( )(LcdGlassShadow &w, P &&p) const { w.putGlyph(5, bm, p); }
	void expect(LcdGlass &g, int8_t &code) const { memcpy(g.cgram[5], bm, 8); code = LCD_GLASS_OK; }
};
struct PubBlank {   /* the controller's clear, at the start */
	const char* name( ) const { return "the clear"; }
	template <typename P> void operator( )(LcdGlassShadow &w, P &&p) const { w.putBlank(p); }
	void expect(LcdGlass &g, int8_t &code) const { memset(g.ddram, ' ', sizeof(g.ddram)); g.blitMs = 100; code = LCD_GLASS_NONE; }   /* the time is left as it was; no frame */
};
/* The instrument's control: a writer that copies and then moves the count once, even to even, with no odd count in between. */
struct PubFrameWithoutOdd : PubFrame {
	template <typename P> void operator( )(LcdGlassShadow &w, P &&p) const {
		for (int r = 0; r < 2; r++) for (int c = 0; c < 16; c++) { w.ddram[r][c] = (uint8_t)line[r][c]; p.storeDone( ); }
		w.blitMs = 200; p.storeDone( );
		w.blitted = true; p.storeDone( );
		w.seq = w.seq + 2; p.storeDone( );
	}
};

/* ── The readers: the real one and the two controls ───────────────────────────────────────────────────────────── */

struct RealReader {
	template <typename P> int8_t operator( )(const LcdGlassShadow &s, LcdGlass &o, Clock &c, P &p) const { return s.read(o, c, p); }
	int8_t operator( )(const LcdGlassShadow &s, LcdGlass &o, Clock &c) const { return s.read(o, c); }
};
template <typename P> void copyAll(const LcdGlassShadow &s, LcdGlass &o, bool &framed, P &p) {
	for (int r = 0; r < 2; r++) for (int c = 0; c < 16; c++) { o.ddram[r][c] = s.ddram[r][c]; p.copyStep( ); }
	for (int sl = 0; sl < 8; sl++) for (int i = 0; i < 8; i++) { o.cgram[sl][i] = s.cgram[sl][i]; p.copyStep( ); }
	o.blitMs = s.blitMs; p.copyStep( );
	framed = s.blitted; p.copyStep( );
}
struct NoCopyProbe { void storeDone( ) {} void copyStep( ) {} };
/* Control: a reader that copies and trusts the copy without looking at the count again. */
struct ReaderWithoutSecondLook {
	template <typename P> int8_t operator( )(const LcdGlassShadow &s, LcdGlass &o, Clock &, P &p) const {
		if (s.seq & 1u) return LCD_GLASS_BUSY;
		bool framed; copyAll(s, o, framed, p);
		return framed ? LCD_GLASS_OK : LCD_GLASS_NONE;
	}
	int8_t operator( )(const LcdGlassShadow &s, LcdGlass &o, Clock &c) const { NoCopyProbe p; return (*this)(s, o, c, p); }
};
/* Control: a reader that does look twice but does not mind an odd count (it takes a count that did not move for a count that is whole). */
struct ReaderThatIgnoresOdd {
	template <typename P> int8_t operator( )(const LcdGlassShadow &s, LcdGlass &o, Clock &, P &p) const {
		const uint32_t before = s.seq;
		bool framed; copyAll(s, o, framed, p);
		if (s.seq != before) return LCD_GLASS_BUSY;
		return framed ? LCD_GLASS_OK : LCD_GLASS_NONE;
	}
	int8_t operator( )(const LcdGlassShadow &s, LcdGlass &o, Clock &c) const { NoCopyProbe p; return (*this)(s, o, c, p); }
};

/* ── The two harnesses ────────────────────────────────────────────────────────────────────────────────────────── */

struct Report {
	unsigned stops = 0, busy = 0, old = 0, fresh = 0, mixes = 0;
	bool lastWasNew = false;
	unsigned firstMix = 0;
};

struct CountStores { unsigned n = 0; void storeDone( ) { n++; } void copyStep( ) {} };

/* The writer stopped after its k-th store, for every k: a reader gets busy, the old glass or the new one. */
template <typename Pub, typename Reader>
Report stopHarness(const Pub &pub, const LcdGlassShadow &old, const LcdGlass &oldGlass, int8_t oldCode, Reader rd) {
	LcdGlass newGlass = oldGlass; int8_t newCode = oldCode;
	pub.expect(newGlass, newCode);
	CountStores cnt;
	{ LcdGlassShadow w = old; pub(w, cnt); }
	Report rep; rep.stops = cnt.n;
	struct Probe {
		const LcdGlassShadow* s; Reader rd; unsigned stopAt, seen = 0; int8_t code = 99; LcdGlass out = {};
		void storeDone( ) { if (++seen == stopAt) { memset(&out, 0xEE, sizeof(out)); Clock c; code = rd(*s, out, c); } }
		void copyStep( ) {}
	};
	for (unsigned k = 1; k <= cnt.n; k++) {
		LcdGlassShadow w = old;
		Probe p{ &w, rd, k };
		pub(w, p);
		if (p.code == LCD_GLASS_BUSY) rep.busy++;
		else if (p.code == oldCode && sameGlass(p.out, oldGlass)) rep.old++;
		else if (p.code == newCode && sameGlass(p.out, newGlass)) rep.fresh++;
		else { if (!rep.mixes) rep.firstMix = k; rep.mixes++; }
		if (k == cnt.n) rep.lastWasNew = (p.code == newCode && sameGlass(p.out, newGlass));
	}
	return rep;
}

/* A whole publish landing inside the reader's copy, at every byte of it: the reader must come back with the new glass. */
template <typename Pub>
struct LandInCopy {
	LcdGlassShadow* s; const Pub* pub; unsigned landAt, seen = 0; bool done = false;
	void storeDone( ) {}
	void copyStep( ) { if (!done && seen++ == landAt) { done = true; (*pub)(*s, NoCopyProbe( )); } }
};
template <typename Pub, typename Reader>
Report landHarness(const Pub &pub, const LcdGlassShadow &old, const LcdGlass &oldGlass, int8_t oldCode, Reader rd) {
	LcdGlass newGlass = oldGlass; int8_t newCode = oldCode;
	pub.expect(newGlass, newCode);
	Report rep; rep.stops = COPY_STEPS;
	for (unsigned j = 0; j < COPY_STEPS; j++) {
		LcdGlassShadow w = old;
		LandInCopy<Pub> p{ &w, &pub, j };
		LcdGlass out; memset(&out, 0xEE, sizeof(out));
		Clock c;
		const int8_t code = rd(w, out, c, p);
		if (!p.done) { rep.mixes++; continue; }   /* the probe never fired: the case would mean nothing */
		if (code == LCD_GLASS_BUSY) rep.busy++;
		else if (code == oldCode && sameGlass(out, oldGlass)) rep.old++;
		else if (code == newCode && sameGlass(out, newGlass)) rep.fresh++;
		else { if (!rep.mixes) rep.firstMix = j + 1; rep.mixes++; }
	}
	return rep;
}

/* ── The cases ────────────────────────────────────────────────────────────────────────────────────────────────── */

void checkNoMix(const Report &r, const char* what) {
	char msg[200];
	snprintf(msg, sizeof(msg), "%s: %u of %u points gave a reader something that is neither the old glass nor the new one (the first at point %u)", what, r.mixes, r.stops, r.firstMix);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, r.mixes, msg);
}

/* Core 1 reset at a store of a publish: the probe throws there, so that nothing after that store runs, as on a core that was reset. */
struct Killed {};
struct KillAt { unsigned at, seen = 0; void storeDone( ) { if (++seen == at) throw Killed( ); } void copyStep( ) {} };

}  // namespace

void test_alpha_glass_reader_gets_what_the_writer_published(void) {
	LcdGlassShadow s{};
	LcdGlass g; Clock c;
	memset(&g, 0xEE, sizeof(g));
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_NONE, s.read(g, c), "nothing published: whole, and no frame");
	TEST_ASSERT_EQUAL_UINT32(0, g.seq);
	s.putBlank( );
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_NONE, s.read(g, c), "the controller's clear is not a frame");
	TEST_ASSERT_EACH_EQUAL_UINT8_MESSAGE(' ', g.ddram, 32, "every cell is a space");
	uint8_t bm[8]; fillGlyph(bm, 0x41);
	s.putGlyph(3, bm);
	s.putGlyph(11, bm);                     /* the driver masks the slot with 7 on the bus: so does the shadow */
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_NONE, s.read(g, c), "a glyph is not a frame either");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(bm, g.cgram[3], 8, "but it is on the glass");
	char line[2][17]; fillLine(line, 0x21);
	s.putFrame(line, 4242);
	memset(&g, 0xEE, sizeof(g));
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, s.read(g, c));
	TEST_ASSERT_EQUAL_MEMORY(line[0], g.ddram[0], 16);
	TEST_ASSERT_EQUAL_MEMORY(line[1], g.ddram[1], 16);
	TEST_ASSERT_EQUAL_MEMORY(bm, g.cgram[3], 8);
	TEST_ASSERT_EQUAL_UINT32(4242, g.blitMs);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, g.seq, "four publishes, two on the count for each");
	TEST_ASSERT_EQUAL_UINT32(0, s.seq & 1u);
}

void test_alpha_glass_stopped_writer_gives_busy_the_old_glass_or_the_new_never_a_mix(void) {
	LcdGlassShadow old{}; makeOld(old);
	const LcdGlass oldGlass = readWhole(old, LCD_GLASS_OK, "the old state");
	const PubFrame frame; const PubGlyph glyph; const PubBlank blank;
	const Report f = stopHarness(frame, old, oldGlass, LCD_GLASS_OK, RealReader( ));
	const Report g = stopHarness(glyph, old, oldGlass, LCD_GLASS_OK, RealReader( ));
	const Report b = stopHarness(blank, old, oldGlass, LCD_GLASS_OK, RealReader( ));
	checkNoMix(f, frame.name( )); checkNoMix(g, glyph.name( )); checkNoMix(b, blank.name( ));
	/* The stores of a frame: the odd count, 32 cells, the time, the flag, the even count. */
	TEST_ASSERT_EQUAL_UINT_MESSAGE(1 + 32 + 1 + 1 + 1, f.stops, "the stops of a frame are all its stores");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(1 + 8 + 1, g.stops, "the stops of a glyph: the odd count, 8 rows, the even count");
	/* A reader does not trust the shadow while the count is odd, and has the new glass at the last stop. */
	TEST_ASSERT_EQUAL_UINT_MESSAGE(f.stops - 1, f.busy, "busy at every stop but the last (frame)");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(g.stops - 1, g.busy, "busy at every stop but the last (glyph)");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(b.stops - 1, b.busy, "busy at every stop but the last (clear)");
	TEST_ASSERT_TRUE_MESSAGE(f.lastWasNew && g.lastWasNew && b.lastWasNew, "after its last store the reader has the new glass");
}

void test_alpha_glass_publish_landing_in_the_readers_copy_never_gives_a_mix(void) {
	LcdGlassShadow old{}; makeOld(old);
	const LcdGlass oldGlass = readWhole(old, LCD_GLASS_OK, "the old state");
	const PubFrame frame; const PubGlyph glyph; const PubBlank blank;
	const Report f = landHarness(frame, old, oldGlass, LCD_GLASS_OK, RealReader( ));
	const Report g = landHarness(glyph, old, oldGlass, LCD_GLASS_OK, RealReader( ));
	const Report b = landHarness(blank, old, oldGlass, LCD_GLASS_OK, RealReader( ));
	checkNoMix(f, frame.name( )); checkNoMix(g, glyph.name( )); checkNoMix(b, blank.name( ));
	/* The copy was overtaken and tried again: what comes back is the new glass every time, never the old one. */
	TEST_ASSERT_EQUAL_UINT_MESSAGE(COPY_STEPS, f.fresh, "a frame landing at any byte of the copy: the new glass");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(COPY_STEPS, g.fresh, "a glyph landing at any byte of the copy: the new glass");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(COPY_STEPS, b.fresh, "the clear landing at any byte of the copy: the new glass");
}

void test_alpha_glass_the_harnesses_see_a_mix_when_there_is_one(void) {
	LcdGlassShadow old{}; makeOld(old);
	const LcdGlass oldGlass = readWhole(old, LCD_GLASS_OK, "the old state");
	const PubFrame frame; const PubFrameWithoutOdd noOdd;
	/* A writer that never counts odd: a reader sees the count unchanged while the cells are half of each frame. */
	const Report w = stopHarness(noOdd, old, oldGlass, LCD_GLASS_OK, RealReader( ));
	TEST_ASSERT_TRUE_MESSAGE(w.mixes >= 30, "the stop harness finds the mix of a writer without the odd count (thirty-odd stops with a half-copied frame)");
	/* A reader that does not look at the count after its copy: a publish landing inside it gives half and half. */
	const Report r1 = landHarness(frame, old, oldGlass, LCD_GLASS_OK, ReaderWithoutSecondLook( ));
	TEST_ASSERT_TRUE_MESSAGE(r1.mixes >= 90, "the landing harness finds the mix of a reader without its second look (every byte but the last)");
	/* A reader that does not look at the count BEFORE its copy: it trusts a copy made while the writer is in the middle. */
	const Report r2 = stopHarness(frame, old, oldGlass, LCD_GLASS_OK, ReaderThatIgnoresOdd( ));
	TEST_ASSERT_TRUE_MESSAGE(r2.mixes >= 30, "the stop harness finds the mix of a reader that takes an odd count that did not move for a whole shadow");
	/* And the real ones, on the same shadows, find none: that is what the cases above say. */
	TEST_ASSERT_EQUAL_UINT(0, stopHarness(frame, old, oldGlass, LCD_GLASS_OK, RealReader( )).mixes);
	TEST_ASSERT_EQUAL_UINT(0, landHarness(frame, old, oldGlass, LCD_GLASS_OK, RealReader( )).mixes);
}

void test_alpha_glass_odd_count_heals_in_one_publish(void) {
	LcdGlassShadow s{}; makeOld(s);
	TEST_ASSERT_EQUAL_UINT32(20, s.seq);
	/* A writer that died after its odd store and before its even one: the count is odd and nothing else tells. */
	s.seq = 21;
	LcdGlass g; Clock c;
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_BUSY, s.read(g, c), "the reader cannot trust a shadow left odd");
	uint8_t bm[8]; fillGlyph(bm, 0x77);
	s.putGlyph(2, bm);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.seq & 1u, "one publish and the count is even again, not odd for good");
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(24, s.seq, "21 goes to 23 in the one store that opens the publish, and to 24 in the one that closes it");
	Clock c2;
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_NONE, s.read(g, c2), "the cells the dead writer may have left half-copied are not shown as a frame");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(bm, g.cgram[2], 8, "the glyph is");
	/* The next blit rewrites every cell and the parity stays right. */
	char line[2][17]; fillLine(line, 0x55);
	s.putFrame(line, 777);
	TEST_ASSERT_EQUAL_UINT32(26, s.seq);
	Clock c3;
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, s.read(g, c3));
	TEST_ASSERT_EQUAL_MEMORY(line[0], g.ddram[0], 16);
	TEST_ASSERT_EQUAL_MEMORY(line[1], g.ddram[1], 16);
	/* A frame heals as well, with no glyph first. */
	LcdGlassShadow t{}; makeOld(t);
	t.seq = 33;
	t.putFrame(line, 888);
	TEST_ASSERT_EQUAL_UINT32(36, t.seq);
	Clock c4;
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, t.read(g, c4));
	TEST_ASSERT_EQUAL_UINT32(888, g.blitMs);
}

void test_alpha_glass_writer_killed_at_any_store_never_leaves_a_torn_frame_visible(void) {
	LcdGlassShadow old{}; makeOld(old);
	const PubFrame frame;
	unsigned stops;
	{ CountStores cnt; LcdGlassShadow w = old; frame(w, cnt); stops = cnt.n; }
	TEST_ASSERT_EQUAL_UINT_MESSAGE(1 + 32 + 1 + 1 + 1, stops, "the writer's stores were counted (a loop over none would pass for nothing)");
	uint8_t bm[8]; fillGlyph(bm, 0x66);
	char next[2][17]; fillLine(next, 0xC0);
	for (unsigned k = 1; k <= stops; k++) {
		char what[60]; snprintf(what, sizeof(what), "writer killed after store %u of %u", k, stops);
		LcdGlassShadow w = old;
		bool died = false;
		try { frame(w, KillAt{ k }); } catch (const Killed &) { died = true; }
		TEST_ASSERT_TRUE_MESSAGE(died, what);
		const bool whole = (k == stops);            /* the last store is the even count: the publish was over */
		TEST_ASSERT_EQUAL_UINT32_MESSAGE(whole ? 0u : 1u, w.seq & 1u, what);
		LcdGlass g; Clock c;
		if (!whole) TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_BUSY, w.read(g, c), what);
		TEST_ASSERT_TRUE_MESSAGE(c.waited <= LIMIT + STEP, what);
		/* The relaunched loop's first publish is a glyph (BigFont_HD44780::begin( )). */
		w.putGlyph(0, bm);
		TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, w.seq & 1u, what);
		Clock c2;
		memset(&g, 0xEE, sizeof(g));
		const int8_t code = w.read(g, c2);
		if (whole) {
			char m[160]; snprintf(m, sizeof(m), "%s: the publish was over, so the frame is the new one", what);
			TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_OK, code, m);
			TEST_ASSERT_EQUAL_MEMORY_MESSAGE(frame.line[0], g.ddram[0], 16, m);
			TEST_ASSERT_EQUAL_MEMORY_MESSAGE(frame.line[1], g.ddram[1], 16, m);
		} else {
			char m[160]; snprintf(m, sizeof(m), "%s: the cells may be half of two frames, so there is no frame", what);
			TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_NONE, code, m);
		}
		TEST_ASSERT_EQUAL_MEMORY_MESSAGE(bm, g.cgram[0], 8, what);
		/* And the first blit puts the whole frame back, whatever came before. */
		w.putFrame(next, 5000);
		Clock c3;
		TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_OK, w.read(g, c3), what);
		TEST_ASSERT_EQUAL_MEMORY_MESSAGE(next[0], g.ddram[0], 16, what);
		TEST_ASSERT_EQUAL_MEMORY_MESSAGE(next[1], g.ddram[1], 16, what);
		TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, w.seq & 1u, what);
	}
}

void test_alpha_glass_reader_gives_up_within_its_limit(void) {
	LcdGlassShadow s{}; makeOld(s);
	LcdGlass g; Clock c;
	/* The count stays odd. */
	s.seq = s.seq + 1;
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_BUSY, s.read(g, c));
	TEST_ASSERT_TRUE_MESSAGE(c.waited <= LIMIT + STEP, "it gave up within the limit (one step past at most)");
	TEST_ASSERT_TRUE_MESSAGE(c.waited + 100 >= LIMIT, "and it did wait for the limit: a refusal at once would read a publish of tens of microseconds as busy");
	/* The count is even but a publish lands in every attempt: nothing to pause for, so the clock is the looks, and it still ends. */
	LcdGlassShadow t{}; makeOld(t);
	const PubFrame frame;
	struct EveryAttempt {
		LcdGlassShadow* s; const PubFrame* pub; unsigned seen = 0, landed = 0;
		void storeDone( ) {}
		void copyStep( ) { if (seen++ % COPY_STEPS == 0) { (*pub)(*s, NoCopyProbe( )); landed++; } }
	} every{ &t, &frame };
	Clock c2;
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_BUSY, t.read(g, c2, every));
	TEST_ASSERT_TRUE_MESSAGE(c2.t <= LIMIT + 2, "overtaken every time, it gave up within the limit");
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, c2.waited, "without a pause: a copy that was overtaken is tried again at once");
	TEST_ASSERT_TRUE_MESSAGE(every.landed > 1000, "and the writer really did overtake it in each of a thousand attempts");
}

void test_alpha_glass_reader_waits_out_a_publish_that_ends_within_its_limit(void) {
	LcdGlassShadow s{}; makeOld(s);
	const LcdGlass oldGlass = readWhole(s, LCD_GLASS_OK, "the state");
	s.seq = s.seq + 1;                              /* a publish under way */
	struct FinishingClock : Clock {
		LcdGlassShadow* s; unsigned pauses = 0;
		void pause(uint32_t us) { Clock::pause(us); if (++pauses == 5) s->seq = s->seq + 1; }   /* the writer's even store, 100 us in */
	} clk;
	clk.s = &s;
	LcdGlass g;
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_OK, s.read(g, clk), "it was still looking when the count went even");
	TEST_ASSERT_TRUE(sameGlass(g, oldGlass));
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(5 * STEP, clk.waited, "and it had waited for five steps, no more");
}

void test_alpha_glass_fake_lcd_publishes_at_the_blit_not_at_the_composition(void) {
	FakeHd44780 lcd;
	lcd.begin( );
	LcdGlass g;
	lcd.setCursor(0, 0); lcd.print("composed");
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_NONE, lcd.readGlass(g), "composed and not blitted: no frame");
	lcd.nowMs = 111; lcd.blit( );
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, lcd.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY("composed        ", g.ddram[0], 16);
	const uint32_t seq1 = g.seq;
	lcd.clear( ); lcd.setCursor(0, 1); lcd.print("second");
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, lcd.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE("composed        ", g.ddram[0], 16, "composed over it and not blitted: the previous frame");
	TEST_ASSERT_EQUAL_UINT32(111, g.blitMs);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(seq1, g.seq, "and the count has not moved");
	lcd.nowMs = 222; lcd.blit( );
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, lcd.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY("                ", g.ddram[0], 16);
	TEST_ASSERT_EQUAL_MEMORY("second          ", g.ddram[1], 16);
	TEST_ASSERT_EQUAL_UINT32(222, g.blitMs);
	TEST_ASSERT_EQUAL_UINT32(seq1 + 2, g.seq);
	/* The states a test of the route needs: the count left odd is busy, within the reader's limit. */
	lcd.glass.seq = lcd.glass.seq | 1u;
	lcd.clock = FakeHd44780::Clock( );
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_BUSY, lcd.readGlass(g));
	TEST_ASSERT_TRUE(lcd.clock.waited <= LIMIT + STEP);
}
