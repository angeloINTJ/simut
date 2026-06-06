/**
 * @file DisplayManager_Dashboard.cpp
 * @brief Dashboard rendering: top bar, ambient/slot panels, bottom buttons.
 * @details Sub-file of DisplayManager.cpp.
 * Includes rounded corner helpers (fixCardCorners,
 * maskStripCorners), drawInterfaceFixed (fixed bg), and blitCanvas
 * (DMA push of canvas -> TFT). restoreNormalDashboard repositions
 * everything after modal events (auth/license/alarm).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "StorageManager.h"
#include "sensors/SensorPanelDispatch.h"

void DisplayManager::fixCardCorners(int16_t x, int16_t y, int16_t w,
 int16_t h, int16_t r,
 uint16_t borderColor) {
 if (!_tft) return;
 for (int16_t i = 0; i < r; i++) {
 int16_t span = (int16_t)(sqrtf(2.0f * r * i - (float)(i * i)) + 0.5f);
 int16_t gap = r - span;
 if (gap <= 0) continue;
 _tft->drawFastHLine(x, y + i, gap, C_BG_MAIN);
 _tft->drawFastHLine(x + w - gap, y + i, gap, C_BG_MAIN);
 _tft->drawFastHLine(x, y + h - 1 - i, gap, C_BG_MAIN);
 _tft->drawFastHLine(x + w - gap, y + h - 1 - i, gap, C_BG_MAIN);
 }
 _tft->drawRoundRect(x, y, w, h, r, borderColor);
}


void DisplayManager::maskStripCorners(GFXcanvas16* canvas,
 int16_t stripRow, int16_t stripH,
 int16_t cardW, int16_t cardH,
 int16_t r, uint16_t bgColor,
 uint16_t borderColor) {
 if (!canvas || r <= 0) return;
 uint16_t* buf = canvas->getBuffer( );
 int16_t stride = canvas->width( );


 constexpr int16_t MAX_R = 24;
 int16_t borderMin[MAX_R], borderMax[MAX_R];
 int16_t rr = (r > MAX_R) ? MAX_R : r;

 for (int16_t i = 0; i < rr; i++) { borderMin[i] = rr; borderMax[i] = -1; }

 {

 int16_t f = 1 - rr;
 int16_t ddF_x = 1;
 int16_t ddF_y = -2 * rr;
 int16_t cx = 0;
 int16_t cy = rr;

 while (cx < cy) {
 if (f >= 0) { cy--; ddF_y += 2; f += ddF_y; }
 cx++; ddF_x += 2; f += ddF_x;


 int16_t row1 = rr - cx, col1 = rr - cy;
 int16_t row2 = rr - cy, col2 = rr - cx;

 if (row1 >= 0 && row1 < rr) {
 if (col1 < borderMin[row1]) borderMin[row1] = col1;
 if (col1 > borderMax[row1]) borderMax[row1] = col1;
 }
 if (row2 >= 0 && row2 < rr) {
 if (col2 < borderMin[row2]) borderMin[row2] = col2;
 if (col2 > borderMax[row2]) borderMax[row2] = col2;
 }
 }
 }


 for (int16_t row = 0; row < stripH; row++) {
 int16_t cardY = stripRow + row;
 uint16_t* rowPtr = buf + (row * stride);


 int16_t bMin = -1, bMax = -1;

 if (cardY < rr) {
 bMin = borderMin[cardY];
 bMax = borderMax[cardY];
 } else if (cardY >= cardH - rr) {
 int16_t mirror = cardH - 1 - cardY;
 bMin = borderMin[mirror];
 bMax = borderMax[mirror];
 }

 if (cardY == 0 || cardY == cardH - 1) {


 for (int16_t x = 0; x < bMin; x++)
 rowPtr[x] = bgColor;
 for (int16_t x = bMin; x < cardW - bMin; x++)
 rowPtr[x] = borderColor;
 for (int16_t x = cardW - bMin; x < cardW; x++)
 rowPtr[x] = bgColor;

 } else if (bMin >= 0) {


 for (int16_t x = 0; x < bMin; x++)
 rowPtr[x] = bgColor;
 for (int16_t x = bMin; x <= bMax; x++)
 rowPtr[x] = borderColor;

 int16_t rBMax = cardW - 1 - bMin;
 int16_t rBMin = cardW - 1 - bMax;
 for (int16_t x = rBMin; x <= rBMax; x++)
 rowPtr[x] = borderColor;
 for (int16_t x = cardW - bMin; x < cardW; x++)
 rowPtr[x] = bgColor;

 } else {

 rowPtr[0] = borderColor;
 rowPtr[cardW - 1] = borderColor;
 }
 }
}



void DisplayManager::restoreNormalDashboard( ) {
 if (!_tft || !_canvasSmall || !_canvasWide) return;
 drawSlotPanel(_lastRenderedState.topSlotTemp, _lastRenderedState.topSlotHum,
 _lastRenderedState.topSlotType, _lastRenderedState.topSlotValid,
 _lastRenderedState.topSlotIdx, _lastRenderedState.topSlotName, true, _topPanel);
 drawSlotPanel(_lastRenderedState.slotTemp, _lastRenderedState.slotHum, _lastRenderedState.slotType,
 _lastRenderedState.slotValid,
 _lastRenderedState.selectedSlotIdx,
 _lastRenderedState.slotName, true, _bottomPanel);
 drawBottomButtons(_lastRenderedState.selectedSlotIdx, true);
}

void DisplayManager::drawInterfaceFixed( ) {


 _tft->fillScreen(C_BG_MAIN);
}

void DisplayManager::blitCanvas(GFXcanvas16* canvas, int16_t dstX, int16_t dstY, int16_t w, int16_t h) {
 if (!canvas || !_tft) return;

 /*
 * Applies LCD alignment offset explicitly here because the
 * drawRGBBitmap routine of Adafruit_SPITFT may devirtualize (or inline) the
 * internal setAddrWindow call depending on version/toolchain, bypassing the
 * TftWithOffset override. We apply the offset directly to the destination
 * coordinates and enable the bypass flag on _tft to ensure that, if the
 * override IS called virtually, it won't apply the offset again (without
 * bypass a double offset would occur on libraries where dispatch works).
 */
 const int8_t ox = _tft->getOffsetX( );
 const int8_t oy = _tft->getOffsetY( );
 dstX += ox;
 dstY += oy;

 int16_t cw = canvas->width( );
 _tft->setOffsetBypass(true);
 if (w == cw) {
 _tft->drawRGBBitmap(dstX, dstY, canvas->getBuffer( ), w, h);
 } else {
 uint16_t* buf = canvas->getBuffer( );
 for (int16_t row = 0; row < h; row++) {
 _tft->drawRGBBitmap(dstX, dstY + row, buf + (row * cw), w, 1);
 }
 }
 _tft->setOffsetBypass(false);
}

