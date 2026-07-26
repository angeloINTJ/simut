/**
 * @file DisplayManager_Graph.cpp
 * @brief Graph rendering: showStats/showGraphPlot + draw* screens.
 * @details Sub-file of DisplayManager.cpp.
 * Includes: dual Y-axis plot, decimation, peak markers, alternating header
 * bar (name/date), detailed numeric screen, loading screen
 * + period buttons (1H/6H/12H/24H/7D), formatGraphTime helper.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "DisplayManager_FmtFloat.h"

void DisplayManager::showStats(const GraphDataPackage& data, float minHum, float maxHum) {
 mutex_enter_blocking(&_stateMutex);
 _graphData = data; _currentMinHum = minHum; _currentMaxHum = maxHum;
 _uiMode = MODE_STATS_VIEW;
 __dmb( );
 _repaintGraph = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::showGraphPlot(const GraphDataPackage& data, float minHum, float maxHum) {
 mutex_enter_blocking(&_stateMutex);
 _graphData = data; _currentMinHum = minHum; _currentMaxHum = maxHum;
 _uiMode = MODE_GRAPH_VIEW;
 _headerShowName = false;
 _headerNameTimer = 0;
 __dmb( );
 _repaintGraph = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::drawLoadingScreen( ) {

 _driver.tft->fillScreen(C_BG_MAIN);
 _driver.tft->setFont(&simutFont12pt);
 _driver.tft->setTextColor(C_TEXT_MAIN);
 int16_t x1, y1; uint16_t w, h;
 String t1 = tr(TR_LOADING);
 _driver.tft->getTextBounds(t1, 0, 0, &x1, &y1, &w, &h);
 _driver.tft->setCursor(160 - (w/2), 127);
 _driver.tft->print(t1);
 _loadingDrawn = true;
}



void DisplayManager::requestLoadingScreen( ) {
 _loadingDrawn = false;
 _repaintLoading = true;
 _uiMode = MODE_GRAPH_LOADING;
}

void DisplayManager::drawPeriodButtons( ) {
 if (!_driver.canvas) return;

 /*
 * Layout: 5 buttons with pixel-art icons (60x40 each, gap=4)
 * [◀ Past] [▶ Future] [📅 Cal] [🔍+ ZoomIn] [🔍- ZoomOut]
 * Total: 5x60 + 4x4 = 316px, startX=2.
 */
 const int btnW = 60, btnH = 40, btnR = 12, gap = 4, startX = 2;
 const char* ranges[] = {"1H", "6H", "12H", "24H", "7D"};

 bool canFwd = (_graphNavOffset < 0);
 bool canZoomIn = (_graphData.timeRange > 0); /* 0=1H is max zoom */
 bool canZoomOut = (_graphData.timeRange < 4); /* 4=7D is min zoom */

 GFXcanvas16* cv = _driver.canvas;
 cv->fillScreen(C_BG_MAIN);

 /* Helper: draws button background and returns X */
 auto btnBase = [&](int idx, bool enabled) -> int {
 int x = startX + idx * (btnW + gap);
 uint16_t bg = enabled ? C_CARD_BG : C_BG_MAIN;
 cv->fillRoundRect(x, 0, btnW, btnH, btnR, bg);
 if (!enabled) cv->drawRoundRect(x, 0, btnW, btnH, btnR, C_TEXT_OFF);
 return x;
 };

 int cx, cy;

 /* ════ 0: Past (◀◀) ════ */
 {
 int x = btnBase(0, true);
 cx = x + btnW / 2; cy = btnH / 2;
 uint16_t ic = C_ACCENT_HIGH;

 /* Double left chevron */
 cv->drawLine(cx + 2, cy - 7, cx - 5, cy, ic);
 cv->drawLine(cx - 5, cy, cx + 2, cy + 7, ic);
 cv->drawLine(cx + 3, cy - 7, cx - 4, cy, ic);
 cv->drawLine(cx - 4, cy, cx + 3, cy + 7, ic);

 cv->drawLine(cx + 8, cy - 7, cx + 1, cy, ic);
 cv->drawLine(cx + 1, cy, cx + 8, cy + 7, ic);
 cv->drawLine(cx + 9, cy - 7, cx + 2, cy, ic);
 cv->drawLine(cx + 2, cy, cx + 9, cy + 7, ic);
 }

 /* ════ 1: Future (▶▶) ════ */
 {
 int x = btnBase(1, canFwd);
 cx = x + btnW / 2; cy = btnH / 2;
 uint16_t ic = canFwd ? C_ACCENT_HIGH : C_TEXT_OFF;

 /* Double right chevron */
 cv->drawLine(cx - 8, cy - 7, cx - 1, cy, ic);
 cv->drawLine(cx - 1, cy, cx - 8, cy + 7, ic);
 cv->drawLine(cx - 9, cy - 7, cx - 2, cy, ic);
 cv->drawLine(cx - 2, cy, cx - 9, cy + 7, ic);

 cv->drawLine(cx - 2, cy - 7, cx + 5, cy, ic);
 cv->drawLine(cx + 5, cy, cx - 2, cy + 7, ic);
 cv->drawLine(cx - 3, cy - 7, cx + 4, cy, ic);
 cv->drawLine(cx + 4, cy, cx - 3, cy + 7, ic);
 }

 /* ════ 2: Calendar (📅) ════ */
 {
 int x = btnBase(2, true);
 cx = x + btnW / 2; cy = btnH / 2;
 uint16_t ic = C_ACCENT;
 int gx = cx - 8, gy = cy - 8;

 /* Calendar body 16x16 */
 cv->drawRoundRect(gx, gy + 2, 16, 14, 2, ic);

 /* Filled title bar */
 cv->fillRect(gx + 1, gy + 3, 14, 4, ic);

 /* Top handles */
 cv->drawFastVLine(gx + 4, gy, 4, ic);
 cv->drawFastVLine(gx + 11, gy, 4, ic);

 /* Internal grid: 3 columns x 2 rows of dots */
 for (int r = 0; r < 2; r++) {
 for (int c = 0; c < 3; c++) {
 cv->fillRect(gx + 2 + c * 5, gy + 9 + r * 4, 3, 2, ic);
 }
 }
 }

 /* ════ 3: Zoom In (🔍+) ════ */
 {
 int x = btnBase(3, canZoomIn);
 cx = x + btnW / 2; cy = btnH / 2;
 uint16_t ic = canZoomIn ? C_TEMP_OK : C_TEXT_OFF;

 /* Magnifying glass */
 int lx = cx - 3, ly = cy - 3, lr = 8;
 cv->drawCircle(lx, ly, lr, ic);
 cv->drawCircle(lx, ly, lr - 1, ic);

 /* Diagonal handle */
 cv->drawLine(lx + 6, ly + 5, lx + 11, ly + 10, ic);
 cv->drawLine(lx + 5, ly + 6, lx + 10, ly + 11, ic);

 /* + symbol */
 cv->drawFastHLine(lx - 4, ly, 9, ic);
 cv->drawFastVLine(lx, ly - 4, 9, ic);

 /* Next range label (zoom in = range-1) */
 if (canZoomIn) {
 cv->setFont(NULL); cv->setTextSize(1);
 cv->setTextColor(ic);
 const char* lbl = ranges[_graphData.timeRange - 1];
 int lblW = strlen(lbl) * 6; /* NULL font: 6px/char */
 cv->setCursor(x + (btnW - lblW) / 2, btnH - 9);
 cv->print(lbl);
 }
 }

 /* ════ 4: Zoom Out (🔍−) ════ */
 {
 int x = btnBase(4, canZoomOut);
 cx = x + btnW / 2; cy = btnH / 2;
 uint16_t ic = canZoomOut ? C_TEMP_WARM : C_TEXT_OFF;

 /* Magnifying glass (same shape) */
 int lx = cx - 3, ly = cy - 3, lr = 8;
 cv->drawCircle(lx, ly, lr, ic);
 cv->drawCircle(lx, ly, lr - 1, ic);

 /* Handle */
 cv->drawLine(lx + 6, ly + 5, lx + 11, ly + 10, ic);
 cv->drawLine(lx + 5, ly + 6, lx + 10, ly + 11, ic);

 /* - symbol */
 cv->drawFastHLine(lx - 4, ly, 9, ic);

 /* Next range label (zoom out = range+1) */
 if (canZoomOut) {
 cv->setFont(NULL); cv->setTextSize(1);
 cv->setTextColor(ic);
 const char* lbl = ranges[_graphData.timeRange + 1];
 int lblW = strlen(lbl) * 6;
 cv->setCursor(x + (btnW - lblW) / 2, btnH - 9);
 cv->print(lbl);
 }
 }

 /* y=195 + h=btnH; avoids reaching y=240. If btnH is 45
 * (standard footer height), limits to 41. */
 int16_t footerH = (btnH > 41) ? 41 : (int16_t)btnH;
 blitCanvas(_driver.canvas, 0, 195, 320, footerH);
}


