/**
 * @file    DisplayManager_Dashboard.cpp
 * @brief   Dashboard rendering: top bar, ambient/slot panels, bottom buttons.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *          Inclui também helpers de rounded corners (fixCardCorners,
 *          maskStripCorners), drawInterfaceFixed (bg fixo) e blitCanvas
 *          (DMA push de canvas → TFT). restoreNormalDashboard reposiciona
 *          tudo depois de eventos modais (auth/license/alarm).
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "StorageManager.h"

void DisplayManager::fixCardCorners(int16_t x, int16_t y, int16_t w,
                                    int16_t h, int16_t r,
                                    uint16_t borderColor) {
    if (!_tft) return;
    for (int16_t i = 0; i < r; i++) {
        int16_t span = (int16_t)(sqrtf(2.0f * r * i - (float)(i * i)) + 0.5f);
        int16_t gap  = r - span;
        if (gap <= 0) continue;
        _tft->drawFastHLine(x,           y + i,         gap, C_BG_MAIN);
        _tft->drawFastHLine(x + w - gap, y + i,         gap, C_BG_MAIN);
        _tft->drawFastHLine(x,           y + h - 1 - i, gap, C_BG_MAIN);
        _tft->drawFastHLine(x + w - gap, y + h - 1 - i, gap, C_BG_MAIN);
    }
    _tft->drawRoundRect(x, y, w, h, r, borderColor);
}


void DisplayManager::maskStripCorners(GFXcanvas16* canvas,
                                      int16_t stripRow, int16_t stripH,
                                      int16_t cardW, int16_t cardH,
                                      int16_t r, uint16_t bgColor,
                                      uint16_t borderColor) {
    if (!canvas || r <= 0) return;
    uint16_t* buf    = canvas->getBuffer();
    int16_t   stride = canvas->width();


    constexpr int16_t MAX_R = 24;
    int16_t borderMin[MAX_R], borderMax[MAX_R];
    int16_t rr = (r > MAX_R) ? MAX_R : r;

    for (int16_t i = 0; i < rr; i++) { borderMin[i] = rr; borderMax[i] = -1; }

    {

        int16_t f     = 1 - rr;
        int16_t ddF_x = 1;
        int16_t ddF_y = -2 * rr;
        int16_t cx    = 0;
        int16_t cy    = rr;

        while (cx < cy) {
            if (f >= 0) { cy--; ddF_y += 2; f += ddF_y; }
            cx++; ddF_x += 2; f += ddF_x;


            int16_t row1 = rr - cx, col1 = rr - cy;
            int16_t row2 = rr - cy, col2 = rr - cx;

            if (row1 >= 0 && row1 < rr) {
                if (col1 < borderMin[row1]) borderMin[row1] = col1;
                if (col1 > borderMax[row1]) borderMax[row1] = col1;
            }
            if (row2 >= 0 && row2 < rr) {
                if (col2 < borderMin[row2]) borderMin[row2] = col2;
                if (col2 > borderMax[row2]) borderMax[row2] = col2;
            }
        }
    }


    for (int16_t row = 0; row < stripH; row++) {
        int16_t   cardY  = stripRow + row;
        uint16_t* rowPtr = buf + (row * stride);


        int16_t bMin = -1, bMax = -1;

        if (cardY < rr) {
            bMin = borderMin[cardY];
            bMax = borderMax[cardY];
        } else if (cardY >= cardH - rr) {
            int16_t mirror = cardH - 1 - cardY;
            bMin = borderMin[mirror];
            bMax = borderMax[mirror];
        }

        if (cardY == 0 || cardY == cardH - 1) {


            for (int16_t x = 0; x < bMin; x++)
                rowPtr[x] = bgColor;
            for (int16_t x = bMin; x < cardW - bMin; x++)
                rowPtr[x] = borderColor;
            for (int16_t x = cardW - bMin; x < cardW; x++)
                rowPtr[x] = bgColor;

        } else if (bMin >= 0) {


            for (int16_t x = 0; x < bMin; x++)
                rowPtr[x] = bgColor;
            for (int16_t x = bMin; x <= bMax; x++)
                rowPtr[x] = borderColor;

            int16_t rBMax = cardW - 1 - bMin;
            int16_t rBMin = cardW - 1 - bMax;
            for (int16_t x = rBMin; x <= rBMax; x++)
                rowPtr[x] = borderColor;
            for (int16_t x = cardW - bMin; x < cardW; x++)
                rowPtr[x] = bgColor;

        } else {

            rowPtr[0]          = borderColor;
            rowPtr[cardW - 1]  = borderColor;
        }
    }
}



void DisplayManager::restoreNormalDashboard() {
    if (!_tft || !_canvasSmall || !_canvasWide) return;
    drawAmbientPanel(_lastRenderedState.ambientTemp,
                     _lastRenderedState.ambientHum,
                     _lastRenderedState.ambientValid);
    drawSlotPanel(_lastRenderedState.slotTemp, _lastRenderedState.slotValid,
                  _lastRenderedState.selectedSlotIdx,
                  _lastRenderedState.slotName, true);
    drawBottomButtons(_lastRenderedState.selectedSlotIdx, true);
}

void DisplayManager::drawInterfaceFixed() {


    _tft->fillScreen(C_BG_MAIN);
}

void DisplayManager::blitCanvas(GFXcanvas16* canvas, int16_t dstX, int16_t dstY, int16_t w, int16_t h) {
    if (!canvas || !_tft) return;

    /*
     * Aplica offset de alinhamento do LCD explicitamente aqui porque a rotina
     * drawRGBBitmap do Adafruit_SPITFT pode devirtualizar (ou inlinar) a chamada
     * interna de setAddrWindow dependendo da versão/toolchain, bypassando o
     * override de TftWithOffset. Aplicamos o offset nas coordenadas de destino
     * e ligamos o flag de bypass no _tft para garantir que, se o override FOR
     * chamado virtualmente, ele não aplique o offset novamente (sem bypass
     * ocorreria offset duplo em bibliotecas em que o dispatch funciona).
     */
    const int8_t ox = _tft->getOffsetX();
    const int8_t oy = _tft->getOffsetY();
    dstX += ox;
    dstY += oy;

    int16_t cw = canvas->width();
    _tft->setOffsetBypass(true);
    if (w == cw) {
        _tft->drawRGBBitmap(dstX, dstY, canvas->getBuffer(), w, h);
    } else {
        uint16_t* buf = canvas->getBuffer();
        for (int16_t row = 0; row < h; row++) {
            _tft->drawRGBBitmap(dstX, dstY + row, buf + (row * cw), w, 1);
        }
    }
    _tft->setOffsetBypass(false);
}

/* ─── F-DISPLAY-ATOMIC: full-screen render via strips de 40px ─────────────
 * Reusa `_canvasWide` (320×45, alocado no boot do Core 1 pro dashboard top
 * bar). Durante full-screen renders (auth/settings/etc), o dashboard não
 * está ativo — canvas está livre pra reuse. Blita só 40 das 45 rows do
 * canvas por strip; 5 rows extras são ignoradas no blit.
 *
 * Sem heap dinâmica = zero risco de OOM/null-buffer crash. Telemetria roda
 * normalmente durante render (free heap intacta). */
