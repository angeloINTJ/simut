/**
 * @file    DisplayManager_Calibration.cpp
 * @brief   Touch calibration, sensitivity, display offset screens.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *          Inclui: calibração 4-point do touch, ajuste de sensibilidade
 *          (threshold), offset físico do display (±4 px), e o load/save
 *          dessa config persistente para SystemConfig::reserved[].
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include <math.h>

/* D1.A (bisect stage 1): globals POD volatile + writes em show*().
 * Volatile força writes a permanecerem. */
namespace {
    volatile bool  g_sensFirstDraw     = true;
    volatile bool  g_lastSensDone      = false;
    volatile float g_lastSensStability = -1.0f;
    volatile int   g_lastSensThreshold = -1;
    volatile int   g_lastCalPointIdx   = -1;
    volatile bool  g_lastCalHoldReady  = false;
    volatile int   g_lastCalStep       = -1;
}

void DisplayManager::showTouchCalibration() {
    /*
     * Fluxo integrado: sensibilidade primeiro, depois posição.
     * 1. MODE_SETTINGS_TOUCH_SENS — taps para calibrar threshold de pressão
     * 2. MODE_SETTINGS_TOUCH_CAL  — crosshairs para calibrar posição
     * A transição 1→2 é automática após conclusão da sensibilidade.
     */
    _sensCount     = 0;
    _sensStability = 0.0f;
    _sensThreshold = 0;
    _sensDone      = false;
    _sensDoneTime  = 0;
    _calStep = 0;
    _calPhase = 0;
    memset(_calRawX, 0, sizeof(_calRawX));
    memset(_calRawY, 0, sizeof(_calRawY));

    /* D1.A reset globals */
    g_sensFirstDraw     = true;
    g_lastSensDone      = false;
    g_lastSensStability = -1.0f;
    g_lastSensThreshold = -1;
    g_lastCalPointIdx   = -1;
    g_lastCalHoldReady  = false;
    g_lastCalStep       = -1;

    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_TOUCH_SENS;
    _forceSettingsRedraw = true;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}


void DisplayManager::loadTouchCalibration(const TouchCalData* cal) {
    if (!cal || cal->magic != 0xCA) {
        _calValid = false;
        return;
    }
    _calSwapXY = (cal->flags & 0x01) != 0;
    _calXMin   = cal->xMin;
    _calXMax   = cal->xMax;
    _calYMin   = cal->yMin;
    _calYMax   = cal->yMax;
    _calValid  = true;

    /* Threshold de sensibilidade: usa valor salvo, fallback 400 se zero */
    _sensZThreshold = (cal->zThreshold > 0) ? cal->zThreshold : 400;
}


void DisplayManager::fillCalData(TouchCalData* cal) const {
    if (!cal) return;
    cal->magic = 0xCA;
    cal->flags = _calSwapXY ? 0x01 : 0x00;
    cal->xMin  = _calXMin;
    cal->xMax  = _calXMax;
    cal->yMin  = _calYMin;
    cal->yMax  = _calYMax;
    cal->zThreshold = _sensZThreshold;
}


/* =========================================================================== */
/*              TELA DE AJUSTE DE POSICIONAMENTO DO DISPLAY                  */
/* =========================================================================== */

/**
 * @brief Entra na tela de ajuste de offset; snapshot do estado salvo para BACK.
 *
 * O estado "preview" é inicializado com o offset atualmente aplicado ao TFT
 * (que corresponde ao valor persistido, carregado via loadDisplayOffset() no
 * boot). Qualquer alteração via setas é aplicada live ao _tft, e BACK restaura
 * o snapshot caso o usuário desista.
 */
