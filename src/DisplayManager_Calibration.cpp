/**
 * @file DisplayManager_Calibration.cpp
 * @brief Touch calibration, sensitivity, display offset screens.
 * @details Sub-file of DisplayManager.cpp.
 * Includes: 4-point touch calibration, sensitivity adjustment
 * (threshold), physical display offset (±4 px), and load/save
 * of this persistent config to SystemConfig::reserved[].
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include <math.h>

/* POD volatile globals + writes in show*(). Volatile forces writes to persist. */
namespace {
 volatile bool g_sensFirstDraw = true;
 volatile bool g_lastSensDone = false;
 volatile float g_lastSensStability = -1.0f;
 volatile int g_lastSensThreshold = -1;
 volatile int g_lastCalPointIdx = -1;
 volatile bool g_lastCalHoldReady = false;
 volatile int g_lastCalStep = -1;
}

void DisplayManager::showTouchCalibration( ) {
 /*
 * Integrated flow: sensitivity first, then position.
 * 1. MODE_SETTINGS_TOUCH_SENS — taps to calibrate pressure threshold
 * 2. MODE_SETTINGS_TOUCH_CAL — crosshairs to calibrate position
 * Transition 1→2 is automatic after sensitivity completion.
 */
 _sensCount = 0;
 _sensStability = 0.0f;
 _sensThreshold = 0;
 _sensDone = false;
 _sensDoneTime = 0;
 _calStep = 0;
 _calPhase = 0;
 memset(_calRawX, 0, sizeof(_calRawX));
 memset(_calRawY, 0, sizeof(_calRawY));

 /* Reset globals */
 g_sensFirstDraw = true;
 g_lastSensDone = false;
 g_lastSensStability = -1.0f;
 g_lastSensThreshold = -1;
 g_lastCalPointIdx = -1;
 g_lastCalHoldReady = false;
 g_lastCalStep = -1;

 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_TOUCH_SENS;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}


void DisplayManager::loadTouchCalibration(const TouchCalData* cal) {
 if (!cal || cal->magic != 0xCA) {
 _calValid = false;
 return;
 }
 _calSwapXY = (cal->flags & 0x01) != 0;
 _calXMin = cal->xMin;
 _calXMax = cal->xMax;
 _calYMin = cal->yMin;
 _calYMax = cal->yMax;
 _calValid = true;

 /* Sensitivity threshold: uses saved value, fallback 400 if zero */
 _sensZThreshold = (cal->zThreshold > 0) ? cal->zThreshold : 400;
}


void DisplayManager::fillCalData(TouchCalData* cal) const {
 if (!cal) return;
 cal->magic = 0xCA;
 cal->flags = _calSwapXY ? 0x01 : 0x00;
 cal->xMin = _calXMin;
 cal->xMax = _calXMax;
 cal->yMin = _calYMin;
 cal->yMax = _calYMax;
 cal->zThreshold = _sensZThreshold;
}


/* =========================================================================== */
/* DISPLAY OFFSET ADJUSTMENT SCREEN */
/* =========================================================================== */

/**
 * @brief Enters the display offset adjustment screen; snapshots saved state for BACK.
 *
 * The "preview" state is initialized with the offset currently applied to the TFT
 * (which corresponds to the persisted value, loaded via loadDisplayOffset() at
 * boot). Any change via arrows is applied live to _driver.tft, and BACK restores
 * the snapshot if the user cancels.
 */
void DisplayManager::showSettingsDisplayOffset( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_DISPLAY_OFFSET;
 _offsetSavedX = _driver.tft ? _driver.tft->getOffsetX( ) : 0;
 _offsetSavedY = _driver.tft ? _driver.tft->getOffsetY( ) : 0;
 _offsetPreviewX = _offsetSavedX;
 _offsetPreviewY = _offsetSavedY;
 _lastOffsetDrawX = 99; /* sentinel forces redraw of numeric values */
 _lastOffsetDrawY = 99;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}


/**
 * @brief Renders the display offset adjustment screen following the pattern of other
 * settings screens (top bar with title + bottom button bar).
 *
 * Layout:
 * [TITLE] (0..320, 0..32)
 * Direction pad centered at (160,120):
 * ▲ (130..190, 55..95)
 * ◀ (80..140, 100..140) ● (148..172, 108..132) ▶ (180..240, 100..140)
 * ▼ (130..190, 145..185)
 * Numeric indicator (190..310, 60..180) — "X:+2 Y:-1"
 * Footer: [BACK] [APPLY] (y >= 200)
 */
