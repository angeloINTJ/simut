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

/* ── Bucket accumulation state ─────────────────────────────────────────────
 * File-static struct + noinline function instead of a captured lambda: the
 * two call sites (day files and the RAM tail) would clone the body and the
 * pico_w_test image lives a few hundred bytes from the flash ceiling. */
struct GraphBucketAcc {
 time_t cutoff; float bucketW; int buckets;
 float bMin1[GRAPH_WIDTH], bMax1[GRAPH_WIDTH], bSum1[GRAPH_WIDTH];
 float bMin2[GRAPH_WIDTH], bMax2[GRAPH_WIDTH], bSum2[GRAPH_WIDTH];
 uint16_t bCnt1[GRAPH_WIDTH], bCnt2[GRAPH_WIDTH], bCntT[GRAPH_WIDTH];
 uint32_t bTsSum[GRAPH_WIDTH];   /* sum of (ts - cutoff) per bucket */
 float sumT, sqSumT; int cntT;
 float sumH, sqSumH; int cntH;
 float firstT, lastT, firstH, lastH;
 int idxMaxBucket, idxMinBucket;
 float humMin, humMax;           /* displayed extremes of the V2 axis */
 float pressBase, pressSum, pressSumSq, pressLast; int pressCnt;
};
static GraphBucketAcc gAcc;

