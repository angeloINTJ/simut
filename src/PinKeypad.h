/**
 * @file PinKeypad.h
 * @brief Alphabet, geometry and CPU ceiling of the panel-PIN keypad, shared
 *        by the renderer (DisplayManager_Users.cpp::drawPinScreen), the touch
 *        mapper (handleTouchPanelV24), the validator (SystemDefs_Validate.h)
 *        and the CLI/HTTP readouts used by the bench.
 * @details Two keypads live here, and which one is on screen depends on what
 *          the screen is for.
 *
 *          IDENTIFYING (PIN_FOR_AUTH): the alphabet is dealt at random over a
 *          grid of cards every time the screen opens and again after every
 *          tap. A whole card is ONE tap: it says "one of these S" and never
 *          which. Since v25 the account is chosen BEFORE the PIN, so Core 0
 *          resolves the tap tree against ONE digest instead of the table.
 *
 *          SETTING a PIN: an ordinary ordered pad, characters where a pad puts
 *          them. Scrambling hides a PIN someone already has from someone
 *          watching; choosing one is the opposite problem, and hunting a
 *          character through a shuffled deal only costs the operator taps.
 *
 * @section v25 Why the policy has exactly these three knobs
 *          `S` (glyphs per card) is the same number twice: it is the set that
 *          hides the character from a watcher, and it is the branching factor
 *          of the search that a blind guess explores. Lowering it buys
 *          guessing resistance and sells shoulder-surfing resistance at the
 *          same rate — the trade is zero-sum and the admin owns it.
 *
 *          What is NOT zero-sum is the alphabet. The search costs S^n hashes
 *          whatever the alphabet is, while a guess is worth (S/C)^n: going
 *          from 10 digits to 36 alphanumerics multiplies the work of a blind
 *          guess by 168 (4 characters: 1 in 123 -> 1 in 20,736) and costs the
 *          CPU nothing. It also makes C = keys * S come out exact, so the deal
 *          has no decoys and no card is fatter than another — with ten digits
 *          over twelve slots a watcher-free attacker picks a 3-digit card and
 *          gets 3/10 per tap instead of the 2.5/10 an honest deal would give.
 *
 *          The CPU ceiling is real and it is why maxLenFor( ) exists. Measured
 *          on the rig on 2026-09-19: eight taps of three glyphs is a 9,840-node
 *          tree at 36.6 us per node, 360 ms of Core 0 with the web server
 *          stopped behind it (449 ms worst HTTP response against 91 ms idle).
 *          Ten taps would be 88,572 nodes and 3.2 s; sixteen would be 64.5
 *          million and 39 minutes. So the length ceiling is a function of the
 *          keypad, not a constant, and isValidPinPolicy( ) enforces it.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include "SystemDefs_Limits.h"   /* SIMUT_PANEL_PIN */

#if SIMUT_PANEL_PIN

