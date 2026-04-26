/**
 * @file    DisplayManager_Settings.cpp
 * @brief   Settings screens: themes, alarms, main menu, password, sounds, system status.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *          Inclui também sound preview helpers (consume + request) e
 *          setTelemetryPending/setTelemetrySendStatus. License (drawSettingsLicense)
 *          fica em core porque usa _licenseBuf file-static. Touch helpers
 *          (acceptTouch/Hold/Slide) ficam em core junto com handleTouch.
 *
 * @project SIMUT
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "DisplayManager_FmtFloat.h"
#include "LogManager.h"

void DisplayManager::showSettingsThemes(int currentThemeIdx) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_THEMES;
    _previewThemeIdx = currentThemeIdx;
    _themePage = currentThemeIdx / 4;
    _forceSettingsRedraw = true; _lastThemePage = -1; _lastPreviewThemeIdx = -1; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsThemes() {
    if(!_canvasWide) return;
    bool fullRedraw = _forceSettingsRedraw;
    bool pageChanged = (_themePage != _lastThemePage);
    int totalThemes = getThemeCount();
    int totalPages = (totalThemes + 3) / 4;
    if (_themePage >= totalPages) _themePage = totalPages - 1;
    if (_themePage < 0) _themePage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_CONFIG_THEMES));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw)/2, btnY + 25); _tft->print(backTxt);
        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String appTxt = tr(TR_APPLY);
        _tft->getTextBounds(appTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw)/2, btnY + 25); _tft->print(appTxt);
    }

    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
        int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
        int thumbY = trackY; if (totalPages > 1) { thumbY += (_themePage * (trackH - thumbH)) / (totalPages - 1); }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }

    int startIdx = _themePage * 4;
    int yBase = 40; int itemW = 285;
    for (int i = 0; i < 4; i++) {
        int actualIdx = startIdx + i; int y = yBase + (i * 38);
        if (!fullRedraw && !pageChanged) { if (actualIdx != _previewThemeIdx && actualIdx != _lastPreviewThemeIdx) continue; }
        _canvasWide->fillScreen(C_BG_MAIN);
        if (actualIdx < totalThemes) {
            bool isSelected = (actualIdx == _previewThemeIdx);
            uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
            _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24); _canvasWide->print(availableThemes[actualIdx].displayName);
            int pX = itemW - 55; int pY = 9;
            _canvasWide->fillRect(pX, pY, 16, 16, availableThemes[actualIdx].bgMain);
            _canvasWide->fillRect(pX + 16, pY, 16, 16, availableThemes[actualIdx].cardBg);
            _canvasWide->fillRect(pX + 32, pY, 16, 16, availableThemes[actualIdx].accent);
            if (isSelected) _canvasWide->drawRect(pX-1, pY-1, 49, 18, C_BG_MAIN); else _canvasWide->drawRect(pX-1, pY-1, 49, 18, C_TEXT_SUB);
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }
    _forceSettingsRedraw = false; _lastThemePage = _themePage; _lastPreviewThemeIdx = _previewThemeIdx;
}

void DisplayManager::showSettingsAlarms(SystemConfig* cfg) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_ALARMS; _sysConfigPtr = cfg; _activeSensorCount = 0;
    if (cfg->ambientSensor.active) { _activeSensorsMap[_activeSensorCount++] = -1; }
    for(int i = 0; i < MAX_SENSORS; i++) { if(cfg->sensors[i].active) { _activeSensorsMap[_activeSensorCount++] = i; } }
    _alarmSelection = 0; _alarmPage = 0; _lastAlarmSelection = -1; _forceSettingsRedraw = true; _lastAlarmPage = -1; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsAlarms() {
    if(!_canvasWide) return;
    bool fullRedraw = _forceSettingsRedraw; bool pageChanged = (_alarmPage != _lastAlarmPage);
    int totalPages = (_activeSensorCount + 3) / 4; if (totalPages == 0) totalPages = 1;
    if (_alarmPage >= totalPages) _alarmPage = totalPages - 1; if (_alarmPage < 0) _alarmPage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_ALARMS_TITLE));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
        /* Botão SAIR ocupa toda a largura restante */
        _tft->fillRoundRect(141, btnY, 174, btnH, 8, C_ACCENT);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_BG_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (174 - bw)/2, btnY + 25); _tft->print(backTxt);
    }

    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
        int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
        int thumbY = trackY; if (totalPages > 1) { thumbY += (_alarmPage * (trackH - thumbH)) / (totalPages - 1); }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }

    int startIdx = _alarmPage * 4; int yBase = 40; int itemW = 285;
    for (int i = 0; i < 4; i++) {
        int y = yBase + (i * 38); int mapIdx = startIdx + i;

        /* Só redesenha itens que mudaram de estado de seleção ou em fullRedraw/pageChanged */
        if (!fullRedraw && !pageChanged) {
            if (mapIdx != _alarmSelection && mapIdx != _lastAlarmSelection) continue;
        }

        _canvasWide->fillScreen(C_BG_MAIN);
        if (mapIdx < _activeSensorCount) {
            int actualSensorId = _activeSensorsMap[mapIdx];
            SensorRecord* rec = (actualSensorId == -1) ? &_sysConfigPtr->ambientSensor : &_sysConfigPtr->sensors[actualSensorId];
            bool isSelected = (mapIdx == _alarmSelection);
            uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

            /* Medir a largura do indicador SIM/NAO para reservar espaço */
            const char* statusTxt = rec->alarmsActive ? tr(TR_ON) : tr(TR_OFF);
            _canvasWide->setFont(&simutFont9pt);
            int16_t sx1, sy1; uint16_t sw, sh;
            _canvasWide->getTextBounds(statusTxt, 0, 0, &sx1, &sy1, &sw, &sh);
            int statusAreaW = (int)sw + 20;  /* margem de 10px de cada lado */

            /* Nome do sensor — truncado se necessário para não colidir */
            int maxNameW = itemW - statusAreaW - 15;
            char nameBuf[40];
            truncateText(_canvasWide, rec->friendlyName, nameBuf, sizeof(nameBuf), maxNameW);
            _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24);
            _canvasWide->print(nameBuf);

            /* Indicador SIM/NAO alinhado à direita */
            uint16_t statusColor;
            if (isSelected) {
                statusColor = C_BG_MAIN;
            } else {
                statusColor = rec->alarmsActive ? C_TEMP_OK : C_TEXT_OFF;
            }
            _canvasWide->setTextColor(statusColor);
            _canvasWide->setCursor(itemW - 10 - (int)sw, 24);
            _canvasWide->print(statusTxt);
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }
    _forceSettingsRedraw = false; _lastAlarmPage = _alarmPage; _lastAlarmSelection = _alarmSelection;
}