static void __attribute__((noinline)) graphAccumRecord(GraphDataPackage& pkg,
 time_t ts, float vr, float hr, float pr)
{
 int b = (int)((float)(ts - gAcc.cutoff) / gAcc.bucketW);
 if (b < 0) b = 0;
 if (b >= gAcc.buckets) b = gAcc.buckets - 1;
 gAcc.bTsSum[b] += (uint32_t)(ts - gAcc.cutoff);
 gAcc.bCntT[b]++;

 if (!isnan(vr)) {
 if (vr < pkg.realMinVal) { pkg.realMinVal = vr; pkg.tsRealMin = ts; gAcc.idxMinBucket = b; }
 if (vr > pkg.realMaxVal) { pkg.realMaxVal = vr; pkg.tsRealMax = ts; gAcc.idxMaxBucket = b; }
 if (gAcc.bCnt1[b] == 0 || vr < gAcc.bMin1[b]) gAcc.bMin1[b] = vr;
 if (gAcc.bCnt1[b] == 0 || vr > gAcc.bMax1[b]) gAcc.bMax1[b] = vr;
 gAcc.bSum1[b] += vr; gAcc.bCnt1[b]++;
 gAcc.sumT += vr; gAcc.sqSumT += vr * vr; gAcc.cntT++;
 if (isnan(gAcc.firstT)) gAcc.firstT = vr;
 gAcc.lastT = vr;
 }

 if (!isnan(pr)) {
 if (pr < pkg.realMinPress) { pkg.realMinPress = pr; pkg.tsRealMinPress = ts; }
 if (pr > pkg.realMaxPress) { pkg.realMaxPress = pr; pkg.tsRealMaxPress = ts; }
 if (gAcc.pressCnt == 0) gAcc.pressBase = pr;
 gAcc.pressLast = pr;
 float pd = pr - gAcc.pressBase;
 gAcc.pressSum += pd; gAcc.pressSumSq += pd * pd; gAcc.pressCnt++;
 }

 /* Secondary curve: humidity, or pressure standing in for it. */
 float v2 = pkg.hasHumidity ? hr : (pkg.v2IsPress ? pr : NAN);
 if (!isnan(v2)) {
 if (gAcc.bCnt2[b] == 0 || v2 < gAcc.bMin2[b]) gAcc.bMin2[b] = v2;
 if (gAcc.bCnt2[b] == 0 || v2 > gAcc.bMax2[b]) gAcc.bMax2[b] = v2;
 gAcc.bSum2[b] += v2; gAcc.bCnt2[b]++;
 if (v2 < gAcc.humMin) { gAcc.humMin = v2; if (pkg.hasHumidity) pkg.tsMinHum = ts; }
 if (v2 > gAcc.humMax) { gAcc.humMax = v2; if (pkg.hasHumidity) pkg.tsMaxHum = ts; }
 }
 if (pkg.hasHumidity && !isnan(hr)) {
 gAcc.sumH += hr; gAcc.sqSumH += hr * hr; gAcc.cntH++;
 if (isnan(gAcc.firstH)) gAcc.firstH = hr;
 gAcc.lastH = hr;
 }
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

 /* Pressure real extremes. The avg/std accumulators live in gAcc and take
 * sums as deviations from the first sample (gAcc.pressBase): absolute hPa
 * values are ~1013 and their squares eat float precision, while deviations
 * stay in the units digit. Spans both the file walk and the RAM tail. */
 pkg.realMinPress = 1e9f; /* same reason as localHumMin above */
 pkg.realMaxPress = -1000.0f;
 pkg.tsRealMinPress = 0;
 pkg.tsRealMaxPress = 0;

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

 /*
 * Duration table per range:
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

 cutoff = effectiveEnd - step;
 if (range == 4) daysToLoad = 7;

 if (range <= 3) {
 struct tm todayTm;
 localtime_r(&effectiveEnd, &todayTm);
 todayTm.tm_hour = 0; todayTm.tm_min = 0; todayTm.tm_sec = 0;
 time_t todayMidnight = mktime(&todayTm);
 daysToLoad = (cutoff < todayMidnight) ? 2 : 1;
 }

 /* ── Time buckets instead of stride decimation ───────────────────────
  * The old path emitted 1 record in N, with N fixed per range and tuned
  * for a 1-minute cadence. Three ways that lied: a 1-minute peak
  * survived only by luck (a 7-day view drew each of the freezer's
  * IDENTICAL defrosts at a different random height); a changed logging
  * interval broke every count; and the renderer spaces points by index,
  * so a 6-hour outage compressed into one invisible step. Buckets are
  * uniform in TIME: index spacing becomes time-proportional for free,
  * an empty bucket is NAN — a gap the plot draws as a gap — and each
  * bucket carries min/max/mean, so the extreme IS the point. The scan
  * already visited every record for the real extremes; cost unchanged. */
 int buckets = GRAPH_WIDTH;
 {
 uint16_t im = _storageMgr->getHistoryIntervalMin( );
 if (im < 1) im = 1;
 const long expected = (long)((effectiveEnd - cutoff) / (60L * im));
 if (expected < (long)buckets) buckets = (int)expected;
 if (buckets < 40) buckets = 40;
 }
 const float bucketW = (float)(effectiveEnd - cutoff) / (float)buckets;

 /* Reset the file-static accumulator (see graphAccumRecord above).
  * avg/std/delta used to be computed from the decimated points, so they
  * carried the same sampling bias the plot did; now every record in the
  * window contributes. */
 gAcc.cutoff = cutoff; gAcc.bucketW = bucketW; gAcc.buckets = buckets;
 for (int b = 0; b < buckets; b++) {
 gAcc.bMin1[b] = NAN; gAcc.bMax1[b] = NAN; gAcc.bSum1[b] = 0.0f;
 gAcc.bMin2[b] = NAN; gAcc.bMax2[b] = NAN; gAcc.bSum2[b] = 0.0f;
 gAcc.bCnt1[b] = 0; gAcc.bCnt2[b] = 0; gAcc.bCntT[b] = 0; gAcc.bTsSum[b] = 0;
 }
 gAcc.sumT = 0.0f; gAcc.sqSumT = 0.0f; gAcc.cntT = 0;
 gAcc.sumH = 0.0f; gAcc.sqSumH = 0.0f; gAcc.cntH = 0;
 gAcc.firstT = NAN; gAcc.lastT = NAN; gAcc.firstH = NAN; gAcc.lastH = NAN;
 gAcc.idxMaxBucket = -1; gAcc.idxMinBucket = -1;
 gAcc.humMin = localHumMin; gAcc.humMax = localHumMax;
 gAcc.pressBase = 0.0f; gAcc.pressSum = 0.0f; gAcc.pressSumSq = 0.0f;
 gAcc.pressLast = NAN; gAcc.pressCnt = 0;

 /*
 * Stores the temporal window in the package so the renderer
 * positions points proportionally to time (uniform buckets make the
 * index axis time-true).
 */
 pkg.tsCutoff = cutoff;
 pkg.tsEnd = effectiveEnd;

 /* Pre-populate timestamps for header (shows period even without data) */
 pkg.tsFirst = cutoff;
 pkg.tsLast = effectiveEnd;
 pkg.tsMid = cutoff + (effectiveEnd - cutoff) / 2;

 /* Real min/max: tracked from ALL records, not just displayed buckets */
 pkg.realMinVal = 1000.0f;
 pkg.realMaxVal = -1000.0f;
 pkg.tsRealMin = 0;
 pkg.tsRealMax = 0;


 for (int d = daysToLoad - 1; d >= 0; d--) {
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
 while (true) {
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

 graphAccumRecord(pkg, ts, vr, hr, pr);
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
 for (uint8_t i = 0; i < ramCount; i++) {
 if (!_storageMgr->h5RamRecord(i, epoch, vals)) break;

 time_t ts = (time_t)epoch;
 if (ts < cutoff) continue;
 if (ts > effectiveEnd) continue;   /* window ends in the past: no tail */

 float vr = NAN, hr = NAN, pr = NAN;
 if (vals[tCi] != H5_NAN_SENTINEL) vr = (float)vals[tCi] * scaleT;
 if (hCi >= 0 && vals[hCi] != H5_NAN_SENTINEL) hr = (float)vals[hCi] * scaleH;
 if (pCi >= 0 && vals[pCi] != H5_NAN_SENTINEL) pr = (float)vals[pCi] * scaleP;
 if (ts < epochLimit) vr = NAN;

 /* Every tail record lands in its time bucket like any other — the
  * newest one included, so the right edge is as current as the RAM
  * itself, with no decimation special case to carry. */
 graphAccumRecord(pkg, ts, vr, hr, pr);
 }
 feedWdt( );
 }
 }

 /* Everything the scan learned now lives in gAcc; pull the axis extremes
 * back into the locals the clamp logic and showGraphPlot consume. */
 localHumMin = gAcc.humMin;
 localHumMax = gAcc.humMax;

 /* Pressure stats close out here, independent of the buckets: a window can
 * hold temperature yet no pressure (schema without the column). tsReal-
 * MaxPress == 0 means no finite pressure was ever seen. */
 if (pkg.tsRealMaxPress == 0) { pkg.realMinPress = NAN; pkg.realMaxPress = NAN; }
 pkg.avgPress = (gAcc.pressCnt > 0) ? (gAcc.pressBase + gAcc.pressSum / (float)gAcc.pressCnt) : NAN;
 if (gAcc.pressCnt > 2) {
 float pressMean = gAcc.pressSum / (float)gAcc.pressCnt;
 float pressVar = (gAcc.pressSumSq - gAcc.pressSum * pressMean) / (float)(gAcc.pressCnt - 1);
 pkg.stdPress = (pressVar > 0.0f) ? sqrtf(pressVar) : 0.0f;
 } else {
 pkg.stdPress = NAN;
 }
 pkg.deltaPress = (gAcc.pressCnt > 0) ? (gAcc.pressLast - gAcc.pressBase) : NAN;

 if (gAcc.cntT > 0 || gAcc.cntH > 0) {
 /* ── Emit the buckets. Empty bucket = NAN = a drawn gap; the point of
  * a bucket sits at the MEAN timestamp of its records, which pins the
  * curve where the data actually is inside partial buckets. ── */
 pkg.count = buckets;
 pkg.sampleCount = gAcc.cntT;
 int firstData = -1, lastData = -1;
 for (int b = 0; b < buckets; b++) {
 if (gAcc.bCntT[b] > 0) {
 pkg.tsPoints[b] = (uint32_t)(cutoff + (time_t)(gAcc.bTsSum[b] / gAcc.bCntT[b]));
 if (firstData < 0) firstData = b;
 lastData = b;
 } else {
 pkg.tsPoints[b] = (uint32_t)(cutoff + (time_t)((b + 0.5f) * bucketW));
 }
 pkg.pointsV1[b] = (gAcc.bCnt1[b] > 0) ? (gAcc.bSum1[b] / (float)gAcc.bCnt1[b]) : NAN;
 pkg.minV1[b]   = (gAcc.bCnt1[b] > 0) ? gAcc.bMin1[b] : NAN;
 pkg.maxV1[b]   = (gAcc.bCnt1[b] > 0) ? gAcc.bMax1[b] : NAN;
 pkg.pointsV2[b] = (gAcc.bCnt2[b] > 0) ? (gAcc.bSum2[b] / (float)gAcc.bCnt2[b]) : NAN;
 pkg.minV2[b]   = (gAcc.bCnt2[b] > 0) ? gAcc.bMin2[b] : NAN;
 pkg.maxV2[b]   = (gAcc.bCnt2[b] > 0) ? gAcc.bMax2[b] : NAN;
 }
 if (firstData >= 0) {
 pkg.tsFirst = (time_t)pkg.tsPoints[firstData];
 pkg.tsLast = (time_t)pkg.tsPoints[lastData];
 pkg.tsMid = pkg.tsFirst + (pkg.tsLast - pkg.tsFirst) / 2;
 }

 /* Displayed extremes == real extremes, by construction: the marker
  * bucket carries the true peak on its envelope edge. */
 pkg.minVal = pkg.realMinVal;
 pkg.maxVal = pkg.realMaxVal;
 pkg.idxMinTemp = gAcc.idxMinBucket;
 pkg.idxMaxTemp = gAcc.idxMaxBucket;
 pkg.tsMinTemp = pkg.tsRealMin;
 pkg.tsMaxTemp = pkg.tsRealMax;

 /* ── Stats over EVERY record in the window (not the drawn points) ── */
 pkg.avgTemp = (gAcc.cntT > 0) ? (gAcc.sumT / (float)gAcc.cntT) : NAN;
 if (gAcc.cntT > 1) {
 float varT = (gAcc.sqSumT - gAcc.sumT * (gAcc.sumT / (float)gAcc.cntT)) / (float)(gAcc.cntT - 1);
 pkg.stdTemp = (varT > 0.0f) ? sqrtf(varT) : 0.0f;
 } else {
 pkg.stdTemp = (gAcc.cntT > 0) ? 0.0f : NAN;
 }
 pkg.deltaTemp = (!isnan(gAcc.firstT) && !isnan(gAcc.lastT)) ? (gAcc.lastT - gAcc.firstT) : NAN;

 pkg.avgHum = (gAcc.cntH > 0) ? (gAcc.sumH / (float)gAcc.cntH) : NAN;
 if (gAcc.cntH > 2) {
 float varH = (gAcc.sqSumH - gAcc.sumH * (gAcc.sumH / (float)gAcc.cntH)) / (float)(gAcc.cntH - 1);
 pkg.stdHum = (varH > 0.0f) ? sqrtf(varH) : 0.0f;
 } else {
 pkg.stdHum = NAN;
 }
 pkg.deltaHum = (!isnan(gAcc.firstH) && !isnan(gAcc.lastH)) ? (gAcc.lastH - gAcc.firstH) : NAN;

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
 pkg.count = 0;
 pkg.sampleCount = 0;
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
