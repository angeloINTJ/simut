/**
 * @file AppManager_HistoryAlarm.cpp
 * @brief History logging, min/max computation, alarm monitoring, live display updates.
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "SoundManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include "TelemetryManager.h"
#include <LittleFS.h>
#include <time.h>

void AppManager::pauseDisplayForFlash(bool lock) {
 /* During cooperative quiet mode, Core 1 is frozen in a RAM-only
 * loop with IRQs OFF. Attempting an IRQ-based multicore_lockout here
 * blocks forever (IRQ never handled) until WDT kills Core 0.
 * Lockout is unnecessary in this scenario because Core 1 already
 * doesn't touch flash. Early-return makes requestFsLock (LogManager)
 * and enterFlashSafeMode (StorageManager) no-ops when already inside
 * quiet mode. */
 if (_displayMgr->isInQuietMode( )) return;
 _displayMgr->pauseRendering(lock);
}

bool AppManager::requestDisplayQuietMode(bool enable) {
 if (enable) return _displayMgr->requestQuietMode( ); /* default 15s */
 _displayMgr->releaseQuietMode( );
 return true;
}

void AppManager::refreshSelectedSlot( ) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 /* Restore persisted panel selection on first call */
 if (_lastSavedSlotIdx < 0) {
 _lastSavedSlotIdx = (int8_t)cfg.reserved[53];
 _lastSavedTopIdx = (int8_t)cfg.reserved[52];
 if (cfg.reserved[53] < MAX_SENSORS) _currentSensorIdx = cfg.reserved[53];
 if (cfg.reserved[52] < MAX_SENSORS) _displayMgr->setTopSlotFixedIdx(cfg.reserved[52]);
 _lastSlotChangeTime = millis( );
 }
 const auto& sensors = _sensorMgr->getRuntimeSensors( );
 bool found = false;

 if (_currentSensorIdx >= 0 && _currentSensorIdx < MAX_SENSORS && cfg.sensors[_currentSensorIdx].active) {
 uint8_t targetGpio = cfg.sensors[_currentSensorIdx].pins[0];
 for (const auto &s : sensors) {
 if (s.config.pins[0] == targetGpio) {
 _displayMgr->setSlotData(s.avgValue[0], s.avgValue[1], s.avgValue[2], s.type, !s.inErrorState, _currentSensorIdx, String(s.config.friendlyName));
 found = true;
 if (_displayMgr->getTopSlotIdx( ) == _currentSensorIdx)
 _displayMgr->setTopSlotData(s.avgValue[0], s.avgValue[1], s.avgValue[2], s.type, !s.inErrorState, _currentSensorIdx, String(s.config.friendlyName));
 break;
 }
 }
 }

 if (!found) _displayMgr->setSlotData(NAN, NAN, NAN, TYPE_NONE, false, _currentSensorIdx, "Empty / Inactive");

 /* Fixed panels: override with pinned sensor data when panel is fixed elsewhere */
 {
 int topIdx = _displayMgr->getTopSlotIdx( );
 if (topIdx != _currentSensorIdx && topIdx >= 0 && topIdx < MAX_SENSORS && cfg.sensors[topIdx].active) {
 uint8_t tgpio = cfg.sensors[topIdx].pins[0];
 for (const auto &s : sensors) {
 if (s.config.pins[0] == tgpio) {
 _displayMgr->setTopSlotData(s.avgValue[0], s.avgValue[1], s.avgValue[2], s.type, !s.inErrorState, topIdx, String(s.config.friendlyName));
 break;
 }
 }
 }
 }
}

/**
 * @brief Push current sensor data and system status to the display shared state.
 * System status (time, RSSI, pending count) updates every cycle.
 * Sensor data updates only when new readings are available.
 */