/* Full-screen render via 40px strips.
 * Reuses `_canvasWide` (320x45, allocated at Core 1 boot for the dashboard top
 * bar). During full-screen renders (auth/settings/etc), the dashboard is not
 * active — canvas is free for reuse. Blits only 40 of the 45 canvas rows per
 * strip; 5 extra rows are ignored in the blit.
 *
 * No dynamic heap = zero risk of OOM/null-buffer crash. Telemetry runs
 * normally during render (free heap intact). */
GFXcanvas16* DisplayManager::beginScreenRender( ) {
 if (!_canvasWide) return nullptr; /* Core 1 not initialized — unlikely during render */
 _canvasWide->fillScreen(C_BG_MAIN);
 return _canvasWide;
}

void DisplayManager::commitScreenStrip(int16_t stripIdx) {
 if (!_canvasWide || !_tft) return;
 int16_t stripY = stripIdx * RENDER_STRIP_H;
 /* Blit 40 of the 45 canvas rows (5 leftover ignored). */
 blitCanvas(_canvasWide, 0, stripY, 320, RENDER_STRIP_H);
 /* Clear for next strip to be drawn from scratch. Caller may overwrite. */
 _canvasWide->fillScreen(C_BG_MAIN);
}

void DisplayManager::endScreenRender( ) {
 /* No-op: _canvasWide is persistent, nothing to free.
 * Kept in API for consistency (caller still calls it at the end). */
}

