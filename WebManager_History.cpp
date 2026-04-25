/**
 * @file    WebManager_History.cpp
 * @brief   History/log endpoints: binary history data, log viewer, screenshot, history days.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include <LittleFS.h>
#include <time.h>

using ReadGuard = StorageManager::ReadGuard;

void WebManager::handleApiHistoryData() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_HISTORY)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }
    if (!_server.hasArg("sensor")) { _server.send(400, "application/json", "{\"error\":\"Missing sensor param\"}"); return; }


    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
        return;
    }


    if (__atomic_exchange_n(&_inHistoryHandler, true, __ATOMIC_ACQ_REL)) {
        _server.send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
    }

    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) {
        __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
        _server.send(503, "application/json", "{\"error\":\"System Busy.\"}");
        return;
    }


    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;


    if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str());

    int sensorIdx = _server.arg("sensor").toInt();
    String reqDate = _server.arg("date");
    String reqRange = _server.arg("range");
    String reqEnd   = _server.arg("end");

    uint32_t epochLimit = 0;
    if (sensorIdx >= 0 && sensorIdx < MAX_SENSORS) {
        epochLimit = _storageRef->getConfig().sensors[sensorIdx].provisionEpoch;
    }

    /*
     * Calcula janela temporal — mesma lógica do display.
     * Parâmetros: range=0..4, end=epoch (âncora), date=YYYYMMDD
     */
    time_t now = _netRef->getEpoch();
    static const time_t rangeDuration[] = { 3600, 21600, 43200, 86400, 604800 };
    static const int rangeDecimation[]  = { 1, 1, 2, 3, 15 };
    static const int rangeDays[]        = { 1, 1, 1, 2, 7 };

    time_t effectiveEnd = now;
    time_t cutoff = 0;
    int decimation = 1;
    int daysToLoad = 1;

    if (reqRange.length() > 0) {
        int r = reqRange.toInt();
        if (r < 0) r = 0; if (r > 4) r = 4;

        /* Âncora: se 'end' especificado, usa como fim da janela */
        if (reqEnd.length() > 0) {
            effectiveEnd = (time_t)reqEnd.toInt();
            if (effectiveEnd > now) effectiveEnd = now;
        }

        cutoff = effectiveEnd - rangeDuration[r];
        decimation = rangeDecimation[r];
        daysToLoad = rangeDays[r];

        /* Para ranges ≤24H, verifica se cruza meia-noite */
        if (r <= 3) {
            struct tm etm;
            localtime_r(&effectiveEnd, &etm);
            etm.tm_hour = 0; etm.tm_min = 0; etm.tm_sec = 0;
            time_t eMidnight = mktime(&etm);
            if (cutoff < eMidnight) daysToLoad = 2;
        }
    } else if (reqDate.length() == 8) {
        /* Modo data: dia inteiro (00:00–23:59) */
        int y = reqDate.substring(0,4).toInt();
        int m = reqDate.substring(4,6).toInt();
        int d = reqDate.substring(6,8).toInt();
        struct tm dtm = {};
        dtm.tm_year = y - 1900; dtm.tm_mon = m - 1; dtm.tm_mday = d;
        cutoff = mktime(&dtm);
        effectiveEnd = cutoff + 86400;
        decimation = 3;
        daysToLoad = 1;
    }

    /* Monta lista de arquivos a ler */
    std::vector<String> filesToRead;
    for (int d = daysToLoad - 1; d >= 0; d--) {
        time_t targetDay = effectiveEnd - (d * 86400);
        struct tm timeinfo; localtime_r(&targetDay, &timeinfo);
        char defPath[40];
        snprintf(defPath, sizeof(defPath), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        filesToRead.push_back(String(defPath));
    }

    /* Metadados para o frontend: min/max reais, janela temporal */
    float realMinT = 1000.0f, realMaxT = -1000.0f;
    time_t tsRealMinT = 0, tsRealMaxT = 0;
    float realMinH = 1000.0f, realMaxH = -1000.0f;

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    /* Envia header com metadados da janela temporal */
    {
        char metaBuf[128];
        snprintf(metaBuf, sizeof(metaBuf),
            "{\"cutoff\":%lu,\"end\":%lu,\"now\":%lu,\"data\":[",
            (unsigned long)cutoff, (unsigned long)effectiveEnd, (unsigned long)now);
        safeSend(metaBuf);
    }

    bool first = true;
    static char chunkBuf[2048];
    chunkBuf[0] = '\0';
    int chunkLen = 0;
    bool aborted = false;
    int lineIdx = 0;

    for (size_t fi = 0; fi < filesToRead.size(); fi++) {
        if (aborted) break;
        String path = filesToRead[fi];

        File f;
        bool fileOk = false;
        {
            ReadGuard rg(_storageRef);
            if (LittleFS.exists(path)) {
                f = LittleFS.open(path, "r");
                fileOk = (bool)f;
            }
        }

        if (fileOk) {
            size_t fileSize = f.size();
            size_t totalRecords = fileSize / HISTORY_RECORD_SIZE;

            /* Seek otimizado — mesma lógica do display */
            if (totalRecords > 50 && cutoff > 0) {
                struct tm fileTm;
                {
                    time_t targetDay = effectiveEnd - (int)(filesToRead.size() - 1 - fi) * 86400;
                    localtime_r(&targetDay, &fileTm);
                }
                fileTm.tm_hour = 0; fileTm.tm_min = 0; fileTm.tm_sec = 0;
                time_t fileMidnight = mktime(&fileTm);

                if (cutoff > fileMidnight) {
                    int seekFromMidnight = max(0, (int)((cutoff - fileMidnight) / 60) - 10);
                    static const int maxRec[] = { 80, 380, 740, 1460, 1460 };
                    int rIdx = (reqRange.length() > 0) ? constrain(reqRange.toInt(), 0, 4) : 3;
                    int seekFromEnd = max(0, (int)totalRecords - maxRec[rIdx]);
                    int seekRecord = (seekFromMidnight < (int)totalRecords)
                                     ? min(seekFromMidnight, seekFromEnd) : seekFromEnd;
                    if (seekRecord > 0 && seekRecord < (int)totalRecords) {
                        ReadGuard rg(_storageRef);
                        f.seek((size_t)seekRecord * HISTORY_RECORD_SIZE);
                    }
                }
            }

            bool fileHasMore = true;
            while (fileHasMore) {
                if (isClientGone() || isHandlerOvertime()) {
                    LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
                    { ReadGuard rg(_storageRef); f.close(); }
                    aborted = true;
                    break;
                }

                BinaryHistoryRecord readBatch[20];
                int batchCount = 0;

                {
                    ReadGuard rg(_storageRef);
                    while (f.available() >= HISTORY_RECORD_SIZE && batchCount < 20) {
                        if (f.read((uint8_t*)&readBatch[batchCount], HISTORY_RECORD_SIZE)
                            == HISTORY_RECORD_SIZE)
                        {
                            batchCount++;
                        }
                    }
                    fileHasMore = (f.available() >= HISTORY_RECORD_SIZE);
                }

                bool pastWindow = false;
                for (int bi = 0; bi < batchCount && !aborted; bi++) {
                    const BinaryHistoryRecord& rec = readBatch[bi];
                    time_t ts = (time_t)rec.epoch;

                    if (ts < cutoff && cutoff > 0) continue;
                    if (ts > effectiveEnd) { pastWindow = true; break; }

                    /* Rastreia min/max reais de TODOS os registros (pré-decimação) */
                    float preValT = NAN;
                    if (sensorIdx == -1) preValT = BinaryHistoryRecord::i16ToFloat(rec.ambientTemp);
                    else if (sensorIdx >= 0 && sensorIdx < MAX_SENSORS) preValT = BinaryHistoryRecord::i16ToFloat(rec.sensors[sensorIdx]);
                    if (ts < epochLimit) preValT = NAN;

                    if (!isnan(preValT)) {
                        if (preValT < realMinT) { realMinT = preValT; tsRealMinT = ts; }
                        if (preValT > realMaxT) { realMaxT = preValT; tsRealMaxT = ts; }
                    }

                    lineIdx++;
                    if (lineIdx % decimation != 0) continue;

                    float valT = preValT;
                    float valH = NAN;
                    if (sensorIdx == -1) valH = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);

                    if (!isnan(valH)) {
                        if (valH < realMinH) realMinH = valH;
                        if (valH > realMaxH) realMaxH = valH;
                    }

                    /* Emite ponto: NAN como null para o Chart.js criar buracos */
                    char pointBuf[96];
                    if (!isnan(valT) && valT > -50.0f && valT < 150.0f) {
                        const char* signT = (valT < 0.0f) ? "-" : "";
                        int tInt = abs((int)valT);
                        int tDec = abs((int)(valT * 100.0f) % 100);

                        if (sensorIdx == -1 && !isnan(valH) && valH >= 0.0f && valH <= 100.0f) {
                            int hInt = abs((int)valH);
                            int hDec = abs((int)(valH * 10.0f) % 10);
                            snprintf(pointBuf, sizeof(pointBuf), "%s{\"t\":%lu,\"v1\":%s%d.%02d,\"v2\":%d.%01d}",
                                     first ? "" : ",", (unsigned long)ts, signT, tInt, tDec, hInt, hDec);
                        } else {
                            snprintf(pointBuf, sizeof(pointBuf), "%s{\"t\":%lu,\"v1\":%s%d.%02d}",
                                     first ? "" : ",", (unsigned long)ts, signT, tInt, tDec);
                        }
                    } else {
                        /* Ponto NAN: emite com v1:null para buraco visível no Chart.js */
                        snprintf(pointBuf, sizeof(pointBuf), "%s{\"t\":%lu,\"v1\":null}",
                                 first ? "" : ",", (unsigned long)ts);
                    }

                    int pLen = strlen(pointBuf);
                    if (chunkLen + pLen >= (int)sizeof(chunkBuf) - 1) {
                        if (!safeSend(chunkBuf)) {
                            { ReadGuard rg(_storageRef); f.close(); }
                            aborted = true;
                            break;
                        }
                        chunkBuf[0] = '\0';
                        chunkLen = 0;
                        delay(5);
                        watchdog_update();
                    }
                    memcpy(chunkBuf + chunkLen, pointBuf, pLen + 1);
                    chunkLen += pLen;
                    first = false;

                    if (chunkLen > 1500) {
                        if (!safeSend(chunkBuf)) {
                            { ReadGuard rg(_storageRef); f.close(); }
                            aborted = true;
                            break;
                        }
                        chunkBuf[0] = '\0';
                        chunkLen = 0;
                        delay(5);
                        watchdog_update();
                    }
                }

                if (pastWindow) break;
                if (aborted) break;
                if (_lightYieldCb) _lightYieldCb();
                delay(5);
                watchdog_update();
            }
            if (!aborted) { ReadGuard rg(_storageRef); f.close(); }
        }
    }

    if (!aborted) {
        if (chunkLen > 0) safeSend(chunkBuf);

        /* Fecha array e emite metadados de min/max reais */
        char metaEnd[192];
        if (realMaxT > -999.0f) {
            const char* sMin = (realMinT < 0) ? "-" : "";
            const char* sMax = (realMaxT < 0) ? "-" : "";
            snprintf(metaEnd, sizeof(metaEnd),
                "],\"minT\":%s%d.%02d,\"maxT\":%s%d.%02d,\"tsMinT\":%lu,\"tsMaxT\":%lu}",
                sMin, abs((int)realMinT), abs((int)(realMinT*100)%100),
                sMax, abs((int)realMaxT), abs((int)(realMaxT*100)%100),
                (unsigned long)tsRealMinT, (unsigned long)tsRealMaxT);
        } else {
            snprintf(metaEnd, sizeof(metaEnd), "]}");
        }
        safeSend(metaEnd);
        safeSend("");
    }
    _handlerDeadline = savedDeadline;
    if (_displayRef) _displayRef->setWebBusy(false);
    __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
}

