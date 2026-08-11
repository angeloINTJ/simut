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

	 /* V5 turns this from a full-day decode into a header walk.
	  *
	  * Under V4 the day's ~1440 records had to be decoded one by one just to
	  * find four numbers per slot, which is why this function carried a 5 s
	  * budget and a "partial cache" log — a boot in the afternoon regularly
	  * ran out of time and left the dashboard showing min/max of mid-morning.
	  *
	  * A V5 DATA header already carries the per-channel min/max of its block
	  * (§3.3), so the answer for a whole day is 24 header reads and no
	  * payload at all. The budget stays as a guard, but nothing here is
	  * expected to approach it. */
	 const String path = _storageMgr->getHistoryFileNameV5((uint32_t)now);

	 bool opened = false;
	 {
	 StorageManager::ReadGuard rg(_storageMgr.get( ));
	 opened = LittleFS.exists(path)
	          && _storageMgr->h5OpenDay(path, /*verifyPayload=*/false);
	 }
	 if (!opened) return;

	 const uint32_t budget = millis( );
	 uint16_t blocks = 0;
	 bool truncated = false;

	 for (;;) {
	 if (timeSince(budget, 5000)) { truncated = true; break; }

	 H5DataHeader hdr;
	 const int16_t *mn = nullptr, *mx = nullptr;
	 bool got = false;
	 {
	 StorageManager::ReadGuard rg(_storageMgr.get( ));
	 got = _storageMgr->h5NextBlock(hdr, mn, mx);
	 }
	 if (!got) break;
	 blocks++;

	 const H5ChannelDesc* schema = _storageMgr->h5ReaderSchema( );
	 const uint8_t nCh = _storageMgr->h5ReaderChannels( );
	 for (uint8_t c = 0; c < nCh && schema; c++) {
	 /* id is slot*MAX_SENSOR_CHANNELS + channel by construction
	  * (StorageManager::buildH5Schema), so the slot the display
	  * indexes by comes straight out of the descriptor — no string
	  * pool, no per-record sensor scan. That whole apparatus, and
	  * the M1 hoisting that made it survivable, is gone. */
	 const uint8_t slot = (uint8_t)(schema[c].id / MAX_SENSOR_CHANNELS);
	 const uint8_t ch   = (uint8_t)(schema[c].id % MAX_SENSOR_CHANNELS);
	 if (slot >= MAX_SENSORS) continue;
	 if (mn[c] == H5_NAN_SENTINEL || mx[c] == H5_NAN_SENTINEL) continue;

	 const float scale = powf(10.0f, (float)schema[c].scaleExp);
	 const float lo = (float)mn[c] * scale;
	 const float hi = (float)mx[c] * scale;
	 if (ch == CH_TEMP) {
	 if (lo < _cachedMin[slot]) _cachedMin[slot] = lo;
	 if (hi > _cachedMax[slot]) _cachedMax[slot] = hi;
	 } else if (ch == CH_HUM) {
	 if (lo < _cachedHumMin[slot]) _cachedHumMin[slot] = lo;
	 if (hi > _cachedHumMax[slot]) _cachedHumMax[slot] = hi;
	 }
	 }

	 feedWdt( );
	 }

	 { StorageManager::ReadGuard rg(_storageMgr.get( )); _storageMgr->h5CloseDay( ); }

	 /* Snapshot: save a copy of caches so live values can separate preload from real-time */
	 memcpy(_preloadMin, _cachedMin, sizeof(_preloadMin));
	 memcpy(_preloadMax, _cachedMax, sizeof(_preloadMax));
	 memcpy(_preloadHumMin, _cachedHumMin, sizeof(_preloadHumMin));
	 memcpy(_preloadHumMax, _cachedHumMax, sizeof(_preloadHumMax));

	 if (truncated) {
	 LOG_CODE(LOG_WARN, "APP", APP_PRELOAD_BUDGET, (int)blocks, "");
	 LOG_CODE(LOG_INFO, "APP", APP_CACHE_MINMAX_PARTIAL, (int)blocks, "");
	 } else {
	 LOG_CODE(LOG_INFO, "APP", APP_CACHE_PRELOAD_DONE, (int)blocks, "");
	 }
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

	 /* ── Build the V5 record — one value per schema channel ───────────
	  * The schema comes from the provisioned slots (StorageManager
	  * ::buildH5Schema) and is emitted into the file's SCHEMA chunk, so
	  * this loop only has to fill values in that same order: slot by
	  * slot, channel by channel. */
	 _storageMgr->ensureH5Schema( );
	 const H5ChannelDesc* h5schema = _storageMgr->getH5Schema( );
	 const uint8_t nCh = _storageMgr->getH5ChannelCount( );
	 if (!h5schema || nCh == 0) {
	  if (!_histSchemaEmptyWarned) {
	   LOG_CODE(LOG_WARN, "HIST", APP_HIST_NO_SCHEMA, 0, "V5 schema empty — no active sensors?");
	   _histSchemaEmptyWarned = true;
	  }
	  return;
	 }
	 _histSchemaEmptyWarned = false;

	 SystemConfig &cfg = _storageMgr->getConfig( );
	 int16_t values[H5_MAX_CHANNELS];
	 for (uint8_t i = 0; i < nCh; i++) values[i] = H5_NAN_SENTINEL;
	 uint8_t matched = 0;   /* channels that actually took a live reading */

	 uint8_t idx = 0;
	 for (int slot = 0; slot < MAX_SENSORS && idx < nCh; slot++) {
	 	 if (!cfg.sensors[slot].active) continue;
	 	 const uint8_t stype = cfg.sensors[slot].sensorType;

	 	 /* The runtime sensor for this slot, matched by GPIO the way the
	 	  * rest of the system does. Absent or in error leaves its channels
	 	  * as NAN, which costs 1 bit per record and per channel (§3.7-1) —
	 	  * a transient dropout is not a schema change. */
	 	 const RuntimeSensor* live = nullptr;
	 	 for (const auto &s : sensors) {
	 	 	 if (s.config.pins[0] == cfg.sensors[slot].pins[0] && !s.inErrorState) {
	 	 	 	 live = &s;
	 	 	 	 break;
	 	 	 }
	 	 }

	 	 for (uint8_t ch = 0; ch < MAX_SENSOR_CHANNELS && idx < nCh; ch++) {
	 	 	 if (!sensorHasChannel((SensorType)stype, ch)) continue;
	 	 	 const ChannelInfo &ci = channelInfo(ch);
	 	 	 const uint8_t chIdx = idx++;
	 	 	 if (!live) continue;

	 	 	 const float v = live->avgValue[ch];
	 	 	 /* isfinite, not isnan: the BMP280 humidity compensation yields
	 	 	  * +INF, and isnan(inf) is false. */
	 	 	 if (!isfinite(v)) continue;

	 	 	 /* Scale must match the scaleExp buildH5Schema wrote into the
	 	 	  * SCHEMA, or the file would describe values it does not hold. */
	 	 	 float scale = (float)ci.scale;
	 	 	 if (ci.bitWidth > 16) scale = 1.0f;      /* see buildH5Schema */
	 	 	 float scaled = v * scale;
	 	 	 if (scaled > 32767.0f) scaled = 32767.0f;
	 	 	 if (scaled < -32767.0f) scaled = -32767.0f;  /* -32768 is the sentinel */
	 	 	 values[chIdx] = (int16_t)lroundf(scaled);
	 	 	 matched++;

	 	 	 /* Min/max cache, indexed by slot for the display. */
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
	 }

	 /* Every channel still the sentinel means nothing the sensors reported
	  * lined up with the schema. An all-empty row has a valid timestamp and
	  * no data, which looks like data — refuse it and say so. */
	 if (matched == 0) {
	 	 if (!_histSchemaMismatchWarned) {
	 	 	 LOG_CODE(LOG_WARN, "HIST", APP_HIST_SCHEMA_MISMATCH, (int)nCh,
	 	 	          TRL("History skip: schema matches no active sensor"));
	 	 	 _histSchemaMismatchWarned = true;
	 	 }
	 	 return;
	 }
	 if (_histSchemaMismatchWarned) {
	 	 LOG_CODE(LOG_INFO, "HIST", APP_HIST_SCHEMA_MISMATCH, (int)matched,
	 	          TRL("History resumed: schema matches active sensors"));
	 	 _histSchemaMismatchWarned = false;
	 }

	 if (_storageMgr->writeHistoryEntryV5(values, nCh, (uint32_t)now)) {
	 	 /* Only a record that really landed closes the first-sample window;
	 	  * a refusal has to leave it open so the loop retries. */
	 	 _histFirstDone = true;
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

 /* Every channel the part reports, against that channel's own pair of
  * limits. This tested temperature, then humidity if the sensor had it,
  * and stopped — so a BMP280's pressure could leave its range without
  * anything noticing, no matter what the UI offered. */
 bool tripped = false;
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS && !tripped; c++) {
 if (!sensorHasChannel(s.type, c)) continue;
 const float v = s.avgValue[c];
 if (!isfinite(v)) continue;
 if (v < cfg.sensors[i].chMin[c] || v > cfg.sensors[i].chMax[c]) {
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