void DisplayManager::drawTopBar(const SystemState& state) {
 if(!_canvasWide) return;
 const int W = 320, H = 29;
 _canvasWide->fillScreen(C_BG_MAIN);


 _canvasWide->setFont(&simutFont9pt);
 _canvasWide->setTextSize(1);
 _canvasWide->setTextColor(C_ACCENT);
 _canvasWide->setCursor(3, 20);
 _canvasWide->print("SIMUT");


 bool showingSilence = false;
 if (_alarmSilenced && _alarmSilenceEnd > 0) {
 uint32_t now = millis( );
 if (now < _alarmSilenceEnd) {
 showingSilence = true;
 uint32_t remaining = (_alarmSilenceEnd - now) / 1000;
 char silBuf[32];
 snprintf(silBuf, sizeof(silBuf), "%s: %lus", tr(TR_SILENCED), (unsigned long)remaining);
 _canvasWide->setFont(&simutFont9pt);
 _canvasWide->setTextColor(RGB565(200, 100, 0));
 _canvasWide->setCursor(75, 20);
 _canvasWide->print(silBuf);
 }
 }


 bool showingNotify = false;
 if (!showingSilence && _webNotifyStartMs > 0) {
 uint32_t elapsed = millis( ) - _webNotifyStartMs;
 if (elapsed < WEB_NOTIFY_DURATION_MS) {
 showingNotify = true;
 _canvasWide->setFont(&simutFont9pt);
 _canvasWide->setTextColor(C_ACCENT_HIGH);
 _canvasWide->setCursor(75, 20);
 char notifyBuf[32];
 snprintf(notifyBuf, sizeof(notifyBuf), "Web: %s", _webNotifyUser);
 _canvasWide->print(notifyBuf);
 } else {

 _webNotifyStartMs = 0;
 _webNotifyUser[0] = '\0';
 }
 }


 if (!showingSilence && !showingNotify) {
 /*
 * Date and time centered in the available area.
 * Format: "dd/mm/yy - HH:MM"
 * The " - " separator stays fixed in the center; the date grows to the
 * left and the time grows to the right, ensuring the text
 * does not jump when digits change.
 */
 _canvasWide->setTextSize(1);
 _canvasWide->setFont(&simutFont9pt);
 _canvasWide->setTextColor(C_TITLE_TEXT);

 /* Separate date and time by " - " */
 String fullTime = String(state.timeString);
 int sepIdx = fullTime.indexOf(" - ");
 String datePart = (sepIdx >= 0) ? fullTime.substring(0, sepIdx) : fullTime;
 String timePart = (sepIdx >= 0) ? fullTime.substring(sepIdx + 3) : "";

 /* Measure only sep and date — timeX = sepX + sepW (no need to measure timeW). */
 int16_t bx, by; uint16_t bw, bh;
 uint16_t sepW, dateW;

 _canvasWide->getTextBounds(" - ", 0, 0, &bx, &by, &bw, &bh);
 sepW = bw;
 _canvasWide->getTextBounds(datePart, 0, 0, &bx, &by, &bw, &bh);
 dateW = bw;

 /*
 * Separator center fixed at display middle (x=160).
 * Date grows left, time grows right.
 */
 const int centerX = 160;

 int sepX = centerX - (int)sepW / 2;
 int dateX = sepX - (int)dateW;
 int timeX = sepX + (int)sepW;

 _canvasWide->setCursor(dateX, 20);
 _canvasWide->print(datePart);
 _canvasWide->setTextColor(C_TEXT_SUB);
 _canvasWide->setCursor(sepX, 20);
 _canvasWide->print(" - ");
 _canvasWide->setTextColor(C_TITLE_TEXT);
 _canvasWide->setCursor(timeX, 20);
 _canvasWide->print(timePart);
 }


 int xIcon = 305;

 /* Memory barrier before reading _pktArrowState + flash vars
 * published by Core 0 in setTelemetrySendStatus. */
 __dmb( );
 if (state.pendingPkts > 0 || _pktArrowState > 0) {
 /*
 * NUMBER color: based on last send result.
 * state 1 or 3 -> blue (success / success flash)
 * state 2 -> red (failure)
 * state 0 -> blue (idle, never sent)
 */
 uint16_t numColor = (_pktArrowState == 2) ? C_TEMP_HOT : C_ACCENT_HIGH;

 /*
 * ARROW color: same as number, except during flash (state 3)
 * where it alternates blue/white every 300ms for 1 second.
 */
 uint16_t arrowColor = numColor;

 if (_pktArrowState == 3) {
 uint32_t now = millis( );
 if (now >= _pktArrowFlashEnd) {
 _pktArrowState = 1;
 arrowColor = C_ACCENT_HIGH;
 } else {
 if (now - _pktArrowFlashTime >= 300) {
 _pktArrowFlashOn = !_pktArrowFlashOn;
 _pktArrowFlashTime = now;
 }
 arrowColor = _pktArrowFlashOn ? RGB565(255, 255, 255) : C_ACCENT_HIGH;
 }
 }

 if (state.pendingPkts > 0) {
 char pktBuf[10];
 /* >=1000 abbreviates as "Nk" to fit in the top bar. */
 if (state.pendingPkts >= 1000) {
 snprintf(pktBuf, sizeof(pktBuf), "%uk", state.pendingPkts / 1000);
 } else {
 snprintf(pktBuf, sizeof(pktBuf), "%u", state.pendingPkts);
 }

 _canvasWide->setFont(&simutFont9pt);

 int16_t tx1, ty1; uint16_t tw, th;
 _canvasWide->getTextBounds(pktBuf, 0, 0, &tx1, &ty1, &tw, &th);

 /*
 * Layout: [number][gapNum][arrow][gapWifi][wifi]
 * Arrow: 12px. Gap between number and arrow: 4px.
 * Gap between arrow and wifi: 3px.
 * When number is wide (>=3 digits), xIcon backs up 1 character.
 */
 const int arrowTotalW = 12;
 const int gapToWifi = 3;
 const int gapNumArrow = 4;
 int effectiveXIcon = xIcon;
 if ((int)tw > 24) effectiveXIcon -= 8; /* back up for large numbers */

 int arrowRight = effectiveXIcon - gapToWifi;
 int arrowLeft = arrowRight - arrowTotalW;
 int textX = arrowLeft - gapNumArrow - (int)tw;

 /* Number — fixed color based on status */
 _canvasWide->setTextColor(numColor);
 _canvasWide->setCursor(textX, 20);
 _canvasWide->print(pktBuf);

 /*
 * Right-pointing arrow:
 * - Rectangular shaft (6x3 px) at vertical center
 * - Triangular tip (6x8 px) on the right
 */
 int ay = 13;
 int shaftX = arrowLeft;
 int shaftW = 6;
 int tipX = shaftX + shaftW;
 int tipW = arrowTotalW - shaftW;

 _canvasWide->fillRect(shaftX, ay - 1, shaftW, 3, arrowColor);
 _canvasWide->fillTriangle(tipX, ay - 4,
 tipX, ay + 4,
 tipX + tipW, ay,
 arrowColor);

 /* Reposition wifi if needed */
 if ((int)tw > 24) xIcon = effectiveXIcon;
 }
 }


 int barras = 0;
 if (state.wifiRssi > -100) {
 if (state.wifiRssi > -55) barras = 4;
 else if (state.wifiRssi > -65) barras = 3;
 else if (state.wifiRssi > -75) barras = 2;
 else barras = 1;
 }
 for (int i = 0; i < 4; i++) {
 _canvasWide->fillRect(xIcon + (i * 3), 20 - (4 + (i * 2)), 2, 4 + (i * 2),
 (i < barras) ? C_TEMP_OK : C_BAR_BG);
 }

 blitCanvas(_canvasWide, 0, 0, W, H);
}


