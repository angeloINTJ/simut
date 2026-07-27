/**
 * @file AppManager_Sensors.cpp
 * @brief Sensor health: auto-heal, background scan, calibration.
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "CommandManager.h"
#include "LogManager.h"
#include "SensorManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include <vector>

void AppManager::checkAndAutoHealSensors( ) {
 if (_sensorMgr->isScanning( )) return;
 SystemConfig &cfg = _storageMgr->getConfig( );

 for (uint8_t gpio = 0; gpio < MAX_SENSORS; gpio++) {
 if (!cfg.sensors[gpio].active) continue;

 /* 1-Wire ROM check only applies to DS18B20 sensors.
  * DHT22, BME280, and other types use their own driver-specific
  * error detection — don't flag them as missing here. */
 if (cfg.sensors[gpio].sensorType != TYPE_DS18B20) continue;

 /* Skip ROM verification if config ROM is all zeros — unpaired sensor
  * accepts any DS18B20 on the bus (no hardware mismatch possible). */
 bool romIsZero = true;
 for (int k = 0; k < 8; k++) if (cfg.sensors[gpio].rom[k] != 0) romIsZero = false;
 if (romIsZero) continue;

 uint8_t foundRom[8];
#if SIMUT_SENSOR_DS18B20
 if (_sensorMgr->identifyPhysicalSensor(gpio, foundRom)) {
 if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) continue;

 if (memcmp(cfg.sensors[gpio].rom, foundRom, 8) != 0) {
 _sensorMgr->setHardwareMismatch(gpio, true);
 } else {
 _sensorMgr->setHardwareMismatch(gpio, false);
 }
 } else {
 /* DS18B20 configured but not found on the 1-Wire bus */
 static uint32_t lastMissingLog[MAX_SENSORS] = {0};
 if (timeSince(lastMissingLog[gpio], 60000)) {
 lastMissingLog[gpio] = millis( );
 LOG_CODE(LOG_WARN, "SENSOR", ERR_SENSOR_MISSING, gpio,
 String(cfg.sensors[gpio].friendlyName));
 }
 }
#endif /* SIMUT_SENSOR_DS18B20 */
 }
}

