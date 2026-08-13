/**
 * @file UiWidgets.cpp
 * @brief Implementation of the shared TFT widget layer (see UiWidgets.h).
 * @details Single TU on purpose: the build has no LTO, so header-inline
 * widgets would be duplicated into every DisplayManager_*.cpp that
 * uses them. Everything here is theme-driven and heap-free.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#include "simut_config.h"
#if SIMUT_DISPLAY_TFT

#include "UiWidgets.h"
#include "Themes.h"
#include "DisplayManager_Fonts.h"

/* Centered text inside a rect, 9pt UI font. Assumes font/color already set. */
static void uiCenteredText(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w,
	int16_t h, const char* label) {
	int16_t bx, by; uint16_t bw, bh;
	g->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
	g->setCursor(x + (w - (int16_t)bw) / 2 - bx, y + (h - (int16_t)bh) / 2 - by);
	g->print(label);
}

void uiTitleBar(Adafruit_GFX* g, int16_t yTop, const char* title,
	int curPage, int totalPages, int16_t h) {
	g->fillRoundRect(4, yTop, 312, h, 8, C_CARD_BG);
	/* Accent tab: what visually brands "this is a screen title". */
	g->fillRoundRect(4, yTop, 6, h, 3, C_ACCENT);

	if (title) {
		g->setFont(&simutFont9pt);
		g->setTextSize(1);
		g->setTextColor(C_TITLE_TEXT);
		g->setCursor(18, yTop + h / 2 + 6);
		g->print(title);
	}

	if (curPage >= 0 && totalPages > 1)
		uiPageDots(g, (int16_t)(yTop + h / 2), curPage, totalPages);
}

void uiPageDots(Adafruit_GFX* g, int16_t cy, int cur, int total) {
	if (total < 2 || total > 8) return;
	for (int p = 0; p < total; p++) {
		int16_t cx = (int16_t)(304 - (total - 1 - p) * 11);
		if (p == cur) g->fillCircle(cx, cy, 3, C_ACCENT);
		else g->drawCircle(cx, cy, 2, C_TEXT_OFF);
	}
}

void uiButton(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h,
	const char* label, UiBtnStyle style) {
	const bool primary = (style == UI_BTN_PRIMARY);
	g->fillRoundRect(x, y, w, h, 8, primary ? C_ACCENT : C_CARD_BG);
	if (!primary) g->drawRoundRect(x, y, w, h, 8, C_TEXT_SUB);
	g->setFont(&simutFont9pt);
	g->setTextSize(1);
	g->setTextColor(primary ? C_BG_MAIN : C_TEXT_MAIN);
	uiCenteredText(g, x, y, w, h, label);
}

void uiNavArrow(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h,
	UiArrowDir dir) {
	g->fillRoundRect(x, y, w, h, 8, C_CARD_BG);
	g->drawRoundRect(x, y, w, h, 8, C_TEXT_SUB);
	const int16_t cx = x + w / 2, cy = y + h / 2;
	switch (dir) {
	case UI_UP:
		g->fillTriangle(cx, cy - 7, cx - 10, cy + 7, cx + 10, cy + 7, C_TEXT_MAIN);
		break;
	case UI_DOWN:
		g->fillTriangle(cx - 10, cy - 7, cx + 10, cy - 7, cx, cy + 7, C_TEXT_MAIN);
		break;
	case UI_LEFT:
		g->fillTriangle(cx + 7, cy - 10, cx + 7, cy + 10, cx - 7, cy, C_TEXT_MAIN);
		break;
	case UI_RIGHT:
		g->fillTriangle(cx - 7, cy - 10, cx - 7, cy + 10, cx + 7, cy, C_TEXT_MAIN);
		break;
	}
}