void DisplayManager::showAlarmEdit(int sensorIdx) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_ALARM_EDIT; _editSensorIdx = sensorIdx;
    if (sensorIdx == -1) { _tempAlarmConfig = _sysConfigPtr->ambientSensor; } else { _tempAlarmConfig = _sysConfigPtr->sensors[sensorIdx]; }
    _editFieldFocus = 0; _forceSettingsRedraw = true; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawAlarmEdit() {
    bool hasHum = (_editSensorIdx == -1 || _tempAlarmConfig.rom[0] != 0x28);


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
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        int16_t tx1, ty1; uint16_t tw, th;
        String titleTxt = String(_tempAlarmConfig.friendlyName);
        _tft->getTextBounds(titleTxt, 0, 0, &tx1, &ty1, &tw, &th);
        _tft->setCursor((320 - tw) / 2, 22); _tft->print(titleTxt);
        _tft->setTextColor(C_TEXT_SUB); _tft->setCursor(10, 52); _tft->print(tr(TR_TEMP));
        if (hasHum) { _tft->setCursor(10, 122); _tft->print(tr(TR_HUMIDITY)); }
        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 26, 26, btnY + 12, 46, btnY + 12, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 12, 94, btnY + 26, 114, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw)/2, btnY + 25); _tft->print(backTxt);
        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String saveTxt = tr(TR_SAVE);
        _tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw)/2, btnY + 25); _tft->print(saveTxt);
        _forceSettingsRedraw = false;
    }
    auto drawBox = [&](int fieldId, int x, int y, const char* label, float val, bool isHum) {
        _canvasSmall->fillScreen(C_BG_MAIN);
        bool focused = (_editFieldFocus == fieldId);
        uint16_t bg = focused ? C_ACCENT : C_CARD_BG;
        uint16_t txt = focused ? C_BG_MAIN : C_TEXT_MAIN;
        _canvasSmall->fillRoundRect(0, 0, 140, 40, 10, bg);
        if (!focused) _canvasSmall->drawRoundRect(0, 0, 140, 40, 10, C_TEXT_SUB);
        _canvasSmall->setFont(&simutFont9pt); _canvasSmall->setTextColor(focused ? C_BG_MAIN : C_TEXT_SUB);
        _canvasSmall->setCursor(8, 17); _canvasSmall->print(label);
        _canvasSmall->setFont(&simutFont12pt); _canvasSmall->setTextColor(txt);
        char intPart[8]; char decPart[4];
        if (val < 0 && val > -1.0) { snprintf(intPart, sizeof(intPart), "-0"); } else { snprintf(intPart, sizeof(intPart), "%d", (int)val); }
        int fractional = abs((int)round(val * 10.0f) % 10);
        snprintf(decPart, sizeof(decPart), ".%d", fractional);
        int textAnchor = 98; int16_t bx, by; uint16_t bw, bh;
        _canvasSmall->getTextBounds(intPart, 0, 0, &bx, &by, &bw, &bh);
        _canvasSmall->setCursor(textAnchor - bw, 32); _canvasSmall->print(intPart);
        _canvasSmall->setCursor(textAnchor, 32); _canvasSmall->print(decPart);
        _canvasSmall->setFont(NULL); _canvasSmall->setCursor(122, 20);
        if (isHum) _canvasSmall->print("%"); else _canvasSmall->print("C");
        blitCanvas(_canvasSmall, x, y, 140, 40);
    };
    drawBox(0, 10,  60, "MIN", _tempAlarmConfig.tempMin, false); drawBox(1, 160, 60, "MAX", _tempAlarmConfig.tempMax, false);
    if (hasHum) { drawBox(2, 10,  130, "MIN", _tempAlarmConfig.humMin, true); drawBox(3, 160, 130, "MAX", _tempAlarmConfig.humMax, true); }
}

