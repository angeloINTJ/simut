/**
 * @file    AppManager_Sensors.cpp
 * @brief   Sensor health: auto-heal, background scan, calibration.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "SystemDefs.h"

void AppManager::checkAndAutoHealSensors() {
    if (_sensorMgr.isScanning()) return;
    SystemConfig &cfg = _storageMgr.getConfig();

    for (uint8_t gpio = 0; gpio < 10; gpio++) {
        if (!cfg.sensors[gpio].active) continue;

        uint8_t foundRom[8];
        if (_sensorMgr.identifyPhysicalSensor(gpio, foundRom)) {
            if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) continue;

            if (memcmp(cfg.sensors[gpio].rom, foundRom, 8) != 0) {
                _sensorMgr.setHardwareMismatch(gpio, true);
            } else {
                _sensorMgr.setHardwareMismatch(gpio, false);
            }
        } else {
            /* Sensor configurado mas não encontrado no barramento físico */
            static uint32_t lastMissingLog[10] = {0};
            if (timeSince(lastMissingLog[gpio], 60000)) {
                lastMissingLog[gpio] = millis();
                LOG_CODE(LOG_WARN, "SENSOR", ERR_SENSOR_MISSING, gpio,
                    String(cfg.sensors[gpio].friendlyName));
            }
        }
    }
}

void AppManager::processBackgroundScan() {
    std::vector<ScanResult> results;
    if (_sensorMgr.getScanResults(results)) {
        _waitingScan = false;
        _cmdMgr.renderScanResults(results);
        loadAndCalibrateSensors();
        _cmdMgr.printPrompt();
    }
}
void AppManager::loadAndCalibrateSensors() {
    SystemConfig &cfg = _storageMgr.getConfig();
    _sensorMgr.initRuntimeSensors(cfg);

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].active && cfg.sensors[i].gpio != PIN_DHT_DEFAULT) {
            String dbId; float dbOffset = 0.0f; String dbName;
            if (_storageMgr.getCalibrationData(cfg.sensors[i].rom, dbId, dbOffset, dbName)) {
                _sensorMgr.applyCalibration(cfg.sensors[i].gpio, dbId, dbOffset, dbName);
                if (dbId.length() > 0) { safeCopy(cfg.sensors[i].hwId, dbId.c_str(), sizeof(cfg.sensors[i].hwId)); }
                if (dbName.length() > 0) { safeCopy(cfg.sensors[i].friendlyName, dbName.c_str(), sizeof(cfg.sensors[i].friendlyName)); }
            }
        }
    }
    LOG_CODE(LOG_INFO, "APP", APP_SENSORS_CALIBRATED, 0, "");
}
