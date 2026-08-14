/**
 * @file DisplayManager_Settings.cpp
 * @brief Settings screens: themes, alarms, main menu, password, sounds, system status.
 * @details Sub-file of DisplayManager.cpp.
 * Also includes sound preview helpers (consume + request) and
 * setTelemetryPending/setTelemetrySendStatus. Touch helpers
 * (acceptTouch/Hold/Slide) are in core along with handleTouch.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "SensorManager.h"
#include "DisplayManager_Fonts.h"
#include "DisplayManager_FmtFloat.h"
#include "LogManager.h"
#include "StorageManager.h" /* getBoardSerialNumber in System Status */
#include "UiWidgets.h"
#include "PasswordKeyboard.h"

void DisplayManager::showSettingsThemes(int currentThemeIdx) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_THEMES;
 _previewThemeIdx = currentThemeIdx;
 _themePage = currentThemeIdx / 4;
 _forceSettingsRedraw = true; _lastThemePage = -1; _lastPreviewThemeIdx = -1; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsThemes( ) {
 if(!_driver.canvas) return;
 bool fullRedraw = _forceSettingsRedraw;
 bool pageChanged = (_themePage != _lastThemePage);
 int totalThemes = getThemeCount( );
 int totalPages = (totalThemes + 3) / 4;
 if (_themePage >= totalPages) _themePage = totalPages - 1;
 if (_themePage < 0) _themePage = 0;

 if (fullRedraw) {
 fastClearScreen(C_BG_MAIN);
 blitTitleBar(tr(TR_CONFIG_THEMES));
 blitFooterMenu(tr(TR_BACK), tr(TR_APPLY)); /* T1.2: no heap */
 }

 if (fullRedraw || pageChanged) {
 uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, _themePage);
 }

 int startIdx = _themePage * 4;
 int yBase = 40; int itemW = 285;
 for (int i = 0; i < 4; i++) {
 int actualIdx = startIdx + i; int y = yBase + (i * 38);
 if (!fullRedraw && !pageChanged) { if (actualIdx != _previewThemeIdx && actualIdx != _lastPreviewThemeIdx) continue; }
 _driver.canvas->fillScreen(C_BG_MAIN);
 if (actualIdx < totalThemes) {
 bool isSelected = (actualIdx == _previewThemeIdx);
 uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
 uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
 if (!isSelected) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(txt);
 const ThemePalette* tp = getThemePalette(actualIdx);
 _driver.canvas->setCursor(10, 24); _driver.canvas->print(tp->displayName);
 int pX = itemW - 55; int pY = 9;
 _driver.canvas->fillRect(pX, pY, 16, 16, tp->bgMain);
 _driver.canvas->fillRect(pX + 16, pY, 16, 16, tp->cardBg);
 _driver.canvas->fillRect(pX + 32, pY, 16, 16, tp->accent);
 if (isSelected) _driver.canvas->drawRect(pX-1, pY-1, 49, 18, C_BG_MAIN); else _driver.canvas->drawRect(pX-1, pY-1, 49, 18, C_TEXT_SUB);
 }
 blitCanvas(_driver.canvas, 10, y, itemW, 34);
 }
 _forceSettingsRedraw = false; _lastThemePage = _themePage; _lastPreviewThemeIdx = _previewThemeIdx;
}

void DisplayManager::refreshAlarmStatus( ) {
 mutex_enter_blocking(&_stateMutex);
 _alarmStatusDirty = true; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::showSettingsAlarms(SystemConfig* cfg) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_ALARMS; _sysConfigPtr = cfg; _activeSensorCount = 0;
 for(int i = 0; i < MAX_SENSORS; i++) { if(cfg->sensors[i].active) { _activeSensorsMap[_activeSensorCount++] = i; } }
 _alarmSelection = 0; _alarmPage = 0; _lastAlarmSelection = -1; _forceSettingsRedraw = true; _lastAlarmPage = -1; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsAlarms( ) {
 if(!_driver.canvas) return;
 bool fullRedraw = _forceSettingsRedraw; bool pageChanged = (_alarmPage != _lastAlarmPage);
 int totalPages = (_activeSensorCount + 3) / 4; if (totalPages == 0) totalPages = 1;
 if (_alarmPage >= totalPages) _alarmPage = totalPages - 1;
 if (_alarmPage < 0) _alarmPage = 0;

 if (fullRedraw) {
 fastClearScreen(C_BG_MAIN);
 blitTitleBar(tr(TR_ALARMS_TITLE));
 /* Custom footer (wide EXIT, no primary) — same rects, composed in the
  * canvas at y=0 and pushed as one full-width band. */
 _driver.canvas->fillScreen(C_BG_MAIN);
 uiNavArrow(_driver.canvas, 5, 0, 62, 40, UI_UP);
 uiNavArrow(_driver.canvas, 73, 0, 62, 40, UI_DOWN);
 /* Exit keeps its full remaining width (same touch zone) but drops the
  * accent: EXIT is never the screen's primary action. */
 uiButton(_driver.canvas, 141, 0, 174, 40, tr(TR_BACK), UI_BTN_SECONDARY);
 blitCanvas(_driver.canvas, 0, 195, 320, 45);
 }

 if (fullRedraw || pageChanged) {
 uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, _alarmPage);
 }

 int startIdx = _alarmPage * 4;
 for (int i = 0; i < ALARM_LIST_ROWS; i++) {
 int y = ALARM_EDIT_Y0 + (i * ALARM_EDIT_STEP); int mapIdx = startIdx + i;

 /* Only redraw items whose selection state changed, or on fullRedraw/pageChanged */
 if (!fullRedraw && !pageChanged) {
 if (mapIdx != _alarmSelection && mapIdx != _lastAlarmSelection) continue;
 }

 int16_t statusX;
 renderAlarmRow(mapIdx, statusX);
 blitCanvas(_driver.canvas, ALARM_EDIT_BAR_X, y, ALARM_EDIT_BAR_W, ALARM_EDIT_BAR_H);
 }
 _forceSettingsRedraw = false; _lastAlarmPage = _alarmPage; _lastAlarmSelection = _alarmSelection;
}

/**
 * @brief Renders one row of the alarm list into _driver.canvas.
 * @param outStatusX canvas column where the ON/OFF area begins, wide enough
 *        for whichever of the two words is longer in the active language —
 *        the slice a status-only repaint has to cover.
 */