void DisplayManager::showSettingsMain() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_MAIN; _menuSelection = 0; _mainMenuPage = 0; _lastMainMenuPage = -1;
    _forceSettingsRedraw = true; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsMain() {
    if(!_canvasWide) return;
    bool fullRedraw = _forceSettingsRedraw; bool pageChanged = (_mainMenuPage != _lastMainMenuPage);
    const int TOTAL_ITEMS = 9; LangKey menuItems[] = {TR_MENU_THEMES, TR_MENU_ALARMS, TR_MENU_SOUNDS, TR_MENU_LANG, TR_MENU_PASSWORD, TR_MENU_TOUCH_CAL, TR_MENU_LICENSE, TR_MENU_STATUS, TR_MENU_DISPLAY_OFFSET};
    int totalPages = (TOTAL_ITEMS + 3) / 4; if (totalPages == 0) totalPages = 1;
    if (_mainMenuPage >= totalPages) _mainMenuPage = totalPages - 1; if (_mainMenuPage < 0) _mainMenuPage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_CONFIG_MAIN));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;
        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);
        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);
        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw)/2, btnY + 25); _tft->print(backTxt);
        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String enterTxt = tr(TR_ENTER);
        _tft->getTextBounds(enterTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw)/2, btnY + 25); _tft->print(enterTxt);
    }

    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
        int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
        int thumbY = trackY; if (totalPages > 1) { thumbY += (_mainMenuPage * (trackH - thumbH)) / (totalPages - 1); }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }

    int startIdx = _mainMenuPage * 4; int yBase = 40; int itemW = 285;
    for (int i = 0; i < 4; i++) {
        int y = yBase + (i * 38); int mapIdx = startIdx + i;
        _canvasWide->fillScreen(C_BG_MAIN);
        _canvasWide->setTextSize(1); /* Garante reset após tela de status */
        if (mapIdx < TOTAL_ITEMS) {
            bool isSelected = (mapIdx == _menuSelection);
            uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
            _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24); _canvasWide->print(tr(menuItems[mapIdx]));
            _canvasWide->fillTriangle(itemW - 20, 11, itemW - 20, 23, itemW - 10, 17, isSelected ? C_BG_MAIN : C_TEXT_SUB);
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }
    _forceSettingsRedraw = false; _lastMainMenuPage = _mainMenuPage;
}