GFXcanvas16* DisplayManager::beginScreenRender() {
    if (!_canvasWide) return nullptr;  /* Core 1 não inicializado — improvável durante render */
    _canvasWide->fillScreen(C_BG_MAIN);
    return _canvasWide;
}

void DisplayManager::commitScreenStrip(int16_t stripIdx) {
    if (!_canvasWide || !_tft) return;
    int16_t stripY = stripIdx * RENDER_STRIP_H;
    /* Blita 40 das 45 rows do canvas (5 sobrando ignoradas). */
    blitCanvas(_canvasWide, 0, stripY, 320, RENDER_STRIP_H);
    /* Limpa pra próxima strip ser desenhada do zero. Caller pode overwrite. */
    _canvasWide->fillScreen(C_BG_MAIN);
}

void DisplayManager::endScreenRender() {
    /* No-op: _canvasWide é persistente, não há nada pra liberar.
     * Mantido na API por consistência (caller ainda chama no final). */
}

void DisplayManager::drawTopBar(const SystemState& state) {
    if(!_canvasWide) return;
    const int W = 320, H = 29;
    _canvasWide->fillScreen(C_BG_MAIN);


    _canvasWide->setFont(&simutFont9pt);
    _canvasWide->setTextSize(1);
    _canvasWide->setTextColor(C_ACCENT);
    _canvasWide->setCursor(3, 20);
    _canvasWide->print("SIMUT");


    bool showingSilence = false;
    if (_alarmSilenced && _alarmSilenceEnd > 0) {
        uint32_t now = millis();
        if (now < _alarmSilenceEnd) {
            showingSilence = true;
            uint32_t remaining = (_alarmSilenceEnd - now) / 1000;
            char silBuf[32];
            snprintf(silBuf, sizeof(silBuf), "%s: %lus", tr(TR_SILENCED), (unsigned long)remaining);
            _canvasWide->setFont(&simutFont9pt);
            _canvasWide->setTextColor(RGB565(200, 100, 0));
            _canvasWide->setCursor(75, 20);
            _canvasWide->print(silBuf);
        }
    }


    bool showingNotify = false;
    if (!showingSilence && _webNotifyStartMs > 0) {
        uint32_t elapsed = millis() - _webNotifyStartMs;
        if (elapsed < WEB_NOTIFY_DURATION_MS) {
            showingNotify = true;
            _canvasWide->setFont(&simutFont9pt);
            _canvasWide->setTextColor(C_ACCENT_HIGH);
            _canvasWide->setCursor(75, 20);
            char notifyBuf[32];
            snprintf(notifyBuf, sizeof(notifyBuf), "Web: %s", _webNotifyUser);
            _canvasWide->print(notifyBuf);
        } else {

            _webNotifyStartMs = 0;
            _webNotifyUser[0] = '\0';
        }
    }


    if (!showingSilence && !showingNotify) {
        /*
         * Data e hora centralizadas na área disponível.
         * Formato: "dd/mm/yy - HH:MM"
         * O separador " - " fica fixo no centro; a data cresce para
         * a esquerda e a hora cresce para a direita, garantindo que
         * o texto não pule ao trocar dígitos.
         */
        _canvasWide->setTextSize(1);
        _canvasWide->setFont(&simutFont9pt);
        _canvasWide->setTextColor(C_TITLE_TEXT);

        /* Separar data e hora pelo " - " */
        String fullTime = String(state.timeString);
        int sepIdx = fullTime.indexOf(" - ");
        String datePart = (sepIdx >= 0) ? fullTime.substring(0, sepIdx) : fullTime;
        String timePart = (sepIdx >= 0) ? fullTime.substring(sepIdx + 3) : "";

        /* Medir as 3 partes */
        int16_t bx, by; uint16_t bw, bh;
        uint16_t sepW, dateW, timeW;

        _canvasWide->getTextBounds(" - ", 0, 0, &bx, &by, &bw, &bh);
        sepW = bw;
        _canvasWide->getTextBounds(datePart, 0, 0, &bx, &by, &bw, &bh);
        dateW = bw;
        _canvasWide->getTextBounds(timePart, 0, 0, &bx, &by, &bw, &bh);
        timeW = bw;

        /*
         * Centro do separador fixo no meio do display (x=160).
         * Data cresce para a esquerda, hora para a direita.
         */
        const int centerX = 160;

        int sepX  = centerX - (int)sepW / 2;
        int dateX = sepX - (int)dateW;
        int timeX = sepX + (int)sepW;

        _canvasWide->setCursor(dateX, 20);
        _canvasWide->print(datePart);
        _canvasWide->setTextColor(C_TEXT_SUB);
        _canvasWide->setCursor(sepX, 20);
        _canvasWide->print(" - ");
        _canvasWide->setTextColor(C_TITLE_TEXT);
        _canvasWide->setCursor(timeX, 20);
        _canvasWide->print(timePart);
    }


    int xIcon = 305;

    /* BUG-002: barrier antes de ler _pktArrowState + vars de flash
     * publicadas por Core 0 em setTelemetrySendStatus. */
    __dmb();
    if (state.pendingPkts > 0 || _pktArrowState > 0) {
        /*
         * Cor do NÚMERO: baseada no último resultado de envio.
         *   estado 1 ou 3 → azul (sucesso / flash de sucesso)
         *   estado 2       → vermelho (falha)
         *   estado 0       → azul (idle, nunca enviou)
         */
        uint16_t numColor = (_pktArrowState == 2) ? C_TEMP_HOT : C_ACCENT_HIGH;

        /*
         * Cor da SETA: igual ao número, exceto durante flash (estado 3)
         * onde alterna azul/branco a cada 300ms por 1 segundo.
         */
        uint16_t arrowColor = numColor;

        if (_pktArrowState == 3) {
            uint32_t now = millis();
            if (now >= _pktArrowFlashEnd) {
                _pktArrowState = 1;
                arrowColor = C_ACCENT_HIGH;
            } else {
                if (now - _pktArrowFlashTime >= 300) {
                    _pktArrowFlashOn   = !_pktArrowFlashOn;
                    _pktArrowFlashTime = now;
                }
                arrowColor = _pktArrowFlashOn ? RGB565(255, 255, 255) : C_ACCENT_HIGH;
            }
        }

        if (state.pendingPkts > 0) {
            char pktBuf[10];
            /* >=1000 abrevia como "Nk" para caber na barra superior. */
            if (state.pendingPkts >= 1000) {
                snprintf(pktBuf, sizeof(pktBuf), "%uk", state.pendingPkts / 1000);
            } else {
                snprintf(pktBuf, sizeof(pktBuf), "%u", state.pendingPkts);
            }

            _canvasWide->setFont(&simutFont9pt);

            int16_t tx1, ty1; uint16_t tw, th;
            _canvasWide->getTextBounds(pktBuf, 0, 0, &tx1, &ty1, &tw, &th);

            /*
             * Layout: [número][gapNum][seta][gapWifi][wifi]
             * Seta: 12px. Gap entre número e seta: 4px.
             * Gap entre seta e wifi: 3px.
             * Quando o número é largo (>=3 dígitos), o xIcon recua 1 caractere.
             */
            const int arrowTotalW = 12;
            const int gapToWifi   = 3;
            const int gapNumArrow = 4;
            int effectiveXIcon = xIcon;
            if ((int)tw > 24) effectiveXIcon -= 8;  /* recua para números grandes */

            int arrowRight = effectiveXIcon - gapToWifi;
            int arrowLeft  = arrowRight - arrowTotalW;
            int textX      = arrowLeft - gapNumArrow - (int)tw;

            /* Número — cor fixa baseada no status */
            _canvasWide->setTextColor(numColor);
            _canvasWide->setCursor(textX, 20);
            _canvasWide->print(pktBuf);

            /*
             * Seta para a direita:
             *   - Haste retangular (6×3 px) no centro vertical
             *   - Ponta triangular (6×8 px) à direita
             */
            int ay     = 13;
            int shaftX = arrowLeft;
            int shaftW = 6;
            int tipX   = shaftX + shaftW;
            int tipW   = arrowTotalW - shaftW;

            _canvasWide->fillRect(shaftX, ay - 1, shaftW, 3, arrowColor);
            _canvasWide->fillTriangle(tipX,        ay - 4,
                                      tipX,        ay + 4,
                                      tipX + tipW, ay,
                                      arrowColor);

            /* Reposicionar wifi se necessário */
            if ((int)tw > 24) xIcon = effectiveXIcon;
        }
    }


    int barras = 0;
    if (state.wifiRssi > -100) {
        if      (state.wifiRssi > -55) barras = 4;
        else if (state.wifiRssi > -65) barras = 3;
        else if (state.wifiRssi > -75) barras = 2;
        else                           barras = 1;
    }
    for (int i = 0; i < 4; i++) {
        _canvasWide->fillRect(xIcon + (i * 3), 20 - (4 + (i * 2)), 2, 4 + (i * 2),
                              (i < barras) ? C_TEMP_OK : C_BAR_BG);
    }

    blitCanvas(_canvasWide, 0, 0, W, H);
}

