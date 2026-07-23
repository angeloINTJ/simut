/**
 * @file AppManager_Graph.cpp
 * @brief Graph rendering from binary history: cache, render, preload, append.
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include "sensors/SensorHelpers.h"
#include <LittleFS.h>
#include <time.h>

static inline float readRecordValue(const BinaryHistoryRecord& rec,
 int sensorId, float& humOut)
{
 humOut = NAN;

 if (sensorId >= 0 && sensorId < MAX_SENSORS) {
 /* Per-slot humidity from v3 record (fallback to ambientHum for v2 / slot 10) */
 humOut = BinaryHistoryRecord::i16ToFloat(rec.humidity[sensorId]);
 if (isnan(humOut) && sensorId == 10) {
 humOut = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);
 }
 return BinaryHistoryRecord::i16ToFloat(rec.sensors[sensorId]);
 }

 return NAN;
}

/* GRAPH RENDERING FROM BINARY HISTORY */
/* =========================================================================== */
/**
 * @brief Load and render a temperature/humidity graph from binary history.
 *
 * Uses fixed-size records for exact seek (offset = recordIndex * 28).
 * Eliminates CSV parsing, seek fallback, and line realignment.
 * 6-second budget limit prevents watchdog timeout.
 */
void AppManager::renderGraphOptimized(int sensorId, int range, bool showAfterLoad, int navOffset, time_t forceEndEpoch) {
 if (!_storageMgr->lockHeavyTask( )) {
 LOG_CODE(LOG_WARN, "APP", APP_FLASH_BUSY, 0, "");
 _displayMgr->forceDashboard( );
 return;
 }
 /*
 * Context-aware WdtWindow: renderGraph can be called from a UI event
 * (main loop), from core0Yield (inside web handler), or from
 * preloadSensorRanges (5x for 6s = up to 30s). 30s covers any case.
 * Nested inside telemetry (120s) or web handler, keeps the outer.
 */
 LogManager::WdtWindow _wdt(30000);
 LOG_CODE(LOG_INFO, "APP", APP_GRAPH_LOADING, 0, "");

 uint32_t _graphBudgetStart = millis( );
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

 SystemConfig &cfg = _storageMgr->getConfig( );
 uint32_t epochLimit = 0;

 if (sensorId == (int)MINMAX_SLOT_BOARD_TEMP) {
 snprintf(pkg.title, sizeof(pkg.title), "Board Temp");
 snprintf(pkg.hwId, sizeof(pkg.hwId), "SYS");
 snprintf(pkg.rom, sizeof(pkg.rom), "RP2040-ADC");
 pkg.hasHumidity = false;
 } else if (sensorId >= 0 && sensorId < MAX_SENSORS) {
 pkg.hasHumidity = sensorHasHumidity((SensorType)cfg.sensors[sensorId].sensorType);
 if (cfg.sensors[sensorId].active) {
 safeCopy(pkg.title, cfg.sensors[sensorId].friendlyName, sizeof(pkg.title));
 safeCopy(pkg.hwId, cfg.sensors[sensorId].hwId, sizeof(pkg.hwId));
 epochLimit = 0; /* 0 = accept all history regardless of provision date */
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
 * Duration and step table per range:
 * 1H → 3600s  6H → 21600s  12H → 43200s
 * 24H → 86400s  7D → 604800s
 *
 * navOffset shifts the temporal window in steps of the range.
 * e.g.: range=24H, navOffset=-2 → show 2 days ago.
 */
 static const time_t rangeDuration[] = { 3600, 21600, 43200, 86400, 604800 };
 time_t step = (range >= 0 && range <= 4) ? rangeDuration[range] : 86400;
 time_t effectiveEnd;

 if (forceEndEpoch > 0) {
 /* Calendar mode: fixed window midnight to midnight */
 effectiveEnd = forceEndEpoch;
 } else {
 effectiveEnd = now + (time_t)navOffset * step;
 if (effectiveEnd > now) effectiveEnd = now; /* Don't allow viewing the future */
 }

 if (range == 0) { cutoff = effectiveEnd - 3600; decimation = 1; }
 else if (range == 1) { cutoff = effectiveEnd - 21600; decimation = 2; }
 else if (range == 2) { cutoff = effectiveEnd - 43200; decimation = 4; }
 else if (range == 3) { cutoff = effectiveEnd - 86400; decimation = 8; }
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
 * Stores the temporal window in the package so the renderer
 * positions points proportionally to time (not to index).
 */
 pkg.tsCutoff = cutoff;
 pkg.tsEnd = effectiveEnd;

 /* Pre-populate timestamps for header (shows period even without data) */
 pkg.tsFirst = cutoff;
 pkg.tsLast = effectiveEnd;
 pkg.tsMid = cutoff + (effectiveEnd - cutoff) / 2;

 /* Real min/max: tracked from ALL records, not just decimated ones */
 pkg.realMinVal = 1000.0f;
 pkg.realMaxVal = -1000.0f;
 pkg.tsRealMin = 0;
 pkg.tsRealMax = 0;

 for (int d = daysToLoad - 1; d >= 0; d--) {
 if (pkg.count >= GRAPH_WIDTH) break;

 time_t targetDay = effectiveEnd - (d * 86400);
 struct tm timeinfo;
 localtime_r(&targetDay, &timeinfo);

 char path[42];
 snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

 File f;
 _storageMgr->enterFlashReadLock( );
 bool fileExists = LittleFS.exists(path);
 if (!fileExists) {
 /* Try .sim4 if .bin not found */
 snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_V4_FILE_EXT,
 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
 fileExists = LittleFS.exists(path);
 }
 if (fileExists) f = LittleFS.open(path, "r");
 _storageMgr->exitFlashReadLock( );

 if (fileExists && f) {
 /* Detect V4 (.sim4) vs legacy (.bin) by checking path extension */
 bool isV4 = (strlen(path) > 5 && path[strlen(path)-5] == '.' && path[strlen(path)-4] == 's');
 if (!isV4 && f.size() >= 4) {
 StorageManager::ReadGuard rg(_storageMgr.get( ));
 char m[4]; f.seek(0);
 if (f.read((uint8_t*)m, 4) == 4 && memcmp(m, HIST_V4_MAGIC, 4) == 0) isV4 = true;
 }
 if (isV4) {
 /* Static buffers — avoid 5KB+ stack on RP2040 ~4KB stack. */
 static HistV4State g4st;
 static uint8_t hdrBuf[HIST_V4_MAX_HEADER];
 { StorageManager::ReadGuard rg(_storageMgr.get( ));
 f.seek(0);
 int hdrRead = f.read(hdrBuf, sizeof(hdrBuf));
 /* Same cursor bug fixed in handleApiHistoryMulti and in the v1.5.3
  * StorageManager scan: reposition to the REAL header end. Reading up
  * to 2 KB left the cursor at EOF for small files (today's) and
  * misaligned at byte 2048 for bigger ones — the record loop below
  * then decoded nothing: blank graph on the TFT. */
 size_t g4HdrLen = (hdrRead >= (int)HIST_V4_HEADER_FIXED)
                   ? histV4ReadHeaderBuf(hdrBuf, (size_t)hdrRead, g4st) : 0;
 if (g4HdrLen == 0) { f.close(); continue; }
 f.seek(g4HdrLen);
 }
 int tMi = -1, hMi = -1;
 { SystemConfig &cfg = _storageMgr->getConfig();
 const char* tHwId = (sensorId >= 0 && sensorId < MAX_SENSORS) ? cfg.sensors[sensorId].hwId : "";
 for (uint8_t i = 0; i < g4st.measureCount; i++) {
 char mHwId[17]; uint8_t si = g4st.measures[i].sensorIdx;
 histV4StrPoolGet(mHwId, sizeof(mHwId), g4st.strPool,
 g4st.sensors[si].hwIdOffset, g4st.sensors[si].hwIdLen);
 if (strcmp(mHwId, tHwId) == 0) {
 if (g4st.measures[i].channel == CH_TEMP) tMi = i;
 else if (g4st.measures[i].channel == CH_HUM) hMi = i;
 }
 }}
 if (tMi < 0) { _storageMgr->enterFlashReadLock(); f.close(); _storageMgr->exitFlashReadLock(); continue; }

 static uint8_t g4RdBuf[HIST_V4_READ_BUF];
 static int64_t g4vals[HIST_V4_MAX_MEASUREMENTS];
 size_t g4RdFilled = 0;
 uint32_t g4epoch;
 bool hm4 = true; uint32_t gb4 = millis();

 while (hm4 && pkg.count < GRAPH_WIDTH) {
 if (timeSince(gb4, GRAPH_BUDGET_MS)) break;
 size_t cons = 0;
 { StorageManager::ReadGuard rg(_storageMgr.get( ));
 if (g4RdFilled < g4st.anchorByteSize && f.available() > 0) {
 int rN = f.read(g4RdBuf + g4RdFilled, sizeof(g4RdBuf) - g4RdFilled);
 if (rN > 0) g4RdFilled += (size_t)rN;
 }
 if (g4RdFilled > 0) {
 cons = histV4DecodeNext(g4RdBuf, g4RdFilled, g4st, g4vals, &g4epoch);
 if (cons > 0) { memmove(g4RdBuf, g4RdBuf + cons, g4RdFilled - cons); g4RdFilled -= cons; }
 }
 hm4 = (g4RdFilled > 0 || f.available() > 0);
 }
 if (cons == 0) break;

 time_t ts = (time_t)g4epoch;
 if (ts < cutoff) continue;
 if (ts > effectiveEnd) break;

 float vr = NAN, hr = NAN;
 if (tMi >= 0 && !histV4IsNan(g4vals[tMi], g4st.measures[tMi].bitWidth))
 vr = histV4ToFloat(g4vals[tMi], g4st.measures[tMi]);
 if (hMi >= 0 && !histV4IsNan(g4vals[hMi], g4st.measures[hMi].bitWidth))
 hr = histV4ToFloat(g4vals[hMi], g4st.measures[hMi]);
 if (ts < epochLimit) vr = NAN;

 if (!isnan(vr)) {
 if (vr < pkg.realMinVal) { pkg.realMinVal = vr; pkg.tsRealMin = ts; }
 if (vr > pkg.realMaxVal) { pkg.realMaxVal = vr; pkg.tsRealMax = ts; }
 }

 lineIdx++;
 if (lineIdx % decimation != 0) continue;

 pkg.pointsV1[pkg.count] = vr;
 pkg.tsPoints[pkg.count] = (uint32_t)ts;
 if (pkg.hasHumidity) pkg.pointsV2[pkg.count] = hr;
 if (pkg.count == 0) pkg.tsFirst = ts;

 if (!isnan(vr)) {
 if (vr < pkg.minVal) { pkg.minVal = vr; pkg.idxMinTemp = pkg.count; pkg.tsMinTemp = ts; }
 if (vr > pkg.maxVal) { pkg.maxVal = vr; pkg.idxMaxTemp = pkg.count; pkg.tsMaxTemp = ts; }
 }
 if (pkg.hasHumidity && !isnan(hr)) {
 if (hr < localHumMin) { localHumMin = hr; pkg.tsMinHum = ts; }
 if (hr > localHumMax) { localHumMax = hr; pkg.tsMaxHum = ts; }
 }
 pkg.count++;
 }
 _storageMgr->enterFlashReadLock(); f.close(); _storageMgr->exitFlashReadLock();
 continue;
 }

 /* v2: validate SIM2 header. No optimized seek (variable records). */
 HistoryFileHeaderV2 hdrG;
 bool headerOkG = false;
 {
 StorageManager::ReadGuard rg(_storageMgr.get( ));
 if (f.size( ) >= HIST_V2_HEADER_SIZE) {
 f.seek(0);
 if (f.read((uint8_t*)&hdrG, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
 headerOkG = (memcmp(hdrG.magic, HIST_V2_MAGIC, 4) == 0 &&
 (hdrG.version == HIST_V2_VERSION || hdrG.version == HIST_V3_VERSION) &&
 hdrG.anchorPeriod > 0);
 }
 }
 }
 if (!headerOkG) { _storageMgr->enterFlashReadLock( ); f.close( ); _storageMgr->exitFlashReadLock( ); continue; }

 HistoryCodecState gState;
 historyCodecReset(gState);
 gState.fileVersion = hdrG.version; /* MUST set before decode — auto-detect unreliable */
 uint16_t gAnchorPeriod = hdrG.anchorPeriod;
 uint8_t gRdBuf[256];
 size_t gRdFilled = 0;

 bool hasMore = true;
 bool budgetExceeded = false;

 while (hasMore && pkg.count < GRAPH_WIDTH && !budgetExceeded) {
 if (timeSince(_graphBudgetStart, GRAPH_BUDGET_MS)) {
 LOG_CODE(LOG_WARN, "APP", APP_GRAPH_BUDGET, 0, "");
 budgetExceeded = true;
 break;
 }

 _storageMgr->enterFlashReadLock( );
 BinaryHistoryRecord batch[20];
 int batchCount = 0;
 while (batchCount < 20 && pkg.count < GRAPH_WIDTH) {
 if (gRdFilled < HIST_V2_MAX_DELTA_SIZE && f.available( ) > 0) {
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
 hasMore = (gRdFilled > 0 || f.available( ) > 0);
 _storageMgr->exitFlashReadLock( );

 bool pastWindow = false;

 for (int bi = 0; bi < batchCount && pkg.count < GRAPH_WIDTH; bi++) {
 const BinaryHistoryRecord& rec = batch[bi];

 time_t ts = (time_t)rec.epoch;
 if (ts < cutoff) continue;

 /*
 * Records are chronological: if this one exceeded effectiveEnd,
 * all subsequent ones will too. Immediate break instead of
 * continue avoids reading the rest of the file uselessly.
 * Critical for 1H: without this, reads ~1380 extra records in a
 * file of 1440 → exceeds 6s budget.
 */
 if (ts > effectiveEnd) { pastWindow = true; break; }

 float humRead = NAN;
 float valRead = readRecordValue(rec, sensorId, humRead);
 if (ts < epochLimit) valRead = NAN;

 /*
 * REAL min/max: tracked from EVERY record in the window,
 * regardless of decimation. Ensures the Y axis and badges
 * show the true extreme values.
 */
 if (!isnan(valRead)) {
 if (valRead < pkg.realMinVal) { pkg.realMinVal = valRead; pkg.tsRealMin = ts; }
 if (valRead > pkg.realMaxVal) { pkg.realMaxVal = valRead; pkg.tsRealMax = ts; }
 }

 /* Decimation: skip intermediate records to fit on screen */
 lineIdx++;
 if (lineIdx % decimation != 0) continue;

 /*
 * ALWAYS add the point to the array, even if NAN.
 * NAN points preserve temporal position on the X axis,
 * creating visible holes in the graph where the sensor
 * was in error. The renderer skips segments with NAN.
 */
 pkg.pointsV1[pkg.count] = valRead;
 pkg.tsPoints[pkg.count] = (uint32_t)ts;

 if (pkg.hasHumidity) {
 pkg.pointsV2[pkg.count] = humRead;
 }

 if (pkg.count == 0) pkg.tsFirst = ts;

 /* Statistics of displayed points (for markers on the graph) */
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

 /* Left temporal window: stop reading this file */
 if (pastWindow) break;

 feedWdt( );
 yield( );
 }

 _storageMgr->enterFlashReadLock( );
 f.close( );
 _storageMgr->exitFlashReadLock( );

 if (budgetExceeded) {
 _storageMgr->unlockHeavyTask( );
 _displayMgr->forceDashboard( );
 return;
 }
 }

 feedWdt( );
 yield( );
 }

 if (pkg.count > 0) {
 pkg.tsMid = pkg.tsFirst + (pkg.tsLast - pkg.tsFirst) / 2;

 {
 float sumT = 0.0f;
 float sumH = 0.0f;
 int tempCount = 0;
 int humCount = 0;

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
 pkg.avgHum = (humCount > 0) ? (sumH / (float)humCount) : NAN;

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
 pkg.stdHum = (humCount > 2) ? sqrtf(sqSumH / (float)(humCount - 1)) : NAN;

 /* Delta: find first and last VALID values */
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
 pkg.deltaHum = (!isnan(firstValidH) && !isnan(lastValidH)) ? (lastValidH - firstValidH) : NAN;
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
 * Even without data, fill tsFirst/tsLast with the requested time
 * window so the header displays the reference period.
 */
 pkg.tsFirst = cutoff;
 pkg.tsLast = effectiveEnd;
 pkg.tsMid = cutoff + (effectiveEnd - cutoff) / 2;
 }

 /*
 * Calendar mode (forceEndEpoch > 0): the header and X axis must
 * always show the COMPLETE period of the selected day (00:00–23:59),
 * regardless of where real data starts/ends.
 * Adjust tsLast to 23:59 (effectiveEnd - 60s) to avoid the display
 * showing "08/04 00:00" (midnight of the following day).
 */
 if (forceEndEpoch > 0) {
 pkg.tsFirst = cutoff;                 /* 00:00 of the day */
 pkg.tsLast = forceEndEpoch - 60;      /* 23:59 of the day */
 pkg.tsMid = cutoff + (forceEndEpoch - cutoff) / 2;  /* ~12:00 */
 }

 _storageMgr->unlockHeavyTask( );

 if (showAfterLoad) {
 _displayMgr->showGraphPlot(pkg, localHumMin, localHumMax);
 }
}
