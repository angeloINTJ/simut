/**
 * @file    DisplayManager_Graph.cpp
 * @brief   Graph rendering: showStats/showGraphPlot + draw* screens.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *          Inclui: dual Y-axis plot, decimação, peak markers, header bar
 *          alternada (nome/data), tela numérica detalhada, loading screen
 *          + period buttons (1H/6H/12H/24H/7D), formatGraphTime helper.
 *
 * @project SIMUT
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "DisplayManager_FmtFloat.h"

void DisplayManager::showStats(const GraphDataPackage& data, float minHum, float maxHum) {
    mutex_enter_blocking(&_stateMutex);
    _graphData = data; _currentMinHum = minHum; _currentMaxHum = maxHum;
    _uiMode = MODE_STATS_VIEW;
    __dmb();
    _repaintGraph = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::showGraphPlot(const GraphDataPackage& data, float minHum, float maxHum) {
    mutex_enter_blocking(&_stateMutex);
    _graphData = data; _currentMinHum = minHum; _currentMaxHum = maxHum;
    _uiMode = MODE_GRAPH_VIEW;
    _headerShowName = false;
    _headerNameTimer = 0;
    __dmb();
    _repaintGraph = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::drawLoadingScreen() {

    _tft->fillScreen(C_BG_MAIN);
    _tft->setFont(&simutFont12pt);
    _tft->setTextColor(C_TEXT_MAIN);
    int16_t x1, y1; uint16_t w, h;
    String t1 = tr(TR_LOADING);
    _tft->getTextBounds(t1, 0, 0, &x1, &y1, &w, &h);
    _tft->setCursor(160 - (w/2), 127);
    _tft->print(t1);
    _loadingDrawn = true;
}


void __not_in_flash_func(DisplayManager::drawWebBusyOverlay)() {


    _tft->fillScreen(C_BG_MAIN);
    _tft->setFont(&simutFont12pt);
    char userLine[32];
    mutex_enter_blocking(&_stateMutex);
    snprintf(userLine, sizeof(userLine), "'%s'", _webBusyUser);
    mutex_exit(&_stateMutex);
    int16_t x1, y1; uint16_t w, h;

    _tft->setTextColor(C_TEXT_MAIN);
    _tft->getTextBounds(userLine, 0, 0, &x1, &y1, &w, &h);
    _tft->setCursor(160 - (w / 2), 100);
    _tft->print(userLine);

    const char* line2 = "Acessando via Web.";
    _tft->setTextColor(C_TEXT_SUB);
    _tft->getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
    _tft->setCursor(160 - (w / 2), 130);
    _tft->print(line2);

    const char* line3 = "Aguarde...";
    _tft->getTextBounds(line3, 0, 0, &x1, &y1, &w, &h);
    _tft->setCursor(160 - (w / 2), 160);
    _tft->print(line3);
    _webOverlayShown = true;
}

void DisplayManager::requestLoadingScreen() {
    _loadingDrawn = false;
    _repaintLoading = true;
    _uiMode = MODE_GRAPH_LOADING;
}

void DisplayManager::drawPeriodButtons() {
    if (!_canvasWide) return;

    /*
     * Layout: 5 botões com ícones pixel-art (60×40 cada, gap=4)
     *   [◀ Past] [▶ Future] [📅 Cal] [🔍+ ZoomIn] [🔍- ZoomOut]
     * Total: 5×60 + 4×4 = 316px, startX=2.
     */
    const int btnW = 60, btnH = 40, btnR = 12, gap = 4, startX = 2;
    const char* ranges[] = {"1H", "6H", "12H", "24H", "7D"};

    bool canFwd     = (_graphNavOffset < 0);
    bool canZoomIn  = (_graphData.timeRange > 0);   /* 0=1H é max zoom   */
    bool canZoomOut = (_graphData.timeRange < 4);   /* 4=7D é min zoom   */

    GFXcanvas16* cv = _canvasWide;
    cv->fillScreen(C_BG_MAIN);

    /* Helper: desenha fundo do botão e retorna X */
    auto btnBase = [&](int idx, bool enabled) -> int {
        int x = startX + idx * (btnW + gap);
        uint16_t bg = enabled ? C_CARD_BG : C_BG_MAIN;
        cv->fillRoundRect(x, 0, btnW, btnH, btnR, bg);
        if (!enabled) cv->drawRoundRect(x, 0, btnW, btnH, btnR, C_TEXT_OFF);
        return x;
    };

    int cx, cy;

    /* ════ 0: Passado (◀◀) ════ */
    {
        int x = btnBase(0, true);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = C_ACCENT_HIGH;

        /* Chevron duplo esquerdo */
        cv->drawLine(cx + 2, cy - 7, cx - 5, cy, ic);
        cv->drawLine(cx - 5, cy, cx + 2, cy + 7, ic);
        cv->drawLine(cx + 3, cy - 7, cx - 4, cy, ic);
        cv->drawLine(cx - 4, cy, cx + 3, cy + 7, ic);

        cv->drawLine(cx + 8, cy - 7, cx + 1, cy, ic);
        cv->drawLine(cx + 1, cy, cx + 8, cy + 7, ic);
        cv->drawLine(cx + 9, cy - 7, cx + 2, cy, ic);
        cv->drawLine(cx + 2, cy, cx + 9, cy + 7, ic);
    }

    /* ════ 1: Futuro (▶▶) ════ */
    {
        int x = btnBase(1, canFwd);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = canFwd ? C_ACCENT_HIGH : C_TEXT_OFF;

        /* Chevron duplo direito */
        cv->drawLine(cx - 8, cy - 7, cx - 1, cy, ic);
        cv->drawLine(cx - 1, cy, cx - 8, cy + 7, ic);
        cv->drawLine(cx - 9, cy - 7, cx - 2, cy, ic);
        cv->drawLine(cx - 2, cy, cx - 9, cy + 7, ic);

        cv->drawLine(cx - 2, cy - 7, cx + 5, cy, ic);
        cv->drawLine(cx + 5, cy, cx - 2, cy + 7, ic);
        cv->drawLine(cx - 3, cy - 7, cx + 4, cy, ic);
        cv->drawLine(cx + 4, cy, cx - 3, cy + 7, ic);
    }

    /* ════ 2: Calendário (📅) ════ */
    {
        int x = btnBase(2, true);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = C_ACCENT;
        int gx = cx - 8, gy = cy - 8;

        /* Corpo do calendário 16×16 */
        cv->drawRoundRect(gx, gy + 2, 16, 14, 2, ic);

        /* Barra de título preenchida */
        cv->fillRect(gx + 1, gy + 3, 14, 4, ic);

        /* Alças superiores */
        cv->drawFastVLine(gx + 4,  gy, 4, ic);
        cv->drawFastVLine(gx + 11, gy, 4, ic);

        /* Grade interna: 3 colunas × 2 linhas de pontos */
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 3; c++) {
                cv->fillRect(gx + 2 + c * 5, gy + 9 + r * 4, 3, 2, ic);
            }
        }
    }

    /* ════ 3: Zoom In (🔍+) ════ */
    {
        int x = btnBase(3, canZoomIn);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = canZoomIn ? C_TEMP_OK : C_TEXT_OFF;

        /* Lupa */
        int lx = cx - 3, ly = cy - 3, lr = 8;
        cv->drawCircle(lx, ly, lr, ic);
        cv->drawCircle(lx, ly, lr - 1, ic);

        /* Haste diagonal */
        cv->drawLine(lx + 6, ly + 5, lx + 11, ly + 10, ic);
        cv->drawLine(lx + 5, ly + 6, lx + 10, ly + 11, ic);

        /* Símbolo + */
        cv->drawFastHLine(lx - 4, ly, 9, ic);
        cv->drawFastVLine(lx, ly - 4, 9, ic);

        /* Label do próximo range (zoom in = range-1) */
        if (canZoomIn) {
            cv->setFont(NULL); cv->setTextSize(1);
            cv->setTextColor(ic);
            const char* lbl = ranges[_graphData.timeRange - 1];
            int lblW = strlen(lbl) * 6;  /* NULL font: 6px/char */
            cv->setCursor(x + (btnW - lblW) / 2, btnH - 9);
            cv->print(lbl);
        }
    }

    /* ════ 4: Zoom Out (🔍−) ════ */
    {
        int x = btnBase(4, canZoomOut);
        cx = x + btnW / 2; cy = btnH / 2;
        uint16_t ic = canZoomOut ? C_TEMP_WARM : C_TEXT_OFF;

        /* Lupa (mesmo formato) */
        int lx = cx - 3, ly = cy - 3, lr = 8;
        cv->drawCircle(lx, ly, lr, ic);
        cv->drawCircle(lx, ly, lr - 1, ic);

        /* Haste */
        cv->drawLine(lx + 6, ly + 5, lx + 11, ly + 10, ic);
        cv->drawLine(lx + 5, ly + 6, lx + 10, ly + 11, ic);

        /* Símbolo − */
        cv->drawFastHLine(lx - 4, ly, 9, ic);

        /* Label do próximo range (zoom out = range+1) */
        if (canZoomOut) {
            cv->setFont(NULL); cv->setTextSize(1);
            cv->setTextColor(ic);
            const char* lbl = ranges[_graphData.timeRange + 1];
            int lblW = strlen(lbl) * 6;
            cv->setCursor(x + (btnW - lblW) / 2, btnH - 9);
            cv->print(lbl);
        }
    }

    /* y=195 + h=btnH; evita chegar a y=240 (ver nota acima). Se btnH for 45
     * (altura do rodapé padrão), limita a 41. */
    int16_t footerH = (btnH > 41) ? 41 : (int16_t)btnH;
    blitCanvas(_canvasWide, 0, 195, 320, footerH);
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*                   HEADER DO GRÁFICO (alternância nome/data)               */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief  Desenha apenas a barra superior (28px) do gráfico.
 *
 * Alterna a cada 3 segundos entre:
 *   - Nome do sensor (ex: "Ambiente")
 *   - Intervalo de datas/horas do gráfico (ex: "06/04 14:00 - 15:00")
 *
 * Chamada pelo strip rendering no sTop==0 e pelo timer periódico no Core 1.
 * Blita diretamente em y=0, sem repintar o corpo do gráfico.
 */