void DisplayManager::showSettingsPassword() {
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


void DisplayManager::drawPasswordMessage() {
    if (!_tft) return;
    int16_t x1, y1; uint16_t w, h_bound;

    _tft->fillScreen(C_BG_MAIN);


    bool isSuccess = (_kbPhase == 3);
    uint16_t iconColor = isSuccess ? C_TEMP_OK : C_TEMP_WARM;

    if (isSuccess) {

        _tft->drawLine(130, 90, 150, 110, iconColor);
        _tft->drawLine(131, 90, 151, 110, iconColor);
        _tft->drawLine(150, 110, 190, 70, iconColor);
        _tft->drawLine(151, 110, 191, 70, iconColor);
    } else {

        _tft->drawLine(145, 70, 175, 100, iconColor);
        _tft->drawLine(146, 70, 176, 100, iconColor);
        _tft->drawLine(175, 70, 145, 100, iconColor);
        _tft->drawLine(176, 70, 146, 100, iconColor);
    }


    const char* msg = (_kbMsgKey < TR_KEYS_COUNT) ? tr(_kbMsgKey) : "Error";
    _tft->setFont(&simutFont9pt);
    _tft->setTextColor(C_TEXT_MAIN);
    _tft->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor((320 - w) / 2, 130);
    _tft->print(msg);


    _tft->fillRoundRect(60, 185, 200, 40, 12, C_ACCENT);
    _tft->setFont(&simutFont12pt);
    _tft->setTextColor(C_BG_MAIN);
    const char* btnLabel = tr(TR_UNDERSTOOD);
    _tft->getTextBounds(btnLabel, 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor(160 - (w / 2), 212);
    _tft->print(btnLabel);
}

void DisplayManager::drawSettingsPassword() {
    if (!_tft) return;


    if (_kbPhase >= 2) {
        drawPasswordMessage();
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
                                  :                   layer0;


    char* activeBuf = (_kbPhase == 0) ? _kbBuffer : _kbConfirmBuf;

    int16_t x1, y1; uint16_t w, h_bound;
    bool fullRedraw = _forceSettingsRedraw;


    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
    }

    /* Título — redesenha sempre via canvas (muda entre fases) */
    {
        /* Barra de ponta a ponta sem cantos arredondados */
        _canvasWide->fillScreen(C_CARD_BG);
        _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(C_TEXT_MAIN);
        _canvasWide->setCursor(14, 18);
        _canvasWide->print((_kbPhase == 0) ? tr(TR_NEW_PASSWORD) : tr(TR_CONFIRM_PASSWORD));

        /* Botão X sobreposto à barra — y=4 mantém 4 px de margem no topo,
         * resistindo ao offset de display -4V sem clip das linhas superiores. */
        _canvasWide->fillRoundRect(282, 4, 30, 22, 4, C_TEMP_WARM);
        _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(C_BG_MAIN);
        _canvasWide->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
        _canvasWide->setCursor(297 - w / 2, 20); _canvasWide->print("X");

        /* Blit com dstY=4 empurra o header 4 px para baixo na tela — evita
         * clip do topo a offset -4V. h sobe para 30 para acompanhar a
         * extensão vertical do conteúdo (título + botão X até y=26). */
        blitCanvas(_canvasWide, 0, 4, 320, 30);
    }


    {
        const int MAX_BOXES = 7;
        const int MIN_BOXES = 4;
        const int boxW = 32, boxH = 28, gap = 6;
        const int startY = 33;
        const int stripH = boxH + 10;

        /*
         * Número de boxes visíveis: na fase 0 (digitação), mostra o máximo
         * entre MIN_BOXES e (cursor + 1), até MAX_BOXES.
         * Na fase 1 (confirmação), mostra exatamente o tamanho da senha
         * já definida em _kbBuffer.
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

        /* Desenha boxes + contador em canvas para evitar flicker */
        _canvasWide->fillScreen(C_BG_MAIN);

        for (int i = 0; i < visibleBoxes; i++) {
            int bx = startX + i * (boxW + gap);
            bool filled = (i < _kbCursor);
            bool isRequired = (i < MIN_BOXES);

            /* Box arredondado */
            _canvasWide->fillRoundRect(bx, 0, boxW, boxH, 4, C_CARD_BG);

            /* Borda com cor condicional */
            uint16_t borderColor = isRequired ? C_ACCENT_HIGH : C_TEXT_OFF;
            if (i == _kbCursor && _kbCursor < visibleBoxes) borderColor = C_ACCENT;
            _canvasWide->drawRoundRect(bx, 0, boxW, boxH, 4, borderColor);

            if (filled) {
                if (_kbShowRaw) {
                    /* Mostra caractere real */
                    _canvasWide->setFont(&simutFont9pt);
                    _canvasWide->setTextColor(C_TEXT_MAIN);
                    char ch[2] = { activeBuf[i], '\0' };
                    int16_t cx1, cy1; uint16_t cw, ch1;
                    _canvasWide->getTextBounds(ch, 0, 0, &cx1, &cy1, &cw, &ch1);
                    _canvasWide->setCursor(bx + (boxW - cw) / 2, 20);
                    _canvasWide->print(ch);
                } else {
                    /* Mostra bolinha mascarada */
                    _canvasWide->fillCircle(bx + boxW / 2, boxH / 2, 5, C_TEXT_MAIN);
                }
            }
        }

        /* Contador abaixo dos boxes */
        char countBuf[8];
        snprintf(countBuf, sizeof(countBuf), "%d / %d", _kbCursor, visibleBoxes);
        uint16_t countColor = (_kbCursor < MIN_BOXES) ? C_TEXT_OFF : C_ACCENT;
        _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
        _canvasWide->setTextColor(countColor);
        int16_t cx1, cy1; uint16_t cw, ch1;
        _canvasWide->getTextBounds(countBuf, 0, 0, &cx1, &cy1, &cw, &ch1);
        _canvasWide->setCursor((320 - cw) / 2, boxH + 3);
        _canvasWide->print(countBuf);

        /* Blit de uma vez — sem flicker */
        blitCanvas(_canvasWide, 0, startY, 320, stripH);
    }


    {
        const int keyW = 30, keyH = 30, gap = 2;
        const int startX = 1, startY = 72;

        /* Desenha uma fila de teclas por vez via canvas para evitar flicker */
        for (int row = 0; row < 3; row++) {
            int ky = startY + row * (keyH + gap);

            /* Preenche o canvas com o fundo da tela */
            _canvasWide->fillScreen(C_BG_MAIN);

            for (int col = 0; col < 10; col++) {
                int kx = startX + col * (keyW + gap);
                char ch = activeLayer[row][col];
                bool selected = (row == _kbSelRow && col == _kbSelCol);

                /* Tecla com bordas arredondadas — destaque se selecionada */
                uint16_t keyBg  = selected ? C_ACCENT    : C_CARD_BG;
                uint16_t keyFg  = selected ? C_BG_MAIN   : C_TEXT_MAIN;
                uint16_t hintFg = selected ? C_BG_MAIN   : C_TEXT_OFF;

                _canvasWide->fillRoundRect(kx, 0, keyW, keyH, 4, keyBg);
                if (!selected) _canvasWide->drawRoundRect(kx, 0, keyW, keyH, 4, C_TEXT_SUB);

                /* Caractere principal centralizado */
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(keyFg);
                char label[2] = {ch, '\0'};
                int16_t lx1, ly1; uint16_t lw, lh;
                _canvasWide->getTextBounds(label, 0, 0, &lx1, &ly1, &lw, &lh);
                _canvasWide->setCursor(kx + (keyW - lw) / 2 - lx1, (keyH - lh) / 2 - ly1);
                _canvasWide->print(label);

                /* Hint do layer alternativo — canto superior direito, dentro da tecla */
                char hint = '\0';
                if (_kbLayer == 0)      hint = layer2[row][col];
                else if (_kbLayer == 1) hint = layer2[row][col];
                else                    hint = layer0[row][col];
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(hintFg);
                char hintStr[2] = {hint, '\0'};
                int16_t hx1, hy1; uint16_t hw, hh;
                _canvasWide->getTextBounds(hintStr, 0, 0, &hx1, &hy1, &hw, &hh);
                _canvasWide->setCursor(kx + keyW - (int)hw - 4, 3);
                _canvasWide->print(hintStr);
            }

            /* Blit da fila inteira de uma vez — sem flicker */
            blitCanvas(_canvasWide, 0, ky, 320, keyH);
        }
    }


    {
        /*
         * Barra de ações: Shift, 123, Espaço, Backspace, OK.
         * Mesma largura total das filas de teclas (x=1..319).
         * Shift=48, 123=48, Espaço=118, Backspace=48, OK=48, gap=2.
         */
        const int barY = 170, barH = 22;
        const int bx0 = 1;       /* Shift */
        const int bx1 = 51;      /* 123 */
        const int bx2 = 101;     /* Espaço */
        const int bx3 = 221;     /* Backspace */
        const int bx4 = 271;     /* OK */
        const int bw01 = 48;     /* Shift e 123 */
        const int bw2 = 118;     /* Espaço */
        const int bw34 = 48;     /* Backspace e OK */
        bool barActive = (_kbSelRow == 3);

        _canvasWide->fillScreen(C_BG_MAIN);

        /* Botão Shift (col 0) */
        {
            bool layerActive = (_kbLayer == 1) || _kbShiftLock;
            bool sel = barActive && (_kbSelCol == 0);
            uint16_t bg = sel ? C_ACCENT_HIGH : (layerActive ? C_ACCENT : C_CARD_BG);
            uint16_t fg = (sel || layerActive) ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(bx0, 0, bw01, barH, 4, bg);
            if (!sel && !layerActive) _canvasWide->drawRoundRect(bx0, 0, bw01, barH, 4, C_TEXT_SUB);
            if (sel) _canvasWide->drawRoundRect(bx0, 0, bw01, barH, 4, C_ACCENT);
            int cx = bx0 + bw01 / 2, cy = 5;
            _canvasWide->fillTriangle(cx - 5, cy + 5, cx, cy, cx + 5, cy + 5, fg);
            _canvasWide->fillRect(cx - 2, cy + 5, 4, 6, fg);
            if (_kbShiftLock) {
                _canvasWide->drawFastHLine(bx0 + 10, barH - 3, 28, fg);
            }
        }

        /* Botão 123 (col 1) */
        {
            bool layerActive = (_kbLayer == 2);
            bool sel = barActive && (_kbSelCol == 1);
            uint16_t bg = sel ? C_ACCENT_HIGH : (layerActive ? C_ACCENT : C_CARD_BG);
            uint16_t fg = (sel || layerActive) ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(bx1, 0, bw01, barH, 4, bg);
            if (!sel && !layerActive) _canvasWide->drawRoundRect(bx1, 0, bw01, barH, 4, C_TEXT_SUB);
            if (sel) _canvasWide->drawRoundRect(bx1, 0, bw01, barH, 4, C_ACCENT);
            _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(fg);
            int16_t tx1, ty1; uint16_t tw, th;
            _canvasWide->getTextBounds("123", 0, 0, &tx1, &ty1, &tw, &th);
            _canvasWide->setCursor(bx1 + (bw01 - (int)tw) / 2, (barH - (int)th) / 2);
            _canvasWide->print("123");
        }

        /* Barra de espaço (col 2) */
        {
            bool sel = barActive && (_kbSelCol == 2);
            uint16_t bg = sel ? C_ACCENT_HIGH : C_CARD_BG;
            _canvasWide->fillRoundRect(bx2, 0, bw2, barH, 4, bg);
            if (sel) _canvasWide->drawRoundRect(bx2, 0, bw2, barH, 4, C_ACCENT);
            else     _canvasWide->drawRoundRect(bx2, 0, bw2, barH, 4, C_TEXT_SUB);
            uint16_t lineCol = sel ? C_BG_MAIN : C_TEXT_OFF;
            int lineX = bx2 + 20;
            int lineW = bw2 - 40;
            _canvasWide->drawFastHLine(lineX, 14, lineW, lineCol);
        }

        /* Botão Backspace (col 3) */
        {
            bool sel = barActive && (_kbSelCol == 3);
            uint16_t bg = sel ? C_ACCENT_HIGH : C_CARD_BG;
            uint16_t fg = sel ? C_BG_MAIN : C_TEXT_MAIN;
            _canvasWide->fillRoundRect(bx3, 0, bw34, barH, 4, bg);
            if (sel) _canvasWide->drawRoundRect(bx3, 0, bw34, barH, 4, C_ACCENT);
            else     _canvasWide->drawRoundRect(bx3, 0, bw34, barH, 4, C_TEXT_SUB);
            int cx = bx3 + bw34 / 2, cy = barH / 2;
            _canvasWide->fillTriangle(cx - 8, cy, cx - 2, cy - 5, cx - 2, cy + 5, fg);
            _canvasWide->fillRect(cx - 2, cy - 3, 10, 6, fg);
        }

        /* Botão OK (col 4) */
        {
            bool sel = barActive && (_kbSelCol == 4);
            uint16_t bg = sel ? C_ACCENT_HIGH : C_ACCENT;
            uint16_t fg = C_BG_MAIN;
            _canvasWide->fillRoundRect(bx4, 0, bw34, barH, 4, bg);
            if (sel) _canvasWide->drawRoundRect(bx4, 0, bw34, barH, 4, C_BG_MAIN);
            _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(fg);
            int16_t tx1, ty1; uint16_t tw, th;
            _canvasWide->getTextBounds("OK", 0, 0, &tx1, &ty1, &tw, &th);
            _canvasWide->setCursor(bx4 + (bw34 - (int)tw) / 2, (barH - (int)th) / 2);
            _canvasWide->print("OK");
        }

        blitCanvas(_canvasWide, 0, barY, 320, barH);
    }


    {
        /*
         * 5 botões de navegação no estilo dashboard (58x40, raio 12).
         * ▲  ▼  ◄  ►  ✓(confirma caractere)
         * Posicionados na parte inferior da tela (Y=195).
         */
        const int btnW = 58, btnH = 40, gap = 5, startX = 5;
        const int navY = 195;

        _canvasWide->fillScreen(C_BG_MAIN);

        /* Botão ◄ (esquerda) */
        {
            _canvasWide->fillRoundRect(startX, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = startX + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx + 6, cy - 8, cx + 6, cy + 8, cx - 8, cy, C_TEXT_MAIN);
        }

        /* Botão ► (direita) */
        {
            int bx = startX + (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 6, cy - 8, cx - 6, cy + 8, cx + 8, cy, C_TEXT_MAIN);
        }

        /* Botão ▲ (cima) */
        {
            int bx = startX + 2 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 8, cy + 6, cx + 8, cy + 6, cx, cy - 8, C_TEXT_MAIN);
        }

        /* Botão ▼ (baixo) */
        {
            int bx = startX + 3 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_CARD_BG);
            int cx = bx + btnW / 2, cy = btnH / 2;
            _canvasWide->fillTriangle(cx - 8, cy - 6, cx + 8, cy - 6, cx, cy + 8, C_TEXT_MAIN);
        }

        /* Botão ✓ (confirma caractere selecionado) */
        {
            int bx = startX + 4 * (btnW + gap);
            _canvasWide->fillRoundRect(bx, 0, btnW, btnH, 12, C_ACCENT);
            int cx = bx + btnW / 2, cy = btnH / 2;
            /* Ícone de check */
            _canvasWide->drawLine(cx - 8, cy, cx - 3, cy + 6, C_BG_MAIN);
            _canvasWide->drawLine(cx - 7, cy, cx - 2, cy + 6, C_BG_MAIN);
            _canvasWide->drawLine(cx - 3, cy + 6, cx + 8, cy - 6, C_BG_MAIN);
            _canvasWide->drawLine(cx - 2, cy + 6, cx + 9, cy - 6, C_BG_MAIN);
        }

        blitCanvas(_canvasWide, 0, navY, 320, btnH);
    }

    _forceSettingsRedraw = false;
}




