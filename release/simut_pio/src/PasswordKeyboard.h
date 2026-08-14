/**
 * @file PasswordKeyboard.h
 * @brief Geometry + character tables of the password-change keyboard
 *        (group grid + zoom popups), shared by the renderer
 *        (DisplayManager_Settings.cpp) and the touch mapper
 *        (DisplayManager_Touch.cpp).
 * @details Interaction model: every character costs exactly two taps.
 *          1st tap picks a group key (abc..wxyz, 123, @#!); 2nd tap picks
 *          the character inside a popup that shows lower AND upper case
 *          together — no Shift key, no layers. Tapping outside the popup
 *          card cancels it. Character set is identical to the pre-2.1.9
 *          three-layer keyboard: a-z, A-Z, 0-9, space and 28 symbols.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>
#include <string.h>

namespace PwdKb {

static const char* const GROUPS[8] = {
	"abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"
};
static const char DIGITS[]  = "1234567890";
static const char SYMBOLS[] = ".,!?:;\"'@#$%&*-+=~()[]{}/\\^_"; /* 28 */

/* Values of DisplayManager::_kbPopup. */
enum : int8_t {
	POPUP_NONE = 0,
	POPUP_GROUP0 = 1, /* 1..8 = GROUPS[popup - POPUP_GROUP0] */
	POPUP_DIGITS = 9,
	POPUP_SYMBOLS = 10,
};

/* Password strip: up to 7 boxes + OK beside them (y=33..60). */
constexpr int16_t BOX_X0 = 6, BOX_Y = 33, BOX_W = 26, BOX_H = 28, BOX_GAP = 4;
constexpr int16_t OK_X = 240, OK_Y = 33, OK_W = 74, OK_H = 28;

/* Main grid: 4 cols x 3 rows of 76x54 finger keys (~13.7 x 9.7 mm).
 * Row 0-1 = letter groups; row 2 = 123 / @#! / space / backspace. */
constexpr int16_t GRID_X0 = 2, GRID_COL_W = 80, GRID_KEY_W = 76;
constexpr int16_t GRID_Y0 = 68, GRID_ROW_H = 58, GRID_KEY_H = 54;

/* Letter popup: n keys of 68x56 per row, row 0 = lower, row 1 = UPPER. */
constexpr int16_t LP_KEY_W = 68, LP_KEY_H = 56, LP_GAP = 8;
constexpr int16_t LP_ROW0_Y = 72, LP_ROW1_Y = 136;
constexpr int16_t LP_CARD_Y = 64, LP_CARD_H = 136, LP_CARD_PAD = 8;

/* Digits popup: 5x2 keys of 52x56 ("12345" / "67890"). */
constexpr int16_t DP_X0 = 22, DP_KEY_W = 52, DP_KEY_H = 56, DP_GAP = 4;
constexpr int16_t DP_CARD_X = 14, DP_CARD_W = 292;

/* Symbols popup: 7x4 keys of 40x44 — all 28 symbols on one card. */
constexpr int16_t SP_X0 = 14, SP_COL_W = 42, SP_KEY_W = 40;
constexpr int16_t SP_Y0 = 44, SP_ROW_H = 47, SP_KEY_H = 44;
constexpr int16_t SP_CARD_X = 6, SP_CARD_Y = 36, SP_CARD_W = 308, SP_CARD_H = 200;

inline int letterCount(int8_t popup) {
	return (int)strlen(GROUPS[popup - POPUP_GROUP0]);
}
inline int16_t lpTotalW(int n) {
	return (int16_t)(n * LP_KEY_W + (n - 1) * LP_GAP);
}
inline int16_t lpX0(int n) { return (int16_t)((320 - lpTotalW(n)) / 2); }

/** Character produced by key index `idx` of popup `popup` (0 = invalid).
 *  Letter popups: 0..n-1 = lower row, n..2n-1 = upper row. */
inline char popupChar(int8_t popup, int idx) {
	if (popup == POPUP_DIGITS)
		return (idx >= 0 && idx < 10) ? DIGITS[idx] : '\0';
	if (popup == POPUP_SYMBOLS)
		return (idx >= 0 && idx < 28) ? SYMBOLS[idx] : '\0';
	const int n = letterCount(popup);
	if (idx < 0 || idx >= 2 * n) return '\0';
	const char c = GROUPS[popup - POPUP_GROUP0][idx % n];
	return (idx >= n) ? (char)(c - ('a' - 'A')) : c;
}

/** Hit test for an open popup.
 *  @return >=0 key index (see popupChar), -1 card but no key, -2 outside
 *          the card (= cancel). */
inline int popupHit(int8_t popup, int16_t x, int16_t y) {
	if (popup == POPUP_SYMBOLS) {
		if (x < SP_CARD_X || x >= SP_CARD_X + SP_CARD_W ||
		    y < SP_CARD_Y || y >= SP_CARD_Y + SP_CARD_H) return -2;
		const int row = (y - SP_Y0) / SP_ROW_H;
		const int col = (x - SP_X0) / SP_COL_W;
		if (row < 0 || row > 3 || col < 0 || col > 6) return -1;
		if (y >= SP_Y0 + row * SP_ROW_H + SP_KEY_H) return -1; /* row gap */
		if (x >= SP_X0 + col * SP_COL_W + SP_KEY_W) return -1; /* col gap */
		return row * 7 + col;
	}

	/* Letter and digit popups share the card band (y=64..199). */
	int n, keyW, gap; int16_t x0, cardX, cardW;
	if (popup == POPUP_DIGITS) {
		n = 5; keyW = DP_KEY_W; gap = DP_GAP; x0 = DP_X0;
		cardX = DP_CARD_X; cardW = DP_CARD_W;
	} else {
		n = letterCount(popup); keyW = LP_KEY_W; gap = LP_GAP; x0 = lpX0(n);
		cardX = (int16_t)(x0 - LP_CARD_PAD);
		cardW = (int16_t)(lpTotalW(n) + 2 * LP_CARD_PAD);
	}
	if (x < cardX || x >= cardX + cardW ||
	    y < LP_CARD_Y || y >= LP_CARD_Y + LP_CARD_H) return -2;
	int row;
	if (y >= LP_ROW0_Y && y < LP_ROW0_Y + LP_KEY_H) row = 0;
	else if (y >= LP_ROW1_Y && y < LP_ROW1_Y + LP_KEY_H) row = 1;
	else return -1;
	const int col = (x - x0) / (keyW + gap);
	if (col < 0 || col >= n) return -1;
	if (x >= x0 + col * (keyW + gap) + keyW) return -1; /* in the gap */
	return row * n + col;
}

} /* namespace PwdKb */
