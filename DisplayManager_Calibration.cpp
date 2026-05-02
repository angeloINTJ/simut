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
        _tft->fillScreen(C_BG_MAIN);

        /* Barra superior — título. */
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22);
        _tft->print(tr(TR_DISPLAY_OFFSET_TITLE));

        /* Pad direcional — desenha os 4 cursos como cápsulas com seta. */
        const int cx = 160, cy = 120;
        /* UP */
        _tft->fillRoundRect(130, 55, 60, 40, 8, C_CARD_BG);
        _tft->fillTriangle(cx, 62, cx - 10, 86, cx + 10, 86, C_TEXT_MAIN);
        /* DOWN */
        _tft->fillRoundRect(130, 145, 60, 40, 8, C_CARD_BG);
        _tft->fillTriangle(cx - 10, 154, cx + 10, 154, cx, 178, C_TEXT_MAIN);
        /* LEFT */
        _tft->fillRoundRect(80, 100, 60, 40, 8, C_CARD_BG);
        _tft->fillTriangle(90, cy, 120, cy - 10, 120, cy + 10, C_TEXT_MAIN);
        /* RIGHT */
        _tft->fillRoundRect(180, 100, 60, 40, 8, C_CARD_BG);
        _tft->fillTriangle(230, cy, 200, cy - 10, 200, cy + 10, C_TEXT_MAIN);
        /* Botão central de reset (círculo pequeno). */
        _tft->fillRoundRect(148, 108, 24, 24, 4, C_ACCENT);
        _tft->drawCircle(cx, cy, 4, C_BG_MAIN);

        /* Hint curto abaixo do pad. */
        _tft->setFont(&simutFont9pt);
        _tft->setTextColor(C_TEXT_SUB);
        {
            const char* hint = tr(TR_DISPLAY_OFFSET_HINT);
            _tft->getTextBounds(hint, 0, 0, &bx, &by, &bw, &bh);
            int hx = (320 - (int)bw) / 2;
            if (hx < 4) hx = 4;
            _tft->setCursor(hx, 198);
            _tft->print(hint);
        }

        /* Rodapé — BACK (esq) e APPLY (dir). */
        _tft->fillRoundRect(10, 204, 120, 32, 8, C_CARD_BG);
        _tft->setTextColor(C_TEXT_MAIN);
        {
            String back = tr(TR_BACK);
            _tft->getTextBounds(back, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(10 + (120 - (int)bw) / 2, 226);
            _tft->print(back);
        }
        _tft->fillRoundRect(190, 204, 120, 32, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        {
            String apply = tr(TR_APPLY);
            _tft->getTextBounds(apply, 0, 0, &bx, &by, &bw, &bh);
            _tft->setCursor(190 + (120 - (int)bw) / 2, 226);
            _tft->print(apply);
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
        _tft->fillScreen(C_BG_MAIN);

        /* Barra de título */
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22);
        _tft->print(tr(TR_SENS_TITLE));

        /* Botão CANCEL (canto inferior esquerdo) */
        _tft->fillRoundRect(5, 195, 120, 40, 8, C_CARD_BG);
        int16_t bx, by; uint16_t bw, bh;
        String backTxt = tr(TR_CANCEL);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(5 + (120 - bw) / 2, 220);
        _tft->print(backTxt);

        /* Moldura da barra vertical (direita) */
        _tft->drawRect(289, 38, 26, 154, C_TEXT_OFF);
    }

    /* Crosshair central */
    int cx = 140, cy = 115;
    uint16_t crossColor = _sensDone ? C_TEMP_OK : C_ACCENT;
    _tft->fillRect(cx - 30, cy - 1, 60, 3, C_BG_MAIN);
    _tft->fillRect(cx - 1, cy - 30, 3, 60, C_BG_MAIN);
    _tft->drawLine(cx - 15, cy, cx + 15, cy, crossColor);
    _tft->drawLine(cx, cy - 15, cx, cy + 15, crossColor);
    _tft->drawCircle(cx, cy, 12, crossColor);

    /* Texto de progresso */
    _tft->fillRect(80, 150, 140, 30, C_BG_MAIN);
    _tft->setFont(&simutFont9pt);
    _tft->setTextColor(C_TEXT_MAIN);

    if (_sensDone) {
        int16_t bx2, by2; uint16_t bw2, bh2;
        String doneMsg = tr(TR_SENS_DONE);
        _tft->getTextBounds(doneMsg, 0, 0, &bx2, &by2, &bw2, &bh2);
        _tft->setCursor(140 - bw2 / 2, 168);
        _tft->print(doneMsg);
    } else {
        /* Instrução: reutiliza "Toque na mira" da calibração de posição */
        int16_t bx2, by2; uint16_t bw2, bh2;
        String holdMsg = tr(TR_CAL_TOUCH_POINT);
        _tft->getTextBounds(holdMsg, 0, 0, &bx2, &by2, &bw2, &bh2);
        _tft->setCursor(140 - bw2 / 2, 168);
        _tft->print(holdMsg);
    }

    /* Barra vertical de estabilidade (dentro da moldura) */
    int barX = 290, barY = 39, barW = 24, barH = 152;
    int fillH = (int)(barH * _sensStability);
    if (fillH > barH) fillH = barH;

    /* Fundo (parte não preenchida) */
    if (fillH < barH) {
        _tft->fillRect(barX, barY, barW, barH - fillH, C_BG_MAIN);
    }
    /* Preenchimento (de baixo para cima) */
    uint16_t barColor = (_sensStability >= 0.85f) ? C_TEMP_OK : C_ACCENT;
    if (fillH > 0) {
        _tft->fillRect(barX, barY + barH - fillH, barW, fillH, barColor);
    }

    /* Valor numérico abaixo da barra */
    _tft->fillRect(280, 195, 40, 20, C_BG_MAIN);
    _tft->setFont(NULL);
    _tft->setTextSize(1);
    _tft->setTextColor(C_TEXT_OFF);
    char valBuf[8];
    snprintf(valBuf, sizeof(valBuf), "%d", _sensThreshold);
    _tft->setCursor(295, 198);
    _tft->print(valBuf);
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


void DisplayManager::drawCalibrationMessage() {
    if (!_tft) return;
    int16_t x1, y1; uint16_t w, h_bound;

    _tft->fillScreen(C_BG_MAIN);


    bool isSuccess = (_calPhase == 2);
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


    const char* msg = isSuccess ? tr(TR_CAL_DONE) : tr(TR_CAL_REJECTED);
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


void DisplayManager::drawTouchCalibration() {
    bool fullRedraw = _forceSettingsRedraw;


    if (_calPhase >= 1) {
        drawCalibrationMessage();
        _forceSettingsRedraw = false;
        return;
    }


    int pointIdx = _calStep % 4;
    int cycleNum = (_calStep < 4) ? 1 : 2;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);
    }

    if (_calStep < 8) {

        if (!fullRedraw && pointIdx > 0) {
            drawCrosshair(CAL_SCR_X[pointIdx - 1], CAL_SCR_Y[pointIdx - 1], C_BG_MAIN);
        }

        if (!fullRedraw && _calStep == 4) {
            drawCrosshair(CAL_SCR_X[3], CAL_SCR_Y[3], C_BG_MAIN);
        }


        drawCrosshair(CAL_SCR_X[pointIdx], CAL_SCR_Y[pointIdx],
                      _calHoldReady ? C_TEMP_OK : C_ACCENT);


        _tft->fillRect(20, 85, 280, 65, C_BG_MAIN);


        _tft->setFont(&simutFont9pt);
        _tft->setTextColor(C_ACCENT);
        int16_t bx, by; uint16_t bw, bh;
        const char* title = tr(TR_CAL_TITLE);
        _tft->getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor((320 - bw) / 2, 100);
        _tft->print(title);


        char msg[48];
        snprintf(msg, sizeof(msg), "%s (%d/4)", tr(TR_CAL_TOUCH_POINT), pointIdx + 1);
        _tft->setTextColor(C_TEXT_MAIN);
        _tft->getTextBounds(msg, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor((320 - bw) / 2, 122);
        _tft->print(msg);


        char cycleBuf[24];
        snprintf(cycleBuf, sizeof(cycleBuf), "[ %d / 2 ]", cycleNum);
        _tft->setFont(NULL); _tft->setTextSize(1);
        _tft->setTextColor(C_TEXT_OFF);
        _tft->getTextBounds(cycleBuf, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor((320 - bw) / 2, 140);
        _tft->print(cycleBuf);
    }

    _forceSettingsRedraw = false;
}

