/**
 * @file    WebManager_History.cpp
 * @brief   History/log endpoints: binary history data, log viewer, screenshot, history days.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include "HistoryCodec.h"
#include <LittleFS.h>
#include <time.h>

using ReadGuard = StorageManager::ReadGuard;


/* =========================================================================== */
/*  F-GRAPH-REVAMP: GET /api/history_multi?sensors=<csv>&range=<0..6>&end=<ep> */
/* =========================================================================== */
/* Versao multi-sensor de handleApiHistoryData. Retorna 1 response com TODAS
 * as series solicitadas, evitando overhead de N fetches sequenciais bloqueados
 * pelo _inHistoryHandler atomic guard.
 *
 * Args:
 *   sensors=-1,0,5    CSV de IDs (-1=ambient, 0..9=DS18B20). Default: -1.
 *   range=0..6        Niveis: 1h, 6h, 24h, 7d, 1M, 1A, MAX (=todos arquivos).
 *   end=<epoch>       Ancora (default: agora).
 *
 * JSON:
 *   {"cutoff":..., "end":..., "now":...,
 *    "sensors":[{"id":-1,"hwId":"AMB","name":"...","hasH":true}, ...],
 *    "data":[{"t":epoch,"v":[float|null,...],"h":float}, ...],
 *    "minT":..., "maxT":..., "tsMinT":..., "tsMaxT":...,
 *    "rangeUsed":N}
 *
 * v[] e' alinhado por indice com sensors[]. h so' aparece se ambient esta
 * incluso e tem leitura valida no record.
 */