void DisplayManager::drawAmbientPanel(float t, float h, bool isValid) {
    if (!_canvasWide) return;
    int16_t x1, y1; uint16_t w, h_bound;

    bool leftRed  = _alarmAmbientTemp && _alarmFlashPhase && !_alarmSilenced;
    bool rightRed = _alarmAmbientHum  && _alarmFlashPhase && !_alarmSilenced;
    bool isRed    = leftRed || rightRed;

    /* Card do ambiente (painel com dupla moldura) — insetado em 4 px para
     * manter 4 px de margem em cada lado horizontal do display, absorvendo
     * o offset de alinhamento de até ±4H sem perda de borda. A altura e
     * posição vertical ficam inalteradas: o topo em y=35 e a altura de 75
     * já terminam em y=110 (bem dentro de y≤236). */
    static constexpr int16_t CARD_X = 4, CARD_Y = 35;
    static constexpr int16_t CARD_W = 312, CARD_H = 75, CARD_R = 12;

    bool ambAlarm = (_alarmAmbientTemp || _alarmAmbientHum) && _alarmFlashPhase;
    uint16_t borderColor = ambAlarm ? RGB565(255, 60, 60) : C_ACCENT_HIGH;
    uint16_t cardBg = isRed ? RGB565(180, 30, 30) : C_CARD_BG;

    if (_ambientShowMinMax) {
        /* Rastrear transição de modo (sem limpeza prévia — blits cobrem 100%) */
        _ambientLastMinMax = true;

        /* =============================================================
         * MODO MIN/MAX — 3 blits com moldura incorporada
         * Sem maskStripCorners nas strips individuais.
         * ============================================================= */

        uint16_t txtSub  = isRed ? RGB565(220, 200, 200) : C_TEXT_MAIN;
        uint16_t icCol   = isRed ? RGB565(220, 200, 200) : C_TEXT_SUB;
        uint16_t mercCol = isRed ? RGB565(255, 255, 255) : C_TEMP_HOT;
        uint16_t dropCol = isRed ? RGB565(220, 200, 200) : C_HUMIDITY;
        uint16_t humCol  = isRed ? RGB565(255, 255, 255) : C_HUMIDITY;

        /* Posições calculadas dinamicamente */
        _canvasWide->setFont(&simutFont9pt);
        uint16_t minLblW, maxLblW;
        _canvasWide->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &minLblW, &h_bound);
        _canvasWide->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &maxLblW, &h_bound);
        int biggestLbl = ((int)minLblW > (int)maxLblW) ? (int)minLblW : (int)maxLblW;

        const int THERM_X = 8 + biggestLbl + 8;
        const int DOT_X   = THERM_X + 36;
        const int BTN_X   = 268;
        const int BTN_W   = 44;

        uint16_t sufW;
        _canvasWide->getTextBounds(tr(TR_HUM_SUFFIX), 0, 0, &x1, &y1, &sufW, &h_bound);

        /* Posição fixa da gota: pior caso "100" + 3px gap + sufixo, a 8px do botão */
        uint16_t numMaxW;
        _canvasWide->getTextBounds("100", 0, 0, &x1, &y1, &numMaxW, &h_bound);
        int worstNumX = BTN_X - 8 - (int)sufW - 3 - (int)numMaxW;
        const int DROP_FIX = worstNumX - 6;

        /* Blit 1: Título (20px) — com cantos superiores + bordas */
        {
            _canvasWide->fillScreen(cardBg);
            _canvasWide->setFont(&simutFont9pt);
            _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(txtSub);
            _canvasWide->getTextBounds(tr(TR_AMBIENT), 0, 0, &x1, &y1, &w, &h_bound);
            _canvasWide->setCursor((CARD_W - (int)w) / 2, 15);
            _canvasWide->print(tr(TR_AMBIENT));
            maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
        }

        /* Blit 2: Mín + Máx juntas (43px) */
        {
            _canvasWide->fillScreen(cardBg);

            /* ---- Linha Mín (y=0..20) ---- */
            {
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 15);
                _canvasWide->print(tr(TR_MIN_LBL));

                /* Termômetro mini melhorado (escala proporcional do normal) */
                int tx = THERM_X, ty = 0;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);       /* base (contorno) */
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);  /* haste (contorno) */
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, cardBg); /* haste (vazio) */
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, cardBg);      /* base (vazio) */
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);     /* mercúrio (coluna) */
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);     /* mercúrio (bolha) */
                _canvasWide->fillCircle(tx + 4, ty + 2, 2, icCol);        /* topo arredondado */

                uint16_t tCol = isRed ? RGB565(255, 255, 255) : C_TEMP_OK;
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(tCol);
                if (isnan(_ambMinTemp)) {
                    uint16_t dw;
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)dw + 10, 15);
                    _canvasWide->print("--.-");
                } else {
                    char iP[8], dP[4];
                    snprintf(iP, sizeof(iP), "%d", (int)_ambMinTemp);
                    snprintf(dP, sizeof(dP), ".%d", abs((int)(_ambMinTemp * 10) % 10));
                    uint16_t iPw;
                    _canvasWide->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)iPw, 15); _canvasWide->print(iP);
                    _canvasWide->setCursor(DOT_X, 15); _canvasWide->print(dP);
                }
                uint16_t dpW;
                _canvasWide->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &h_bound);
                int endT = DOT_X + (int)dpW + 3;
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(endT, 2); _canvasWide->print("o");
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setCursor(endT + 6, 15); _canvasWide->print("C");

                _canvasWide->setFont(&simutFont9pt);
                char hnum[8];
                if (isnan(_ambMinHum)) snprintf(hnum, sizeof(hnum), "--");
                else                   snprintf(hnum, sizeof(hnum), "%d", (int)_ambMinHum);
                uint16_t hnW;
                _canvasWide->getTextBounds(hnum, 0, 0, &x1, &y1, &hnW, &h_bound);
                /* Posicionar de trás para frente: sufixo termina a 8px do botão */
                int sufX = BTN_X - 8 - (int)sufW;
                int numX = sufX - 3 - (int)hnW;
                _canvasWide->setTextColor(humCol);
                _canvasWide->setCursor(numX, 15);
                _canvasWide->print(hnum);
                _canvasWide->setTextColor(isRed ? RGB565(255, 255, 255) : C_TEXT_MAIN);
                _canvasWide->setCursor(sufX, 15);
                _canvasWide->print(tr(TR_HUM_SUFFIX));
                /* Gota fixa alinhada verticalmente */
                uint16_t shine = isRed ? RGB565(255, 255, 255) : RGB565(200, 230, 255);
                _canvasWide->fillCircle(DROP_FIX + 5, 13, 6, dropCol);
                _canvasWide->fillTriangle(DROP_FIX + 5, 1, DROP_FIX, 11, DROP_FIX + 10, 11, dropCol);
                _canvasWide->fillCircle(DROP_FIX + 3, 11, 2, shine);
                _canvasWide->drawPixel(DROP_FIX + 3, 8, shine);
            }

            /* ---- Linha Máx (y=22..42) ---- */
            {
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 37);
                _canvasWide->print(tr(TR_MAX_LBL));

                /* Termômetro mini melhorado */
                int tx = THERM_X, ty = 22;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, cardBg);
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, cardBg);
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 2, 2, icCol);

                uint16_t tCol = isRed ? RGB565(255, 255, 255) : C_TEMP_OK;
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(tCol);
                if (isnan(_ambMaxTemp)) {
                    uint16_t dw;
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)dw + 10, 37);
                    _canvasWide->print("--.-");
                } else {
                    char iP[8], dP[4];
                    snprintf(iP, sizeof(iP), "%d", (int)_ambMaxTemp);
                    snprintf(dP, sizeof(dP), ".%d", abs((int)(_ambMaxTemp * 10) % 10));
                    uint16_t iPw;
                    _canvasWide->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)iPw, 37); _canvasWide->print(iP);
                    _canvasWide->setCursor(DOT_X, 37); _canvasWide->print(dP);
                }
                uint16_t dpW;
                _canvasWide->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &h_bound);
                int endT = DOT_X + (int)dpW + 3;
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(endT, 24); _canvasWide->print("o");
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setCursor(endT + 6, 37); _canvasWide->print("C");

                _canvasWide->setFont(&simutFont9pt);
                char hnum[8];
                if (isnan(_ambMaxHum)) snprintf(hnum, sizeof(hnum), "--");
                else                   snprintf(hnum, sizeof(hnum), "%d", (int)_ambMaxHum);
                uint16_t hnW;
                _canvasWide->getTextBounds(hnum, 0, 0, &x1, &y1, &hnW, &h_bound);
                int sufX = BTN_X - 8 - (int)sufW;
                int numX = sufX - 3 - (int)hnW;
                _canvasWide->setTextColor(humCol);
                _canvasWide->setCursor(numX, 37);
                _canvasWide->print(hnum);
                _canvasWide->setTextColor(isRed ? RGB565(255, 255, 255) : C_TEXT_MAIN);
                _canvasWide->setCursor(sufX, 37);
                _canvasWide->print(tr(TR_HUM_SUFFIX));
                /* Gota fixa alinhada verticalmente */
                uint16_t shine = isRed ? RGB565(255, 255, 255) : RGB565(200, 230, 255);
                _canvasWide->fillCircle(DROP_FIX + 5, 35, 6, dropCol);
                _canvasWide->fillTriangle(DROP_FIX + 5, 23, DROP_FIX, 33, DROP_FIX + 10, 33, dropCol);
                _canvasWide->fillCircle(DROP_FIX + 3, 33, 2, shine);
                _canvasWide->drawPixel(DROP_FIX + 3, 30, shine);
            }

            /* Botão de gráfico — altura total das duas linhas */
            _canvasWide->fillRoundRect(BTN_X, 1, BTN_W, 42, 8, C_ACCENT);
            {
                int cx = BTN_X + BTN_W / 2;
                int cy = 22;
                /* Barras arredondadas do gráfico */
                _canvasWide->fillRoundRect(cx - 11, cy,     4, 8, 1, C_BG_MAIN);
                _canvasWide->fillRoundRect(cx - 5,  cy - 6, 4, 14, 1, C_BG_MAIN);
                _canvasWide->fillRoundRect(cx + 1,  cy - 3, 4, 11, 1, C_BG_MAIN);
                /* Eixo horizontal */
                _canvasWide->drawFastHLine(cx - 12, cy + 9, 19, C_BG_MAIN);
                /* Seta play */
                _canvasWide->fillTriangle(cx + 10, cy - 5,
                                          cx + 10, cy + 5,
                                          cx + 17, cy, C_BG_MAIN);
            }

            /* Bordas laterais (strip intermediária, sem cantos) */
            {
                uint16_t* buf = _canvasWide->getBuffer();
                int stride = _canvasWide->width();
                for (int row = 0; row < 43; row++) {
                    buf[row * stride] = borderColor;
                    buf[row * stride + CARD_W - 1] = borderColor;
                }
            }

            blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 43);
        }

        /* Blit 3: Fundo inferior (12px) — com cantos inferiores + bordas */
        {
            _canvasWide->fillScreen(cardBg);
            maskStripCorners(_canvasWide, 63, 12, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 63, CARD_W, 12);
        }

    } else {
        /* Rastrear transição de modo */
        _ambientLastMinMax = false;

        /* =============================================================
         * MODO NORMAL — layout com ícones grandes
         * ============================================================= */
        uint16_t leftBg  = leftRed  ? RGB565(180, 30, 30) : C_CARD_BG;
        uint16_t rightBg = rightRed ? RGB565(180, 30, 30) : C_CARD_BG;

        /* Strip 1: Nome centralizado (20px) — mesma posição do min/max */
        {
            _canvasWide->fillRect(0, 0, 160, 20, leftBg);
            _canvasWide->fillRect(160, 0, 160, 20, rightBg);
            _canvasWide->setFont(&simutFont9pt);
            _canvasWide->setTextSize(1);
            uint16_t nameColor = isRed ? RGB565(255, 255, 255) : C_TEXT_MAIN;
            _canvasWide->setTextColor(nameColor);
            _canvasWide->getTextBounds(tr(TR_AMBIENT), 0, 0, &x1, &y1, &w, &h_bound);
            _canvasWide->setCursor((CARD_W - (int)w) / 2, 15);
            _canvasWide->print(tr(TR_AMBIENT));
            maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
        }

        /* Strip 2: Gap para centralizar conteúdo (8px) */
        {
            _canvasWide->fillRect(0, 0, 160, 8, leftBg);
            _canvasWide->fillRect(160, 0, 160, 8, rightBg);
            /* Bordas laterais */
            uint16_t* buf = _canvasWide->getBuffer();
            int stride = _canvasWide->width();
            for (int row = 0; row < 8; row++) {
                buf[row * stride] = borderColor;
                buf[row * stride + CARD_W - 1] = borderColor;
            }
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 8);
        }

        /* Strip 3: Temperatura + Umidade (40px) */
        {
            _canvasWide->fillRect(0, 0, 160, 40, leftBg);
            _canvasWide->fillRect(160, 0, 160, 40, rightBg);

            /* --- Temperatura --- */
            if (!isValid) {
                _canvasWide->setFont(&simutFont12pt);
                _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(leftRed ? RGB565(255,255,255) : C_TEMP_HOT);
                _canvasWide->setCursor(25, 28);
                _canvasWide->print(tr(TR_ERROR_LBL));
            } else {
                _canvasWide->setFont(&simutFont24pt);
                _canvasWide->setTextSize(1);
                uint16_t corT = C_TEMP_OK;
                if (isnan(t)) corT = C_TEXT_OFF;
                if (leftRed) corT = RGB565(255, 255, 255);
                _canvasWide->setTextColor(corT);

                int textAnchor = 92;
                int unitX = 0;
                if (isnan(t)) {
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &w, &h_bound);
                    _canvasWide->setCursor(textAnchor - w, 35);
                    _canvasWide->print("--.-");
                    unitX = textAnchor + 3;
                } else {
                    char intPart[10]; char decPart[5];
                    int fractional = abs((int)(t * 10) % 10);
                    snprintf(intPart, sizeof(intPart), "%d", (int)t);
                    snprintf(decPart, sizeof(decPart), ".%d", fractional);
                    _canvasWide->getTextBounds(intPart, 0, 0, &x1, &y1, &w, &h_bound);
                    int numCursorX = textAnchor - w - 4;
                    _canvasWide->setCursor(numCursorX, 35);
                    _canvasWide->print(intPart);
                    if (t < 0) {
                        int16_t mx1, my1; uint16_t mw, mh;
                        _canvasWide->getTextBounds("-", 0, 0, &mx1, &my1, &mw, &mh);
                        int eraseW = (int)mw / 3;
                        if (eraseW < 2) eraseW = 2;
                        _canvasWide->fillRect(numCursorX, 0, eraseW, 40, leftBg);
                    }
                    _canvasWide->setFont(&simutFont24pt);
                    _canvasWide->setCursor(textAnchor, 35);
                    _canvasWide->print(decPart);
                    uint16_t decW;
                    _canvasWide->getTextBounds(decPart, 0, 0, &x1, &y1, &decW, &h_bound);
                    unitX = textAnchor + (int)decW + 3;
                }
                uint16_t unitCol = leftRed ? RGB565(220, 200, 200) : C_TEXT_MAIN;
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(unitCol);
                _canvasWide->setCursor(unitX, 17);
                _canvasWide->print("o");
                _canvasWide->setFont(&simutFont12pt);
                _canvasWide->setCursor(unitX + 8, 35);
                _canvasWide->print("C");
                /* Ícone de termômetro — por último */
                {
                    uint16_t ic   = leftRed ? RGB565(220, 200, 200) : C_TEXT_SUB;
                    uint16_t merc = leftRed ? RGB565(255, 255, 255) : C_TEMP_HOT;
                    int ix = 14, iy = 4;
                    _canvasWide->fillCircle(ix + 5, iy + 26, 7, ic);
                    _canvasWide->fillRoundRect(ix + 1, iy, 8, 24, 4, ic);
                    _canvasWide->fillRoundRect(ix + 3, iy + 2, 4, 20, 2, leftBg);
                    _canvasWide->fillCircle(ix + 5, iy + 26, 5, leftBg);
                    _canvasWide->fillRect(ix + 4, iy + 10, 2, 14, merc);
                    _canvasWide->fillCircle(ix + 5, iy + 26, 4, merc);
                    _canvasWide->fillCircle(ix + 5, iy + 2, 2, ic);
                }
            }

            /* --- Umidade --- */
            if (!isValid) {
                _canvasWide->setFont(&simutFont12pt);
                _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(rightRed ? RGB565(255,255,255) : C_TEMP_HOT);
                _canvasWide->setCursor(185, 28);
                _canvasWide->print(tr(TR_ERROR_LBL));
            } else {
                _canvasWide->setFont(&simutFont12pt);
                int16_t px1, py1; uint16_t pctW, pctH;
                _canvasWide->getTextBounds(tr(TR_HUM_SUFFIX), 0, 0, &px1, &py1, &pctW, &pctH);
                const int rightMargin = 15;
                int pctX = CARD_W - rightMargin - (int)pctW;
                int humAnchor = pctX - 3;
                _canvasWide->setFont(&simutFont24pt);
                _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(rightRed ? RGB565(255,255,255) : C_HUMIDITY);
                char humBuffer[6];
                if (isnan(h)) snprintf(humBuffer, sizeof(humBuffer), "--");
                else snprintf(humBuffer, sizeof(humBuffer), "%d", (int)h);
                _canvasWide->getTextBounds(humBuffer, 0, 0, &x1, &y1, &w, &h_bound);
                _canvasWide->setCursor(humAnchor - w, 35);
                _canvasWide->print(humBuffer);
                uint16_t pctCol = rightRed ? RGB565(220, 200, 200) : C_TEXT_MAIN;
                _canvasWide->setFont(&simutFont12pt);
                _canvasWide->setTextColor(pctCol);
                _canvasWide->setCursor(pctX, 34);
                _canvasWide->print(tr(TR_HUM_SUFFIX));
                /* Ícone de gota */
                {
                    int dropRight = humAnchor - (int)w - 6;
                    int ix = dropRight - 14;
                    int iy = 4;
                    uint16_t ic    = rightRed ? RGB565(220, 200, 200) : C_HUMIDITY;
                    uint16_t shine = rightRed ? RGB565(255,255,255) : RGB565(200, 230, 255);
                    _canvasWide->fillCircle(ix + 6, iy + 20, 8, ic);
                    _canvasWide->fillTriangle(ix + 6, iy,
                                              ix - 1, iy + 18,
                                              ix + 13, iy + 18, ic);
                    _canvasWide->fillCircle(ix + 4, iy + 17, 3, shine);
                    _canvasWide->fillCircle(ix + 3, iy + 14, 1, shine);
                }
            }
            maskStripCorners(_canvasWide, 28, 40, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 28, CARD_W, 40);
        }

        /* Strip 4: Fundo inferior (7px) */
        {
            uint16_t lBg = leftRed  ? RGB565(180, 30, 30) : C_CARD_BG;
            uint16_t rBg = rightRed ? RGB565(180, 30, 30) : C_CARD_BG;
            _canvasWide->fillRect(0, 0, 160, 7, lBg);
            _canvasWide->fillRect(160, 0, 160, 7, rBg);
            maskStripCorners(_canvasWide, 68, 7, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 68, CARD_W, 7);
        }
    }
}

