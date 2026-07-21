/**
 * @file WebManager_History.cpp
 * @brief History/log endpoints: binary history data, log viewer, screenshot, history days.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include "HistoryCodec.h"
#include "ota/backup.h" /* ota::crc32_update for screenshot_chunk */
#include <LittleFS.h>
#include <time.h>

using ReadGuard = StorageManager::ReadGuard;

// Inline insertion sort (replaces std::sort, eliminates qsort ~1.4KB)
static void sortStrings(String* arr, int n, bool descending) {
 for (int i = 1; i < n; i++) {
 String key = arr[i];
 int j = i - 1;
 while (j >= 0 && (descending ? arr[j] < key : key < arr[j])) {
 arr[j + 1] = arr[j]; j--;
 }
 arr[j + 1] = key;
 }
}


/* =========================================================================== */
/* GET /api/history_multi?sensors=<csv>&range=<0..6>&end=<ep> */
/* =========================================================================== */
/* Multi-sensor version of history data endpoint. Returns 1 response with ALL
 * requested series, avoiding overhead of N sequential blocked fetches
 * guarded by the _inHistoryHandler atomic.
 *
 * Args:
 * sensors=-1,0,5 CSV of IDs (-1=ambient, 0..9=DS18B20). Default: -1.
 * range=0..6 Levels: 1h, 6h, 24h, 7d, 1M, 1Y, MAX (=all files).
 * end=<epoch> Anchor (default: now).
 *
 * JSON:
 * {"cutoff":..., "end":..., "now":...,
 * "sensors":[{"id":-1,"hwId":"AMB","name":"...","hasH":true}, ...],
 * "data":[{"t":epoch,"v":[float|null,...],"h":float}, ...],
 * "minT":..., "maxT":..., "tsMinT":..., "tsMaxT":...,
 * "rangeUsed":N}
 *
 * v[] is aligned by index with sensors[]. h only appears if ambient is
 * included and has a valid reading in the record.
 */

/* V4 history multi handler — decodes .sim4 file and emits generic JSON.
 * Called from handleApiHistoryMulti when a V4 file is detected.
 * Static helper keeps the V4 logic self-contained without header changes. */
void WebManager::handleApiHistoryMulti( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_HISTORY)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

 if (TouchPriority::isActive( )) {
 _server.sendHeader("Retry-After", "3");
 _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
 return;
 }
 if (__atomic_exchange_n(&_inHistoryHandler, true, __ATOMIC_ACQ_REL)) {
 _server.send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
 }
 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
 _server.send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
 }

 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;
 if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str( ));

 /* ── Parse sensors=... (CSV of IDs) ─────────────────────────────────── */
 int sensorIds[MAX_SENSORS]; /* up to 16 slots */
 int sensorCount = 0;
 String sArg = _server.hasArg("sensors") ? _server.arg("sensors") : String("10");
 {
 int start = 0;
 while (start < (int)sArg.length( ) && sensorCount < MAX_SENSORS) {
 int comma = sArg.indexOf(',', start);
 String tok = (comma < 0) ? sArg.substring(start) : sArg.substring(start, comma);
 tok.trim( );
 if (tok.length( ) > 0) {
 int id = tok.toInt( );
 /* Accept 0..MAX_SENSORS-1. Filter duplicates. */
 if (id >= 0 && id < MAX_SENSORS) {
 bool dup = false;
 for (int i = 0; i < sensorCount; i++) if (sensorIds[i] == id) { dup = true; break; }
 if (!dup) {
 sensorIds[sensorCount] = id;
 sensorCount++;
 }
 }
 }
 if (comma < 0) break;
 start = comma + 1;
 }
 }
 if (sensorCount == 0) { sensorIds[0] = 10; sensorCount = 1; }

 /* ── Parse range/end ────────────────────────────────────────────────── */
 String reqRange = _server.arg("range");
 String reqEnd = _server.arg("end");

 static const time_t rangeDuration[] = { 3600, 21600, 86400, 604800, 2592000, 31536000, 0 };
 /* Tested decimation=480 for 1Y/MAX but time didn't
 * change — bottleneck is flash read, not JSON emit. Kept at 240
 * for higher graph fidelity. */
 static const int rangeDecimation[] = { 1, 1, 3, 15, 60, 240, 240 };

 time_t now = _netRef->getEpoch( );
 time_t effectiveEnd = now;
 time_t cutoff = 0;
 int decimation = 1;
 int rangeIdx = 2; /* Default: 24h */

 if (reqRange.length( ) > 0) {
 rangeIdx = reqRange.toInt( );
 if (rangeIdx < 0) rangeIdx = 0;
 if (rangeIdx > 6) rangeIdx = 6;
 }
 if (reqEnd.length( ) > 0) {
 effectiveEnd = (time_t)reqEnd.toInt( );
 if (effectiveEnd > now) effectiveEnd = now;
 }
 decimation = rangeDecimation[rangeIdx];
 cutoff = (rangeIdx == 6) ? 0 : (effectiveEnd - rangeDuration[rangeIdx]);