void DisplayManager::setTelemetryPending(uint16_t count) {
    _sharedState.pendingPkts = count;
}


/**
 * @brief Informa o resultado do último envio de telemetria.
 *
 * Sucesso: inicia animação de flash (azul/branco por 1s), depois
 *          estabiliza em azul fixo.
 * Falha:   vermelho fixo imediatamente.
 */
void DisplayManager::setTelemetrySendStatus(bool success) {
    if (success) {
        /* BUG-002: publica vars auxiliares ANTES do state (a flag que
         * dispara o ramo de flash no render em Core 1). */
        _pktArrowFlashOn   = false;
        _pktArrowFlashTime = millis();
        _pktArrowFlashEnd  = millis() + 1000;
        __dmb();
        _pktArrowState = 3;  /* flash de envio */
    } else {
        _pktArrowState = 2;  /* vermelho fixo */
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


void DisplayManager::drawSettingsSounds() {
    if (!_canvasWide) return;

    static int lastSoundPage = -1;
    int soundPage = _soundSelection / 4;
    bool fullRedraw = _forceSettingsRedraw;
    bool pageChanged = (soundPage != lastSoundPage);

    const int TOTAL_ITEMS = 8;
    int totalPages = (TOTAL_ITEMS + 3) / 4;


    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_SOUNDS_TITLE));

        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);

        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);

        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw) / 2, btnY + 25); _tft->print(backTxt);

        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String saveTxt = tr(TR_SAVE);
        _tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw) / 2, btnY + 25); _tft->print(saveTxt);
    }


    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40; int trackW = 8; int trackH = 146;
        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);
        int thumbH = trackH / totalPages; if (thumbH < 20) thumbH = 20;
        int thumbY = trackY;
        if (totalPages > 1) { thumbY += (soundPage * (trackH - thumbH)) / (totalPages - 1); }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }


    LangKey itemLabels[TOTAL_ITEMS] = {
        TR_SND_TOUCH, TR_SND_CONFIRM, TR_SND_ERROR, TR_SND_ALARM,
        TR_SND_WEB,   TR_SND_MUTE,    TR_SND_VOLUME, TR_SND_ALARM_VOL
    };

    int startIdx = soundPage * 4;
    int yBase = 40; int itemW = 285;

    for (int i = 0; i < 4; i++) {
        int actualIdx = startIdx + i;
        int y = yBase + (i * 38);

        _canvasWide->fillScreen(C_BG_MAIN);

        if (actualIdx < TOTAL_ITEMS) {
            bool isSelected = (actualIdx == _soundSelection);
            uint16_t bg  = isSelected ? C_ACCENT  : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;

            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

            _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24); _canvasWide->print(tr(itemLabels[actualIdx]));

            if (actualIdx == 6 || actualIdx == 7) {

                uint8_t volVal = (actualIdx == 6)
                               ? _soundSettings.volume
                               : _soundSettings.alarmVolume;
                char buf[8];
                snprintf(buf, sizeof(buf), "%d%%", volVal);
                int16_t bx, by; uint16_t bw, bh;
                _canvasWide->getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
                _canvasWide->setCursor(itemW - 15 - bw, 24);
                _canvasWide->print(buf);

                int barW = 100;
                int barX = itemW - 15 - bw - 10 - barW;
                int barY = 11; int barH = 12;
                int fillW = (int)((uint32_t)barW * volVal / 100);

                uint16_t barBg   = isSelected ? C_ACCENT_HIGH : C_BAR_BG;
                uint16_t barFill = isSelected ? C_BG_MAIN     : C_ACCENT;
                _canvasWide->fillRoundRect(barX, barY, barW, barH, 3, barBg);
                if (fillW > 0) {
                    _canvasWide->fillRoundRect(barX, barY, fillW, barH, 3, barFill);
                }
            } else {

                bool val = false;
                switch (actualIdx) {
                    case 0: val = _soundSettings.touchEnabled;   break;
                    case 1: val = _soundSettings.confirmEnabled; break;
                    case 2: val = _soundSettings.errorEnabled;   break;
                    case 3: val = _soundSettings.alarmEnabled;   break;
                    case 4: val = _soundSettings.webEnabled;     break;
                    case 5: val = _soundSettings.muted;          break;
                }
                const char* valStr = val ? tr(TR_ON) : tr(TR_OFF);
                int16_t bx, by; uint16_t bw, bh;
                _canvasWide->getTextBounds(valStr, 0, 0, &bx, &by, &bw, &bh);
                _canvasWide->setCursor(itemW - 15 - bw, 24);
                _canvasWide->print(valStr);
            }
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }

    _forceSettingsRedraw = false;
    lastSoundPage = soundPage;
}