void DisplayManager::renderAlarmRow(int mapIdx, int16_t& outStatusX) {
 const int itemW = ALARM_EDIT_BAR_W;
 _driver.canvas->fillScreen(C_BG_MAIN);
 _driver.canvas->setFont(&simutFont9pt);

 /* Measured from both words, not from the one on screen: "SIM" and "NAO"
  * differ in width, and a slice sized to the narrower one would leave a
  * sliver of the wider one behind when the value flips. */
 int16_t bx, by; uint16_t wOn, wOff, hh;
 _driver.canvas->getTextBounds(tr(TR_ON), 0, 0, &bx, &by, &wOn, &hh);
 _driver.canvas->getTextBounds(tr(TR_OFF), 0, 0, &bx, &by, &wOff, &hh);
 const int widest = (int)((wOn > wOff) ? wOn : wOff);
 outStatusX = (int16_t)(itemW - 10 - widest - 6);
 if (outStatusX < 0) outStatusX = 0;

 if (mapIdx >= _activeSensorCount) return;

 int actualSensorId = _activeSensorsMap[mapIdx];
 SensorRecord* rec = &_sysConfigPtr->sensors[actualSensorId];
 bool isSelected = (mapIdx == _alarmSelection);
 uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
 uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(0, 0, itemW, ALARM_EDIT_BAR_H, ALARM_EDIT_BAR_R, bg);
 if (!isSelected) _driver.canvas->drawRoundRect(0, 0, itemW, ALARM_EDIT_BAR_H,
 ALARM_EDIT_BAR_R, C_TEXT_SUB);

 const char* statusTxt = rec->alarmsActive ? tr(TR_ON) : tr(TR_OFF);
 uint16_t sw;
 _driver.canvas->getTextBounds(statusTxt, 0, 0, &bx, &by, &sw, &hh);

 /* Sensor name — truncated if needed to avoid collision */
 int maxNameW = itemW - (widest + 20) - 15;
 char nameBuf[40];
 truncateText(_driver.canvas, rec->friendlyName, nameBuf, sizeof(nameBuf), maxNameW);
 _driver.canvas->setTextColor(txt);
 _driver.canvas->setCursor(10, 24);
 _driver.canvas->print(nameBuf);

 /* ON/OFF indicator right-aligned */
 uint16_t statusColor;
 if (isSelected) {
 statusColor = C_BG_MAIN;
 } else {
 statusColor = rec->alarmsActive ? C_TEMP_OK : C_TEXT_OFF;
 }
 _driver.canvas->setTextColor(statusColor);
 _driver.canvas->setCursor(itemW - 10 - (int)sw, 24);
 _driver.canvas->print(statusTxt);
}

/**
 * @brief Repaints only the ON/OFF word of the selected row.
 *
 * Toggling the flag used to route through showSettingsAlarms(), which forces a
 * whole-screen redraw AND resets the cursor to the first sensor on page 0 —
 * so flipping the fifth item flashed the display and threw the selection away.
 * The row is still rendered in full (into RAM, which is cheap); only the
 * badge-sized slice is pushed over SPI.
 */
void DisplayManager::drawAlarmStatusOnly( ) {
 if (!_driver.canvas || !_sysConfigPtr) return;
 const int row = _alarmSelection - (_alarmPage * ALARM_LIST_ROWS);
 if (row < 0 || row >= ALARM_LIST_ROWS) return;
 int16_t statusX;
 renderAlarmRow(_alarmSelection, statusX);
 const int y = ALARM_EDIT_Y0 + (row * ALARM_EDIT_STEP);
 blitCanvas(_driver.canvas, (int16_t)(ALARM_EDIT_BAR_X + statusX), (int16_t)y,
 (int16_t)(ALARM_EDIT_BAR_W - statusX), ALARM_EDIT_BAR_H, statusX);
}

void DisplayManager::showAlarmEdit(int sensorIdx) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_ALARM_EDIT; _editSensorIdx = sensorIdx;
 _tempAlarmConfig = _sysConfigPtr->sensors[sensorIdx];
 _editFieldFocus = 0; _lastEditPage = -1;
 _forceSettingsRedraw = true; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

/* The built-in GFX font is CP437, not UTF-8. The channel table stores "\u00b0C"
 * as the bytes C2 B0 43. Unit rendering (that "°" prefix drawn as a real
 * ring + 9pt text) now lives in uiUnit/uiUnitWidth in UiWidgets.cpp,
 * shared with every screen that prints a channel unit. */
void DisplayManager::drawAlarmEdit( ) {
 /* One limit per bar, scrolled four at a time — the same rows, spacing and
  * scrollbar as every other menu on this display. The screen it replaces laid
  * out four fixed boxes, which meant a hard ceiling of two channels: a part
  * with three quantities had its third reachable only from the web. Rows are
  * derived from the channel table via sensorLimitCount(), so a driver that
  * adds a quantity gets its two rows here with no edit to this function. */
 if (!_driver.canvas) return;
 const SensorType sType = (SensorType)_tempAlarmConfig.sensorType;
 const uint8_t nLimits = sensorLimitCount(sType);
 if (nLimits == 0) return;

 /* MIN >= MAX is not representable on screen and would arm an alarm that can
  * never clear, so every channel is pulled apart before anything is drawn. */
 for (uint8_t i = 0; i < nLimits; i += 2) {
 const uint8_t c = sensorLimitChannel(sType, i);
 if (!channelValid(c)) continue;
 if (_tempAlarmConfig.chMin[c] < _tempAlarmConfig.chMax[c]) continue;
 const ChannelInfo& ci = channelInfo(c);
 _tempAlarmConfig.chMax[c] = round((_tempAlarmConfig.chMin[c] + 0.1f) * 10.0f) / 10.0f;
 if (_tempAlarmConfig.chMax[c] > ci.saneMax) {
 _tempAlarmConfig.chMax[c] = ci.saneMax;
 _tempAlarmConfig.chMin[c] = ci.saneMax - 0.1f;
 }
 }

 const int totalPages = (nLimits + ALARM_EDIT_ROWS - 1) / ALARM_EDIT_ROWS;
 if (_editFieldFocus >= (int)nLimits) _editFieldFocus = (int)nLimits - 1;
 if (_editFieldFocus < 0) _editFieldFocus = 0;
 const int page = _editFieldFocus / ALARM_EDIT_ROWS;
 const bool pageChanged = (page != _lastEditPage);

 if (_forceSettingsRedraw) {
 fastClearScreen(C_BG_MAIN);
 /* T1.2: no heap in Core-1 render */
 blitTitleBar(_tempAlarmConfig.friendlyName);

 /* The footer arrows move the SELECTION, not the value — the value is
  * adjusted on the bar itself. Hence the menu's up/down orientation. */
 blitFooterMenu(tr(TR_BACK), tr(TR_SAVE));
 }

 if (_forceSettingsRedraw || pageChanged) {
 uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, page);
 }

 for (int row = 0; row < ALARM_EDIT_ROWS; row++) {
 const int idx = page * ALARM_EDIT_ROWS + row;
 const int y = ALARM_EDIT_Y0 + row * ALARM_EDIT_STEP;
 _driver.canvas->fillScreen(C_BG_MAIN);
 if (idx < (int)nLimits) {
 const uint8_t ch = sensorLimitChannel(sType, (uint8_t)idx);
 const bool isMax = sensorLimitIsMax((uint8_t)idx);
 if (channelValid(ch)) {
 const ChannelInfo& ci = channelInfo(ch);
 const bool focused = (idx == _editFieldFocus);
 const uint16_t bg = focused ? C_ACCENT : C_CARD_BG;
 const uint16_t txt = focused ? C_BG_MAIN : C_TEXT_MAIN;

 _driver.canvas->fillRoundRect(0, 0, ALARM_EDIT_BAR_W, ALARM_EDIT_BAR_H,
 ALARM_EDIT_BAR_R, bg);
 if (!focused) _driver.canvas->drawRoundRect(0, 0, ALARM_EDIT_BAR_W,
 ALARM_EDIT_BAR_H, ALARM_EDIT_BAR_R, C_TEXT_SUB);

 /* The step buttons sit inset by the same amount on all four sides, so
  * their arcs are concentric with the bar's: r_btn = r_bar - inset. */
 const int lx = ALARM_EDIT_INSET;
 const int rx = ALARM_EDIT_BAR_W - ALARM_EDIT_INSET - ALARM_EDIT_BTN_W;
 const uint16_t btnBg = focused ? C_BG_MAIN : C_BAR_BG;
 const uint16_t arrow = focused ? C_ACCENT : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(lx, ALARM_EDIT_INSET, ALARM_EDIT_BTN_W,
 ALARM_EDIT_BTN_H, ALARM_EDIT_BTN_R, btnBg);
 _driver.canvas->fillRoundRect(rx, ALARM_EDIT_INSET, ALARM_EDIT_BTN_W,
 ALARM_EDIT_BTN_H, ALARM_EDIT_BTN_R, btnBg);
 const int cy = ALARM_EDIT_INSET + ALARM_EDIT_BTN_H / 2;
 _driver.canvas->fillTriangle(lx + 8, cy, lx + 18, cy - 7, lx + 18, cy + 7, arrow);
 _driver.canvas->fillTriangle(rx + 18, cy, rx + 8, cy - 7, rx + 8, cy + 7, arrow);

 const float val = isMax ? _tempAlarmConfig.chMax[ch] : _tempAlarmConfig.chMin[ch];
 char numBuf[12];
 if (val < 0 && val > -1.0f) snprintf(numBuf, sizeof(numBuf), "-0.%d",
 abs((int)round(val * 10.0f) % 10));
 else snprintf(numBuf, sizeof(numBuf), "%d.%d", (int)val,
 abs((int)round(val * 10.0f) % 10));

 _driver.canvas->setFont(&simutFont9pt);
 int16_t bx, by; uint16_t bw, bh;
 _driver.canvas->getTextBounds(numBuf, 0, 0, &bx, &by, &bw, &bh);
 /* Unit rendered by uiUnit (real degree ring + 9pt text), so the
  * reserved width comes from the same helper that draws it. */
 const int unitW = (int)uiUnitWidth(_driver.canvas, ci.display.unit);
 const int valW = (int)bw + 4 + unitW;
 const int textL = lx + ALARM_EDIT_BTN_W + 6;
 const int textR = rx - 6;

 /* The suffix is copied out of tr()'s rotating scratch before the channel
  * name is fetched, so the order of the two lookups cannot matter. */
 char suffix[16];
 snprintf(suffix, sizeof(suffix), " %s", tr(isMax ? TR_MAX_LBL : TR_MIN_LBL));
 char label[48];
 truncateTextKeepSuffix(_driver.canvas, channelLabel(ch), suffix,
 label, sizeof(label), (int16_t)(textR - valW - 8 - textL));
 _driver.canvas->setTextColor(txt);
 _driver.canvas->setCursor(textL, 23); _driver.canvas->print(label);
 _driver.canvas->setCursor(textR - valW, 23); _driver.canvas->print(numBuf);
 uiUnit(_driver.canvas, (int16_t)(textR - unitW), 23, ci.display.unit, txt);
 }
 }
 blitCanvas(_driver.canvas, ALARM_EDIT_BAR_X, y, ALARM_EDIT_BAR_W, ALARM_EDIT_BAR_H);
 }
 _forceSettingsRedraw = false;
 _lastEditPage = page;
}