void DisplayManager::drawSlotPanel(float t, float h, SensorType type, bool isValid, int slotIdx, const char* name, bool forceNameRedraw, DashPanel& panel) {
 if(!_canvasWide) return;
 int16_t x1, y1; uint16_t h_bound;


 uint16_t panelBg = slotAlarmBg(slotIdx);
 bool isRedPhase = _alarmFlashPhase && isSlotAlarming(slotIdx) && !_alarmSilenced;
 uint16_t nameColor = isRedPhase ? RGB565(255, 255, 255) : C_SENSOR_NAME;
 uint16_t unitColor = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_MAIN;
 if (isSlotAlarming(slotIdx)) forceNameRedraw = true;


 /* Selected slot card (second panel with double border of the dashboard),
 * positioned below the ambient card. Same 4 px horizontal inset
 * to ensure 4 px margin on each side. */
 static constexpr int16_t CARD_X = 4, CARD_Y = 115;
 static constexpr int16_t CARD_W = 312, CARD_H = 75, CARD_R = 12;


 bool slotAlarm = isSlotAlarming(slotIdx) && _alarmFlashPhase;
 uint16_t borderColor = slotAlarm ? RGB565(255, 60, 60) : C_ACCENT_HIGH;

 if (panel.showMinMax) {
 /* Track mode transition */
 panel.lastMinMax = true;

 /* =============================================================
 * MIN/MAX MODE — 3 blits with embedded border
 * Slot has no humidity.
 * ============================================================= */

 uint16_t txtSub = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_MAIN;

 /* Blit 1: Name (20px) */
 {
 _canvasWide->fillScreen(panelBg);
 _canvasWide->setFont(&simutFont9pt);
 _canvasWide->setTextSize(1);
 _canvasWide->setTextColor(txtSub);
 const char* displayName = name;
 char buf[16];
 if (strlen(name) == 0) {
 snprintf(buf, 16, "Sensor %d", slotIdx);
 displayName = buf;
 }
 int16_t nx1, ny1; uint16_t nw, nh;
 _canvasWide->getTextBounds(displayName, 0, 0, &nx1, &ny1, &nw, &nh);
 _canvasWide->setCursor((CARD_W - (int)nw) / 2, 15);
 _canvasWide->print(displayName);
 maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R,
 C_BG_MAIN, borderColor);
 blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
 }

 /* Blit 2: Min + Max together (43px) — driver-rendered */
 {
 _canvasWide->fillScreen(panelBg);
 sensorRenderMinMax(_canvasWide, type,
     panel.minTemp, panel.maxTemp, panel.minHum, panel.maxHum,
     isValid, CARD_W, isRedPhase, panelBg,
     simutFont9pt,
     txtSub, C_TEMP_OK, C_TEMP_HOT, C_HUMIDITY, C_TEXT_OFF,
     C_ACCENT_HIGH, C_BTN_TEXT_ACTIVE,
     tr(TR_MIN_LBL), tr(TR_MAX_LBL), tr(TR_HUM_SUFFIX));
 /* Side borders (intermediate strip, no corners) */
 {
 uint16_t* buf = _canvasWide->getBuffer( );
 int stride = _canvasWide->width( );
 for (int row = 0; row < 43; row++) {
 buf[row * stride] = borderColor;
 buf[row * stride + CARD_W - 1] = borderColor;
 }
 }
 blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 43);
 }



 /* Blit 3: Bottom fill (12px) — with bottom corners + borders */
 {
 _canvasWide->fillScreen(panelBg);
 maskStripCorners(_canvasWide, 63, 12, CARD_W, CARD_H, CARD_R,
 C_BG_MAIN, borderColor);
 blitCanvas(_canvasWide, CARD_X, CARD_Y + 63, CARD_W, 12);
 }

 } else {
 /* Force name redraw on min/max -> normal transition */
 if (panel.lastMinMax) forceNameRedraw = true;
 panel.lastMinMax = false;

 /* =============================================================
 * NORMAL MODE — centered temperature with large icon
 * ============================================================= */

 if (forceNameRedraw) {
 _canvasWide->fillScreen(panelBg);
 _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextSize(1);
 _canvasWide->setTextColor(nameColor);
 const char* displayName = name;
 char buf[16];
 if (strlen(name) == 0) {
 snprintf(buf, 16, "Sensor %d", slotIdx);
 displayName = buf;
 }
 int16_t nx1, ny1; uint16_t nw, nh;
 _canvasWide->getTextBounds(displayName, 0, 0, &nx1, &ny1, &nw, &nh);
 _canvasWide->setCursor((CARD_W - (int)nw) / 2, 15);
 _canvasWide->print(displayName);
 maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
 blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
 }

 /* Gap strip to center content (8px) */
 {
 _canvasWide->fillScreen(panelBg);
 uint16_t* buf = _canvasWide->getBuffer( );
 int stride = _canvasWide->width( );
 for (int row = 0; row < 8; row++) {
 buf[row * stride] = borderColor;
 buf[row * stride + CARD_W - 1] = borderColor;
 }
 blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 8);
 }

 _canvasWide->fillScreen(panelBg);

 if (!isValid) {
 _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextSize(1);
 _canvasWide->setTextColor(isRedPhase ? RGB565(255,255,255) : C_TEMP_HOT);
 int16_t ex1, ey1; uint16_t ew, eh;
 _canvasWide->getTextBounds(tr(TR_ERROR_LBL), 0, 0, &ex1, &ey1, &ew, &eh);
 _canvasWide->setCursor((CARD_W - (int)ew) / 2, 28);
 _canvasWide->print(tr(TR_ERROR_LBL));
 } else {
 _canvasWide->fillScreen(C_BG_MAIN);
 sensorRenderPanel(_canvasWide, type, t, h, isValid, CARD_W, true,
                   isRedPhase, panelBg,
                   simutFont24pt, simutFont12pt, simutFont9pt,
                   C_TEXT_SUB, C_TEMP_OK, C_TEMP_HOT, C_HUMIDITY, C_TEXT_OFF, tr(TR_HUM_SUFFIX));
 maskStripCorners(_canvasWide, 28, 40, CARD_W, CARD_H, CARD_R, C_BG_MAIN,
                  isRedPhase ? RGB565(200,0,0) : C_TEXT_SUB);
 blitCanvas(_canvasWide, CARD_X, CARD_Y + 28, CARD_W, 40);
 goto _slot_bottom_fill;

 const int iconW = 20;
 const int iconGap = 8;
 const int unitGap = 3;
 const int dotGap = 4;

 _canvasWide->setFont(&simutFont24pt); _canvasWide->setTextSize(1);

 char intPart[10]; char decPart[5];
 bool isNan = isnan(t);
 uint16_t intW = 0, decW = 0;

 if (isNan) {
 _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &intW, &h_bound);
 decW = 0;
 } else {
 int fractional = abs((int)(t * 10) % 10);
 snprintf(intPart, sizeof(intPart), "%d", (int)t);
 snprintf(decPart, sizeof(decPart), ".%d", fractional);
 _canvasWide->getTextBounds(intPart, 0, 0, &x1, &y1, &intW, &h_bound);
 _canvasWide->getTextBounds(decPart, 0, 0, &x1, &y1, &decW, &h_bound);
 }

 _canvasWide->setFont(&simutFont9pt);
 uint16_t degW;
 _canvasWide->getTextBounds("o", 0, 0, &x1, &y1, &degW, &h_bound);
 _canvasWide->setFont(&simutFont12pt);
 uint16_t cW;
 _canvasWide->getTextBounds("C", 0, 0, &x1, &y1, &cW, &h_bound);
 int unitTotalW = (int)degW + 8 + (int)cW;

 int numW = (int)intW + (isNan ? 0 : dotGap + (int)decW);
 int totalW = iconW + iconGap + numW + unitGap + unitTotalW;
 int offsetX = (CARD_W - totalW) / 2;

 int iconX = offsetX;
 int numAnchorX = iconX + iconW + iconGap + (int)intW;
 int unitX;

 _canvasWide->setFont(&simutFont24pt);
 if (isNan) {
 _canvasWide->setTextColor(isRedPhase ? RGB565(200,180,180) : C_TEXT_OFF);
 _canvasWide->setCursor(iconX + iconW + iconGap, 35);
 _canvasWide->print("--.-");
 unitX = iconX + iconW + iconGap + (int)intW + unitGap;
 } else {
 _canvasWide->setTextColor(isRedPhase ? RGB565(255,255,255) : C_TEMP_OK);
 int numCursorX = numAnchorX - (int)intW;
 _canvasWide->setCursor(numCursorX, 35);
 _canvasWide->print(intPart);
 if (t < 0) {
 int16_t mx1, my1; uint16_t mw, mh;
 _canvasWide->getTextBounds("-", 0, 0, &mx1, &my1, &mw, &mh);
 int eraseW = (int)mw / 3;
 if (eraseW < 2) eraseW = 2;
 _canvasWide->fillRect(numCursorX, 0, eraseW, 45, panelBg);
 }
 _canvasWide->setFont(&simutFont24pt);
 _canvasWide->setCursor(numAnchorX + dotGap, 35);
 _canvasWide->print(decPart);
 unitX = numAnchorX + dotGap + (int)decW + unitGap;
 }

 _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(unitColor);
 _canvasWide->setCursor(unitX, 17); _canvasWide->print("o");
 _canvasWide->setFont(&simutFont12pt);
 _canvasWide->setCursor(unitX + 8, 35); _canvasWide->print("C");

 /* --- Humidity --- */
 if (!isnan(h)) {
 _canvasWide->setFont(&simutFont12pt);
 _canvasWide->setTextColor(isRedPhase ? RGB565(255,255,255) : C_HUMIDITY);
 _canvasWide->setCursor(CARD_W - 56, 35);
 _canvasWide->print((int)h);
 _canvasWide->setFont(&simutFont9pt);
 _canvasWide->setCursor(CARD_W - 22, 17);
 _canvasWide->print("%");
 }

 /* Thermometer icon — drawn last (slot) */
 {
 uint16_t ic = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_SUB;
 uint16_t merc = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_HOT;
 int ix = iconX, iy = 4;
 _canvasWide->fillCircle(ix + 10, iy + 26, 7, ic);
 _canvasWide->fillRoundRect(ix + 6, iy, 8, 24, 4, ic);
 _canvasWide->fillRoundRect(ix + 8, iy + 2, 4, 20, 2, panelBg);
 _canvasWide->fillCircle(ix + 10, iy + 26, 5, panelBg);
 _canvasWide->fillRect(ix + 9, iy + 10, 2, 14, merc);
 _canvasWide->fillCircle(ix + 10, iy + 26, 4, merc);
 _canvasWide->fillCircle(ix + 10, iy + 2, 2, ic);
 }
 }

 maskStripCorners(_canvasWide, 28, 40, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
 blitCanvas(_canvasWide, CARD_X, CARD_Y + 28, CARD_W, 40);

_slot_bottom_fill:
 /* Strip 4: Bottom fill (7px) */
 {
 _canvasWide->fillScreen(panelBg);
 maskStripCorners(_canvasWide, 68, 7, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
 blitCanvas(_canvasWide, CARD_X, CARD_Y + 68, CARD_W, 7);
 }
 }
}