void DisplayManager::drawSettingsDisplayOffset( ) {
 if (!_driver.tft) return;
 bool full = _forceSettingsRedraw;
 int16_t bx, by; uint16_t bw, bh;

 if (full) {
 /* Full redraw via strips. Static layout:
 * - title bar y=4..36
 * - direction pad (4 arrow buttons) at y=55..185
 * - reset button center at y=108..132
 * - back/apply buttons at y=204..236 */
 const String titleTxt = tr(TR_DISPLAY_OFFSET_TITLE);
 const char* backTxt = tr(TR_BACK);
 const String applyTxt = tr(TR_APPLY);
 const int cx = 160, cy = 120;
 GFXcanvas16* cv = beginScreenRender( );
 if (cv) {
 for (int strip = 0; strip < 6; strip++) {
 cv->fillScreen(C_BG_MAIN);
 const int16_t yOff = -strip * RENDER_STRIP_H;

 /* Title bar */
 cv->fillRect(4, 4 + yOff, 312, 32, C_CARD_BG);
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TEXT_MAIN);
 cv->setCursor(10, 22 + yOff);
 cv->print(titleTxt);

 /* Direction pad — 4 capsules with arrows */
 cv->fillRoundRect(130, 55 + yOff, 60, 40, 8, C_CARD_BG); /* UP */
 cv->fillTriangle(cx, 62 + yOff, cx - 10, 86 + yOff, cx + 10, 86 + yOff, C_TEXT_MAIN);
 cv->fillRoundRect(130, 145 + yOff, 60, 40, 8, C_CARD_BG); /* DOWN */
 cv->fillTriangle(cx - 10, 154 + yOff, cx + 10, 154 + yOff, cx, 178 + yOff, C_TEXT_MAIN);
 cv->fillRoundRect(80, 100 + yOff, 60, 40, 8, C_CARD_BG); /* LEFT */
 cv->fillTriangle(90, cy + yOff, 120, cy - 10 + yOff, 120, cy + 10 + yOff, C_TEXT_MAIN);
 cv->fillRoundRect(180, 100 + yOff, 60, 40, 8, C_CARD_BG); /* RIGHT */
 cv->fillTriangle(230, cy + yOff, 200, cy - 10 + yOff, 200, cy + 10 + yOff, C_TEXT_MAIN);

 /* Reset center */
 cv->fillRoundRect(148, 108 + yOff, 24, 24, 4, C_ACCENT);
 cv->drawCircle(cx, cy + yOff, 4, C_BG_MAIN);

 /* Back button */
 cv->fillRoundRect(10, 204 + yOff, 120, 32, 8, C_CARD_BG);
 cv->setTextColor(C_TEXT_MAIN);
 cv->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(10 + (120 - (int)bw) / 2, 226 + yOff);
 cv->print(backTxt);

 /* Apply button */
 cv->fillRoundRect(190, 204 + yOff, 120, 32, 8, C_ACCENT);
 cv->setTextColor(C_BG_MAIN);
 cv->getTextBounds(applyTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(190 + (120 - (int)bw) / 2, 226 + yOff);
 cv->print(applyTxt);

 /* Bright green frame on safe area (4..315, 4..235)
 * — exactly the offset range (-4..+4) that loadDisplayOffset
 * accepts. The frame moves with the entire UI when offset is
 * applied: user adjusts until it coincides with the physical
 * enclosure opening. Drawn last to be visible on top of title
 * bar and edge buttons. */
 cv->drawRect(4, 4 + yOff, 312, 232, 0x07E0); /* BRIGHT_GREEN */

 commitScreenStrip(strip);
 }
 endScreenRender( );
 }
 _forceSettingsRedraw = false;
 _lastOffsetDrawX = 99; /* forces numeric redraw below */
 }

 /* Numeric offset values — repainted only when they change. */
 if (_offsetPreviewX != _lastOffsetDrawX || _offsetPreviewY != _lastOffsetDrawY) {
 _driver.tft->fillRect(245, 60, 70, 90, C_BG_MAIN);
 _driver.tft->setFont(&simutFont12pt);
 _driver.tft->setTextColor(C_TEXT_MAIN);
 char buf[16];
 snprintf(buf, sizeof(buf), "X %c%d",
 _offsetPreviewX >= 0 ? '+' : '-',
 abs((int)_offsetPreviewX));
 _driver.tft->setCursor(250, 90);
 _driver.tft->print(buf);
 snprintf(buf, sizeof(buf), "Y %c%d",
 _offsetPreviewY >= 0 ? '+' : '-',
 abs((int)_offsetPreviewY));
 _driver.tft->setCursor(250, 130);
 _driver.tft->print(buf);
 _lastOffsetDrawX = _offsetPreviewX;
 _lastOffsetDrawY = _offsetPreviewY;
 }
}


