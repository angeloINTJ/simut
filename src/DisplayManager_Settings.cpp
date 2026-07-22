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
 _driver.tft->fillScreen(C_BG_MAIN);
 _driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 _driver.tft->setCursor(10, 22); _driver.tft->print(tr(TR_CONFIG_THEMES));

 int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
 _driver.tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 const char* backTxt = tr(TR_BACK);
 _driver.tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(141 + (75 - bw)/2, btnY + 25); _driver.tft->print(backTxt);
 _driver.tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
 _driver.tft->setTextColor(C_BG_MAIN);
 String appTxt = tr(TR_APPLY);
 _driver.tft->getTextBounds(appTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(222 + (93 - bw)/2, btnY + 25); _driver.tft->print(appTxt);
 }

 if (fullRedraw || pageChanged) {
 int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
 _driver.tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
 _driver.tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
 int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
 int thumbY = trackY; if (totalPages > 1) { thumbY += (_themePage * (trackH - thumbH)) / (totalPages - 1); }
 _driver.tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
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
 _driver.tft->fillScreen(C_BG_MAIN);
 _driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 _driver.tft->setCursor(10, 22); _driver.tft->print(tr(TR_ALARMS_TITLE));

 int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
 _driver.tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
 /* Back button fills the entire remaining width */
 _driver.tft->fillRoundRect(141, btnY, 174, btnH, 8, C_ACCENT);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_BG_MAIN);
 const char* backTxt = tr(TR_BACK);
 _driver.tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(141 + (174 - bw)/2, btnY + 25); _driver.tft->print(backTxt);
 }

 if (fullRedraw || pageChanged) {
 int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
 _driver.tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
 _driver.tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
 int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
 int thumbY = trackY; if (totalPages > 1) { thumbY += (_alarmPage * (trackH - thumbH)) / (totalPages - 1); }
 _driver.tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
 }

 int startIdx = _alarmPage * 4; int yBase = 40; int itemW = 285;
 for (int i = 0; i < 4; i++) {
 int y = yBase + (i * 38); int mapIdx = startIdx + i;

 /* Only redraw items whose selection state changed, or on fullRedraw/pageChanged */
 if (!fullRedraw && !pageChanged) {
 if (mapIdx != _alarmSelection && mapIdx != _lastAlarmSelection) continue;
 }

 _driver.canvas->fillScreen(C_BG_MAIN);
 if (mapIdx < _activeSensorCount) {
 int actualSensorId = _activeSensorsMap[mapIdx];
 SensorRecord* rec = &_sysConfigPtr->sensors[actualSensorId];
 bool isSelected = (mapIdx == _alarmSelection);
 uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
 uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
 if (!isSelected) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

 /* Measure ON/OFF indicator width to reserve space */
 const char* statusTxt = rec->alarmsActive ? tr(TR_ON) : tr(TR_OFF);
 _driver.canvas->setFont(&simutFont9pt);
 int16_t sx1, sy1; uint16_t sw, sh;
 _driver.canvas->getTextBounds(statusTxt, 0, 0, &sx1, &sy1, &sw, &sh);
 int statusAreaW = (int)sw + 20; /* 10px margin on each side */

 /* Sensor name — truncated if needed to avoid collision */
 int maxNameW = itemW - statusAreaW - 15;
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
 blitCanvas(_driver.canvas, 10, y, itemW, 34);
 }
 _forceSettingsRedraw = false; _lastAlarmPage = _alarmPage; _lastAlarmSelection = _alarmSelection;
}

