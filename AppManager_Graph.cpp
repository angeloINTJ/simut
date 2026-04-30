/**
 * @file    AppManager_Graph.cpp
 * @brief   Graph rendering from binary history: cache, render, preload, append.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "SystemDefs.h"
#include <LittleFS.h>
#include <time.h>

static inline float readRecordValue(const BinaryHistoryRecord& rec,
                                     int sensorId, float& humOut)
{
    humOut = NAN;

    if (sensorId == -1) {
        humOut = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);
        return BinaryHistoryRecord::i16ToFloat(rec.ambientTemp);
    }

    if (sensorId >= 0 && sensorId < MAX_SENSORS) {
        return BinaryHistoryRecord::i16ToFloat(rec.sensors[sensorId]);
    }

    return NAN;
}

/*                     GRAPH RENDERING FROM BINARY HISTORY                   */
/* =========================================================================== */
/**
 * @brief Load and render a temperature/humidity graph from binary history.
 *
 * Uses fixed-size records for exact seek (offset = recordIndex * 28).
 * Eliminates CSV parsing, seek fallback, and line realignment.
 * 6-second budget limit prevents watchdog timeout.
 */
void AppManager::renderGraphOptimized(int sensorId, int range, bool showAfterLoad, int navOffset, time_t forceEndEpoch) {
    if (!_storageMgr.lockHeavyTask()) {
        LOG_CODE(LOG_WARN, "APP", APP_FLASH_BUSY, 0, "");
        _displayMgr.forceDashboard();
        return;
    }
    /*
     * WdtWindow context-aware: renderGraph pode ser chamado de UI event
     * (main loop), de core0Yield (dentro de web handler), ou de
     * preloadSensorRanges (5x por 6s = até 30s). 30s cobre qualquer caso.
     * Aninhado dentro de telemetria (120s) ou web handler, mantém o outer.
     */
    LogManager::WdtWindow _wdt(30000);
    LOG_CODE(LOG_INFO, "APP", APP_GRAPH_LOADING, 0, "");

    uint32_t _graphBudgetStart = millis();
    const uint32_t GRAPH_BUDGET_MS = 6000;

    static GraphDataPackage pkg;
    memset(&pkg, 0, sizeof(GraphDataPackage));
    pkg.sensorIdx = sensorId;
    pkg.timeRange = range;
    pkg.count = 0;

    pkg.minVal = 1000.0f;
    pkg.maxVal = -1000.0f;
    pkg.idxMinTemp = -1;
    pkg.idxMaxTemp = -1;
    pkg.tsMaxHum = 0;
    pkg.tsMinHum = 0;
    float localHumMin = 1000.0f;
    float localHumMax = -1000.0f;

    pkg.hasHumidity = (sensorId == -1);

    SystemConfig &cfg = _storageMgr.getConfig();
    uint32_t epochLimit = 0;

    if (sensorId == -1) {
        snprintf(pkg.title, sizeof(pkg.title), "%s", _displayMgr.tr(TR_AMBIENT));
        snprintf(pkg.hwId, sizeof(pkg.hwId), "AMB");
        snprintf(pkg.rom, sizeof(pkg.rom), "INTERNAL-DHT");
    } else if (sensorId == 10) {
        snprintf(pkg.title, sizeof(pkg.title), "Board Temp");
        snprintf(pkg.hwId, sizeof(pkg.hwId), "SYS");
        snprintf(pkg.rom, sizeof(pkg.rom), "RP2040-ADC");
    } else {
        if (sensorId < 10 && cfg.sensors[sensorId].active) {
            safeCopy(pkg.title, cfg.sensors[sensorId].friendlyName, sizeof(pkg.title));
            safeCopy(pkg.hwId, cfg.sensors[sensorId].hwId, sizeof(pkg.hwId));
            epochLimit = cfg.sensors[sensorId].provisionEpoch;
            snprintf(pkg.rom, sizeof(pkg.rom), "%02X%02X%02X%02X%02X%02X%02X%02X",
                cfg.sensors[sensorId].rom[0], cfg.sensors[sensorId].rom[1],
                cfg.sensors[sensorId].rom[2], cfg.sensors[sensorId].rom[3],
                cfg.sensors[sensorId].rom[4], cfg.sensors[sensorId].rom[5],
                cfg.sensors[sensorId].rom[6], cfg.sensors[sensorId].rom[7]);
        } else {
            snprintf(pkg.title, sizeof(pkg.title), "Sensor %d", sensorId + 1);
            snprintf(pkg.hwId, sizeof(pkg.hwId), "--");
            snprintf(pkg.rom, sizeof(pkg.rom), "N/A");
        }
    }
    pkg.title[31] = '\0'; pkg.hwId[15] = '\0'; pkg.rom[23] = '\0';

    time_t now = time(nullptr);
    time_t cutoff = 0;
    int daysToLoad = 1;
    int decimation = 1;

    /*
     * Tabela de duração e passo por range:
     *   1H  → 3600s     6H  → 21600s    12H → 43200s
     *   24H → 86400s    7D  → 604800s
     *
     * navOffset desloca a janela temporal em passos do range.
     * ex: range=24H, navOffset=-2 → mostra 2 dias atrás.
     */
    static const time_t rangeDuration[] = { 3600, 21600, 43200, 86400, 604800 };
    time_t step = (range >= 0 && range <= 4) ? rangeDuration[range] : 86400;
    time_t effectiveEnd;

    if (forceEndEpoch > 0) {
        /* Modo calendário: janela fixa meia-noite a meia-noite */
        effectiveEnd = forceEndEpoch;
    } else {
        effectiveEnd = now + (time_t)navOffset * step;
        if (effectiveEnd > now) effectiveEnd = now; /* Não permite ver o futuro */
    }

    if (range == 0) { cutoff = effectiveEnd - 3600;   decimation = 1;  }
    else if (range == 1) { cutoff = effectiveEnd - 21600;  decimation = 2;  }
    else if (range == 2) { cutoff = effectiveEnd - 43200;  decimation = 4;  }
    else if (range == 3) { cutoff = effectiveEnd - 86400;  decimation = 8;  }
    else if (range == 4) { cutoff = effectiveEnd - 604800; decimation = 51; daysToLoad = 7; }

    if (range <= 3) {
        struct tm todayTm;
        localtime_r(&effectiveEnd, &todayTm);
        todayTm.tm_hour = 0; todayTm.tm_min = 0; todayTm.tm_sec = 0;
        time_t todayMidnight = mktime(&todayTm);
        daysToLoad = (cutoff < todayMidnight) ? 2 : 1;
    }

    int lineIdx = decimation - 1;

    /*
     * Armazena a janela temporal no pacote para que o renderer
     * posicione os pontos proporcionalmente ao tempo (não ao índice).
     */
    pkg.tsCutoff = cutoff;
    pkg.tsEnd    = effectiveEnd;

    /* Pré-popula timestamps para header (mostra período mesmo sem dados) */
    pkg.tsFirst = cutoff;
    pkg.tsLast  = effectiveEnd;
    pkg.tsMid   = cutoff + (effectiveEnd - cutoff) / 2;

    /* Min/max reais: rastreados de TODOS os registros, não apenas decimados */
    pkg.realMinVal = 1000.0f;
    pkg.realMaxVal = -1000.0f;
    pkg.tsRealMin  = 0;
    pkg.tsRealMax  = 0;

    for (int d = daysToLoad - 1; d >= 0; d--) {
        if (pkg.count >= GRAPH_WIDTH) break;

        time_t targetDay = effectiveEnd - (d * 86400);
        struct tm timeinfo;
        localtime_r(&targetDay, &timeinfo);

        char path[40];
        snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

        File f;
        _storageMgr.enterFlashReadLock();
        bool fileExists = LittleFS.exists(path);
        if (fileExists) f = LittleFS.open(path, "r");
        _storageMgr.exitFlashReadLock();

        if (fileExists && f) {
            /* v2: valida header SIM2. Sem seek otimizado (records variaveis). */
            HistoryFileHeaderV2 hdrG;
            bool headerOkG = false;
            {
                StorageManager::ReadGuard rg(&_storageMgr);
                if (f.size() >= HIST_V2_HEADER_SIZE) {
                    f.seek(0);
                    if (f.read((uint8_t*)&hdrG, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
                        headerOkG = (memcmp(hdrG.magic, HIST_V2_MAGIC, 4) == 0 &&
                                     hdrG.version == HIST_V2_VERSION &&
                                     hdrG.anchorPeriod > 0);
                    }
                }
            }
            if (!headerOkG) { _storageMgr.enterFlashReadLock(); f.close(); _storageMgr.exitFlashReadLock(); continue; }

            HistoryCodecState gState;
            historyCodecReset(gState);
            uint16_t gAnchorPeriod = hdrG.anchorPeriod;
            uint8_t  gRdBuf[256];
            size_t   gRdFilled = 0;

            bool hasMore = true;
            bool budgetExceeded = false;

            while (hasMore && pkg.count < GRAPH_WIDTH && !budgetExceeded) {
                if (timeSince(_graphBudgetStart, GRAPH_BUDGET_MS)) {
                    LOG_CODE(LOG_WARN, "APP", APP_GRAPH_BUDGET, 0, "");
                    budgetExceeded = true;
                    break;
                }

                _storageMgr.enterFlashReadLock();
                BinaryHistoryRecord batch[20];
                int batchCount = 0;
                while (batchCount < 20 && pkg.count < GRAPH_WIDTH) {
                    if (gRdFilled < HIST_V2_MAX_DELTA_SIZE && f.available() > 0) {
                        int rN = f.read(gRdBuf + gRdFilled, sizeof(gRdBuf) - gRdFilled);
                        if (rN > 0) gRdFilled += (size_t)rN;
                    }
                    if (gRdFilled == 0) break;
                    bool isAnc = (gState.recordsSinceAnchor == 0) ||
                                 (gState.recordsSinceAnchor == gAnchorPeriod);
                    size_t consumed = historyDecodeRecord(gRdBuf, gRdFilled, gState,
                                                           batch[batchCount], isAnc);
                    if (consumed == 0) break;
                    memmove(gRdBuf, gRdBuf + consumed, gRdFilled - consumed);
                    gRdFilled -= consumed;
                    batchCount++;
                }
                hasMore = (gRdFilled > 0 || f.available() > 0);
                _storageMgr.exitFlashReadLock();

                bool pastWindow = false;

                for (int bi = 0; bi < batchCount && pkg.count < GRAPH_WIDTH; bi++) {
                    const BinaryHistoryRecord& rec = batch[bi];

                    time_t ts = (time_t)rec.epoch;
                    if (ts < cutoff) continue;

                    /*
                     * Registros são cronológicos: se este ultrapassou effectiveEnd,
                     * todos os seguintes também ultrapassarão. Break imediato
                     * em vez de continue evita ler o resto do arquivo inutilmente.
                     * Crítico para 1H: sem isso, lê ~1380 registros a mais num
                     * arquivo de 1440 → estoura budget de 6s.
                     */
                    if (ts > effectiveEnd) { pastWindow = true; break; }

                    float humRead = NAN;
                    float valRead = readRecordValue(rec, sensorId, humRead);
                    if (ts < epochLimit) valRead = NAN;

                    /*
                     * Min/max REAIS: rastreados de CADA registro na janela,
                     * independente da decimação. Garante que o eixo Y e os
                     * badges mostrem os valores extremos verdadeiros.
                     */
                    if (!isnan(valRead)) {
                        if (valRead < pkg.realMinVal) { pkg.realMinVal = valRead; pkg.tsRealMin = ts; }
                        if (valRead > pkg.realMaxVal) { pkg.realMaxVal = valRead; pkg.tsRealMax = ts; }
                    }

                    /* Decimação: pula registros intermediários para caber na tela */
                    lineIdx++;
                    if (lineIdx % decimation != 0) continue;

                    /*
                     * SEMPRE adiciona o ponto ao array, mesmo se NAN.
                     * Pontos NAN preservam a posição temporal no eixo X,
                     * criando buracos visíveis no gráfico onde o sensor
                     * estava em erro. O renderer pula segmentos com NAN.
                     */
                    pkg.pointsV1[pkg.count] = valRead;
                    pkg.tsPoints[pkg.count] = (uint32_t)ts;

                    if (pkg.hasHumidity) {
                        pkg.pointsV2[pkg.count] = humRead;
                    }

                    if (pkg.count == 0) pkg.tsFirst = ts;

                    /* Estatísticas dos pontos exibidos (para marcadores no gráfico) */
                    if (!isnan(valRead)) {
                        if (valRead < pkg.minVal) {
                            pkg.minVal = valRead;
                            pkg.idxMinTemp = pkg.count;
                            pkg.tsMinTemp = ts;
                        }
                        if (valRead > pkg.maxVal) {
                            pkg.maxVal = valRead;
                            pkg.idxMaxTemp = pkg.count;
                            pkg.tsMaxTemp = ts;
                        }
                    }

                    if (pkg.hasHumidity && !isnan(humRead)) {
                        if (humRead < localHumMin) {
                            localHumMin = humRead;
                            pkg.tsMinHum = ts;
                        }
                        if (humRead > localHumMax) {
                            localHumMax = humRead;
                            pkg.tsMaxHum = ts;
                        }
                    }

                    pkg.tsLast = ts;
                    pkg.count++;
                }

                /* Saiu da janela temporal: interrompe leitura deste arquivo */
                if (pastWindow) break;

                feedWdt();
                yield();
            }

            _storageMgr.enterFlashReadLock();
            f.close();
            _storageMgr.exitFlashReadLock();

            if (budgetExceeded) {
                _storageMgr.unlockHeavyTask();
                _displayMgr.forceDashboard();
                return;
            }
        }

        feedWdt();
        yield();
    }

    if (pkg.count > 0) {
        pkg.tsMid = pkg.tsFirst + (pkg.tsLast - pkg.tsFirst) / 2;

        {
            float sumT = 0.0f;
            float sumH = 0.0f;
            int   tempCount = 0;
            int   humCount = 0;

            for (int i = 0; i < pkg.count; i++) {
                if (!isnan(pkg.pointsV1[i])) {
                    sumT += pkg.pointsV1[i];
                    tempCount++;
                }
                if (pkg.hasHumidity && !isnan(pkg.pointsV2[i])) {
                    sumH += pkg.pointsV2[i];
                    humCount++;
                }
            }
            pkg.avgTemp = (tempCount > 0) ? (sumT / (float)tempCount) : NAN;
            pkg.avgHum  = (humCount > 0) ? (sumH / (float)humCount) : NAN;

            float sqSumT = 0.0f;
            float sqSumH = 0.0f;
            for (int i = 0; i < pkg.count; i++) {
                if (!isnan(pkg.pointsV1[i]) && !isnan(pkg.avgTemp)) {
                    float diffT = pkg.pointsV1[i] - pkg.avgTemp;
                    sqSumT += diffT * diffT;
                }
                if (pkg.hasHumidity && !isnan(pkg.pointsV2[i]) && !isnan(pkg.avgHum)) {
                    float diffH = pkg.pointsV2[i] - pkg.avgHum;
                    sqSumH += diffH * diffH;
                }
            }
            pkg.stdTemp = (tempCount > 1) ? sqrtf(sqSumT / (float)(tempCount - 1)) : 0.0f;
            pkg.stdHum  = (humCount > 2) ? sqrtf(sqSumH / (float)(humCount - 1)) : NAN;

            /* Delta: busca primeiro e último valores VÁLIDOS */
            float firstValid = NAN, lastValid = NAN;
            float firstValidH = NAN, lastValidH = NAN;
            for (int i = 0; i < pkg.count; i++) {
                if (!isnan(pkg.pointsV1[i])) {
                    if (isnan(firstValid)) firstValid = pkg.pointsV1[i];
                    lastValid = pkg.pointsV1[i];
                }
                if (pkg.hasHumidity && !isnan(pkg.pointsV2[i])) {
                    if (isnan(firstValidH)) firstValidH = pkg.pointsV2[i];
                    lastValidH = pkg.pointsV2[i];
                }
            }
            pkg.deltaTemp = (!isnan(firstValid) && !isnan(lastValid)) ? (lastValid - firstValid) : NAN;
            pkg.deltaHum  = (!isnan(firstValidH) && !isnan(lastValidH)) ? (lastValidH - firstValidH) : NAN;
        }

        if (pkg.hasHumidity && localHumMax > -1000.0f) {
            if (localHumMax > 100.0f) localHumMax = 100.0f;
            if (localHumMin < 0.0f) localHumMin = 0.0f;
        } else {
            localHumMin = 0.0f;
            localHumMax = 100.0f;
        }
    } else {
        pkg.minVal = 0.0f;
        pkg.maxVal = 40.0f;
        pkg.realMinVal = 0.0f;
        pkg.realMaxVal = 40.0f;
        pkg.avgTemp = NAN;
        pkg.stdTemp = NAN;
        pkg.deltaTemp = NAN;
        pkg.avgHum = NAN;
        pkg.stdHum = NAN;
        pkg.deltaHum = NAN;
        localHumMin = 0.0f;
        localHumMax = 100.0f;

        /*
         * Mesmo sem dados, preenche tsFirst/tsLast com a janela temporal
         * solicitada para que o header exiba o período de referência.
         */
        pkg.tsFirst = cutoff;
        pkg.tsLast  = effectiveEnd;
        pkg.tsMid   = cutoff + (effectiveEnd - cutoff) / 2;
    }

    /*
     * Modo calendário (forceEndEpoch > 0): o header e eixo X devem
     * sempre mostrar o período COMPLETO do dia selecionado (00:00–23:59),
     * independente de onde os dados reais começam/terminam.
     * Ajusta tsLast para 23:59 (effectiveEnd - 60s) para evitar que
     * o display mostre "08/04 00:00" (meia-noite do dia seguinte).
     */
    if (forceEndEpoch > 0) {
        pkg.tsFirst = cutoff;                                /* 00:00 do dia */
        pkg.tsLast  = forceEndEpoch - 60;                   /* 23:59 do dia */
        pkg.tsMid   = cutoff + (forceEndEpoch - cutoff) / 2; /* ~12:00      */
    }

    /* F-MEM-NOCACHE: caches eliminados em alpha14 — sem writes. */

    _storageMgr.unlockHeavyTask();

    if (showAfterLoad) {
        _displayMgr.showGraphPlot(pkg, localHumMin, localHumMax);
    }
}