/* ── List of files to read ────────────────────────────────────────── */
 std::vector<String> filesToRead;
 if (rangeIdx >= 4) {
 /* 1M, 1Y, MAX: list ALL files in directory (filter by epoch
 * later). Avoids iterating 365x exists( ) in vain. */
 ReadGuard rg(_storageRef);
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 if (dir.fileName( ).endsWith(HISTORY_FILE_EXT) || dir.fileName( ).endsWith(HISTORY_V4_FILE_EXT)) {
 filesToRead.push_back(String(DIR_HISTORY) + "/" + dir.fileName( ));
 }
 }
 sortStrings(filesToRead.data( ), (int)filesToRead.size( ), false); /* YYYYMMDD ascending */
 } else {
 int daysToLoad = 1;
 switch (rangeIdx) {
 case 0: case 1: case 2: daysToLoad = 1; break;
 case 3: daysToLoad = 7; break;
 }
 /* Cross-midnight: may need +1 day */
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

 /* ── Accumulated stats (T of set, H of ambient) ─────────────────── */
 float realMinT = 1000.0f, realMaxT = -1000.0f;
 time_t tsRealMinT = 0, tsRealMaxT = 0;

 /* ── Response: header + sensors[] + data[] streaming ────────────────── */
 _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 _server.send(200, "application/json", "");

 {
 char metaBuf[160];
 snprintf(metaBuf, sizeof(metaBuf),
 "{\"cutoff\":%lu,\"end\":%lu,\"now\":%lu,\"rangeUsed\":%d,\"sensors\":[",
 (unsigned long)cutoff, (unsigned long)effectiveEnd, (unsigned long)now, rangeIdx);
 safeSend(metaBuf);
 }

 /* sensors[] — emit metadata */
 {
 const SystemConfig& cfg = _storageRef->getConfig( );
 for (int i = 0; i < sensorCount; i++) {
 int id = sensorIds[i];
 char b[160];
 const char* hwId; const char* name; const char* type;
 bool hasH;
 {
 hwId = cfg.sensors[id].hwId;
 name = cfg.sensors[id].friendlyName;
 type = sensorHasHumidity((SensorType)cfg.sensors[id].sensorType) ? "ambient" : "ds18b20";
 hasH = sensorHasHumidity((SensorType)cfg.sensors[id].sensorType);
 }
 /* Minimal escape: double quotes become \" */
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

 for (size_t fi = 0; fi < filesToRead.size( ) && !aborted; fi++) {
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

	 /* V4 detection: .sim4 files use universal format */
	 bool _v4 = path.endsWith(HISTORY_V4_FILE_EXT);
	 if (!_v4) { ReadGuard rg(_storageRef); if (f.size() >= 4) { char m[4]; f.seek(0); if (f.read((uint8_t*)m,4)==4) _v4=(memcmp(m,HIST_V4_MAGIC,4)==0); } }
	 if (_v4) {
	 /* ── V4 inline decode + emit ─────────────────────── */
	 HistV4State v4st;
	 {
	 ReadGuard rg(_storageRef);
	 f.seek(0); uint8_t hdrBuf[HIST_V4_MAX_HEADER];
	 int hdrRead = f.read(hdrBuf, sizeof(hdrBuf));
	 if (hdrRead < (int)HIST_V4_HEADER_FIXED ||
	 histV4ReadHeaderBuf(hdrBuf, (size_t)hdrRead, v4st) == 0) { f.close(); continue; }
	 }

	 uint8_t rdBuf[HIST_V4_READ_BUF]; size_t rdFilled = 0;
	 int64_t v4vals[HIST_V4_MAX_MEASUREMENTS]; uint32_t v4epoch;
	 bool fileHasMoreV4 = true;

	 while (fileHasMoreV4 && !aborted) {
	 if (isClientGone() || isHandlerOvertime()) { aborted = true; break; }
	 {
	 ReadGuard rg(_storageRef);
	 if (rdFilled < v4st.anchorByteSize && f.available() > 0) {
	 int r = f.read(rdBuf + rdFilled, sizeof(rdBuf) - rdFilled);
	 if (r > 0) rdFilled += (size_t)r;
	 }
	 if (rdFilled == 0) { fileHasMoreV4 = false; break; }
	 size_t c = histV4DecodeNext(rdBuf, rdFilled, v4st, v4vals, &v4epoch);
	 if (c == 0) break;
	 memmove(rdBuf, rdBuf + c, rdFilled - c); rdFilled -= c;
	 }

	 time_t ts = (time_t)v4epoch;
	 if (cutoff > 0 && ts < cutoff) continue;
	 if (ts > effectiveEnd) { fileHasMoreV4 = false; break; }

	 for (uint8_t m = 0; m < v4st.measureCount; m++) {
	 if (histV4IsNan(v4vals[m], v4st.measures[m].bitWidth)) continue;
	 float v = histV4ToFloat(v4vals[m], v4st.measures[m]);
	 if (v < realMinT) { realMinT = v; tsRealMinT = ts; }
	 if (v > realMaxT) { realMaxT = v; tsRealMaxT = ts; }
	 }

	 lineIdx++;
	 if (lineIdx % decimation != 0) continue;

	 char ptBuf[1024]; int pp = 0;
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp,
	 "%s{\"t\":%lu,\"v\":{", firstPoint ? "" : ",", (unsigned long)ts);
	 bool fk = true;
	 for (uint8_t m = 0; m < v4st.measureCount; m++) {
	 if (histV4IsNan(v4vals[m], v4st.measures[m].bitWidth)) continue;
	 float v = histV4ToFloat(v4vals[m], v4st.measures[m]);
	 char key[32]; uint8_t si = v4st.measures[m].sensorIdx;
	 char hwId[17];
	 histV4StrPoolGet(hwId, sizeof(hwId), v4st.strPool,
	 v4st.sensors[si].hwIdOffset, v4st.sensors[si].hwIdLen);
	 histV4MakeMeasKey(key, sizeof(key), v4st.measures[m].channel, hwId);
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp,
	 "%s\"%s\":%.2f", fk ? "" : ",", key, (double)v);
	 fk = false;
	 }
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp, "}}");
	 safeSend(ptBuf);
	 firstPoint = false;
	 }
	 { ReadGuard rg(_storageRef); f.close(); }
	 continue;
	 }

 HistoryFileHeaderV2 hdr;
 bool headerOk = false;
 {
 ReadGuard rg(_storageRef);
 if (f.size( ) >= HIST_V2_HEADER_SIZE) {
 f.seek(0);
 if (f.read((uint8_t*)&hdr, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
 headerOk = (memcmp(hdr.magic, HIST_V2_MAGIC, 4) == 0 &&
 (hdr.version == HIST_V2_VERSION || hdr.version == HIST_V3_VERSION) &&
 hdr.anchorPeriod > 0);
 }
 }
 }
 if (!headerOk) { ReadGuard rg(_storageRef); f.close( ); continue; }

 HistoryCodecState rdState;
 historyCodecReset(rdState);
 rdState.fileVersion = hdr.version; /* MUST set before decode — auto-detect unreliable */
 uint16_t anchorPeriod = hdr.anchorPeriod;
 uint8_t rdBuf[256];
 size_t rdFilled = 0;
 bool fileHasMore = true;

 while (fileHasMore && !aborted) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
 aborted = true; break;
 }

 BinaryHistoryRecord batch[20];
 int batchCount = 0;
 {
 ReadGuard rg(_storageRef);
 while (batchCount < 20) {
 if (rdFilled < HIST_V2_MAX_DELTA_SIZE && f.available( ) > 0) {
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
 fileHasMore = (rdFilled > 0 || f.available( ) > 0);
 }

 for (int bi = 0; bi < batchCount && !aborted; bi++) {
 const BinaryHistoryRecord& rec = batch[bi];
 time_t ts = (time_t)rec.epoch;

 if (cutoff > 0 && ts < cutoff) continue;
 if (ts > effectiveEnd) { fileHasMore = false; break; }

 /* T stats of set (pre-decimation) */
 for (int s = 0; s < sensorCount; s++) {
 int id = sensorIds[s];
 int16_t raw = rec.sensors[id];
 if (raw == HIST_NAN_SENTINEL) continue;
 float v = BinaryHistoryRecord::i16ToFloat(raw);
 if (v < realMinT) { realMinT = v; tsRealMinT = ts; }
 if (v > realMaxT) { realMaxT = v; tsRealMaxT = ts; }
 }

 lineIdx++;
 if (lineIdx % decimation != 0) continue;

 /* Emit point: {"t":epoch,"v":[v0,v1,...]} or null for NAN. */
 char pointBuf[512]; /* v3 needs room for hv[] + pressure fields */
 int pos = 0;
 pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos,
 "%s{\"t\":%lu,\"v\":[", firstPoint ? "" : ",",
 (unsigned long)ts);
 for (int s = 0; s < sensorCount; s++) {
 int id = sensorIds[s];
 int16_t raw = rec.sensors[id];
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
 /* h: ambient humidity (backward compat — always from ambientHum field) */
 if (rec.ambientHum != HIST_NAN_SENTINEL) {
 float h = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);
 int hInt = abs((int)h);
 int hDec = abs((int)(h * 10.0f) % 10);
 pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos,
 ",\"h\":%d.%01d", hInt, hDec);
 }
 /* hv: per-slot humidity array (aligned with sensors[]) */
 {
 pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos, ",\"hv\":[");
 for (int s = 0; s < sensorCount; s++) {
 int id = sensorIds[s];
 int16_t hRaw = rec.humidity[id];
 if (s > 0) pointBuf[pos++] = ',';
 if (hRaw == HIST_NAN_SENTINEL) {
 pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos, "null");
 } else {
 float hv = BinaryHistoryRecord::i16ToFloat(hRaw);
 pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos,
 "%.1f", hv);
 }
 }
 pointBuf[pos++] = ']';
 }
 /* p: atmospheric pressure (hPa) */
 if (rec.pressure != HIST_NAN_SENTINEL) {
 float p = BinaryHistoryRecord::i16ToFloatx10(rec.pressure);
 int pInt = abs((int)p);
 int pDec = abs((int)(p * 10.0f) % 10);
 pos += snprintf(pointBuf + pos, sizeof(pointBuf) - pos,
 ",\"p\":%d.%01d", pInt, pDec);
 }
 pointBuf[pos++] = '}';
 pointBuf[pos] = '\0';

 int pLen = pos;
 if (chunkLen + pLen >= (int)sizeof(chunkBuf) - 1) {
 if (!safeSend(chunkBuf)) { aborted = true; break; }
 chunkBuf[0] = '\0'; chunkLen = 0;
 delay(5); watchdog_update( );
 }
 memcpy(chunkBuf + chunkLen, pointBuf, pLen + 1);
 chunkLen += pLen;
 firstPoint = false;

 if (chunkLen > 1500) {
 if (!safeSend(chunkBuf)) { aborted = true; break; }
 chunkBuf[0] = '\0'; chunkLen = 0;
 delay(5); watchdog_update( );
 }
 }
 if (_lightYieldCb) _lightYieldCb( );
 delay(5); watchdog_update( );
 }
 { ReadGuard rg(_storageRef); f.close( ); }
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
/* GET /api/export/history.bin?from=<epoch>&to=<epoch> */
/* =========================================================================== */
/* Emits .simx bundle kind='H' (CRC32 trailer) for the browser to expand into CSV
 * locally. Hard cap of 31 days. PAYLOAD = N x BinaryHistoryRecord (74 B
 * packed) raw, without reformatting. Sensor filtering is on the client.
 *
 * Format (all LE):
 * HEADER (32 B): "SIMX" | ver=1 | kind='H' | rsv | recSize=74 | rsv |
 * rangeFrom u32 | rangeTo u32 | sensorTblSize u32 | rsv x2
 * SENSOR_TABLE (variable): per active slot: idx u8, hwidLen u8, hwid[],
 * friendlyLen u8, friendly[]
 * PAYLOAD (variable): N x BinaryHistoryRecord (74 B each)
 * TRAILER (4 B): crc32 u32 (over HEADER+TABLE+PAYLOAD)
 */