/* =========================================================================== */
/* DISPLAY OFFSET PERSISTENCE (public API) */
/* =========================================================================== */

/**
 * @brief Loads the display offset from the persistent config block and applies
 * it immediately to the TFT. Called at boot by AppManager.
 */
/* Called from Core 0 during boot, with Core 1 already rendering — so it sets
 * the offset WITHOUT painting the margins. Two cores writing the same SPI bus
 * is not a race worth taking to blacken 4 pixel columns that the next frame
 * covers on its own. */
void DisplayManager::loadDisplayOffset(const DisplayOffsetData* data) {
 if (!data || data->magic != 0xD0) {
 if (_driver.tft) _driver.tft->setDisplayOffset(0, 0, false);
 _offsetSavedX = 0;
 _offsetSavedY = 0;
 return;
 }
 int8_t ox = constrain((int)data->offsetX, -4, 4);
 int8_t oy = constrain((int)data->offsetY, -4, 4);
 if (_driver.tft) _driver.tft->setDisplayOffset(ox, oy, false);
 _offsetSavedX = ox;
 _offsetSavedY = oy;
}


/**
 * @brief Fills the struct with the currently applied offset, for persistence.
 */
void DisplayManager::fillDisplayOffsetData(DisplayOffsetData* data) const {
 if (!data) return;
 data->magic = 0xD0;
 data->offsetX = _driver.tft ? _driver.tft->getOffsetX( ) : 0;
 data->offsetY = _driver.tft ? _driver.tft->getOffsetY( ) : 0;
 data->reserved = 0;
}


int8_t DisplayManager::getDisplayOffsetX( ) const {
 return _driver.tft ? _driver.tft->getOffsetX( ) : 0;
}

int8_t DisplayManager::getDisplayOffsetY( ) const {
 return _driver.tft ? _driver.tft->getOffsetY( ) : 0;
}


/**
 * @brief Resets the touch calibration to factory default values.
 *
 * Restores generic raw limits (200..3800) and invalidates the flag.
 * The next interaction will use estimated mapping until new calibration.
 */
void DisplayManager::resetTouchCalibration( ) {
 _calValid = false;
 _calSwapXY = false;
 _calXMin = 200;
 _calXMax = 3800;
 _calYMin = 200;
 _calYMax = 3800;
 _sensZThreshold = 400;
}


/* =========================================================================== */
/* TOUCH SENSITIVITY CALIBRATION */
/* =========================================================================== */

/**
 * @brief Starts the touch sensitivity calibration screen.
 * Resets counters and enters sample collection mode.
 */
void DisplayManager::showTouchSensitivity( ) {
 _sensCount = 0;
 _sensStability = 0.0f;
 _sensThreshold = 0;
 _sensDone = false;
 _sensDoneTime = 0;
 _uiMode = MODE_SETTINGS_TOUCH_SENS;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
}

/**
 * @brief Draws the touch sensitivity calibration screen.
 *
 * Layout:
 * - Title in top bar
 * - Crosshair target in center
 * - Progress text "Tap N/20"
 * - Vertical bar on the right showing stability (0..100%)
 * - Numeric threshold value
 */