void DisplayManager::drawSlotPanel(float t, bool isValid, int slotIdx, const char* name, bool forceNameRedraw) {
    if(!_canvasWide) return;
    int16_t x1, y1; uint16_t w, h_bound;


    uint16_t panelBg   = slotAlarmBg(slotIdx);
    bool isRedPhase    = _alarmFlashPhase && isSlotAlarming(slotIdx) && !_alarmSilenced;
    uint16_t nameColor = isRedPhase ? RGB565(255, 255, 255) : C_SENSOR_NAME;
    uint16_t unitColor = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_MAIN;
    if (isSlotAlarming(slotIdx)) forceNameRedraw = true;


    /* Card do slot selecionado (segundo painel com dupla moldura do dashboard),
     * posicionado abaixo do card do ambiente. Mesmo inset horizontal de 4 px
     * para garantir 4 px de margem em cada lado. */
    static constexpr int16_t CARD_X = 4, CARD_Y = 115;
    static constexpr int16_t CARD_W = 312, CARD_H = 75, CARD_R = 12;


    bool slotAlarm = isSlotAlarming(slotIdx) && _alarmFlashPhase;
    uint16_t borderColor = slotAlarm ? RGB565(255, 60, 60) : C_ACCENT_HIGH;

    if (_slotShowMinMax) {
        /* Rastrear transição de modo */
        _slotLastMinMax = true;

        /* =============================================================
         * MODO MIN/MAX — 3 blits com moldura incorporada
         * Slot não tem umidade.
         * ============================================================= */

        uint16_t txtSub  = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_MAIN;
        uint16_t icCol   = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_SUB;
        uint16_t mercCol = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_HOT;

        _canvasWide->setFont(&simutFont9pt);
        uint16_t minLblW, maxLblW;
        _canvasWide->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &minLblW, &h_bound);
        _canvasWide->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &maxLblW, &h_bound);
        int biggestLbl = ((int)minLblW > (int)maxLblW) ? (int)minLblW : (int)maxLblW;

        const int THERM_X = 8 + biggestLbl + 8;
        const int DOT_X   = THERM_X + 36;
        const int BTN_X   = 268;
        const int BTN_W   = 44;

        /* Blit 1: Nome (20px) */
        {
            _canvasWide->fillScreen(panelBg);
            _canvasWide->setFont(&simutFont9pt);
            _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(txtSub);
            const char* displayName = name;
            char buf[16];
            if (strlen(name) == 0) {
                snprintf(buf, 16, "Sensor %d", slotIdx);
                displayName = buf;
            }
            int16_t nx1, ny1; uint16_t nw, nh;
            _canvasWide->getTextBounds(displayName, 0, 0, &nx1, &ny1, &nw, &nh);
            _canvasWide->setCursor((CARD_W - (int)nw) / 2, 15);
            _canvasWide->print(displayName);
            maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
        }

        /* Blit 2: Mín + Máx juntas (43px) */
        {
            _canvasWide->fillScreen(panelBg);

            /* ---- Linha Mín (y=0..20) ---- */
            {
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 15);
                _canvasWide->print(tr(TR_MIN_LBL));

                /* Termômetro mini melhorado */
                int tx = THERM_X, ty = 0;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, panelBg);
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, panelBg);
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 2, 2, icCol);

                uint16_t tCol = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_OK;
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(tCol);
                if (isnan(_slotMinTemp)) {
                    uint16_t dw;
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)dw + 10, 15);
                    _canvasWide->print("--.-");
                } else {
                    char iP[8], dP[4];
                    snprintf(iP, sizeof(iP), "%d", (int)_slotMinTemp);
                    snprintf(dP, sizeof(dP), ".%d", abs((int)(_slotMinTemp * 10) % 10));
                    uint16_t iPw;
                    _canvasWide->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)iPw, 15); _canvasWide->print(iP);
                    _canvasWide->setCursor(DOT_X, 15); _canvasWide->print(dP);
                }
                uint16_t dpW;
                _canvasWide->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &h_bound);
                int endT = DOT_X + (int)dpW + 3;
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(endT, 2); _canvasWide->print("o");
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setCursor(endT + 6, 15); _canvasWide->print("C");
            }

            /* ---- Linha Máx (y=22..42) ---- */
            {
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(8, 37);
                _canvasWide->print(tr(TR_MAX_LBL));

                /* Termômetro mini melhorado */
                int tx = THERM_X, ty = 22;
                _canvasWide->fillCircle(tx + 4, ty + 15, 5, icCol);
                _canvasWide->fillRoundRect(tx + 1, ty, 7, 14, 3, icCol);
                _canvasWide->fillRoundRect(tx + 2, ty + 1, 5, 12, 2, panelBg);
                _canvasWide->fillCircle(tx + 4, ty + 15, 4, panelBg);
                _canvasWide->fillRect(tx + 3, ty + 8, 3, 6, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 15, 3, mercCol);
                _canvasWide->fillCircle(tx + 4, ty + 2, 2, icCol);

                uint16_t tCol = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_OK;
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setTextColor(tCol);
                if (isnan(_slotMaxTemp)) {
                    uint16_t dw;
                    _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &dw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)dw + 10, 37);
                    _canvasWide->print("--.-");
                } else {
                    char iP[8], dP[4];
                    snprintf(iP, sizeof(iP), "%d", (int)_slotMaxTemp);
                    snprintf(dP, sizeof(dP), ".%d", abs((int)(_slotMaxTemp * 10) % 10));
                    uint16_t iPw;
                    _canvasWide->getTextBounds(iP, 0, 0, &x1, &y1, &iPw, &h_bound);
                    _canvasWide->setCursor(DOT_X - (int)iPw, 37); _canvasWide->print(iP);
                    _canvasWide->setCursor(DOT_X, 37); _canvasWide->print(dP);
                }
                uint16_t dpW;
                _canvasWide->getTextBounds(".0", 0, 0, &x1, &y1, &dpW, &h_bound);
                int endT = DOT_X + (int)dpW + 3;
                _canvasWide->setFont(NULL); _canvasWide->setTextSize(1);
                _canvasWide->setTextColor(txtSub);
                _canvasWide->setCursor(endT, 24); _canvasWide->print("o");
                _canvasWide->setFont(&simutFont9pt);
                _canvasWide->setCursor(endT + 6, 37); _canvasWide->print("C");
            }

            /* Botão de gráfico */
            _canvasWide->fillRoundRect(BTN_X, 1, BTN_W, 42, 8, C_ACCENT);
            {
                int cx = BTN_X + BTN_W / 2;
                int cy = 22;
                /* Barras arredondadas do gráfico */
                _canvasWide->fillRoundRect(cx - 11, cy,     4, 8, 1, C_BG_MAIN);
                _canvasWide->fillRoundRect(cx - 5,  cy - 6, 4, 14, 1, C_BG_MAIN);
                _canvasWide->fillRoundRect(cx + 1,  cy - 3, 4, 11, 1, C_BG_MAIN);
                /* Eixo horizontal */
                _canvasWide->drawFastHLine(cx - 12, cy + 9, 19, C_BG_MAIN);
                /* Seta play */
                _canvasWide->fillTriangle(cx + 10, cy - 5,
                                          cx + 10, cy + 5,
                                          cx + 17, cy, C_BG_MAIN);
            }

            /* Bordas laterais (strip intermediária, sem cantos) */
            {
                uint16_t* buf = _canvasWide->getBuffer();
                int stride = _canvasWide->width();
                for (int row = 0; row < 43; row++) {
                    buf[row * stride] = borderColor;
                    buf[row * stride + CARD_W - 1] = borderColor;
                }
            }

            blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 43);
        }

        /* Blit 3: Fundo inferior (12px) — com cantos inferiores + bordas */
        {
            _canvasWide->fillScreen(panelBg);
            maskStripCorners(_canvasWide, 63, 12, CARD_W, CARD_H, CARD_R,
                             C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 63, CARD_W, 12);
        }

    } else {
        /* Forçar redraw do nome na transição min/max → normal */
        if (_slotLastMinMax) forceNameRedraw = true;
        _slotLastMinMax = false;

        /* =============================================================
         * MODO NORMAL — temperatura centralizada com ícone grande
         * ============================================================= */

        if (forceNameRedraw) {
            _canvasWide->fillScreen(panelBg);
            _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(nameColor);
            const char* displayName = name;
            char buf[16];
            if (strlen(name) == 0) {
                snprintf(buf, 16, "Sensor %d", slotIdx);
                displayName = buf;
            }
            int16_t nx1, ny1; uint16_t nw, nh;
            _canvasWide->getTextBounds(displayName, 0, 0, &nx1, &ny1, &nw, &nh);
            _canvasWide->setCursor((CARD_W - (int)nw) / 2, 15);
            _canvasWide->print(displayName);
            maskStripCorners(_canvasWide, 0, 20, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y, CARD_W, 20);
        }

        /* Strip de gap para centralizar conteúdo (8px) */
        {
            _canvasWide->fillScreen(panelBg);
            uint16_t* buf = _canvasWide->getBuffer();
            int stride = _canvasWide->width();
            for (int row = 0; row < 8; row++) {
                buf[row * stride] = borderColor;
                buf[row * stride + CARD_W - 1] = borderColor;
            }
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 20, CARD_W, 8);
        }

        _canvasWide->fillScreen(panelBg);

        if (!isValid) {
            _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextSize(1);
            _canvasWide->setTextColor(isRedPhase ? RGB565(255,255,255) : C_TEMP_HOT);
            int16_t ex1, ey1; uint16_t ew, eh;
            _canvasWide->getTextBounds(tr(TR_ERROR_LBL), 0, 0, &ex1, &ey1, &ew, &eh);
            _canvasWide->setCursor((CARD_W - (int)ew) / 2, 28);
            _canvasWide->print(tr(TR_ERROR_LBL));
        } else {
            const int iconW     = 20;
            const int iconGap   = 8;
            const int unitGap   = 3;
            const int dotGap    = 4;

            _canvasWide->setFont(&simutFont24pt); _canvasWide->setTextSize(1);

            char intPart[10]; char decPart[5];
            bool isNan = isnan(t);
            uint16_t intW = 0, decW = 0;

            if (isNan) {
                _canvasWide->getTextBounds("--.-", 0, 0, &x1, &y1, &intW, &h_bound);
                decW = 0;
            } else {
                int fractional = abs((int)(t * 10) % 10);
                snprintf(intPart, sizeof(intPart), "%d", (int)t);
                snprintf(decPart, sizeof(decPart), ".%d", fractional);
                _canvasWide->getTextBounds(intPart, 0, 0, &x1, &y1, &intW, &h_bound);
                _canvasWide->getTextBounds(decPart, 0, 0, &x1, &y1, &decW, &h_bound);
            }

            _canvasWide->setFont(&simutFont9pt);
            uint16_t degW;
            _canvasWide->getTextBounds("o", 0, 0, &x1, &y1, &degW, &h_bound);
            _canvasWide->setFont(&simutFont12pt);
            uint16_t cW;
            _canvasWide->getTextBounds("C", 0, 0, &x1, &y1, &cW, &h_bound);
            int unitTotalW = (int)degW + 8 + (int)cW;

            int numW = (int)intW + (isNan ? 0 : dotGap + (int)decW);
            int totalW = iconW + iconGap + numW + unitGap + unitTotalW;
            int offsetX = (CARD_W - totalW) / 2;

            int iconX      = offsetX;
            int numAnchorX = iconX + iconW + iconGap + (int)intW;
            int unitX;

            _canvasWide->setFont(&simutFont24pt);
            if (isNan) {
                _canvasWide->setTextColor(isRedPhase ? RGB565(200,180,180) : C_TEXT_OFF);
                _canvasWide->setCursor(iconX + iconW + iconGap, 35);
                _canvasWide->print("--.-");
                unitX = iconX + iconW + iconGap + (int)intW + unitGap;
            } else {
                _canvasWide->setTextColor(isRedPhase ? RGB565(255,255,255) : C_TEMP_OK);
                int numCursorX = numAnchorX - (int)intW;
                _canvasWide->setCursor(numCursorX, 35);
                _canvasWide->print(intPart);
                if (t < 0) {
                    int16_t mx1, my1; uint16_t mw, mh;
                    _canvasWide->getTextBounds("-", 0, 0, &mx1, &my1, &mw, &mh);
                    int eraseW = (int)mw / 3;
                    if (eraseW < 2) eraseW = 2;
                    _canvasWide->fillRect(numCursorX, 0, eraseW, 45, panelBg);
                }
                _canvasWide->setFont(&simutFont24pt);
                _canvasWide->setCursor(numAnchorX + dotGap, 35);
                _canvasWide->print(decPart);
                unitX = numAnchorX + dotGap + (int)decW + unitGap;
            }

            _canvasWide->setFont(&simutFont9pt); _canvasWide->setTextColor(unitColor);
            _canvasWide->setCursor(unitX, 17); _canvasWide->print("o");
            _canvasWide->setFont(&simutFont12pt);
            _canvasWide->setCursor(unitX + 8, 35); _canvasWide->print("C");

            /* Ícone de termômetro — por último */
            {
                uint16_t ic   = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_SUB;
                uint16_t merc = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_HOT;
                int ix = iconX, iy = 4;
                _canvasWide->fillCircle(ix + 10, iy + 26, 7, ic);
                _canvasWide->fillRoundRect(ix + 6, iy, 8, 24, 4, ic);
                _canvasWide->fillRoundRect(ix + 8, iy + 2, 4, 20, 2, panelBg);
                _canvasWide->fillCircle(ix + 10, iy + 26, 5, panelBg);
                _canvasWide->fillRect(ix + 9, iy + 10, 2, 14, merc);
                _canvasWide->fillCircle(ix + 10, iy + 26, 4, merc);
                _canvasWide->fillCircle(ix + 10, iy + 2, 2, ic);
            }
        }

        maskStripCorners(_canvasWide, 28, 40, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
        blitCanvas(_canvasWide, CARD_X, CARD_Y + 28, CARD_W, 40);

        /* Strip 4: Fundo inferior (7px) */
        {
            _canvasWide->fillScreen(panelBg);
            maskStripCorners(_canvasWide, 68, 7, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
            blitCanvas(_canvasWide, CARD_X, CARD_Y + 68, CARD_W, 7);
        }
    }
}