void DisplayManager::showSettingsDisplayOffset() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_DISPLAY_OFFSET;
    _offsetSavedX  = _tft ? _tft->getOffsetX() : 0;
    _offsetSavedY  = _tft ? _tft->getOffsetY() : 0;
    _offsetPreviewX = _offsetSavedX;
    _offsetPreviewY = _offsetSavedY;
    _lastOffsetDrawX = 99;  /* sentinel força redraw dos valores numéricos */
    _lastOffsetDrawY = 99;
    _forceSettingsRedraw = true;
    _repaintSettings     = true;
    mutex_exit(&_stateMutex);
}


/**
 * @brief Renderiza a tela de ajuste de offset seguindo o padrão das demais
 *        telas de configuração (barra superior com título + rodapé de botões).
 *
 * Layout:
 *   [TÍTULO]     (0..320, 0..32)
 *   Pad direcional centralizado em (160,120):
 *       ▲ (130..190, 55..95)
 *     ◀ (80..140, 100..140)   ● (148..172, 108..132)   ▶ (180..240, 100..140)
 *       ▼ (130..190, 145..185)
 *   Indicador numérico       (190..310, 60..180) — "X:+2  Y:-1"
 *   Hint de uso              (y ≈ 175..195) — pequena legenda da tela
 *   Rodapé: [BACK] [APPLY]   (y >= 200)
 */
void DisplayManager::drawSettingsDisplayOffset() {
    if (!_tft) return;
    bool full = _forceSettingsRedraw;
    int16_t bx, by; uint16_t bw, bh;

    if (full) {
        /* F-DISPLAY-ATOMIC Fase 2: full redraw via strips. Layout estático:
         *   - title bar y=4..36
         *   - direction pad (4 botões com setas) em y=55..185
         *   - reset button center em y=108..132
         *   - hint em y=198
         *   - back/apply buttons em y=204..236 */
        const String titleTxt = tr(TR_DISPLAY_OFFSET_TITLE);
        const String hintTxt = tr(TR_DISPLAY_OFFSET_HINT);
        const String backTxt = tr(TR_BACK);
        const String applyTxt = tr(TR_APPLY);
        const int cx = 160, cy = 120;
        GFXcanvas16* cv = beginScreenRender();
        if (cv) {
            for (int strip = 0; strip < 6; strip++) {
                cv->fillScreen(C_BG_MAIN);
                const int16_t yOff = -strip * RENDER_STRIP_H;

                /* Title bar */
                cv->fillRect(4, 4 + yOff, 312, 32, C_CARD_BG);
                cv->setFont(&simutFont9pt);
                cv->setTextColor(C_TEXT_MAIN);
                cv->setCursor(10, 22 + yOff);
                cv->print(titleTxt);

                /* Direction pad — 4 cápsulas com setas */
                cv->fillRoundRect(130, 55 + yOff, 60, 40, 8, C_CARD_BG);  /* UP */
                cv->fillTriangle(cx, 62 + yOff, cx - 10, 86 + yOff, cx + 10, 86 + yOff, C_TEXT_MAIN);
                cv->fillRoundRect(130, 145 + yOff, 60, 40, 8, C_CARD_BG); /* DOWN */
                cv->fillTriangle(cx - 10, 154 + yOff, cx + 10, 154 + yOff, cx, 178 + yOff, C_TEXT_MAIN);
                cv->fillRoundRect(80, 100 + yOff, 60, 40, 8, C_CARD_BG);  /* LEFT */
                cv->fillTriangle(90, cy + yOff, 120, cy - 10 + yOff, 120, cy + 10 + yOff, C_TEXT_MAIN);
                cv->fillRoundRect(180, 100 + yOff, 60, 40, 8, C_CARD_BG); /* RIGHT */
                cv->fillTriangle(230, cy + yOff, 200, cy - 10 + yOff, 200, cy + 10 + yOff, C_TEXT_MAIN);

                /* Reset center */
                cv->fillRoundRect(148, 108 + yOff, 24, 24, 4, C_ACCENT);
                cv->drawCircle(cx, cy + yOff, 4, C_BG_MAIN);

                /* Hint */
                cv->setFont(&simutFont9pt);
                cv->setTextColor(C_TEXT_SUB);
                cv->getTextBounds(hintTxt, 0, 0, &bx, &by, &bw, &bh);
                int hx = (320 - (int)bw) / 2;
                if (hx < 4) hx = 4;
                cv->setCursor(hx, 198 + yOff);
                cv->print(hintTxt);

                /* Back button */
                cv->fillRoundRect(10, 204 + yOff, 120, 32, 8, C_CARD_BG);
                cv->setTextColor(C_TEXT_MAIN);
                cv->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
                cv->setCursor(10 + (120 - (int)bw) / 2, 226 + yOff);
                cv->print(backTxt);

                /* Apply button */
                cv->fillRoundRect(190, 204 + yOff, 120, 32, 8, C_ACCENT);
                cv->setTextColor(C_BG_MAIN);
                cv->getTextBounds(applyTxt, 0, 0, &bx, &by, &bw, &bh);
                cv->setCursor(190 + (120 - (int)bw) / 2, 226 + yOff);
                cv->print(applyTxt);

                commitScreenStrip(strip);
            }
            endScreenRender();
        }
        _forceSettingsRedraw = false;
        _lastOffsetDrawX = 99;  /* força redraw numérico abaixo */
    }

    /* Valores numéricos do offset — repintados só quando mudam. */
    if (_offsetPreviewX != _lastOffsetDrawX || _offsetPreviewY != _lastOffsetDrawY) {
        _tft->fillRect(245, 60, 70, 90, C_BG_MAIN);
        _tft->setFont(&simutFont12pt);
        _tft->setTextColor(C_TEXT_MAIN);
        char buf[16];
        snprintf(buf, sizeof(buf), "X %c%d",
                 _offsetPreviewX >= 0 ? '+' : '-',
                 abs((int)_offsetPreviewX));
        _tft->setCursor(250, 90);
        _tft->print(buf);
        snprintf(buf, sizeof(buf), "Y %c%d",
                 _offsetPreviewY >= 0 ? '+' : '-',
                 abs((int)_offsetPreviewY));
        _tft->setCursor(250, 130);
        _tft->print(buf);
        _lastOffsetDrawX = _offsetPreviewX;
        _lastOffsetDrawY = _offsetPreviewY;
    }
}


