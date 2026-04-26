/**
 * @file    DisplayManager_Calendar.cpp
 * @brief   Calendar screen: state setters + drawCalendarScreen.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *
 * @project SIMUT
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"

void DisplayManager::showCalendar(int year, int month, uint32_t daysMask) {
    mutex_enter_blocking(&_stateMutex);
    _calYear = year;
    _calMonth = month;
    _calDaysMask = daysMask;
    _uiMode = MODE_CALENDAR;
    __dmb();
    _repaintCalendar = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setCalendarDays(uint32_t daysMask) {
    mutex_enter_blocking(&_stateMutex);
    _calDaysMask = daysMask;
    _repaintCalendar = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setGraphNavOffset(int offset) {
    mutex_enter_blocking(&_stateMutex);
    _graphNavOffset = offset;
    mutex_exit(&_stateMutex);
}


void DisplayManager::drawCalendarScreen() {
    if (!_canvasWide) return;

    const char* monthNames[] = {
        "Jan","Fev","Mar","Abr","Mai","Jun",
        "Jul","Ago","Set","Out","Nov","Dez"
    };
    const char* dowHeaders[] = {"D","S","T","Q","Q","S","S"};

    /* Calcula primeiro dia da semana e total de dias */
    struct tm firstTm = {};
    firstTm.tm_year = _calYear - 1900;
    firstTm.tm_mon  = _calMonth - 1;
    firstTm.tm_mday = 1;
    mktime(&firstTm);
    int firstDow = firstTm.tm_wday;  /* 0 = Domingo */

    /* Dias no mês */
    struct tm lastTm = {};
    lastTm.tm_year = _calYear - 1900;
    lastTm.tm_mon  = _calMonth;  /* mês seguinte */
    lastTm.tm_mday = 0;          /* dia 0 do mês seguinte = último do atual */
    mktime(&lastTm);
    int daysInMonth = lastTm.tm_mday;

    /* Dia de hoje (para highlight) */
    time_t now = time(nullptr);
    struct tm nowTm;
    localtime_r(&now, &nowTm);
    int todayDay = (nowTm.tm_year + 1900 == _calYear &&
                    nowTm.tm_mon + 1 == _calMonth) ? nowTm.tm_mday : -1;

    /* ═══ STRIP RENDERING ═══ */
    GFXcanvas16* cv = _canvasWide;
    const int sH = 45;

    for (int s = 0; s * sH < 195; s++) {
        int sTop = s * sH;
        int h = sH;
        if (sTop + h > 195) h = 195 - sTop;

        cv->fillScreen(C_BG_MAIN);

        /* ── Header (y=0..27) ── */
        if (sTop == 0) {
            cv->fillRect(4, 4, 312, 28, C_CARD_BG);

            /* Botão ◀ mês */
            cv->setFont(&simutFont12pt);
            cv->setTextColor(C_ACCENT_HIGH);
            cv->setCursor(8, 22);
            cv->print("<");

            /* Título "Abr 2026" */
            char titleBuf[16];
            snprintf(titleBuf, sizeof(titleBuf), "%s %d",
                     monthNames[_calMonth - 1], _calYear);
            cv->setFont(&simutFont9pt);
            cv->setTextColor(C_TEXT_MAIN);
            int16_t bx, by; uint16_t bw, bh;
            cv->getTextBounds(titleBuf, 0, 0, &bx, &by, &bw, &bh);
            cv->setCursor(160 - bw / 2 - bx, 20);
            cv->print(titleBuf);

            /* Botão ▶ mês */
            cv->setFont(&simutFont12pt);
            cv->setTextColor(C_ACCENT_HIGH);
            cv->setCursor(298, 22);
            cv->print(">");

            /* Botão X (voltar ao gráfico) — y=4 garante 4 px de margem no topo */
            cv->fillRoundRect(270, 4, 24, 24, 6, C_TEMP_WARM);
            cv->setFont(&simutFont9pt);
            cv->setTextColor(C_BG_MAIN);
            cv->setCursor(277, 21);
            cv->print("X");
        }

        /* ── Cabeçalho dos dias da semana (y=30..42) ── */
        if (sTop <= 30 && sTop + h > 30) {
            int ry = 34 - sTop;
            cv->setFont(NULL);
            cv->setTextSize(1);
            cv->setTextColor(C_TEXT_OFF);
            for (int d = 0; d < 7; d++) {
                cv->setCursor(10 + d * 44, ry);
                cv->print(dowHeaders[d]);
            }
        }

        /* ── Grade de dias (y=44..190) ── */
        const int gridStartY = 46;
        const int cellW = 44, cellH = 24;

        for (int cell = 0; cell < 42; cell++) {
            int dayNum = cell - firstDow + 1;
            if (dayNum < 1 || dayNum > daysInMonth) continue;

            int row = cell / 7;
            int col = cell % 7;
            int cx = 10 + col * cellW + cellW / 2;
            int cy = gridStartY + row * cellH + cellH / 2;

            /* Verifica se este dia está na strip atual */
            if (cy - 8 >= sTop + h || cy + 12 < sTop) continue;

            int ry = cy - sTop;  /* Coordenada relativa ao canvas */

            bool hasData = (_calDaysMask & (1UL << dayNum)) != 0;
            bool isToday = (dayNum == todayDay);

            /* Highlight de hoje */
            if (isToday) {
                cv->fillRoundRect(cx - 16, ry - 9, 32, 20, 5, C_ACCENT);
                cv->setTextColor(C_BG_MAIN);
            } else {
                cv->setTextColor(hasData ? C_TEXT_MAIN : C_TEXT_OFF);
            }

            /* Número do dia */
            cv->setFont(&simutFont9pt);
            cv->setTextSize(1);
            char dayStr[4];
            snprintf(dayStr, sizeof(dayStr), "%d", dayNum);
            int16_t bx, by; uint16_t bw, bh;
            cv->getTextBounds(dayStr, 0, 0, &bx, &by, &bw, &bh);
            cv->setCursor(cx - bw / 2 - bx, ry + bh / 2 - by - bh);
            cv->print(dayStr);

            /* Bolinha de indicador de dados */
            if (hasData && !isToday) {
                cv->fillCircle(cx, ry + 10, 2, C_ACCENT);
            }
        }

        blitCanvas(cv, 0, sTop, 320, h);
    }

    /* ── Bottom bar: [◀ Mês] [Hoje] [Mês ▶] ── */
    cv->fillScreen(C_BG_MAIN);

    /* Botão ◀ Mês */
    cv->fillRoundRect(5, 3, 98, 36, 10, C_CARD_BG);
    cv->setFont(&simutFont9pt);
    cv->setTextColor(C_TEXT_SUB);
    cv->setCursor(20, 26);
    cv->print("< Mes");

    /* Botão Hoje */
    cv->fillRoundRect(108, 3, 104, 36, 10, C_ACCENT);
    cv->setTextColor(C_BG_MAIN);
    int16_t bx2, by2; uint16_t bw2, bh2;
    cv->getTextBounds("Hoje", 0, 0, &bx2, &by2, &bw2, &bh2);
    cv->setCursor(160 - bw2 / 2 - bx2, 26);
    cv->print("Hoje");

    /* Botão Mês ▶ */
    cv->fillRoundRect(217, 3, 98, 36, 10, C_CARD_BG);
    cv->setTextColor(C_TEXT_SUB);
    cv->setCursor(237, 26);
    cv->print("Mes >");

    /* 4 px de margem inferior: y=195 + h=41 = 236 ≤ 236 */
    blitCanvas(cv, 0, 195, 320, 41);
}