void WebManager::handleApiHistoryMulti() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_HISTORY)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

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
        _server.send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
    }

    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;
    if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str());

    /* ── Parse sensors=... (CSV de IDs) ─────────────────────────────────── */
    int  sensorIds[11];           /* MAX_SENSORS + 1 (ambient) */
    int  sensorCount = 0;
    bool ambientIncluded = false;
    int  ambientPos = -1;         /* indice dentro de sensorIds[] */
    String sArg = _server.hasArg("sensors") ? _server.arg("sensors") : String("-1");
    {
        int start = 0;
        while (start < (int)sArg.length() && sensorCount < 11) {
            int comma = sArg.indexOf(',', start);
            String tok = (comma < 0) ? sArg.substring(start) : sArg.substring(start, comma);
            tok.trim();
            if (tok.length() > 0) {
                int id = tok.toInt();
                /* Aceita -1 (ambient) ou 0..MAX_SENSORS-1. Filtra duplicados. */
                if (id >= -1 && id < MAX_SENSORS) {
                    bool dup = false;
                    for (int i = 0; i < sensorCount; i++) if (sensorIds[i] == id) { dup = true; break; }
                    if (!dup) {
                        sensorIds[sensorCount] = id;
                        if (id == -1) { ambientIncluded = true; ambientPos = sensorCount; }
                        sensorCount++;
                    }
                }
            }
            if (comma < 0) break;
            start = comma + 1;
        }
    }
    if (sensorCount == 0) { sensorIds[0] = -1; sensorCount = 1; ambientIncluded = true; ambientPos = 0; }

    /* ── Parse range/end ────────────────────────────────────────────────── */
    String reqRange = _server.arg("range");
    String reqEnd   = _server.arg("end");

    static const time_t rangeDuration[] = { 3600, 21600, 86400, 604800, 2592000, 31536000, 0 };
    /* PERF (test_perf.sh): testei decimacao=480 p/ 1A/MAX mas tempo nao
     * mudou — bottleneck e flash read, nao JSON emit. Mantido em 240
     * pra fidelidade maior no grafico. */
    static const int rangeDecimation[]  = { 1, 1, 3, 15, 60, 240, 240 };

    time_t now = _netRef->getEpoch();
    time_t effectiveEnd = now;
    time_t cutoff = 0;
    int decimation = 1;
    int rangeIdx = 2;             /* Default: 24h */

    if (reqRange.length() > 0) {
        rangeIdx = reqRange.toInt();
        if (rangeIdx < 0) rangeIdx = 0;
        if (rangeIdx > 6) rangeIdx = 6;
    }
    if (reqEnd.length() > 0) {
        effectiveEnd = (time_t)reqEnd.toInt();
        if (effectiveEnd > now) effectiveEnd = now;
    }
    decimation = rangeDecimation[rangeIdx];
    cutoff = (rangeIdx == 6) ? 0 : (effectiveEnd - rangeDuration[rangeIdx]);

    /* ── Lista de arquivos a ler ────────────────────────────────────────── */
    std::vector<String> filesToRead;
    if (rangeIdx >= 4) {
        /* 1M, 1A, MAX: lista TODOS os arquivos no diretorio (filtra por epoch
         * depois). Evita iterar 365× exists() em vao. */
        ReadGuard rg(_storageRef);
        Dir dir = LittleFS.openDir(DIR_HISTORY);
        while (dir.next()) {
            if (dir.fileName().endsWith(HISTORY_FILE_EXT)) {
                filesToRead.push_back(String(DIR_HISTORY) + "/" + dir.fileName());
            }
        }
        std::sort(filesToRead.begin(), filesToRead.end());  /* YYYYMMDD ordena cronologico */
    } else {
        int daysToLoad = 1;
        switch (rangeIdx) {
            case 0: case 1: case 2: daysToLoad = 1; break;
            case 3: daysToLoad = 7; break;
        }
        /* Cross-midnight: pode precisar de +1 dia */
        if (rangeIdx <= 2) {
            struct tm etm; localtime_r(&effectiveEnd, &etm);
            etm.tm_hour = 0; etm.tm_min = 0; etm.tm_sec = 0;
            time_t eMidnight = mktime(&etm);
            if (cutoff < eMidnight) daysToLoad++;
        }
        for (int d = daysToLoad - 1; d >= 0; d--) {
            time_t targetDay = effectiveEnd - (d * 86400);
            struct tm timeinfo; localtime_r(&targetDay, &timeinfo);
            char defPath[40];
            snprintf(defPath, sizeof(defPath), "%s/%04d%02d%02d%s",
                     DIR_HISTORY,
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     HISTORY_FILE_EXT);
            filesToRead.push_back(String(defPath));
        }
    }

    /* ── Stats acumulados (T do conjunto, H do ambient) ─────────────────── */
    float realMinT = 1000.0f, realMaxT = -1000.0f;
    time_t tsRealMinT = 0, tsRealMaxT = 0;

    /* ── Resposta: header + sensors[] + data[] streaming ────────────────── */
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    {
        char metaBuf[160];
        snprintf(metaBuf, sizeof(metaBuf),
            "{\"cutoff\":%lu,\"end\":%lu,\"now\":%lu,\"rangeUsed\":%d,\"sensors\":[",
            (unsigned long)cutoff, (unsigned long)effectiveEnd, (unsigned long)now, rangeIdx);
        safeSend(metaBuf);
    }

    /* sensors[] — emitir metadados */
    {
        const SystemConfig& cfg = _storageRef->getConfig();
        for (int i = 0; i < sensorCount; i++) {
            int id = sensorIds[i];
            char b[160];
            const char* hwId; const char* name; const char* type;
            bool hasH;
            if (id == -1) {
                hwId = cfg.ambientSensor.hwId;
                name = cfg.ambientSensor.friendlyName;
                type = "ambient"; hasH = true;
            } else {
                hwId = cfg.sensors[id].hwId;
                name = cfg.sensors[id].friendlyName;
                type = "ds18b20"; hasH = false;
            }
            /* Escape minimo: aspas duplas viram \" */
            char nameEsc[40]; size_t k = 0;
            for (size_t j = 0; name[j] && k < sizeof(nameEsc) - 2; j++) {
                if (name[j] == '"' || name[j] == '\\') { nameEsc[k++] = '\\'; }
                nameEsc[k++] = name[j];
            }
            nameEsc[k] = '\0';
            snprintf(b, sizeof(b),
                "%s{\"id\":%d,\"hwId\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"hasH\":%s}",
                (i == 0) ? "" : ",", id, hwId, nameEsc, type, hasH ? "true" : "false");
            safeSend(b);
        }
    }
    safeSend("],\"data\":[");

    bool firstPoint = true;
    static char chunkBuf[2048];
    chunkBuf[0] = '\0';
    int chunkLen = 0;
    bool aborted = false;
    int lineIdx = 0;

    for (size_t fi = 0; fi < filesToRead.size() && !aborted; fi++) {
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
        if (!fileOk) continue;

        HistoryFileHeaderV2 hdr;
        bool headerOk = false;
        {
            ReadGuard rg(_storageRef);
            if (f.size() >= HIST_V2_HEADER_SIZE) {
                f.seek(0);
                if (f.read((uint8_t*)&hdr, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
                    headerOk = (memcmp(hdr.magic, HIST_V2_MAGIC, 4) == 0 &&
                                hdr.version == HIST_V2_VERSION &&
                                hdr.anchorPeriod > 0);
                }
            }
        }
        if (!headerOk) { ReadGuard rg(_storageRef); f.close(); continue; }

        HistoryCodecState rdState;
        historyCodecReset(rdState);
        uint16_t anchorPeriod = hdr.anchorPeriod;
        uint8_t  rdBuf[256];
        size_t   rdFilled = 0;
        bool fileHasMore = true;

        while (fileHasMore && !aborted) {
            if (isClientGone() || isHandlerOvertime()) {
                LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
                aborted = true; break;
            }

            BinaryHistoryRecord batch[20];
            int batchCount = 0;
            {
                ReadGuard rg(_storageRef);
                while (batchCount < 20) {
                    if (rdFilled < HIST_V2_MAX_DELTA_SIZE && f.available() > 0) {
                        int r = f.read(rdBuf + rdFilled, sizeof(rdBuf) - rdFilled);
                        if (r > 0) rdFilled += (size_t)r;
                    }
                    if (rdFilled == 0) break;
                    bool isAnchor = (rdState.recordsSinceAnchor == 0) ||
                                    (rdState.recordsSinceAnchor == anchorPeriod);
                    size_t consumed = historyDecodeRecord(rdBuf, rdFilled, rdState, batch[batchCount], isAnchor);
                    if (consumed == 0) break;
                    memmove(rdBuf, rdBuf + consumed, rdFilled - consumed);
                    rdFilled -= consumed;
                    batchCount++;
                }
                fileHasMore = (rdFilled > 0 || f.available() > 0);
            }

            for (int bi = 0; bi < batchCount && !aborted; bi++) {
                const BinaryHistoryRecord& rec = batch[bi];
                time_t ts = (time_t)rec.epoch;

                if (cutoff > 0 && ts < cutoff) continue;
                if (ts > effectiveEnd) { fileHasMore = false; break; }

                /* Stats T do conjunto (pre-decimacao) */
                for (int s = 0; s < sensorCount; s++) {
                    int id = sensorIds[s];
                    int16_t raw = (id == -1) ? rec.ambientTemp : rec.sensors[id];
                    if (raw == HIST_NAN_SENTINEL) continue;
                    float v = BinaryHistoryRecord::i16ToFloat(raw);
                    if (v < realMinT) { realMinT = v; tsRealMinT = ts; }
                    if (v > realMaxT) { realMaxT = v; tsRealMaxT = ts; }
                }

                lineIdx++;
                if (lineIdx % decimation != 0) continue;

                /* Emite ponto: {"t":epoch,"v":[v0,v1,...]} ou null para NAN. */
                char pointBuf[256];
                int pos = 0;
                pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos,
                                "%s{\"t\":%lu,\"v\":[", firstPoint ? "" : ",",
                                (unsigned long)ts);
                for (int s = 0; s < sensorCount; s++) {
                    int id = sensorIds[s];
                    int16_t raw = (id == -1) ? rec.ambientTemp : rec.sensors[id];
                    if (s > 0) pointBuf[pos++] = ',';
                    if (raw == HIST_NAN_SENTINEL) {
                        pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos, "null");
                    } else {
                        float v = BinaryHistoryRecord::i16ToFloat(raw);
                        const char* sg = (v < 0) ? "-" : "";
                        int vInt = abs((int)v);
                        int vDec = abs((int)(v * 100.0f) % 100);
                        pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos,
                                        "%s%d.%02d", sg, vInt, vDec);
                    }
                }
                pointBuf[pos++] = ']';
                /* h: ambient hum se incluso e valido */
                if (ambientIncluded && rec.ambientHum != HIST_NAN_SENTINEL) {
                    float h = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);
                    int hInt = abs((int)h);
                    int hDec = abs((int)(h * 10.0f) % 10);
                    pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos,
                                    ",\"h\":%d.%01d", hInt, hDec);
                }
                pointBuf[pos++] = '}';
                pointBuf[pos] = '\0';

                int pLen = pos;
                if (chunkLen + pLen >= (int)sizeof(chunkBuf) - 1) {
                    if (!safeSend(chunkBuf)) { aborted = true; break; }
                    chunkBuf[0] = '\0'; chunkLen = 0;
                    delay(5); watchdog_update();
                }
                memcpy(chunkBuf + chunkLen, pointBuf, pLen + 1);
                chunkLen += pLen;
                firstPoint = false;

                if (chunkLen > 1500) {
                    if (!safeSend(chunkBuf)) { aborted = true; break; }
                    chunkBuf[0] = '\0'; chunkLen = 0;
                    delay(5); watchdog_update();
                }
            }
            if (_lightYieldCb) _lightYieldCb();
            delay(5); watchdog_update();
        }
        { ReadGuard rg(_storageRef); f.close(); }
        (void)ambientPos;  /* reservado p/ uso futuro (front ja sabe a posicao) */
    }

    if (!aborted) {
        if (chunkLen > 0) safeSend(chunkBuf);
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


/* =========================================================================== */
/*       F-CSV.2: GET /api/export/history.bin?from=<epoch>&to=<epoch>          */
/* =========================================================================== */
/* Emite bundle .simx kind='H' (CRC32 trailer) para o browser expandir em CSV
 * localmente. Cap hard de 31 dias. PAYLOAD = N x BinaryHistoryRecord (28 B
 * packed) cru, sem reformatar. Filtragem de sensores fica no client.
 *
 * Formato (todos LE):
 *   HEADER (32 B): "SIMX" | ver=1 | kind='H' | rsv | recSize=28 | rsv |
 *                  rangeFrom u32 | rangeTo u32 | sensorTblSize u32 | rsv x2
 *   SENSOR_TABLE (variavel): per slot ativo: idx u8, hwidLen u8, hwid[],
 *                            friendlyLen u8, friendly[]
 *   PAYLOAD (variavel): N x BinaryHistoryRecord (28 B cada)
 *   TRAILER (4 B): crc32 u32 (sobre HEADER+TABLE+PAYLOAD)
 */
namespace {
constexpr uint32_t SIMX_MAX_RANGE_SECS = 31u * 86400u;  /* cap 31 dias */
struct __attribute__((packed)) SimxHeader {
    char     magic[4];          /* "SIMX" */
    uint8_t  version;           /* 1 */
    uint8_t  kind;              /* 'H' history, 'L' logs */
    uint16_t reserved0;
    uint16_t recordSize;        /* 28 (history) ou 12 (logs) */
    uint16_t reserved1;
    uint32_t rangeFrom;
    uint32_t rangeTo;
    uint32_t sensorTableSize;
    uint32_t reserved2;
    uint32_t reserved3;
};
static_assert(sizeof(SimxHeader) == 32, "SimxHeader deve ter 32 bytes");
}

void WebManager::handleApiExportHistory() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_HISTORY)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    if (!_server.hasArg("from") || !_server.hasArg("to")) {
        _server.send(400, "application/json", "{\"error\":\"Missing from/to params\"}"); return;
    }
    uint32_t rangeFrom = (uint32_t)strtoul(_server.arg("from").c_str(), nullptr, 10);
    uint32_t rangeTo   = (uint32_t)strtoul(_server.arg("to").c_str(),   nullptr, 10);
    if (rangeFrom == 0 || rangeTo == 0 || rangeFrom >= rangeTo) {
        _server.send(400, "application/json", "{\"error\":\"Invalid range\"}"); return;
    }
    if (rangeTo - rangeFrom > SIMX_MAX_RANGE_SECS) {
        _server.send(400, "application/json", "{\"error\":\"Range exceeds 31 days\"}"); return;
    }

    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}"); return;
    }
    if (__atomic_exchange_n(&_inHistoryHandler, true, __ATOMIC_ACQ_REL)) {
        _server.send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
    }
    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) {
        __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
        _server.send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
    }

    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;
    if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str());

    /* Monta SENSOR_TABLE em buffer RAM (= soma de todos os slots ativos +
     * ambient). Cap conservador: 11 slots x (1+1+16+1+32) = 561 B max. */
    uint8_t  sensorTbl[640];
    size_t   sensorTblLen = 0;
    {
        const SystemConfig& cfg = _storageRef->getConfig();
        auto appendSensor = [&](uint8_t idx, const char* hwId, const char* friendly) {
            size_t hwLen = hwId ? strnlen(hwId, 16) : 0;
            size_t frLen = friendly ? strnlen(friendly, 32) : 0;
            size_t need = 1 + 1 + hwLen + 1 + frLen;
            if (sensorTblLen + need > sizeof(sensorTbl)) return;
            sensorTbl[sensorTblLen++] = idx;
            sensorTbl[sensorTblLen++] = (uint8_t)hwLen;
            memcpy(sensorTbl + sensorTblLen, hwId, hwLen); sensorTblLen += hwLen;
            sensorTbl[sensorTblLen++] = (uint8_t)frLen;
            memcpy(sensorTbl + sensorTblLen, friendly, frLen); sensorTblLen += frLen;
        };
        for (int i = 0; i < MAX_SENSORS; i++) {
            const SensorRecord& s = cfg.sensors[i];
            if (!s.active) continue;
            appendSensor((uint8_t)i, s.hwId, s.friendlyName);
        }
        /* Ambient: idx especial = 0xFE; usa hwId do ambient (em geral vazio) +
         * friendlyName. So' emite se ambient ativo. */
        if (cfg.ambientSensor.active) {
            appendSensor(0xFE, cfg.ambientSensor.hwId, cfg.ambientSensor.friendlyName);
        }
    }

    /* Acumulador de CRC32 streaming (cobre HEADER + TABLE + PAYLOAD; trailer e' o CRC). */
    uint32_t crc = crc32_init();

    _server.sendHeader("Content-Disposition", "attachment; filename=\"simut_history.simx\"");
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/octet-stream", "");

    /* Emite HEADER */
    SimxHeader hdr = {};
    memcpy(hdr.magic, "SIMX", 4);
    hdr.version = 0x01;
    hdr.kind = 'H';
    hdr.recordSize = (uint16_t)sizeof(BinaryHistoryRecord);  /* 28 */
    hdr.rangeFrom = rangeFrom;
    hdr.rangeTo = rangeTo;
    hdr.sensorTableSize = (uint32_t)sensorTblLen;
    crc = crc32_update(crc, (const uint8_t*)&hdr, sizeof(hdr));
    safeSend((const char*)&hdr, sizeof(hdr));

    /* Emite SENSOR_TABLE */
    if (sensorTblLen > 0) {
        crc = crc32_update(crc, sensorTbl, sensorTblLen);
        safeSend((const char*)sensorTbl, sensorTblLen);
    }

    /* Itera arquivos de history no range. Day-aligned em LOCALTIME (arquivos
     * sao nomeados YYYYMMDD pela data local, nao UTC) — fix para o bug em que
     * o dia atual ficava de fora quando o usuario esta em fuso != UTC. */
    bool aborted = false;
    time_t fromT = (time_t)rangeFrom;
    time_t toDayEnd = (time_t)rangeTo;
    struct tm tFrom; localtime_r(&fromT, &tFrom);
    tFrom.tm_hour = 0; tFrom.tm_min = 0; tFrom.tm_sec = 0;
    time_t dayStart = mktime(&tFrom);
    while (dayStart <= toDayEnd && !aborted) {
        /* Calcula nextDay ANTES de qualquer continue — garantia contra loop
         * infinito quando arquivo nao existe (caso do stress test reproduziu
         * range 30d num flash com so' 21d → WDT 8s reboot). */
        struct tm dtNext; localtime_r(&dayStart, &dtNext);
        dtNext.tm_mday += 1;
        dtNext.tm_hour = 0; dtNext.tm_min = 0; dtNext.tm_sec = 0;
        time_t nextDay = mktime(&dtNext);
        time_t curDay  = dayStart;
        dayStart = nextDay;  /* avanca SEMPRE — proximos continues sao seguros */

        /* Aborto cooperativo: client foi-se ou estouramos deadline */
        if (isClientGone() || isHandlerOvertime()) { aborted = true; break; }

        struct tm dtm; localtime_r(&curDay, &dtm);
        char dayPath[40];
        snprintf(dayPath, sizeof(dayPath), "%s/%04d%02d%02d%s",
                 DIR_HISTORY,
                 dtm.tm_year + 1900, dtm.tm_mon + 1, dtm.tm_mday,
                 HISTORY_FILE_EXT);

        File f;
        bool fileOk = false;
        {
            ReadGuard rg(_storageRef);
            if (LittleFS.exists(dayPath)) {
                f = LittleFS.open(dayPath, "r");
                fileOk = (bool)f;
            }
        }
        if (!fileOk) continue;

        /* Valida header v2 */
        HistoryFileHeaderV2 hdrV2;
        bool headerOk = false;
        {
            ReadGuard rg(_storageRef);
            if (f.size() >= HIST_V2_HEADER_SIZE) {
                f.seek(0);
                if (f.read((uint8_t*)&hdrV2, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
                    headerOk = (memcmp(hdrV2.magic, HIST_V2_MAGIC, 4) == 0 &&
                                hdrV2.version == HIST_V2_VERSION &&
                                hdrV2.anchorPeriod > 0);
                }
            }
        }
        if (!headerOk) { ReadGuard rg(_storageRef); f.close(); continue; }

        HistoryCodecState rdState;
        historyCodecReset(rdState);
        uint16_t anchorPeriod = hdrV2.anchorPeriod;

        uint8_t rdBuf[256];
        size_t  rdFilled = 0;
        bool fileHasMore = true;

        while (fileHasMore && !aborted) {
            if (isClientGone() || isHandlerOvertime()) {
                LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
                aborted = true; break;
            }

            BinaryHistoryRecord batch[20];
            int batchCount = 0;
            {
                ReadGuard rg(_storageRef);
                while (batchCount < 20) {
                    if (rdFilled < HIST_V2_MAX_DELTA_SIZE && f.available() > 0) {
                        int r = f.read(rdBuf + rdFilled, sizeof(rdBuf) - rdFilled);
                        if (r > 0) rdFilled += (size_t)r;
                    }
                    if (rdFilled == 0) break;
                    bool isAnchor = (rdState.recordsSinceAnchor == 0) ||
                                    (rdState.recordsSinceAnchor == anchorPeriod);
                    size_t consumed = historyDecodeRecord(rdBuf, rdFilled, rdState, batch[batchCount], isAnchor);
                    if (consumed == 0) break;
                    memmove(rdBuf, rdBuf + consumed, rdFilled - consumed);
                    rdFilled -= consumed;
                    batchCount++;
                }
                fileHasMore = (rdFilled > 0 || f.available() > 0);
            }

            for (int bi = 0; bi < batchCount && !aborted; bi++) {
                const BinaryHistoryRecord& rec = batch[bi];
                if (rec.epoch < rangeFrom) continue;
                if (rec.epoch > rangeTo)   { fileHasMore = false; break; }

                crc = crc32_update(crc, (const uint8_t*)&rec, sizeof(rec));
                if (!safeSend((const char*)&rec, sizeof(rec))) { aborted = true; break; }
            }

            if (_lightYieldCb) _lightYieldCb();
            delay(2);
            watchdog_update();
        }
        { ReadGuard rg(_storageRef); f.close(); }
        /* dayStart ja' foi avancado no topo do loop (proteção contra loop infinito) */
    }

    /* TRAILER: CRC32 final */
    if (!aborted) {
        uint32_t crcFinal = crc32_final(crc);
        safeSend((const char*)&crcFinal, sizeof(crcFinal));
        safeSend("");
    }

    _handlerDeadline = savedDeadline;
    if (_displayRef) _displayRef->setWebBusy(false);
    __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
}