/* =========================================================================== */
/*          PERSISTÊNCIA DO OFFSET DE DISPLAY (API pública)                  */
/* =========================================================================== */

/**
 * @brief Carrega o offset de posicionamento do bloco de config persistida e
 *        aplica imediatamente ao TFT. Invocado no boot por AppManager.
 */
void DisplayManager::loadDisplayOffset(const DisplayOffsetData* data) {
    if (!data || data->magic != 0xD0) {
        if (_tft) _tft->setDisplayOffset(0, 0);
        _offsetSavedX = 0;
        _offsetSavedY = 0;
        return;
    }
    int8_t ox = constrain((int)data->offsetX, -4, 4);
    int8_t oy = constrain((int)data->offsetY, -4, 4);
    if (_tft) _tft->setDisplayOffset(ox, oy);
    _offsetSavedX = ox;
    _offsetSavedY = oy;
}


/**
 * @brief Preenche o struct com o offset atualmente aplicado, para persistência.
 */
void DisplayManager::fillDisplayOffsetData(DisplayOffsetData* data) const {
    if (!data) return;
    data->magic    = 0xD0;
    data->offsetX  = _tft ? _tft->getOffsetX() : 0;
    data->offsetY  = _tft ? _tft->getOffsetY() : 0;
    data->reserved = 0;
}


int8_t DisplayManager::getDisplayOffsetX() const {
    return _tft ? _tft->getOffsetX() : 0;
}

int8_t DisplayManager::getDisplayOffsetY() const {
    return _tft ? _tft->getOffsetY() : 0;
}