void WebManager::handleApiLogs() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_LOGS)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (isRateLimited(200)) { _server.send(429, "text/plain", "Too Fast"); return; }


    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
        return;
    }

    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) {
        _server.send(503, "text/plain", "System Busy.");
        return;
    }


    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;
    if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str());

    /*
     * Envia logs binários brutos (12 bytes/registro) para máxima
     * eficiência de transferência. A tradução acontece no browser.
     * Formato: application/octet-stream, N × CompactLogRecord(12 bytes).
     * ~10x menor que o CSV traduzido anterior.
     */
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/octet-stream", "");

    auto streamRawLog = [&](const char* path) -> bool {
        File f;
        {
            ReadGuard rg(_storageRef);
            if (!LittleFS.exists(path)) return true;
            f = LittleFS.open(path, "r");
        }

        if (!f) return true;

        int count = 0;
        while (f.available() >= LOG_RECORD_SIZE) {
            if (count > 0 && count % 80 == 0) {
                if (isClientGone() || isHandlerOvertime()) {
                    f.close();
                    return false;
                }
            }

            /* Lê batch de até 40 registros (480 bytes) e envia de uma vez */
            uint8_t buf[480];
            int bytesRead = 0;
            {
                ReadGuard rg(_storageRef);
                while (f.available() >= LOG_RECORD_SIZE && bytesRead + LOG_RECORD_SIZE <= (int)sizeof(buf)) {
                    if (f.read(buf + bytesRead, LOG_RECORD_SIZE) == LOG_RECORD_SIZE) {
                        bytesRead += LOG_RECORD_SIZE;
                    }
                }
            }

            if (bytesRead > 0) {
                _server.sendContent((const char*)buf, bytesRead);
                count += bytesRead / LOG_RECORD_SIZE;
            }

            if (_lightYieldCb) _lightYieldCb();
            delay(2);
            watchdog_update();
        }
        f.close();
        return true;
    };

    if (streamRawLog(LOG_FILE_OLD)) {
        streamRawLog(LOG_FILE_CURRENT);
    }

    safeSend("");
    _handlerDeadline = savedDeadline;
    if (_displayRef) _displayRef->setWebBusy(false);
}

