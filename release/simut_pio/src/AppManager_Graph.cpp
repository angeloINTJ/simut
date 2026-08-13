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
 /* 1e9, not the 1000.0f the temperature sentinels use: this pair also
 * hosts the pressure axis when v2IsPress, and sea-level pressure
 * (~1013 hPa) sits ABOVE 1000 — with the old sentinel the minimum never
 * updated and the curve rendered squeezed against the top of the plot. */
 float localHumMin = 1e9f;
 float localHumMax = -1000.0f;

 /* Pressure real extremes + avg/std accumulators. The sums are taken as
 * deviations from the first sample (pressBase): absolute hPa values are
 * ~1013 and their squares eat float precision, while deviations stay in
 * the units digit. Spans both the file walk and the RAM tail. */
 pkg.realMinPress = 1e9f; /* same reason as localHumMin above */
 pkg.realMaxPress = -1000.0f;
 pkg.tsRealMinPress = 0;
 pkg.tsRealMaxPress = 0;
 float pressBase = 0.0f, pressSum = 0.0f, pressSumSq = 0.0f;
 float pressLast = NAN; /* newest valid sample — pressBase is the oldest */
 int pressCnt = 0;

 SystemConfig &cfg = _storageMgr->getConfig( );
 uint32_t epochLimit = 0;

 if (sensorId == (int)MINMAX_SLOT_BOARD_TEMP) {
 snprintf(pkg.title, sizeof(pkg.title), "Board Temp");
 snprintf(pkg.hwId, sizeof(pkg.hwId), "SYS");
 snprintf(pkg.rom, sizeof(pkg.rom), "RP2040-ADC");
 pkg.hasHumidity = false;
 } else if (sensorId >= 0 && sensorId < MAX_SENSORS) {
 pkg.hasHumidity = sensorHasHumidity((SensorType)cfg.sensors[sensorId].sensorType);
 pkg.hasPressure = sensorHasChannel((SensorType)cfg.sensors[sensorId].sensorType, CH_PRESS);
 /* BMP280 (pressure, no humidity): pressure takes the plot's second curve. */
 pkg.v2IsPress = pkg.hasPressure && !pkg.hasHumidity;
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
 snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

 File f;
 _storageMgr->enterFlashReadLock( );
 bool fileExists = LittleFS.exists(path);
 if (fileExists) f = LittleFS.open(path, "r");
 _storageMgr->exitFlashReadLock( );

 /* One reader. Under V4 this carried its own HistV4State, its own header
  * buffer, its own refill loop and its own hwId-to-measurement search —
  * the same apparatus the web handler, the CSV export, telemetry and the
  * preload each had a copy of. V5 keeps the schema in the file and the
  * decode in StorageManager, so what is left here is the graph's own
  * business: pick the channels this slot owns and plot them. */
 if (fileExists && f) {
 { StorageManager::ReadGuard rg(_storageMgr.get( )); f.close( ); }

 bool opened = false;
 { StorageManager::ReadGuard rg(_storageMgr.get( ));
 opened = _storageMgr->h5OpenDay(path); }
 if (!opened) { feedWdt( ); yield( ); continue; }

 /* Channel indices for this slot, from the descriptor ids. */
 int tCi = -1, hCi = -1, pCi = -1;
 {
 const H5ChannelDesc* schema = _storageMgr->h5ReaderSchema( );
 const uint8_t nCh = _storageMgr->h5ReaderChannels( );
 for (uint8_t c = 0; c < nCh && schema; c++) {
 if (schema[c].id / MAX_SENSOR_CHANNELS != (uint8_t)sensorId) continue;
 const uint8_t ch = schema[c].id % MAX_SENSOR_CHANNELS;
 if (ch == CH_TEMP) tCi = c;
 else if (ch == CH_HUM) hCi = c;
 else if (ch == CH_PRESS) pCi = c;
 }
 }
 if (tCi < 0) {
 { StorageManager::ReadGuard rg(_storageMgr.get( )); _storageMgr->h5CloseDay( ); }
 feedWdt( ); yield( ); continue;
 }

 /* Skip straight to the block that holds `cutoff`: blocks are hops of
  * one hour, so a 24 h window on a 30-day file reads 24 headers rather
  * than walking the file from byte 0. */
 { StorageManager::ReadGuard rg(_storageMgr.get( )); _storageMgr->h5SeekTo((uint32_t)cutoff); }

 float scaleT = 1.0f, scaleH = 1.0f, scaleP = 1.0f;
 {
 const H5ChannelDesc* schema = _storageMgr->h5ReaderSchema( );
 if (schema) {
 scaleT = powf(10.0f, (float)schema[tCi].scaleExp);
 if (hCi >= 0) scaleH = powf(10.0f, (float)schema[hCi].scaleExp);
 if (pCi >= 0) scaleP = powf(10.0f, (float)schema[pCi].scaleExp);
 }
 }

 int16_t vals[H5_MAX_CHANNELS];
 uint32_t epoch = 0;
 while (pkg.count < GRAPH_WIDTH) {
 if (timeSince(_graphBudgetStart, GRAPH_BUDGET_MS)) {
 LOG_CODE(LOG_WARN, "APP", APP_GRAPH_BUDGET, 1, "");
 break;
 }
 /* feedWdt( ) and not feedWatchdog( ): we are inside the flash read
  * lock, and the latter runs the light yield, which allocates and can
  * reach saveConfiguration( ). */
 feedWdt( );
 bool got = false;
 { StorageManager::ReadGuard rg(_storageMgr.get( ));
 got = _storageMgr->h5NextRecord(epoch, vals); }
 if (!got) break;

 time_t ts = (time_t)epoch;
 if (ts < cutoff) continue;
 /* Skip rather than abandon the file: one block stamped past the window
  * would otherwise hide every block behind it, and blocks are only in time
  * order while nothing writes one out of turn. */
 if (ts > effectiveEnd) continue;

 float vr = NAN, hr = NAN, pr = NAN;
 if (vals[tCi] != H5_NAN_SENTINEL) vr = (float)vals[tCi] * scaleT;
 if (hCi >= 0 && vals[hCi] != H5_NAN_SENTINEL) hr = (float)vals[hCi] * scaleH;
 if (pCi >= 0 && vals[pCi] != H5_NAN_SENTINEL) pr = (float)vals[pCi] * scaleP;
 if (ts < epochLimit) vr = NAN;

 if (!isnan(vr)) {
 if (vr < pkg.realMinVal) { pkg.realMinVal = vr; pkg.tsRealMin = ts; }
 if (vr > pkg.realMaxVal) { pkg.realMaxVal = vr; pkg.tsRealMax = ts; }
 }
 if (!isnan(pr)) {
 if (pr < pkg.realMinPress) { pkg.realMinPress = pr; pkg.tsRealMinPress = ts; }
 if (pr > pkg.realMaxPress) { pkg.realMaxPress = pr; pkg.tsRealMaxPress = ts; }
 }

 lineIdx++;
 if (lineIdx % decimation != 0) continue;

 pkg.pointsV1[pkg.count] = vr;
 pkg.tsPoints[pkg.count] = (uint32_t)ts;
 if (pkg.hasHumidity) pkg.pointsV2[pkg.count] = hr;
 else if (pkg.v2IsPress) pkg.pointsV2[pkg.count] = pr;
 if (pkg.count == 0) pkg.tsFirst = ts;
 pkg.tsLast = ts;

 if (!isnan(vr)) {
 if (vr < pkg.minVal) { pkg.minVal = vr; pkg.idxMinTemp = pkg.count; pkg.tsMinTemp = ts; }
 if (vr > pkg.maxVal) { pkg.maxVal = vr; pkg.idxMaxTemp = pkg.count; pkg.tsMaxTemp = ts; }
 }
 if (pkg.hasHumidity && !isnan(hr)) {
 if (hr < localHumMin) { localHumMin = hr; pkg.tsMinHum = ts; }
 if (hr > localHumMax) { localHumMax = hr; pkg.tsMaxHum = ts; }
 }
 /* Pressure owning the plotted curve: its DISPLAYED extremes feed the
 * right-axis scale (same role localHumMin/Max play for humidity). */
 if (pkg.v2IsPress && !isnan(pr)) {
 if (pr < localHumMin) localHumMin = pr;
 if (pr > localHumMax) localHumMax = pr;
 }
 if (pkg.hasPressure && !isnan(pr)) {
 if (pressCnt == 0) pressBase = pr;
 pressLast = pr;
 float pd = pr - pressBase;
 pressSum += pd; pressSumSq += pd * pd; pressCnt++;
 }
 pkg.count++;
 }
 { StorageManager::ReadGuard rg(_storageMgr.get( )); _storageMgr->h5CloseDay( ); }
 }

 feedWdt( );
 yield( );
 }

 /* ── The hour still open in RAM ───────────────────────────────────────
  * A V5 block reaches its day file only when it seals, which at one record a
  * minute is once an hour. Everything that reads .h5 therefore trails the
  * present by up to that hour — which is why opening the graph showed nothing
  * for the last few minutes. The samples were never missing: they are held in
  * the encoder, plain rather than bit-packed, so reaching them costs a copy
  * and no decode. The /history/.wip alongside them is a crash bound, not a
  * read path — boot adopts it into the day file and nothing else opens it.
  *
  * No yield inside this walk: the history writer runs on this same core, and
  * letting it in here could seal the block mid-read. It is at most
  * H5_BLOCK_MAX_RECORDS records of pure memory, no flash and no lock. */
 {
 const uint8_t ramCount = _storageMgr->h5RamCount( );
 const H5ChannelDesc* ramSchema = _storageMgr->getH5Schema( );
 const uint8_t ramNCh = _storageMgr->getH5ChannelCount( );

 /* Resolved against the LIVE schema, not the reader's: the open block is
  * encoded against the sensor set in force right now, which is not
  * necessarily the one the last file on flash was written with. */
 int tCi = -1, hCi = -1, pCi = -1;
 for (uint8_t c = 0; c < ramNCh && ramSchema; c++) {
 if (ramSchema[c].id / MAX_SENSOR_CHANNELS != (uint8_t)sensorId) continue;
 const uint8_t ch = ramSchema[c].id % MAX_SENSOR_CHANNELS;
 if (ch == CH_TEMP) tCi = c;
 else if (ch == CH_HUM) hCi = c;
 else if (ch == CH_PRESS) pCi = c;
 }

 if (ramCount > 0 && tCi >= 0) {
 const float scaleT = powf(10.0f, (float)ramSchema[tCi].scaleExp);
 const float scaleH = (hCi >= 0) ? powf(10.0f, (float)ramSchema[hCi].scaleExp) : 1.0f;
 const float scaleP = (pCi >= 0) ? powf(10.0f, (float)ramSchema[pCi].scaleExp) : 1.0f;

 int16_t vals[H5_MAX_CHANNELS];
 uint32_t epoch = 0;
 for (uint8_t i = 0; i < ramCount && pkg.count < GRAPH_WIDTH; i++) {
 if (!_storageMgr->h5RamRecord(i, epoch, vals)) break;

 time_t ts = (time_t)epoch;
 if (ts < cutoff) continue;
 if (ts > effectiveEnd) continue;   /* window ends in the past: no tail */

 float vr = NAN, hr = NAN, pr = NAN;
 if (vals[tCi] != H5_NAN_SENTINEL) vr = (float)vals[tCi] * scaleT;
 if (hCi >= 0 && vals[hCi] != H5_NAN_SENTINEL) hr = (float)vals[hCi] * scaleH;
 if (pCi >= 0 && vals[pCi] != H5_NAN_SENTINEL) pr = (float)vals[pCi] * scaleP;
 if (ts < epochLimit) vr = NAN;

 if (!isnan(vr)) {
 if (vr < pkg.realMinVal) { pkg.realMinVal = vr; pkg.tsRealMin = ts; }
 if (vr > pkg.realMaxVal) { pkg.realMaxVal = vr; pkg.tsRealMax = ts; }
 }
 if (!isnan(pr)) {
 if (pr < pkg.realMinPress) { pkg.realMinPress = pr; pkg.tsRealMinPress = ts; }
 if (pr > pkg.realMaxPress) { pkg.realMaxPress = pr; pkg.tsRealMaxPress = ts; }
 }

 /* The cadence carries on from the file loop so the tail is spaced like the
  * rest of the series — except for the newest record, which is emitted
  * whatever the decimation says. Otherwise a 24 h range (step 8) would
  * still leave the right edge up to eight minutes stale, and the right
  * edge being current is the whole point of reading RAM at all. */
 lineIdx++;
 const bool newest = (i + 1 == ramCount);
 if (lineIdx % decimation != 0 && !newest) continue;

 pkg.pointsV1[pkg.count] = vr;
 pkg.tsPoints[pkg.count] = (uint32_t)ts;
 if (pkg.hasHumidity) pkg.pointsV2[pkg.count] = hr;
 else if (pkg.v2IsPress) pkg.pointsV2[pkg.count] = pr;
 if (pkg.count == 0) pkg.tsFirst = ts;
 pkg.tsLast = ts;

 if (!isnan(vr)) {
 if (vr < pkg.minVal) { pkg.minVal = vr; pkg.idxMinTemp = pkg.count; pkg.tsMinTemp = ts; }
 if (vr > pkg.maxVal) { pkg.maxVal = vr; pkg.idxMaxTemp = pkg.count; pkg.tsMaxTemp = ts; }
 }
 if (pkg.hasHumidity && !isnan(hr)) {
 if (hr < localHumMin) { localHumMin = hr; pkg.tsMinHum = ts; }
 if (hr > localHumMax) { localHumMax = hr; pkg.tsMaxHum = ts; }
 }
 if (pkg.v2IsPress && !isnan(pr)) {
 if (pr < localHumMin) localHumMin = pr;
 if (pr > localHumMax) localHumMax = pr;
 }
 if (pkg.hasPressure && !isnan(pr)) {
 if (pressCnt == 0) pressBase = pr;
 pressLast = pr;
 float pd = pr - pressBase;
 pressSum += pd; pressSumSq += pd * pd; pressCnt++;
 }
 pkg.count++;
 }
 feedWdt( );
 }
 }

 /* Pressure stats close out here, independent of pkg.count: a window can
 * hold temperature yet no pressure (schema without the column). tsReal-
 * MaxPress == 0 means no finite pressure was ever seen. */
 if (pkg.tsRealMaxPress == 0) { pkg.realMinPress = NAN; pkg.realMaxPress = NAN; }
 pkg.avgPress = (pressCnt > 0) ? (pressBase + pressSum / (float)pressCnt) : NAN;
 if (pressCnt > 2) {
 float pressMean = pressSum / (float)pressCnt;
 float pressVar = (pressSumSq - pressSum * pressMean) / (float)(pressCnt - 1);
 pkg.stdPress = (pressVar > 0.0f) ? sqrtf(pressVar) : 0.0f;
 } else {
 pkg.stdPress = NAN;
 }
 pkg.deltaPress = (pressCnt > 0) ? (pressLast - pressBase) : NAN;

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
 } else if (pkg.v2IsPress && localHumMax > -1000.0f) {
 /* Pressure on the second axis: keep its real hPa extremes — the
 * 0..100 clamp above is a humidity rule. */
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