void DisplayManager::drawTouchSensitivity( ) {
 bool fullRedraw = _forceSettingsRedraw;
 _forceSettingsRedraw = false;

 if (fullRedraw) {
 /* Full redraw via strips. Static layout:
 * - title bar y=4..36 (text at y=22)
 * - cancel button y=195..235 (text at y=220)
 * - bar frame at (289, 38..192). */
 const String titleTxt = tr(TR_SENS_TITLE);
 const char* backTxt = tr(TR_CANCEL);
 GFXcanvas16* cv = beginScreenRender( );
 if (cv) {
 int16_t bx, by; uint16_t bw, bh;
 for (int strip = 0; strip < 6; strip++) {
 cv->fillScreen(C_BG_MAIN);
 const int16_t yOff = -strip * RENDER_STRIP_H;

 /* Title bar */
 cv->fillRect(4, 4 + yOff, 312, 32, C_CARD_BG);
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TEXT_MAIN);
 cv->setCursor(10, 22 + yOff);
 cv->print(titleTxt);

 /* Cancel button */
 cv->fillRoundRect(5, 195 + yOff, 120, 40, 8, C_CARD_BG);
 cv->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(5 + (120 - bw) / 2, 220 + yOff);
 cv->print(backTxt);

 /* Bar frame (vertical, right side) */
 cv->drawRect(289, 38 + yOff, 26, 154, C_TEXT_OFF);

 commitScreenStrip(strip);
 }
 endScreenRender( );
 }
 }

 /* Crosshair only repaints on 1st draw or when _sensDone changes */
 int cx = 140, cy = 115;
 if (g_sensFirstDraw || g_lastSensDone != _sensDone) {
 uint16_t crossColor = _sensDone ? C_TEMP_OK : C_ACCENT;
 _driver.tft->fillRect(cx - 30, cy - 1, 60, 3, C_BG_MAIN);
 _driver.tft->fillRect(cx - 1, cy - 30, 3, 60, C_BG_MAIN);
 _driver.tft->drawLine(cx - 15, cy, cx + 15, cy, crossColor);
 _driver.tft->drawLine(cx, cy - 15, cx, cy + 15, crossColor);
 _driver.tft->drawCircle(cx, cy, 12, crossColor);
 }

 /* Progress text only when _sensDone changes or on 1st draw */
 if (g_sensFirstDraw || g_lastSensDone != _sensDone) {
 _driver.tft->fillRect(80, 150, 140, 30, C_BG_MAIN);
 _driver.tft->setFont(&simutFont9pt);
 _driver.tft->setTextColor(C_TEXT_MAIN);
 int16_t bx2, by2; uint16_t bw2, bh2;
 String txt = _sensDone ? tr(TR_SENS_DONE) : tr(TR_CAL_TOUCH_POINT);
 _driver.tft->getTextBounds(txt, 0, 0, &bx2, &by2, &bw2, &bh2);
 _driver.tft->setCursor(140 - bw2 / 2, 168);
 _driver.tft->print(txt);
 }

 /* Bar only when _sensStability changes */
 if (g_sensFirstDraw || fabsf(g_lastSensStability - _sensStability) > 0.01f) {
 int barX = 290, barY = 39, barW = 24, barH = 152;
 int fillH = (int)(barH * _sensStability);
 if (fillH > barH) fillH = barH;
 if (fillH < barH) _driver.tft->fillRect(barX, barY, barW, barH - fillH, C_BG_MAIN);
 uint16_t barColor = (_sensStability >= 0.85f) ? C_TEMP_OK : C_ACCENT;
 if (fillH > 0) _driver.tft->fillRect(barX, barY + barH - fillH, barW, fillH, barColor);
 g_lastSensStability = _sensStability;
 }

 /* Numeric value only when it changes */
 if (g_sensFirstDraw || g_lastSensThreshold != (int)_sensThreshold) {
 _driver.tft->fillRect(280, 195, 40, 20, C_BG_MAIN);
 _driver.tft->setFont(NULL); _driver.tft->setTextSize(1);
 _driver.tft->setTextColor(C_TEXT_OFF);
 char valBuf[8];
 snprintf(valBuf, sizeof(valBuf), "%d", _sensThreshold);
 _driver.tft->setCursor(295, 198);
 _driver.tft->print(valBuf);
 g_lastSensThreshold = (int)_sensThreshold;
 }

 g_lastSensDone = _sensDone;
 g_sensFirstDraw = false;
}


void DisplayManager::mapTouchPoint(TS_Point raw, int16_t &outX, int16_t &outY) {
 /* Bypass mapping if simulated touch is active.
 * CLI 'touch sim X Y' sets _simTouchActive + _simTouchX/Y; here
 * we return coords already in screen-space, ignoring raw ADC.
 * Auto-clear after 100ms (1-2 frames @ ~30fps) — simulates a tap. */
 if (__atomic_load_n(&_simTouchActive, __ATOMIC_ACQUIRE)) {
 outX = __atomic_load_n(&_simTouchX, __ATOMIC_ACQUIRE);
 outY = __atomic_load_n(&_simTouchY, __ATOMIC_ACQUIRE);
 if (millis( ) - __atomic_load_n(&_simTouchSetMs, __ATOMIC_ACQUIRE) > 100) {
 __atomic_store_n(&_simTouchActive, false, __ATOMIC_RELEASE);
 }
 return;
 }

 int16_t rawForX, rawForY;

 if (_calSwapXY) {
 rawForX = raw.y;
 rawForY = raw.x;
 } else {
 rawForX = raw.x;
 rawForY = raw.y;
 }

 outX = (int16_t)constrain(map(rawForX, _calXMin, _calXMax, 0, 320), 0, 319);
 outY = (int16_t)constrain(map(rawForY, _calYMin, _calYMax, 0, 240), 0, 239);
}


