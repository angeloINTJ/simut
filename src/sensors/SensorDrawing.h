/**
 * @file SensorDrawing.h
 * @brief Shared procedural icons for sensor panel rendering.
 *
 * These are plain inline drawing primitives. Each icon used to carry a
 * per-sensor compile guard (#if SIMUT_SENSOR_*) so a disabled sensor's icon
 * left the flash — redundant, because an inline helper that no enabled driver
 * calls is dropped by the linker (--gc-sections) regardless, which is how the
 * ungated °C helpers below have always worked. Removing the seven guards took
 * SensorDrawing.h out of the sensor feature macros entirely, with no change to
 * a build that compiles every family (feature model, P2, sensor seam).
 *
 * All functions draw into a GFXcanvas16. The caller is responsible for
 * blitting the canvas to the TFT at the correct position.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once
#if SIMUT_DISPLAY_TFT
#include <Adafruit_GFX.h>
#include "../Themes.h" /* C_ALARM_* state colors + themeTint for glints */
#endif
#ifndef RGB565
#define RGB565(r,g,b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#endif

#if SIMUT_DISPLAY_TFT
/* ── Thermometer (large, 18×28 px) ──────────────────────────────────────── */

inline void drawThermometerLarge(GFXcanvas16* cv, int16_t x, int16_t y,
                                 uint16_t outline, uint16_t bg, uint16_t mercury) {
    cv->fillCircle(x + 5, y + 26, 7, outline);
    cv->fillRoundRect(x + 1, y, 8, 24, 4, outline);
    cv->fillRoundRect(x + 3, y + 2, 4, 20, 2, bg);
    cv->fillCircle(x + 5, y + 26, 5, bg);
    cv->fillRect(x + 4, y + 10, 2, 14, mercury);
    cv->fillCircle(x + 5, y + 26, 4, mercury);
    cv->fillCircle(x + 5, y + 2, 2, outline);
}

/* ── Thermometer (mini, ~12×20 px) ──────────────────────────────────────── */

inline void drawThermometerMini(GFXcanvas16* cv, int16_t x, int16_t y,
                                uint16_t outline, uint16_t bg, uint16_t mercury) {
    cv->fillCircle(x + 4, y + 15, 5, outline);
    cv->fillRoundRect(x + 1, y, 7, 14, 3, outline);
    cv->fillRoundRect(x + 2, y + 1, 5, 12, 2, bg);
    cv->fillCircle(x + 4, y + 15, 4, bg);
    cv->fillRect(x + 3, y + 8, 3, 6, mercury);
    cv->fillCircle(x + 4, y + 15, 3, mercury);
    cv->fillCircle(x + 4, y + 2, 2, outline);
}

/* ── Drop / water droplet (large, ~20×22 px) ────────────────────────────── */

inline void drawDropLarge(GFXcanvas16* cv, int16_t x, int16_t y,
                          uint16_t color, uint16_t shine) {
    cv->fillCircle(x + 6, y + 20, 8, color);
    cv->fillTriangle(x + 6, y, x - 1, y + 18, x + 13, y + 18, color);
    cv->fillCircle(x + 4, y + 17, 3, shine);
    cv->fillCircle(x + 3, y + 14, 1, shine);
}

/* ── Drop / water droplet (mini, ~12×14 px) ─────────────────────────────── */

inline void drawDropMini(GFXcanvas16* cv, int16_t x, int16_t y,
                         uint16_t color, uint16_t shine) {
    cv->fillCircle(x + 5, y + 7, 6, color);
    cv->fillTriangle(x + 5, y - 5, x, y + 5, x + 10, y + 5, color);
    cv->fillCircle(x + 3, y + 5, 2, shine);
    cv->drawPixel(x + 3, y + 2, shine);
}

/* ── Barometer / pressure gauge (large, ~20×22 px) ──────────────────────── */

inline void drawBarometerLarge(GFXcanvas16* cv, int16_t x, int16_t y,
                                uint16_t outline, uint16_t bg, uint16_t needle) {
    /* Outer ring */
    cv->fillCircle(x + 7, y + 13, 8, outline);
    cv->fillCircle(x + 7, y + 13, 6, bg);
    /* Inner disc */
    cv->fillCircle(x + 7, y + 13, 3, outline);
    /* Needle — diagonal line from center to upper-right */
    cv->drawLine(x + 7, y + 13, x + 12, y + 6, needle);
    cv->drawLine(x + 7, y + 13, x + 11, y + 7, needle);
    /* Top mounting point */
    cv->fillCircle(x + 7, y, 2, outline);
}

/* ── Barometer / pressure gauge (mini, ~12×14 px) ───────────────────────── */

inline void drawBarometerMini(GFXcanvas16* cv, int16_t x, int16_t y,
                               uint16_t outline, uint16_t bg, uint16_t needle) {
    cv->fillCircle(x + 5, y + 6, 6, outline);
    cv->fillCircle(x + 5, y + 6, 5, bg);
    cv->fillCircle(x + 5, y + 6, 2, outline);
    cv->drawLine(x + 5, y + 6, x + 9, y + 2, needle);
    cv->drawLine(x + 5, y + 6, x + 8, y + 2, needle);
    cv->fillCircle(x + 5, y, 2, outline);
}