void WebManager::handleApiClearLogs() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_LOGS) || !(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (isPasswordChangeRequired()) return;
    if (rejectIfTouchPriority()) return;

    {
        RenderGuard rg(_displayRef);
        LittleFS.remove(LOG_FILE_CURRENT);
        LittleFS.remove(LOG_FILE_OLD);
        /* Remove também logs CSV legados (pré-v3.4.7) */
        LittleFS.remove("/system.log");
        LittleFS.remove("/system.old");
        LogManager::instance().resetAfterExternalWipe();
    }

    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Admin erased System Logs", "Admin apagou logs do sistema"));
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleApiScreenshot() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }


    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
        return;
    }


    if (__atomic_exchange_n(&_isProcessingScreenshot, true, __ATOMIC_ACQ_REL)) {
        /* Screenshot em andamento: sinalizar cancelamento e retornar 409 */
        _cancelScreenshot = true;
        _server.send(409, "application/json", "{\"error\":\"Screenshot in progress, cancelling.\"}");
        return;
    }
    _cancelScreenshot = false;

    if (!_displayRef) {
        __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
        _server.send(500, "text/plain", "Display offline");
        return;
    }


    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;

    uint32_t w = 320;
    uint32_t h = 240;
    uint32_t rowSize = 960;
    uint32_t imgSize = rowSize * h;
    uint32_t fileSize = 54 + imgSize;

    uint8_t bmpHeader[54] = {
        'B', 'M',
        (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
        (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24),
        (uint8_t)(h), (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24),
        1, 0, 24, 0, 0, 0, 0, 0,
        (uint8_t)(imgSize), (uint8_t)(imgSize >> 8), (uint8_t)(imgSize >> 16), (uint8_t)(imgSize >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    _server.setContentLength(fileSize);
    _server.send(200, "image/bmp", "");
    safeSend((const char*)bmpHeader, 54);

    uint8_t rowBuffer[960];
    uint16_t pixelRow[320];
    bool clientDisconnected = false;

    for (int y = h - 1; y >= 0; y--) {


        _displayRef->pauseRendering(true);
        _displayRef->readRow(y, pixelRow, w);
        _displayRef->pauseRendering(false);


        for (int x = 0; x < (int)w; x++) {
            uint16_t color = pixelRow[x];
            rowBuffer[x*3 + 0] = (color & 0x001F) << 3;
            rowBuffer[x*3 + 1] = ((color & 0x07E0) >> 5) << 2;
            rowBuffer[x*3 + 2] = ((color & 0xF800) >> 11) << 3;
        }

        if (!_server.client().connected() || isHandlerOvertime() || _cancelScreenshot) {
            clientDisconnected = true;
            break;
        }

        safeSend((const char*)rowBuffer, 960);


        if (y % 4 == 0) {
            watchdog_update();
            if (_lightYieldCb) _lightYieldCb();
            delay(1);
        }
    }

    __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
    _handlerDeadline = savedDeadline;
    if (clientDisconnected) LOG_CODE(LOG_WARN, "WEB", WEB_SCREENSHOT_ABORTED, 0, "");
}

void WebManager::handleApiHistoryDays() {
    if ((getAuthPerms() & PERM_HISTORY) == 0) { _server.send(403); return; }


    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) { _server.send(503, "application/json", "{\"error\":\"System Busy\"}"); return; }

    std::vector<String> files;
    {

        ReadGuard rg(_storageRef);
        Dir dir = LittleFS.openDir(DIR_HISTORY);
        while (dir.next()) {
            feedWatchdog();
            if (dir.fileName().endsWith(HISTORY_FILE_EXT)) {
                files.push_back(dir.fileName());
            }
        }
    }

    std::sort(files.begin(), files.end(), std::greater<String>());

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");
    safeSend("[");
    for (size_t i = 0; i < files.size(); i++) {
        files[i].replace(HISTORY_FILE_EXT, "");
        String entry = (i > 0 ? ",\"" : "\"") + files[i] + "\"";
        safeSend(entry);
    }
    safeSend("]");
    safeSend("");
}