void uiFooterMenu(Adafruit_GFX* g, const char* exitLabel,
	const char* primaryLabel, int16_t yBase) {
	/* Geometry is the historical footer: zones in DisplayManager_Touch.cpp
	 * are derived from these same rects — do not move them on screen. */
	uiNavArrow(g, 5, yBase, 62, 40, UI_UP);
	uiNavArrow(g, 73, yBase, 62, 40, UI_DOWN);
	uiButton(g, 141, yBase, 75, 40, exitLabel, UI_BTN_SECONDARY);
	if (primaryLabel) uiButton(g, 222, yBase, 93, 40, primaryLabel, UI_BTN_PRIMARY);
}

void uiCloseX(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h) {
	/* Radius 4 — the system's SMALL-element radius (keyboard keys, the
	 * editor's step buttons, the scrollbar all use it). GFX's fill arcs at
	 * r=6..8 drop one pixel per column for most of the arc, i.e. they ARE
	 * a 45-degree chamfer at this scale; only r<=4 keeps the diagonal run
	 * short enough to read as a rounded corner on a 24 px button.
	 * Standard rect: 32x24 flush to the title bar's right edge. */
	int16_t rad = 4;
	if (rad > h / 2) rad = h / 2;
	if (rad > w / 2) rad = w / 2;
	g->fillRoundRect(x, y, w, h, rad, C_ACCENT_HIGH);
	/* Stroked X, 3 px weight — matches the bold face used everywhere. */
	const int16_t r = (h < w ? h : w) / 2 - 6;
	const int16_t cx = x + w / 2, cy = y + h / 2;
	for (int8_t i = -1; i <= 1; i++) {
		g->drawLine(cx - r + i, cy - r, cx + r + i, cy + r, C_BG_MAIN);
		g->drawLine(cx - r + i, cy + r, cx + r + i, cy - r, C_BG_MAIN);
	}
}

void uiScrollbar(Adafruit_GFX* g, int16_t x, int16_t y, int16_t w, int16_t h,
	int pages, int cur) {
	g->fillRoundRect(x, y, w, h, 4, C_CARD_BG);
	g->drawRoundRect(x, y, w, h, 4, C_TEXT_SUB);
	if (pages < 1) pages = 1;
	int thumbH = h / pages;
	if (thumbH < 20) thumbH = 20;
	int thumbY = y;
	if (pages > 1) thumbY += (cur * (h - thumbH)) / (pages - 1);
	g->fillRoundRect(x, (int16_t)thumbY, w, (int16_t)thumbH, 4, C_ACCENT);
}

/* Channel units arrive as UTF-8 ("°C" = C2 B0 43). The Latin-1 fonts index
 * the degree glyph at 0xB0, so the unit is converted and typeset directly. */
static void uiUnitToLatin1(const char* unit, char* out, size_t outSize) {
	size_t o = 0;
	if (unit) {
		const uint8_t* p = (const uint8_t*)unit;
		while (*p && o + 1 < outSize) {
			if ((p[0] == 0xC2 || p[0] == 0xC3) && (p[1] & 0xC0) == 0x80) {
				out[o++] = (char)(((p[0] & 0x03) << 6) | (p[1] & 0x3F));
				p += 2;
			} else {
				out[o++] = (char)*p++;
			}
		}
	}
	out[o] = '\0';
}

void uiUnit(Adafruit_GFX* g, int16_t x, int16_t baselineY, const char* unit,
	uint16_t color) {
	if (!unit) return;
	char l1[12];
	uiUnitToLatin1(unit, l1, sizeof(l1));
	g->setFont(&simutFont9pt);
	g->setTextSize(1);
	g->setTextColor(color);
	g->setCursor(x, baselineY);
	g->print(l1);
}

int16_t uiUnitWidth(Adafruit_GFX* g, const char* unit) {
	if (!unit) return 0;
	char l1[12];
	uiUnitToLatin1(unit, l1, sizeof(l1));
	g->setFont(&simutFont9pt);
	g->setTextSize(1);
	int16_t bx, by; uint16_t bw, bh;
	g->getTextBounds(l1, 0, 0, &bx, &by, &bw, &bh);
	return (int16_t)bw;
}