/* ─────────────────────────────────────────────────────────────────────────── */
/* GRAPH HEADER (name/date alternation) */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief Draws only the top bar (28px) of the graph.
 *
 * Alternates every 3 seconds between:
 * - Sensor name (e.g. "Ambiente")
 * - Date/time interval of the graph (e.g. "06/04 14:00 - 15:00")
 *
 * Called by strip rendering at sTop==0 and by the periodic timer on Core 1.
 * Blits directly at y=0, without repainting the graph body.
 */
void DisplayManager::drawGraphHeaderBar(bool blitNow) {
 if (!_driver.canvas) return;

 GFXcanvas16* cv = _driver.canvas;
 /* Top safe zone (canvas y=0..3) in BG + header itself at
 * canvas y=4..31. With this layout, standalone callers and callers from
 * inside strip-render (whose external blit copies canvas y=0..44 -> display
 * y=0..44) produce exactly the same visual result on the display:
 * header at y=4..31 with 4 px safe zone above. */
 cv->fillRect(0, 0, 320, 4, C_BG_MAIN);
 cv->fillRect(0, 4, 320, 28, C_CARD_BG);
 cv->setFont(&simutFont9pt);

 /* ── Current range pill on left corner ── */
 int contentStartX = 4;
 {
 const char* ranges[] = {"1H", "6H", "12H", "24H", "7D"};
 const char* rLabel = ranges[_graphData.timeRange];
 int16_t rx, ry; uint16_t rw, rh;
 cv->getTextBounds(rLabel, 0, 0, &rx, &ry, &rw, &rh);
 int pillW = rw + 12;
 cv->fillRoundRect(4, 8, pillW, 20, 8, C_ACCENT);
 cv->setTextColor(C_BG_MAIN);
 cv->setCursor(10 - rx, 23);
 cv->print(rLabel);
 contentStartX = 4 + pillW + 4; /* Space after pill */
 }

 /* Usable area for centered text: contentStartX .. 280 */
 int centerZone = 280 - contentStartX;

 if (_headerShowName) {
 /* ── Header tap: show sensor name for 3 seconds ── */
 cv->setTextColor(C_TEXT_MAIN);
 int16_t bx, by; uint16_t bw, bh;
 cv->getTextBounds(_graphData.title, 0, 0, &bx, &by, &bw, &bh);
 int tx = contentStartX + (centerZone - (int)bw) / 2 - bx;
 if (tx < contentStartX) tx = contentStartX;
 cv->setCursor(tx, 24);
 cv->print(_graphData.title);
 } else if (_graphData.tsCutoff > 0 && _graphData.tsEnd > 0) {
 /*
 * Centered date interval.
 * Shows the full time window (tsCutoff..tsEnd),
 * not just the range of available data.
 */
 char dateBuf[32];
 struct tm tmFirst, tmLast;
 localtime_r(&_graphData.tsCutoff, &tmFirst);
 localtime_r(&_graphData.tsEnd, &tmLast);

 bool sameDay = (tmFirst.tm_mday == tmLast.tm_mday
 && tmFirst.tm_mon == tmLast.tm_mon);

 if (sameDay) {
 snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d %02d:%02d - %02d:%02d",
 tmFirst.tm_mday, tmFirst.tm_mon + 1,
 tmFirst.tm_hour, tmFirst.tm_min,
 tmLast.tm_hour, tmLast.tm_min);
 } else {
 snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d %02d:%02d - %02d/%02d %02d:%02d",
 tmFirst.tm_mday, tmFirst.tm_mon + 1,
 tmFirst.tm_hour, tmFirst.tm_min,
 tmLast.tm_mday, tmLast.tm_mon + 1,
 tmLast.tm_hour, tmLast.tm_min);
 }

 uint16_t dateColor = (_graphData.count >= 2) ? C_ACCENT_HIGH : C_TEXT_SUB;
 cv->setTextColor(dateColor);
 int16_t bx, by; uint16_t bw, bh;
 cv->getTextBounds(dateBuf, 0, 0, &bx, &by, &bw, &bh);
 int tx = contentStartX + (centerZone - (int)bw) / 2 - bx;
 if (tx < contentStartX) tx = contentStartX;
 cv->setCursor(tx, 24);
 cv->print(dateBuf);
 } else {
 /* No data and no reference timestamps */
 cv->setTextColor(C_TEXT_SUB);
 cv->setCursor(contentStartX, 24);
 cv->print(_graphData.title);
 }

 /* X button (close) in top right corner (284, 6, 32, 24).
 * x+w=316 stays within the 4 px right safe zone. */
 cv->fillRoundRect(284, 6, 32, 24, 6, C_TEMP_WARM);
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_BG_MAIN);
 cv->setCursor(293, 23);
 cv->print("X");

 /* Blit the entire block (safe zone + header) directly to display 1:1.
 * Suppressed in strip-render: the external strip blit covers this region. */
 if (blitNow) blitCanvas(cv, 0, 0, 320, 32);
}


