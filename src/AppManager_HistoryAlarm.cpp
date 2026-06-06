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
 const auto& sensors = _sensorMgr->getRuntimeSensors( );
 bool found = false;

 if (_currentSensorIdx < 10) {
 if (cfg.sensors[_currentSensorIdx].active) {
 uint8_t targetGpio = cfg.sensors[_currentSensorIdx].pins[0];
 for (const auto &s : sensors) {
 if (s.config.pins[0] != 10 && s.config.pins[0] == targetGpio) {
 _displayMgr->setSlotData(s.avgValue1, s.avgValue2, !s.inErrorState, _currentSensorIdx, String(s.config.friendlyName));
 found = true; break;
 }
 }
 }
 } else if (_currentSensorIdx == 10) {
 _displayMgr->setSlotData(analogReadTemp( ), NAN, true, 10, "Board (Internal)"); found = true;
 }

 if (!found) _displayMgr->setSlotData(NAN, NAN, false, _currentSensorIdx, "Empty / Inactive");
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

 /* Daily min/max (preload CSV + real-time accumulated readings) */
 float ambMinT = (_cachedMin[10] < 999.0f) ? _cachedMin[10] : NAN;
 float ambMaxT = (_cachedMax[10] > -999.0f) ? _cachedMax[10] : NAN;
 float ambMinH = (_cachedHumMin < 999.0f) ? _cachedHumMin : NAN;
 float ambMaxH = (_cachedHumMax > -999.0f) ? _cachedHumMax : NAN;
 _displayMgr->setAmbientMinMax(ambMinT, ambMaxT, ambMinH, ambMaxH);

 /* Active slot min/max */
 int slotIdx = _currentSensorIdx;
 if (slotIdx >= 0 && slotIdx < 10) {
 float sMinT = (_cachedMin[slotIdx] < 999.0f) ? _cachedMin[slotIdx] : NAN;
 float sMaxT = (_cachedMax[slotIdx] > -999.0f) ? _cachedMax[slotIdx] : NAN;
 _displayMgr->setSlotMinMax(sMinT, sMaxT);
 }
 }


 if (_sensorMgr->hasNewReadings( )) {
 const auto& sensors = _sensorMgr->getRuntimeSensors( );
 SystemConfig &cfg = _storageMgr->getConfig( );

 for (const auto &s : sensors) {
 if (s.config.pins[0] == 10) _displayMgr->setAmbientData(s.avgValue1, s.avgValue2, !s.inErrorState);
 else if (_currentSensorIdx < 10 && cfg.sensors[_currentSensorIdx].active && cfg.sensors[_currentSensorIdx].pins[0] == s.config.pins[0]) {
 _displayMgr->setSlotData(s.avgValue1, s.avgValue2, !s.inErrorState, _currentSensorIdx, String(s.config.friendlyName));
 }
 }

 if (_currentSensorIdx == 10) _displayMgr->setSlotData(analogReadTemp( ), NAN, true, 10, "Board (Internal)");
 }
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

 char path[40];
 snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

 File f;
 _storageMgr->enterFlashReadLock( );
 bool fileExists = LittleFS.exists(path);
 if (fileExists) f = LittleFS.open(path, "r");
 _storageMgr->exitFlashReadLock( );

 if (fileExists && f) {
 /* v2: validate SIM2 header. */
 HistoryFileHeaderV2 hdrP;
 bool headerOkP = false;
 {
 StorageManager::ReadGuard rg(_storageMgr.get( ));
 if (f.size( ) >= HIST_V2_HEADER_SIZE) {
 f.seek(0);
 if (f.read((uint8_t*)&hdrP, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
 headerOkP = (memcmp(hdrP.magic, HIST_V2_MAGIC, 4) == 0 &&
 hdrP.version == HIST_V2_VERSION &&
 hdrP.anchorPeriod > 0);
 }
 }
 }
 if (!headerOkP) {
 { StorageManager::ReadGuard rg(_storageMgr.get( )); f.close( ); }
 return;
 }

 HistoryCodecState pState;
 historyCodecReset(pState);
 uint16_t pAnchorPeriod = hdrP.anchorPeriod;
 uint8_t pRdBuf[256];
 size_t pRdFilled = 0;

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
 BinaryHistoryRecord batch[20];
 int count = 0;
 while (count < 20) {
 if (pRdFilled < HIST_V2_MAX_DELTA_SIZE && f.available( ) > 0) {
 int rN = f.read(pRdBuf + pRdFilled, sizeof(pRdBuf) - pRdFilled);
 if (rN > 0) pRdFilled += (size_t)rN;
 }
 if (pRdFilled == 0) break;
 bool isAnc = (pState.recordsSinceAnchor == 0) ||
 (pState.recordsSinceAnchor == pAnchorPeriod);
 size_t consumed = historyDecodeRecord(pRdBuf, pRdFilled, pState,
 batch[count], isAnc);
 if (consumed == 0) break;
 memmove(pRdBuf, pRdBuf + consumed, pRdFilled - consumed);
 pRdFilled -= consumed;
 count++;
 }
 hasMore = (pRdFilled > 0 || f.available( ) > 0);
 _storageMgr->exitFlashReadLock( );

 /* Process batch outside the lock */
 for (int b = 0; b < count; b++) {
 const BinaryHistoryRecord& rec = batch[b];

 float ambT = BinaryHistoryRecord::i16ToFloat(rec.ambientTemp);
 if (!isnan(ambT)) {
 if (ambT < _cachedMin[10]) _cachedMin[10] = ambT;
 if (ambT > _cachedMax[10]) _cachedMax[10] = ambT;
 }

 float ambH = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);
 if (!isnan(ambH)) {
 if (ambH < _cachedHumMin) _cachedHumMin = ambH;
 if (ambH > _cachedHumMax) _cachedHumMax = ambH;
 }

 for (int i = 0; i < MAX_SENSORS; i++) {
 float v = BinaryHistoryRecord::i16ToFloat(rec.sensors[i]);
 if (!isnan(v)) {
 if (v < _cachedMin[i]) _cachedMin[i] = v;
 if (v > _cachedMax[i]) _cachedMax[i] = v;
 }
 }
 }

 feedWdt( );
 delay(2);
 }

 _storageMgr->enterFlashReadLock( );
 f.close( );
 _storageMgr->exitFlashReadLock( );
 }

 /* Save preload snapshot (CSV data only, without real-time readings) */
 for (int i = 0; i < MINMAX_SLOT_COUNT; i++) {
 _preloadMin[i] = _cachedMin[i];
 _preloadMax[i] = _cachedMax[i];
 }
 _preloadHumMin = _cachedHumMin;
 _preloadHumMax = _cachedHumMax;

 LOG_CODE(LOG_INFO, "APP", APP_CACHE_MINMAX_FULL, 0, "");
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
 SystemConfig &cfg = _storageMgr->getConfig( );

 /* ── Build binary record ── */
 BinaryHistoryRecord rec;
 rec.clear( );
 rec.epoch = (uint32_t)now;

 /* Ambient sensor (DHT22 on GPIO 10) */
 float ambT = NAN, ambH = NAN;
 for (const auto &s : sensors) {
 if (s.config.pins[0] == 10 && !s.inErrorState) {
 ambT = s.avgValue1;
 ambH = s.avgValue2;

 if (!isnan(ambT)) {
 if (ambT < _cachedMin[10]) _cachedMin[10] = ambT;
 if (ambT > _cachedMax[10]) _cachedMax[10] = ambT;
 if (ambT < _preloadMin[10]) _preloadMin[10] = ambT;
 if (ambT > _preloadMax[10]) _preloadMax[10] = ambT;
 }
 if (!isnan(ambH)) {
 if (ambH < _cachedHumMin) _cachedHumMin = ambH;
 if (ambH > _cachedHumMax) _cachedHumMax = ambH;
 if (ambH < _preloadHumMin) _preloadHumMin = ambH;
 if (ambH > _preloadHumMax) _preloadHumMax = ambH;
 }
 break;
 }
 }

 rec.ambientTemp = BinaryHistoryRecord::floatToI16(ambT);
 rec.ambientHum = BinaryHistoryRecord::floatToI16(ambH);

 /* DS18B20 sensors (slots 0..9) */
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfg.sensors[i].active) {
 for (const auto &s : sensors) {
 if (s.config.pins[0] == cfg.sensors[i].pins[0] && !s.inErrorState) {
 float v = s.avgValue1;
 if (!isnan(v)) {
 rec.sensors[i] = BinaryHistoryRecord::floatToI16(v);
 if (v < _cachedMin[i]) _cachedMin[i] = v;
 if (v > _cachedMax[i]) _cachedMax[i] = v;
 if (v < _preloadMin[i]) _preloadMin[i] = v;
 if (v > _preloadMax[i]) _preloadMax[i] = v;
 }
 break;
 }
 }
 }
 }

 if (_storageMgr->writeHistoryEntry(rec)) {
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

 int cacheIdx = (sensorId == -1) ? 10 : sensorId;
 if (cacheIdx < 0 || cacheIdx > 10) cacheIdx = 10;

 pkg.minVal = _cachedMin[cacheIdx];
 pkg.maxVal = _cachedMax[cacheIdx];

 if (pkg.minVal == 1000.0f) pkg.minVal = 0.0f;
 if (pkg.maxVal == -1000.0f) pkg.maxVal = 0.0f;

 float humMin = _cachedHumMin;
 float humMax = _cachedHumMax;
 if (humMin == 1000.0f) humMin = 0.0f;
 if (humMax == -1000.0f) humMax = 0.0f;

 SystemConfig &cfg = _storageMgr->getConfig( );
 pkg.hasHumidity = (sensorId == -1);

 if (sensorId == -1) {
 snprintf(pkg.title, sizeof(pkg.title), "%s", _displayMgr->tr(TR_AMBIENT));
 snprintf(pkg.hwId, sizeof(pkg.hwId), "AMB");
 snprintf(pkg.rom, sizeof(pkg.rom), "INTERNAL-DHT");
 } else if (sensorId == 10) {
 snprintf(pkg.title, sizeof(pkg.title), "Board Temp");
 snprintf(pkg.hwId, sizeof(pkg.hwId), "SYS");
 snprintf(pkg.rom, sizeof(pkg.rom), "RP2040-ADC");
 } else {
 if (cfg.sensors[sensorId].active) {
 safeCopy(pkg.title, cfg.sensors[sensorId].friendlyName, sizeof(pkg.title));
 safeCopy(pkg.hwId, cfg.sensors[sensorId].hwId, sizeof(pkg.hwId));
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

 _displayMgr->showStats(pkg, humMin, humMax);
}
void AppManager::checkAlarmConditions( ) {
 const auto& sensors = _sensorMgr->getRuntimeSensors( );
 SystemConfig &cfg = _storageMgr->getConfig( );
 bool anyAlarm = false;
 uint16_t mask = 0;
 int8_t firstSlot = -1;


 bool ambTempAlarm = false;
 bool ambHumAlarm = false;
 if (cfg.ambientSensor.alarmsActive) {
 for (const auto &s : sensors) {
 if (s.config.pins[0] != 10 || s.inErrorState) continue;
 if (!isnan(s.avgValue1)) {
 if (s.avgValue1 < cfg.ambientSensor.tempMin ||
 s.avgValue1 > cfg.ambientSensor.tempMax) {
 ambTempAlarm = true;
 anyAlarm = true;
 }
 }
 if (s.type == TYPE_DHT22 && !isnan(s.avgValue2)) {
 if (s.avgValue2 < cfg.ambientSensor.humMin ||
 s.avgValue2 > cfg.ambientSensor.humMax) {
 ambHumAlarm = true;
 anyAlarm = true;
 }
 }
 break;
 }
 }


 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!cfg.sensors[i].active || !cfg.sensors[i].alarmsActive) continue;
 uint8_t targetGpio = cfg.sensors[i].pins[0];

 for (const auto &s : sensors) {
 if (s.config.pins[0] != targetGpio || s.inErrorState) continue;

 bool tripped = false;

 if (!isnan(s.avgValue1)) {
 if (s.avgValue1 < cfg.sensors[i].tempMin ||
 s.avgValue1 > cfg.sensors[i].tempMax) {
 tripped = true;
 }
 }

 if (!tripped && s.type == TYPE_DHT22 && !isnan(s.avgValue2)) {
 if (s.avgValue2 < cfg.sensors[i].humMin ||
 s.avgValue2 > cfg.sensors[i].humMax) {
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
 _displayMgr->setAlarmState(mask, firstSlot, ambTempAlarm, ambHumAlarm);
 LOG_CODE(LOG_WARN, "APP", APP_ALARM_TRIGGERED, 0, "");
 } else if (anyAlarm && (_soundMgr->isAlarming( ) || silenced)) {

 _displayMgr->setAlarmState(mask, -1, ambTempAlarm, ambHumAlarm);
 } else if (!anyAlarm && (_soundMgr->isAlarming( ) || silenced)) {

 _soundMgr->stopAlarm( );
 _displayMgr->setAlarmState(0, -1, false, false);

 if (silenced) {
 _displayMgr->setAlarmSilenced(false, 0);
 LOG_CODE(LOG_INFO, "APP", APP_ALARM_SILENCE_CANCEL, 0, "");
 }
 LOG_CODE(LOG_INFO, "APP", APP_ALARM_CLEARED, 0, "");
 }
}
