/**
 * @file    DisplayManager_Auth.cpp
 * @brief   Authentication keypad: PIN entry, scrambled layout, lockout.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *          readPixel/readRow são helpers de blur effect; fastRandom é PRNG
 *          local para scramble; scrambleKeys reorganiza layout do teclado.
 *
 * @project SIMUT
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"

uint16_t DisplayManager::readPixel(int16_t x, int16_t y) {
    if (!_tft) return 0;
    _tft->startWrite(); _tft->setAddrWindow(x, y, 1, 1); _tft->endWrite();
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, LOW); SPI.transfer(0x2E);
    digitalWrite(TFT_DC, HIGH); SPI.transfer(0x00);
    uint8_t r = SPI.transfer(0x00); uint8_t g = SPI.transfer(0x00); uint8_t b = SPI.transfer(0x00);
    digitalWrite(TFT_CS, HIGH); SPI.endTransaction();
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}


void DisplayManager::readRow(int16_t y, uint16_t* buffer, int16_t w) {
    if (!_tft || !buffer) return;


    _tft->startWrite();
    _tft->setAddrWindow(0, y, w, 1);
    _tft->endWrite();


    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TFT_CS, LOW);
    digitalWrite(TFT_DC, LOW);
    SPI.transfer(0x2E);
    digitalWrite(TFT_DC, HIGH);
    SPI.transfer(0x00);

    for (int16_t x = 0; x < w; x++) {
        uint8_t r = SPI.transfer(0x00);
        uint8_t g = SPI.transfer(0x00);
        uint8_t b = SPI.transfer(0x00);
        buffer[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    digitalWrite(TFT_CS, HIGH);
    SPI.endTransaction();
}

uint32_t DisplayManager::fastRandom(uint32_t maxVal) {
    _rngState ^= _rngState << 13; _rngState ^= _rngState >> 17; _rngState ^= _rngState << 5;
    return _rngState % maxVal;
}

void DisplayManager::scrambleKeys() {
    const char poolNum[] = "0123456789";
    const char poolUpper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char poolLower[] = "abcdefghijklmnopqrstuvwxyz";
    const char poolSpec[] = "!@#$%^&*()_+-=[]{}|;':\",./<>?\\~";
    char expected = '\0'; if (_authStep < _expectedPin.length()) { expected = _expectedPin[_authStep]; }
    int expectedType = -1;
    if (expected >= '0' && expected <= '9') expectedType = 0;
    else if (expected >= 'A' && expected <= 'Z') expectedType = 1;
    else if (expected >= 'a' && expected <= 'z') expectedType = 2;
    else if (expected != '\0') expectedType = 3;
    int correctBtn = -1; if (expected != '\0') correctBtn = fastRandom(4);
    for (int i = 0; i < 4; i++) {
        char chars[4];
        if (i == correctBtn && expectedType == 0) chars[0] = expected; else do { chars[0] = poolNum[fastRandom(sizeof(poolNum)-1)]; } while(chars[0] == expected);
        if (i == correctBtn && expectedType == 1) chars[1] = expected; else do { chars[1] = poolUpper[fastRandom(sizeof(poolUpper)-1)]; } while(chars[1] == expected);
        if (i == correctBtn && expectedType == 2) chars[2] = expected; else do { chars[2] = poolLower[fastRandom(sizeof(poolLower)-1)]; } while(chars[2] == expected);
        if (i == correctBtn && expectedType == 3) chars[3] = expected; else do { chars[3] = poolSpec[fastRandom(sizeof(poolSpec)-1)]; } while(chars[3] == expected);
        for (int k = 3; k > 0; k--) { int j = fastRandom(k + 1); char temp = chars[k]; chars[k] = chars[j]; chars[j] = temp; }
        _keypadChars[i][0] = chars[0]; _keypadChars[i][1] = chars[1]; _keypadChars[i][2] = chars[2]; _keypadChars[i][3] = chars[3]; _keypadChars[i][4] = '\0';
    }
}

void DisplayManager::showAuthScreen(String expectedPin) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_AUTH; _forceSettingsRedraw = true; _repaintSettings = true;
    if (_permanentLockout) { _lockoutUntil = millis() + 10000; } else {
        _expectedPin = expectedPin; _authStep = 0; _authFailed = false; _isCurrentAttemptValid = true;
        _rngState = micros() ^ 0xA5A5A5A5; if (_rngState == 0) _rngState = 1;
        scrambleKeys();
    }
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawAuthScreen() {
    int16_t bx, by; uint16_t bw, bh;
    String titleTxt = tr(TR_AUTH_TITLE); String cancelTxt = tr(TR_CANCEL);

    if (_permanentLockout) {
        if (_forceSettingsRedraw) {
            _tft->fillScreen(C_TEMP_HOT); _tft->fillRect(4, 4, 312, 32, C_CARD_BG); _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
            _tft->setFont(&simutFont12pt); _tft->setTextColor(C_BG_MAIN); String msg1 = tr(TR_ACCESS_BLOCKED);
            _tft->getTextBounds(msg1, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 110); _tft->print(msg1);
            _tft->setFont(&simutFont9pt); String msg2 = tr(TR_REBOOT_REQ);
            _tft->getTextBounds(msg2, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 140); _tft->print(msg2);
            _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
            /* Botão de licença */
            _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
            _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_SUB);
            String licTxt = tr(TR_LICENSE_TITLE);
            _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
            _forceSettingsRedraw = false;
        }
        return;
    }

    if (_lockoutUntil > 0 && !timeReached(_lockoutUntil)) {
        static long lastSec = -1;
        if (_forceSettingsRedraw) {
            _tft->fillScreen(C_BG_MAIN); _tft->fillRect(4, 4, 312, 32, C_CARD_BG); _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
            _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
            _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG);
            _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
            /* Botão de licença */
            _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
            _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_SUB);
            String licTxt = tr(TR_LICENSE_TITLE);
            _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
            _forceSettingsRedraw = false; lastSec = -1;
        }
        long secondsLeft = (long)(timeRemaining(_lockoutUntil) / 1000) + 1;
        if (secondsLeft != lastSec) {
            lastSec = secondsLeft;
            _canvasWide->fillScreen(C_BG_MAIN); _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextColor(C_TEMP_WARM);
            String txt1 = tr(TR_ATTEMPTS_EXCEEDED); _canvasWide->getTextBounds(txt1, 0, 0, &bx, &by, &bw, &bh);
            _canvasWide->setCursor((320 - bw) / 2, 25); _canvasWide->print(txt1); blitCanvas(_canvasWide, 0, 90, 320, 45);
            _canvasWide->fillScreen(C_BG_MAIN); char timeStr[64]; snprintf(timeStr, sizeof(timeStr), tr(TR_WAIT_SECONDS), secondsLeft);
            _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(C_TEXT_SUB);
            _canvasWide->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh); _canvasWide->setCursor((320 - bw) / 2, 25); _canvasWide->print(timeStr);
            blitCanvas(_canvasWide, 0, 135, 320, 45);
        }
        return;
    }

    if (_forceSettingsRedraw) {
        _tft->fillScreen(C_BG_MAIN); _tft->fillRect(4, 4, 312, 32, C_CARD_BG); _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        _tft->getTextBounds(titleTxt, 0, 0, &bx, &by, &bw, &bh); _tft->setCursor((320 - bw) / 2, 22); _tft->print(titleTxt);
        _tft->fillRoundRect(10, 202, 110, 32, 8, C_CARD_BG); _tft->getTextBounds(cancelTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(10 + (110 - bw) / 2, 224); _tft->print(cancelTxt);
        /* Botão de licença no canto inferior direito */
        _tft->fillRoundRect(200, 202, 110, 32, 8, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_SUB);
        String licTxt = tr(TR_LICENSE_TITLE);
        _tft->getTextBounds(licTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(200 + (110 - bw) / 2, 224); _tft->print(licTxt);
        _forceSettingsRedraw = false;
    }

    /* Status da autenticação via canvas — evita flicker */
    _canvasWide->fillScreen(C_BG_MAIN);
    if (_authFailed) {
        _canvasWide->setFont(&simutFont9pt);
        _canvasWide->setTextColor(C_TEMP_HOT);
        String invMsg = tr(TR_INVALID_PASSWORD);
        _canvasWide->getTextBounds(invMsg, 0, 0, &bx, &by, &bw, &bh);
        _canvasWide->setCursor((320 - bw) / 2, 20);
        _canvasWide->print(invMsg);
    } else {
        int pinLen = (int)_expectedPin.length();
        int dotSpacing = 20;
        int dotsStartX = (320 - (pinLen * dotSpacing)) / 2 + dotSpacing / 2;
        for (int i = 0; i < pinLen; i++) {
            int cx = dotsStartX + (i * dotSpacing);
            if (i < _authStep) _canvasWide->fillCircle(cx, 15, 6, C_ACCENT);
            else               _canvasWide->drawCircle(cx, 15, 6, C_TEXT_SUB);
        }
    }
    blitCanvas(_canvasWide, 0, 35, 320, 30);

    /* Botões do keypad via canvas — 2 botões por fila, 2 filas */
    for (int row = 0; row < 2; row++) {
        int rowY = 80 + (row * 60);
        _canvasWide->fillScreen(C_BG_MAIN);
        _canvasWide->setFont(&simutFont12pt);

        for (int col = 0; col < 2; col++) {
            int btnIdx = (row * 2) + col;
            int bx0 = (col == 0) ? 15 : 165;

            /* Botão com bordas arredondadas bem acabadas */
            _canvasWide->fillRoundRect(bx0, 0, 140, 45, 10, C_CARD_BG);
            _canvasWide->drawRoundRect(bx0, 0, 140, 45, 10, C_TEXT_SUB);

            /* Caracteres distribuídos no botão */
            _canvasWide->setTextColor(C_TEXT_MAIN);
            String chars = String(_keypadChars[btnIdx]);
            int slotWidth = 35;
            for (int j = 0; j < 4; j++) {
                String singleChar = String(chars.charAt(j));
                int16_t cbx, cby; uint16_t cbw, cbh;
                _canvasWide->getTextBounds(singleChar, 0, 0, &cbx, &cby, &cbw, &cbh);
                int charX = bx0 + (j * slotWidth) + ((slotWidth - cbw) / 2) - cbx;
                _canvasWide->setCursor(charX, 31);
                _canvasWide->print(singleChar);
            }
        }
        blitCanvas(_canvasWide, 0, rowY, 320, 45);
    }
}
