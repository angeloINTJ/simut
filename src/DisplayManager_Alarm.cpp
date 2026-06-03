/**
 * @file DisplayManager_Alarm.cpp
 * @brief Alarm rendering: state setters, drawAlarmAction, slot helpers, flash.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 ).
 * drawAlarmAction usa fixCardCorners (Dashboard) — chamada
 * cross-file via this-> (member method).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"

void DisplayManager::setAlarmState(uint16_t slotMask, int8_t navSlot,
 bool ambTemp, bool ambHum) {
 _alarmSlotMask = slotMask;
 _alarmAmbientTemp = ambTemp;
 _alarmAmbientHum = ambHum;
 if (navSlot >= 0) _alarmNavPending = navSlot;
}


void DisplayManager::setAlarmSilenced(bool silenced, uint32_t endTime) {
 _alarmSilenced = silenced;
 _alarmSilenceEnd = endTime;
}


void DisplayManager::setAlarmDeactivated(bool deactivated) {
 _alarmDeactivated = deactivated;
}


void DisplayManager::showAlarmAction(int8_t slotIdx) {
 mutex_enter_blocking(&_stateMutex);
 _alarmActionSlot = slotIdx;
 _uiMode = MODE_ALARM_ACTION;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}


/* drawAlarmAction via strips (header + 3 botões).
 * Padrão "draw-everything-with-offset-per-strip" — cada strip desenha TODOS
 * os elementos no canvas (320×40), com Y offset apropriado pra strip.
 * Adafruit_GFX clipa automaticamente pixels fora da canvas → cada strip
 * mostra só a porção correspondente. ~5× mais CPU vs single-pass mas elimina
 * top-down visual (~80ms total render). */
void DisplayManager::drawAlarmAction( ) {
 if (!_canvasWide) return;
 if (!_forceSettingsRedraw) return;
 _forceSettingsRedraw = false;

 /* Header text — calculado uma vez, usado em todas as strips. */
 char headerBuf[40];
 if (_alarmActionSlot < 0) {
 snprintf(headerBuf, sizeof(headerBuf), "! %s", tr(TR_AMBIENT));
 } else {
 mutex_enter_blocking(&_stateMutex);
 char friendlyName[32];
 safeCopy(friendlyName, _sharedState.slotName, sizeof(friendlyName));
 mutex_exit(&_stateMutex);
 if (strlen(friendlyName) > 0) {
 snprintf(headerBuf, sizeof(headerBuf), "! %s", friendlyName);
 } else {
 snprintf(headerBuf, sizeof(headerBuf), "! Sensor %d", _alarmActionSlot);
 }
 }
 String silTxt = tr(TR_SILENCE_120S);
 String deactTxt = tr(TR_DEACTIVATE);
 String mmTxt = tr(TR_MINMAX);

 const int btnX = 20, btnW = 280, btnH = 45, btnR = 10;

 GFXcanvas16* cv = beginScreenRender( );
 if (!cv) return;

 int16_t bx, by; uint16_t bw, bh;

 for (int strip = 0; strip < 6; strip++) {
 cv->fillScreen(C_BG_MAIN);
 const int16_t yOff = -strip * RENDER_STRIP_H;

 /* Header (y_screen=4..52) — visível em strips 0 e 1 */
 cv->fillRect(4, 4 + yOff, 312, 48, RGB565(180, 30, 30));
 cv->setFont(&simutFont12pt);
 cv->setTextColor(RGB565(255, 255, 255));
 cv->getTextBounds(headerBuf, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor((320 - bw) / 2, 32 + yOff);
 cv->print(headerBuf);

 /* Silence button (y_screen=60..105) — visível em strips 1 e 2 */
 cv->fillRoundRect(btnX, 60 + yOff, btnW, btnH, btnR, RGB565(200, 100, 0));
 cv->setFont(&simutFont12pt);
 cv->setTextColor(RGB565(255, 255, 255));
 cv->getTextBounds(silTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(btnX + (btnW - bw) / 2, 90 + yOff);
 cv->print(silTxt);

 /* Deactivate button (y_screen=115..160) — visível em strips 2 e 3 */
 cv->fillRoundRect(btnX, 115 + yOff, btnW, btnH, btnR, RGB565(180, 30, 30));
 cv->getTextBounds(deactTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(btnX + (btnW - bw) / 2, 145 + yOff);
 cv->print(deactTxt);

 /* Main menu button (y_screen=170..215) — visível em strips 4 e 5 */
 cv->fillRoundRect(btnX, 170 + yOff, btnW, btnH, btnR, C_ACCENT);
 cv->setTextColor(C_BG_MAIN);
 cv->getTextBounds(mmTxt, 0, 0, &bx, &by, &bw, &bh);
 cv->setCursor(btnX + (btnW - bw) / 2, 200 + yOff);
 cv->print(mmTxt);

 commitScreenStrip(strip);
 }
 endScreenRender( );
}

bool DisplayManager::isSlotAlarming(int slotIdx) const {
 return (slotIdx >= 0 && slotIdx < 16) && (_alarmSlotMask & (1 << slotIdx));
}

uint16_t DisplayManager::slotAlarmBg(int slotIdx) const {
 if (!isSlotAlarming(slotIdx)) return C_CARD_BG;

 if (_alarmSilenced) return C_CARD_BG;
 return _alarmFlashPhase ? RGB565(180, 30, 30) : C_CARD_BG;
}

bool DisplayManager::isAnyAlarmActive( ) const {
 return (_alarmSlotMask != 0) || _alarmAmbientTemp || _alarmAmbientHum;
}


void DisplayManager::redrawAlarmFlash( ) {
 if (!_tft || !_canvasSmall || !_canvasWide) return;

 if (_alarmAmbientTemp || _alarmAmbientHum) {
 drawAmbientPanel(_lastRenderedState.ambientTemp,
 _lastRenderedState.ambientHum,
 _lastRenderedState.ambientValid);
 }

 int sel = _lastRenderedState.selectedSlotIdx;
 if (isSlotAlarming(sel)) {
 drawSlotPanel(_lastRenderedState.slotTemp, _lastRenderedState.slotValid,
 sel, _lastRenderedState.slotName, true);
 }


 bool pageHasAlarm = false;
 bool otherPageHasAlarm = false;
 for (int i = 0; i < 10; i++) {
 if (!isSlotAlarming(i)) continue;
 int slotPage = (i < 4) ? 0 : (i < 8) ? 1 : 2;
 if (slotPage == _currentPage) pageHasAlarm = true;
 else otherPageHasAlarm = true;
 }
 if (pageHasAlarm || otherPageHasAlarm) {
 drawBottomButtons(sel, true);
 }
}