/**
 * @brief Reseta a calibração do touch para os valores padrão de fábrica.
 *
 * Restaura os limites raw genéricos (200..3800) e invalida a flag.
 * A próxima interação usará mapeamento estimado até nova calibração.
 */
void DisplayManager::resetTouchCalibration() {
    _calValid  = false;
    _calSwapXY = false;
    _calXMin   = 200;
    _calXMax   = 3800;
    _calYMin   = 200;
    _calYMax   = 3800;
    _sensZThreshold = 400;
}


/* =========================================================================== */
/*               CALIBRAÇÃO DE SENSIBILIDADE DO TOUCH                        */
/* =========================================================================== */

/**
 * @brief Inicia a tela de calibração de sensibilidade.
 * Reseta contadores e entra no modo de coleta de amostras.
 */
void DisplayManager::showTouchSensitivity() {
    _sensCount     = 0;
    _sensStability = 0.0f;
    _sensThreshold = 0;
    _sensDone      = false;
    _sensDoneTime  = 0;
    _uiMode = MODE_SETTINGS_TOUCH_SENS;
    _forceSettingsRedraw = true;
    _repaintSettings = true;
}

/**
 * @brief Desenha a tela de calibração de sensibilidade.
 *
 * Layout:
 * - Título na barra superior
 * - Crosshair alvo no centro
 * - Texto de progresso "Tap N/20"
 * - Barra vertical à direita mostrando estabilidade (0..100%)
 * - Valor numérico do threshold
 */
void DisplayManager::drawTouchSensitivity() {
    bool fullRedraw = _forceSettingsRedraw;
    _forceSettingsRedraw = false;

    if (fullRedraw) {
        /* F-DISPLAY-ATOMIC Fase 2: full redraw via strips. Layout estático:
         *   - title bar y=4..36 (text em y=22)
         *   - cancel button y=195..235 (text em y=220)
         *   - bar frame em (289, 38..192). */
        const String titleTxt = tr(TR_SENS_TITLE);
        const String backTxt = tr(TR_CANCEL);
        GFXcanvas16* cv = beginScreenRender();
        if (cv) {
            int16_t bx, by; uint16_t bw, bh;
            for (int strip = 0; strip < 6; strip++) {
                cv->fillScreen(C_BG_MAIN);
                const int16_t yOff = -strip * RENDER_STRIP_H;

                /* Title bar */
                cv->fillRect(4, 4 + yOff, 312, 32, C_CARD_BG);
                cv->setFont(&simutFont9pt);
                cv->setTextColor(C_TEXT_MAIN);
                cv->setCursor(10, 22 + yOff);
                cv->print(titleTxt);

                /* Cancel button */
                cv->fillRoundRect(5, 195 + yOff, 120, 40, 8, C_CARD_BG);
                cv->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
                cv->setCursor(5 + (120 - bw) / 2, 220 + yOff);
                cv->print(backTxt);

                /* Bar frame (vertical à direita) */
                cv->drawRect(289, 38 + yOff, 26, 154, C_TEXT_OFF);

                commitScreenStrip(strip);
            }
            endScreenRender();
        }
    }

    /* D1.C: crosshair só repinta na 1ª vez ou quando _sensDone muda */
    int cx = 140, cy = 115;
    if (g_sensFirstDraw || g_lastSensDone != _sensDone) {
        uint16_t crossColor = _sensDone ? C_TEMP_OK : C_ACCENT;
        _tft->fillRect(cx - 30, cy - 1, 60, 3, C_BG_MAIN);
        _tft->fillRect(cx - 1, cy - 30, 3, 60, C_BG_MAIN);
        _tft->drawLine(cx - 15, cy, cx + 15, cy, crossColor);
        _tft->drawLine(cx, cy - 15, cx, cy + 15, crossColor);
        _tft->drawCircle(cx, cy, 12, crossColor);
    }

    /* D1.C: texto de progresso só quando _sensDone muda ou no 1º draw */
    if (g_sensFirstDraw || g_lastSensDone != _sensDone) {
        _tft->fillRect(80, 150, 140, 30, C_BG_MAIN);
        _tft->setFont(&simutFont9pt);
        _tft->setTextColor(C_TEXT_MAIN);
        int16_t bx2, by2; uint16_t bw2, bh2;
        String txt = _sensDone ? tr(TR_SENS_DONE) : tr(TR_CAL_TOUCH_POINT);
        _tft->getTextBounds(txt, 0, 0, &bx2, &by2, &bw2, &bh2);
        _tft->setCursor(140 - bw2 / 2, 168);
        _tft->print(txt);
    }

    /* D1.C: barra só quando _sensStability muda */
    if (g_sensFirstDraw || fabsf(g_lastSensStability - _sensStability) > 0.01f) {
        int barX = 290, barY = 39, barW = 24, barH = 152;
        int fillH = (int)(barH * _sensStability);
        if (fillH > barH) fillH = barH;
        if (fillH < barH) _tft->fillRect(barX, barY, barW, barH - fillH, C_BG_MAIN);
        uint16_t barColor = (_sensStability >= 0.85f) ? C_TEMP_OK : C_ACCENT;
        if (fillH > 0) _tft->fillRect(barX, barY + barH - fillH, barW, fillH, barColor);
        g_lastSensStability = _sensStability;
    }

    /* D1.C: valor numérico só quando muda */
    if (g_sensFirstDraw || g_lastSensThreshold != (int)_sensThreshold) {
        _tft->fillRect(280, 195, 40, 20, C_BG_MAIN);
        _tft->setFont(NULL); _tft->setTextSize(1);
        _tft->setTextColor(C_TEXT_OFF);
        char valBuf[8];
        snprintf(valBuf, sizeof(valBuf), "%d", _sensThreshold);
        _tft->setCursor(295, 198);
        _tft->print(valBuf);
        g_lastSensThreshold = (int)_sensThreshold;
    }

    g_lastSensDone = _sensDone;
    g_sensFirstDraw = false;
}


