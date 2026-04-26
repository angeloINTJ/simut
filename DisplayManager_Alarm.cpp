/**
 * @file    DisplayManager_Alarm.cpp
 * @brief   Alarm rendering: state setters, drawAlarmAction, slot helpers, flash.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *          drawAlarmAction usa fixCardCorners (Dashboard) — chamada
 *          cross-file via this-> (member method).
 *
 * @project SIMUT
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"

void DisplayManager::setAlarmState(uint16_t slotMask, int8_t navSlot,
                                   bool ambTemp, bool ambHum) {
    _alarmSlotMask    = slotMask;
    _alarmAmbientTemp = ambTemp;
    _alarmAmbientHum  = ambHum;
    if (navSlot >= 0) _alarmNavPending = navSlot;
}


void DisplayManager::setAlarmSilenced(bool silenced, uint32_t endTime) {
    _alarmSilenced   = silenced;
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


void DisplayManager::drawAlarmAction() {
    if (!_canvasWide) return;

    if (!_forceSettingsRedraw) return;
    _forceSettingsRedraw = false;

    _tft->fillScreen(C_BG_MAIN);


    _tft->fillRect(4, 4, 312, 48, RGB565(180, 30, 30));
    _tft->setFont(&simutFont12pt);
    _tft->setTextColor(RGB565(255, 255, 255));

    char headerBuf[40];
    if (_alarmActionSlot < 0) {
        snprintf(headerBuf, sizeof(headerBuf), "! %s", tr(TR_AMBIENT));
    } else {
        /* Usar nome amigável do sensor (do sharedState) */
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
    int16_t bx, by; uint16_t bw, bh;
    _tft->getTextBounds(headerBuf, 0, 0, &bx, &by, &bw, &bh);
    _tft->setCursor((320 - bw) / 2, 32);
    _tft->print(headerBuf);


    int btnX = 20, btnW = 280, btnH = 45, btnR = 10;
    _tft->fillRoundRect(btnX, 60, btnW, btnH, btnR, RGB565(200, 100, 0));
    _tft->setFont(&simutFont12pt);
    _tft->setTextColor(RGB565(255, 255, 255));
    String silTxt = tr(TR_SILENCE_120S);
    _tft->getTextBounds(silTxt, 0, 0, &bx, &by, &bw, &bh);
    _tft->setCursor(btnX + (btnW - bw) / 2, 60 + 30);
    _tft->print(silTxt);


    _tft->fillRoundRect(btnX, 115, btnW, btnH, btnR, RGB565(180, 30, 30));
    String deactTxt = tr(TR_DEACTIVATE);
    _tft->getTextBounds(deactTxt, 0, 0, &bx, &by, &bw, &bh);
    _tft->setCursor(btnX + (btnW - bw) / 2, 115 + 30);
    _tft->print(deactTxt);


    _tft->fillRoundRect(btnX, 170, btnW, btnH, btnR, C_ACCENT);
    _tft->setTextColor(C_BG_MAIN);
    String mmTxt = tr(TR_MINMAX);
    _tft->getTextBounds(mmTxt, 0, 0, &bx, &by, &bw, &bh);
    _tft->setCursor(btnX + (btnW - bw) / 2, 170 + 30);
    _tft->print(mmTxt);
}

bool DisplayManager::isSlotAlarming(int slotIdx) const {
    return (slotIdx >= 0 && slotIdx < 16) && (_alarmSlotMask & (1 << slotIdx));
}

uint16_t DisplayManager::slotAlarmBg(int slotIdx) const {
    if (!isSlotAlarming(slotIdx)) return C_CARD_BG;

    if (_alarmSilenced) return C_CARD_BG;
    return _alarmFlashPhase ? RGB565(180, 30, 30) : C_CARD_BG;
}

bool DisplayManager::isAnyAlarmActive() const {
    return (_alarmSlotMask != 0) || _alarmAmbientTemp || _alarmAmbientHum;
}


void DisplayManager::redrawAlarmFlash() {
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
        else                          otherPageHasAlarm = true;
    }
    if (pageHasAlarm || otherPageHasAlarm) {
        drawBottomButtons(sel, true);
    }
}

