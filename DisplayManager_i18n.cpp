/**
 * @file    DisplayManager_i18n.cpp
 * @brief   i18n: DICTIONARY (EN+PT), tr(), language settings screen.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *          DICTIONARY tem internal linkage (const namespace scope), então
 *          tr() precisa ficar no mesmo TU. drawSettingsLang() vem junto
 *          porque usa LANG_NAMES/LANG_FLAGS file-static.
 *
 * @project SIMUT
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"

static const char* const LANG_NAMES[LANG_COUNT] = {
    "English", "Portugues"
};
static const char* const LANG_FLAGS[LANG_COUNT] = {
    "EN", "PT"
};
static_assert(sizeof(LANG_NAMES)/sizeof(LANG_NAMES[0]) == LANG_COUNT,
              "LANG_NAMES count must match LanguageCode LANG_COUNT");
static_assert(sizeof(LANG_FLAGS)/sizeof(LANG_FLAGS[0]) == LANG_COUNT,
              "LANG_FLAGS count must match LanguageCode LANG_COUNT");

const char* const DICTIONARY[LANG_COUNT][TR_KEYS_COUNT] = {

    {
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
    },

    {
        "AMBIENTE", "Configuracoes > Principal", "Configuracoes > Temas", "Configuracoes > Idioma", "SAIR",
        "APLICAR", "CANCELAR", "Autenticacao de Seguranca", "ACESSO BLOQUEADO", "Reinicializacao requerida",
        "Tentativas Excedidas", "Aguarde %ld segundos...", "Senha Invalida!", "Carregando...", "Lendo Historico...",
        "Sem Dados", "MAXIMO", "MINIMO", "Temperatura", "Umidade",
        "GERAR GRAFICO", "1. Temas Visuais", "2. Limites de Alarme", "3. Sons de Alarme", "4. Idioma do Sistema",
        "Aplicando Tema...", "SALVAR", "Limites de Alarme", "Temp Min", "Temp Max",
        "Umid Min", "Umid Max", "ENTRAR", "PULAR", "5. Alterar Senha",
        "Nova Senha", "6. Calibrar Touch", "Calibracao do Touch", "Toque na mira", "Calibracao Concluida!",
        "Toques imprecisos! Tente novamente.", "Confirmar Senha", "Senha muito curta! (min 4)", "Senhas nao coincidem!", "Senha salva!",
        "ENTENDI", "Config. de Sons", "Toque na Tela", "Confirmacao", "Som de Erro",
        "Som de Alarme", "Silenciar Tudo", "Vol. Sistema", "Vol. Alarme", "SIM",
        "NAO", "Acesso Web", "Melodia", "7. Licenca", "Licenca MIT",
        "ATIVO", "Silenciar 120s", "Desativar", "Min/Max", "Silenciado",
        "%UR", "7. Sensibilidade do Toque", "Sensibilidade do Toque", "Toque %d/%d", "Calibracao Concluida!",
        "MEDIA", "DESVIO", "Erro", "Modo de Configuracao", "8. Status do Sistema",
        "Status do Sistema",
        "9. Alinhamento da Tela", "Alinhamento da Tela", "Ajuste +/-4 px. Salvar reinicia calibracao do touch."
    }
};

const char* DisplayManager::tr(LangKey key) { return DICTIONARY[_currentLangIdx][key]; }

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


    int totalPages = (LANG_COUNT + 3) / 4;
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

        if (actualIdx < LANG_COUNT) {
            bool isSelected = (actualIdx == _previewLangIdx);
            uint16_t bg  = isSelected ? C_ACCENT  : C_CARD_BG;
            uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;


            _canvasWide->fillRoundRect(0, 0, itemW, 34, 8, bg);
            if (!isSelected) _canvasWide->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);


            _canvasWide->setFont(&simutFont9pt);
            _canvasWide->setTextColor(txt);
            _canvasWide->setCursor(10, 24);
            _canvasWide->print(LANG_NAMES[actualIdx]);


            _canvasWide->setCursor(itemW - 35, 24);
            _canvasWide->print(LANG_FLAGS[actualIdx]);
        }

        blitCanvas(_canvasWide, 10, y, itemW, 34);
    }


    _forceSettingsRedraw = false;
    _lastLangPage = _langPage;
    _lastPreviewLangIdx = _previewLangIdx;
}