void DisplayManager::mapTouchPoint(TS_Point raw, int16_t &outX, int16_t &outY) {
    int16_t rawForX, rawForY;

    if (_calSwapXY) {
        rawForX = raw.y;
        rawForY = raw.x;
    } else {
        rawForX = raw.x;
        rawForY = raw.y;
    }

    outX = (int16_t)constrain(map(rawForX, _calXMin, _calXMax, 0, 320), 0, 319);
    outY = (int16_t)constrain(map(rawForY, _calYMin, _calYMax, 0, 240), 0, 239);
}


void DisplayManager::drawCrosshair(int16_t cx, int16_t cy, uint16_t color) {
    const int16_t sz = 10;
    _tft->drawLine(cx - sz, cy, cx + sz, cy, color);
    _tft->drawLine(cx, cy - sz, cx, cy + sz, color);
    _tft->drawCircle(cx, cy, sz - 2, color);
}

/* Versão canvas-aware do drawCrosshair pra strip-based rendering.
 * Usado pelos blocos de full-screen redraw das 4 telas de calibração. */
static inline void drawCrosshairOnCanvas(GFXcanvas16* cv, int16_t cx, int16_t cy, uint16_t color) {
    const int16_t sz = 10;
    cv->drawLine(cx - sz, cy, cx + sz, cy, color);
    cv->drawLine(cx, cy - sz, cx, cy + sz, color);
    cv->drawCircle(cx, cy, sz - 2, color);
}


/* F-DISPLAY-ATOMIC Fase 2: drawCalibrationMessage via strips.
 * Layout: ícone success/fail (y=70..110) + mensagem (y=130) + botão (y=185..225). */