void DisplayManager::drawCrosshair(int16_t cx, int16_t cy, uint16_t color) {
 const int16_t sz = 10;
 _driver.tft->drawLine(cx - sz, cy, cx + sz, cy, color);
 _driver.tft->drawLine(cx, cy - sz, cx, cy + sz, color);
 _driver.tft->drawCircle(cx, cy, sz - 2, color);
}

/* Canvas-aware version of drawCrosshair for strip-based rendering.
 * Used by full-screen redraw blocks of the 4 calibration screens. */
static inline void drawCrosshairOnCanvas(GFXcanvas16* cv, int16_t cx, int16_t cy, uint16_t color) {
 const int16_t sz = 10;
 cv->drawLine(cx - sz, cy, cx + sz, cy, color);
 cv->drawLine(cx, cy - sz, cx, cy + sz, color);
 cv->drawCircle(cx, cy, sz - 2, color);
}


/* drawCalibrationMessage via strips.
 * Layout: success/fail icon (y=70..110) + message (y=130) + button (y=185..225). */
void DisplayManager::drawCalibrationMessage( ) {
 if (!_driver.tft) return;

 bool isSuccess = (_calPhase == 2);
 uint16_t iconColor = isSuccess ? C_TEMP_OK : C_TEMP_WARM;
 const char* msg = isSuccess ? tr(TR_CAL_DONE) : tr(TR_CAL_REJECTED);
 const char* btnLabel = tr(TR_UNDERSTOOD);

 GFXcanvas16* cv = beginScreenRender( );
 if (!cv) return;

 int16_t x1, y1; uint16_t w, h_bound;

 for (int strip = 0; strip < 6; strip++) {
 cv->fillScreen(C_BG_MAIN);
 const int16_t yOff = -strip * RENDER_STRIP_H;

 /* Icon (y_screen=70..110) */
 if (isSuccess) {
 cv->drawLine(130, 90 + yOff, 150, 110 + yOff, iconColor);
 cv->drawLine(131, 90 + yOff, 151, 110 + yOff, iconColor);
 cv->drawLine(150, 110 + yOff, 190, 70 + yOff, iconColor);
 cv->drawLine(151, 110 + yOff, 191, 70 + yOff, iconColor);
 } else {
 cv->drawLine(145, 70 + yOff, 175, 100 + yOff, iconColor);
 cv->drawLine(146, 70 + yOff, 176, 100 + yOff, iconColor);
 cv->drawLine(175, 70 + yOff, 145, 100 + yOff, iconColor);
 cv->drawLine(176, 70 + yOff, 146, 100 + yOff, iconColor);
 }

 /* Message at y_screen=130 */
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TEXT_MAIN);
 cv->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h_bound);
 cv->setCursor((320 - w) / 2, 130 + yOff);
 cv->print(msg);

 /* Button at y_screen=185..225 */
 cv->fillRoundRect(60, 185 + yOff, 200, 40, 12, C_ACCENT);
 cv->setFont(&simutFont12pt);
 cv->setTextColor(C_BG_MAIN);
 cv->getTextBounds(btnLabel, 0, 0, &x1, &y1, &w, &h_bound);
 cv->setCursor(160 - (w / 2), 212 + yOff);
 cv->print(btnLabel);

 commitScreenStrip(strip);
 }
 endScreenRender( );
}


/* drawTouchCalibration. Full-screen redraw via strips (renders the
 * ENTIRE screen: current crosshair + text labels). Incremental
 * updates (between taps of the 8 points) go directly to _driver.tft — small
 * areas, imperceptible top-down effect. */