namespace {
constexpr uint32_t SIMX_MAX_RANGE_SECS = 31u * 86400u; /* cap 31 days */
struct __attribute__((packed)) SimxHeader {
 char magic[4]; /* "SIMX" */
 uint8_t version; /* 1 */
 uint8_t kind; /* 'H' history, 'L' logs */
 uint16_t reserved0;
 uint16_t recordSize; /* 28 (history) or 12 (logs) */
 uint16_t reserved1;
 uint32_t rangeFrom;
 uint32_t rangeTo;
 uint32_t sensorTableSize;
 uint32_t reserved2;
 uint32_t reserved3;
};
static_assert(sizeof(SimxHeader) == 32, "SimxHeader must be 32 bytes");
}

void WebManager::handleApiExportHistory( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_HISTORY)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

 if (!_server.hasArg("from") || !_server.hasArg("to")) {
 _server.send(400, "application/json", "{\"error\":\"Missing from/to params\"}"); return;
 }
 uint32_t rangeFrom = (uint32_t)strtoul(_server.arg("from").c_str( ), nullptr, 10);
 uint32_t rangeTo = (uint32_t)strtoul(_server.arg("to").c_str( ), nullptr, 10);
 if (rangeFrom == 0 || rangeTo == 0 || rangeFrom >= rangeTo) {
 _server.send(400, "application/json", "{\"error\":\"Invalid range\"}"); return;
 }
 if (rangeTo - rangeFrom > SIMX_MAX_RANGE_SECS) {
 _server.send(400, "application/json", "{\"error\":\"Range exceeds 31 days\"}"); return;
 }

 if (TouchPriority::isActive( )) {
 _server.sendHeader("Retry-After", "3");
 _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}"); return;
 }
 if (__atomic_exchange_n(&_inHistoryHandler, true, __ATOMIC_ACQ_REL)) {
 _server.send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
 }
 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
 _server.send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
 }

 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;
 if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str( ));

 /* Build SENSOR_TABLE in RAM buffer (= sum of all active slots +
 * ambient). Conservative cap: 11 slots x (1+1+16+1+32) = 561 B max. */
 uint8_t sensorTbl[640];
 size_t sensorTblLen = 0;
 {
 const SystemConfig& cfg = _storageRef->getConfig( );
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
 /* Slot 10 (humidity-capable sensor): special idx = 0xFE for backward compat */
 if (cfg.sensors[10].active) {
 appendSensor(0xFE, cfg.sensors[10].hwId, cfg.sensors[10].friendlyName);
 }
 }

 /* Streaming CRC32 accumulator (covers HEADER + TABLE + PAYLOAD; trailer is the CRC). */
 uint32_t crc = crc32_init( );

 _server.sendHeader("Content-Disposition", "attachment; filename=\"simut_history.simx\"");
 _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 _server.send(200, "application/octet-stream", "");

 /* Emit HEADER */
 SimxHeader hdr = {};
 memcpy(hdr.magic, "SIMX", 4);
 hdr.version = 0x01;
 hdr.kind = 'H';
 hdr.recordSize = (uint16_t)sizeof(BinaryHistoryRecord); /* 74 */
 hdr.rangeFrom = rangeFrom;
 hdr.rangeTo = rangeTo;
 hdr.sensorTableSize = (uint32_t)sensorTblLen;
 crc = crc32_update(crc, (const uint8_t*)&hdr, sizeof(hdr));
 safeSend((const char*)&hdr, sizeof(hdr));

 /* Emit SENSOR_TABLE */
 if (sensorTblLen > 0) {
 crc = crc32_update(crc, sensorTbl, sensorTblLen);
 safeSend((const char*)sensorTbl, sensorTblLen);
 }

 /* Iterate history files in range. Day-aligned in LOCALTIME (files
 * are named YYYYMMDD by local date, not UTC) — fix for the bug where
 * the current day was left out when user is in timezone != UTC. */
 bool aborted = false;
 time_t fromT = (time_t)rangeFrom;
 time_t toDayEnd = (time_t)rangeTo;
 struct tm tFrom; localtime_r(&fromT, &tFrom);
 tFrom.tm_hour = 0; tFrom.tm_min = 0; tFrom.tm_sec = 0;
 time_t dayStart = mktime(&tFrom);
 while (dayStart <= toDayEnd && !aborted) {
 /* Calculate nextDay BEFORE any continue — guard against infinite
 * loop when file doesn't exist. */
 struct tm dtNext; localtime_r(&dayStart, &dtNext);
 dtNext.tm_mday += 1;
 dtNext.tm_hour = 0; dtNext.tm_min = 0; dtNext.tm_sec = 0;
 time_t nextDay = mktime(&dtNext);
 time_t curDay = dayStart;
 dayStart = nextDay; /* advance ALWAYS — subsequent continues are safe */

 /* Cooperative abort: client gone or deadline exceeded */
 if (isClientGone( ) || isHandlerOvertime( )) { aborted = true; break; }

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

 /* Validate v2 header */
 HistoryFileHeaderV2 hdrV2;
 bool headerOk = false;
 {
 ReadGuard rg(_storageRef);
 if (f.size( ) >= HIST_V2_HEADER_SIZE) {
 f.seek(0);
 if (f.read((uint8_t*)&hdrV2, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
 headerOk = (memcmp(hdrV2.magic, HIST_V2_MAGIC, 4) == 0 &&
 (hdrV2.version == HIST_V2_VERSION || hdrV2.version == HIST_V3_VERSION) &&
 hdrV2.anchorPeriod > 0);
 }
 }
 }
 if (!headerOk) { ReadGuard rg(_storageRef); f.close( ); continue; }

 HistoryCodecState rdState;
 historyCodecReset(rdState);
 rdState.fileVersion = hdrV2.version; /* MUST set before decode — auto-detect unreliable */
 uint16_t anchorPeriod = hdrV2.anchorPeriod;

 uint8_t rdBuf[256];
 size_t rdFilled = 0;
 bool fileHasMore = true;

 while (fileHasMore && !aborted) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
 aborted = true; break;
 }

 BinaryHistoryRecord batch[20];
 int batchCount = 0;
 {
 ReadGuard rg(_storageRef);
 while (batchCount < 20) {
 if (rdFilled < HIST_V2_MAX_DELTA_SIZE && f.available( ) > 0) {
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
 fileHasMore = (rdFilled > 0 || f.available( ) > 0);
 }

 for (int bi = 0; bi < batchCount && !aborted; bi++) {
 const BinaryHistoryRecord& rec = batch[bi];
 if (rec.epoch < rangeFrom) continue;
 if (rec.epoch > rangeTo) { fileHasMore = false; break; }

 crc = crc32_update(crc, (const uint8_t*)&rec, sizeof(rec));
 if (!safeSend((const char*)&rec, sizeof(rec))) { aborted = true; break; }
 }

 if (_lightYieldCb) _lightYieldCb( );
 delay(2);
 watchdog_update( );
 }
 { ReadGuard rg(_storageRef); f.close( ); }
 /* dayStart already advanced at top of loop (infinite loop protection) */
 }

 /* TRAILER: final CRC32 */
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
/* GET /api/export/logs.bin?from=<epoch>&to=<epoch>&level=... */
/* =========================================================================== */
/* Emits .simx bundle kind='L' (CRC32 trailer). PAYLOAD = N x CompactLogRecord
 * (12 B packed) raw, same as existing /api/logs, but filtered by epoch and
 * level. Hard cap 31 days. SENSOR_TABLE empty (sensorTableSize=0).
 *
 * level=err -> LOG_ERROR only (3)
 * level=inf -> LOG_INFO only (1)
 * level=all -> everything (default)
 *
 * Client decodes records, looks up `code` -> text via /api/lang and
 * generates CSV in browser.
 */
void WebManager::handleApiExportLogs( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_LOGS)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

 if (!_server.hasArg("from") || !_server.hasArg("to")) {
 _server.send(400, "application/json", "{\"error\":\"Missing from/to params\"}"); return;
 }
 uint32_t rangeFrom = (uint32_t)strtoul(_server.arg("from").c_str( ), nullptr, 10);
 uint32_t rangeTo = (uint32_t)strtoul(_server.arg("to").c_str( ), nullptr, 10);
 if (rangeFrom == 0 || rangeTo == 0 || rangeFrom >= rangeTo) {
 _server.send(400, "application/json", "{\"error\":\"Invalid range\"}"); return;
 }
 if (rangeTo - rangeFrom > SIMX_MAX_RANGE_SECS) {
 _server.send(400, "application/json", "{\"error\":\"Range exceeds 31 days\"}"); return;
 }

 /* Level filter: 0 = all, 1 = INFO only, 3 = ERROR only.
 * Keep numeric LogLevel code for direct comparison. */
 String levelArg = _server.hasArg("level") ? _server.arg("level") : "all";
 uint8_t levelFilter = 0xFF; /* 0xFF = no filter */
 if (levelArg == "err") levelFilter = LOG_ERROR;
 else if (levelArg == "inf") levelFilter = LOG_INFO;
 else if (levelArg != "all") {
 _server.send(400, "application/json", "{\"error\":\"Invalid level (use err|inf|all)\"}"); return;
 }

 if (TouchPriority::isActive( )) {
 _server.sendHeader("Retry-After", "3");
 _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}"); return;
 }
 if (__atomic_exchange_n(&_inExportLogsHandler, true, __ATOMIC_ACQ_REL)) {
 _server.send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
 }
 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 __atomic_store_n(&_inExportLogsHandler, false, __ATOMIC_RELEASE);
 _server.send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
 }

 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;
 if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str( ));

 /* Streaming CRC32 accumulator (covers HEADER + PAYLOAD; sensorTblSize = 0). */
 uint32_t crc = crc32_init( );

 _server.sendHeader("Content-Disposition", "attachment; filename=\"simut_logs.simx\"");
 _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 _server.send(200, "application/octet-stream", "");

 /* Emit HEADER */
 SimxHeader hdr = {};
 memcpy(hdr.magic, "SIMX", 4);
 hdr.version = 0x01;
 hdr.kind = 'L';
 hdr.recordSize = (uint16_t)LOG_RECORD_SIZE; /* 12 */
 hdr.rangeFrom = rangeFrom;
 hdr.rangeTo = rangeTo;
 hdr.sensorTableSize = 0;
 crc = crc32_update(crc, (const uint8_t*)&hdr, sizeof(hdr));
 safeSend((const char*)&hdr, sizeof(hdr));

 /* Iterate /system.old.blog first (oldest) then /system.blog */
 bool aborted = false;
 auto streamFiltered = [&](const char* path) -> bool {
 File f;
 {
 ReadGuard rg(_storageRef);
 if (!LittleFS.exists(path)) return true;
 f = LittleFS.open(path, "r");
 }
 if (!f) return true;

 uint8_t batch[480]; /* 40 records x 12B */
 while (f.available( ) >= LOG_RECORD_SIZE && !aborted) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
 aborted = true; break;
 }

 int bytesRead = 0;
 {
 ReadGuard rg(_storageRef);
 while (f.available( ) >= LOG_RECORD_SIZE &&
 bytesRead + LOG_RECORD_SIZE <= (int)sizeof(batch)) {
 if (f.read(batch + bytesRead, LOG_RECORD_SIZE) == LOG_RECORD_SIZE) {
 bytesRead += LOG_RECORD_SIZE;
 }
 }
 }

 for (int off = 0; off < bytesRead && !aborted; off += LOG_RECORD_SIZE) {
 const CompactLogRecord* rec = (const CompactLogRecord*)(batch + off);
 if (rec->epoch < rangeFrom || rec->epoch > rangeTo) continue;
 if (levelFilter != 0xFF && rec->getLevel( ) != levelFilter) continue;

 crc = crc32_update(crc, (const uint8_t*)rec, LOG_RECORD_SIZE);
 if (!safeSend((const char*)rec, LOG_RECORD_SIZE)) { aborted = true; break; }
 }

 if (_lightYieldCb) _lightYieldCb( );
 delay(2);
 watchdog_update( );
 }
 { ReadGuard rg(_storageRef); f.close( ); }
 return !aborted;
 };

 if (streamFiltered(LOG_FILE_OLD)) {
 streamFiltered(LOG_FILE_CURRENT);
 }

 /* TRAILER: final CRC32 */
 if (!aborted) {
 uint32_t crcFinal = crc32_final(crc);
 safeSend((const char*)&crcFinal, sizeof(crcFinal));
 safeSend("");
 }

 _handlerDeadline = savedDeadline;
 if (_displayRef) _displayRef->setWebBusy(false);
 __atomic_store_n(&_inExportLogsHandler, false, __ATOMIC_RELEASE);
}

