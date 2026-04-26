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
            size_t fileSize = f.size();
            size_t totalRecords = fileSize / HISTORY_RECORD_SIZE;

            /* Seek otimizado para o cutoff */
            if (totalRecords > 50 && cutoff > 0) {
                struct tm fileTm = timeinfo;
                fileTm.tm_hour = 0; fileTm.tm_min = 0; fileTm.tm_sec = 0;
                time_t fileMidnight = mktime(&fileTm);

                /*
                 * Seek só quando o cutoff está DENTRO do dia deste arquivo.
                 * Se cutoff < fileMidnight, precisamos do arquivo inteiro
                 * (ex: 24H lendo arquivo de hoje, cutoff é ontem).
                 */
                if (cutoff > fileMidnight) {
                    /*
                     * Duas estratégias — usa a mais avançada:
                     *
                     * 1) Midnight-based: assume ~1 registro/minuto desde 00:00.
                     *    Preciso se o arquivo não tem lacunas.
                     *
                     * 2) End-based: recua N registros do fim do arquivo.
                     *    Robusto contra lacunas (reboots, boot loops).
                     */
                    int seekFromMidnight = max(0, (int)((cutoff - fileMidnight) / 60) - 10);

                    /*
                     * Registros BRUTOS necessários por range (pré-decimação).
                     * duração_em_minutos + margem de 20.
                     * 1H=80, 6H=380, 12H=740, 24H=1460, 7D=1460
                     * Para 24H/7D, seekFromEnd será 0 (arquivo inteiro).
                     */
                    static const int maxRecordsNeeded[] = { 80, 380, 740, 1460, 1460 };
                    int needed = (range >= 0 && range <= 4) ? maxRecordsNeeded[range] : 200;
                    int seekFromEnd = max(0, (int)totalRecords - needed);

                    /*
                     * Usa o MENOR dos dois (mais conservador = mais longe do fim).
                     * Se o arquivo tem lacunas, midnight-based pode overshoot.
                     * min() garante que nunca pulamos dados válidos.
                     */
                    int seekRecord;
                    if (seekFromMidnight < (int)totalRecords) {
                        seekRecord = min(seekFromMidnight, seekFromEnd);
                    } else {
                        seekRecord = seekFromEnd;
                    }

                    if (seekRecord > 0 && seekRecord < (int)totalRecords) {
                        { StorageManager::ReadGuard rg(&_storageMgr);
                          f.seek((size_t)seekRecord * HISTORY_RECORD_SIZE); }
                    }
                }
                /* Se cutoff <= fileMidnight: sem seek, lê o arquivo inteiro */
            }

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
                while (f.available() >= HISTORY_RECORD_SIZE
                       && batchCount < 20
                       && pkg.count < GRAPH_WIDTH)
                {
                    if (f.read((uint8_t*)&batch[batchCount], HISTORY_RECORD_SIZE)
                        == HISTORY_RECORD_SIZE)
                    {
                        batchCount++;
                    }
                }
                hasMore = (f.available() >= HISTORY_RECORD_SIZE);
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

    /* ── Salva no cache 7d de background se aplicável (e cache existe) ── */
    if (_graphCachesAllocated && range == 4 && pkg.count > 0) {
        int ci = graphCacheIdx(sensorId);
        _graphCache[ci].pkg         = pkg;
        _graphCache[ci].humMin      = localHumMin;
        _graphCache[ci].humMax      = localHumMax;
        _graphCache[ci].lastRefresh = time(nullptr);
        _graphCache[ci].valid       = true;
    }

    /* ── Salva no cache do sensor ativo (todos os ranges) ── */
    if (_graphCachesAllocated && sensorId == _sensorCacheId && range >= 0 && range < 5) {
        _sensorCache[range].pkg         = pkg;
        _sensorCache[range].humMin      = localHumMin;
        _sensorCache[range].humMax      = localHumMax;
        _sensorCache[range].lastRefresh = time(nullptr);
        _sensorCache[range].valid       = true;
    }

    _storageMgr.unlockHeavyTask();

    if (showAfterLoad) {
        _displayMgr.showGraphPlot(pkg, localHumMin, localHumMax);
    }
}

/* =========================================================================== */
/*                      GRAPH CACHE — PRE-LOADING SYSTEM                     */
/* =========================================================================== */