void DisplayManager::drawTouchCalibration( ) {
 bool fullRedraw = _forceSettingsRedraw;

 if (_calPhase >= 1) {
 drawCalibrationMessage( );
 _forceSettingsRedraw = false;
 return;
 }

 int pointIdx = _calStep % 4;
 int cycleNum = (_calStep < 4) ? 1 : 2;

 if (_calStep >= 8) {
 _forceSettingsRedraw = false;
 return;
 }

 /* Text buffers (used in both paths). */
 const char* title = tr(TR_CAL_TITLE);
 char msg[48];
 snprintf(msg, sizeof(msg), "%s (%d/4)", tr(TR_CAL_TOUCH_POINT), pointIdx + 1);
 char cycleBuf[24];
 snprintf(cycleBuf, sizeof(cycleBuf), "[ %d / 2 ]", cycleNum);

 if (fullRedraw) {
 /* Full-screen render via strips: current crosshair + 3 lines of text. */
 const int16_t cx = CAL_SCR_X[pointIdx];
 const int16_t cy = CAL_SCR_Y[pointIdx];
 const uint16_t crossColor = _calHoldReady ? C_TEMP_OK : C_ACCENT;

 GFXcanvas16* cv = beginScreenRender( );
 if (cv) {
 int16_t bx, by; uint16_t bw, bh;
 for (int strip = 0; strip < 6; strip++) {
 cv->fillScreen(C_BG_MAIN);
 const int16_t yOff = -strip * RENDER_STRIP_H;

 /* Crosshair at (cx, cy) — visible only on strip containing cy */
 drawCrosshairOnCanvas(cv, cx, cy + yOff, crossColor);

 /* Title at y=100 (font 9pt accent) */
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_ACCENT);
 cv->getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 100 + yOff);
 cv->print(title);

 /* Msg at y=122 */
 cv->setTextColor(C_TEXT_MAIN);
 cv->getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 122 + yOff);
 cv->print(msg);

 /* Cycle at y=140 (default font, size 1) */
 cv->setFont(NULL); cv->setTextSize(1);
 cv->setTextColor(C_TEXT_OFF);
 cv->getTextBounds(cycleBuf, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 140 + yOff);
 cv->print(cycleBuf);

 commitScreenStrip(strip);
 }
 endScreenRender( );
 }
 _forceSettingsRedraw = false;
 return;
 }

 /* Crosshair only repaints when pointIdx or _calHoldReady change. */
 bool crosshairChanged = (g_lastCalPointIdx != pointIdx) || (g_lastCalHoldReady != _calHoldReady);
 if (crosshairChanged) {
 if (pointIdx > 0) {
 drawCrosshair(CAL_SCR_X[pointIdx - 1], CAL_SCR_Y[pointIdx - 1], C_BG_MAIN);
 }
 if (_calStep == 4) {
 drawCrosshair(CAL_SCR_X[3], CAL_SCR_Y[3], C_BG_MAIN);
 }
 drawCrosshair(CAL_SCR_X[pointIdx], CAL_SCR_Y[pointIdx],
 _calHoldReady ? C_TEMP_OK : C_ACCENT);
 g_lastCalPointIdx = pointIdx;
 g_lastCalHoldReady = _calHoldReady;
 }

 /* Text via atomic canvas — no direct fillRect on _driver.tft (which
 * caused visible black flash before text reappeared).
 * Layout covers y=85..150 (65px). Since _driver.canvas is 320x45, split
 * into 2 strips: strip 0 (y=85..130) covers title+msg, strip 1 (y=130..150)
 * covers cycle. Each blit is atomic (1 SPI burst). */
 if (g_lastCalStep != _calStep && _driver.canvas) {
 GFXcanvas16* cv = _driver.canvas;
 int16_t bx, by; uint16_t bw, bh;

 /* Strip 0: y_screen 85..130 (45px). Title (y=100) + msg (y=122). */
 cv->fillScreen(C_BG_MAIN);
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_ACCENT);
 cv->getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 100 - 85); cv->print(title);
 cv->setTextColor(C_TEXT_MAIN);
 cv->getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 122 - 85); cv->print(msg);
 blitCanvas(cv, 0, 85, 320, 45);

 /* Strip 1: y_screen 130..150 (20px). Cycle (y=140 -> canvas y=10). */
 cv->fillScreen(C_BG_MAIN);
 cv->setFont(NULL); cv->setTextSize(1);
 cv->setTextColor(C_TEXT_OFF);
 cv->getTextBounds(cycleBuf, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 140 - 130); cv->print(cycleBuf);
 blitCanvas(cv, 0, 130, 320, 20);

 g_lastCalStep = _calStep;
 }

 _forceSettingsRedraw = false;
}