void DisplayManager::drawCalibrationMessage() {
    if (!_tft) return;

    bool isSuccess = (_calPhase == 2);
    uint16_t iconColor = isSuccess ? C_TEMP_OK : C_TEMP_WARM;
    const char* msg = isSuccess ? tr(TR_CAL_DONE) : tr(TR_CAL_REJECTED);
    const char* btnLabel = tr(TR_UNDERSTOOD);

    GFXcanvas16* cv = beginScreenRender();
    if (!cv) return;

    int16_t x1, y1; uint16_t w, h_bound;

    for (int strip = 0; strip < 6; strip++) {
        cv->fillScreen(C_BG_MAIN);
        const int16_t yOff = -strip * RENDER_STRIP_H;

        /* Ícone (y_screen=70..110) */
        if (isSuccess) {
            cv->drawLine(130, 90 + yOff,  150, 110 + yOff, iconColor);
            cv->drawLine(131, 90 + yOff,  151, 110 + yOff, iconColor);
            cv->drawLine(150, 110 + yOff, 190, 70 + yOff,  iconColor);
            cv->drawLine(151, 110 + yOff, 191, 70 + yOff,  iconColor);
        } else {
            cv->drawLine(145, 70 + yOff,  175, 100 + yOff, iconColor);
            cv->drawLine(146, 70 + yOff,  176, 100 + yOff, iconColor);
            cv->drawLine(175, 70 + yOff,  145, 100 + yOff, iconColor);
            cv->drawLine(176, 70 + yOff,  146, 100 + yOff, iconColor);
        }

        /* Mensagem em y_screen=130 */
        cv->setFont(&simutFont9pt);
        cv->setTextColor(C_TEXT_MAIN);
        cv->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h_bound);
        cv->setCursor((320 - w) / 2, 130 + yOff);
        cv->print(msg);

        /* Botão "Entendi" em y_screen=185..225 */
        cv->fillRoundRect(60, 185 + yOff, 200, 40, 12, C_ACCENT);
        cv->setFont(&simutFont12pt);
        cv->setTextColor(C_BG_MAIN);
        cv->getTextBounds(btnLabel, 0, 0, &x1, &y1, &w, &h_bound);
        cv->setCursor(160 - (w / 2), 212 + yOff);
        cv->print(btnLabel);

        commitScreenStrip(strip);
    }
    endScreenRender();
}


/* F-DISPLAY-ATOMIC Fase 2: drawTouchCalibration. Full-screen redraw via
 * strips (renderiza TODA a tela: crosshair atual + text labels). Atualizações
 * incrementais (entre tap dos 8 pontos) ficam direto no _tft — áreas pequenas,
 * top-down não-perceptível. */