void AppManager::updateLiveDisplay( ) {


 {
 String dateStr = _netMgr->getFormattedDate( );
 dateStr.replace("/20", "/");
 String fullStatus = dateStr + " - " + _netMgr->getFormattedTime( );
 _displayMgr->setSystemStatus(_netMgr->getRssi( ), false, fullStatus);


 static uint32_t lastPendingRefresh = 0;
 if (timeSince(lastPendingRefresh, 10000)) {
 _telemetryMgr->refreshPendingCount( );
 lastPendingRefresh = millis( );
 }
 _displayMgr->setTelemetryPending(_telemetryMgr->getPendingEstimate( ));

 /* Real-time min/max accumulation — feed from current sensor readings */
 {
 const auto& sensors = _sensorMgr->getRuntimeSensors( );
 for (const auto &s : sensors) {
 if (s.inErrorState) continue;
 int gpio = s.config.pins[0];
 if (gpio < 0 || gpio >= MINMAX_SLOT_COUNT) continue;
 float v = s.avgValue[0];
 if (!isnan(v)) {
 if (v < _cachedMin[gpio]) _cachedMin[gpio] = v;
 if (v > _cachedMax[gpio]) _cachedMax[gpio] = v;
 }
 float h = s.avgValue[1];
 if (!isnan(h)) {
 if (h < _cachedHumMin[gpio]) _cachedHumMin[gpio] = h;
 if (h > _cachedHumMax[gpio]) _cachedHumMax[gpio] = h;
 }
 }
 }

 /* Debounced panel config save (3s after last interaction) */
 if (_lastSlotChangeTime > 0 && millis( ) - _lastSlotChangeTime > 3000) {
 int8_t ct = (int8_t)_displayMgr->getTopPanelFixedIdx( );
 int8_t cs = (int8_t)_currentSensorIdx;
 if (ct != _lastSavedTopIdx || cs != _lastSavedSlotIdx) {
 _lastSavedTopIdx = ct;
 _lastSavedSlotIdx = cs;
 SystemConfig &cfg = _storageMgr->getConfig( );
 cfg.reserved[52] = (uint8_t)(ct >= 0 ? ct : 0xFF);
 cfg.reserved[53] = (uint8_t)cs;
 _storageMgr->saveConfiguration( );
 }
 }

 /* Daily min/max — all slots uniform */
 int slotIdx = _currentSensorIdx;
 if (slotIdx >= 0 && slotIdx < MAX_SENSORS) {
 float sMinT = (_cachedMin[slotIdx] < 999.0f) ? _cachedMin[slotIdx] : NAN;
 float sMaxT = (_cachedMax[slotIdx] > -999.0f) ? _cachedMax[slotIdx] : NAN;
 float sMinH = (_cachedHumMin[slotIdx] < 999.0f) ? _cachedHumMin[slotIdx] : NAN;
 float sMaxH = (_cachedHumMax[slotIdx] > -999.0f) ? _cachedHumMax[slotIdx] : NAN;
 _displayMgr->setSlotMinMax(sMinT, sMaxT, sMinH, sMaxH);
 /* Top slot min/max — may differ from _currentSensorIdx when fixed */
 int topIdx = _displayMgr->getTopSlotIdx( );
 if (topIdx >= 0 && topIdx < MAX_SENSORS && topIdx != slotIdx) {
 float tMinT = (_cachedMin[topIdx] < 999.0f) ? _cachedMin[topIdx] : NAN;
 float tMaxT = (_cachedMax[topIdx] > -999.0f) ? _cachedMax[topIdx] : NAN;
 float tMinH = (_cachedHumMin[topIdx] < 999.0f) ? _cachedHumMin[topIdx] : NAN;
 float tMaxH = (_cachedHumMax[topIdx] > -999.0f) ? _cachedHumMax[topIdx] : NAN;
 _displayMgr->setTopSlotMinMax(tMinT, tMaxT, tMinH, tMaxH);
 } else {
 _displayMgr->setTopSlotMinMax(sMinT, sMaxT, sMinH, sMaxH);
 }
 }
 }

 if (_sensorMgr->hasNewReadings( )) {
 const auto& sensors = _sensorMgr->getRuntimeSensors( );
 SystemConfig &cfg = _storageMgr->getConfig( );
 int topIdx = _displayMgr->getTopSlotIdx( );

 for (const auto &s : sensors) {
 /* Selected slot data */
 if (_currentSensorIdx >= 0 && _currentSensorIdx < MAX_SENSORS &&
 cfg.sensors[_currentSensorIdx].active &&
 cfg.sensors[_currentSensorIdx].pins[0] == s.config.pins[0]) {
 _displayMgr->setSlotData(s.avgValue[0], s.avgValue[1], s.avgValue[2], s.type, !s.inErrorState,
 _currentSensorIdx, String(s.config.friendlyName));
 if (topIdx == _currentSensorIdx)
 _displayMgr->setTopSlotData(s.avgValue[0], s.avgValue[1], s.avgValue[2], s.type, !s.inErrorState,
 _currentSensorIdx, String(s.config.friendlyName));
 }
 /* Top panel (fixed elsewhere) */
 if (topIdx != _currentSensorIdx && topIdx >= 0 && topIdx < MAX_SENSORS &&
 cfg.sensors[topIdx].active &&
 cfg.sensors[topIdx].pins[0] == s.config.pins[0]) {
 _displayMgr->setTopSlotData(s.avgValue[0], s.avgValue[1], s.avgValue[2], s.type, !s.inErrorState,
 topIdx, String(s.config.friendlyName));
 }
 }

 }

 /* Keep main slot data in sync so the alpha LCD sees sensor faults
    (inErrorState changes) without waiting for a slot-change event. */
 refreshSelectedSlot( );
}
/**
 * @brief Pre-load daily Min/Max values from binary history for fast display.
 * Runs during boot to avoid flash I/O competition with the dashboard.
 * Uses ReadLock (no Core 1 pause) with 5-second budget limit.
 */