int DisplayManager::buildDashLayout(DashBtn out[5], int *totalPages, bool *hasPaging) {
 /* Builds layout of 5 fixed slots (left->right). kind=-1 = empty.
 * The pagination button ALWAYS stays at position 4 (right corner) when
 * it exists; partial page slots leave gaps instead of pushing
 * the page button left. */
 for (int i = 0; i < 5; i++) { out[i].kind = -1; out[i].slotId = -1; }

 if (!_sysConfigPtr) return 0;
 SystemConfig &cfg = *_sysConfigPtr;
 DashBtn all[11];
 int total = 0;
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfg.sensors[i].active) {
 all[total].kind = 0;
 all[total].slotId = (int8_t)i;
 total++;
 }
 }
 all[total].kind = 1; /* CFG always present */
 all[total].slotId = -1;
 total++;

 const int LINE_CAP = 5;
 bool paging = (total > LINE_CAP);
 int perPage = paging ? 4 : LINE_CAP; /* paging reserves pos 4 for page btn */
 int pages = (total + perPage - 1) / perPage;
 if (_currentPage >= pages) _currentPage = 0; /* clamp after config change */

 int firstIdx = _currentPage * perPage;
 int lastIdx = firstIdx + perPage;
 if (lastIdx > total) lastIdx = total;

 int pos = 0;
 for (int i = firstIdx; i < lastIdx; i++) out[pos++] = all[i];
 if (paging) { out[4].kind = 2; out[4].slotId = -1; } /* always position 4 */

 if (totalPages) *totalPages = pages;
 if (hasPaging) *hasPaging = paging;
 return paging ? 5 : pos;
}

