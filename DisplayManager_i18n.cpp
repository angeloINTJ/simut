/**
 * @file    DisplayManager_i18n.cpp
 * @brief   i18n: DICTIONARY_EN (hardcoded), tr() dinâmico, settings screen.
 * @details F-LANGPACK Etapa 1+2 — DICTIONARY agora é 1D só EN. Slot 1
 *          do menu mostra _activeLang.name/code dinamicamente (via
 *          unaccent para ASCII no TFT). Sem .lng carregado, slot 1
 *          fica oculto (gated por _activeLangLoaded).
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"

static const char* const DICTIONARY_EN[TR_KEYS_COUNT] = {
    "AMBIENT", "Settings > Main", "Settings > Themes", "Settings > Language", "EXIT",
    "APPLY", "CANCEL", "Security Authentication", "ACCESS BLOCKED", "Reboot required",
    "Attempts Exceeded", "Wait %ld seconds...", "Invalid Password!", "Loading...", "Reading History...",
    "No Data", "MAXIMUM", "MINIMUM", "Temperature", "Humidity",
    "PLOT CHART", "1. Visual Themes", "2. Alarm Limits", "3. Alarm Sounds", "4. System Language",
    "Applying Theme...", "SAVE", "Alarm Limits", "Temp Min", "Temp Max",
    "Hum Min", "Hum Max", "ENTER", "SKIP", "5. Change Password",
    "New Password", "6. Touch Calibration", "Touch Calibration", "Touch the crosshair", "Calibration Done!",
    "Imprecise touches! Try again.", "Confirm Password", "Password too short! (min 4)", "Passwords don't match!", "Password saved!",
    "UNDERSTOOD", "Sound Settings", "Touch Click", "Confirmation", "Error Sound",
    "Alarm Sound", "Mute All", "Sys Volume", "Alarm Vol", "ON",
    "OFF", "Web Access", "Melody", "7. License", "MIT License",
    "ACTIVE", "Silence 120s", "Deactivate", "Min/Max", "Silenced",
    "%RH", "7. Touch Sensitivity", "Touch Sensitivity", "Tap %d/%d", "Calibration Done!",
    "AVERAGE", "STD DEV", "Error", "Configuration Mode", "8. System Status",
    "System Status",
    "9. Display Alignment", "Display Alignment", "Adjust +/-4 px. Saving clears touch calibration."
};

const char* DisplayManager::tr(LangKey key) {
    if (_activeLangLoaded && _currentLangIdx != LANG_EN &&
        _activeLang.strings[key] != nullptr) {
        /* F-LANGPACK-ASCII: TFT renderiza só ASCII (fonte sem glifos
         * Latin-1). Aplica unaccent em scratch rotativo de 4 slots —
         * suporta até 4 chamadas concorrentes na mesma expressão (ex:
         * snprintf("%s %s ...", tr(A), tr(B), ...)). */
        static char scratch[4][96];
        static uint8_t scratchIdx = 0;
        char* buf = scratch[scratchIdx];
        scratchIdx = (scratchIdx + 1) & 3;
        unaccent(_activeLang.strings[key], buf, sizeof(scratch[0]));
        return buf;
    }
    return DICTIONARY_EN[key];
}

void DisplayManager::showSettingsLang(int currentLang) {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_LANG;
    _previewLangIdx = currentLang;
    _langPage = currentLang / 4;
    _forceSettingsRedraw = true;
    _lastLangPage = -1;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsLang() {
    if (!_canvasWide) return;

    bool fullRedraw  = _forceSettingsRedraw;
    bool pageChanged = (_langPage != _lastLangPage);


    /* F-LANGPACK Etapa 2: slot 1 visível quando .lng carregado;
     * mostra _activeLang.name/code transliterados (unaccent) para o
     * TFT, que renderiza só ASCII na fonte atual. */
    int activeSlots = _activeLangLoaded ? LANG_COUNT : 1;
    char slot1Name[40] = "(install .lng)";
    char slot1Code[12] = "??";
    if (_activeLangLoaded && _activeLang.name[0]) {
        unaccent(_activeLang.name, slot1Name, sizeof(slot1Name));
    }
    if (_activeLangLoaded && _activeLang.code[0]) {
        unaccent(_activeLang.code, slot1Code, sizeof(slot1Code));
    }
    int totalPages = (activeSlots + 3) / 4;
    if (_langPage >= totalPages) _langPage = totalPages - 1;
    if (_langPage < 0) _langPage = 0;


    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);


        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22);
        _tft->print(tr(TR_CONFIG_LANG));


        int btnY = 195; int btnH = 40;
        int16_t bx, by; uint16_t bw, bh;


        _tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);


        _tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);


        _tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
        _tft->setFont(&simutFont9pt);
        _tft->setTextColor(C_TEXT_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(141 + (75 - bw) / 2, btnY + 25);
        _tft->print(backTxt);


        _tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String appTxt = tr(TR_APPLY);
        _tft->getTextBounds(appTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(222 + (93 - bw) / 2, btnY + 25);
        _tft->print(appTxt);
    }


    if (fullRedraw || pageChanged) {
        int trackX = 302; int trackY = 40;
        int trackW = 8;   int trackH = 146;

        _tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
        _tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);

        int thumbH = trackH / totalPages;
        if (thumbH < 20) thumbH = 20;
        int thumbY = trackY;
        if (totalPages > 1) {
            thumbY += (_langPage * (trackH - thumbH)) / (totalPages - 1);
        }
        _tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
    }


    int startIdx = _langPage * 4;
    int yBase    = 40;
    int itemW    = 285;

    for (int i = 0; i < 4; i++) {
        int actualIdx = startIdx + i;
        int y = yBase + (i * 38);


        if (!fullRedraw && !pageChanged) {
            if (actualIdx != _previewLangIdx && actualIdx != _lastPreviewLangIdx) continue;
        }

        _canvasWide->fillScreen(C_BG_MAIN);

        if (actualIdx < activeSlots) {
            bool isSelected = (actualIdx == _previewLangIdx);
            uint16_t bg  = isSelected ? C_ACCENT  : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;


            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);


            _canvasWide->setFont(&simutFont9pt);
            _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24);
            _canvasWide->print(actualIdx == LANG_EN ? "English" : slot1Name);


            _canvasWide->setCursor(itemW - 35, 24);
            _canvasWide->print(actualIdx == LANG_EN ? "EN" : slot1Code);
        }

        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }


    _forceSettingsRedraw = false;
    _lastLangPage = _langPage;
    _lastPreviewLangIdx = _previewLangIdx;
}