void AppManager::preloadMinMax( ) {
	 time_t now = _netMgr->getEpoch( );
	 struct tm timeinfo;
	 localtime_r(&now, &timeinfo);

	 char path[42];
	 snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_V4_FILE_EXT,
	          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

	 _storageMgr->enterFlashReadLock( );
	 bool fileExists = LittleFS.exists(path);
	 File f;
	 if (fileExists) f = LittleFS.open(path, "r");
	 _storageMgr->exitFlashReadLock( );

	 if (!fileExists || !f) return;

	 /* V4: read header into buffer, then parse.
	  * Static allocation — HistV4State (2.2KB) + hdrBuf (2KB) + pRdBuf (256B)
	  * exceeds the RP2040 ~4KB stack. Static moves these to BSS (global RAM). */
	 static HistV4State pState;
	 static uint8_t hdrBuf[HIST_V4_MAX_HEADER];
	 static uint8_t pRdBuf[HIST_V4_READ_BUF];
	 {
	 StorageManager::ReadGuard rg(_storageMgr.get( ));
	 int hdrRead = f.read(hdrBuf, sizeof(hdrBuf));
	 /* Cursor fix (same as graph/web/scan): reposition to the real
	  * header end, else small files decode nothing (cursor at EOF). */
	 size_t pHdrLen = (hdrRead >= (int)HIST_V4_HEADER_FIXED)
	                  ? histV4ReadHeaderBuf(hdrBuf, (size_t)hdrRead, pState) : 0;
	 if (pHdrLen == 0) {
	 f.close( );
	 return;
	 }
	 f.seek(pHdrLen);
	 }

	 size_t pRdFilled = 0;

	 /* ── M1: mapa medição → slot, resolvido UMA VEZ por arquivo ──────────
	  * A tabela de medições é fixa para o arquivo inteiro, mas o laço
	  * abaixo fazia histV4StrPoolGet + varredura de MAX_SENSORS para CADA
	  * medição de CADA registro — a mesma resposta recalculada 1.440 × 4
	  * vezes num dia cheio. É o padrão que derrubava o handler web por
	  * watchdog (416f2dc); aqui o teto de 5 s evita o reboot mas entrega
	  * cache PARCIAL: min/max errados no dashboard após reboot à tarde. */
	 int8_t measSlot[HIST_V4_MAX_MEASUREMENTS];
	 {
	 SystemConfig &cfg = _storageMgr->getConfig( );
	 for (uint8_t m = 0; m < pState.measureCount; m++) {
	 measSlot[m] = -1;                       /* -1 = medição sem dono ativo */
	 uint8_t sIdx = pState.measures[m].sensorIdx;
	 if (sIdx >= pState.sensorCount) continue;

	 char hwId[17];
	 histV4StrPoolGet(hwId, sizeof(hwId), pState.strPool,
	                  pState.sensors[sIdx].hwIdOffset,
	                  pState.sensors[sIdx].hwIdLen);

	 for (int i = 0; i < MAX_SENSORS; i++) {
	 if (cfg.sensors[i].active && strcmp(cfg.sensors[i].hwId, hwId) == 0) {
	 measSlot[m] = (int8_t)i;
	 break;
	 }
	 }
	 }
	 }

	 uint32_t _preloadBudget = millis( );
	 bool hasMore = true;

	 while (hasMore) {
	 if (timeSince(_preloadBudget, 5000)) {
	 LOG_CODE(LOG_WARN, "APP", APP_PRELOAD_BUDGET, 0, "");
	 { StorageManager::ReadGuard rg(_storageMgr.get( )); f.close( ); }
	 LOG_CODE(LOG_INFO, "APP", APP_CACHE_MINMAX_PARTIAL, 0, "");
	 return;
	 }

	 _storageMgr->enterFlashReadLock( );
	 /* Batch-decode up to 20 records — static to avoid 10KB stack allocation */
	 static int64_t s_batchVals[20][HIST_V4_MAX_MEASUREMENTS];
	 static uint32_t s_batchEpochs[20];
	 int count = 0;
	 while (count < 20) {
	 /* A1: o limiar antigo (`pRdFilled < anchorByteSize`) não reabastecia
	  * quando o buffer tinha bytes suficientes para uma âncora mas não
	  * para um delta grande — o preload parava no meio do dia e o
	  * dashboard ficava com min/max de meia manhã. */
	 size_t consumed = histV4DecodeNextRefill(
	 pRdBuf, sizeof(pRdBuf), pRdFilled, pState,
	 s_batchVals[count], &s_batchEpochs[count],
	 [&f](uint8_t *dst, size_t maxBytes) -> size_t {
	 if (f.available( ) <= 0) return 0;
	 int rN = f.read(dst, maxBytes);
	 return (rN > 0) ? (size_t)rN : 0;
	 });
	 if (consumed == 0) break;
	 count++;
	 }
	 hasMore = (pRdFilled > 0 || f.available( ) > 0);
	 _storageMgr->exitFlashReadLock( );

	 /* Process batch: map measurements to slot-indexed caches */
	 for (int b = 0; b < count; b++) {
	 for (uint8_t m = 0; m < pState.measureCount; m++) {
	 if (histV4IsNan(s_batchVals[b][m], pState.measures[m].bitWidth)) continue;

	 /* M1: slot já resolvido por arquivo — sem string pool nem
	  * varredura de sensores aqui dentro. */
	 const int slot = measSlot[m];
	 if (slot < 0) continue;

	 float v = histV4ToFloat(s_batchVals[b][m], pState.measures[m]);
	 if (isnan(v)) continue;

	 uint8_t ch = pState.measures[m].channel;
	 if (ch == CH_TEMP) {
	 if (v < _cachedMin[slot]) _cachedMin[slot] = v;
	 if (v > _cachedMax[slot]) _cachedMax[slot] = v;
	 } else if (ch == CH_HUM) {
	 if (v < _cachedHumMin[slot]) _cachedHumMin[slot] = v;
	 if (v > _cachedHumMax[slot]) _cachedHumMax[slot] = v;
	 }
	 }
	 }

	 feedWdt( );
	 delay(2);
	 }


		 /* Close file under read lock */
		 { StorageManager::ReadGuard rg(_storageMgr.get( )); f.close( ); }

	 /* Snapshot: save a copy of caches so live values can separate preload from real-time */
	 memcpy(_preloadMin, _cachedMin, sizeof(_preloadMin));
	 memcpy(_preloadMax, _cachedMax, sizeof(_preloadMax));
	 memcpy(_preloadHumMin, _cachedHumMin, sizeof(_preloadHumMin));
	 memcpy(_preloadHumMax, _cachedHumMax, sizeof(_preloadHumMax));

	 LOG_CODE(LOG_INFO, "APP", APP_CACHE_PRELOAD_DONE, 0, "");
}