void DisplayManager::showSettingsMain( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_MAIN; _menuSelection = 0; _mainMenuPage = 0; _lastMainMenuPage = -1;
 _forceSettingsRedraw = true; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsMain( ) {
 if(!_driver.canvas) return;
 bool fullRedraw = _forceSettingsRedraw; bool pageChanged = (_mainMenuPage != _lastMainMenuPage);
 const int TOTAL_ITEMS = 9; LangKey menuItems[] = {TR_MENU_THEMES, TR_MENU_ALARMS, TR_MENU_SOUNDS, TR_MENU_LANG, TR_MENU_PASSWORD, TR_MENU_TOUCH_CAL, TR_MENU_LICENSE, TR_MENU_STATUS, TR_MENU_DISPLAY_OFFSET};
 int totalPages = (TOTAL_ITEMS + 3) / 4; if (totalPages == 0) totalPages = 1;
 if (_mainMenuPage >= totalPages) _mainMenuPage = totalPages - 1;
 if (_mainMenuPage < 0) _mainMenuPage = 0;

 if (fullRedraw) {
 fastClearScreen(C_BG_MAIN);
 blitTitleBar(tr(TR_CONFIG_MAIN));
 blitFooterMenu(tr(TR_BACK), tr(TR_ENTER)); /* T1.2: no heap */
 }

 if (fullRedraw || pageChanged) {
 uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, _mainMenuPage);
 }

 /* Cursor moves repaint only the two rows whose selection state changed —
  * the same skip the themes/alarms/language lists already had. */
 static int s_lastMenuSel = -1;
 int startIdx = _mainMenuPage * 4; int yBase = 40; int itemW = 285;
 for (int i = 0; i < 4; i++) {
 int y = yBase + (i * 38); int mapIdx = startIdx + i;
 if (!fullRedraw && !pageChanged &&
     mapIdx != _menuSelection && mapIdx != s_lastMenuSel) continue;
 _driver.canvas->fillScreen(C_BG_MAIN);
 _driver.canvas->setTextSize(1); /* Ensures reset after status screen */
 if (mapIdx < TOTAL_ITEMS) {
 bool isSelected = (mapIdx == _menuSelection);
 uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
 uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
 if (!isSelected) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
 /* Menu items keep their table order, so the icon id IS the index:
  * 0 themes, 1 alarms, 2 sounds, 3 lang, 4 password, 5 touch-cal,
  * 6 license, 7 status, 8 display-offset. */
 uiMenuIcon(_driver.canvas, 10, 9, (uint8_t)mapIdx,
 isSelected ? C_BG_MAIN : C_ACCENT);
 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(txt);
 _driver.canvas->setCursor(34, 24); _driver.canvas->print(tr(menuItems[mapIdx]));
 _driver.canvas->fillTriangle(itemW - 20, 11, itemW - 20, 23, itemW - 10, 17, isSelected ? C_BG_MAIN : C_TEXT_SUB);
 }
 blitCanvas(_driver.canvas, 10, y, itemW, 34);
 }
 _forceSettingsRedraw = false; _lastMainMenuPage = _mainMenuPage;
 s_lastMenuSel = _menuSelection;
}




void DisplayManager::showSettingsPassword( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_PASSWORD;
 _kbCursor = 0;
 _kbShowRaw = false;
 _kbPhase = 0;
 _kbMsgKey = TR_KEYS_COUNT;
 _kbPopup = PwdKb::POPUP_NONE;
 memset(_kbBuffer, 0, sizeof(_kbBuffer));
 memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}


void DisplayManager::getNewPassword(char* out, size_t maxLen) const {
 strncpy(out, _kbBuffer, maxLen - 1);
 out[maxLen - 1] = '\0';
}