void DisplayManager::drawMelodySelect() {
    if (!_canvasWide) return;


    static const char* MEL_NAMES[4][6] = {
        {"1. Click",      "2. Bubble",    "3. Tick",
         "4. Snap",       "5. Drop",      "6. Chirp"},
        {"1. Ascending",  "2. Fanfare",   "3. Chime",
         "4. Triumph",    "5. Sparkle",   "6. Resolve"},
        {"1. Descending", "2. Buzz",      "3. Low",
         "4. Harsh",      "5. Decline",   "6. Blip"},
        {"1. Dual Beep",  "2. Siren",     "3. Rapid",
         "4. Pulse",      "5. Escalate",  "6. Staccato"}
    };
    static const LangKey TYPE_LABELS[4] = {
        TR_SND_TOUCH, TR_SND_CONFIRM, TR_SND_ERROR, TR_SND_ALARM
    };

    uint8_t typeIdx = _melSelectType;
    if (typeIdx > 3) typeIdx = 0;

    const int TOTAL_VARIANTS = 6;
    bool fullRedraw = _forceSettingsRedraw;


    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);


        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_ACCENT);
        _tft->setCursor(10, 22);
        _tft->print(tr(TYPE_LABELS[typeIdx]));


        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);

        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);

        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw) / 2, btnY + 25); _tft->print(backTxt);

        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String saveTxt = tr(TR_SAVE);
        _tft->getTextBounds(saveTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw) / 2, btnY + 25); _tft->print(saveTxt);
    }


    int melPage  = _melSelectIdx / 4;
    int startIdx = melPage * 4;
    int yBase    = 40;
    int itemW    = 285;

    for (int i = 0; i < 4; i++) {
        int actualIdx = startIdx + i;
        int y = yBase + (i * 38);

        _canvasWide->fillScreen(C_BG_MAIN);

        if (actualIdx < TOTAL_VARIANTS) {
            bool isSelected = (actualIdx == _melSelectIdx);
            uint16_t bg  = isSelected ? C_ACCENT  : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;

            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);

            _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(14, 24);
            _canvasWide->print(MEL_NAMES[typeIdx][actualIdx]);
        }
        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }

    _forceSettingsRedraw = false;
}