void AppManager::processHistoryLogging( ) {
 _lastHistoryTime = millis( );
 time_t now = _netMgr->getEpoch( );

 /* History only saves with a valid time reference (NTP synced OR
 * provisional active). Threshold 1600000000 = approximate Unix
 * timestamp for 2020-09-13; below that is fallback ~1970 without
 * any time reference. Without the gate, records would get epoch=0
 * or bizarre values, contaminating telemetry + UI.
 *
 * Warn-once: without the warning, it could go minutes/hours losing
 * records with no indication. Logs once when entering the state,
 * once when leaving. */
 if (now <= 1600000000) {
 if (!_histTimeRefWarned) {
 LOG_CODE(LOG_WARN, "HIST", APP_HIST_NO_TIME_REF, 0,
 TRL("History skip: no time reference (NTP off + no provisional)"));
 _histTimeRefWarned = true;
 }
 return;
 }
 /* Recovered: log once when resuming saves after a period without time ref. */
 if (_histTimeRefWarned) {
 LOG_CODE(LOG_INFO, "HIST", APP_HIST_TIME_REF_RECOVERED, 0,
 TRL("History resumed: time reference acquired"));
 _histTimeRefWarned = false;
 }

 {
 const auto& sensors = _sensorMgr->getRuntimeSensors( );

	 /* ── Build V4 universal record (schema-driven, no legacy compat) ── */
	 const HistV4State* schema = _storageMgr->getV4Schema( );
	 if (!schema) {
	  /* Bootstrap: first-ever history call — create the .sim4 file with
	   * schema header. The codec starts invalid and only becomes valid
	   * inside writeHistoryEntryFlashV4(). One-time init, then real data
	   * flows on the next history interval. */
	  _storageMgr->ensureV4Schema( );
	  return;
	 }
	 if (schema->measureCount == 0) {
	  if (!_histSchemaEmptyWarned) {
	   LOG_CODE(LOG_WARN, "HIST", APP_HIST_NO_SCHEMA, 0, "V4 schema empty — no active sensors?");
	   _histSchemaEmptyWarned = true;
	  }
	  return;
	 }

	 SystemConfig &cfg = _storageMgr->getConfig( );
	 int64_t values[HIST_V4_MAX_MEASUREMENTS];
	 uint8_t mc = schema->measureCount;
	 uint8_t matched = 0; /* channels that actually took a live reading */
	 for (uint8_t m = 0; m < mc; m++) {
	 	 values[m] = histV4NanSentinel(schema->measures[m].bitWidth);
	 }

	 for (int slot = 0; slot < MAX_SENSORS; slot++) {
	 	 if (!cfg.sensors[slot].active) continue;
	 	 for (const auto &s : sensors) {
	 	 	 if (s.config.pins[0] != cfg.sensors[slot].pins[0] || s.inErrorState) continue;

	 	 	 for (uint8_t m = 0; m < mc; m++) {
	 	 	 	 if (schema->measures[m].sensorIdx >= schema->sensorCount) continue;
	 	 	 	 char sensorHwId[17];
	 	 	 	 histV4StrPoolGet(sensorHwId, sizeof(sensorHwId),
	 	 	 	 	 schema->strPool,
	 	 	 	 	 schema->sensors[schema->measures[m].sensorIdx].hwIdOffset,
	 	 	 	 	 schema->sensors[schema->measures[m].sensorIdx].hwIdLen);
	 	 	 	 if (strcmp(sensorHwId, cfg.sensors[slot].hwId) != 0) continue;

	 	 	 	 uint8_t ch = schema->measures[m].channel;
	 	 	 	 float v = s.avgValue[ch];
	 	 	 	 /* isfinite: INFINITY passa por isnan e virava 1022 no clamp. */
	 	 	 	 if (!isfinite(v)) continue;

	 	 	 	 values[m] = histV4FromFloat(v, schema->measures[m]);
	 	 	 	 matched++;

	 	 	 	 /* Update cache (indexed by slot for display compatibility) */
	 	 	 	 if (ch == CH_TEMP) {
	 	 	 	 	 if (v < _cachedMin[slot]) _cachedMin[slot] = v;
	 	 	 	 	 if (v > _cachedMax[slot]) _cachedMax[slot] = v;
	 	 	 	 	 if (v < _preloadMin[slot]) _preloadMin[slot] = v;
	 	 	 	 	 if (v > _preloadMax[slot]) _preloadMax[slot] = v;
	 	 	 	 } else if (ch == CH_HUM) {
	 	 	 	 	 if (v < _cachedHumMin[slot]) _cachedHumMin[slot] = v;
	 	 	 	 	 if (v > _cachedHumMax[slot]) _cachedHumMax[slot] = v;
	 	 	 	 	 if (v < _preloadHumMin[slot]) _preloadHumMin[slot] = v;
	 	 	 	 	 if (v > _preloadHumMax[slot]) _preloadHumMax[slot] = v;
	 	 	 	 }
	 	 	 }
	 	 	 break;
	 	 }
	 }

	 /* Every channel is still the NaN sentinel: nothing the sensors reported
	  * lined up with the schema. Writing that produces a record with a valid
	  * timestamp and no data — which is exactly what happened on the bench for
	  * hours while APP_HISTORY_SAVED kept claiming success, because
	  * writeHistoryEntryV4 succeeds regardless of what the values are.
	  *
	  * The usual cause is a sensor identity change mid-day: the schema lives in
	  * the .sim4 header and is restored from the file by ensureV4Schema, so
	  * editing a hwId leaves the match in strcmp(schemaHwId, cfg hwId) failing
	  * until tomorrow's file. Refuse the record and say so — an empty row is
	  * worse than a gap, because it looks like data. */
	 if (matched == 0) {
	 	 if (!_histSchemaMismatchWarned) {
	 	 	 LOG_CODE(LOG_WARN, "HIST", APP_HIST_SCHEMA_MISMATCH, (int)mc,
	 	 	          TRL("History skip: schema matches no active sensor (hwId changed?)"));
	 	 	 _histSchemaMismatchWarned = true;
	 	 }
	 	 return;
	 }
	 if (_histSchemaMismatchWarned) {
	 	 LOG_CODE(LOG_INFO, "HIST", APP_HIST_SCHEMA_MISMATCH, (int)matched,
	 	          TRL("History resumed: schema matches active sensors"));
	 	 _histSchemaMismatchWarned = false;
	 }

	 if (_storageMgr->writeHistoryEntryV4(values, mc, (uint32_t)now)) {
	 	 LOG_CODE(LOG_INFO, "HIST", APP_HISTORY_SAVED, 0, "");
	 	 _telemetryMgr->notifyNewRecord( );
	 }
 }

 /* Periodic heap log — only when low or once per hour (avoids premature log rotation) */
 {
 uint32_t heapFree = rp2040.getFreeHeap( );
 static uint32_t lastFullHeapLog = 0;

 if (heapFree < 32768 || timeSince(lastFullHeapLog, 3600000)) {
 char heapMsg[48];
 snprintf(heapMsg, sizeof(heapMsg), "Heap: %lu free / %lu total",
 (unsigned long)heapFree,
 (unsigned long)rp2040.getTotalHeap( ));
 LOG_CODE(LOG_INFO, "SYS", APP_HEAP_REPORT, (int)(heapFree/1024), heapMsg);
 lastFullHeapLog = millis( );
 }

 /* Alert when heap drops below 16KB (margin for WiFi+TLS) */
 if (heapFree < 16384) {
 LOG_CODE(LOG_WARN, "SYS", SYS_HEAP_LOW, (int)(heapFree/1024), "");
 }
 }
}