void DisplayManager::drawGraphHeaderBar(bool blitNow) {
    if (!_canvasWide) return;

    GFXcanvas16* cv = _canvasWide;
    /* Safe zone superior (canvas y=0..3) em BG + header propriamente dito em
     * canvas y=4..31. Com este layout, callers standalone e callers de dentro
     * de strip-render (cujo blit externo copia canvas y=0..44 → display
     * y=0..44) produzem exatamente o mesmo resultado visual no display:
     * header em y=4..31 com 4 px de safe zone acima. */
    cv->fillRect(0, 0, 320, 4,  C_BG_MAIN);
    cv->fillRect(0, 4, 320, 28, C_CARD_BG);
    cv->setFont(&simutFont9pt);

    /* ── Pill do range atual no canto esquerdo ── */
    int contentStartX = 4;
    {
        const char* ranges[] = {"1H", "6H", "12H", "24H", "7D"};
        const char* rLabel = ranges[_graphData.timeRange];
        int16_t rx, ry; uint16_t rw, rh;
        cv->getTextBounds(rLabel, 0, 0, &rx, &ry, &rw, &rh);
        int pillW = rw + 12;
        cv->fillRoundRect(4, 8, pillW, 20, 8, C_ACCENT);
        cv->setTextColor(C_BG_MAIN);
        cv->setCursor(10 - rx, 23);
        cv->print(rLabel);
        contentStartX = 4 + pillW + 4;  /* Espaço após o pill */
    }

    /* Área útil para texto central: contentStartX .. 280 */
    int centerZone = 280 - contentStartX;

    if (_headerShowName) {
        /* ── Toque no header: nome do sensor por 3 segundos ── */
        cv->setTextColor(C_TEXT_MAIN);
        int16_t bx, by; uint16_t bw, bh;
        cv->getTextBounds(_graphData.title, 0, 0, &bx, &by, &bw, &bh);
        int tx = contentStartX + (centerZone - (int)bw) / 2 - bx;
        if (tx < contentStartX) tx = contentStartX;
        cv->setCursor(tx, 24);
        cv->print(_graphData.title);
    } else if (_graphData.tsCutoff > 0 && _graphData.tsEnd > 0) {
        /*
         * Intervalo de datas centralizado.
         * Mostra a janela temporal completa (tsCutoff..tsEnd),
         * não apenas o range dos dados disponíveis.
         */
        char dateBuf[32];
        struct tm tmFirst, tmLast;
        localtime_r(&_graphData.tsCutoff, &tmFirst);
        localtime_r(&_graphData.tsEnd,    &tmLast);

        bool sameDay = (tmFirst.tm_mday == tmLast.tm_mday
                     && tmFirst.tm_mon  == tmLast.tm_mon);

        if (sameDay) {
            snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d  %02d:%02d - %02d:%02d",
                     tmFirst.tm_mday, tmFirst.tm_mon + 1,
                     tmFirst.tm_hour, tmFirst.tm_min,
                     tmLast.tm_hour,  tmLast.tm_min);
        } else {
            snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d %02d:%02d - %02d/%02d %02d:%02d",
                     tmFirst.tm_mday, tmFirst.tm_mon + 1,
                     tmFirst.tm_hour, tmFirst.tm_min,
                     tmLast.tm_mday,  tmLast.tm_mon + 1,
                     tmLast.tm_hour,  tmLast.tm_min);
        }

        uint16_t dateColor = (_graphData.count >= 2) ? C_ACCENT_HIGH : C_TEXT_SUB;
        cv->setTextColor(dateColor);
        int16_t bx, by; uint16_t bw, bh;
        cv->getTextBounds(dateBuf, 0, 0, &bx, &by, &bw, &bh);
        int tx = contentStartX + (centerZone - (int)bw) / 2 - bx;
        if (tx < contentStartX) tx = contentStartX;
        cv->setCursor(tx, 24);
        cv->print(dateBuf);
    } else {
        /* Sem dados e sem timestamps de referência */
        cv->setTextColor(C_TEXT_SUB);
        cv->setCursor(contentStartX, 24);
        cv->print(_graphData.title);
    }

    /* Botão X (fechar) no canto superior direito (284, 6, 32, 24).
     * x+w=316 fica dentro da safe zone direita de 4 px. */
    cv->fillRoundRect(284, 6, 32, 24, 6, C_TEMP_WARM);
    cv->setFont(&simutFont9pt);
    cv->setTextColor(C_BG_MAIN);
    cv->setCursor(293, 23);
    cv->print("X");

    /* Blit do bloco inteiro (safe zone + header) direto pra display 1:1.
     * Suprimido em strip-render: o blit externo do strip cobre essa região. */
    if (blitNow) blitCanvas(cv, 0, 0, 320, 32);
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*                     TELA DE CALENDÁRIO DE HISTÓRICO                       */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief  Desenha o calendário mensal com indicadores de dias com dados.
 *
 * Layout (320×240):
 *   Header (0..27):  [◀ Mês]  "Abr 2026"  [Mês ▶]
 *   Grid (28..194):  Cabeçalho D S T Q Q S S + grade 6×7
 *   Bottom (195..239): [◀ Mês] [Hoje] [Mês ▶]
 *
 * Dias com dados recebem bolinha azul (C_ACCENT).
 * Dia atual destacado com fundo semitransparente.
 * Toque num dia com dados envia EVT_CALENDAR_DAY.
 */