void WebManager::handleApiLogs( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_LOGS)) { _server.send(403, "text/plain", "Forbidden"); return; }
 if (isRateLimited(200)) { _server.send(429, "text/plain", "Too Fast"); return; }


 if (TouchPriority::isActive( )) {
 _server.sendHeader("Retry-After", "3");
 _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
 return;
 }

 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 _server.send(503, "text/plain", "System Busy.");
 return;
 }


 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;
 if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str( ));

 /*
 * Send raw binary logs (12 bytes/record) for maximum
 * transfer efficiency. Translation happens in the browser.
 * Format: application/octet-stream, N × CompactLogRecord(12 bytes).
 * ~10x smaller than the previous translated CSV.
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
 while (f.available( ) >= LOG_RECORD_SIZE) {
 if (count > 0 && count % 80 == 0) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 f.close( );
 return false;
 }
 }

 /* Read batch of up to 40 records (480 bytes) and send at once */
 uint8_t buf[480];
 int bytesRead = 0;
 {
 ReadGuard rg(_storageRef);
 while (f.available( ) >= LOG_RECORD_SIZE && bytesRead + LOG_RECORD_SIZE <= (int)sizeof(buf)) {
 if (f.read(buf + bytesRead, LOG_RECORD_SIZE) == LOG_RECORD_SIZE) {
 bytesRead += LOG_RECORD_SIZE;
 }
 }
 }

 if (bytesRead > 0) {
 _server.sendContent((const char*)buf, bytesRead);
 count += bytesRead / LOG_RECORD_SIZE;
 }

 if (_lightYieldCb) _lightYieldCb( );
 delay(2);
 watchdog_update( );
 }
 f.close( );
 return true;
 };

 if (streamRawLog(LOG_FILE_OLD)) {
 streamRawLog(LOG_FILE_CURRENT);
 }

 safeSend("");
 _handlerDeadline = savedDeadline;
 if (_displayRef) _displayRef->setWebBusy(false);
}

