/**
 * @file    AppManager.h
 * @brief   Application orchestrator — top-level coordinator for all subsystems.
 * @details Owns all manager instances (Sensor, Storage, Command, Display,
 *          Network, Web, Telemetry, Sound) and coordinates their lifecycle.
 *          Handles boot sequence, main loop scheduling, UI event dispatch,
 *          alarm monitoring, sensor calibration, and NTP time correction.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include "SensorManager.h"
#include "StorageManager.h"
#include "CommandManager.h"
#include "SystemDefs.h"
#include "DisplayManager.h"
#include "NetworkManager.h"
#include "WebManager.h"
#include "TelemetryManager.h"
#include "SoundManager.h"

class AppManager {
public:
    AppManager();
    void setup();
    void loop();

    bool isDisplayAlive();
    void restartDisplayCore();
    void pauseDisplayForFlash(bool lock);
    void core0Yield();


    bool isUserInteracting() const;

private:
    SensorManager    _sensorMgr;
    StorageManager   _storageMgr;
    CommandManager   _cmdMgr;
    DisplayManager   _displayMgr;
    NetworkManager   _netMgr;
    WebManager       _webMgr;
    TelemetryManager _telemetryMgr;
    SoundManager     _soundMgr;

    uint32_t _lastHistoryTime = 0;
    uint32_t _lastSensorCheck = 0;
    uint32_t _bootCompletedAt = 0;


    volatile bool _pendingTimeSync = false;
    uint32_t _timeSyncBootTs = 0;
    int32_t _timeSyncDelta = 0;

    float _cachedMin[11];
    float _cachedMax[11];
    float _cachedHumMin;
    float _cachedHumMax;

    /* Min/max values from preload only (daily CSV snapshot) */
    float _preloadMin[11];
    float _preloadMax[11];
    float _preloadHumMin;
    float _preloadHumMax;

    void preloadMinMax();
    void openStatsScreen(int sensorId);

    void updateLiveDisplay();
    void refreshSelectedSlot();
    void renderGraphOptimized(int sensorId, int range);

    void processHistoryLogging();
    void processBackgroundScan();

    void executeCommand(CliDemand cmd);

    void checkAndAutoHealSensors();
    void loadAndCalibrateSensors();
    void handleTimeSync(uint32_t bootTs, int32_t delta);
    void checkAlarmConditions();


    bool _pendingAlarmDeactivate = false;

    static constexpr uint32_t TOUCH_PRIORITY_MS = 2000;
};