/**
 * @brief Converte sensorId para índice no array _graphCache[].
 *
 * Mapeamento:
 *   sensorId -1      → slot 11 (ambient/DHT)
 *   sensorId 0..9    → slot 0..9 (DS18B20)
 *   sensorId 10      → slot 10 (board temp / RP2040 ADC)
 */
int AppManager::graphCacheIdx(int sensorId) {
    if (sensorId == -1) return MAX_SENSORS + 1;  /* 11 = ambient */
    if (sensorId == 10) return MAX_SENSORS;       /* 10 = board   */
    if (sensorId >= 0 && sensorId < MAX_SENSORS) return sensorId;
    return 0;
}

/**
 * @brief Pré-carrega o cache 7d de todos os sensores ativos.
 *
 * Chamado durante o boot e pode ser re-chamado periodicamente.
 * Cada sensor usa lockHeavyTask (exclusão mútua com o web server),
 * liberando entre sensores para não bloquear watchdog e WiFi.
 */
void AppManager::preloadGraphCaches() {
    SystemConfig &cfg = _storageMgr.getConfig();

    /* Ambient (sempre presente) */
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_GRAPH_AMBIENT, 0, "");
    renderGraphOptimized(-1, 4, false);
    feedWdt();

    /* Board temp */
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_GRAPH_BOARD, 0, "");
    renderGraphOptimized(10, 4, false);
    feedWdt();

    /* DS18B20 ativos */
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].active) {
            char _logBuf[40];
            snprintf(_logBuf, sizeof(_logBuf), "Graph cache: loading sensor %d...", i);
            LOG_CODE(LOG_INFO, "APP", APP_GRAPH_LOADING, 0, String(_logBuf));
            renderGraphOptimized(i, 4, false);
            feedWdt();
        }
    }

    _lastGraphCacheRefresh = millis();
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_PRELOAD_DONE, 0, "");
}

/**
 * @brief Pré-carrega todos os ranges restantes do sensor ativo.
 *
 * Chamado imediatamente após exibir o gráfico solicitado pelo usuário.
 * Para o range 7D, copia do cache de background se disponível (zero I/O).
 * Para os demais, usa renderGraphOptimized com seek otimizado (~10-150ms cada).
 *
 * @param sensorId  ID do sensor (-1 = ambient, 0-9 = DS18B20, 10 = board).
 * @param skipRange Range que já foi carregado e exibido (não recarregar).
 */
void AppManager::preloadSensorRanges(int sensorId, int skipRange) {
    /* F-MEM-LAZYGRAPH: sem cache, não há o que pré-carregar. */
    if (!_graphCachesAllocated) return;
    for (int r = 0; r < 5; r++) {
        if (r == skipRange || _sensorCache[r].valid) continue;

        /* 7D: usa cache de background com atualização incremental */
        if (r == 4) {
            int ci = graphCacheIdx(sensorId);
            if (_graphCache[ci].valid) {
                _sensorCache[4] = _graphCache[ci];
                /* Se stale, faz append incremental (sem loading screen) */
                time_t age = time(nullptr) - _sensorCache[4].lastRefresh;
                if (age >= 1800) {
                    appendToGraphCache(_sensorCache[4], sensorId);
                }
                continue;
            }
        }

        /* Carrega do flash com seek otimizado */
        renderGraphOptimized(sensorId, r, false);
        feedWdt();
    }
}


/**
 * @brief Atualiza incrementalmente o cache 7D de um sensor.
 *
 * Em vez de recarregar todos os 7 dias do flash (~2-4s + loading screen),
 * lê APENAS os registros novos desde entry.pkg.tsLast e os anexa ao array
 * existente, removendo pontos antigos que saíram da janela de 7 dias.
 *
 * Fluxo:
 * 1. Calcula quantos pontos novos existem desde tsLast (com decimation=51)
 * 2. Remove pontos antigos que ultrapassaram a janela de 7 dias
 * 3. Lê novos registros do CSV (seek direto para tsLast)
 * 4. Anexa ao array e recalcula estatísticas
 *
 * @return true se o cache foi atualizado, false se não há dados novos.
 */