void WebManager::handleApiClearLogs( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_LOGS) || !(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }
 if (isPasswordChangeRequired( )) return;
 if (rejectIfTouchPriority( )) return;

 {
 RenderGuard rg(_displayRef);
 LittleFS.remove(LOG_FILE_CURRENT);
 LittleFS.remove(LOG_FILE_OLD);
 /* Also remove legacy CSV logs */
 LittleFS.remove("/system.log");
 LittleFS.remove("/system.old");
 LogManager::instance( ).resetAfterExternalWipe( );
 }

 LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Admin erased System Logs"));
 _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

/* Shared helper for handleApiScreenshot + handleApiScreenshotChunk.
 * Reads N rows from TFT (3x each with majority vote) and converts RGB565→BGR888 in
 * out_bgr. chunk_start_bmp_y is the offset IN THE BMP IMAGE (bottom-up), display_y
 * is mapped to TFT order (top-down). Caller is responsible for pause/unpause
 * of Core 1 around the call. */
static void read_chunk_bgr(DisplayManager* d, int chunk_start_bmp_y,
 int rows_this, int w, int h, uint8_t* out_bgr) {
 uint16_t pixelRow1[320], pixelRow2[320], pixelRow3[320];
 for (int i = 0; i < rows_this; i++) {
 int display_y = h - 1 - (chunk_start_bmp_y + i);
 if (display_y < 0) break;
 d->readRow(display_y, pixelRow1, w);
 d->readRow(display_y, pixelRow2, w);
 d->readRow(display_y, pixelRow3, w);
 uint8_t* row_dst = out_bgr + i * w * 3;
 for (int x = 0; x < w; x++) {
 uint16_t a = pixelRow1[x], b = pixelRow2[x], c = pixelRow3[x];
 uint16_t color = (a == b) ? a : (b == c ? b : (a == c ? a : b));
 row_dst[x*3 + 0] = (color & 0x001F) << 3; /* B */
 row_dst[x*3 + 1] = ((color & 0x07E0) >> 5) << 2; /* G */
 row_dst[x*3 + 2] = ((color & 0xF800) >> 11) << 3;/* R */
 }
 }
}

