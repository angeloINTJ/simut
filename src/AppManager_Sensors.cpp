/**
 * @file AppManager_Sensors.cpp
 * @brief Sensor health: auto-heal, background scan, calibration.
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
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

 for (uint8_t gpio = 0; gpio < 10; gpio++) {
 if (!cfg.sensors[gpio].active) continue;

 uint8_t foundRom[8];
 if (_sensorMgr->identifyPhysicalSensor(gpio, foundRom)) {
 if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) continue;

 if (memcmp(cfg.sensors[gpio].rom, foundRom, 8) != 0) {
 _sensorMgr->setHardwareMismatch(gpio, true);
 } else {
 _sensorMgr->setHardwareMismatch(gpio, false);
 }
 } else {
 /* Sensor configured but not found on the physical bus */
 static uint32_t lastMissingLog[10] = {0};
 if (timeSince(lastMissingLog[gpio], 60000)) {
 lastMissingLog[gpio] = millis( );
 LOG_CODE(LOG_WARN, "SENSOR", ERR_SENSOR_MISSING, gpio,
 String(cfg.sensors[gpio].friendlyName));
 }
 }
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
 if (cfg.sensors[i].active && cfg.sensors[i].gpio != PIN_DHT_DEFAULT) {
 String dbId; float dbOffset = 0.0f; String dbName;
 if (_storageMgr->getCalibrationData(cfg.sensors[i].rom, dbId, dbOffset, dbName)) {
 _sensorMgr->applyCalibration(cfg.sensors[i].gpio, dbId, dbOffset, dbName);
 if (dbId.length( ) > 0) { safeCopy(cfg.sensors[i].hwId, dbId.c_str( ), sizeof(cfg.sensors[i].hwId)); }
 if (dbName.length( ) > 0) { safeCopy(cfg.sensors[i].friendlyName, dbName.c_str( ), sizeof(cfg.sensors[i].friendlyName)); }
 }
 }
 }

 /* Ambient (DHT22) — calibration via picoUID. Line `t<id>` defines
 * ID, name, and temperature offset. Line `u<id>` defines only the
 * humidity offset (its ID/name are ignored — shared option B).
 * If no line matches, keeps defaults ("AMB"/"Ambiente Central"). */
 if (cfg.ambientSensor.active) {
 String dbIdT, dbNameT, dbIdU, dbNameU;
 float dbOffsetT = 0.0f, dbOffsetU = 0.0f;
 bool gotT = _storageMgr->getCalibrationDataAmbient('t', dbIdT, dbOffsetT, dbNameT);
 bool gotU = _storageMgr->getCalibrationDataAmbient('u', dbIdU, dbOffsetU, dbNameU);
 if (gotT) {
 if (dbIdT.length( ) > 0) { safeCopy(cfg.ambientSensor.hwId, dbIdT.c_str( ), sizeof(cfg.ambientSensor.hwId)); }
 if (dbNameT.length( ) > 0) { safeCopy(cfg.ambientSensor.friendlyName, dbNameT.c_str( ), sizeof(cfg.ambientSensor.friendlyName)); }
 }
 if (gotT || gotU) {
 _sensorMgr->applyAmbientCalibration(dbOffsetT, dbOffsetU);
 }
 }

 LOG_CODE(LOG_INFO, "APP", APP_SENSORS_CALIBRATED, 0, "");
}