void DisplayManager::showAlarmEdit(int sensorIdx) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_ALARM_EDIT; _editSensorIdx = sensorIdx;
 _tempAlarmConfig = _sysConfigPtr->sensors[sensorIdx];
 _editFieldFocus = 0; _forceSettingsRedraw = true; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::drawAlarmEdit( ) {
 bool hasHum = (_editSensorIdx == -1 || sensorHasHumidity((SensorType)_tempAlarmConfig.sensorType));


 if (_tempAlarmConfig.tempMin >= _tempAlarmConfig.tempMax) {
 _tempAlarmConfig.tempMax = _tempAlarmConfig.tempMin + 0.1f;
 _tempAlarmConfig.tempMax = round(_tempAlarmConfig.tempMax * 10.0f) / 10.0f;
 }
 if (hasHum && _tempAlarmConfig.humMin >= _tempAlarmConfig.humMax) {
 _tempAlarmConfig.humMax = _tempAlarmConfig.humMin + 0.1f;
 _tempAlarmConfig.humMax = round(_tempAlarmConfig.humMax * 10.0f) / 10.0f;
 if (_tempAlarmConfig.humMax > 100.0f) {
 _tempAlarmConfig.humMax = 100.0f;
 _tempAlarmConfig.humMin = 99.9f;
 }
 }

 if (_forceSettingsRedraw) {
 _driver.tft->fillScreen(C_BG_MAIN);
 _driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 int16_t tx1, ty1; uint16_t tw, th;
 String titleTxt = String(_tempAlarmConfig.friendlyName);
 _driver.tft->getTextBounds(titleTxt, 0, 0, &tx1, &ty1, &tw, &th);
 _driver.tft->setCursor((320 - tw) / 2, 22); _driver.tft->print(titleTxt);
 _driver.tft->setTextColor(C_TEXT_SUB); _driver.tft->setCursor(10, 52); _driver.tft->print(tr(TR_TEMP));
 if (hasHum) { _driver.tft->setCursor(10, 122); _driver.tft->print(tr(TR_HUMIDITY)); }
 int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
 _driver.tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(36, btnY + 26, 26, btnY + 12, 46, btnY + 12, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(104, btnY + 12, 94, btnY + 26, 114, btnY + 26, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 const char* backTxt = tr(TR_BACK);
 _driver.tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(141 + (75 - bw)/2, btnY + 25); _driver.tft->print(backTxt);
 _driver.tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
 _driver.tft->setTextColor(C_BG_MAIN);
 String saveTxt = tr(TR_SAVE);
 _driver.tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(222 + (93 - bw)/2, btnY + 25); _driver.tft->print(saveTxt);
 _forceSettingsRedraw = false;
 }
 auto drawBox = [&](int fieldId, int x, int y, const char* label, float val, bool isHum) {
 _driver.canvasSmall->fillScreen(C_BG_MAIN);
 bool focused = (_editFieldFocus == fieldId);
 uint16_t bg = focused ? C_ACCENT : C_CARD_BG;
 uint16_t txt = focused ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvasSmall->fillRoundRect(0, 0, 140, 40, 10, bg);
 if (!focused) _driver.canvasSmall->drawRoundRect(0, 0, 140, 40, 10, C_TEXT_SUB);
 _driver.canvasSmall->setFont(&simutFont9pt); _driver.canvasSmall->setTextColor(focused ? C_BG_MAIN : C_TEXT_SUB);
 _driver.canvasSmall->setCursor(8, 17); _driver.canvasSmall->print(label);
 _driver.canvasSmall->setFont(&simutFont12pt); _driver.canvasSmall->setTextColor(txt);
 char intPart[8]; char decPart[4];
 if (val < 0 && val > -1.0) { snprintf(intPart, sizeof(intPart), "-0"); } else { snprintf(intPart, sizeof(intPart), "%d", (int)val); }
 int fractional = abs((int)round(val * 10.0f) % 10);
 snprintf(decPart, sizeof(decPart), ".%d", fractional);
 int textAnchor = 98; int16_t bx, by; uint16_t bw, bh;
 _driver.canvasSmall->getTextBounds(intPart, 0, 0, &bx, &by, &bw, &bh);
 _driver.canvasSmall->setCursor(textAnchor - bw, 32); _driver.canvasSmall->print(intPart);
 _driver.canvasSmall->setCursor(textAnchor, 32); _driver.canvasSmall->print(decPart);
 _driver.canvasSmall->setFont(NULL); _driver.canvasSmall->setCursor(122, 20);
 if (isHum) _driver.canvasSmall->print("%"); else _driver.canvasSmall->print("C");
 blitCanvas(_driver.canvasSmall, x, y, 140, 40);
 };
 drawBox(0, 10, 60, "MIN", _tempAlarmConfig.tempMin, false); drawBox(1, 160, 60, "MAX", _tempAlarmConfig.tempMax, false);
 if (hasHum) { drawBox(2, 10, 130, "MIN", _tempAlarmConfig.humMin, true); drawBox(3, 160, 130, "MAX", _tempAlarmConfig.humMax, true); }
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
 _driver.tft->fillScreen(C_BG_MAIN);
 _driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 _driver.tft->setCursor(10, 22); _driver.tft->print(tr(TR_CONFIG_MAIN));

 int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
 _driver.tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 const char* backTxt = tr(TR_BACK);
 _driver.tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(141 + (75 - bw)/2, btnY + 25); _driver.tft->print(backTxt);
 _driver.tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
 _driver.tft->setTextColor(C_BG_MAIN);
 String enterTxt = tr(TR_ENTER);
 _driver.tft->getTextBounds(enterTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(222 + (93 - bw)/2, btnY + 25); _driver.tft->print(enterTxt);
 }

 if (fullRedraw || pageChanged) {
 int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
 _driver.tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
 _driver.tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
 int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
 int thumbY = trackY; if (totalPages > 1) { thumbY += (_mainMenuPage * (trackH - thumbH)) / (totalPages - 1); }
 _driver.tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
 }

 int startIdx = _mainMenuPage * 4; int yBase = 40; int itemW = 285;
 for (int i = 0; i < 4; i++) {
 int y = yBase + (i * 38); int mapIdx = startIdx + i;
 _driver.canvas->fillScreen(C_BG_MAIN);
 _driver.canvas->setTextSize(1); /* Ensures reset after status screen */
 if (mapIdx < TOTAL_ITEMS) {
 bool isSelected = (mapIdx == _menuSelection);
 uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
 uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
 if (!isSelected) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(txt);
 _driver.canvas->setCursor(10, 24); _driver.canvas->print(tr(menuItems[mapIdx]));
 _driver.canvas->fillTriangle(itemW - 20, 11, itemW - 20, 23, itemW - 10, 17, isSelected ? C_BG_MAIN : C_TEXT_SUB);
 }
 blitCanvas(_driver.canvas, 10, y, itemW, 34);
 }
 _forceSettingsRedraw = false; _lastMainMenuPage = _mainMenuPage;
}




void DisplayManager::showSettingsPassword( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_PASSWORD;
 _kbLayer = 0;
 _kbShiftLock = false;
 _kbCursor = 0;
 _kbShowRaw = false;
 _kbPhase = 0;
 _kbMsgKey = TR_KEYS_COUNT;
 _kbSelRow = 0;
 _kbSelCol = 0;
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

 _driver.tft->fillScreen(C_BG_MAIN);


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

void DisplayManager::drawSettingsPassword( ) {
 if (!_driver.tft) return;


 if (_kbPhase >= 2) {
 drawPasswordMessage( );
 _forceSettingsRedraw = false;
 return;
 }


 static const char layer0[3][10] = {
 {'q','w','e','r','t','y','u','i','o','p'},
 {'a','s','d','f','g','h','j','k','l','.'},
 {'z','x','c','v','b','n','m',',','!','?'}
 };
 static const char layer1[3][10] = {
 {'Q','W','E','R','T','Y','U','I','O','P'},
 {'A','S','D','F','G','H','J','K','L',':'},
 {'Z','X','C','V','B','N','M',';','"','\''}
 };
 static const char layer2[3][10] = {
 {'1','2','3','4','5','6','7','8','9','0'},
 {'@','#','$','%','&','*','-','+','=','~'},
 {'(',')','[',']','{','}','/','\\','^','_'}
 };


 const char (*activeLayer)[10] = (_kbLayer == 2) ? layer2
 : (_kbLayer == 1) ? layer1
 : layer0;


 char* activeBuf = (_kbPhase == 0) ? _kbBuffer : _kbConfirmBuf;

 int16_t x1, y1; uint16_t w, h_bound;
 bool fullRedraw = _forceSettingsRedraw;


 if (fullRedraw) {
 _driver.tft->fillScreen(C_BG_MAIN);
 }

 /* Title — always redraw via canvas (changes between phases) */
 {
 /* Full-width bar without rounded corners */
 _driver.canvas->fillScreen(C_CARD_BG);
 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(C_TEXT_MAIN);
 _driver.canvas->setCursor(14, 18);
 _driver.canvas->print((_kbPhase == 0) ? tr(TR_NEW_PASSWORD) : tr(TR_CONFIRM_PASSWORD));

 /* X button overlaid on bar — y=4 keeps 4 px top margin,
 * resisting display offset -4V without clipping upper lines. */
 _driver.canvas->fillRoundRect(282, 4, 30, 22, 4, C_TEMP_WARM);
 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(C_BG_MAIN);
 _driver.canvas->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
 _driver.canvas->setCursor(297 - w / 2, 20); _driver.canvas->print("X");

 /* Blit with dstY=4 pushes the header 4 px down on screen — avoids
 * top clipping at offset -4V. h goes to 30 to match the
 * vertical extent of content (title + X button up to y=26). */
 blitCanvas(_driver.canvas, 0, 4, 320, 30);
 }


 {
 const int MAX_BOXES = 7;
 const int MIN_BOXES = 4;
 const int boxW = 32, boxH = 28, gap = 6;
 const int startY = 33;
 const int stripH = boxH + 10;

 /*
 * Number of visible boxes: in phase 0 (typing), shows the max
 * between MIN_BOXES and (cursor + 1), up to MAX_BOXES.
 * In phase 1 (confirmation), shows exactly the password length
 * already set in _kbBuffer.
 */
 int visibleBoxes;
 if (_kbPhase == 1) {
 visibleBoxes = (int)strlen(_kbBuffer);
 } else {
 visibleBoxes = _kbCursor + 1;
 if (visibleBoxes < MIN_BOXES) visibleBoxes = MIN_BOXES;
 if (visibleBoxes > MAX_BOXES) visibleBoxes = MAX_BOXES;
 }

 int totalW = visibleBoxes * boxW + (visibleBoxes - 1) * gap;
 int startX = (320 - totalW) / 2;

 /* Draw boxes + counter in canvas to avoid flicker */
 _driver.canvas->fillScreen(C_BG_MAIN);

 for (int i = 0; i < visibleBoxes; i++) {
 int bx = startX + i * (boxW + gap);
 bool filled = (i < _kbCursor);
 bool isRequired = (i < MIN_BOXES);

 /* Rounded box */
 _driver.canvas->fillRoundRect(bx, 0, boxW, boxH, 4, C_CARD_BG);

 /* Border with conditional color */
 uint16_t borderColor = isRequired ? C_ACCENT_HIGH : C_TEXT_OFF;
 if (i == _kbCursor && _kbCursor < visibleBoxes) borderColor = C_ACCENT;
 _driver.canvas->drawRoundRect(bx, 0, boxW, boxH, 4, borderColor);

 if (filled) {
 if (_kbShowRaw) {
 /* Show real character */
 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setTextColor(C_TEXT_MAIN);
 char ch[2] = { activeBuf[i], '\0' };
 int16_t cx1, cy1; uint16_t cw, ch1;
 _driver.canvas->getTextBounds(ch, 0, 0, &cx1, &cy1, &cw, &ch1);
 _driver.canvas->setCursor(bx + (boxW - cw) / 2, 20);
 _driver.canvas->print(ch);
 } else {
 /* Show masked dot */
 _driver.canvas->fillCircle(bx + boxW / 2, boxH / 2, 5, C_TEXT_MAIN);
 }
 }
 }

 /* Counter below boxes */
 char countBuf[8];
 snprintf(countBuf, sizeof(countBuf), "%d / %d", _kbCursor, visibleBoxes);
 uint16_t countColor = (_kbCursor < MIN_BOXES) ? C_TEXT_OFF : C_ACCENT;
 _driver.canvas->setFont(NULL); _driver.canvas->setTextSize(1);
 _driver.canvas->setTextColor(countColor);
 int16_t cx1, cy1; uint16_t cw, ch1;
 _driver.canvas->getTextBounds(countBuf, 0, 0, &cx1, &cy1, &cw, &ch1);
 _driver.canvas->setCursor((320 - cw) / 2, boxH + 3);
 _driver.canvas->print(countBuf);

 /* Single blit — no flicker */
 blitCanvas(_driver.canvas, 0, startY, 320, stripH);
 }


 {
 const int keyW = 30, keyH = 30, gap = 2;
 const int startX = 1, startY = 72;

 /* Draw one row of keys at a time via canvas to avoid flicker */
 for (int row = 0; row < 3; row++) {
 int ky = startY + row * (keyH + gap);

 /* Fill canvas with screen background */
 _driver.canvas->fillScreen(C_BG_MAIN);

 for (int col = 0; col < 10; col++) {
 int kx = startX + col * (keyW + gap);
 char ch = activeLayer[row][col];
 bool selected = (row == _kbSelRow && col == _kbSelCol);

 /* Key with rounded corners — highlight if selected */
 uint16_t keyBg = selected ? C_ACCENT : C_CARD_BG;
 uint16_t keyFg = selected ? C_BG_MAIN : C_TEXT_MAIN;
 uint16_t hintFg = selected ? C_BG_MAIN : C_TEXT_OFF;

 _driver.canvas->fillRoundRect(kx, 0, keyW, keyH, 4, keyBg);
 if (!selected) _driver.canvas->drawRoundRect(kx, 0, keyW, keyH, 4, C_TEXT_SUB);

 /* Main character centered */
 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setTextColor(keyFg);
 char label[2] = {ch, '\0'};
 int16_t lx1, ly1; uint16_t lw, lh;
 _driver.canvas->getTextBounds(label, 0, 0, &lx1, &ly1, &lw, &lh);
 _driver.canvas->setCursor(kx + (keyW - lw) / 2 - lx1, (keyH - lh) / 2 - ly1);
 _driver.canvas->print(label);

 /* Alternate layer hint — upper right corner, inside the key */
 char hint = '\0';
 if (_kbLayer == 0) hint = layer2[row][col];
 else if (_kbLayer == 1) hint = layer2[row][col];
 else hint = layer0[row][col];
 _driver.canvas->setFont(NULL); _driver.canvas->setTextSize(1);
 _driver.canvas->setTextColor(hintFg);
 char hintStr[2] = {hint, '\0'};
 int16_t hx1, hy1; uint16_t hw, hh;
 _driver.canvas->getTextBounds(hintStr, 0, 0, &hx1, &hy1, &hw, &hh);
 _driver.canvas->setCursor(kx + keyW - (int)hw - 4, 3);
 _driver.canvas->print(hintStr);
 }

 /* Blit the entire row at once — no flicker */
 blitCanvas(_driver.canvas, 0, ky, 320, keyH);
 }
 }


 {
 /*
 * Action bar: Shift, 123, Space, Backspace, OK.
 * Same total width as the key rows (x=1..319).
 * Shift=48, 123=48, Space=118, Backspace=48, OK=48, gap=2.
 */
 const int barY = 170, barH = 22;
 const int bx0 = 1; /* Shift */
 const int bx1 = 51; /* 123 */
 const int bx2 = 101; /* Space */
 const int bx3 = 221; /* Backspace */
 const int bx4 = 271; /* OK */
 const int bw01 = 48; /* Shift and 123 */
 const int bw2 = 118; /* Space */
 const int bw34 = 48; /* Backspace and OK */
 bool barActive = (_kbSelRow == 3);

 _driver.canvas->fillScreen(C_BG_MAIN);

 /* Shift button (col 0) */
 {
 bool layerActive = (_kbLayer == 1) || _kbShiftLock;
 bool sel = barActive && (_kbSelCol == 0);
 uint16_t bg = sel ? C_ACCENT_HIGH : (layerActive ? C_ACCENT : C_CARD_BG);
 uint16_t fg = (sel || layerActive) ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(bx0, 0, bw01, barH, 4, bg);
 if (!sel && !layerActive) _driver.canvas->drawRoundRect(bx0, 0, bw01, barH, 4, C_TEXT_SUB);
 if (sel) _driver.canvas->drawRoundRect(bx0, 0, bw01, barH, 4, C_ACCENT);
 int cx = bx0 + bw01 / 2, cy = 5;
 _driver.canvas->fillTriangle(cx - 5, cy + 5, cx, cy, cx + 5, cy + 5, fg);
 _driver.canvas->fillRect(cx - 2, cy + 5, 4, 6, fg);
 if (_kbShiftLock) {
 _driver.canvas->drawFastHLine(bx0 + 10, barH - 3, 28, fg);
 }
 }

 /* 123 button (col 1) */
 {
 bool layerActive = (_kbLayer == 2);
 bool sel = barActive && (_kbSelCol == 1);
 uint16_t bg = sel ? C_ACCENT_HIGH : (layerActive ? C_ACCENT : C_CARD_BG);
 uint16_t fg = (sel || layerActive) ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(bx1, 0, bw01, barH, 4, bg);
 if (!sel && !layerActive) _driver.canvas->drawRoundRect(bx1, 0, bw01, barH, 4, C_TEXT_SUB);
 if (sel) _driver.canvas->drawRoundRect(bx1, 0, bw01, barH, 4, C_ACCENT);
 _driver.canvas->setFont(NULL); _driver.canvas->setTextSize(1);
 _driver.canvas->setTextColor(fg);
 int16_t tx1, ty1; uint16_t tw, th;
 _driver.canvas->getTextBounds("123", 0, 0, &tx1, &ty1, &tw, &th);
 _driver.canvas->setCursor(bx1 + (bw01 - (int)tw) / 2, (barH - (int)th) / 2);
 _driver.canvas->print("123");
 }

 /* Space bar (col 2) */
 {
 bool sel = barActive && (_kbSelCol == 2);
 uint16_t bg = sel ? C_ACCENT_HIGH : C_CARD_BG;
 _driver.canvas->fillRoundRect(bx2, 0, bw2, barH, 4, bg);
 if (sel) _driver.canvas->drawRoundRect(bx2, 0, bw2, barH, 4, C_ACCENT);
 else _driver.canvas->drawRoundRect(bx2, 0, bw2, barH, 4, C_TEXT_SUB);
 uint16_t lineCol = sel ? C_BG_MAIN : C_TEXT_OFF;
 int lineX = bx2 + 20;
 int lineW = bw2 - 40;
 _driver.canvas->drawFastHLine(lineX, 14, lineW, lineCol);
 }

 /* Backspace button (col 3) */
 {
 bool sel = barActive && (_kbSelCol == 3);
 uint16_t bg = sel ? C_ACCENT_HIGH : C_CARD_BG;
 uint16_t fg = sel ? C_BG_MAIN : C_TEXT_MAIN;
 _driver.canvas->fillRoundRect(bx3, 0, bw34, barH, 4, bg);
 if (sel) _driver.canvas->drawRoundRect(bx3, 0, bw34, barH, 4, C_ACCENT);
 else _driver.canvas->drawRoundRect(bx3, 0, bw34, barH, 4, C_TEXT_SUB);
 int cx = bx3 + bw34 / 2, cy = barH / 2;
 _driver.canvas->fillTriangle(cx - 8, cy, cx - 2, cy - 5, cx - 2, cy + 5, fg);
 _driver.canvas->fillRect(cx - 2, cy - 3, 10, 6, fg);
 }

 /* OK button (col 4) */
 {
 bool sel = barActive && (_kbSelCol == 4);
 uint16_t bg = sel ? C_ACCENT_HIGH : C_ACCENT;
 uint16_t fg = C_BG_MAIN;
 _driver.canvas->fillRoundRect(bx4, 0, bw34, barH, 4, bg);
 if (sel) _driver.canvas->drawRoundRect(bx4, 0, bw34, barH, 4, C_BG_MAIN);
 _driver.canvas->setFont(NULL); _driver.canvas->setTextSize(1);
 _driver.canvas->setTextColor(fg);
 int16_t tx1, ty1; uint16_t tw, th;
 _driver.canvas->getTextBounds("OK", 0, 0, &tx1, &ty1, &tw, &th);
 _driver.canvas->setCursor(bx4 + (bw34 - (int)tw) / 2, (barH - (int)th) / 2);
 _driver.canvas->print("OK");
 }

 blitCanvas(_driver.canvas, 0, barY, 320, barH);
 }


 {
 /*
 * 5 navigation buttons in dashboard style (58x40, radius 12).
 * ▲ ▼ ◄ ► ✓(confirm character)
 * Positioned at the bottom of the screen (Y=195).
 */
 const int btnW = 58, btnH = 40, gap = 5, startX = 5;
 const int navY = 195;

 _driver.canvas->fillScreen(C_BG_MAIN);

 /* ◄ button (left) */
 {
 _driver.canvas->fillRoundRect(startX, 0, btnW, btnH, 12, C_CARD_BG);
 int cx = startX + btnW / 2, cy = btnH / 2;
 _driver.canvas->fillTriangle(cx + 6, cy - 8, cx + 6, cy + 8, cx - 8, cy, C_TEXT_MAIN);
 }

 /* ► button (right) */
 {
 int bx = startX + (btnW + gap);
 _driver.canvas->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
 int cx = bx + btnW / 2, cy = btnH / 2;
 _driver.canvas->fillTriangle(cx - 6, cy - 8, cx - 6, cy + 8, cx + 8, cy, C_TEXT_MAIN);
 }

 /* ▲ button (up) */
 {
 int bx = startX + 2 * (btnW + gap);
 _driver.canvas->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
 int cx = bx + btnW / 2, cy = btnH / 2;
 _driver.canvas->fillTriangle(cx - 8, cy + 6, cx + 8, cy + 6, cx, cy - 8, C_TEXT_MAIN);
 }

 /* ▼ button (down) */
 {
 int bx = startX + 3 * (btnW + gap);
 _driver.canvas->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
 int cx = bx + btnW / 2, cy = btnH / 2;
 _driver.canvas->fillTriangle(cx - 8, cy - 6, cx + 8, cy - 6, cx, cy + 8, C_TEXT_MAIN);
 }

 /* ✓ button (confirm selected character) */
 {
 int bx = startX + 4 * (btnW + gap);
 _driver.canvas->fillRoundRect(bx, 0, btnW, btnH, 12, C_ACCENT);
 int cx = bx + btnW / 2, cy = btnH / 2;
 /* Check icon */
 _driver.canvas->drawLine(cx - 8, cy, cx - 3, cy + 6, C_BG_MAIN);
 _driver.canvas->drawLine(cx - 7, cy, cx - 2, cy + 6, C_BG_MAIN);
 _driver.canvas->drawLine(cx - 3, cy + 6, cx + 8, cy - 6, C_BG_MAIN);
 _driver.canvas->drawLine(cx - 2, cy + 6, cx + 9, cy - 6, C_BG_MAIN);
 }

 blitCanvas(_driver.canvas, 0, navY, 320, btnH);
 }

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
 const char* msgL1 = isPt ? "Todos os sons serao" : "All sounds will be";
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
 _driver.tft->fillScreen(C_BG_MAIN);
 _driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 _driver.tft->setCursor(10, 22); _driver.tft->print(tr(TR_SOUNDS_TITLE));

 int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

 _driver.tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);

 _driver.tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);

 _driver.tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 const char* backTxt = tr(TR_BACK);
 _driver.tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(141 + (75 - bw) / 2, btnY + 25); _driver.tft->print(backTxt);

 _driver.tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
 _driver.tft->setTextColor(C_BG_MAIN);
 String saveTxt = tr(TR_SAVE);
 _driver.tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(222 + (93 - bw) / 2, btnY + 25); _driver.tft->print(saveTxt);
 }


 if (fullRedraw || pageChanged) {
 int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
 _driver.tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
 _driver.tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
 int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
 int thumbY = trackY;
 if (totalPages > 1) { thumbY += (soundPage * (trackH - thumbH)) / (totalPages - 1); }
 _driver.tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
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
 const char* attentionLabel = isPt ? "Atencao" : "Attention";
 const char* muteLabel = isPt ? "Mudo Global" : "Mute All";

 int startIdx = soundPage * 4;
 int yBase = 40; int itemW = 285;

 for (int i = 0; i < 4; i++) {
 int actualIdx = startIdx + i;
 int y = yBase + (i * 38);

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
 _driver.tft->fillScreen(C_BG_MAIN);


 _driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_ACCENT);
 _driver.tft->setCursor(10, 22);
 if (typeIdx == 4) {
 _driver.tft->print(isPt ? "Atencao" : "Attention");
 } else {
 _driver.tft->print(tr(TYPE_LABELS[typeIdx]));
 }


 int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

 _driver.tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);

 _driver.tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);

 _driver.tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 const char* backTxt = tr(TR_BACK);
 _driver.tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(141 + (75 - bw) / 2, btnY + 25); _driver.tft->print(backTxt);

 _driver.tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
 _driver.tft->setTextColor(C_BG_MAIN);
 String saveTxt = tr(TR_SAVE);
 _driver.tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(222 + (93 - bw) / 2, btnY + 25); _driver.tft->print(saveTxt);
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
 cv->fillScreen(C_CARD_BG);
 cv->setFont(&simutFont9pt);
 cv->setTextColor(C_TEXT_MAIN);
 cv->setCursor(10, 20); cv->print(tr(TR_STATUS_TITLE));

 /* Page dots */
 cv->setFont(NULL); cv->setTextSize(1);
 for (int p = 0; p < STATUS_PAGES; p++) {
 int dx = 280 + p * 10;
 if (p == _statusPage) cv->fillCircle(dx, 14, 3, C_ACCENT);
 else cv->drawCircle(dx, 14, 2, C_TEXT_OFF);
 }
 blitCanvas(cv, 0, 0, 320, 28);

 /* ← → BACK buttons */
 _driver.tft->fillRoundRect(5, 195, 62, 40, 8, C_CARD_BG);
 _driver.tft->fillTriangle(36, 207, 26, 221, 46, 221, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(73, 195, 62, 40, 8, C_CARD_BG);
 _driver.tft->fillTriangle(104, 221, 94, 207, 114, 207, C_TEXT_MAIN);
 _driver.tft->fillRoundRect(141, 195, 75, 40, 8, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
 int16_t bx, by; uint16_t bw, bh;
 const char* bt = tr(TR_BACK);
 _driver.tft->getTextBounds(bt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(141 + (75 - bw) / 2, 220); _driver.tft->print(bt);
 }

 /*
 * Simple table: NULL font size 2 (12x16px).
 * Each row: 20px (16px text + 4px gap).
 * Useful area: y=28..194 = 166px -> 8 rows per page.
 * Label on left, value on right, separated by dotted line.
 */

 static char buf[64];
 static char fbuf[12];

 /* Build row array for the current page */
 struct Row { const char* lbl; char val[28]; uint16_t color; };
 static Row rows[8];
 int nRows = 0;

 auto addRow = [&](const char* lbl, const char* val, uint16_t c = 0) {
 if (nRows >= 8) return;
 rows[nRows].lbl = lbl;
 safeCopy(rows[nRows].val, val, sizeof(rows[nRows].val));
 rows[nRows].color = c ? c : C_TEXT_MAIN;
 nRows++;
 };

 if (_statusPage == 0) {
 addRow("Device", d.deviceName);
 addRow("Firmware", d.fwVersion);
 /* Pico serial — key for calib.csv for ambient (DHT22). */
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
 snprintf(buf, sizeof(buf), "%s oC", fbuf);
 addRow("Board Temp", buf);
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
 * Long values (SSID up to 32 chars, MAC 17 chars) that don't fit
 * in valX..312 with font size 2 get 2 lines (label top, value with
 * indent on the line below) — both size 2 as requested. RowH:
 * normal = 20 px (size 2 text = 16 px + 4 gap/separator)
 * wrapped = 32 px (16 + 16 = 2 tight lines, zero gap)
 * Render row-by-row via _driver.canvas (320x45 fits both heights).
 * Stop when accumulated y exceeds yEnd — avoids writing over the
 * bottom button bar (at y=195). Page 2 (network) with long
 * SSID/MAC/NTP Server values gets close to the limit. */
 const int valX = 150;
 const int yStart = 28;
 const int yEnd = 194;
 int curY = yStart;

 for (int ri = 0; ri < nRows; ri++) {
 cv->setFont(NULL);
 cv->setTextSize(2);
 int16_t bx, by; uint16_t bw, bh;
 cv->getTextBounds(rows[ri].val, 0, 0, &bx, &by, &bw, &bh);
 const bool wraps = (valX + (int)bw > 312);
 const int rowH = wraps ? 32 : 20;

 if (curY + rowH > yEnd) break; /* no space, drop tail */

 /* Clear row area on canvas + draw content at local y. */
 cv->fillRect(0, 0, 320, rowH, C_BG_MAIN);

 if (!wraps) {
 cv->setTextColor(C_TEXT_SUB);
 cv->setCursor(4, 2);
 cv->print(rows[ri].lbl);
 cv->setTextColor(rows[ri].color);
 cv->setCursor(valX, 2);
 cv->print(rows[ri].val);
 } else {
 /* Line 1: label size 2 at y=0 (top). Line 2: value size 2
 * at y=16 with 16 px indent. Total 32 px. */
 cv->setTextColor(C_TEXT_SUB);
 cv->setCursor(4, 0);
 cv->print(rows[ri].lbl);
 cv->setTextColor(rows[ri].color);
 cv->setCursor(16, 16);
 cv->print(rows[ri].val);
 }

 /* Dotted separator at row bottom. */
 int sepY = rowH - 3;
 for (int dx = 4; dx < 316; dx += 4)
 cv->drawPixel(dx, sepY, C_GRID);

 blitCanvas(cv, 0, curY, 320, rowH);
 curY += rowH;
 }

 /* Clear eventual residue from previous pages that had more rows. */
 if (curY < yEnd) {
 _driver.tft->fillRect(0, curY, 320, yEnd - curY, C_BG_MAIN);
 }

 _statusLastDraw = millis( );

 /* Restore textSize to avoid contaminating other screens */
 cv->setTextSize(1);
 cv->setFont(NULL);
}
