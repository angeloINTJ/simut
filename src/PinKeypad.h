/**
 * @file PinKeypad.h
 * @brief Geometry and character set of the scrambled panel-PIN keypad, shared
 *        by the renderer (DisplayManager_Users.cpp::drawPinScreen), the touch
 *        mapper (handleTouchPanelV24) and the CLI readout used by the bench.
 * @details Two keypads live here, and which one is on screen depends on what
 *          the screen is for.
 *
 *          IDENTIFYING (PIN_FOR_AUTH): four cards of three glyphs, the ten
 *          digits dealt over the twelve slots at random every time the screen
 *          opens. The two slots left over take a symbol, so that every card
 *          shows three glyphs and the shape of a card says nothing about how
 *          many digits it holds. A whole card is ONE tap: it says "one of
 *          these three" and never which, and Core 0 resolves the sequence
 *          against every account.
 *
 *          SETTING a PIN: an ordinary numeric pad, digits where a numeric pad
 *          puts them. Scrambling is for hiding a PIN someone already has from
 *          someone watching; choosing one is the opposite problem, and hunting
 *          a digit through a shuffled deal only costs the operator taps.
 *
 *          The keypad was 94 characters over four keys with a zoom popup for a
 *          few hours on 2026-09-19, because the PIN briefly accepted the whole
 *          printable set. The maintainer's verdict was that the faces were too
 *          cluttered to read, so both went back: digits only, and with only ten
 *          of them a second tap has nothing left to disambiguate.
 *
 *          What could not come back is the pre-v24 keypad's ONE tap naming a
 *          set of four. It held the expected PIN in plaintext and planted the
 *          next character among three decoys, asking only which key held it.
 *          v24 stores a digest and identifies BY the PIN, so at the moment of
 *          typing there is no expected character to plant and no account to
 *          plant it for. The decoys survive as what they always were to the
 *          person watching over your shoulder: noise on the glass.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>

namespace PinKb {

/* The PIN alphabet: ASCII digits, and nothing else. isValidPanelPin( ) in
 * SystemDefs_Validate.h enforces exactly this range, so what the panel can
 * type and what the web and the CLI accept are the same set by construction. */
constexpr char FIRST = '0';
constexpr char LAST  = '9';
constexpr int  CHARS = LAST - FIRST + 1;   /* 10 */

constexpr int  KEYS   = 4;                      /* cards */
constexpr int  SLOTS  = 3;                      /* per card — 12 slots for 10 digits */
constexpr int  DECOYS = KEYS * SLOTS - CHARS;   /* 2 */

/* Decoys are drawn from here, two per deal and never the same one twice in a
 * deal. They are symbols and not letters so that nobody reads them as part of
 * a PIN: every other surface says digits, and the glass must not suggest
 * otherwise. */
constexpr char DECOY_POOL[] = "!@#$%&*+-=?~";
constexpr int  DECOY_POOL_N = sizeof(DECOY_POOL) - 1;  /* 12 */

inline bool isDigitChar(char c) { return c >= FIRST && c <= LAST; }

/* ── Cards: 2x2 of 152x44, inside the 4-px safe area on every side ──
 * columns at 6 and 162 end at x=314; rows at 72 and 122 end at y=166, clear of
 * the footer strip (202..234) even with the display alignment offset at ±4.
 * The height is 44 and not more because the shared canvas is 320x45
 * (DisplayManager.cpp): a card taller than that would have to be blitted in
 * strips, and a card is one object. */
constexpr int16_t KEY_W = 152, KEY_H = 44;
constexpr int16_t KEY_X[KEYS] = { 6, 162, 6, 162 };
constexpr int16_t KEY_Y[KEYS] = { 72, 72, 122, 122 };

/* Three slots of 50 x 44 across a card. Identifying, they are drawn without
 * separators and the whole card is the target; setting a PIN never shows the
 * cards at all. The slots survive as the layout of the glyphs. */
constexpr int16_t SLOT_W = 50, SLOT_X0 = 1;

/* ── The ordered pad, for choosing a PIN ──
 * 3 x 4 keys of 80 x 29 where a numeric pad puts them: 1..9, then 0 alone on
 * the last row. Backspace and OK are in the footer, with the same rects the
 * rest of the panel uses, so they are not keys here. */
constexpr int16_t NUM_KEY_W = 80, NUM_KEY_H = 29;
constexpr int16_t NUM_X0 = 25, NUM_COL_PITCH = 90;
constexpr int16_t NUM_Y0 = 70, NUM_ROW_PITCH = 31;
constexpr int  NUM_COLS = 3, NUM_ROWS = 4;

/** Digit under (x, y) on the ordered pad, or 0 when the tap hits no key. */
inline char numKeyAt(int16_t x, int16_t y) {
	const int r = (y - NUM_Y0) / NUM_ROW_PITCH;
	const int c = (x - NUM_X0) / NUM_COL_PITCH;
	if (r < 0 || r >= NUM_ROWS || c < 0 || c >= NUM_COLS) return 0;
	if (y >= NUM_Y0 + r * NUM_ROW_PITCH + NUM_KEY_H) return 0;   /* between rows */
	if (x >= NUM_X0 + c * NUM_COL_PITCH + NUM_KEY_W) return 0;   /* between columns */
	if (r == NUM_ROWS - 1) return (c == 1) ? '0' : 0;            /* last row: 0 alone */
	return (char)('1' + r * NUM_COLS + c);
}

/* ── Chrome ── */
constexpr int16_t DOTS_Y = 40, DOTS_H = 28;          /* dots, or the refusal message */
/* The footer every other screen of the panel draws: y=195, 40 tall, exit at
 * 141 and the primary action at 222. Backspace takes the two nav-arrow slots
 * as one key, which is how this panel already merges slots it does not need
 * (drawSettingsThemes does it with a 174-wide BACK). The license button that
 * used to sit here is gone: the menu has the item, and four buttons of
 * different widths on this screen alone looked like a different product. */
constexpr int16_t FOOT_Y = 195, FOOT_H = 40;
constexpr int16_t FOOT_BACK_X = 5,   FOOT_BACK_W = 130;   /* backspace glyph */
constexpr int16_t FOOT_EXIT_X = 141, FOOT_EXIT_W = 75;    /* TR_BACK */
constexpr int16_t FOOT_OK_X   = 222, FOOT_OK_W   = 93;    /* TR_ENTER */

/** Index of the card containing (x, y), or -1. */
inline int keyAt(int16_t x, int16_t y) {
	for (int i = 0; i < KEYS; i++) {
		if (x >= KEY_X[i] && x < KEY_X[i] + KEY_W &&
		    y >= KEY_Y[i] && y < KEY_Y[i] + KEY_H) return i;
	}
	return -1;
}

/** Slot 0..SLOTS-1 inside card `key`, for an x already known to be on it. */
inline int slotAt(int key, int16_t x) {
	if (key < 0 || key >= KEYS) return -1;
	const int s = (x - KEY_X[key] - SLOT_X0) / SLOT_W;
	return (s < 0) ? 0 : (s >= SLOTS ? SLOTS - 1 : s);
}

/** Left edge of a slot, in screen coordinates. */
inline int16_t slotX(int key, int slot) {
	return (int16_t)(KEY_X[key] + SLOT_X0 + slot * SLOT_W);
}

}  /* namespace PinKb */
