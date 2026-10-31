/**
 * @file PinKeypadLayout.h
 * @brief The geometry of the PIN keypad that only the original UI's paint, its touch handler and the native suite
 *        read: where a card's corner and its glyph slots sit (keyX, keyY, slotX, slotW), the label an ordered key
 *        prints (orderedLabel), the group popup's card (POP_CARD_X/W, popupCardY/H), its rows and its hit test
 *        (popupRows, popupRowKeys, popupRowY, popupRowX0, popupKeyAt), the strip of dots (DOTS_Y/H, dotSpacing,
 *        dotRadius) and the footer's zones (FOOT_Y, FOOT_H, FOOT_BACK_X/W, FOOT_EXIT_X/W, FOOT_OK_X/W). Moved word
 *        for word out of PinKeypad.h, inside the same namespace, so no mangled name changed: in S10c, C8
 *        (2026-10-28), and the last thirteen names (the popup's rows and hit test, the footer) in S12, A3 (2026-10-31).
 *
 * Why it is a header of its own: the Ângulo draws the keypad with its own layout (ui/angulo/IdentidadeToque.h)
 * and links none of this. What the core, the web route (GET /api/keypad publishes the six POP_KEY_W/H, POP_GAP,
 * POP_ROW0/1_Y and POP_ONE_Y on the original panel, and none of them on the Ângulo), the CLI or the Ângulo read stays in PinKeypad.h. tools/check_ui_boundary.py refuses
 * an include of anything under src/ui/original/ from an Ângulo unit, so a dependency of the Ângulo on this layout
 * is a finding at the gate and not bytes in its image. keyAt( ), the cards' hit test, is the original's too and
 * stays in PinKeypad.h all the same: S11's first try moved it with the thirteen and six of the eleven images
 * changed, and S12's A3 is kept only with every image byte for byte (docs/analysis/UI_DRIVERS.md section 4.7).
 *
 * Who includes it: DisplayManager_UsersUi.cpp, which draws the keypad, maps a tap on it and is TFT-only (it is listed in
 * [ui.original].units of tools/features.toml), and the native suite test/test_validators, which holds the layout
 * to the panel's safe area. It has no #if: only TFT-only units include it, and the static_assert says to the
 * compiler what the #if would (as DisplayManager_PinActions.h does), so the feature-sprawl gate
 * (tools/check_feature_sprawl.py) counts no new site.
 */
#pragma once

#include "../../PinKeypad.h"

static_assert(SIMUT_PANEL_PIN, "the PIN keypad's original layout exists only where a panel proves a PIN: include this header from a TFT-only unit");

namespace PinKb {

/** What to PRINT on key `k`, which is not always what it CARRIES: the ten
 *  digits of group 0 need ~100 px at 9 pt and the key is 100 px wide, so that
 *  one is labelled "0-9". Everything else prints its characters. */
inline void orderedLabel(uint8_t alphabet, int k, char* out, size_t cap) {
	orderedFace(alphabet, k, out, cap);
	if (alphabet == ALPHA_ALNUM && k == 0 && cap >= 4) {
		out[0] = '0'; out[1] = '-'; out[2] = '9'; out[3] = '\0';
	}
}

/* The group popup of the ALPHA_ALNUM keyboard: its card. The six numbers that GET /api/keypad publishes on
 * the original panel (the Ângulo's answer has none) are in PinKeypad.h; the popup's rows and hit test are below. */
constexpr int16_t POP_CARD_X = 6, POP_CARD_W = 308;

/* One card for both shapes, covering the whole keyboard area (70..190). A
 * card sized to its row left the group keys showing above and below it, which
 * reads as a keyboard with a stripe through it rather than as a layer over
 * one — and a single row centred in the full card lands on POP_ONE_Y anyway. */
inline int16_t popupCardY(int n) { (void)n; return (int16_t)70; }
inline int16_t popupCardH(int n) { (void)n; return (int16_t)120; }

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

/* ── Chrome ──
 * The dots, or the refusal message, at y=40. Spacing shrinks once a PIN is
 * long enough that 20 px would run past the safe area: sixteen dots at 20 px
 * is 320, one pixel wider than the panel allows (measured 2026-09-20). */
constexpr int16_t DOTS_Y = 40, DOTS_H = 28;
inline int16_t dotSpacing(int n) { return (n > 14) ? (int16_t)18 : (int16_t)20; }
inline int16_t dotRadius(int n)  { return (n > 14) ? (int16_t)5  : (int16_t)6;  }

/* ── The popup's rows and hit test, and the footer's zones ──────────────────────────────────────────
 * Moved word for word out of PinKeypad.h in S12, A3 (2026-10-31): only the original's paint and its touch handler
 * (DisplayManager_UsersUi.cpp) and the native suite read them. They lean on POP_KEY_W/H, POP_GAP, POP_ROW0/1_Y and
 * POP_ONE_Y, which GET /api/keypad publishes on the original panel and which stay in PinKeypad.h, included above. */
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

/* The footer every other screen of the panel draws: y=195, 40 tall, exit at
 * 141 and the primary action at 222. Backspace takes the two nav-arrow slots
 * as one key, which is how this panel already merges slots it does not need
 * (drawSettingsThemes does it with a 174-wide BACK). */
constexpr int16_t FOOT_Y = 195, FOOT_H = 40;
constexpr int16_t FOOT_BACK_X = 5,   FOOT_BACK_W = 130;   /* backspace glyph */
constexpr int16_t FOOT_EXIT_X = 141, FOOT_EXIT_W = 75;    /* TR_BACK */
constexpr int16_t FOOT_OK_X   = 222, FOOT_OK_W   = 93;    /* TR_ENTER */

}  /* namespace PinKb */