void AppManager::processBackgroundScan( ) {
 std::vector<ScanResult> results;
 if (_sensorMgr->getScanResults(results)) {
 _waitingScan = false;
#if SIMUT_CLI_FULL
 /* Console rendering only — the scan itself stays available so the web
  * action can drive it without the CLI. */
 _cmdMgr->renderScanResults(results);
#endif
 loadAndCalibrateSensors( );
 _cmdMgr->printPrompt( );
 }
}
void AppManager::loadAndCalibrateSensors( ) {
 SystemConfig &cfg = _storageMgr->getConfig( );

 /* ── 1. Identity ──
  * Auto-generate hardware IDs for slots without one:
  * <TYPE><2D-SLOT> (e.g. BMP28000, DHT2201, DS18B2004).
  *
  * Runs FIRST: the calibration rows of ROM-less sensors are keyed by hwId,
  * so every active slot needs one before the lookup below.
  *
  * EMPTY IS THE ONLY TRIGGER. This also fired when the hwId began with
  * "STH" — a marker from an older scheme meaning "auto-assigned, safe to
  * replace". The current generator never emits that prefix, so the clause
  * could only ever hit an id a USER had chosen, and it silently rewrote it
  * on the next boot: type STH0001, save, reboot, get DHT2202 back, with no
  * error anywhere. Easy to walk into on a board whose DS18B20s are already
  * named STM0001/STM0002 — one letter away.
  *
  * Keeping a stored id is also the safer half of the trade: the V4 history
  * schema keys every measurement by hwId, so rewriting one behind the
  * user's back stops that sensor being recorded for the rest of the day. */
 for (int i = 0; i < MAX_SENSORS; i++) {
  if (!cfg.sensors[i].active) continue;
  if (cfg.sensors[i].hwId[0] != '\0') continue;
  const char* tName = sensorTypeName((SensorType)cfg.sensors[i].sensorType);
  snprintf(cfg.sensors[i].hwId, sizeof(cfg.sensors[i].hwId),
           "%s%02d", tName, i);
  if (cfg.sensors[i].friendlyName[0] == '\0'
      || strncmp(cfg.sensors[i].friendlyName, "Simut_casa", 10) == 0) {
   snprintf(cfg.sensors[i].friendlyName, sizeof(cfg.sensors[i].friendlyName),
            "%s #%d", tName, i);
  }
 }

 /* ── 2. Runtime ──
  * BEFORE calibration, not after. initRuntimeSensors rebuilds the vector
  * from scratch with every calibrationOffset back at 0.0f, so applying the
  * offsets first wrote them into the vector that was about to be discarded
  * and no stored offset ever reached a running sensor. */
 _sensorMgr->initRuntimeSensors(cfg);

 /* initRuntimeSensors may have retyped an I2C slot from its chip ID
  * (BME280 <-> BMP280). That correction lives in the runtime copy; write it
  * back to the config so the channel set is right for everything that reads
  * cfg instead of runtime — the V4 history schema, /api/alarms, /api/calib —
  * and so the next boot does not have to discover it again. */
 if (_sensorMgr->takeRetypedCount( ) > 0) {
  const auto& rt = _sensorMgr->getRuntimeSensors( );
  for (const auto& rs : rt) {
   for (int i = 0; i < MAX_SENSORS; i++) {
    if (cfg.sensors[i].active && cfg.sensors[i].pins[0] == rs.config.pins[0]) {
     cfg.sensors[i].sensorType = (uint8_t)rs.type;
    }
   }
  }
  _storageMgr->saveConfiguration( );
 }

 /* ── 3. Offsets ── */
 for (int i = 0; i < MAX_SENSORS; i++) {
  if (!cfg.sensors[i].active) continue;

  if (cfg.sensors[i].sensorType == TYPE_DS18B20) {
   /* 1-Wire: keyed by ROM. The row also carries the ID and name the probe
    * was adopted with, which is how `sensor accept` restores them. */
   String dbId; float dbOffset = 0.0f; String dbName;
   if (_storageMgr->getCalibrationData(cfg.sensors[i].rom, dbId, dbOffset, dbName)) {
    if (dbId.length( ) > 0) { safeCopy(cfg.sensors[i].hwId, dbId.c_str( ), sizeof(cfg.sensors[i].hwId)); }
    if (dbName.length( ) > 0) { safeCopy(cfg.sensors[i].friendlyName, dbName.c_str( ), sizeof(cfg.sensors[i].friendlyName)); }
    _sensorMgr->applyCalibration(cfg.sensors[i].pins[0], dbId, dbOffset, dbName);
   }
  } else {
   /* No ROM (DHT22, BMP280): keyed by the board serial, one row per
    * quantity, each tagged with this slot's hwId — `t<hwId>` for
    * temperature, `u<hwId>` for humidity.
    *
    * This used to be a single device-wide pair found by "first row whose
    * id starts with t/u" and pushed onto "the first DHT22 in the runtime
    * list". Two DHT22s on one board shared one offset, and which sensor
    * got it depended on slot order. */
   float offT = 0.0f, offH = 0.0f;
   String unusedName;
   bool gotT = _storageMgr->getCalibrationByHwId('t', cfg.sensors[i].hwId, offT, unusedName);
   bool gotH = false;
   if (sensorHasHumidity((SensorType)cfg.sensors[i].sensorType)) {
    gotH = _storageMgr->getCalibrationByHwId('u', cfg.sensors[i].hwId, offH, unusedName);
   }
   if (gotT || gotH) {
    _sensorMgr->applyCalibrationOffsets(cfg.sensors[i].pins[0],
                                        gotT ? offT : 0.0f,
                                        gotH ? offH : 0.0f);
   }
  }
 }
 LOG_CODE(LOG_INFO, "APP", APP_SENSORS_CALIBRATED, 0, "");
}