bool AppManager::appendToGraphCache(GraphCacheEntry& entry, int sensorId) {
    GraphDataPackage& pkg = entry.pkg;
    time_t now = time(nullptr);

    if (now - pkg.tsLast < 120) return true;

    time_t cutoff = now - 604800;
    const int decimation = 51;

    /* ── Fase 1: Remover pontos antigos que saíram da janela ── */
    if (pkg.count >= 2 && pkg.tsFirst < cutoff) {
        float dtPerPoint = (float)(pkg.tsLast - pkg.tsFirst) / (float)(pkg.count - 1);
        if (dtPerPoint < 1.0f) dtPerPoint = 51.0f * 60.0f;

        int discard = (int)((float)(cutoff - pkg.tsFirst) / dtPerPoint);
        if (discard < 0) discard = 0;
        if (discard > pkg.count) discard = pkg.count;

        if (discard > 0) {
            int remaining = pkg.count - discard;
            if (remaining > 0) {
                memmove(pkg.pointsV1, pkg.pointsV1 + discard, remaining * sizeof(float));
                memmove(pkg.tsPoints, pkg.tsPoints + discard, remaining * sizeof(uint32_t));
                if (pkg.hasHumidity) {
                    memmove(pkg.pointsV2, pkg.pointsV2 + discard, remaining * sizeof(float));
                }
            }
            pkg.count = remaining;
            pkg.tsFirst = pkg.tsFirst + (time_t)(discard * dtPerPoint);
        }
    }

    /* ── Fase 2: Ler novos registros do arquivo binário ── */
    if (!_storageMgr.lockHeavyTask()) return false;

    SystemConfig& cfg = _storageMgr.getConfig();
    uint32_t epochLimit = 0;
    if (sensorId >= 0 && sensorId < MAX_SENSORS && cfg.sensors[sensorId].active) {
        epochLimit = cfg.sensors[sensorId].provisionEpoch;
    }

    struct tm todayTm;
    localtime_r(&now, &todayTm);
    todayTm.tm_hour = 0; todayTm.tm_min = 0; todayTm.tm_sec = 0;
    time_t todayMidnight = mktime(&todayTm);
    int daysToLoad = (pkg.tsLast < todayMidnight) ? 2 : 1;

    int lineIdx = decimation - 1;
    int newPoints = 0;
    float localHumMin = entry.humMin;
    float localHumMax = entry.humMax;

    for (int d = daysToLoad - 1; d >= 0; d--) {
        if (pkg.count >= GRAPH_WIDTH) break;

        time_t targetDay = now - (d * 86400);
        struct tm ti;
        localtime_r(&targetDay, &ti);

        char path[40];
        snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);

        _storageMgr.enterFlashReadLock();
        bool exists = LittleFS.exists(path);
        File f;
        if (exists) f = LittleFS.open(path, "r");
        _storageMgr.exitFlashReadLock();

        if (!exists || !f) continue;

        /* Seek exato para tsLast */
        size_t fileSize = f.size();
        size_t totalRecords = fileSize / HISTORY_RECORD_SIZE;
        if (totalRecords > 20 && pkg.tsLast > 0) {
            struct tm fileTm = ti;
            fileTm.tm_hour = 0; fileTm.tm_min = 0; fileTm.tm_sec = 0;
            time_t fileMidnight = mktime(&fileTm);

            if (pkg.tsLast > fileMidnight) {
                int minPast = (int)((pkg.tsLast - fileMidnight) / 60);
                int seekRecord = max(0, minPast - 5);
                if (seekRecord < (int)totalRecords) {
                    _storageMgr.enterFlashReadLock();
                    f.seek((size_t)seekRecord * HISTORY_RECORD_SIZE);
                    _storageMgr.exitFlashReadLock();
                }
            }
        }

        bool hasMore = true;
        while (hasMore && pkg.count < GRAPH_WIDTH) {
            _storageMgr.enterFlashReadLock();
            BinaryHistoryRecord batch[20];
            int batchCount = 0;
            while (f.available() >= HISTORY_RECORD_SIZE
                   && batchCount < 20
                   && pkg.count < GRAPH_WIDTH)
            {
                if (f.read((uint8_t*)&batch[batchCount], HISTORY_RECORD_SIZE)
                    == HISTORY_RECORD_SIZE)
                {
                    batchCount++;
                }
            }
            hasMore = (f.available() >= HISTORY_RECORD_SIZE);
            _storageMgr.exitFlashReadLock();

            for (int bi = 0; bi < batchCount && pkg.count < GRAPH_WIDTH; bi++) {
                const BinaryHistoryRecord& rec = batch[bi];

                time_t ts = (time_t)rec.epoch;
                if (ts <= pkg.tsLast) continue;

                float humRead = NAN;
                float valRead = readRecordValue(rec, sensorId, humRead);
                if (ts < epochLimit) valRead = NAN;

                /* Real min/max de todos os registros (pré-decimação) */
                if (!isnan(valRead)) {
                    if (valRead < pkg.realMinVal) { pkg.realMinVal = valRead; pkg.tsRealMin = ts; }
                    if (valRead > pkg.realMaxVal) { pkg.realMaxVal = valRead; pkg.tsRealMax = ts; }
                }

                lineIdx++;
                if (lineIdx % decimation != 0) continue;

                /* Inclui NAN para preservar buracos no gráfico */
                pkg.pointsV1[pkg.count] = valRead;
                pkg.tsPoints[pkg.count] = (uint32_t)ts;
                if (pkg.hasHumidity) {
                    pkg.pointsV2[pkg.count] = humRead;
                    if (!isnan(humRead)) {
                        if (humRead < localHumMin) localHumMin = humRead;
                        if (humRead > localHumMax) localHumMax = humRead;
                    }
                }
                pkg.tsLast = ts;
                pkg.count++;
                newPoints++;
            }

            feedWdt(); yield();
        }

        _storageMgr.enterFlashReadLock();
        f.close();
        _storageMgr.exitFlashReadLock();
    }

    _storageMgr.unlockHeavyTask();

    /* ── Fase 3: Recalcular estatísticas (ignorando NANs) ── */
    if (newPoints > 0 && pkg.count >= 2) {
        pkg.tsMid = pkg.tsFirst + (pkg.tsLast - pkg.tsFirst) / 2;

        pkg.minVal = 1000.0f; pkg.maxVal = -1000.0f;
        pkg.idxMinTemp = -1;  pkg.idxMaxTemp = -1;
        float sumT = 0, sqSumT = 0;
        float sumH = 0, sqSumH = 0;
        int tempCount = 0;
        int humCount = 0;

        for (int i = 0; i < pkg.count; i++) {
            float v = pkg.pointsV1[i];
            if (!isnan(v)) {
                sumT += v;
                tempCount++;
                if (v < pkg.minVal) { pkg.minVal = v; pkg.idxMinTemp = i; }
                if (v > pkg.maxVal) { pkg.maxVal = v; pkg.idxMaxTemp = i; }
            }
            if (pkg.hasHumidity && !isnan(pkg.pointsV2[i])) {
                sumH += pkg.pointsV2[i]; humCount++;
            }
        }
        pkg.avgTemp = (tempCount > 0) ? (sumT / (float)tempCount) : NAN;
        pkg.avgHum  = (humCount > 0) ? (sumH / (float)humCount) : NAN;

        for (int i = 0; i < pkg.count; i++) {
            if (!isnan(pkg.pointsV1[i]) && !isnan(pkg.avgTemp)) {
                float dT = pkg.pointsV1[i] - pkg.avgTemp;
                sqSumT += dT * dT;
            }
            if (pkg.hasHumidity && !isnan(pkg.pointsV2[i]) && !isnan(pkg.avgHum)) {
                float dH = pkg.pointsV2[i] - pkg.avgHum;
                sqSumH += dH * dH;
            }
        }
        pkg.stdTemp = (tempCount > 1) ? sqrtf(sqSumT / (float)(tempCount - 1)) : 0.0f;
        pkg.stdHum  = (humCount > 2) ? sqrtf(sqSumH / (float)(humCount - 1)) : NAN;

        /* Delta: primeiro e último valores VÁLIDOS */
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

        /* Timestamps de extremos: usa tsPoints real em vez de interpolação linear */
        if (pkg.idxMinTemp >= 0) pkg.tsMinTemp = (time_t)pkg.tsPoints[pkg.idxMinTemp];
        if (pkg.idxMaxTemp >= 0) pkg.tsMaxTemp = (time_t)pkg.tsPoints[pkg.idxMaxTemp];

        /*
         * Cache 7D: sincroniza realMinVal/realMaxVal com os pontos no array.
         * Após descartar pontos antigos + recalcular, os extremos anteriores
         * podem ser de registros que já saíram da janela.
         */
        pkg.realMinVal = pkg.minVal;
        pkg.realMaxVal = pkg.maxVal;
        pkg.tsRealMin  = pkg.tsMinTemp;
        pkg.tsRealMax  = pkg.tsMaxTemp;

        entry.humMin = localHumMin;
        entry.humMax = localHumMax;
        entry.lastRefresh = now;
    }

    return (newPoints > 0);
}