void DisplayManager::drawPasswordMessage( ) {
 if (!_driver.tft) return;
 int16_t x1, y1; uint16_t w, h_bound;

 fastClearScreen(C_BG_MAIN);


 bool isSuccess = (_kbPhase == 3);
 uint16_t iconColor = isSuccess ? C_TEMP_OK : C_TEMP_WARM;

 if (isSuccess) {

 _driver.tft->drawLine(130, 90, 150, 110, iconColor);
 _driver.tft->drawLine(131, 90, 151, 110, iconColor);
 _driver.tft->drawLine(150, 110, 190, 70, iconColor);
 _driver.tft->drawLine(151, 110, 191, 70, iconColor);
 } else {

 _driver.tft->drawLine(145, 70, 175, 100, iconColor);
 _driver.tft->drawLine(146, 70, 176, 100, iconColor);
 _driver.tft->drawLine(175, 70, 145, 100, iconColor);
 _driver.tft->drawLine(176, 70, 146, 100, iconColor);
 }


 const char* msg = (_kbMsgKey < TR_KEYS_COUNT) ? tr(_kbMsgKey) : "Error";
 _driver.tft->setFont(&simutFont9pt);
 _driver.tft->setTextColor(C_TEXT_MAIN);
 _driver.tft->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor((320 - w) / 2, 130);
 _driver.tft->print(msg);


 _driver.tft->fillRoundRect(60, 185, 200, 40, 12, C_ACCENT);
 _driver.tft->setFont(&simutFont12pt);
 _driver.tft->setTextColor(C_BG_MAIN);
 const char* btnLabel = tr(TR_UNDERSTOOD);
 _driver.tft->getTextBounds(btnLabel, 0, 0, &x1, &y1, &w, &h_bound);
 _driver.tft->setCursor(160 - (w / 2), 212);
 _driver.tft->print(btnLabel);
}

/* ---- password keyboard (option A): local draw helpers ---------------- */

/* Centered one-line label — canvas variant of uiCenteredText (which is
 * private to UiWidgets.cpp). Sets font/color itself. */
static void kbCenteredLabel(GFXcanvas16* cv, int16_t x, int16_t y, int16_t w,
 int16_t h, const char* label, const GFXfont* font, uint16_t color) {
 cv->setFont(font); cv->setTextSize(1); cv->setTextColor(color);
 int16_t bx, by; uint16_t bw, bh;
 cv->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(x + (w - (int16_t)bw) / 2 - bx, y + (h - (int16_t)bh) / 2 - by);
 cv->print(label);
}

/* Backspace glyph (triangle + body) centered at (cx, cy) — the action-bar
 * icon of the old keyboard, scaled to the 54 px keys. */
static void kbBackspaceIcon(GFXcanvas16* cv, int16_t cx, int16_t cy,
 uint16_t fg) {
 cv->fillTriangle(cx - 12, cy, cx - 3, cy - 8, cx - 3, cy + 8, fg);
 cv->fillRect(cx - 3, cy - 5, 14, 10, fg);
}

/* Popup card: 2 px background ring for separation + accent double border. */
static void kbPopupCard(GFXcanvas16* cv, int16_t x, int16_t y, int16_t w,
 int16_t h) {
 cv->fillRoundRect(x - 2, y - 2, w + 4, h + 4, 12, C_BG_MAIN);
 cv->fillRoundRect(x, y, w, h, 10, C_CARD_BG);
 cv->drawRoundRect(x, y, w, h, 10, C_ACCENT);
 cv->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, C_ACCENT);
}

/* One popup key: bar-bg capsule + centered character. */
static void kbPopupKey(GFXcanvas16* cv, int16_t x, int16_t y, int16_t w,
 int16_t h, int16_t r, char c, const GFXfont* font, uint16_t color) {
 cv->fillRoundRect(x, y, w, h, r, C_BAR_BG);
 cv->drawRoundRect(x, y, w, h, r, C_TEXT_SUB);
 char s[2] = { c, '\0' };
 kbCenteredLabel(cv, x, y, w, h, s, font, color);
}

/* The open popup, drawn over the grid. Letter popups show the lower-case
 * row above the UPPER-case row (accent-high) — no Shift key anywhere. */
static void kbDrawPopup(GFXcanvas16* cv, int8_t popup, int16_t yOff) {
 using namespace PwdKb;

 if (popup == POPUP_SYMBOLS) {
 kbPopupCard(cv, SP_CARD_X, (int16_t)(SP_CARD_Y + yOff), SP_CARD_W,
 SP_CARD_H);
 for (int r = 0; r < 4; r++)
 for (int c = 0; c < 7; c++)
 kbPopupKey(cv, (int16_t)(SP_X0 + c * SP_COL_W),
 (int16_t)(SP_Y0 + r * SP_ROW_H + yOff),
 SP_KEY_W, SP_KEY_H, 6, SYMBOLS[r * 7 + c],
 &simutFont9pt, C_TEXT_MAIN);
 return;
 }

 if (popup == POPUP_DIGITS) {
 kbPopupCard(cv, DP_CARD_X, (int16_t)(LP_CARD_Y + yOff), DP_CARD_W,
 LP_CARD_H);
 for (int i = 0; i < 10; i++)
 kbPopupKey(cv, (int16_t)(DP_X0 + (i % 5) * (DP_KEY_W + DP_GAP)),
 (int16_t)(((i < 5) ? LP_ROW0_Y : LP_ROW1_Y) + yOff),
 DP_KEY_W, DP_KEY_H, 8, DIGITS[i], &simutFont12pt,
 C_TEXT_MAIN);
 return;
 }

 const int n = letterCount(popup);
 const int16_t x0 = lpX0(n);
 kbPopupCard(cv, (int16_t)(x0 - LP_CARD_PAD), (int16_t)(LP_CARD_Y + yOff),
 (int16_t)(lpTotalW(n) + 2 * LP_CARD_PAD), LP_CARD_H);
 for (int i = 0; i < 2 * n; i++) {
 const bool upperRow = (i >= n);
 kbPopupKey(cv, (int16_t)(x0 + (i % n) * (LP_KEY_W + LP_GAP)),
 (int16_t)((upperRow ? LP_ROW1_Y : LP_ROW0_Y) + yOff),
 LP_KEY_W, LP_KEY_H, 8, popupChar(popup, i), &simutFont12pt,
 upperRow ? C_ACCENT_HIGH : C_TEXT_MAIN);
 }
}

/**
 * @brief Password-change keyboard, option A: group grid + zoom popups.
 *
 * Every character costs two taps on finger-sized targets (grid keys
 * 76x54 px): tap a group, then tap the character in a popup that offers
 * both cases at once. 123 and @#! open digit/symbol popups the same way;
 * space and backspace act directly. Full screen is composed through the
 * 6-strip renderer — no partial-blit paths, every repaint is one pass.
 */