namespace PinKb {

/* The tables below are `inline constexpr` and not plain `constexpr`: at
 * namespace scope a constexpr array has internal linkage, so every translation
 * unit that takes its address gets its own copy — GRIDS alone was costing
 * 128 B per object file across eight of them (measured 2026-09-20, while
 * pico_w_test_https was 2 kB over its FLASH region).
 *
 * ── The alphabets a PIN may be drawn from (PinPolicy::alphabet) ─────────── */
enum Alphabet : uint8_t {
	ALPHA_DIGITS = 0,   /**< 0-9 — ten characters, the v24 set            */
	ALPHA_ALNUM  = 1,   /**< 0-9A-Z — 36, upper case only (see below)     */
	ALPHA_COUNT  = 2
};

/* Upper case only, and no lower case ever: the cards are read at arm's length
 * on a 320x240 panel, and 'l' against '1' or 'O' against '0' is a refused
 * login the operator cannot explain. The web and the CLI upper-case what they
 * receive before validating, so a PIN typed anywhere is the same PIN. */
inline constexpr char DIGIT_SET[] = "0123456789";
inline constexpr char ALNUM_SET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

inline constexpr int ALPHA_SIZE[ALPHA_COUNT] = { 10, 36 };

inline int alphaCount(uint8_t a) { return ALPHA_SIZE[(a < ALPHA_COUNT) ? a : 0]; }
inline const char* alphaChars(uint8_t a) { return (a == ALPHA_ALNUM) ? ALNUM_SET : DIGIT_SET; }

/** Is `c` a character SOME alphabet can produce? Used to tell a dealt
 *  character from a decoy without passing the policy around: decoys are
 *  symbols by construction, so the test is the union of the alphabets. */
inline bool isPinChar(char c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

/** Is `c` inside alphabet `a`? The rule the validator applies. */
inline bool inAlphabet(char c, uint8_t a) {
	if (c >= '0' && c <= '9') return true;
	return (a == ALPHA_ALNUM) && c >= 'A' && c <= 'Z';
}

/* ── The keypads (PinPolicy::keypad). The value IS the glyphs per card ───── */
enum Keypad : uint8_t {
	KB_PLAIN = 1,   /**< one character per key, positions shuffled        */
	KB_SET2  = 2,   /**< a tap names two                                  */
	KB_SET3  = 3,   /**< a tap names three — the v24 keypad               */
	KB_MIN   = 1,
	KB_MAX   = 3
};

/* Longest PIN each keypad can resolve inside ~400 ms of Core 0. See the CPU
 * note in the file header: the tree is S + S^2 + ... + S^n nodes, one SHA-256
 * each, 36.6 us per node measured. KB_PLAIN searches nothing (one candidate),
 * so its ceiling is the buffer, not the clock. */
constexpr uint8_t PIN_LEN_MIN      = 4;
constexpr uint8_t PIN_LEN_CEILING  = 16;  /**< buffer ceiling, KB_PLAIN       */
inline constexpr uint8_t LEN_MAX[KB_MAX + 1] = { 0, 16, 12, 8 };

inline uint8_t maxLenFor(uint8_t keypad) {
	return (keypad >= KB_MIN && keypad <= KB_MAX) ? LEN_MAX[keypad] : LEN_MAX[KB_SET3];
}

/* ── The lockout ladder ──────────────────────────────────────────────────
 * Six failures take the ACCOUNT out until reboot; PANEL_FAIL_CEILING failures
 * across all accounts take the PANEL out. Both are needed: only-per-panel is
 * what v24 had, and six wrong taps from anybody shut the panel for everyone
 * until someone power-cycled it; only-per-account hands an attacker
 * MAX_USERS x 6 attempts, which is the table-size factor the account picker
 * was added to remove. */
constexpr uint8_t SLOT_FAIL_MAX       = 6;
constexpr uint8_t PANEL_FAIL_CEILING  = 20;

/* ── The grid ────────────────────────────────────────────────────────────
 * One layout per (alphabet, keypad). Every one keeps the 4-px safe area on
 * all sides and clears both the dot strip above (40..68) and the footer
 * below (195), so the display alignment offset at +-4 can never cut a key.
 *
 * The six-column layout starts at x=4 and not 6: at 6 the row ended on 316,
 * a pixel past the safe area, and shaving the key to 49 put a glyph slot at
 * 23 px — under the 24 the native suite holds the layouts to.
 *
 * GRIDS holds the DEALT layouts only — KB_SET2 and KB_SET3. KB_PLAIN is not
 * dealt at all since 2026-09-20: one glyph per key means the tap names the
 * character outright, so a shuffle hides nothing from anybody who can read
 * the glass, and it costs the operator their muscle memory on every entry.
 * Its layouts are the ORDERED keyboards further down (orderedGrid( )) — which
 * is also why ALPHA_ALNUM + KB_PLAIN is supported now and was not before: it
 * is no longer 36 keys of 50x19 px, it is nine group keys and a popup. */
struct Grid {
	uint8_t keys;    /**< cards on screen                                  */
	uint8_t slots;   /**< glyphs per card (= the policy's keypad value)     */
	uint8_t cols, rows;
	int16_t x0, y0;  /**< top-left of key 0                                */
	int16_t w, h;    /**< key size                                         */
	int16_t px, py;  /**< pitch between keys                               */
};

/* keys x slots must be >= the alphabet; what is left over is dealt as decoys,
 * so that every card carries the same number of glyphs and the SHAPE of a
 * card says nothing about how many real characters it holds. */
inline constexpr Grid GRIDS[ALPHA_COUNT][KB_MAX + 1] = {
	/* ALPHA_DIGITS (10 characters) */
	{ { 0,0,0,0,0,0,0,0,0,0 },
	  { 0,0,0,0,0,0,0,0,0,0 },                   /* plain:  ordered, see below */
	  {  6, 2, 3, 2,  6, 72,100, 44,104, 50 },   /* set2:   12 slots, 2 decoys */
	  {  4, 3, 2, 2,  6, 72,152, 44,156, 50 } }, /* set3:   12 slots, 2 decoys — v24 */
	/* ALPHA_ALNUM (36 characters) */
	{ { 0,0,0,0,0,0,0,0,0,0 },
	  { 0,0,0,0,0,0,0,0,0,0 },                   /* plain:  ordered, see below */
	  { 18, 2, 6, 3,  4, 72, 50, 35, 52, 40 },   /* set2:   36 slots, 0 decoys */
	  { 12, 3, 4, 3,  6, 72, 74, 35, 78, 40 } }  /* set3:   36 slots, 0 decoys */
};

/** Is this keypad the ORDERED keyboard rather than a dealt one? One glyph per
 *  key is the whole condition: there is nothing to hide inside a set of one,
 *  so the characters sit where the operator expects them. */
inline bool isOrdered(uint8_t keypad) { return keypad == KB_PLAIN; }

/* ── The ORDERED keyboards ───────────────────────────────────────────────
 * Two of them, chosen by ALPHABET, and they serve two screens: identifying
 * under KB_PLAIN, and choosing a PIN under any keypad (choosing is always
 * ordered — the operator is picking a PIN, not proving one).
 *
 * ALPHA_DIGITS: the numeric pad, 1..9 then 0 alone (NUM_GRID). Two of its
 * twelve cells are empty and their face is "" — keyAt( ) still returns them
 * and the caller drops an empty face, which is the same rule a decoy-free
 * deal already needs.
 *
 * ALPHA_ALNUM: nine group keys of 100x36, and the second tap picks inside a
 * popup. Two taps per character is the interaction PasswordKeyboard.h has
 * used since 2.1.9, and it is what makes 36 characters fit a finger: the
 * alternative, one key each, is a 6x6 grid of 50x19 px. */
inline constexpr char GROUP_CHARS[9][11] = {
	"0123456789", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"
};
constexpr int GROUP_COUNT = 9;
/** Longest face a key can carry: the digits group. _pinKeyChars is sized from
 *  this, and it is why that buffer is not SLOTS_MAX wide. */
constexpr int FACE_MAX = 10;

/* Three columns of 100 px at 6/110/214 end at 314, and three rows of 36 at
 * 72/112/152 end at 188 — inside the 4..315 x 4..235 safe area and clear of
 * both the dot strip (40..68) and the footer (195). */
inline constexpr Grid GROUP_GRID = { 9, 1, 3, 3,  6, 72, 100, 36, 104, 40 };
inline constexpr Grid NUM_GRID   = { 12, 1, 3, 4, 25, 70,  80, 29,  90, 31 };

/** The ordered layout for this alphabet. */
inline Grid orderedGrid(uint8_t alphabet) {
	return (alphabet == ALPHA_ALNUM) ? GROUP_GRID : NUM_GRID;
}

/** The layout on screen for this pair. Never returns an empty grid: a
 *  renderer handed `keys == 0` draws a blank keypad, and a stored policy that
 *  no build supports any more has to degrade to a keypad somebody can type
 *  on, not to nothing. */
inline Grid gridFor(uint8_t alphabet, uint8_t keypad) {
	if (alphabet >= ALPHA_COUNT || keypad < KB_MIN || keypad > KB_MAX)
		return GRIDS[ALPHA_DIGITS][KB_SET3];
	if (isOrdered(keypad)) return orderedGrid(alphabet);
	if (GRIDS[alphabet][keypad].keys == 0) return GRIDS[ALPHA_DIGITS][KB_SET3];
	return GRIDS[alphabet][keypad];
}

/** Does this pair have a layout? The one rule isValidPinPolicy( ) asks about.
 *  Every alphabet has an ordered keyboard, so KB_PLAIN is always supported. */
inline bool comboSupported(uint8_t alphabet, uint8_t keypad) {
	if (alphabet >= ALPHA_COUNT || keypad < KB_MIN || keypad > KB_MAX) return false;
	return isOrdered(keypad) || GRIDS[alphabet][keypad].keys != 0;
}

/** What key `k` of the ordered keyboard carries: a group for ALPHA_ALNUM, one
 *  digit for ALPHA_DIGITS, "" for the two empty cells of the numeric pad.
 *  Returns the length written. */
inline uint8_t orderedFace(uint8_t alphabet, int k, char* out, size_t cap) {
	if (!out || cap == 0) return 0;
	out[0] = '\0';
	if (alphabet == ALPHA_ALNUM) {
		if (k < 0 || k >= GROUP_COUNT) return 0;
		const size_t n = strlen(GROUP_CHARS[k]);
		if (n + 1 > cap) return 0;
		memcpy(out, GROUP_CHARS[k], n + 1);
		return (uint8_t)n;
	}
	if (k < 0 || k >= 12 || cap < 2) return 0;
	/* 1..9 across the first three rows, then 0 alone in the middle of the
	 * fourth: cells 9 and 11 are the empty ones. */
	const char c = (k < 9) ? (char)('1' + k) : ((k == 10) ? '0' : '\0');
	if (!c) return 0;
	out[0] = c; out[1] = '\0';
	return 1;
}

/** What to PRINT on key `k`, which is not always what it CARRIES: the ten
 *  digits of group 0 need ~100 px at 9 pt and the key is 100 px wide, so that
 *  one is labelled "0-9". Everything else prints its characters. */
inline void orderedLabel(uint8_t alphabet, int k, char* out, size_t cap) {
	orderedFace(alphabet, k, out, cap);
	if (alphabet == ALPHA_ALNUM && k == 0 && cap >= 4) {
		out[0] = '0'; out[1] = '-'; out[2] = '9'; out[3] = '\0';
	}
}

/* ── The popup of the ALPHA_ALNUM keyboard ───────────────────────────────
 * Up to five keys per row, two rows at most, every row centred. That one rule
 * covers both shapes it has to draw: a letter group of three or four on one
 * row, and the ten digits as 5 + 5. */
constexpr int16_t POP_KEY_W = 56, POP_KEY_H = 52, POP_GAP = 6;
/* Two rows at 76 and 132 end at 184, and their card at 70 ends at 190 — clear
 * of the dot strip above (40..68) and of the footer below (195). The first
 * draft put them at 84/142 and the card ran to 198, six pixels INTO the
 * footer, where BACK and ENTER live. */
constexpr int16_t POP_ROW0_Y = 76, POP_ROW1_Y = 132, POP_ONE_Y = 104;
constexpr int16_t POP_CARD_X = 6, POP_CARD_W = 308;

inline int popupRows(int n)          { return (n > 5) ? 2 : 1; }
inline int popupRowKeys(int n, int r) {
	if (n <= 5) return (r == 0) ? n : 0;
	const int first = (n + 1) / 2;
	return (r == 0) ? first : n - first;
}
inline int16_t popupRowY(int n, int r) {
	if (n <= 5) return POP_ONE_Y;
	return (r == 0) ? POP_ROW0_Y : POP_ROW1_Y;
}
inline int16_t popupRowX0(int m) {
	return (int16_t)((320 - (m * POP_KEY_W + (m - 1) * POP_GAP)) / 2);
}
/* One card for both shapes, covering the whole keyboard area (70..190). A
 * card sized to its row left the group keys showing above and below it, which
 * reads as a keyboard with a stripe through it rather than as a layer over
 * one — and a single row centred in the full card lands on POP_ONE_Y anyway. */
inline int16_t popupCardY(int n) { (void)n; return (int16_t)70; }
inline int16_t popupCardH(int n) { (void)n; return (int16_t)120; }

/** Index into the group of the popup key at (x, y), or -1 — which the caller
 *  reads as "cancel", the same as tapping off the card. */
inline int popupKeyAt(int n, int16_t x, int16_t y) {
	int base = 0;
	for (int r = 0; r < popupRows(n); r++) {
		const int m = popupRowKeys(n, r);
		const int16_t ry = popupRowY(n, r), rx = popupRowX0(m);
		if (y >= ry && y < ry + POP_KEY_H) {
			for (int i = 0; i < m; i++) {
				const int16_t kx = (int16_t)(rx + i * (POP_KEY_W + POP_GAP));
				if (x >= kx && x < kx + POP_KEY_W) return base + i;
			}
			return -1;
		}
		base += m;
	}
	return -1;
}

/* The widest grid and the widest card, for the fixed-size buffers that hold a
 * deal (DisplayManager::_pinKeyChars) and one tap (DisplayManager::_pinTaps).
 * KEYS_MAX is ALPHA_ALNUM/KB_SET2; SLOTS_MAX is KB_SET3. */
constexpr int KEYS_MAX  = 18;
constexpr int SLOTS_MAX = 3;
constexpr int DEAL_MAX  = KEYS_MAX * SLOTS_MAX;   /* 54 — the shuffle buffer */

/* Decoys are drawn from here, never the same one twice in a deal. They are
 * symbols and not letters so that nobody reads them as part of a PIN: every
 * other surface says digits or capitals, and the glass must not suggest
 * otherwise. With ALPHA_ALNUM the alphabet fills the grid exactly and none is
 * dealt — which is the whole point of the wider alphabet. */
inline constexpr char DECOY_POOL[] = "!@#$%&*+-=?~";
constexpr int  DECOY_POOL_N = sizeof(DECOY_POOL) - 1;  /* 12 */

/** How many decoys a deal needs. Zero for every ALPHA_ALNUM layout, and zero
 *  for the ordered keyboards — they are not dealt, so `keys * slots` is not a
 *  slot count there and the subtraction would go negative (the group keyboard
 *  is 9 keys carrying 36 characters). */
inline int decoyCount(uint8_t alphabet, uint8_t keypad) {
	if (isOrdered(keypad)) return 0;
	const Grid g = gridFor(alphabet, keypad);
	return (int)g.keys * (int)g.slots - alphaCount(alphabet);
}

/* ── Hit tests ─────────────────────────────────────────────────────────── */

/** Index of the card containing (x, y) in this layout, or -1. */
inline int keyAt(const Grid& g, int16_t x, int16_t y) {
	for (int i = 0; i < (int)g.keys; i++) {
		const int16_t kx = (int16_t)(g.x0 + (i % g.cols) * g.px);
		const int16_t ky = (int16_t)(g.y0 + (i / g.cols) * g.py);
		if (x >= kx && x < kx + g.w && y >= ky && y < ky + g.h) return i;
	}
	return -1;
}

/** Top-left of card `k`. */
inline int16_t keyX(const Grid& g, int k) { return (int16_t)(g.x0 + (k % g.cols) * g.px); }
inline int16_t keyY(const Grid& g, int k) { return (int16_t)(g.y0 + (k / g.cols) * g.py); }

/** Left edge of glyph slot `s` inside card `k`. Slots split the card evenly;
 *  identifying, they are drawn without separators and the whole card is the
 *  target, so they survive only as the layout of the glyphs. */
inline int16_t slotX(const Grid& g, int k, int s) {
	return (int16_t)(keyX(g, k) + 1 + s * ((g.w - 2) / (g.slots ? g.slots : 1)));
}
inline int16_t slotW(const Grid& g) {
	return (int16_t)((g.w - 2) / (g.slots ? g.slots : 1));
}

/* The numeric pad's geometry used to live here as NUM_X0/NUM_Y0/NUM_KEY_W/…
 * beside its own hit test. It is NUM_GRID above now, hit-tested by the same
 * keyAt( ) as every other layout: two copies of one rectangle is two things
 * to keep in step, and this panel has already paid for that once. */

/* ── Chrome ──
 * The dots, or the refusal message, at y=40. Spacing shrinks once a PIN is
 * long enough that 20 px would run past the safe area: sixteen dots at 20 px
 * is 320, one pixel wider than the panel allows (measured 2026-09-20). */
constexpr int16_t DOTS_Y = 40, DOTS_H = 28;
inline int16_t dotSpacing(int n) { return (n > 14) ? (int16_t)18 : (int16_t)20; }
inline int16_t dotRadius(int n)  { return (n > 14) ? (int16_t)5  : (int16_t)6;  }

/* The footer every other screen of the panel draws: y=195, 40 tall, exit at
 * 141 and the primary action at 222. Backspace takes the two nav-arrow slots
 * as one key, which is how this panel already merges slots it does not need
 * (drawSettingsThemes does it with a 174-wide BACK). */
constexpr int16_t FOOT_Y = 195, FOOT_H = 40;
constexpr int16_t FOOT_BACK_X = 5,   FOOT_BACK_W = 130;   /* backspace glyph */
constexpr int16_t FOOT_EXIT_X = 141, FOOT_EXIT_W = 75;    /* TR_BACK */
constexpr int16_t FOOT_OK_X   = 222, FOOT_OK_W   = 93;    /* TR_ENTER */

}  /* namespace PinKb */

#endif /* SIMUT_PANEL_PIN — an image with no touch panel has no keypad */