int DisplayManager::buildDashLayout(DashBtn out[5], int *totalPages, bool *hasPaging) {
    /* Constrói layout de 5 slots fixos (esquerda→direita). kind=-1 = vazio.
     * O botão de paginação fica SEMPRE na posição 4 (canto direito) quando
     * existe; slots de uma página parcial deixam gaps em vez de empurrar
     * o page button pra esquerda. */
    for (int i = 0; i < 5; i++) { out[i].kind = -1; out[i].slotId = -1; }

    if (!_sysConfigPtr) return 0;
    SystemConfig &cfg = *_sysConfigPtr;
    DashBtn all[11];
    int total = 0;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].active) {
            all[total].kind = 0;
            all[total].slotId = (int8_t)i;
            total++;
        }
    }
    all[total].kind = 1;        /* CFG sempre presente */
    all[total].slotId = -1;
    total++;

    const int LINE_CAP = 5;
    bool paging = (total > LINE_CAP);
    int perPage = paging ? 4 : LINE_CAP;   /* paging reserva pos 4 pro page btn */
    int pages   = (total + perPage - 1) / perPage;
    if (_currentPage >= pages) _currentPage = 0;   /* clamp pós mudança de config */

    int firstIdx = _currentPage * perPage;
    int lastIdx  = firstIdx + perPage;
    if (lastIdx > total) lastIdx = total;

    int pos = 0;
    for (int i = firstIdx; i < lastIdx; i++) out[pos++] = all[i];
    if (paging) { out[4].kind = 2; out[4].slotId = -1; }   /* sempre posição 4 */

    if (totalPages) *totalPages = pages;
    if (hasPaging)  *hasPaging  = paging;
    return paging ? 5 : pos;
}

