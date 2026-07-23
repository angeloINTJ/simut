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
 _cmdMgr->renderScanResults(results);
 loadAndCalibrateSensors( );
 _cmdMgr->printPrompt( );
 }
}
void AppManager::loadAndCalibrateSensors( ) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 /* initRuntimeSensors deferred to after calibration + auto-ID */

 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!cfg.sensors[i].active) continue;

 if (cfg.sensors[i].sensorType == TYPE_DS18B20) {
 /* DS18B20: calibration via ROM (1-Wire address) */
 String dbId; float dbOffset = 0.0f; String dbName;
 if (_storageMgr->getCalibrationData(cfg.sensors[i].rom, dbId, dbOffset, dbName)) {
 _sensorMgr->applyCalibration(cfg.sensors[i].pins[0], dbId, dbOffset, dbName);
 if (dbId.length( ) > 0) { safeCopy(cfg.sensors[i].hwId, dbId.c_str( ), sizeof(cfg.sensors[i].hwId)); }
 if (dbName.length( ) > 0) { safeCopy(cfg.sensors[i].friendlyName, dbName.c_str( ), sizeof(cfg.sensors[i].friendlyName)); }
 }
 } else if (sensorHasHumidity((SensorType)cfg.sensors[i].sensorType)) {
 /* DHT22/BME280: calibration via picoUID (board-specific key).
 * Line `t<id>` = ID + name + temperature offset.
 * Line `u<id>` = humidity offset only (ID/name from `t` line). */
 String dbIdT, dbNameT, dbIdU, dbNameU;
 float dbOffsetT = 0.0f, dbOffsetU = 0.0f;
 bool gotT = _storageMgr->getCalibrationDataAmbient('t', dbIdT, dbOffsetT, dbNameT);
 bool gotU = _storageMgr->getCalibrationDataAmbient('u', dbIdU, dbOffsetU, dbNameU);
 if (gotT) {
  bool isAuto = (cfg.sensors[i].hwId[0] == '\0' || strncmp(cfg.sensors[i].hwId, "STH", 3) == 0);
  if (dbIdT.length( ) > 0 && isAuto) { safeCopy(cfg.sensors[i].hwId, dbIdT.c_str( ), sizeof(cfg.sensors[i].hwId)); }
  if (dbNameT.length( ) > 0 && isAuto) { safeCopy(cfg.sensors[i].friendlyName, dbNameT.c_str( ), sizeof(cfg.sensors[i].friendlyName)); }
 }
 _sensorMgr->applyCalibration(cfg.sensors[i].pins[0], dbIdT, dbOffsetT, dbNameT);
 if (gotT || gotU) {
 _sensorMgr->applyAmbientCalibration(dbOffsetT, dbOffsetU);
 }
 }
 }

  /* Auto-generate hardware IDs for sensors without one.
   * Format: <TYPE><2D-SLOT> (e.g. BMP28000, DHT2201, DS18B2004) */
  for (int i = 0; i < MAX_SENSORS; i++) {
   if (!cfg.sensors[i].active) continue;
   /* Skip if hwId is already set (not empty and not the default STH prefix) */
  if (cfg.sensors[i].hwId[0] != '\0' && strncmp(cfg.sensors[i].hwId, "STH", 3) != 0) continue;
   const char* tName = sensorTypeName((SensorType)cfg.sensors[i].sensorType);
   snprintf(cfg.sensors[i].hwId, sizeof(cfg.sensors[i].hwId),
            "%s%02d", tName, i);
   if (cfg.sensors[i].friendlyName[0] == '\0'
       || strncmp(cfg.sensors[i].friendlyName, "Simut_casa", 10) == 0) {
    snprintf(cfg.sensors[i].friendlyName, sizeof(cfg.sensors[i].friendlyName),
             "%s #%d", tName, i);
   }
  }

  _sensorMgr->initRuntimeSensors(cfg);
 LOG_CODE(LOG_INFO, "APP", APP_SENSORS_CALIBRATED, 0, "");
}