void WebManager::handleApiScreenshot( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }


 if (TouchPriority::isActive( )) {
 _server.sendHeader("Retry-After", "3");
 _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
 return;
 }


 if (__atomic_exchange_n(&_isProcessingScreenshot, true, __ATOMIC_ACQ_REL)) {
 /* Screenshot in progress: signal cancellation and return 409 */
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
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;

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

 /* Uses the pattern from screenshot_chunk — pause 1x per 16 rows, read each
 * row 3x with majority vote (helper read_chunk_bgr), stream in chunks. */
 constexpr int ROWS_PER_CHUNK = 16;
 uint8_t* chunkBuf = (uint8_t*)malloc(ROWS_PER_CHUNK * 320 * 3); /* 15360 */
 if (!chunkBuf) {
 __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
 _handlerDeadline = savedDeadline;
 return;
 }
 bool clientDisconnected = false;
 for (int chunk_start = 0; chunk_start < (int)h; chunk_start += ROWS_PER_CHUNK) {
 int rows_this = ((int)h - chunk_start < ROWS_PER_CHUNK) ? ((int)h - chunk_start) : ROWS_PER_CHUNK;
 if (!_server.client( ).connected( ) || isHandlerOvertime( ) || _cancelScreenshot) {
 clientDisconnected = true; break;
 }
 _displayRef->pauseRendering(true);
 read_chunk_bgr(_displayRef, chunk_start, rows_this, w, h, chunkBuf);
 _displayRef->pauseRendering(false);
 watchdog_update( );
 safeSend((const char*)chunkBuf, rows_this * 320 * 3);
 if (_lightYieldCb) _lightYieldCb( );
 }

 free(chunkBuf);
 __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
 _handlerDeadline = savedDeadline;
 if (clientDisconnected) LOG_CODE(LOG_WARN, "WEB", WEB_SCREENSHOT_ABORTED, 0, "");
}