void DisplayManager::drawSettingsPassword( ) {
 if (!_driver.tft) return;

 /* Result screens (error/success) keep the message layout. */
 if (_kbPhase >= 2) {
 drawPasswordMessage( );
 _forceSettingsRedraw = false;
 return;
 }

 using namespace PwdKb;

 const char* activeBuf = (_kbPhase == 0) ? _kbBuffer : _kbConfirmBuf;
 const String title = (_kbPhase == 0) ? tr(TR_NEW_PASSWORD)
 : tr(TR_CONFIRM_PASSWORD);

 /*
 * Number of visible boxes: in phase 0 (typing), the max between 4 and
 * (cursor + 1), up to 7. In phase 1 (confirmation), exactly the length
 * of the password being confirmed — same rule as the old keyboard.
 */
 int visibleBoxes;
 if (_kbPhase == 1) {
 visibleBoxes = (int)strlen(_kbBuffer);
 } else {
 visibleBoxes = _kbCursor + 1;
 if (visibleBoxes < 4) visibleBoxes = 4;
 if (visibleBoxes > 7) visibleBoxes = 7;
 }

 GFXcanvas16* cv = beginScreenRender( );
 if (!cv) return;

 for (int strip = 0; strip < 6; strip++) {
 cv->fillScreen(C_BG_MAIN);
 const int16_t yOff = (int16_t)(-strip * RENDER_STRIP_H);

 uiTitleBar(cv, (int16_t)(4 + yOff), title.c_str( ), -1, 0, 26);
 uiCloseX(cv, 284, (int16_t)(5 + yOff), 32, 24);

 /* Password boxes + counter + OK beside them */
 for (int i = 0; i < visibleBoxes; i++) {
 const int16_t bx = (int16_t)(BOX_X0 + i * (BOX_W + BOX_GAP));
 const int16_t by = (int16_t)(BOX_Y + yOff);

 cv->fillRoundRect(bx, by, BOX_W, BOX_H, 4, C_CARD_BG);
 uint16_t borderColor = (i < 4) ? C_ACCENT_HIGH : C_TEXT_OFF;
 if (i == _kbCursor) borderColor = C_ACCENT;
 cv->drawRoundRect(bx, by, BOX_W, BOX_H, 4, borderColor);

 if (i < _kbCursor) {
 if (_kbShowRaw) {
 char ch[2] = { activeBuf[i], '\0' };
 kbCenteredLabel(cv, bx, by, BOX_W, BOX_H, ch, &simutFont9pt,
 C_TEXT_MAIN);
 } else {
 cv->fillCircle(bx + BOX_W / 2, by + BOX_H / 2, 5, C_TEXT_MAIN);
 }
 }
 }

 {
 char countBuf[8];
 snprintf(countBuf, sizeof(countBuf), "%d/%d", _kbCursor, visibleBoxes);
 cv->setFont(NULL); cv->setTextSize(1);
 cv->setTextColor((_kbCursor < 4) ? C_TEXT_OFF : C_ACCENT);
 cv->setCursor(BOX_X0 + visibleBoxes * (BOX_W + BOX_GAP) + 4,
 BOX_Y + 10 + yOff);
 cv->print(countBuf);
 }

 uiButton(cv, OK_X, (int16_t)(OK_Y + yOff), OK_W, OK_H, "OK",
 UI_BTN_PRIMARY);

 /* Group grid, rows 0-1: the 8 letter groups */
 for (int k = 0; k < 8; k++) {
 const int16_t kx = (int16_t)(GRID_X0 + (k % 4) * GRID_COL_W);
 const int16_t ky = (int16_t)(GRID_Y0 + (k / 4) * GRID_ROW_H + yOff);
 const bool active = (_kbPopup == POPUP_GROUP0 + k);

 cv->fillRoundRect(kx, ky, GRID_KEY_W, GRID_KEY_H, 10,
 active ? C_ACCENT : C_CARD_BG);
 if (!active)
 cv->drawRoundRect(kx, ky, GRID_KEY_W, GRID_KEY_H, 10, C_TEXT_SUB);
 kbCenteredLabel(cv, kx, ky, GRID_KEY_W, GRID_KEY_H, GROUPS[k],
 &simutFont12pt, active ? C_BG_MAIN : C_TEXT_MAIN);
 }

 /* Row 2: 123 / @#! / space / backspace */
 {
 const int16_t ry = (int16_t)(GRID_Y0 + 2 * GRID_ROW_H + yOff);

 const bool digitsActive = (_kbPopup == POPUP_DIGITS);
 cv->fillRoundRect(GRID_X0, ry, GRID_KEY_W, GRID_KEY_H, 10,
 digitsActive ? C_ACCENT : C_BAR_BG);
 cv->drawRoundRect(GRID_X0, ry, GRID_KEY_W, GRID_KEY_H, 10,
 digitsActive ? C_ACCENT : C_TEXT_SUB);
 kbCenteredLabel(cv, GRID_X0, ry, GRID_KEY_W, GRID_KEY_H, "123",
 &simutFont12pt, digitsActive ? C_BG_MAIN : C_ACCENT_HIGH);

 const bool symActive = (_kbPopup == POPUP_SYMBOLS);
 const int16_t sx = (int16_t)(GRID_X0 + GRID_COL_W);
 cv->fillRoundRect(sx, ry, GRID_KEY_W, GRID_KEY_H, 10,
 symActive ? C_ACCENT : C_BAR_BG);
 cv->drawRoundRect(sx, ry, GRID_KEY_W, GRID_KEY_H, 10,
 symActive ? C_ACCENT : C_TEXT_SUB);
 kbCenteredLabel(cv, sx, ry, GRID_KEY_W, GRID_KEY_H, "@#!",
 &simutFont12pt, symActive ? C_BG_MAIN : C_ACCENT_HIGH);

 const int16_t spx = (int16_t)(GRID_X0 + 2 * GRID_COL_W);
 cv->fillRoundRect(spx, ry, GRID_KEY_W, GRID_KEY_H, 10, C_CARD_BG);
 cv->drawRoundRect(spx, ry, GRID_KEY_W, GRID_KEY_H, 10, C_TEXT_SUB);
 /* 2 px space bar line — 1 px reads thinner than everything else. */
 cv->drawFastHLine(spx + 18, ry + GRID_KEY_H / 2 + 2, 40, C_TEXT_SUB);
 cv->drawFastHLine(spx + 18, ry + GRID_KEY_H / 2 + 3, 40, C_TEXT_SUB);

 const int16_t bkx = (int16_t)(GRID_X0 + 3 * GRID_COL_W);
 cv->fillRoundRect(bkx, ry, GRID_KEY_W, GRID_KEY_H, 10, C_CARD_BG);
 cv->drawRoundRect(bkx, ry, GRID_KEY_W, GRID_KEY_H, 10, C_TEXT_SUB);
 kbBackspaceIcon(cv, (int16_t)(bkx + GRID_KEY_W / 2),
 (int16_t)(ry + GRID_KEY_H / 2), C_TEXT_MAIN);
 }

 /* Popup last — on top of everything below the title. */
 if (_kbPopup != POPUP_NONE) kbDrawPopup(cv, _kbPopup, yOff);

 commitScreenStrip(strip);
 }
 endScreenRender( );

 _forceSettingsRedraw = false;
}




void DisplayManager::setTelemetryPending(uint16_t count) {
 _sharedState.pendingPkts = count;
}


/**
 * @brief Reports the result of the last telemetry send.
 *
 * Success: starts flash animation (blue/white for 1s), then
 * stabilizes to fixed blue.
 * Failure: fixed red immediately.
 */
void DisplayManager::setTelemetrySendStatus(bool success) {
 if (success) {
 /* Publish auxiliary vars BEFORE the state (the flag that
 * triggers the flash branch in Core 1 render). */
 _pktArrowFlashOn = false;
 _pktArrowFlashTime = millis( );
 _pktArrowFlashEnd = millis( ) + 1000;
 __dmb( );
 _pktArrowState = 3; /* send flash */
 } else {
 _pktArrowState = 2; /* fixed red */
 }
}



void DisplayManager::showSettingsSounds(const SoundSettingsState& state) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_SOUNDS;
 _soundSettings = state;
 _soundSelection = 0;
 _inMelodySelect = false;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}


