/**
 * @file DisplayManager_Calendar.cpp
 * @brief Calendar screen: state setters + drawCalendarScreen.
 * @details Sub-file of DisplayManager.cpp.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "UiWidgets.h"

void DisplayManager::showCalendar(int year, int month, uint32_t daysMask) {
 mutex_enter_blocking(&_stateMutex);
 _calYear = year;
 _calMonth = month;
 _calDaysMask = daysMask;
 _uiMode = MODE_CALENDAR;
 __dmb( );
 _repaintCalendar = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::setCalendarDays(uint32_t daysMask) {
 mutex_enter_blocking(&_stateMutex);
 _calDaysMask = daysMask;
 _repaintCalendar = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::setGraphNavOffset(int offset) {
 mutex_enter_blocking(&_stateMutex);
 _graphNavOffset = offset;
 mutex_exit(&_stateMutex);
}


void DisplayManager::drawCalendarScreen( ) {
 if (!_driver.canvas) return;

 const char* monthNames[] = {
 "Jan","Fev","Mar","Abr","Mai","Jun",
 "Jul","Ago","Set","Out","Nov","Dez"
 };
 const char* dowHeaders[] = {"D","S","T","Q","Q","S","S"};

 /* Calculate first day of week and total days in month */
 struct tm firstTm = {};
 firstTm.tm_year = _calYear - 1900;
 firstTm.tm_mon = _calMonth - 1;
 firstTm.tm_mday = 1;
 mktime(&firstTm);
 int firstDow = firstTm.tm_wday; /* 0 = Sunday */

 /* Days in month: day 0 of next month = last day of current */
 struct tm lastTm = {};
 lastTm.tm_year = _calYear - 1900;
 lastTm.tm_mon = _calMonth; /* next month */
 lastTm.tm_mday = 0;
 mktime(&lastTm);
 int daysInMonth = lastTm.tm_mday;

 /* Today (for highlight) */
 time_t now = time(nullptr);
 struct tm nowTm;
 localtime_r(&now, &nowTm);
 int todayDay = (nowTm.tm_year + 1900 == _calYear &&
 nowTm.tm_mon + 1 == _calMonth) ? nowTm.tm_mday : -1;

 /* ═══ STRIP RENDERING ═══ */
 GFXcanvas16* cv = _driver.canvas;
 const int sH = 45;

 for (int s = 0; s * sH < 195; s++) {
 int sTop = s * sH;
 int h = sH;
 if (sTop + h > 195) h = 195 - sTop;

 cv->fillScreen(C_BG_MAIN);

 /* ── Header (y=0..27) ── */
 if (sTop == 0) {
 uiTitleBar(cv, 4, nullptr, -1, 0, 28);

 /* Month navigation lives ONLY in the bottom bar — the header chevrons
  * duplicated it and crowded the close button. The touch zones at the
  * header corners still work (harmless hidden shortcut). */

 /* Title "Abr 2026" */
 char titleBuf[16];
 snprintf(titleBuf, sizeof(titleBuf), "%s %d",
 monthNames[_calMonth - 1], _calYear);
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TITLE_TEXT);
 int16_t bx, by; uint16_t bw, bh;
 cv->getTextBounds(titleBuf, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(160 - bw / 2 - bx, 20);
 cv->print(titleBuf);

 /* X button (back to graph) — the STANDARD close rect (32x24 flush
  * right, centered in the 28 px bar), same as the graph header. It
  * inherited 24x24 at x=270 from when the ">" chevron sat beside it;
  * with the chevron gone it fills that corner. Touch zone (x>=270,
  * y<28) already covers it. */
 uiCloseX(cv, 284, 6, 32, 24);
 }

 /* ── Day-of-week headers (y=30..42) ── */
 if (sTop <= 30 && sTop + h > 30) {
 int ry = 34 - sTop;
 cv->setFont(NULL);
 cv->setTextSize(1);
 cv->setTextColor(C_TEXT_OFF);
 for (int d = 0; d < 7; d++) {
 cv->setCursor(10 + d * 44, ry);
 cv->print(dowHeaders[d]);
 }
 }

 /* ── Day grid (y=44..190) ── */
 const int gridStartY = 46;
 const int cellW = 44, cellH = 24;

 for (int cell = 0; cell < 42; cell++) {
 int dayNum = cell - firstDow + 1;
 if (dayNum < 1 || dayNum > daysInMonth) continue;

 int row = cell / 7;
 int col = cell % 7;
 int cx = 10 + col * cellW + cellW / 2;
 int cy = gridStartY + row * cellH + cellH / 2;

 /* Check if this day is in the current strip */
 if (cy - 8 >= sTop + h || cy + 12 < sTop) continue;

 int ry = cy - sTop; /* Canvas-relative coordinate */

 bool hasData = (_calDaysMask & (1UL << dayNum)) != 0;
 bool isToday = (dayNum == todayDay);

 /* Today highlight */
 if (isToday) {
 cv->fillRoundRect(cx - 16, ry - 9, 32, 20, 5, C_ACCENT);
 cv->setTextColor(C_BG_MAIN);
 } else {
 cv->setTextColor(hasData ? C_TEXT_MAIN : C_TEXT_OFF);
 }

 /* Day number */
 cv->setFont(&simutFont9pt);
 cv->setTextSize(1);
 char dayStr[4];
 snprintf(dayStr, sizeof(dayStr), "%d", dayNum);
 int16_t bx, by; uint16_t bw, bh;
 cv->getTextBounds(dayStr, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(cx - bw / 2 - bx, ry + bh / 2 - by - bh);
 cv->print(dayStr);

 /* Data indicator dot */
 if (hasData && !isToday) {
 cv->fillCircle(cx, ry + 10, 2, C_ACCENT);
 }
 }

 blitCanvas(cv, 0, sTop, 320, h);
 }

 /* ── Bottom bar: [◀ Month] [Today] [Month ▶] ── */
 cv->fillScreen(C_BG_MAIN);
 uiButton(cv, 5, 3, 98, 36, "< M\xEAs", UI_BTN_SECONDARY);
 uiButton(cv, 108, 3, 104, 36, "Hoje", UI_BTN_PRIMARY);
 uiButton(cv, 217, 3, 98, 36, "M\xEAs >", UI_BTN_SECONDARY);

 /* 4 px bottom margin: y=195 + h=41 = 236 ≤ 236 */
 blitCanvas(cv, 0, 195, 320, 41);
}
