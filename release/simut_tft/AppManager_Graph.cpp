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
#include "SensorHelpers.h"
#include <LittleFS.h>
#include <time.h>

static inline float readRecordValue(const BinaryHistoryRecord& rec,
 int sensorId, float& humOut)
{
 humOut = NAN;

 if (sensorId >= 0 && sensorId < MAX_SENSORS) {
 /* Per-slot humidity. The record's ambientHum field is NOT consulted as a
  * fallback for slot 10 any more: it only ever held that slot's humidity
  * because slot 10 was the ambient sensor, so on a board that moved its
  * humidity sensor elsewhere the fallback grafted one sensor's readings
  * onto another's graph. Nothing in the firmware writes the field today. */
 humOut = BinaryHistoryRecord::i16ToFloat(rec.humidity[sensorId]);
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
 /* Feed per file, and stop once the budget is gone.
  *
  * Without the break, a query that matches nothing kept opening every
  * remaining day's file — exists( ) + open( ) + header parse + close, all
  * LittleFS metadata walks off flash — after the record loops had already
  * given up on the budget. Without the feed, none of that was covered: the
  * feeds in this function sit AFTER the file loop, so the whole scan ran
  * unfed against a watchdog whose real ceiling is 8388 ms, not the 30 s the
  * WdtWindow above appears to grant (see WATCHDOG_TIMEOUT_MS). */
 feedWdt( );
 if (timeSince(_graphBudgetStart, GRAPH_BUDGET_MS)) {
 LOG_CODE(LOG_WARN, "APP", APP_GRAPH_BUDGET, 2, "");
 break;
 }

 time_t targetDay = effectiveEnd - (d * 86400);
 struct tm timeinfo;
 localtime_r(&targetDay, &timeinfo);

 char path[42];
 snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_V4_FILE_EXT,
 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

 File f;
 _storageMgr->enterFlashReadLock( );
 bool fileExists = LittleFS.exists(path);
 if (fileExists) f = LittleFS.open(path, "r");
 _storageMgr->exitFlashReadLock( );

 /* One format. The .bin fallback and the isV4 sniff that went with it are
  * gone with v2/v3 — the extension no longer decides which decoder runs. */
 if (fileExists && f) {
 {
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
 bool hm4 = true;

 /* Budget from _graphBudgetStart, not a fresh millis( ) per file.
  *
  * This loop used a per-file stamp, so GRAPH_BUDGET_MS was granted again for
  * EVERY history file — 6 s each, against ~50 files on this bench. It never
  * showed while queries matched, because pkg.count fills from the newest file
  * and the loop exits almost immediately. A query that matches NOTHING (an
  * empty hwId, a sensor whose hwId changed, a slot with no history) never
  * fills it, so every file burned its full 6 s and the device rebooted long
  * before the scan ended.
  *
  * The legacy loop below always used the render-wide stamp; this is the same
  * semantics, and now the whole render is bounded once. */
 while (hm4 && pkg.count < GRAPH_WIDTH) {
 if (timeSince(_graphBudgetStart, GRAPH_BUDGET_MS)) {
 LOG_CODE(LOG_WARN, "APP", APP_GRAPH_BUDGET, 1, "");
 break;
 }
 /* feedWdt( ) and not feedWatchdog( ): we are inside the flash read lock, and
  * the latter runs the light yield, which allocates and can reach
  * saveConfiguration( ). Same reason as the history scan in the web handler.
  *
  * The budget alone is not enough: 6 s of unfed scanning is already 40% of
  * WATCHDOG_TIMEOUT_MS, and this loop is reached with the lock held. */
 feedWdt( );
 size_t cons = 0;
 { StorageManager::ReadGuard rg(_storageMgr.get( ));
 /* A1: refill DEPOIS da falha do decode. Com o limiar antigo
  * (`g4RdFilled < anchorByteSize`), um delta grande na borda do
  * buffer encerrava o laço e o gráfico aparecia truncado. */
 cons = histV4DecodeNextRefill(
 g4RdBuf, sizeof(g4RdBuf), g4RdFilled, g4st, g4vals, &g4epoch,
 [&f](uint8_t *dst, size_t maxBytes) -> size_t {
 if (f.available() <= 0) return 0;
 int rN = f.read(dst, maxBytes);
 return (rN > 0) ? (size_t)rN : 0;
 });
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