void DisplayManager::showMuteConfirm( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_CONFIRM_MUTE_ALL;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
 /* Fire SND_ATTENTION when entering the confirmation screen. */
 requestPreviewSound(SND_ATTENTION, _soundSettings.attentionMelody);
}


/* Global Mute confirmation screen. Visual pattern: red header
 * (same tone as drawAlarmAction), 3 centered message lines, 2
 * buttons at bottom (Back left, Confirm right). Rendered
 * via strips (atomic canvas) to avoid top-down effect.
 *
 * Strings hardcoded EN/PT — does not touch DICTIONARY_EN or device
 * .lng files (TFT renders only ASCII per policy). */
void DisplayManager::drawMuteConfirm( ) {
 if (!_driver.canvas) return;
 if (!_forceSettingsRedraw) return;
 _forceSettingsRedraw = false;

 bool isPt = (_currentLangIdx == LANG_PT);
 const char* titleTxt = isPt ? "Mudo Global" : "Mute All";
 const char* msgL1 = isPt ? "Todos os sons ser\xE3o" : "All sounds will be";
 const char* msgL2 = isPt ? "desabilitados." : "disabled.";
 const char* msgL3 = isPt ? "Tem certeza?" : "Are you sure?";
 const char* backTxt = tr(TR_BACK);
 const char* confirmTxt = isPt ? "Confirmar" : "Confirm";

 GFXcanvas16* cv = beginScreenRender( );
 if (!cv) return;

 int16_t bx, by; uint16_t bw, bh;

 /* 6 strips x 40px = 240px (atomic canvas, no top-down). */
 for (int strip = 0; strip < 6; strip++) {
 cv->fillScreen(C_BG_MAIN);
 const int16_t yOff = -strip * RENDER_STRIP_H;

 /* Header (y_screen=4..36) — highlighted red background, white text. */
 cv->fillRect(4, 4 + yOff, 312, 32, RGB565(180, 30, 30));
 cv->setFont(&simutFont12pt);
 cv->setTextColor(RGB565(255, 255, 255));
 cv->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 28 + yOff);
 cv->print(titleTxt);

 /* Message (3 centered lines at y=80, 110, 140). */
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TEXT_MAIN);
 cv->getTextBounds(msgL1, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 80 + yOff);
 cv->print(msgL1);
 cv->getTextBounds(msgL2, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 108 + yOff);
 cv->print(msgL2);
 cv->getTextBounds(msgL3, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 148 + yOff);
 cv->print(msgL3);

 /* Back button (left, x=20..150, y=190..230) — neutral card_bg. */
 cv->fillRoundRect(20, 190 + yOff, 130, 40, 10, C_CARD_BG);
 cv->drawRoundRect(20, 190 + yOff, 130, 40, 10, C_TEXT_SUB);
 cv->setFont(&simutFont12pt);
 cv->setTextColor(C_TEXT_MAIN);
 cv->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(20 + (130 - bw) / 2, 218 + yOff);
 cv->print(backTxt);

 /* Confirm button (right, x=170..300, y=190..230) — red highlight. */
 cv->fillRoundRect(170, 190 + yOff, 130, 40, 10, RGB565(180, 30, 30));
 cv->setTextColor(RGB565(255, 255, 255));
 cv->getTextBounds(confirmTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(170 + (130 - bw) / 2, 218 + yOff);
 cv->print(confirmTxt);

 commitScreenStrip(strip);
 }
 endScreenRender( );
}


void DisplayManager::drawSettingsSounds( ) {
 if (!_driver.canvas) return;

 static int lastSoundPage = -1;
 int soundPage = _soundSelection / 4;
 bool fullRedraw = _forceSettingsRedraw;
 bool pageChanged = (soundPage != lastSoundPage);

 /* 9 items (added Attention between Web and Mute). 3 pages. */
 const int TOTAL_ITEMS = 9;
 int totalPages = (TOTAL_ITEMS + 3) / 4;
 bool isPt = (_currentLangIdx == LANG_PT);


 if (fullRedraw) {
 fastClearScreen(C_BG_MAIN);
 blitTitleBar(tr(TR_SOUNDS_TITLE));
 blitFooterMenu(tr(TR_BACK), tr(TR_SAVE)); /* T1.2: no heap */
 }


 if (fullRedraw || pageChanged) {
 uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, soundPage);
 }


 /* Reorganized order — volumes first, mute last.
 * 0=Sys Volume, 1=Alarm Vol, 2=Touch, 3=Confirm, 4=Error, 5=Alarm,
 * 6=Attention, 7=Web, 8=Mute All.
 * Labels hardcoded EN/PT for "Error"/"Alarm"/"Attention"/"Mute All" to
 * (a) eliminate "Sound error"/"Sound alarm" redundancy; (b) rename
 * "Silence all" -> "Mute All"; (c) Attention without TR key. Does not touch
 * DICTIONARY_EN or device .lng files. */
 const char* errorLabel = isPt ? "Erro" : "Error";
 const char* alarmLabel = isPt ? "Alarme" : "Alarm";
 const char* attentionLabel = isPt ? "Aten\xE7\xE3o" : "Attention";
 const char* muteLabel = isPt ? "Mudo Global" : "Mute All";

 /* Cursor moves repaint only the two affected rows; value changes (volume
  * drag, ON/OFF toggle) always land on the selected row, which repaints. */
 static int s_lastSoundSel = -1;
 int startIdx = soundPage * 4;
 int yBase = 40; int itemW = 285;

 for (int i = 0; i < 4; i++) {
 int actualIdx = startIdx + i;
 int y = yBase + (i * 38);

 if (!fullRedraw && !pageChanged &&
     actualIdx != _soundSelection && actualIdx != s_lastSoundSel) continue;

 _driver.canvas->fillScreen(C_BG_MAIN);

 if (actualIdx < TOTAL_ITEMS) {
 bool isSelected = (actualIdx == _soundSelection);
 uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
 uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;

 _driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
 if (!isSelected) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(txt);
 _driver.canvas->setCursor(10, 24);
 const char* label = nullptr;
 switch (actualIdx) {
 case 0: label = tr(TR_SND_VOLUME); break;
 case 1: label = tr(TR_SND_ALARM_VOL); break;
 case 2: label = tr(TR_SND_TOUCH); break;
 case 3: label = tr(TR_SND_CONFIRM); break;
 case 4: label = errorLabel; break; /* hardcoded */
 case 5: label = alarmLabel; break; /* hardcoded */
 case 6: label = attentionLabel; break; /* hardcoded */
 case 7: label = tr(TR_SND_WEB); break;
 case 8: label = muteLabel; break; /* hardcoded */
 }
 _driver.canvas->print(label);

 if (actualIdx == 0 || actualIdx == 1) {
 /* Fixed right-aligned bar for Sys Volume (0) and Alarm Vol (1).
 * Percentage removed — bar is the complete visual feedback. */
 uint8_t volVal = (actualIdx == 0)
 ? _soundSettings.volume
 : _soundSettings.alarmVolume;

 const int barW = 130;
 const int barX = itemW - 15 - barW; /* fixed 15px margin from right */
 const int barY = 11;
 const int barH = 12;
 int fillW = (int)((uint32_t)barW * volVal / 100);

 uint16_t barBg = isSelected ? C_ACCENT_HIGH : C_BAR_BG;
 uint16_t barFill = isSelected ? C_BG_MAIN : C_ACCENT;
 _driver.canvas->fillRoundRect(barX, barY, barW, barH, 3, barBg);
 if (fillW > 0) {
 _driver.canvas->fillRoundRect(barX, barY, fillW, barH, 3, barFill);
 }
 } else {

 bool val = false;
 switch (actualIdx) {
 case 2: val = _soundSettings.touchEnabled; break;
 case 3: val = _soundSettings.confirmEnabled; break;
 case 4: val = _soundSettings.errorEnabled; break;
 case 5: val = _soundSettings.alarmEnabled; break;
 case 6: val = _soundSettings.attentionEnabled; break;
 case 7: val = _soundSettings.webEnabled; break;
 case 8: val = _soundSettings.muted; break;
 }
 const char* valStr = val ? tr(TR_ON) : tr(TR_OFF);
 int16_t bx, by; uint16_t bw, bh;
 _driver.canvas->getTextBounds(valStr, 0, 0, &bx, &by, &bw, &bh);
 _driver.canvas->setCursor(itemW - 15 - bw, 24);
 _driver.canvas->print(valStr);
 }
 }
 blitCanvas(_driver.canvas, 10, y, itemW, 34);
 }

 _forceSettingsRedraw = false;
 lastSoundPage = soundPage;
 s_lastSoundSel = _soundSelection;
}