void DisplayManager::drawTouchCalibration() {
    bool fullRedraw = _forceSettingsRedraw;

    if (_calPhase >= 1) {
        drawCalibrationMessage();
        _forceSettingsRedraw = false;
        return;
    }

    int pointIdx = _calStep % 4;
    int cycleNum = (_calStep < 4) ? 1 : 2;

    if (_calStep >= 8) {
        _forceSettingsRedraw = false;
        return;
    }

    /* Texts buffers (used em ambos paths). */
    const char* title = tr(TR_CAL_TITLE);
    char msg[48];
    snprintf(msg, sizeof(msg), "%s (%d/4)", tr(TR_CAL_TOUCH_POINT), pointIdx + 1);
    char cycleBuf[24];
    snprintf(cycleBuf, sizeof(cycleBuf), "[ %d / 2 ]", cycleNum);

    if (fullRedraw) {
        /* Full-screen render via strips: crosshair atual + 3 linhas de texto. */
        const int16_t cx = CAL_SCR_X[pointIdx];
        const int16_t cy = CAL_SCR_Y[pointIdx];
        const uint16_t crossColor = _calHoldReady ? C_TEMP_OK : C_ACCENT;

        GFXcanvas16* cv = beginScreenRender();
        if (cv) {
            int16_t bx, by; uint16_t bw, bh;
            for (int strip = 0; strip < 6; strip++) {
                cv->fillScreen(C_BG_MAIN);
                const int16_t yOff = -strip * RENDER_STRIP_H;

                /* Crosshair em (cx, cy) — visível só na strip que contem cy */
                drawCrosshairOnCanvas(cv, cx, cy + yOff, crossColor);

                /* Title em y=100 (font 9pt accent) */
                cv->setFont(&simutFont9pt);
                cv->setTextColor(C_ACCENT);
                cv->getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
                cv->setCursor((320 - bw) / 2, 100 + yOff);
                cv->print(title);

                /* Msg em y=122 */
                cv->setTextColor(C_TEXT_MAIN);
                cv->getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);
                cv->setCursor((320 - bw) / 2, 122 + yOff);
                cv->print(msg);

                /* Cycle em y=140 (default font, size 1) */
                cv->setFont(NULL); cv->setTextSize(1);
                cv->setTextColor(C_TEXT_OFF);
                cv->getTextBounds(cycleBuf, 0, 0, &bx, &by, &bw, &bh);
                cv->setCursor((320 - bw) / 2, 140 + yOff);
                cv->print(cycleBuf);

                commitScreenStrip(strip);
            }
            endScreenRender();
        }
        _forceSettingsRedraw = false;
        return;
    }

    /* D1.D: crosshair só repinta quando pointIdx ou _calHoldReady mudam. */
    bool crosshairChanged = (g_lastCalPointIdx != pointIdx) || (g_lastCalHoldReady != _calHoldReady);
    if (crosshairChanged) {
        if (pointIdx > 0) {
            drawCrosshair(CAL_SCR_X[pointIdx - 1], CAL_SCR_Y[pointIdx - 1], C_BG_MAIN);
        }
        if (_calStep == 4) {
            drawCrosshair(CAL_SCR_X[3], CAL_SCR_Y[3], C_BG_MAIN);
        }
        drawCrosshair(CAL_SCR_X[pointIdx], CAL_SCR_Y[pointIdx],
                      _calHoldReady ? C_TEMP_OK : C_ACCENT);
        g_lastCalPointIdx = pointIdx;
        g_lastCalHoldReady = _calHoldReady;
    }

    /* D1.E: texto via canvas atômico — sem fillRect direto no _tft (que
     * causava flash preto visível antes do texto reaparecer).
     * Layout cobre y=85..150 (65px). Como _canvasWide é 320×45, divido em
     * 2 strips: strip 0 (y=85..130) cobre title+msg, strip 1 (y=130..150)
     * cobre cycle. Cada blit é atômico (1 SPI burst). */
    if (g_lastCalStep != _calStep && _canvasWide) {
        GFXcanvas16* cv = _canvasWide;
        int16_t bx, by; uint16_t bw, bh;

        /* Strip 0: y_screen 85..130 (45px). Title (y=100) + msg (y=122). */
        cv->fillScreen(C_BG_MAIN);
        cv->setFont(&simutFont9pt);
        cv->setTextColor(C_ACCENT);
        cv->getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
        cv->setCursor((320 - bw) / 2, 100 - 85); cv->print(title);
        cv->setTextColor(C_TEXT_MAIN);
        cv->getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);
        cv->setCursor((320 - bw) / 2, 122 - 85); cv->print(msg);
        blitCanvas(cv, 0, 85, 320, 45);

        /* Strip 1: y_screen 130..150 (20px). Cycle (y=140 → canvas y=10). */
        cv->fillScreen(C_BG_MAIN);
        cv->setFont(NULL); cv->setTextSize(1);
        cv->setTextColor(C_TEXT_OFF);
        cv->getTextBounds(cycleBuf, 0, 0, &bx, &by, &bw, &bh);
        cv->setCursor((320 - bw) / 2, 140 - 130); cv->print(cycleBuf);
        blitCanvas(cv, 0, 130, 320, 20);

        g_lastCalStep = _calStep;
    }

    _forceSettingsRedraw = false;
}