void DisplayManager::drawBottomButtons(int selectedIdx, bool forceRedraw) {
 if(!_canvasWide) return;
 _canvasWide->fillScreen(C_BG_MAIN);
 const int btnW = 58, gap = 5, xStart = 5, pitch = btnW + gap;

 DashBtn btns[5];
 int totalPages = 1;
 bool paging = false;
 int n = buildDashLayout(btns, &totalPages, &paging);

 /* Detects alarms in ACTIVE slots on other pages (to color the page btn) */
 if (!_sysConfigPtr) { blitCanvas(_canvasWide, 0, 195, 320, 41); return; }
 SystemConfig &cfg = *_sysConfigPtr;
 bool hasAlarmsOnOtherPages = false;
 if (paging && _alarmSlotMask != 0) {
 for (int s = 0; s < MAX_SENSORS; s++) {
 if (!cfg.sensors[s].active) continue;
 if (!isSlotAlarming(s)) continue;
 bool inThisPage = false;
 for (int i = 0; i < n; i++) {
 if (btns[i].kind == 0 && btns[i].slotId == s) { inThisPage = true; break; }
 }
 if (!inThisPage) { hasAlarmsOnOtherPages = true; break; }
 }
 }

 for (int i = 0; i < 5; i++) {
 const DashBtn &b = btns[i];
 if (b.kind < 0) continue; /* gap between slots and page btn anchored to right */
 int x = xStart + (i * pitch);

 if (b.kind == 0) { /* SLOT */
 int realIdx = b.slotId;
 bool isActive = (realIdx == selectedIdx);
 bool btnAlarm = _alarmFlashPhase && isSlotAlarming(realIdx);
 uint16_t bgColor, txtColor;
 if (btnAlarm) {
 bgColor = RGB565(180, 30, 30);
 txtColor = RGB565(255, 255, 255);
 } else if (isActive) {
 bgColor = C_ACCENT_HIGH;
 txtColor = C_BTN_TEXT_ACTIVE;
 } else {
 bgColor = C_CARD_BG;
 txtColor = isSlotAlarming(realIdx) ? C_TEMP_HOT : C_BTN_TEXT;
 }
 _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, bgColor);
 _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextSize(1); _canvasWide->setTextColor(txtColor);
 char label[8]; snprintf(label, sizeof(label), "S%d", realIdx);
 int16_t x1, y1; uint16_t w, h;
 _canvasWide->getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
 _canvasWide->setCursor(x + (btnW - w)/2, 28);
 _canvasWide->print(label);

 } else if (b.kind == 1) { /* CFG */
 _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, C_CARD_BG);
 _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextSize(1); _canvasWide->setTextColor(C_BTN_TEXT);
 int16_t x1, y1; uint16_t w, h;
 _canvasWide->getTextBounds("CFG", 0, 0, &x1, &y1, &w, &h);
 _canvasWide->setCursor(x + (btnW - w)/2, 28);
 _canvasWide->print("CFG");

 } else { /* PAGE */
 uint16_t pagTxtCol = C_BTN_TEXT;
 if (hasAlarmsOnOtherPages && _alarmFlashPhase) {
 _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, RGB565(180, 30, 30));
 pagTxtCol = RGB565(255, 255, 255);
 } else if (hasAlarmsOnOtherPages) {
 _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, C_CARD_BG);
 _canvasWide->drawRoundRect(x, 0, btnW, 40, 12, RGB565(255, 60, 60));
 } else {
 _canvasWide->drawRoundRect(x, 0, btnW, 40, 12, C_TEXT_SUB);
 }
 char pageStr[4]; snprintf(pageStr, sizeof(pageStr), "%d", _currentPage + 1);
 char totStr[4]; snprintf(totStr, sizeof(totStr), "/%d", totalPages);
 _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextColor(pagTxtCol);
 _canvasWide->setCursor(x + 15, 28); _canvasWide->print(pageStr);
 _canvasWide->setFont(NULL); _canvasWide->setCursor(x + 35, 8); _canvasWide->print(totStr);
 }
 }
 /* h=41 instead of 45 ensures 4 px bottom margin (y+h=236 <= 236). */
 blitCanvas(_canvasWide, 0, 195, 320, 41);
}