/* =========================================================================== */
/*    F-CSV.3: GET /api/export/logs.bin?from=<epoch>&to=<epoch>&level=...      */
/* =========================================================================== */
/* Emite bundle .simx kind='L' (CRC32 trailer). PAYLOAD = N x CompactLogRecord
 * (12 B packed) cru, igual ao /api/logs existente, mas filtrado por epoch e
 * level. Cap hard 31 dias. SENSOR_TABLE vazia (sensorTableSize=0).
 *
 *   level=err -> so' LOG_ERROR (3)
 *   level=inf -> so' LOG_INFO  (1)
 *   level=all -> tudo (default)
 *
 * Cliente decodifica records, faz lookup `code` -> texto via /api/lang e
 * gera CSV no browser.
 */
void WebManager::handleApiExportLogs() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_LOGS)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    if (!_server.hasArg("from") || !_server.hasArg("to")) {
        _server.send(400, "application/json", "{\"error\":\"Missing from/to params\"}"); return;
    }
    uint32_t rangeFrom = (uint32_t)strtoul(_server.arg("from").c_str(), nullptr, 10);
    uint32_t rangeTo   = (uint32_t)strtoul(_server.arg("to").c_str(),   nullptr, 10);
    if (rangeFrom == 0 || rangeTo == 0 || rangeFrom >= rangeTo) {
        _server.send(400, "application/json", "{\"error\":\"Invalid range\"}"); return;
    }
    if (rangeTo - rangeFrom > SIMX_MAX_RANGE_SECS) {
        _server.send(400, "application/json", "{\"error\":\"Range exceeds 31 days\"}"); return;
    }

    /* Filtro de level: 0 = all, 1 = INFO only, 3 = ERROR only.
     * Mantemos o codigo numerico do LogLevel para comparacao direta. */
    String levelArg = _server.hasArg("level") ? _server.arg("level") : "all";
    uint8_t levelFilter = 0xFF;  /* 0xFF = sem filtro */
    if      (levelArg == "err") levelFilter = LOG_ERROR;
    else if (levelArg == "inf") levelFilter = LOG_INFO;
    else if (levelArg != "all") {
        _server.send(400, "application/json", "{\"error\":\"Invalid level (use err|inf|all)\"}"); return;
    }

    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}"); return;
    }
    if (__atomic_exchange_n(&_inExportLogsHandler, true, __ATOMIC_ACQ_REL)) {
        _server.send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
    }
    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) {
        __atomic_store_n(&_inExportLogsHandler, false, __ATOMIC_RELEASE);
        _server.send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
    }

    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;
    if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str());

    /* Acumulador de CRC32 streaming (cobre HEADER + PAYLOAD; sensorTblSize = 0). */
    uint32_t crc = crc32_init();

    _server.sendHeader("Content-Disposition", "attachment; filename=\"simut_logs.simx\"");
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/octet-stream", "");

    /* Emite HEADER */
    SimxHeader hdr = {};
    memcpy(hdr.magic, "SIMX", 4);
    hdr.version = 0x01;
    hdr.kind = 'L';
    hdr.recordSize = (uint16_t)LOG_RECORD_SIZE;  /* 12 */
    hdr.rangeFrom = rangeFrom;
    hdr.rangeTo = rangeTo;
    hdr.sensorTableSize = 0;
    crc = crc32_update(crc, (const uint8_t*)&hdr, sizeof(hdr));
    safeSend((const char*)&hdr, sizeof(hdr));

    /* Itera /system.old.blog primeiro (mais antigo) depois /system.blog */
    bool aborted = false;
    auto streamFiltered = [&](const char* path) -> bool {
        File f;
        {
            ReadGuard rg(_storageRef);
            if (!LittleFS.exists(path)) return true;
            f = LittleFS.open(path, "r");
        }
        if (!f) return true;

        uint8_t batch[480];  /* 40 records x 12B */
        while (f.available() >= LOG_RECORD_SIZE && !aborted) {
            if (isClientGone() || isHandlerOvertime()) {
                LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
                aborted = true; break;
            }

            int bytesRead = 0;
            {
                ReadGuard rg(_storageRef);
                while (f.available() >= LOG_RECORD_SIZE &&
                       bytesRead + LOG_RECORD_SIZE <= (int)sizeof(batch)) {
                    if (f.read(batch + bytesRead, LOG_RECORD_SIZE) == LOG_RECORD_SIZE) {
                        bytesRead += LOG_RECORD_SIZE;
                    }
                }
            }

            for (int off = 0; off < bytesRead && !aborted; off += LOG_RECORD_SIZE) {
                const CompactLogRecord* rec = (const CompactLogRecord*)(batch + off);
                if (rec->epoch < rangeFrom || rec->epoch > rangeTo) continue;
                if (levelFilter != 0xFF && rec->getLevel() != levelFilter) continue;

                crc = crc32_update(crc, (const uint8_t*)rec, LOG_RECORD_SIZE);
                if (!safeSend((const char*)rec, LOG_RECORD_SIZE)) { aborted = true; break; }
            }

            if (_lightYieldCb) _lightYieldCb();
            delay(2);
            watchdog_update();
        }
        { ReadGuard rg(_storageRef); f.close(); }
        return !aborted;
    };

    if (streamFiltered(LOG_FILE_OLD)) {
        streamFiltered(LOG_FILE_CURRENT);
    }

    /* TRAILER: CRC32 final */
    if (!aborted) {
        uint32_t crcFinal = crc32_final(crc);
        safeSend((const char*)&crcFinal, sizeof(crcFinal));
        safeSend("");
    }

    _handlerDeadline = savedDeadline;
    if (_displayRef) _displayRef->setWebBusy(false);
    __atomic_store_n(&_inExportLogsHandler, false, __ATOMIC_RELEASE);
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

    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Admin erased System Logs"));
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