/* ─────────────────────────────────────────────────────────────────────────── */
/* HISTORY CALENDAR SCREEN */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief Draws the monthly calendar with data day indicators.
 *
 * Layout (320x240):
 * Header (0..27): [◀ Month] "Apr 2026" [Month ▶]
 * Grid (28..194): D S T Q Q S S headers + 6x7 grid
 * Bottom (195..239): [◀ Month] [Today] [Month ▶]
 *
 * Days with data get a blue dot (C_ACCENT).
 * Current day highlighted with semi-transparent background.
 * Tap on a day with data sends EVT_CALENDAR_DAY.
 */

void DisplayManager::drawGraphIcon(int16_t x, int16_t y, uint16_t color) {
 _driver.tft->fillRect(x, y + 12, 6, 10, color);
 _driver.tft->fillRect(x + 8, y + 4, 6, 18, color);
 _driver.tft->fillRect(x + 16, y + 8, 6, 14, color);
 _driver.tft->drawLine(x, y+2, x+22, y+2, color);
}

void DisplayManager::drawStatsScreen( ) {
 int16_t x1, y1; uint16_t w, h_bound;

 /* Header via canvas — appears instantly */
 if (_driver.canvas) {
 GFXcanvas16* cv = _driver.canvas;
 cv->fillScreen(C_BG_MAIN);
 cv->fillRect(4, 4, 312, 32, C_CARD_BG);
 cv->setFont(&simutFont9pt); cv->setTextColor(C_TEXT_MAIN);
 cv->setCursor(14, 23); cv->print(_graphData.title);
 cv->fillRoundRect(280, 4, 36, 24, 6, C_TEMP_WARM);
 cv->setFont(&simutFont9pt); cv->setTextColor(C_BG_MAIN);
 cv->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
 cv->setCursor(298 - w / 2, 23); cv->print("X");
 blitCanvas(cv, 0, 0, 320, 45);
 } else {
 _driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 _driver.tft->setCursor(14, 23); _driver.tft->print(_graphData.title);
 _driver.tft->fillRoundRect(280, 4, 36, 24, 6, C_TEMP_WARM);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_BG_MAIN);
 _driver.tft->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor(298 - w / 2, 23); _driver.tft->print("X");
 }

 /* Clear zone below header/canvas (y=45..235) — 4px bottom margin */
 _driver.tft->fillRect(4, 45, 312, 191, C_BG_MAIN);


 _driver.tft->setFont(NULL); _driver.tft->setTextSize(1); _driver.tft->setTextColor(C_TEXT_SUB);
 _driver.tft->setCursor(14, 38); _driver.tft->print("ID: "); _driver.tft->print(_graphData.hwId);
 _driver.tft->setCursor(14, 49); _driver.tft->print("SN: "); _driver.tft->print(_graphData.rom);


 auto drawTemp = [&](float val, int anchorX, int y, uint16_t color, bool large) {
 int16_t bx1, by1; uint16_t bw, bh;
 int symbolX = anchorX + (large ? 38 : 28);
 _driver.tft->setTextColor(color);

 if (large) _driver.tft->setFont(&simutFont24pt);
 else _driver.tft->setFont(&simutFont12pt);

 if (isnan(val)) {
 _driver.tft->getTextBounds("--.-", 0, 0, &bx1, &by1, &bw, &bh);
 _driver.tft->setCursor(anchorX - bw, y); _driver.tft->print("--.-");
 } else {
 char iPart[8], dPart[4];
 snprintf(iPart, sizeof(iPart), "%d", (int)val);
 snprintf(dPart, sizeof(dPart), ".%d", abs((int)(val * 10) % 10));
 _driver.tft->getTextBounds(iPart, 0, 0, &bx1, &by1, &bw, &bh);
 _driver.tft->setCursor(anchorX - bw - 2, y); _driver.tft->print(iPart);
 _driver.tft->setCursor(anchorX, y); _driver.tft->print(dPart);
 }


 if (large) {
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setCursor(symbolX, y - 18); _driver.tft->print("o");
 _driver.tft->setFont(&simutFont12pt); _driver.tft->setCursor(symbolX + 8, y); _driver.tft->print("C");
 } else {
 _driver.tft->setFont(NULL); _driver.tft->setCursor(symbolX, y - 12); _driver.tft->print("o");
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setCursor(symbolX + 7, y); _driver.tft->print("C");
 }
 };


 auto drawHum = [&](float val, int anchorX, int y, uint16_t color) {
 int16_t bx1, by1; uint16_t bw, bh;
 char buf[6];
 if (isnan(val)) snprintf(buf, sizeof(buf), "--");
 else snprintf(buf, sizeof(buf), "%d", (int)val);

 _driver.tft->setFont(&simutFont12pt); _driver.tft->setTextColor(color);
 _driver.tft->getTextBounds(buf, 0, 0, &bx1, &by1, &bw, &bh);
 _driver.tft->setCursor(anchorX - bw, y); _driver.tft->print(buf);
 _driver.tft->setTextColor(C_TEXT_SUB); _driver.tft->setCursor(anchorX + 4, y); _driver.tft->print("%");
 };


 if (_graphData.hasHumidity && !isnan(_currentMinHum)) {
 const int cardW = 148, cardH = 96, cardR = 12;
 const int cardY = 62;
 const int leftX = 5, rightX = 167;


 _driver.tft->fillRoundRect(leftX, cardY, cardW, cardH, cardR, C_CARD_BG);
 _driver.tft->drawRoundRect(leftX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEMP_HOT);
 _driver.tft->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor(leftX + (cardW - w) / 2, cardY + 18); _driver.tft->print(tr(TR_MAX_LBL));


 drawTemp(_graphData.realMaxVal, leftX + 68, cardY + 52, C_TEMP_HOT, false);


 _driver.tft->fillCircle(leftX + 25, cardY + 74, 3, C_HUMIDITY);
 drawHum(_currentMaxHum, leftX + 80, cardY + 80, C_HUMIDITY);


 _driver.tft->fillRoundRect(rightX, cardY, cardW, cardH, cardR, C_CARD_BG);
 _driver.tft->drawRoundRect(rightX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEMP_OK);
 _driver.tft->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor(rightX + (cardW - w) / 2, cardY + 18); _driver.tft->print(tr(TR_MIN_LBL));

 drawTemp(_graphData.realMinVal, rightX + 68, cardY + 52, C_TEMP_OK, false);

 _driver.tft->fillCircle(rightX + 25, cardY + 74, 3, C_HUMIDITY);
 drawHum(_currentMinHum, rightX + 80, cardY + 80, C_HUMIDITY);
 }


 else {
 const int cardW = 148, cardH = 96, cardR = 12;
 const int cardY = 62;
 const int leftX = 5, rightX = 167;


 _driver.tft->fillRoundRect(leftX, cardY, cardW, cardH, cardR, C_CARD_BG);
 _driver.tft->drawRoundRect(leftX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEMP_HOT);
 _driver.tft->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor(leftX + (cardW - w) / 2, cardY + 18); _driver.tft->print(tr(TR_MAX_LBL));

 drawTemp(_graphData.realMaxVal, leftX + 55, cardY + 68, C_TEMP_HOT, true);


 _driver.tft->fillRoundRect(rightX, cardY, cardW, cardH, cardR, C_CARD_BG);
 _driver.tft->drawRoundRect(rightX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEMP_OK);
 _driver.tft->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor(rightX + (cardW - w) / 2, cardY + 18); _driver.tft->print(tr(TR_MIN_LBL));

 drawTemp(_graphData.realMinVal, rightX + 55, cardY + 68, C_TEMP_OK, true);
 }


 {
 const char* rangeLabels[] = {"1h", "6h", "24h", "3d", "7d"};
 const char* rangeText = ((_graphData.timeRange >= 0) && (_graphData.timeRange < 5))
 ? rangeLabels[_graphData.timeRange] : "?";
 char periodBuf[16];
 snprintf(periodBuf, sizeof(periodBuf), "[ %s ]", rangeText);
 _driver.tft->setFont(NULL); _driver.tft->setTextSize(1); _driver.tft->setTextColor(C_TEXT_OFF);
 _driver.tft->getTextBounds(periodBuf, 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor(160 - w / 2, 168); _driver.tft->print(periodBuf);
 }


 _driver.tft->fillRoundRect(10, 180, 300, 40, 12, C_ACCENT);


 int icX = 50, icY = 188;
 _driver.tft->fillRect(icX, icY + 8, 4, 12, C_BG_MAIN);
 _driver.tft->fillRect(icX + 6, icY + 2, 4, 18, C_BG_MAIN);
 _driver.tft->fillRect(icX + 12, icY + 6, 4, 14, C_BG_MAIN);
 _driver.tft->drawFastHLine(icX - 2, icY + 20, 20, C_BG_MAIN);


 _driver.tft->setFont(&simutFont12pt); _driver.tft->setTextColor(C_BG_MAIN);
 String btnTxt = tr(TR_PLOT_CHART);
 _driver.tft->getTextBounds(btnTxt, 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor(160 - (w / 2) + 15, 207);
 _driver.tft->print(btnTxt);
}


/* =========================================================================== */
/* IMPROVED GRAPH HELPERS */
/* =========================================================================== */

/**
 * @brief Formats float with 1 decimal place in buffer, without using snprintf %f.
 *
 * snprintf with %f on newlib-nano (RP2040) consumes ~400 bytes of stack
 * internally for float->string conversion, causing stack overflow on
 * Core 1 which has only ~2KB of stack.
 * This function uses only integer arithmetic — zero extra stack consumption.
 *
 * @param buf Output buffer (minimum 10 bytes).
 * @param size Buffer size.
 * @param val Float value to format.
 * @return Pointer to buf (for chaining).
 */

/**
 * @brief Formats float with 2 decimal places in buffer, without using snprintf %f.
 * @param buf Output buffer (minimum 12 bytes).
 * @param size Buffer size.
 * @param val Float value to format.
 * @return Pointer to buf.
 */

/**
 * @brief Formats timestamp for graph X-axis labels.
 *
 * For short ranges (1H, 6H, 12H) shows only HH:MM.
 * For long ranges (24H, 7D) shows DD/MM HHh.
 *
 * @param epoch Unix timestamp of the point.
 * @param buf Output buffer (minimum 12 bytes).
 * @param shortRange true for short format (HH:MM), false for long (DD/MM HHh).
 */
void DisplayManager::formatGraphTime(time_t epoch, char* buf, bool shortRange) {
 struct tm ti;
 localtime_r(&epoch, &ti);
 /* Always display full date and time, regardless of interval */
 (void)shortRange;
 snprintf(buf, 12, "%02d/%02d %02d:%02d", ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min);
}

/**
 * @brief Draws a diamond marker (lozenge) with a floating value label.
 *
 * The diamond has ~4px radius. The label is positioned above or below
 * according to the 'above' parameter, with automatic flipping if it
 * exceeds the vertical limits of the graph area.
 *
 * @param cx X coordinate of the diamond center.
 * @param cy Y coordinate of the diamond center.
 * @param color Marker and label color.
 * @param value Numeric value to display in the label.
 * @param above true = label above the point, false = below.
 * @param unit Unit suffix (e.g. "C", "%").
 * @param graphTop Upper limit of the graph area (clip).
 * @param graphBot Lower limit of the graph area (clip).
 */
void DisplayManager::drawPeakMarker(int16_t cx, int16_t cy, uint16_t color,
 float value, bool above, const char* unit,
 int16_t graphTop, int16_t graphBot) {
 /* Vertical clamp to stay within graph area */
 if (cy < graphTop + 3) cy = graphTop + 3;
 if (cy > graphBot - 3) cy = graphBot - 3;

 /* Filled diamond (lozenge 4px radius) */
 const int r = 4;
 for (int dy = -r; dy <= r; dy++) {
 int span = r - abs(dy);
 _driver.tft->drawFastHLine(cx - span, cy + dy, span * 2 + 1, color);
 }
 /* Center pixel for contrast */
 _driver.tft->drawPixel(cx, cy, C_BG_MAIN);

 /* Format value label */
 static char valBuf[16];
 static char fBuf[10];
 fmtFloat1(fBuf, sizeof(fBuf), value);
 snprintf(valBuf, sizeof(valBuf), "%s%s", fBuf, unit);

 _driver.tft->setFont(NULL);
 _driver.tft->setTextSize(1);
 int16_t bx, by;
 uint16_t bw, bh;
 _driver.tft->getTextBounds(valBuf, 0, 0, &bx, &by, &bw, &bh);

 /* Vertical positioning with automatic flip */
 int16_t labelX = cx - (int16_t)(bw / 2);
 int16_t labelY;
 if (above) {
 labelY = cy - r - (int16_t)bh - 3;
 if (labelY < graphTop) labelY = cy + r + 3; /* Flip down */
 } else {
 labelY = cy + r + 3;
 if (labelY + (int16_t)bh > graphBot) labelY = cy - r - (int16_t)bh - 3; /* Flip up */
 }

 /* Horizontal clamp to stay on screen */
 if (labelX < 2) labelX = 2;
 if (labelX + (int16_t)bw > 318) labelX = 318 - (int16_t)bw;

 /* Opaque background for readability over the curve */
 _driver.tft->fillRect(labelX - 1, labelY - 1, bw + 2, bh + 2, C_BG_MAIN);
 _driver.tft->setTextColor(color);
 _driver.tft->setCursor(labelX, labelY);
 _driver.tft->print(valBuf);
}


/* =========================================================================== */
/* IMPROVED HISTORY GRAPH SCREEN */
/* =========================================================================== */
/**
 * @brief Draws the complete history graph screen with visual improvements.
 *
 * Improvements over the previous version:
 * - Top info bar with MAX/MIN badges containing value + timestamp
 * - If humidity present, additional H.MAX and H.MIN badges
 * - X axis with 3 time labels (start, middle, end of period)
 * - Adaptive format: HH:MM for <=12H, DD/MM HHh for 24H and 7D
 * - Diamond markers at curve peak and valley points
 * - Floating labels at extremes with automatic flip if out of area
 * - Y axis with 5 divisions and decimal values
 * - Humidity Y axis with intermediate values (top, middle, base)
 * - Graph line with 2px thickness
 */
void DisplayManager::drawGraphScreen( ) {
 __dmb( );
 if (!_driver.canvas) return;

 if (_graphData.count < 0 || _graphData.count > GRAPH_WIDTH) {
 _driver.tft->fillScreen(C_BG_MAIN);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_SUB);
 _driver.tft->setCursor(60, 120); _driver.tft->print(tr(TR_ERROR_LBL));
 drawPeriodButtons( );
 return;
 }

 bool shortRange = (_graphData.timeRange <= 3); /* 1H..24H = HH:MM, 7D = DD/MM */
 bool hasHum = _graphData.hasHumidity && !isnan(_currentMinHum);
 bool hasData = (_graphData.count >= 2 && _graphData.idxMaxTemp >= 0);

 /*
 * Maximized layout: graph occupies header(28)..buttons(195).
 * Internal Y margin of 2px — curve touches almost the edges.
 * Y labels: only MAX (top) and MIN (base).
 */
 const int gx = 30; /* Left margin (Y labels) */
 const int gy = 30; /* Grid top */
 const int gw = hasHum ? 250 : 285; /* Grid width */
 const int gh = 155; /* Grid height */
 const int margin = 2; /* Internal graph clearance */
 const int timeAxisY = gy + gh + 2; /* X axis labels */

 float tempRange = 2.0f;
 float humMin = 0, humMax = 100, humRange = 5.0f;

 /*
 * Y scale: uses realMinVal/realMaxVal computed from ALL records
 * in the time window, not just from the decimated display points.
 * Ensures the Y axis represents the true extreme values.
 */
 if (hasData) {
 tempRange = _graphData.realMaxVal - _graphData.realMinVal;
 if (tempRange < 0.001f) tempRange = 1.0f; /* Constant value -> line in middle */
 if (hasHum) {
 humMin = _currentMinHum; humMax = _currentMaxHum;
 humRange = humMax - humMin;
 if (humRange < 0.001f) humRange = 1.0f;
 }
 }

 /* ── Precompute curve coordinates ── */
 static int16_t pxV1[GRAPH_WIDTH], pyV1[GRAPH_WIDTH], pyV2[GRAPH_WIDTH];
 if (hasData) {
 /*
 * X position by index: data always fills the entire grid width.
 * Y position by realMinVal/realMaxVal: real scale of all records.
 * Header and X labels use tsCutoff/tsEnd to show the time window.
 */
 for (int i = 0; i < _graphData.count; i++) {
 pxV1[i] = gx + (int)((long)i * gw / max(1, _graphData.count - 1));

 /* NaN points (sensor in error) -> pyV1 = -1 to create visible gap */
 if (isnan(_graphData.pointsV1[i])) {
 pyV1[i] = -1;
 } else {
 int y = gy + margin + (int)((_graphData.realMaxVal - _graphData.pointsV1[i]) / tempRange * (gh - 2 * margin));
 if (y < gy) y = gy;
 if (y > gy + gh) y = gy + gh;
 pyV1[i] = y;
 }

 if (hasHum && !isnan(_graphData.pointsV2[i])) {
 int yh = gy + margin + (int)((humMax - _graphData.pointsV2[i]) / humRange * (gh - 2 * margin));
 if (yh < gy) yh = gy;
 if (yh > gy + gh) yh = gy + gh;
 pyV2[i] = yh;
 } else {
 pyV2[i] = -1;
 }
 }
 }

 /* ── Pre-format texts ── */
 static char maxLbl[10], minLbl[10];
 static char humMaxLbl[8], humMinLbl[8];

 if (hasData) {
 fmtFloat1(maxLbl, sizeof(maxLbl), _graphData.realMaxVal);
 fmtFloat1(minLbl, sizeof(minLbl), _graphData.realMinVal);
 if (hasHum) {
 snprintf(humMaxLbl, sizeof(humMaxLbl), "%d%%", (int)humMax);
 snprintf(humMinLbl, sizeof(humMinLbl), "%d%%", (int)humMin);
 }
 }

 /* ═══════════════════════════════════════════════════════════════ */
 /* STRIP RENDERING: everything on 320x45 canvas */
 /* ═══════════════════════════════════════════════════════════════ */
 GFXcanvas16* cv = _driver.canvas;
 const int sH = 45;

 for (int s = 0; s * sH < 195; s++) {
 int sTop = s * sH;
 int h = sH;
 if (sTop + h > 195) h = 195 - sTop;
 int sBot = sTop + h;

 cv->fillScreen(C_BG_MAIN);

 if (hasData) {
 /* ── Axes ── */
 if (gy < sBot && gy + gh > sTop) {
 int at = (gy > sTop) ? gy - sTop : 0;
 int ab = (gy + gh < sBot) ? gy + gh - sTop : h;
 cv->drawFastVLine(gx, at, ab - at, C_AXIS);
 if (gy + gh >= sTop && gy + gh < sBot)
 cv->drawFastHLine(gx, gy + gh - sTop, gw, C_AXIS);
 if (hasHum)
 cv->drawFastVLine(gx + gw, at, ab - at, C_AXIS);
 }

 /* ── Dotted horizontal grid (4 divisions) ── */
 for (int gi = 0; gi <= 4; gi++) {
 int lineY = gy + (gh * gi / 4);
 if (lineY >= sTop && lineY < sBot) {
 int ry = lineY - sTop;
 for (int x = gx + 2; x < gx + gw; x += 6) {
 cv->drawPixel(x, ry, C_GRID);
 cv->drawPixel(x + 1, ry, C_GRID);
 }
 }
 }

 /* ── Vertical grid ── */
 if (gy < sBot && gy + gh > sTop) {
 int gt = (gy > sTop) ? gy - sTop : 0;
 int gb = (gy + gh < sBot) ? gy + gh - sTop : h;
 for (int x = gx; x < gx + gw; x += 40)
 cv->drawFastVLine(x, gt, gb - gt, C_GRID);
 }

 /* ── Y axis labels: MAX aligned to top, MIN to grid base ── */
 cv->setFont(NULL); cv->setTextSize(1);
 /* header bar ends at y=31. lyMax = gy (=30) was 2px under the header
 * - 5x7 font starts at y and occupies y..y+7, so the first 2 lines
 * were clipped. +3 px places label at y=33..40, fully below header. */
 int lyMax = gy + 3; /* Grid top + header margin */
 int lyMin = gy + gh - 8; /* Grid base = graph valley */
 /* Intersection condition: label visible if any part crosses the strip */
 if (lyMax < sBot && lyMax + 8 > sTop) {
 cv->setTextColor(C_TEMP_HOT);
 cv->setCursor(1, lyMax - sTop);
 cv->print(maxLbl);
 }
 if (lyMin < sBot && lyMin + 8 > sTop) {
 cv->setTextColor(C_TEMP_OK);
 cv->setCursor(1, lyMin - sTop);
 cv->print(minLbl);
 }

 /* ── Humidity Y axis labels (right side) ── */
 if (hasHum) {
 int rxAxis = gx + gw;
 cv->setTextColor(C_HUMIDITY);
 if (lyMax < sBot && lyMax + 8 > sTop) {
 cv->setCursor(rxAxis + 3, lyMax - sTop);
 cv->print(humMaxLbl);
 }
 if (lyMin < sBot && lyMin + 8 > sTop) {
 cv->setCursor(rxAxis + 3, lyMin - sTop);
 cv->print(humMinLbl);
 }
 }

 /* ── Temperature curve (2px) — skip gaps (pyV1 == -1) ── */
 for (int i = 0; i < _graphData.count - 1; i++) {
 if (pyV1[i] < 0 || pyV1[i + 1] < 0) continue; /* Gap: sensor in error */
 int y1 = pyV1[i], y2 = pyV1[i + 1];
 int yMn = (y1 < y2) ? y1 : y2;
 int yMx = (y1 > y2) ? y1 : y2;
 if (yMx < sTop || yMn >= sBot) continue;
 cv->drawLine(pxV1[i], y1 - sTop, pxV1[i+1], y2 - sTop, C_TEMP_HOT);
 cv->drawLine(pxV1[i], y1 - sTop + 1, pxV1[i+1], y2 - sTop + 1, C_TEMP_HOT);
 }

 /* ── Humidity curve (1px) ── */
 if (hasHum) {
 for (int i = 0; i < _graphData.count - 1; i++) {
 if (pyV2[i] < 0 || pyV2[i+1] < 0) continue;
 int y1 = pyV2[i], y2 = pyV2[i + 1];
 int yMn = (y1 < y2) ? y1 : y2;
 int yMx = (y1 > y2) ? y1 : y2;
 if (yMx < sTop || yMn >= sBot) continue;
 cv->drawLine(pxV1[i], y1 - sTop, pxV1[i+1], y2 - sTop, C_HUMIDITY);
 }
 }

 /* ── Last valid value marker ── */
 {
 /* Find the last valid (non-NaN) point for the marker */
 int lastValidIdx = -1;
 for (int i = _graphData.count - 1; i >= 0; i--) {
 if (pyV1[i] >= 0) { lastValidIdx = i; break; }
 }
 if (lastValidIdx >= 0) {
 int ly = pyV1[lastValidIdx];
 if (ly - 3 < sBot && ly + 3 >= sTop) {
 cv->fillCircle(gx + gw, ly - sTop, 3, C_TEXT_MAIN);
 cv->fillCircle(gx + gw, ly - sTop, 1, C_BG_MAIN);
 }
 }
 }

 /* ── Peak/valley markers (diamond) — skip if point is NaN ── */
 auto drawDiamond = [&](int dx, int dy, uint16_t color) {
 if (dy < 0) return; /* NaN point: no marker */
 if (dy - 3 >= sBot || dy + 3 < sTop) return;
 int ry = dy - sTop;
 for (int dd = -3; dd <= 3; dd++) {
 int span = 3 - abs(dd);
 if (ry + dd >= 0 && ry + dd < h)
 cv->drawFastHLine(dx - span, ry + dd, span * 2 + 1, color);
 }
 if (ry >= 0 && ry < h) cv->drawPixel(dx, ry, C_BG_MAIN);
 };

 if (_graphData.idxMaxTemp >= 0 && _graphData.idxMaxTemp < _graphData.count)
 drawDiamond(pxV1[_graphData.idxMaxTemp], pyV1[_graphData.idxMaxTemp], C_TEMP_HOT);
 if (_graphData.idxMinTemp >= 0 && _graphData.idxMinTemp < _graphData.count)
 drawDiamond(pxV1[_graphData.idxMinTemp], pyV1[_graphData.idxMinTemp], C_TEMP_OK);

 } else {
 /* No data */
 if (120 >= sTop && 130 < sBot) {
 cv->setFont(&simutFont12pt); cv->setTextColor(C_TEXT_SUB);
 String nd = tr(TR_NO_DATA);
 int16_t nbx, nby; uint16_t nw, nh;
 cv->getTextBounds(nd, 0, 0, &nbx, &nby, &nw, &nh);
 cv->setCursor(160 - nw / 2, 125 - sTop); cv->print(nd);
 }
 }

 /*
 * X axis labels (3 timestamps) — correspond to the extremes of the
 * drawn line (tsFirst..tsLast). The header shows the full window
 * (tsCutoff..tsEnd) for zoom context.
 */
 if (timeAxisY < sBot && timeAxisY + 8 > sTop && _graphData.tsFirst > 0) {
 int ry = timeAxisY - sTop;
 cv->setFont(NULL); cv->setTextSize(1); cv->setTextColor(C_TEXT_SUB);

 static char xL[6], xM[6], xR[6];
 struct tm ti;

 time_t tMid = _graphData.tsFirst + (_graphData.tsLast - _graphData.tsFirst) / 2;

 /* First point */
 localtime_r(&_graphData.tsFirst, &ti);
 if (shortRange) snprintf(xL, sizeof(xL), "%02d:%02d", ti.tm_hour, ti.tm_min);
 else snprintf(xL, sizeof(xL), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
 cv->setCursor(gx, ry); cv->print(xL);

 /* Midpoint */
 localtime_r(&tMid, &ti);
 if (shortRange) snprintf(xM, sizeof(xM), "%02d:%02d", ti.tm_hour, ti.tm_min);
 else snprintf(xM, sizeof(xM), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
 int16_t tbx, tby; uint16_t tw, th;
 cv->getTextBounds(xM, 0, 0, &tbx, &tby, &tw, &th);
 cv->setCursor(gx + gw / 2 - (int)tw / 2, ry); cv->print(xM);

 /* Last point */
 localtime_r(&_graphData.tsLast, &ti);
 if (shortRange) snprintf(xR, sizeof(xR), "%02d:%02d", ti.tm_hour, ti.tm_min);
 else snprintf(xR, sizeof(xR), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
 cv->getTextBounds(xR, 0, 0, &tbx, &tby, &tw, &th);
 cv->setCursor(gx + gw - (int)tw, ry); cv->print(xR);
 }

 /* Header drawn LAST on strip 0 — its internal fillRect
 * overwrites any bleed from axes/grid in canvas y=4..31. External
 * strip blit will cover the entire region. */
 if (sTop == 0) {
 drawGraphHeaderBar(/*blitNow=*/false);
 }

 blitCanvas(cv, 0, sTop, 320, h);
 }

 drawPeriodButtons( );
}


/* =========================================================================== */
/* PERIOD DETAIL NUMERIC SCREEN */
/* =========================================================================== */
/**
 * @brief Draws screen with legible numeric data for the selected period.
 *
 * Displays in large cards: MAX, MIN, AVG, stddev, and period.
 * Keeps header with title/X button and period buttons at the bottom.
 * Tap on the central zone returns to the graph.
 * All floats formatted via fmtFloat1/fmtFloat2 (without snprintf %f).
 */
void DisplayManager::drawGraphDetailScreen( ) {
 __dmb( );
 if (!_driver.canvas) return;

 bool shortRange = (_graphData.timeRange <= 3); /* 1H..24H = HH:MM, 7D = DD/MM */
 bool hasHum = _graphData.hasHumidity && !isnan(_currentMinHum);
 bool isHumPage = (_detailPage == 1 && hasHum);

 int16_t bx, by; uint16_t bw, bh;

 struct CardData {
 const char* label;
 char num[14];
 bool isTempUnit; /* true = degree-C with circle, false = tr(TR_HUM_SUFFIX) */
 char sub[12];
 uint16_t numColor;
 int icon;
 };
 static CardData cards[4];

 if (_graphData.count < 2) {
 _driver.tft->fillRect(4, 4, 312, 191, C_BG_MAIN);
 drawGraphHeaderBar( ); /* Shows reference period in header */
 _driver.tft->setFont(&simutFont12pt); _driver.tft->setTextColor(C_TEXT_SUB);
 String nd = tr(TR_NO_DATA);
 _driver.tft->getTextBounds(nd, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(160 - bw / 2, 120); _driver.tft->print(nd);
 drawPeriodButtons( );
 return;
 }

 /* ── Populate cards ── */
 if (!isHumPage) {
 cards[0] = { tr(TR_MAX_LBL), {0}, true, {0}, C_TEMP_OK, 0 };
 fmtFloat1(cards[0].num, sizeof(cards[0].num), _graphData.realMaxVal);
 if (_graphData.tsRealMax > 0) formatGraphTime(_graphData.tsRealMax, cards[0].sub, shortRange);

 cards[1] = { tr(TR_MIN_LBL), {0}, true, {0}, C_TEMP_OK, 1 };
 fmtFloat1(cards[1].num, sizeof(cards[1].num), _graphData.realMinVal);
 if (_graphData.tsRealMin > 0) formatGraphTime(_graphData.tsRealMin, cards[1].sub, shortRange);

 cards[2] = { tr(TR_AVG_LBL), {0}, true, {0}, C_TEMP_OK, 2 };
 fmtFloat1(cards[2].num, sizeof(cards[2].num), _graphData.avgTemp);

 cards[3] = { tr(TR_STD_LBL), {0}, true, {0}, C_TEMP_OK, 3 };
 fmtFloat2(cards[3].num, sizeof(cards[3].num), _graphData.stdTemp);
 } else {
 cards[0] = { tr(TR_MAX_LBL), {0}, false, {0}, C_HUMIDITY, 0 };
 snprintf(cards[0].num, sizeof(cards[0].num), "%d", (int)_currentMaxHum);
 if (_graphData.tsMaxHum > 0) formatGraphTime(_graphData.tsMaxHum, cards[0].sub, shortRange);

 cards[1] = { tr(TR_MIN_LBL), {0}, false, {0}, C_HUMIDITY, 1 };
 snprintf(cards[1].num, sizeof(cards[1].num), "%d", (int)_currentMinHum);
 if (_graphData.tsMinHum > 0) formatGraphTime(_graphData.tsMinHum, cards[1].sub, shortRange);

 cards[2] = { tr(TR_AVG_LBL), {0}, false, {0}, C_HUMIDITY, 2 };
 if (!isnan(_graphData.avgHum)) snprintf(cards[2].num, sizeof(cards[2].num), "%d", (int)_graphData.avgHum);
 else snprintf(cards[2].num, sizeof(cards[2].num), "--");

 cards[3] = { tr(TR_STD_LBL), {0}, false, {0}, C_HUMIDITY, 3 };
 if (!isnan(_graphData.stdHum)) fmtFloat2(cards[3].num, sizeof(cards[3].num), _graphData.stdHum);
 else snprintf(cards[3].num, sizeof(cards[3].num), "--");
 }

 /* ── Layout: 2 rows x 2 columns, larger cards ── */
 const int cardW = 152, cardH = 76, cardR = 8;
 const int colL = 4, colR = 164, gapY = 4;
 const int totalH = 2 * cardH + gapY;
 const int startY = 28 + (167 - totalH) / 2;
 int rowY[2] = { startY, startY + cardH + gapY };

 /**
 * Draws card on canvas (expanded version).
 * - Refined 18x18 icon
 * - Label in FreeSansBold9pt7b, color C_TEXT_SUB (light gray)
 * - Large colored value (green temp / blue hum)
 * - Unit: temp = degree circle "o" (NULL font) + "C" (9pt) white
 * hum = tr(TR_HUM_SUFFIX) (9pt) white
 * - Event date/time at card bottom (FreeSansBold9pt7b, soft yellow)
 */
 auto drawCardOn = [&](GFXcanvas16* cv, int cx, int cy, int stripTop, int idx) {
 int ry = cy - stripTop;
 CardData& d = cards[idx];

 cv->fillRoundRect(cx, ry, cardW, cardH, cardR, C_CARD_BG);

 /* Soft yellow for event date/time */
 const uint16_t C_DATETIME = RGB565(190, 170, 60);

 /* ── 18x18 Icon ── */
 int ix = cx + 6, iy = ry + 2;
 uint16_t ic = d.numColor;
 switch (d.icon) {
 case 0: { /* ▲ MAX — ascending triangle with inner outline */
 cv->fillTriangle(ix, iy+16, ix+9, iy+1, ix+17, iy+16, ic);
 cv->drawTriangle(ix+2, iy+15, ix+9, iy+4, ix+15, iy+15, C_CARD_BG);
 break;
 }
 case 1: { /* ▼ MIN — descending triangle with inner outline */
 cv->fillTriangle(ix, iy+1, ix+9, iy+16, ix+17, iy+1, ic);
 cv->drawTriangle(ix+2, iy+2, ix+9, iy+13, ix+15, iy+2, C_CARD_BG);
 break;
 }
 case 2: { /* ≈ AVG — three proportional horizontal bars */
 cv->fillRect(ix, iy+1, 17, 3, ic);
 cv->fillRect(ix, iy+7, 17, 3, ic);
 cv->fillRect(ix, iy+13, 17, 3, ic);
 break;
 }
 case 3: { /* sigma STDDEV — refined 18x18 bell curve */
 /* Curve top */
 cv->drawPixel(ix+8, iy+1, ic); cv->drawPixel(ix+9, iy+1, ic);
 cv->drawPixel(ix+7, iy+2, ic); cv->drawPixel(ix+10, iy+2, ic);
 cv->drawPixel(ix+6, iy+3, ic); cv->drawPixel(ix+11, iy+3, ic);
 /* Shoulders */
 cv->drawPixel(ix+5, iy+4, ic); cv->drawPixel(ix+12, iy+4, ic);
 cv->drawPixel(ix+5, iy+5, ic); cv->drawPixel(ix+12, iy+5, ic);
 cv->drawPixel(ix+4, iy+6, ic); cv->drawPixel(ix+13, iy+6, ic);
 cv->drawPixel(ix+4, iy+7, ic); cv->drawPixel(ix+13, iy+7, ic);
 /* Body */
 cv->drawPixel(ix+3, iy+8, ic); cv->drawPixel(ix+14, iy+8, ic);
 cv->drawPixel(ix+3, iy+9, ic); cv->drawPixel(ix+14, iy+9, ic);
 cv->drawPixel(ix+2, iy+10, ic); cv->drawPixel(ix+15, iy+10, ic);
 cv->drawPixel(ix+2, iy+11, ic); cv->drawPixel(ix+15, iy+11, ic);
 /* Wide base */
 cv->drawPixel(ix+1, iy+12, ic); cv->drawPixel(ix+16, iy+12, ic);
 cv->drawPixel(ix+1, iy+13, ic); cv->drawPixel(ix+16, iy+13, ic);
 cv->drawPixel(ix, iy+14, ic); cv->drawPixel(ix+17, iy+14, ic);
 /* Solid baseline */
 cv->fillRect(ix, iy+15, 18, 2, ic);
 break;
 }
 }

 /* ── Label (FreeSansBold9pt7b, light gray, to the right of icon) ── */
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TEXT_SUB);
 cv->setCursor(ix + 22, iy + 14);
 cv->print(d.label);

 /* ── Value + Unit (card vertical center) ── */
 int vy = ry + 48;

 /* Measure number width */
 int16_t nb, ny2; uint16_t nw, nh;
 cv->setFont(&simutFont12pt);
 cv->getTextBounds(d.num, 0, 0, &nb, &ny2, &nw, &nh);

 if (d.isTempUnit) {
 /*
 * Temperature: number + "o" (degree circle, NULL font above) + "C" (9pt)
 * Same pattern as the main screen (drawTemp).
 */
 int16_t ub2, uy3; uint16_t cw2, ch2;
 cv->setFont(&simutFont9pt);
 cv->getTextBounds("C", 0, 0, &ub2, &uy3, &cw2, &ch2);

 /* "o" in NULL font is ~6px wide */
 int unitW = 6 + 1 + (int)cw2; /* "o" + gap + "C" */
 int totalW = (int)nw + 2 + unitW;
 int vx = cx + (cardW - totalW) / 2;

 /* Number (colored) */
 cv->setFont(&simutFont12pt);
 cv->setTextColor(d.numColor);
 cv->setCursor(vx, vy);
 cv->print(d.num);

 /* Degree circle "o" (NULL font, white, positioned above baseline) */
 int oX = vx + (int)nw + 2;
 cv->setFont(NULL); cv->setTextSize(1);
 cv->setTextColor(C_TEXT_MAIN);
 cv->setCursor(oX, vy - 16);
 cv->print("o");

 /* "C" (FreeSansBold9pt7b, white) */
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TEXT_MAIN);
 cv->setCursor(oX + 7, vy);
 cv->print("C");

 } else {
 /*
 * Humidity: number + tr(TR_HUM_SUFFIX) (9pt, white)
 */
 int16_t ub2, uy3; uint16_t uw2, uh2;
 cv->setFont(&simutFont9pt);
 cv->getTextBounds(tr(TR_HUM_SUFFIX), 0, 0, &ub2, &uy3, &uw2, &uh2);

 int totalW = (int)nw + 3 + (int)uw2;
 int vx = cx + (cardW - totalW) / 2;

 /* Number (colored) */
 cv->setFont(&simutFont12pt);
 cv->setTextColor(d.numColor);
 cv->setCursor(vx, vy);
 cv->print(d.num);

 /* Humidity suffix (white) */
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TEXT_MAIN);
 cv->setCursor(vx + (int)nw + 3, vy);
 cv->print(tr(TR_HUM_SUFFIX));
 }

 /* ── Event date/time (card bottom, centered, soft yellow) ── */
 if (d.sub[0]) {
 int16_t sx, sy; uint16_t sw, sh;
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_DATETIME);
 cv->getTextBounds(d.sub, 0, 0, &sx, &sy, &sw, &sh);
 cv->setCursor(cx + (cardW - (int)sw) / 2, ry + cardH - 6);
 cv->print(d.sub);
 }
 };

 /* ═══════════════════════════════════════════════════════════════ */
 /* STRIP RENDERING */
 /* ═══════════════════════════════════════════════════════════════ */
 GFXcanvas16* cv = _driver.canvas;
 const int sH = 45;

 for (int s = 0; s * sH < 195; s++) {
 int sTop = s * sH;
 int h = sH;
 if (sTop + h > 195) h = 195 - sTop;

 cv->fillScreen(C_BG_MAIN);

 /* Only 2 rows of cards */
 for (int r = 0; r < 2; r++) {
 int cy = rowY[r];
 if (cy < sTop + h && cy + cardH > sTop) {
 drawCardOn(cv, colL, cy, sTop, r * 2);
 drawCardOn(cv, colR, cy, sTop, r * 2 + 1);
 }
 }

 /* Header drawn LAST on strip 0 — overwrites any
 * card pixel that may have entered the header zone. */
 if (sTop == 0) {
 drawGraphHeaderBar(/*blitNow=*/false);
 }

 blitCanvas(cv, 0, sTop, 320, h);
 }

 drawPeriodButtons( );
}