/* Chunked screenshot with CRC32 — verifiable integrity.
 *
 * GET /api/screenshot_chunk?n=N returns 1 chunk of 16 rows from the TFT display.
 * Total chunks: 15 (240/16). Each chunk:
 * - 16 rows × 320 cols × 3 bytes BGR = 15360 bytes payload
 * - 12-byte binary header:
 * [0..3] uint32 chunk_index (big-endian)
 * [4..7] uint32 payload_size (big-endian)
 * [8..11] uint32 crc32 EDB88320 of payload (big-endian)
 *
 * Client: requests N=0..14 sequentially. Verifies CRC32 each chunk; if
 * fails, re-requests that N. Reassembles BMP locally (header + 240 rows).
 *
 * Advantages vs /api/screenshot full:
 * - Localized failure (individual chunk, not entire BMP)
 * - Integrity verification (CRC32)
 * - Can retry corrupted chunks
 * - Client can pause/resume
 *
 * MIME response: application/octet-stream (pure binary). */
void WebManager::handleApiScreenshotChunk( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }

 if (!_displayRef) { _server.send(500, "text/plain", "Display offline"); return; }

 /* Parse ?n=N */
 int n = -1;
 if (_server.hasArg("n")) n = _server.arg("n").toInt( );
 constexpr int W = 320;
 constexpr int H = 240;
 constexpr int ROWS_PER_CHUNK = 16;
 constexpr int TOTAL_CHUNKS = (H + ROWS_PER_CHUNK - 1) / ROWS_PER_CHUNK; /* 15 */
 if (n < 0 || n >= TOTAL_CHUNKS) {
 _server.send(416, "text/plain", "Invalid chunk index (use n=0..14)");
 return;
 }

 /* Bump handler deadline (same pattern as handleApiScreenshot).
 * 16 rows × ~50ms readRow + CRC32 + send = can take >2s; default deadline
 * kills handler before finishing = empty body. */
 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;

 /* Calculate row range for this chunk */
 int row_start = n * ROWS_PER_CHUNK;
 int rows_this = (row_start + ROWS_PER_CHUNK > H) ? (H - row_start) : ROWS_PER_CHUNK;
 int payload_size = rows_this * W * 3;

 /* Temporary buffer for rows (16 rows × 320 cols × 3 bytes = 15360 max).
 * Heap-alloc on demand instead of static BSS — saves 15 KB
 * permanent (endpoint used rarely: debug + manual capture). Fail
 * with 503 if heap insufficient; client can retry. */
 uint8_t* payload = (uint8_t*)malloc(ROWS_PER_CHUNK * W * 3);
 if (!payload) {
 _handlerDeadline = savedDeadline;
 _server.send(503, "text/plain", "Out of memory");
 return;
 }
 /* Multi-sample 3x + majority vote via shared helper
 * read_chunk_bgr. ILI9341
 * read protocol is fragile; reads each row 3x and takes majority value (2/3),
 * fallback on middle sample. Reduces line defects by ~95%. */
 _displayRef->pauseRendering(true);
 read_chunk_bgr(_displayRef, row_start, rows_this, W, H, payload);
 _displayRef->pauseRendering(false);
 watchdog_update( );

 /* Compute CRC32 EDB88320 (gzip-compatible) */
 uint32_t crc = ota::crc32_update(0xFFFFFFFFu, payload, payload_size) ^ 0xFFFFFFFFu;

 /* Header big-endian */
 uint8_t hdr[12];
 hdr[0] = (n >> 24) & 0xFF; hdr[1] = (n >> 16) & 0xFF; hdr[2] = (n >> 8) & 0xFF; hdr[3] = n & 0xFF;
 hdr[4] = (payload_size >> 24) & 0xFF; hdr[5] = (payload_size >> 16) & 0xFF;
 hdr[6] = (payload_size >> 8) & 0xFF; hdr[7] = payload_size & 0xFF;
 hdr[8] = (crc >> 24) & 0xFF; hdr[9] = (crc >> 16) & 0xFF;
 hdr[10] = (crc >> 8) & 0xFF; hdr[11] = crc & 0xFF;

 _server.setContentLength(12 + payload_size);
 _server.send(200, "application/octet-stream", "");
 safeSend((const char*)hdr, 12);
 safeSend((const char*)payload, payload_size);

 free(payload);
 _handlerDeadline = savedDeadline;
}

void WebManager::handleApiHistoryDays( ) {
 if ((getAuthPerms( ) & PERM_HISTORY) == 0) { _server.send(403); return; }


 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) { _server.send(503, "application/json", "{\"error\":\"System Busy\"}"); return; }

 std::vector<String> files;
 {

 ReadGuard rg(_storageRef);
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 feedWatchdog( );
 if (dir.fileName( ).endsWith(HISTORY_FILE_EXT) || dir.fileName( ).endsWith(HISTORY_V4_FILE_EXT)) {
 files.push_back(dir.fileName( ));
 }
 }
 }

 sortStrings(files.data( ), (int)files.size( ), true); /* descending */

 _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 _server.send(200, "application/json", "");
 safeSend("[");
 for (size_t i = 0; i < files.size( ); i++) {
 files[i].replace(HISTORY_FILE_EXT, "");
 String entry = (i > 0 ? ",\"" : "\"") + files[i] + "\"";
 safeSend(entry);
 }
 safeSend("]");
 safeSend("");
}