/* ── "°C" unit ──────────────────────────────────────────────────────────────
 * The Latin-1 fonts (FreeSansBold*8b_latin1.h) carry a REAL degree glyph
 * at 0xB0, so the unit is simply typeset — this replaced first a printed
 * "o" and then a hand-drawn ring. "\xB0" below is that Latin-1 byte. */

/* ── "°C" unit (normal mode, 24pt value) ────────────────────────────────── */

inline void drawUnitDegC_Normal(GFXcanvas16* cv, int16_t x,
                                uint16_t color, const GFXfont& font9,
                                const GFXfont& font12) {
    (void)font9;
    cv->setTextColor(color);
    cv->setFont(&font12);
    cv->setCursor(x + 2, 35);
    cv->print("\xB0" "C");
}

/* ── "°C" unit (min/max mode, 9pt value) ────────────────────────────────── */

inline void drawUnitDegC_Mini(GFXcanvas16* cv, int16_t x, int16_t baseY,
                              uint16_t color, const GFXfont& font9) {
    cv->setTextColor(color);
    cv->setFont(&font9);
    cv->setCursor(x + 1, baseY + 15);
    cv->print("\xB0" "C");
}

/* ── Min/Max panel helpers ─────────────────────────────────────────────── */


/** Renders temperature right-aligned at dotX (1 decimal).
 *  Returns x after the value for °C unit placement. */
inline int16_t drawTempValue1D(GFXcanvas16* cv, int16_t dotX, int16_t y,
                                float temp, uint16_t color, const GFXfont& font) {
    cv->setFont(&font); cv->setTextSize(1);
    cv->setTextColor(color);
    if (isnan(temp)) {
        int16_t x1, y1; uint16_t dw, dh;
        cv->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &dh);
        cv->setCursor(dotX - (int)dw + 10, y);
        cv->print("--.-");
        return dotX + 10 + 3;
    }
    int intPart = (int)temp;
    int decPart = abs((int)(temp * 10.0f) % 10);
    char iP[8], dP[4];
    snprintf(iP, sizeof(iP), "%d", intPart);
    snprintf(dP, sizeof(dP), ".%d", decPart);
    int16_t x1, y1; uint16_t iPw, dh;
    cv->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &dh);
    cv->setCursor(dotX - (int)iPw, y); cv->print(iP);
    cv->setCursor(dotX, y); cv->print(dP);
    uint16_t dpW;
    cv->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &dh);
    return dotX + (int)dpW + 3;
}

/** Renders one min/max row: label + mini thermometer + temp + °C. */
inline void drawMinMaxTempRow(GFXcanvas16* cv,
    const char* label, int16_t labelX, int16_t thermX, int16_t dotX,
    int16_t baseY, float value, bool isRedPhase,
    uint16_t txtSub, uint16_t icCol, uint16_t mercCol,
    uint16_t tempOk, uint16_t panelBg, uint16_t alarmText,
    const GFXfont& font9) {
    cv->setFont(&font9); cv->setTextSize(1);
    cv->setTextColor(txtSub);
    cv->setCursor(labelX, baseY + 15);
    cv->print(label);
    drawThermometerMini(cv, thermX, baseY, icCol, panelBg, mercCol);
    uint16_t tCol = isRedPhase ? alarmText : tempOk;
    int16_t endX = drawTempValue1D(cv, dotX, baseY + 15, value, tCol, font9);
    drawUnitDegC_Mini(cv, endX, baseY, txtSub, font9);
}

/* Geometry of the min/max graph button, in CARD coordinates.
 *
 * It lived three times, once in each driver that draws a min/max panel, and a
 * fourth number — the touch zone that opens the graph — was written by hand in
 * DisplayManager_Touch.cpp and did not agree with any of them. That is what
 * made the right third of the button toggle the panel mode and the left third
 * turn min/max off: two hot zones, neither matching what is painted. The
 * drawing and the touch now read the same three values.
 *
 * The content of every min/max panel ends at ANCHOR; the button is centred in
 * what is left of the card. */
constexpr int16_t MINMAX_BTN_ANCHOR = 230;
constexpr int16_t MINMAX_BTN_W      = 58;
constexpr int16_t MINMAX_BTN_Y      = 2;
constexpr int16_t MINMAX_BTN_H      = 40;

constexpr int16_t minMaxBtnX(int16_t cardW) {
	return MINMAX_BTN_ANCHOR +
	       ((int16_t)(cardW - 1) - MINMAX_BTN_ANCHOR - MINMAX_BTN_W) / 2;
}

/** Renders the graph history button icon. */
inline void drawMinMaxGraphBtn(GFXcanvas16* cv, int16_t x, int16_t y,
                                int16_t w, int16_t h, uint16_t fill, uint16_t fg) {
    cv->fillRoundRect(x, y, w, h, 12, fill);
    int cx = x + w / 2;
    int cy = y + h / 2;
    cv->fillRoundRect(cx - 11, cy, 4, 8, 1, fg);
    cv->fillRoundRect(cx - 5, cy - 6, 4, 14, 1, fg);
    cv->fillRoundRect(cx + 1, cy - 3, 4, 11, 1, fg);
    cv->drawFastHLine(cx - 12, cy + 9, 19, fg);
    cv->fillTriangle(cx + 10, cy - 5, cx + 10, cy + 5, cx + 17, cy, fg);
}

#endif // SIMUT_DISPLAY_TFT