bool DisplayManager::consumeTouchSound() {
    if (_touchSoundPending) {
        _touchSoundPending = false;
        return true;
    }
    return false;
}


bool DisplayManager::consumeErrorSound() {
    if (_errorSoundPending) {
        _errorSoundPending = false;
        return true;
    }
    return false;
}


/* BUG-002: producers cross-core publicam dado ANTES da flag com __dmb().
 * Sem a barreira, Core 0 pode ver a flag true antes dos campos de dado
 * estarem visíveis (reordering visível no RP2040 entre cores). */
void DisplayManager::requestPreviewSound(SoundEvent ev, uint8_t melIdx) {
    _previewType   = (uint8_t)ev;
    _previewMelIdx = melIdx;
    __dmb();
    _previewPending = true;
}

void DisplayManager::requestVolumePreview(uint8_t level) {
    _volumePreviewLevel = level;
    __dmb();
    _volumePreviewPending = true;
}

void DisplayManager::requestAlarmVolumePreview(uint8_t level) {
    _alarmVolPreviewLevel = level;
    __dmb();
    _alarmVolPreviewPending = true;
}


bool DisplayManager::consumePreviewSound(SoundEvent& outEvent, uint8_t& outIdx) {
    if (!_previewPending) return false;
    __dmb();                       /* BUG-002: lê dados APÓS a flag */
    outEvent = (SoundEvent)_previewType;
    outIdx   = _previewMelIdx;
    _previewPending = false;
    return true;
}


