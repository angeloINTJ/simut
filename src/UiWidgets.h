/**
 * @file UiWidgets.h
 * @brief Shared UI widget layer for the TFT screens.
 * @details One place for the visual idiom every screen composes from:
 *   - uiTitleBar    : rounded title bar with accent tab + optional page dots
 *   - uiButton      : PRIMARY (accent) / SECONDARY (card + border) action button
 *   - uiNavArrow    : secondary button with a directional triangle
 *   - uiFooterMenu  : the standard menu footer (up, down, exit, primary)
 *   - uiCloseX      : the standard modal close button (drawn X, accent-high)
 *   - uiScrollbar   : list scrollbar (track + thumb)
 *   - uiDegC / uiUnit : a real degree ring instead of a printed "o"/"oC"/"c"
 *   - uiMenuIcon    : 16x16 procedural icons for the settings menu rows
 *
 * All functions draw into any Adafruit_GFX surface (TFT or GFXcanvas16) at
 * explicit coordinates, so callers keep their existing geometry and touch
 * zones. Colors always come from the active theme (Themes.h); fonts from
 * DisplayManager_Fonts.h. No heap allocation anywhere — these run inside the
 * Core 1 render path (T1.2).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once
#include "simut_config.h"
#if SIMUT_DISPLAY_TFT

#include <Adafruit_GFX.h>

enum UiBtnStyle : uint8_t {
	UI_BTN_PRIMARY = 0,  /**< Accent fill, background-colored text */
	UI_BTN_SECONDARY = 1 /**< Card fill, subtle border, main text */
};

enum UiArrowDir : uint8_t { UI_UP = 0, UI_DOWN = 1, UI_LEFT = 2, UI_RIGHT = 3 };

/** 50/50 RGB565 blend — cheap "translucency" for fills under curves. */
static inline uint16_t uiBlend565(uint16_t a, uint16_t b) {
	return (uint16_t)(((a & 0xF7DEu) >> 1) + ((b & 0xF7DEu) >> 1));
}

/** Rounded title bar at (4, yTop) 312 wide with an accent tab on the left.
 *  title may be nullptr (caller draws its own content over the bar).
 *  curPage >= 0 draws page dots right-aligned (curPage/totalPages). */
void uiTitleBar(Adafruit_GFX* g, int16_t yTop, const char* title,
	int curPage = -1, int totalPages = 0, int16_t h = 32);

/** Standard action button. Text centered both axes in the 9pt UI font. */
void uiButton(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h,
	const char* label, UiBtnStyle style);

/** Secondary button carrying a directional triangle (menu footers). */
void uiNavArrow(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h,
	UiArrowDir dir);

/** The standard menu footer at y=yBase: [up][down][exit][primary].
 *  primaryLabel == nullptr suppresses the primary slot. Geometry matches the
 *  historical footer exactly, so touch zones are untouched. yBase exists so
 *  the footer can be composed into a canvas strip at 0 and blitted to 195 —
 *  the default keeps every direct-to-TFT caller pixel-identical. */
void uiFooterMenu(Adafruit_GFX* g, const char* exitLabel,
	const char* primaryLabel, int16_t yBase = 195);

/** Modal close button: accent-high pill with a stroked X glyph. */
void uiCloseX(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h);

/** Page dots, right-aligned ending at x=304, centered on cy. */
void uiPageDots(Adafruit_GFX* g, int16_t cy, int cur, int total);

/** List scrollbar. Track at (x, y, w, h); thumb from cur/pages. */
void uiScrollbar(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h,
	int pages, int cur);

/** Prints a channel unit at a 9pt baseline. The unit arrives as UTF-8 from
 *  the channel table ("°C", "%", "hPa"); it is converted to Latin-1 so the
 *  real degree glyph of the 8-bit fonts is used. Sets font+color internally. */
void uiUnit(Adafruit_GFX* g, int16_t x, int16_t baselineY, const char* unit,
	uint16_t color);

/** Drawn width of what uiUnit(unit) will paint, in pixels. */
int16_t uiUnitWidth(Adafruit_GFX* g, const char* unit);

/** 16x16 icons for the settings menu: 0 palette, 1 bell, 2 note, 3 globe,
 *  4 lock, 5 crosshair, 6 document, 7 pulse, 8 move. */
void uiMenuIcon(Adafruit_GFX* g, int16_t x, int16_t y, uint8_t id,
	uint16_t color);

#endif /* SIMUT_DISPLAY_TFT */