void DisplayManager::drawMelodySelect( ) {
 if (!_driver.canvas) return;


 static const char* MEL_NAMES[5][6] = {
 {"1. Click", "2. Bubble", "3. Tick",
 "4. Snap", "5. Drop", "6. Chirp"},
 {"1. Ascending", "2. Fanfare", "3. Chime",
 "4. Triumph", "5. Sparkle", "6. Resolve"},
 {"1. Descending", "2. Buzz", "3. Low",
 "4. Harsh", "5. Decline", "6. Blip"},
 {"1. Dual Beep", "2. Siren", "3. Rapid",
 "4. Pulse", "5. Escalate", "6. Staccato"},
 /* Attention. */
 {"1. Notify", "2. Bell", "3. Pulse",
 "4. Chime Low", "5. Rise", "6. Soft Ding"}
 };
 /* typeIdx==4 (Attention) uses conditional hardcoded label. */
 static const LangKey TYPE_LABELS[4] = {
 TR_SND_TOUCH, TR_SND_CONFIRM, TR_SND_ERROR, TR_SND_ALARM
 };

 uint8_t typeIdx = _melSelectType;
 if (typeIdx > 4) typeIdx = 0;
 bool isPt = (_currentLangIdx == LANG_PT);

 const int TOTAL_VARIANTS = 6;
 bool fullRedraw = _forceSettingsRedraw;


 if (fullRedraw) {
 fastClearScreen(C_BG_MAIN);
 blitTitleBar((typeIdx == 4)
 ? (isPt ? "Aten\xE7\xE3o" : "Attention")
 : tr(TYPE_LABELS[typeIdx]));
 blitFooterMenu(tr(TR_BACK), tr(TR_SAVE)); /* T1.2: no heap */
 }


 int melPage = _melSelectIdx / 4;
 int startIdx = melPage * 4;
 int yBase = 40;
 int itemW = 285;

 for (int i = 0; i < 4; i++) {
 int actualIdx = startIdx + i;
 int y = yBase + (i * 38);

 _driver.canvas->fillScreen(C_BG_MAIN);

 if (actualIdx < TOTAL_VARIANTS) {
 bool isSelected = (actualIdx == _melSelectIdx);
 uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
 uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;

 _driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
 if (!isSelected) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(txt);
 _driver.canvas->setCursor(14, 24);
 _driver.canvas->print(MEL_NAMES[typeIdx][actualIdx]);
 }
 blitCanvas(_driver.canvas, 10, y, itemW, 34);
 }

 _forceSettingsRedraw = false;
}


bool DisplayManager::consumeTouchSound( ) {
 if (_touchSoundPending) {
 _touchSoundPending = false;
 return true;
 }
 return false;
}


bool DisplayManager::consumeErrorSound( ) {
 if (_errorSoundPending) {
 _errorSoundPending = false;
 return true;
 }
 return false;
}


/* Cross-core producers publish data BEFORE the flag with __dmb().
 * Without the barrier, Core 0 may see the flag true before the data
 * fields are visible (reordering visible on RP2040 across cores). */
void DisplayManager::requestPreviewSound(SoundEvent ev, uint8_t melIdx) {
 _previewType = (uint8_t)ev;
 _previewMelIdx = melIdx;
 __dmb( );
 _previewPending = true;
}

void DisplayManager::requestVolumePreview(uint8_t level) {
 _volumePreviewLevel = level;
 __dmb( );
 _volumePreviewPending = true;
}

void DisplayManager::requestAlarmVolumePreview(uint8_t level) {
 _alarmVolPreviewLevel = level;
 __dmb( );
 _alarmVolPreviewPending = true;
}


bool DisplayManager::consumePreviewSound(SoundEvent& outEvent, uint8_t& outIdx) {
 if (!_previewPending) return false;
 __dmb( ); /* Read data AFTER the flag */
 outEvent = (SoundEvent)_previewType;
 outIdx = _previewMelIdx;
 _previewPending = false;
 return true;
}


bool DisplayManager::consumeVolumePreview(uint8_t& outLevel) {
 if (!_volumePreviewPending) return false;
 __dmb( );
 outLevel = _volumePreviewLevel;
 _volumePreviewPending = false;
 return true;
}


/**
 * @brief Accepts a single touch — requires the finger to have been lifted
 * since the last accepted touch. Prevents hold-repeat.
 */


bool DisplayManager::consumeAlarmVolumePreview(uint8_t& outLevel) {
 if (!_alarmVolPreviewPending) return false;
 __dmb( );
 outLevel = _alarmVolPreviewLevel;
 _alarmVolPreviewPending = false;
 return true;
}