bool DisplayManager::consumeVolumePreview(uint8_t& outLevel) {
    if (!_volumePreviewPending) return false;
    __dmb();                       /* BUG-002 */
    outLevel = _volumePreviewLevel;
    _volumePreviewPending = false;
    return true;
}


/**
 * @brief Aceita toque único — exige que o dedo tenha sido levantado
 *        desde o último toque aceito. Impede repetição por segurar.
 */

bool DisplayManager::consumeAlarmVolumePreview(uint8_t& outLevel) {
    if (!_alarmVolPreviewPending) return false;
    __dmb();                       /* BUG-002 */
    outLevel = _alarmVolPreviewLevel;
    _alarmVolPreviewPending = false;
    return true;
}

void DisplayManager::showSystemStatus() {
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
 * @brief Desenha a tela de status do sistema com flicker zero.
 *
 * Usa canvas (strip rendering) para toda a área de conteúdo.
 * 4 páginas: Sistema, Rede, Sensores, Telemetria.
 * Auto-refresh a cada 1 segundo via timer no render loop.
 */
void DisplayManager::drawSystemStatus() {
    bool fullRedraw = _forceSettingsRedraw;
    _forceSettingsRedraw = false;

    GFXcanvas16* cv = _canvasWide;
    if (!cv) return;

    const SystemStatusData& d = _statusData;

    /* ── Header + Botões (somente no fullRedraw) ── */
    if (fullRedraw) {
        cv->fillScreen(C_CARD_BG);
        cv->setFont(&simutFont9pt);
        cv->setTextColor(C_TEXT_MAIN);
        cv->setCursor(10, 20); cv->print(tr(TR_STATUS_TITLE));

        /* Dots de página */
        cv->setFont(NULL); cv->setTextSize(1);
        for (int p = 0; p < STATUS_PAGES; p++) {
            int dx = 280 + p * 10;
            if (p == _statusPage) cv->fillCircle(dx, 14, 3, C_ACCENT);
            else                  cv->drawCircle(dx, 14, 2, C_TEXT_OFF);
        }
        blitCanvas(cv, 0, 0, 320, 28);

        /* Botões ← → BACK */
        _tft->fillRoundRect(5, 195, 62, 40, 8, C_CARD_BG);
        _tft->fillTriangle(36, 207, 26, 221, 46, 221, C_TEXT_MAIN);
        _tft->fillRoundRect(73, 195, 62, 40, 8, C_CARD_BG);
        _tft->fillTriangle(104, 221, 94, 207, 114, 207, C_TEXT_MAIN);
        _tft->fillRoundRect(141, 195, 75, 40, 8, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        int16_t bx, by; uint16_t bw, bh;
        const char* bt = tr(TR_BACK);
        _tft->getTextBounds(bt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw) / 2, 220); _tft->print(bt);
    }

    /*
     * Tabela simples: font NULL size 2 (12×16px).
     * Cada linha: 20px (16px texto + 4px gap).
     * Área útil: y=28..194 = 166px → 8 linhas por página.
     * Label à esquerda, valor à direita, separados por linha pontilhada.
     */

    static char buf[64];
    static char fbuf[12];

    /* Monta array de linhas para a página atual */
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
        if (d.ambientValid) {
            fmtFloat1(fbuf, sizeof(fbuf), d.ambientTemp);
            snprintf(buf, sizeof(buf), "%s oC", fbuf);
            addRow("Ambient T", buf);
            snprintf(buf, sizeof(buf), "%d%%", (int)d.ambientHum);
            addRow("Ambient H", buf);
        } else {
            addRow("Ambient T", "--", C_TEXT_OFF);
            addRow("Ambient H", "--", C_TEXT_OFF);
        }
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

    /* ── Renderiza tabela em strips de 42px ── */
    const int rowH = 20;
    const int valX  = 150; /* Coluna dos valores */

    for (int strip = 0; strip < 4; strip++) {
        int sTop = 28 + strip * 42;
        int sH = 42;
        if (sTop + sH > 195) sH = 195 - sTop;
        if (sH <= 0) break;

        cv->fillScreen(C_BG_MAIN);

        for (int r = 0; r < 2; r++) {
            int ri = strip * 2 + r; /* Índice absoluto da linha */
            if (ri >= nRows) break;

            int ly = r * rowH + 2;

            /* Label */
            cv->setFont(NULL); cv->setTextSize(2);
            cv->setTextColor(C_TEXT_SUB);
            cv->setCursor(4, ly);
            cv->print(rows[ri].lbl);

            /* Valor */
            cv->setTextColor(rows[ri].color);
            cv->setCursor(valX, ly);
            cv->print(rows[ri].val);

            /* Separador pontilhado */
            int sepY = ly + 17;
            if (sepY < sH) {
                for (int dx = 4; dx < 316; dx += 4)
                    cv->drawPixel(dx, sepY, C_GRID);
            }
        }

        blitCanvas(cv, 0, sTop, 320, sH);
    }

    _statusLastDraw = millis();

    /* Restaura textSize para não contaminar outras telas */
    cv->setTextSize(1);
    cv->setFont(NULL);
}

