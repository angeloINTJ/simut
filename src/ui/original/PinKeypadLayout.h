/**
 * @file PinKeypadLayout.h
 * @brief The geometry of the PIN keypad that only the original UI's paint and the native suite read: where a
 *        card's corner and its glyph slots sit (keyX, keyY, slotX, slotW), the label an ordered key prints
 *        (orderedLabel), the group popup's card (POP_CARD_X/W, popupCardY/H) and the strip of dots (DOTS_Y/H,
 *        dotSpacing, dotRadius). Moved word for word out of PinKeypad.h in S10c, C8 (2026-10-28), inside the same
 *        namespace, so no mangled name changed.
 *
 * Why it is a header of its own: the Ângulo draws the keypad with its own layout (ui/angulo/IdentidadeToque.h)
 * and links none of this. What the core, the web route (GET /api/keypad publishes the six POP_KEY_W/H, POP_GAP,
 * POP_ROW0/1_Y and POP_ONE_Y), the CLI or the Ângulo read stays in PinKeypad.h. tools/check_ui_boundary.py refuses
 * an include of anything under src/ui/original/ from an Ângulo unit, so a dependency of the Ângulo on this layout
 * is a finding at the gate and not bytes in its image.
 *
 * Who includes it: DisplayManager_UsersUi.cpp, which draws the keypad and is TFT-only (it is listed in
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

/* The group popup of the ALPHA_ALNUM keyboard: its card. The keys, the rows and the six numbers that
 * GET /api/keypad publishes are in PinKeypad.h. */
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

}  /* namespace PinKb */