void DisplayManager::showSystemStatus( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_STATUS;
 _statusPage = 0;
 _statusLastDraw = 0;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::updateSystemStatus(const SystemStatusData& data) {
 _statusData = data;
}

/**
 * @brief Draws the system status screen with zero flicker.
 *
 * Uses canvas (strip rendering) for the entire content area.
 * 4 pages: System, Network, Sensors, Telemetry.
 * Auto-refresh every 1 second via timer in the render loop.
 */
void DisplayManager::drawSystemStatus( ) {
 bool fullRedraw = _forceSettingsRedraw;
 _forceSettingsRedraw = false;

 GFXcanvas16* cv = _driver.canvas;
 if (!cv) return;

 const SystemStatusData& d = _statusData;

 /* ── Header + Buttons (only on fullRedraw) ── */
 if (fullRedraw) {
 cv->fillScreen(C_BG_MAIN);
 /* Same title bar as every other screen; page dots come with it. */
 uiTitleBar(cv, 2, tr(TR_STATUS_TITLE), _statusPage, STATUS_PAGES, 30);
 blitCanvas(cv, 0, 0, 320, 33);

 /* Footer band via canvas: covers 195..239 wall to wall, so whatever
  * screen came before cannot leak through the gaps between buttons
  * (the alarm editor's SAVE, the dashboard's slot pills — both seen
  * on hardware back when only the button rects were painted). */
 blitFooterMenu(tr(TR_BACK), nullptr);
 }

 /*
 * Simple table in the UI font (9pt), same family as the rest of the
 * display — the classic terminal font only survives on the license
 * page, where character density is the point.
 * Each row: 20px. Useful area: y=34..194 = 160px -> 8 rows per page.
 * Label on left, value on right, separated by dotted line.
 */

 static char buf[64];
 static char fbuf[12];

 /* Build row array for the current page */
 struct Row { const char* lbl; char val[28]; uint16_t color; bool degc; };
 static Row rows[8];
 int nRows = 0;

 auto addRow = [&](const char* lbl, const char* val, uint16_t c = 0,
 bool degc = false) {
 if (nRows >= 8) return;
 rows[nRows].lbl = lbl;
 safeCopy(rows[nRows].val, val, sizeof(rows[nRows].val));
 rows[nRows].color = c ? c : C_TEXT_MAIN;
 rows[nRows].degc = degc;
 nRows++;
 };

 if (_statusPage == 0) {
 addRow("Device", d.deviceName);
 addRow("Firmware", d.fwVersion);
 /* Pico serial — key column in calib.csv for sensors with no 1-Wire
  * ROM (DHT22, BMP280); the row is picked by the sensor's hwId. */
 addRow("Serial", StorageManager::getBoardSerialNumber( ).c_str( ));
 unsigned long s = (unsigned long)d.uptimeSec;
 snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
 s/86400, (s%86400)/3600, (s%3600)/60, s%60);
 addRow("Uptime", buf);
 snprintf(buf, sizeof(buf), "%lu", (unsigned long)d.heapFree);
 addRow("Heap Free", buf, d.heapFree < 20000 ? C_TEMP_HOT : C_TEMP_OK);
 snprintf(buf, sizeof(buf), "%lu", (unsigned long)d.flashUsed);
 addRow("Flash Used", buf);
 fmtFloat1(fbuf, sizeof(fbuf), d.boardTemp);
 addRow("Board Temp", fbuf, 0, /*degc=*/true);
 snprintf(buf, sizeof(buf), "GMT%+d", (int)d.timezone);
 addRow("Timezone", buf);
 }
 else if (_statusPage == 1) {
 addRow("WiFi", d.wifiConnected ? "Connected" : "Disconnected",
 d.wifiConnected ? C_TEMP_OK : C_TEMP_HOT);
 addRow("SSID", d.ssid);
 addRow("IP", d.ip);
 addRow("MAC", d.mac);
 snprintf(buf, sizeof(buf), "%ld dBm", (long)d.rssi);
 uint16_t rc = (d.rssi > -60) ? C_TEMP_OK : (d.rssi > -80) ? C_ACCENT : C_TEMP_HOT;
 addRow("RSSI", buf, rc);
 addRow("NTP", d.ntpSynced ? "Synced" : "Not synced",
 d.ntpSynced ? C_TEMP_OK : C_TEMP_HOT);
 addRow("NTP Server", d.ntpServer);
 }
 else if (_statusPage == 2) {
 snprintf(buf, sizeof(buf), "%d", d.activeSensors);
 addRow("Active", buf);
 /* Sensor values available via dashboard panel display */
 }
 else if (_statusPage == 3) {
 addRow("Transport", d.telTransport == 1 ? "MQTT" : "HTTP");
 addRow("Server", d.telServer);
 snprintf(buf, sizeof(buf), "%u", (unsigned)d.telPending);
 addRow("Pending", buf, d.telPending > 50 ? C_TEMP_HOT : C_TEMP_OK);
 snprintf(buf, sizeof(buf), "%u", (unsigned)d.telFails);
 addRow("Fails", buf, d.telFails > 0 ? C_TEMP_HOT : C_TEMP_OK);
 snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)d.telInterval);
 addRow("Interval", buf);
 if (d.telTransport == 1) {
 addRow("MQTT", d.mqttConnected ? "Connected" : "Disconnected",
 d.mqttConnected ? C_TEMP_OK : C_TEMP_HOT);
 }
 }

 /* ── Render table with variable row height ──
 * Label on the left, value RIGHT-ALIGNED at x=312 — a long value
 * (Serial 16 hex, SSID, MAC) grows toward the label instead of off
 * the screen, so one line fits almost everything the old layout
 * wrapped. Wraps only when label + value truly collide.
 * RowH: normal = 20 px, wrapped = 34 px (two 9pt baselines).
 * Render row-by-row via _driver.canvas (320x45 fits both heights).
 * Stop when accumulated y exceeds yEnd — avoids writing over the
 * bottom button bar (at y=195). */
 const int valR = 312;
 const int yStart = 34;
 const int yEnd = 194;
 int curY = yStart;

 for (int ri = 0; ri < nRows; ri++) {
 cv->setFont(&simutFont9pt);
 cv->setTextSize(1);
 int16_t bx, by; uint16_t bw, bh;
 int16_t lx, ly; uint16_t lw, lh;
 cv->getTextBounds(rows[ri].val, 0, 0, &bx, &by, &bw, &bh);
 cv->getTextBounds(rows[ri].lbl, 0, 0, &lx, &ly, &lw, &lh);
 /* Measured, not guessed: "°C" in the 9pt face is ~24 px, and a fixed
  * reserve smaller than that pushed the C off the right edge. */
 const int degW = rows[ri].degc
 ? (int)uiUnitWidth(cv, "\xC2\xB0" "C") + 5 : 0;
 const bool wraps = (4 + (int)lw + 12 + (int)bw + degW > valR);
 const int rowH = wraps ? 34 : 20;

 if (curY + rowH > yEnd) break; /* no space, drop tail */

 /* Clear row area on canvas + draw content at local y. */
 cv->fillRect(0, 0, 320, rowH, C_BG_MAIN);

 if (!wraps) {
 cv->setTextColor(C_TEXT_SUB);
 cv->setCursor(4, 14);
 cv->print(rows[ri].lbl);
 cv->setTextColor(rows[ri].color);
 cv->setCursor((int16_t)(valR - degW - (int)bw), 14);
 cv->print(rows[ri].val);
 if (rows[ri].degc) {
 uiUnit(cv, (int16_t)(valR - degW + 5), 14, "\xC2\xB0" "C",
 rows[ri].color);
 /* degW = measured width + 5 px gap, so the C ends at valR. */
 }
 } else {
 /* Line 1: label at baseline 13. Line 2: value right-aligned at
 * baseline 29. Total 34 px. */
 cv->setTextColor(C_TEXT_SUB);
 cv->setCursor(4, 13);
 cv->print(rows[ri].lbl);
 cv->setTextColor(rows[ri].color);
 cv->setCursor((int16_t)(valR - (int)bw), 29);
 cv->print(rows[ri].val);
 }

 /* Dotted separator at row bottom. */
 int sepY = rowH - 2;
 for (int dx = 4; dx < 316; dx += 4)
 cv->drawPixel(dx, sepY, C_GRID);

 blitCanvas(cv, 0, curY, 320, rowH);
 curY += rowH;
 }

 /* Clear eventual residue from previous pages that had more rows. */
 if (curY < yEnd) {
 fastFillRect(0, curY, 320, yEnd - curY, C_BG_MAIN);
 }

 _statusLastDraw = millis( );

 /* Restore textSize to avoid contaminating other screens */
 cv->setTextSize(1);
 cv->setFont(NULL);
}