void DisplayManager::drawGraphIcon(int16_t x, int16_t y, uint16_t color) {
    _tft->fillRect(x,      y + 12, 6, 10, color);
    _tft->fillRect(x + 8,  y + 4,  6, 18, color);
    _tft->fillRect(x + 16, y + 8,  6, 14, color);
    _tft->drawLine(x, y+2, x+22, y+2, color);
}

void DisplayManager::drawStatsScreen() {
    int16_t x1, y1; uint16_t w, h_bound;

    /* Header via canvas — aparece instantâneo */
    if (_canvasWide) {
        GFXcanvas16* cv = _canvasWide;
        cv->fillScreen(C_BG_MAIN);
        cv->fillRect(4, 4, 312, 32, C_CARD_BG);
        cv->setFont(&simutFont9pt); cv->setTextColor(C_TEXT_MAIN);
        cv->setCursor(14, 23); cv->print(_graphData.title);
        cv->fillRoundRect(280, 4, 36, 24, 6, C_TEMP_WARM);
        cv->setFont(&simutFont9pt); cv->setTextColor(C_BG_MAIN);
        cv->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
        cv->setCursor(298 - w / 2, 23); cv->print("X");
        blitCanvas(cv, 0, 0, 320, 45);
    } else {
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(14, 23); _tft->print(_graphData.title);
        _tft->fillRoundRect(280, 4, 36, 24, 6, C_TEMP_WARM);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_BG_MAIN);
        _tft->getTextBounds("X", 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(298 - w / 2, 23); _tft->print("X");
    }

    /* Limpa zona abaixo do header/canvas (y=45..235) — 4px de margem inferior */
    _tft->fillRect(4, 45, 312, 191, C_BG_MAIN);


    _tft->setFont(NULL); _tft->setTextSize(1); _tft->setTextColor(C_TEXT_SUB);
    _tft->setCursor(14, 38); _tft->print("ID: "); _tft->print(_graphData.hwId);
    _tft->setCursor(14, 49); _tft->print("SN: "); _tft->print(_graphData.rom);


    auto drawTemp = [&](float val, int anchorX, int y, uint16_t color, bool large) {
        int16_t bx1, by1; uint16_t bw, bh;
        int symbolX = anchorX + (large ? 38 : 28);
        _tft->setTextColor(color);

        if (large) _tft->setFont(&simutFont24pt);
        else       _tft->setFont(&simutFont12pt);

        if (isnan(val)) {
            _tft->getTextBounds("--.-", 0, 0, &bx1, &by1, &bw, &bh);
            _tft->setCursor(anchorX - bw, y); _tft->print("--.-");
        } else {
            char iPart[8], dPart[4];
            snprintf(iPart, sizeof(iPart), "%d", (int)val);
            snprintf(dPart, sizeof(dPart), ".%d", abs((int)(val * 10) % 10));
            _tft->getTextBounds(iPart, 0, 0, &bx1, &by1, &bw, &bh);
            _tft->setCursor(anchorX - bw - 2, y); _tft->print(iPart);
            _tft->setCursor(anchorX, y);           _tft->print(dPart);
        }


        if (large) {
            _tft->setFont(&simutFont9pt);  _tft->setCursor(symbolX, y - 18); _tft->print("o");
            _tft->setFont(&simutFont12pt); _tft->setCursor(symbolX + 8, y);  _tft->print("C");
        } else {
            _tft->setFont(NULL);                _tft->setCursor(symbolX, y - 12); _tft->print("o");
            _tft->setFont(&simutFont9pt);  _tft->setCursor(symbolX + 7, y);  _tft->print("C");
        }
    };


    auto drawHum = [&](float val, int anchorX, int y, uint16_t color) {
        int16_t bx1, by1; uint16_t bw, bh;
        char buf[6];
        if (isnan(val)) snprintf(buf, sizeof(buf), "--");
        else            snprintf(buf, sizeof(buf), "%d", (int)val);

        _tft->setFont(&simutFont12pt); _tft->setTextColor(color);
        _tft->getTextBounds(buf, 0, 0, &bx1, &by1, &bw, &bh);
        _tft->setCursor(anchorX - bw, y); _tft->print(buf);
        _tft->setTextColor(C_TEXT_SUB); _tft->setCursor(anchorX + 4, y); _tft->print("%");
    };


    if (_graphData.hasHumidity && !isnan(_currentMinHum)) {
        const int cardW = 148, cardH = 96, cardR = 12;
        const int cardY = 62;
        const int leftX = 5, rightX = 167;


        _tft->fillRoundRect(leftX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(leftX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEMP_HOT);
        _tft->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(leftX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MAX_LBL));


        drawTemp(_graphData.realMaxVal, leftX + 68, cardY + 52, C_TEMP_HOT, false);


        _tft->fillCircle(leftX + 25, cardY + 74, 3, C_HUMIDITY);
        drawHum(_currentMaxHum, leftX + 80, cardY + 80, C_HUMIDITY);


        _tft->fillRoundRect(rightX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(rightX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEMP_OK);
        _tft->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(rightX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MIN_LBL));

        drawTemp(_graphData.realMinVal, rightX + 68, cardY + 52, C_TEMP_OK, false);

        _tft->fillCircle(rightX + 25, cardY + 74, 3, C_HUMIDITY);
        drawHum(_currentMinHum, rightX + 80, cardY + 80, C_HUMIDITY);
    }


    else {
        const int cardW = 148, cardH = 96, cardR = 12;
        const int cardY = 62;
        const int leftX = 5, rightX = 167;


        _tft->fillRoundRect(leftX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(leftX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEMP_HOT);
        _tft->getTextBounds(tr(TR_MAX_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(leftX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MAX_LBL));

        drawTemp(_graphData.realMaxVal, leftX + 55, cardY + 68, C_TEMP_HOT, true);


        _tft->fillRoundRect(rightX, cardY, cardW, cardH, cardR, C_CARD_BG);
        _tft->drawRoundRect(rightX, cardY, cardW, cardH, cardR, C_ACCENT_HIGH);

        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEMP_OK);
        _tft->getTextBounds(tr(TR_MIN_LBL), 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(rightX + (cardW - w) / 2, cardY + 18); _tft->print(tr(TR_MIN_LBL));

        drawTemp(_graphData.realMinVal, rightX + 55, cardY + 68, C_TEMP_OK, true);
    }


    {
        const char* rangeLabels[] = {"1h", "6h", "24h", "3d", "7d"};
        const char* rangeText = ((_graphData.timeRange >= 0) && (_graphData.timeRange < 5))
                                ? rangeLabels[_graphData.timeRange] : "?";
        char periodBuf[16];
        snprintf(periodBuf, sizeof(periodBuf), "[ %s ]", rangeText);
        _tft->setFont(NULL); _tft->setTextSize(1); _tft->setTextColor(C_TEXT_OFF);
        _tft->getTextBounds(periodBuf, 0, 0, &x1, &y1, &w, &h_bound);
        _tft->setCursor(160 - w / 2, 168); _tft->print(periodBuf);
    }


    _tft->fillRoundRect(10, 180, 300, 40, 12, C_ACCENT);


    int icX = 50, icY = 188;
    _tft->fillRect(icX,      icY + 8, 4, 12, C_BG_MAIN);
    _tft->fillRect(icX + 6,  icY + 2, 4, 18, C_BG_MAIN);
    _tft->fillRect(icX + 12, icY + 6, 4, 14, C_BG_MAIN);
    _tft->drawFastHLine(icX - 2, icY + 20, 20, C_BG_MAIN);


    _tft->setFont(&simutFont12pt); _tft->setTextColor(C_BG_MAIN);
    String btnTxt = tr(TR_PLOT_CHART);
    _tft->getTextBounds(btnTxt, 0, 0, &x1, &y1, &w, &h_bound);
    _tft->setCursor(160 - (w / 2) + 15, 207);
    _tft->print(btnTxt);
}


/* =========================================================================== */
/*               HELPERS PARA GRÁFICO MELHORADO                              */
/* =========================================================================== */

/**
 * @brief Formata float com 1 casa decimal em buffer, sem usar snprintf %f.
 *
 * O snprintf com %f no newlib-nano (RP2040) consome ~400 bytes de stack
 * internamente para conversão float→string, causando stack overflow no
 * Core 1 que tem apenas ~2KB de stack.
 * Esta função usa apenas aritmética inteira — zero consumo de stack extra.
 *
 * @param buf   Buffer de saída (mínimo 10 bytes).
 * @param size  Tamanho do buffer.
 * @param val   Valor float a formatar.
 * @return      Ponteiro para buf (para encadear).
 */

/**
 * @brief Formata float com 2 casas decimais em buffer, sem usar snprintf %f.
 * @param buf   Buffer de saída (mínimo 12 bytes).
 * @param size  Tamanho do buffer.
 * @param val   Valor float a formatar.
 * @return      Ponteiro para buf.
 */

/**
 * @brief Formata timestamp para labels do eixo X do gráfico.
 *
 * Para ranges curtos (1H, 6H, 12H) mostra apenas HH:MM.
 * Para ranges longos (24H, 7D) mostra DD/MM HHh.
 *
 * @param epoch      Timestamp Unix do ponto.
 * @param buf        Buffer de saída (mínimo 12 bytes).
 * @param shortRange true para formato curto (HH:MM), false para longo (DD/MM HHh).
 */
void DisplayManager::formatGraphTime(time_t epoch, char* buf, bool shortRange) {
    struct tm ti;
    localtime_r(&epoch, &ti);
    /* Sempre exibe data e hora completas, independente do intervalo */
    (void)shortRange;
    snprintf(buf, 12, "%02d/%02d %02d:%02d", ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min);
}

/**
 * @brief Desenha marcador de diamante (losango) com label de valor flutuante.
 *
 * O diamante tem ~4px de raio. O label é posicionado acima ou abaixo
 * conforme o parâmetro 'above', com flip automático se ultrapassar
 * os limites verticais da área do gráfico.
 *
 * @param cx        Coordenada X do centro do diamante.
 * @param cy        Coordenada Y do centro do diamante.
 * @param color     Cor do marcador e do label.
 * @param value     Valor numérico para exibir no label.
 * @param above     true = label acima do ponto, false = abaixo.
 * @param unit      Sufixo da unidade (ex: "C", "%").
 * @param graphTop  Limite superior da área do gráfico (clip).
 * @param graphBot  Limite inferior da área do gráfico (clip).
 */
void DisplayManager::drawPeakMarker(int16_t cx, int16_t cy, uint16_t color,
                                     float value, bool above, const char* unit,
                                     int16_t graphTop, int16_t graphBot) {
    /* Clamp vertical para não sair da área do gráfico */
    if (cy < graphTop + 3) cy = graphTop + 3;
    if (cy > graphBot - 3) cy = graphBot - 3;

    /* Diamante preenchido (losango 4px de raio) */
    const int r = 4;
    for (int dy = -r; dy <= r; dy++) {
        int span = r - abs(dy);
        _tft->drawFastHLine(cx - span, cy + dy, span * 2 + 1, color);
    }
    /* Pixel central para contraste */
    _tft->drawPixel(cx, cy, C_BG_MAIN);

    /* Formata label de valor */
    static char valBuf[16];
    static char fBuf[10];
    fmtFloat1(fBuf, sizeof(fBuf), value);
    snprintf(valBuf, sizeof(valBuf), "%s%s", fBuf, unit);

    _tft->setFont(NULL);
    _tft->setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    _tft->getTextBounds(valBuf, 0, 0, &bx, &by, &bw, &bh);

    /* Posicionamento vertical com flip automático */
    int16_t labelX = cx - (int16_t)(bw / 2);
    int16_t labelY;
    if (above) {
        labelY = cy - r - (int16_t)bh - 3;
        if (labelY < graphTop) labelY = cy + r + 3;    /* Flip para baixo */
    } else {
        labelY = cy + r + 3;
        if (labelY + (int16_t)bh > graphBot) labelY = cy - r - (int16_t)bh - 3; /* Flip para cima */
    }

    /* Clamp horizontal para não sair da tela */
    if (labelX < 2) labelX = 2;
    if (labelX + (int16_t)bw > 318) labelX = 318 - (int16_t)bw;

    /* Fundo opaco para legibilidade sobre a curva */
    _tft->fillRect(labelX - 1, labelY - 1, bw + 2, bh + 2, C_BG_MAIN);
    _tft->setTextColor(color);
    _tft->setCursor(labelX, labelY);
    _tft->print(valBuf);
}


/* =========================================================================== */
/*                    TELA DE GRÁFICO DE HISTÓRICO (MELHORADA)               */
/* =========================================================================== */
/**
 * @brief Desenha a tela completa do gráfico de histórico com melhorias visuais.
 *
 * Melhorias sobre a versão anterior:
 * - Barra de info superior com badges MAX/MIN contendo valor + horário
 * - Se houver umidade, badges H.MAX e H.MIN adicionais
 * - Eixo X com 3 labels de tempo (início, meio, fim do período)
 * - Formato adaptativo: HH:MM para ≤12H, DD/MM HHh para 24H e 7D
 * - Marcadores de diamante nos pontos de pico e vale da curva
 * - Labels flutuantes nos extremos com flip automático se fora da área
 * - Eixo Y com 5 divisões e valores decimais
 * - Eixo Y da umidade com valor intermediário (topo, meio, base)
 * - Linha do gráfico com 2px de espessura
 */
void DisplayManager::drawGraphScreen() {
    __dmb();
    if (!_canvasWide) return;

    if (_graphData.count < 0 || _graphData.count > GRAPH_WIDTH) {
        _tft->fillScreen(C_BG_MAIN);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_SUB);
        _tft->setCursor(60, 120); _tft->print(tr(TR_ERROR_LBL));
        drawPeriodButtons();
        return;
    }

    bool shortRange = (_graphData.timeRange <= 3); /* 1H..24H = HH:MM, 7D = DD/MM */
    bool hasHum = _graphData.hasHumidity && !isnan(_currentMinHum);
    bool hasData = (_graphData.count >= 2 && _graphData.idxMaxTemp >= 0);

    /*
     * Layout maximizado: gráfico ocupa header(28)..botões(195).
     * Margem Y interna de 2px — curva toca quase as bordas.
     * Labels Y: apenas MAX (topo) e MIN (base).
     */
    const int gx = 30;                  /* Margem esquerda (labels Y)   */
    const int gy = 30;                  /* Topo da grade                */
    const int gw = hasHum ? 250 : 285;  /* Largura da grade             */
    const int gh = 155;                 /* Altura da grade              */
    const int margin = 2;               /* Folga interna do gráfico     */
    const int timeAxisY = gy + gh + 2;  /* Labels eixo X                */

    float tempRange = 2.0f;
    float humMin = 0, humMax = 100, humRange = 5.0f;

    /*
     * Escala Y: usa realMinVal/realMaxVal calculados de TODOS os registros
     * na janela temporal, não apenas dos pontos decimados para exibição.
     * Garante que o eixo Y represente os valores extremos verdadeiros.
     */
    if (hasData) {
        tempRange = _graphData.realMaxVal - _graphData.realMinVal;
        if (tempRange < 0.001f) tempRange = 1.0f; /* Valor constante → linha no meio */
        if (hasHum) {
            humMin = _currentMinHum; humMax = _currentMaxHum;
            humRange = humMax - humMin;
            if (humRange < 0.001f) humRange = 1.0f;
        }
    }

    /* ── Pré-calcular coordenadas da curva ── */
    static int16_t pxV1[GRAPH_WIDTH], pyV1[GRAPH_WIDTH], pyV2[GRAPH_WIDTH];
    if (hasData) {
        /*
         * Posição X por índice: dados sempre preenchem toda a largura da grade.
         * Posição Y por realMinVal/realMaxVal: escala real de todos os registros.
         * Header e labels X usam tsCutoff/tsEnd para mostrar a janela temporal.
         */
        for (int i = 0; i < _graphData.count; i++) {
            pxV1[i] = gx + (int)((long)i * gw / max(1, _graphData.count - 1));

            /* Pontos NAN (sensor em erro) → pyV1 = -1 para criar buraco visível */
            if (isnan(_graphData.pointsV1[i])) {
                pyV1[i] = -1;
            } else {
                int y = gy + margin + (int)((_graphData.realMaxVal - _graphData.pointsV1[i]) / tempRange * (gh - 2 * margin));
                if (y < gy) y = gy; if (y > gy + gh) y = gy + gh;
                pyV1[i] = y;
            }

            if (hasHum && !isnan(_graphData.pointsV2[i])) {
                int yh = gy + margin + (int)((humMax - _graphData.pointsV2[i]) / humRange * (gh - 2 * margin));
                if (yh < gy) yh = gy; if (yh > gy + gh) yh = gy + gh;
                pyV2[i] = yh;
            } else {
                pyV2[i] = -1;
            }
        }
    }

    /* ── Pré-formatar textos ── */
    static char maxLbl[10], minLbl[10];
    static char humMaxLbl[8], humMinLbl[8];
    static char tBuf[12];

    if (hasData) {
        fmtFloat1(maxLbl, sizeof(maxLbl), _graphData.realMaxVal);
        fmtFloat1(minLbl, sizeof(minLbl), _graphData.realMinVal);
        if (hasHum) {
            snprintf(humMaxLbl, sizeof(humMaxLbl), "%d%%", (int)humMax);
            snprintf(humMinLbl, sizeof(humMinLbl), "%d%%", (int)humMin);
        }
    }

    /* ═══════════════════════════════════════════════════════════════ */
    /*  STRIP RENDERING: tudo no canvas 320×45                       */
    /* ═══════════════════════════════════════════════════════════════ */
    GFXcanvas16* cv = _canvasWide;
    const int sH = 45;

    for (int s = 0; s * sH < 195; s++) {
        int sTop = s * sH;
        int h = sH;
        if (sTop + h > 195) h = 195 - sTop;
        int sBot = sTop + h;

        cv->fillScreen(C_BG_MAIN);

        if (hasData) {
            /* ── Eixos ── */
            if (gy < sBot && gy + gh > sTop) {
                int at = (gy > sTop) ? gy - sTop : 0;
                int ab = (gy + gh < sBot) ? gy + gh - sTop : h;
                cv->drawFastVLine(gx, at, ab - at, C_AXIS);
                if (gy + gh >= sTop && gy + gh < sBot)
                    cv->drawFastHLine(gx, gy + gh - sTop, gw, C_AXIS);
                if (hasHum)
                    cv->drawFastVLine(gx + gw, at, ab - at, C_AXIS);
            }

            /* ── Grade pontilhada horizontal (4 divisões) ── */
            for (int gi = 0; gi <= 4; gi++) {
                int lineY = gy + (gh * gi / 4);
                if (lineY >= sTop && lineY < sBot) {
                    int ry = lineY - sTop;
                    for (int x = gx + 2; x < gx + gw; x += 6) {
                        cv->drawPixel(x, ry, C_GRID);
                        cv->drawPixel(x + 1, ry, C_GRID);
                    }
                }
            }

            /* ── Grade vertical ── */
            if (gy < sBot && gy + gh > sTop) {
                int gt = (gy > sTop) ? gy - sTop : 0;
                int gb = (gy + gh < sBot) ? gy + gh - sTop : h;
                for (int x = gx; x < gx + gw; x += 40)
                    cv->drawFastVLine(x, gt, gb - gt, C_GRID);
            }

            /* ── Labels eixo Y: MAX alinhado ao topo, MIN à base da grade ── */
            cv->setFont(NULL); cv->setTextSize(1);
            int lyMax = gy;              /* Topo da grade = pico do gráfico */
            int lyMin = gy + gh - 8;     /* Base da grade = vale do gráfico */
            /* Condição de interseção: label visível se qualquer parte cruza a strip */
            if (lyMax < sBot && lyMax + 8 > sTop) {
                cv->setTextColor(C_TEMP_HOT);
                cv->setCursor(1, lyMax - sTop);
                cv->print(maxLbl);
            }
            if (lyMin < sBot && lyMin + 8 > sTop) {
                cv->setTextColor(C_TEMP_OK);
                cv->setCursor(1, lyMin - sTop);
                cv->print(minLbl);
            }

            /* ── Labels eixo Y umidade (lado direito) ── */
            if (hasHum) {
                int rxAxis = gx + gw;
                cv->setTextColor(C_HUMIDITY);
                if (lyMax < sBot && lyMax + 8 > sTop) {
                    cv->setCursor(rxAxis + 3, lyMax - sTop);
                    cv->print(humMaxLbl);
                }
                if (lyMin < sBot && lyMin + 8 > sTop) {
                    cv->setCursor(rxAxis + 3, lyMin - sTop);
                    cv->print(humMinLbl);
                }
            }

            /* ── Curva de temperatura (2px) — pula buracos (pyV1 == -1) ── */
            for (int i = 0; i < _graphData.count - 1; i++) {
                if (pyV1[i] < 0 || pyV1[i + 1] < 0) continue;  /* Buraco: sensor em erro */
                int y1 = pyV1[i], y2 = pyV1[i + 1];
                int yMn = (y1 < y2) ? y1 : y2;
                int yMx = (y1 > y2) ? y1 : y2;
                if (yMx < sTop || yMn >= sBot) continue;
                cv->drawLine(pxV1[i], y1 - sTop, pxV1[i+1], y2 - sTop, C_TEMP_HOT);
                cv->drawLine(pxV1[i], y1 - sTop + 1, pxV1[i+1], y2 - sTop + 1, C_TEMP_HOT);
            }

            /* ── Curva de umidade (1px) ── */
            if (hasHum) {
                for (int i = 0; i < _graphData.count - 1; i++) {
                    if (pyV2[i] < 0 || pyV2[i+1] < 0) continue;
                    int y1 = pyV2[i], y2 = pyV2[i + 1];
                    int yMn = (y1 < y2) ? y1 : y2;
                    int yMx = (y1 > y2) ? y1 : y2;
                    if (yMx < sTop || yMn >= sBot) continue;
                    cv->drawLine(pxV1[i], y1 - sTop, pxV1[i+1], y2 - sTop, C_HUMIDITY);
                }
            }

            /* ── Marcador último valor válido ── */
            {
                /* Busca o último ponto válido (não-NAN) para o marcador */
                int lastValidIdx = -1;
                for (int i = _graphData.count - 1; i >= 0; i--) {
                    if (pyV1[i] >= 0) { lastValidIdx = i; break; }
                }
                if (lastValidIdx >= 0) {
                    int ly = pyV1[lastValidIdx];
                    if (ly - 3 < sBot && ly + 3 >= sTop) {
                        cv->fillCircle(gx + gw, ly - sTop, 3, C_TEXT_MAIN);
                        cv->fillCircle(gx + gw, ly - sTop, 1, C_BG_MAIN);
                    }
                }
            }

            /* ── Marcadores pico/vale (diamante) — pula se ponto é NAN ── */
            auto drawDiamond = [&](int dx, int dy, uint16_t color) {
                if (dy < 0) return;  /* Ponto NAN: sem marcador */
                if (dy - 3 >= sBot || dy + 3 < sTop) return;
                int ry = dy - sTop;
                for (int dd = -3; dd <= 3; dd++) {
                    int span = 3 - abs(dd);
                    if (ry + dd >= 0 && ry + dd < h)
                        cv->drawFastHLine(dx - span, ry + dd, span * 2 + 1, color);
                }
                if (ry >= 0 && ry < h) cv->drawPixel(dx, ry, C_BG_MAIN);
            };

            if (_graphData.idxMaxTemp >= 0 && _graphData.idxMaxTemp < _graphData.count)
                drawDiamond(pxV1[_graphData.idxMaxTemp], pyV1[_graphData.idxMaxTemp], C_TEMP_HOT);
            if (_graphData.idxMinTemp >= 0 && _graphData.idxMinTemp < _graphData.count)
                drawDiamond(pxV1[_graphData.idxMinTemp], pyV1[_graphData.idxMinTemp], C_TEMP_OK);

        } else {
            /* Sem dados */
            if (120 >= sTop && 130 < sBot) {
                cv->setFont(&simutFont12pt); cv->setTextColor(C_TEXT_SUB);
                String nd = tr(TR_NO_DATA);
                int16_t nbx, nby; uint16_t nw, nh;
                cv->getTextBounds(nd, 0, 0, &nbx, &nby, &nw, &nh);
                cv->setCursor(160 - nw / 2, 125 - sTop); cv->print(nd);
            }
        }

        /*
         * Labels eixo X (3 timestamps) — correspondem aos extremos da linha
         * desenhada (tsFirst..tsLast). O header mostra a janela completa
         * (tsCutoff..tsEnd) para contexto do zoom.
         */
        if (timeAxisY < sBot && timeAxisY + 8 > sTop && _graphData.tsFirst > 0) {
            int ry = timeAxisY - sTop;
            cv->setFont(NULL); cv->setTextSize(1); cv->setTextColor(C_TEXT_SUB);

            static char xL[6], xM[6], xR[6];
            struct tm ti;

            time_t tMid = _graphData.tsFirst + (_graphData.tsLast - _graphData.tsFirst) / 2;

            /* Primeiro ponto */
            localtime_r(&_graphData.tsFirst, &ti);
            if (shortRange) snprintf(xL, sizeof(xL), "%02d:%02d", ti.tm_hour, ti.tm_min);
            else            snprintf(xL, sizeof(xL), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
            cv->setCursor(gx, ry); cv->print(xL);

            /* Ponto médio */
            localtime_r(&tMid, &ti);
            if (shortRange) snprintf(xM, sizeof(xM), "%02d:%02d", ti.tm_hour, ti.tm_min);
            else            snprintf(xM, sizeof(xM), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
            int16_t tbx, tby; uint16_t tw, th;
            cv->getTextBounds(xM, 0, 0, &tbx, &tby, &tw, &th);
            cv->setCursor(gx + gw / 2 - (int)tw / 2, ry); cv->print(xM);

            /* Último ponto */
            localtime_r(&_graphData.tsLast, &ti);
            if (shortRange) snprintf(xR, sizeof(xR), "%02d:%02d", ti.tm_hour, ti.tm_min);
            else            snprintf(xR, sizeof(xR), "%02d/%02d", ti.tm_mday, ti.tm_mon + 1);
            cv->getTextBounds(xR, 0, 0, &tbx, &tby, &tw, &th);
            cv->setCursor(gx + gw - (int)tw, ry); cv->print(xR);
        }

        /* Header desenhado por ÚLTIMO no strip 0 — seu fillRect interno
         * sobrescreve qualquer bleed de eixos/grid em canvas y=4..31. Blit
         * externo do strip cobrirá a região inteira. */
        if (sTop == 0) {
            drawGraphHeaderBar(/*blitNow=*/false);
        }

        blitCanvas(cv, 0, sTop, 320, h);
    }

    drawPeriodButtons();
}


/* =========================================================================== */
/*              TELA NUMÉRICA DE DETALHES DO PERÍODO                         */
/* =========================================================================== */
/**
 * @brief Desenha tela com dados numéricos legíveis do período selecionado.
 *
 * Exibe em cards grandes: MAX, MIN, AVG, σ, e período.
 * Mantém header com título/botão X e botões de período na base.
 * Toque na zona central retorna ao gráfico.
 * Todos os floats formatados via fmtFloat1/fmtFloat2 (sem snprintf %f).
 */
void DisplayManager::drawGraphDetailScreen() {
    __dmb();
    if (!_canvasWide) return;

    bool shortRange = (_graphData.timeRange <= 3); /* 1H..24H = HH:MM, 7D = DD/MM */
    bool hasHum = _graphData.hasHumidity && !isnan(_currentMinHum);
    bool isHumPage = (_detailPage == 1 && hasHum);
    uint16_t pageColor = isHumPage ? C_HUMIDITY : C_TEMP_OK;

    int16_t bx, by; uint16_t bw, bh;

    struct CardData {
        const char* label;
        char num[14];
        bool isTempUnit;  /* true = oC com circulozinho, false = tr(TR_HUM_SUFFIX) */
        char sub[12];
        uint16_t numColor;
        int icon;
    };
    static CardData cards[4];

    if (_graphData.count < 2) {
        _tft->fillRect(4, 4, 312, 191, C_BG_MAIN);
        drawGraphHeaderBar();  /* Mostra período de referência no header */
        _tft->setFont(&simutFont12pt); _tft->setTextColor(C_TEXT_SUB);
        String nd = tr(TR_NO_DATA);
        _tft->getTextBounds(nd, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(160 - bw / 2, 120); _tft->print(nd);
        drawPeriodButtons();
        return;
    }

    /* ── Popular cards ── */
    if (!isHumPage) {
        cards[0] = { tr(TR_MAX_LBL), {0}, true, {0}, C_TEMP_OK, 0 };
        fmtFloat1(cards[0].num, sizeof(cards[0].num), _graphData.realMaxVal);
        if (_graphData.tsRealMax > 0) formatGraphTime(_graphData.tsRealMax, cards[0].sub, shortRange);

        cards[1] = { tr(TR_MIN_LBL), {0}, true, {0}, C_TEMP_OK, 1 };
        fmtFloat1(cards[1].num, sizeof(cards[1].num), _graphData.realMinVal);
        if (_graphData.tsRealMin > 0) formatGraphTime(_graphData.tsRealMin, cards[1].sub, shortRange);

        cards[2] = { tr(TR_AVG_LBL), {0}, true, {0}, C_TEMP_OK, 2 };
        fmtFloat1(cards[2].num, sizeof(cards[2].num), _graphData.avgTemp);

        cards[3] = { tr(TR_STD_LBL), {0}, true, {0}, C_TEMP_OK, 3 };
        fmtFloat2(cards[3].num, sizeof(cards[3].num), _graphData.stdTemp);
    } else {
        cards[0] = { tr(TR_MAX_LBL), {0}, false, {0}, C_HUMIDITY, 0 };
        snprintf(cards[0].num, sizeof(cards[0].num), "%d", (int)_currentMaxHum);
        if (_graphData.tsMaxHum > 0) formatGraphTime(_graphData.tsMaxHum, cards[0].sub, shortRange);

        cards[1] = { tr(TR_MIN_LBL), {0}, false, {0}, C_HUMIDITY, 1 };
        snprintf(cards[1].num, sizeof(cards[1].num), "%d", (int)_currentMinHum);
        if (_graphData.tsMinHum > 0) formatGraphTime(_graphData.tsMinHum, cards[1].sub, shortRange);

        cards[2] = { tr(TR_AVG_LBL), {0}, false, {0}, C_HUMIDITY, 2 };
        if (!isnan(_graphData.avgHum)) snprintf(cards[2].num, sizeof(cards[2].num), "%d", (int)_graphData.avgHum);
        else snprintf(cards[2].num, sizeof(cards[2].num), "--");

        cards[3] = { tr(TR_STD_LBL), {0}, false, {0}, C_HUMIDITY, 3 };
        if (!isnan(_graphData.stdHum)) fmtFloat2(cards[3].num, sizeof(cards[3].num), _graphData.stdHum);
        else snprintf(cards[3].num, sizeof(cards[3].num), "--");
    }

    /* ── Layout: 2 linhas × 2 colunas, cards maiores ── */
    const int cardW = 152, cardH = 76, cardR = 8;
    const int colL = 4, colR = 164, gapY = 4;
    const int totalH = 2 * cardH + gapY;
    const int startY = 28 + (167 - totalH) / 2;
    int rowY[2] = { startY, startY + cardH + gapY };

    /**
     * Desenha card no canvas (versão expandida).
     * - Ícone 18×18 refinado
     * - Label em FreeSansBold9pt7b, cor C_TEXT_SUB (cinza claro)
     * - Valor grande colorido (verde temp / azul hum)
     * - Unidade: temp = circulozinho "o" (NULL font) + "C" (9pt) branco
     *            hum  = tr(TR_HUM_SUFFIX) (9pt) branco
     * - Data/hora do evento na base do card (FreeSansBold9pt7b, amarelo suave)
     */
    auto drawCardOn = [&](GFXcanvas16* cv, int cx, int cy, int stripTop, int idx) {
        int ry = cy - stripTop;
        CardData& d = cards[idx];

        cv->fillRoundRect(cx, ry, cardW, cardH, cardR, C_CARD_BG);

        /* Amarelo suave para data/hora dos eventos */
        const uint16_t C_DATETIME = RGB565(190, 170, 60);

        /* ── Ícone 18×18 ── */
        int ix = cx + 6, iy = ry + 2;
        uint16_t ic = d.numColor;
        switch (d.icon) {
            case 0: { /* ▲ MAX — triângulo ascendente com contorno interno */
                cv->fillTriangle(ix, iy+16, ix+9, iy+1, ix+17, iy+16, ic);
                cv->drawTriangle(ix+2, iy+15, ix+9, iy+4, ix+15, iy+15, C_CARD_BG);
                break;
            }
            case 1: { /* ▼ MIN — triângulo descendente com contorno interno */
                cv->fillTriangle(ix, iy+1, ix+9, iy+16, ix+17, iy+1, ic);
                cv->drawTriangle(ix+2, iy+2, ix+9, iy+13, ix+15, iy+2, C_CARD_BG);
                break;
            }
            case 2: { /* ≈ MEDIA — três barras horizontais proporcionais */
                cv->fillRect(ix, iy+1,  17, 3, ic);
                cv->fillRect(ix, iy+7,  17, 3, ic);
                cv->fillRect(ix, iy+13, 17, 3, ic);
                break;
            }
            case 3: { /* σ DESVIO — curva sino refinada 18×18 */
                /* Topo da curva */
                cv->drawPixel(ix+8, iy+1, ic); cv->drawPixel(ix+9, iy+1, ic);
                cv->drawPixel(ix+7, iy+2, ic); cv->drawPixel(ix+10, iy+2, ic);
                cv->drawPixel(ix+6, iy+3, ic); cv->drawPixel(ix+11, iy+3, ic);
                /* Ombros */
                cv->drawPixel(ix+5, iy+4, ic); cv->drawPixel(ix+12, iy+4, ic);
                cv->drawPixel(ix+5, iy+5, ic); cv->drawPixel(ix+12, iy+5, ic);
                cv->drawPixel(ix+4, iy+6, ic); cv->drawPixel(ix+13, iy+6, ic);
                cv->drawPixel(ix+4, iy+7, ic); cv->drawPixel(ix+13, iy+7, ic);
                /* Corpo */
                cv->drawPixel(ix+3, iy+8, ic);  cv->drawPixel(ix+14, iy+8, ic);
                cv->drawPixel(ix+3, iy+9, ic);  cv->drawPixel(ix+14, iy+9, ic);
                cv->drawPixel(ix+2, iy+10, ic); cv->drawPixel(ix+15, iy+10, ic);
                cv->drawPixel(ix+2, iy+11, ic); cv->drawPixel(ix+15, iy+11, ic);
                /* Base larga */
                cv->drawPixel(ix+1, iy+12, ic); cv->drawPixel(ix+16, iy+12, ic);
                cv->drawPixel(ix+1, iy+13, ic); cv->drawPixel(ix+16, iy+13, ic);
                cv->drawPixel(ix,   iy+14, ic); cv->drawPixel(ix+17, iy+14, ic);
                /* Linha de base sólida */
                cv->fillRect(ix, iy+15, 18, 2, ic);
                break;
            }
        }

        /* ── Label (FreeSansBold9pt7b, cinza claro, à direita do ícone) ── */
        cv->setFont(&simutFont9pt);
        cv->setTextColor(C_TEXT_SUB);
        cv->setCursor(ix + 22, iy + 14);
        cv->print(d.label);

        /* ── Valor + Unidade (centro vertical do card) ── */
        int vy = ry + 48;

        /* Medir largura do número */
        int16_t nb, ny2; uint16_t nw, nh;
        cv->setFont(&simutFont12pt);
        cv->getTextBounds(d.num, 0, 0, &nb, &ny2, &nw, &nh);

        if (d.isTempUnit) {
            /*
             * Temperatura: número + "o" (circulozinho, NULL font acima) + "C" (9pt)
             * Padrão idêntico à tela principal (drawTemp).
             */
            int16_t ub2, uy3; uint16_t cw2, ch2;
            cv->setFont(&simutFont9pt);
            cv->getTextBounds("C", 0, 0, &ub2, &uy3, &cw2, &ch2);

            /* "o" em NULL font é ~6px wide */
            int unitW = 6 + 1 + (int)cw2; /* "o" + gap + "C" */
            int totalW = (int)nw + 2 + unitW;
            int vx = cx + (cardW - totalW) / 2;

            /* Número (colorido) */
            cv->setFont(&simutFont12pt);
            cv->setTextColor(d.numColor);
            cv->setCursor(vx, vy);
            cv->print(d.num);

            /* Circulozinho "o" (NULL font, branco, posicionado acima do baseline) */
            int oX = vx + (int)nw + 2;
            cv->setFont(NULL); cv->setTextSize(1);
            cv->setTextColor(C_TEXT_MAIN);
            cv->setCursor(oX, vy - 16);
            cv->print("o");

            /* "C" (FreeSansBold9pt7b, branco) */
            cv->setFont(&simutFont9pt);
            cv->setTextColor(C_TEXT_MAIN);
            cv->setCursor(oX + 7, vy);
            cv->print("C");

        } else {
            /*
             * Umidade: número + tr(TR_HUM_SUFFIX) (9pt, branco)
             */
            int16_t ub2, uy3; uint16_t uw2, uh2;
            cv->setFont(&simutFont9pt);
            cv->getTextBounds(tr(TR_HUM_SUFFIX), 0, 0, &ub2, &uy3, &uw2, &uh2);

            int totalW = (int)nw + 3 + (int)uw2;
            int vx = cx + (cardW - totalW) / 2;

            /* Número (colorido) */
            cv->setFont(&simutFont12pt);
            cv->setTextColor(d.numColor);
            cv->setCursor(vx, vy);
            cv->print(d.num);

            /* Sufixo de umidade (branco) */
            cv->setFont(&simutFont9pt);
            cv->setTextColor(C_TEXT_MAIN);
            cv->setCursor(vx + (int)nw + 3, vy);
            cv->print(tr(TR_HUM_SUFFIX));
        }

        /* ── Data/hora do evento (base do card, centralizado, amarelo suave) ── */
        if (d.sub[0]) {
            int16_t sx, sy; uint16_t sw, sh;
            cv->setFont(&simutFont9pt);
            cv->setTextColor(C_DATETIME);
            cv->getTextBounds(d.sub, 0, 0, &sx, &sy, &sw, &sh);
            cv->setCursor(cx + (cardW - (int)sw) / 2, ry + cardH - 6);
            cv->print(d.sub);
        }
    };

    /* ═══════════════════════════════════════════════════════════════ */
    /*  STRIP RENDERING                                              */
    /* ═══════════════════════════════════════════════════════════════ */
    GFXcanvas16* cv = _canvasWide;
    const int sH = 45;

    for (int s = 0; s * sH < 195; s++) {
        int sTop = s * sH;
        int h = sH;
        if (sTop + h > 195) h = 195 - sTop;

        cv->fillScreen(C_BG_MAIN);

        /* Apenas 2 linhas de cards */
        for (int r = 0; r < 2; r++) {
            int cy = rowY[r];
            if (cy < sTop + h && cy + cardH > sTop) {
                drawCardOn(cv, colL, cy, sTop, r * 2);
                drawCardOn(cv, colR, cy, sTop, r * 2 + 1);
            }
        }

        /* Header desenhado por ÚLTIMO no strip 0 — sobreescreve qualquer
         * pixel de card que porventura entre na zona do header. */
        if (sTop == 0) {
            drawGraphHeaderBar(/*blitNow=*/false);
        }

        blitCanvas(cv, 0, sTop, 320, h);
    }

    drawPeriodButtons();
}



