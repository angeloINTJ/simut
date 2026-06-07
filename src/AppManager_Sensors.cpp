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
 /* Sensor configured but not found on the physical bus */
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
 _sensorMgr->initRuntimeSensors(cfg);

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
 if (dbIdT.length( ) > 0) { safeCopy(cfg.sensors[i].hwId, dbIdT.c_str( ), sizeof(cfg.sensors[i].hwId)); }
 if (dbNameT.length( ) > 0) { safeCopy(cfg.sensors[i].friendlyName, dbNameT.c_str( ), sizeof(cfg.sensors[i].friendlyName)); }
 }
 _sensorMgr->applyCalibration(cfg.sensors[i].pins[0], dbIdT, dbOffsetT, dbNameT);
 if (gotT || gotU) {
 _sensorMgr->applyAmbientCalibration(dbOffsetT, dbOffsetU);
 }
 }
 }

 LOG_CODE(LOG_INFO, "APP", APP_SENSORS_CALIBRATED, 0, "");
}