void uiMenuIcon(Adafruit_GFX* g, int16_t x, int16_t y, uint8_t id,
	uint16_t color) {
	switch (id) {
	case 0: /* palette: disc + 3 paint wells */
		g->fillCircle(x + 8, y + 8, 8, color);
		g->fillCircle(x + 12, y + 12, 5, C_CARD_BG); /* thumb notch */
		g->fillCircle(x + 5, y + 5, 2, C_CARD_BG);
		g->fillCircle(x + 11, y + 4, 2, C_CARD_BG);
		g->fillCircle(x + 4, y + 11, 2, C_CARD_BG);
		break;
	case 1: /* bell */
		g->fillTriangle(x + 8, y, x + 2, y + 11, x + 14, y + 11, color);
		g->fillRoundRect(x + 2, y + 8, 13, 4, 2, color);
		g->fillCircle(x + 8, y + 14, 2, color);
		break;
	case 2: /* note */
		g->fillCircle(x + 5, y + 13, 3, color);
		g->fillRect(x + 7, y + 2, 2, 11, color);
		g->fillRect(x + 7, y + 2, 8, 3, color);
		g->fillRect(x + 13, y + 2, 2, 6, color);
		break;
	case 3: /* globe */
		g->drawCircle(x + 8, y + 8, 7, color);
		g->drawCircle(x + 8, y + 8, 6, color);
		g->drawFastHLine(x + 2, y + 8, 13, color);
		g->drawFastVLine(x + 8, y + 1, 14, color);
		g->drawCircle(x + 8, y + 8, 3, color);
		break;
	case 4: /* lock */
		g->drawRoundRect(x + 4, y + 1, 9, 8, 4, color);
		g->fillRoundRect(x + 2, y + 7, 13, 8, 2, color);
		g->fillRect(x + 7, y + 9, 3, 4, C_CARD_BG);
		break;
	case 5: /* crosshair */
		g->drawCircle(x + 8, y + 8, 6, color);
		g->drawFastHLine(x + 1, y + 8, 15, color);
		g->drawFastVLine(x + 8, y + 1, 15, color);
		break;
	case 6: /* document */
		g->drawRoundRect(x + 3, y, 11, 15, 2, color);
		g->drawFastHLine(x + 5, y + 4, 7, color);
		g->drawFastHLine(x + 5, y + 7, 7, color);
		g->drawFastHLine(x + 5, y + 10, 5, color);
		break;
	case 7: /* pulse */
		g->drawFastHLine(x, y + 8, 4, color);
		g->drawLine(x + 4, y + 8, x + 6, y + 2, color);
		g->drawLine(x + 6, y + 2, x + 9, y + 13, color);
		g->drawLine(x + 9, y + 13, x + 11, y + 8, color);
		g->drawFastHLine(x + 11, y + 8, 5, color);
		g->drawFastHLine(x, y + 9, 4, color);
		g->drawLine(x + 4, y + 9, x + 6, y + 3, color);
		g->drawLine(x + 6, y + 3, x + 9, y + 14, color);
		g->drawLine(x + 9, y + 14, x + 11, y + 9, color);
		g->drawFastHLine(x + 11, y + 9, 5, color);
		break;
	default: /* move: 4 outward triangles */
		g->fillTriangle(x + 8, y, x + 5, y + 4, x + 11, y + 4, color);
		g->fillTriangle(x + 8, y + 15, x + 5, y + 11, x + 11, y + 11, color);
		g->fillTriangle(x, y + 8, x + 4, y + 5, x + 4, y + 11, color);
		g->fillTriangle(x + 15, y + 8, x + 11, y + 5, x + 11, y + 11, color);
		break;
	}
}

#endif /* SIMUT_DISPLAY_TFT */