void DisplayManager::drawBottomButtons(int selectedIdx, bool forceRedraw) {
    if(!_canvasWide) return;
    _canvasWide->fillScreen(C_BG_MAIN);
    const int btnW = 58, gap = 5, xStart = 5, pitch = btnW + gap;

    DashBtn btns[5];
    int totalPages = 1;
    bool paging = false;
    int n = buildDashLayout(btns, &totalPages, &paging);

    /* Detecta alarmes em slots ATIVOS de outras páginas (pra colorir o page btn) */
    if (!_sysConfigPtr) { blitCanvas(_canvasWide, 0, 195, 320, 41); return; }
    SystemConfig &cfg = *_sysConfigPtr;
    bool hasAlarmsOnOtherPages = false;
    if (paging && _alarmSlotMask != 0) {
        for (int s = 0; s < MAX_SENSORS; s++) {
            if (!cfg.sensors[s].active) continue;
            if (!isSlotAlarming(s)) continue;
            bool inThisPage = false;
            for (int i = 0; i < n; i++) {
                if (btns[i].kind == 0 && btns[i].slotId == s) { inThisPage = true; break; }
            }
            if (!inThisPage) { hasAlarmsOnOtherPages = true; break; }
        }
    }

    for (int i = 0; i < 5; i++) {
        const DashBtn &b = btns[i];
        if (b.kind < 0) continue;   /* gap entre slots e page btn ancorado à direita */
        int x = xStart + (i * pitch);

        if (b.kind == 0) {  /* SLOT */
            int realIdx = b.slotId;
            bool isActive = (realIdx == selectedIdx);
            bool btnAlarm = _alarmFlashPhase && isSlotAlarming(realIdx);
            uint16_t bgColor, txtColor;
            if (btnAlarm) {
                bgColor  = RGB565(180, 30, 30);
                txtColor = RGB565(255, 255, 255);
            } else if (isActive) {
                bgColor  = C_ACCENT_HIGH;
                txtColor = C_BTN_TEXT_ACTIVE;
            } else {
                bgColor  = C_CARD_BG;
                txtColor = isSlotAlarming(realIdx) ? C_TEMP_HOT : C_BTN_TEXT;
            }
            _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, bgColor);
            _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextSize(1); _canvasWide->setTextColor(txtColor);
            char label[8]; snprintf(label, sizeof(label), "S%d", realIdx);
            int16_t x1, y1; uint16_t w, h;
            _canvasWide->getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
            _canvasWide->setCursor(x + (btnW - w)/2, 28);
            _canvasWide->print(label);

        } else if (b.kind == 1) {  /* CFG */
            _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, C_CARD_BG);
            _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextSize(1); _canvasWide->setTextColor(C_BTN_TEXT);
            int16_t x1, y1; uint16_t w, h;
            _canvasWide->getTextBounds("CFG", 0, 0, &x1, &y1, &w, &h);
            _canvasWide->setCursor(x + (btnW - w)/2, 28);
            _canvasWide->print("CFG");

        } else {  /* PAGE */
            uint16_t pagTxtCol = C_BTN_TEXT;
            if (hasAlarmsOnOtherPages && _alarmFlashPhase) {
                _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, RGB565(180, 30, 30));
                pagTxtCol = RGB565(255, 255, 255);
            } else if (hasAlarmsOnOtherPages) {
                _canvasWide->fillRoundRect(x, 0, btnW, 40, 12, C_CARD_BG);
                _canvasWide->drawRoundRect(x, 0, btnW, 40, 12, RGB565(255, 60, 60));
            } else {
                _canvasWide->drawRoundRect(x, 0, btnW, 40, 12, C_TEXT_SUB);
            }
            char pageStr[4]; snprintf(pageStr, sizeof(pageStr), "%d", _currentPage + 1);
            char totStr[4];  snprintf(totStr, sizeof(totStr), "/%d", totalPages);
            _canvasWide->setFont(&simutFont12pt); _canvasWide->setTextColor(pagTxtCol);
            _canvasWide->setCursor(x + 15, 28); _canvasWide->print(pageStr);
            _canvasWide->setFont(NULL); _canvasWide->setCursor(x + 35, 8); _canvasWide->print(totStr);
        }
    }
    /* h=41 em vez de 45 garante 4 px de margem inferior (y+h=236 ≤ 236). */
    blitCanvas(_canvasWide, 0, 195, 320, 41);
}