void AppManager::openStatsScreen(int sensorId) {
 /**
 * IMPORTANT: pkg must be static to avoid stack overflow.
 * GraphDataPackage is ~3.2KB — exceeds RP2040 stack (~4KB).
 * Same pattern used in renderGraphOptimized( ).
 */
 static GraphDataPackage pkg;
 memset(&pkg, 0, sizeof(GraphDataPackage));
 pkg.sensorIdx = sensorId;
 pkg.timeRange = 3;
 pkg.count = 0;
 pkg.idxMinTemp = -1;
 pkg.idxMaxTemp = -1;
 pkg.avgTemp = NAN;
 pkg.stdTemp = NAN;
 pkg.deltaTemp = NAN;
 pkg.avgHum = NAN;
 pkg.stdHum = NAN;
 pkg.deltaHum = NAN;

 int cacheIdx = sensorId;
 if (sensorId == (int)MINMAX_SLOT_BOARD_TEMP) cacheIdx = MINMAX_SLOT_BOARD_TEMP;
 else if (cacheIdx < 0 || cacheIdx >= MINMAX_SLOT_COUNT) cacheIdx = 0;

 pkg.minVal = _cachedMin[cacheIdx];
 pkg.maxVal = _cachedMax[cacheIdx];

 if (pkg.minVal == 1000.0f) pkg.minVal = 0.0f;
 if (pkg.maxVal == -1000.0f) pkg.maxVal = 0.0f;

 float humMin = _cachedHumMin[cacheIdx];
 float humMax = _cachedHumMax[cacheIdx];
 if (humMin == 1000.0f) humMin = 0.0f;
 if (humMax == -1000.0f) humMax = 0.0f;

 SystemConfig &cfg = _storageMgr->getConfig( );

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
 snprintf(pkg.rom, sizeof(pkg.rom), "%02X%02X%02X%02X%02X%02X%02X%02X",
 cfg.sensors[sensorId].rom[0], cfg.sensors[sensorId].rom[1],
 cfg.sensors[sensorId].rom[2], cfg.sensors[sensorId].rom[3],
 cfg.sensors[sensorId].rom[4], cfg.sensors[sensorId].rom[5],
 cfg.sensors[sensorId].rom[6], cfg.sensors[sensorId].rom[7]);
 } else {
 snprintf(pkg.title, sizeof(pkg.title), "Slot %d", sensorId);
 snprintf(pkg.hwId, sizeof(pkg.hwId), "--");
 snprintf(pkg.rom, sizeof(pkg.rom), "N/A");
 }
 }
 pkg.title[31] = '\0'; pkg.hwId[15] = '\0'; pkg.rom[23] = '\0';

 _displayMgr->showStats(pkg, humMin, humMax);
}
void AppManager::checkAlarmConditions( ) {
 const auto& sensors = _sensorMgr->getRuntimeSensors( );
 SystemConfig &cfg = _storageMgr->getConfig( );
 bool anyAlarm = false;
 uint16_t mask = 0;
 int8_t firstSlot = -1;

 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!cfg.sensors[i].active || !cfg.sensors[i].alarmsActive) continue;
 uint8_t targetGpio = cfg.sensors[i].pins[0];

 for (const auto &s : sensors) {
 if (s.config.pins[0] != targetGpio || s.inErrorState) continue;

 bool tripped = false;

 if (!isnan(s.avgValue[0])) {
 if (s.avgValue[0] < cfg.sensors[i].tempMin ||
 s.avgValue[0] > cfg.sensors[i].tempMax) {
 tripped = true;
 }
 }

 if (!tripped && sensorHasHumidity(s.type) && !isnan(s.avgValue[1])) {
 if (s.avgValue[1] < cfg.sensors[i].humMin ||
 s.avgValue[1] > cfg.sensors[i].humMax) {
 tripped = true;
 }
 }

 if (tripped) {
 mask |= (1 << i);
 anyAlarm = true;
 if (firstSlot < 0) firstSlot = i;
 }
 break;
 }
 }


 bool silenced = _displayMgr->isAlarmSilenced( );

 if (anyAlarm && !_soundMgr->isAlarming( ) && !silenced) {

 _soundMgr->startAlarm( );
 if (firstSlot >= 0) {
 _currentSensorIdx = firstSlot;
 refreshSelectedSlot( );
 }
 _displayMgr->setAlarmState(mask, firstSlot);
 LOG_CODE(LOG_WARN, "APP", APP_ALARM_TRIGGERED, 0, "");
 } else if (anyAlarm && (_soundMgr->isAlarming( ) || silenced)) {

 _displayMgr->setAlarmState(mask, -1);
 } else if (!anyAlarm && (_soundMgr->isAlarming( ) || silenced)) {

 _soundMgr->stopAlarm( );
 _displayMgr->setAlarmState(0, -1);

 if (silenced) {
 _displayMgr->setAlarmSilenced(false, 0);
 LOG_CODE(LOG_INFO, "APP", APP_ALARM_SILENCE_CANCEL, 0, "");
 }
 LOG_CODE(LOG_INFO, "APP", APP_ALARM_CLEARED, 0, "");
 }
}
