/**
 * @file    AppManager.cpp
 * @brief   Implementation of AppManager — boot sequence, main loop, and event dispatch.
 * @details Contains the complete boot flow (filesystem, sensors, network, web server),
 *          the main loop with priority-based task scheduling, CLI command execution,
 *          graph rendering from CSV history, alarm condition checking, and
 *          provisional timestamp correction via NTP synchronization.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "SystemDefs.h"
#include "Themes.h"
#include <LittleFS.h>
#include <time.h>
#include <hardware/watchdog.h>
#include <string.h>

extern AppManager app;

static bool _appWaitingScan = false;
static int _currentSensorIndex = 0;
static bool _isApMode = false;


AppManager::AppManager() {
    for(int i = 0; i < 11; i++) {
        _cachedMin[i] = 1000.0f;
        _cachedMax[i] = -1000.0f;
        _preloadMin[i] = 1000.0f;
        _preloadMax[i] = -1000.0f;
    }
    _cachedHumMin = 1000.0f;
    _cachedHumMax = -1000.0f;
    _preloadHumMin = 1000.0f;
    _preloadHumMax = -1000.0f;
}

/* =========================================================================== */
/*                               BOOT SEQUENCE                               */
/* =========================================================================== */
/**
 * @brief Complete system initialization in deterministic order.
 *
 * Boot flow:
 *   1. Display + Core 1 launch
 *   2. AP mode hold detection (3.5s touch)
 *   3. Filesystem mount + log manager
 *   4. CLI + Bluetooth authentication
 *   5. Theme, language, sound, touch calibration
 *   6. Sensor initialization + calibration
 *   7. WiFi connection (or AP mode)
 *   8. Telemetry + web server
 *   9. Sensor warm-up + NTP correction
 *  10. Dashboard launch
 */
void AppManager::setup() {
    Serial.begin(115200);
    delay(1000);

    TRACE_MOD(0, MOD_BOOT);
    TRACE_BEAT(0);

    _displayMgr.begin();
    _displayMgr.startCore1();
    LOG_INF("APP", "Display UI Launched on Core 1.");

    delay(800);

    bool forceAP = false;
    _displayMgr.setBootStatus("Hold screen for AP Mode...");
    unsigned long waitStart = millis();

    while (millis() - waitStart < 3500) {
        TRACE_BEAT(0);

        if (_displayMgr.isScreenTouched()) {
            unsigned long holdStart = millis();
            bool held = true;
            int missedTouches = 0;

            while (millis() - holdStart < 3000) {
                TRACE_BEAT(0);
                if (!_displayMgr.isScreenTouched()) {
                    missedTouches++;
                    if (missedTouches > 5) {
                        held = false;
                        _displayMgr.setApProgress(-1);
                        _displayMgr.setBootStatus("AP Mode Cancelled.", false);
                        delay(800);
                        break;
                    }
                } else {
                    missedTouches = 0;
                }
                int pct = map(millis() - holdStart, 0, 3000, 0, 100);
                _displayMgr.setApProgress(pct);
                delay(50);
            }
            if (held) forceAP = true;
            break;
        }
        delay(50);
    }

    _displayMgr.setApProgress(-1);

    _storageMgr.setLockCallback([](bool lock) {
        app.pauseDisplayForFlash(lock);
    });

    LogManager::instance().setLockCallback([](bool lock) {
        app.pauseDisplayForFlash(lock);
    });

    _displayMgr.setBootStatus("Mounting File System...");
    bool fsOk = _storageMgr.begin();

    _displayMgr.setBootStatus("Starting Log Manager...");
    LogManager::instance().begin(fsOk, LOG_DEBUG);


    LogManager::instance().setHeavyTaskChecker([]() -> bool {
        return app._storageMgr.isHeavyTaskLocked();
    });


    LogManager::instance().setTouchPriorityChecker([]() -> bool {
        return app.isUserInteracting();
    });

    _displayMgr.setBootStatus("Starting Command Interface...");
    _cmdMgr.begin();


    _cmdMgr.setBtValidator([this](String attempt) -> bool {
        SystemConfig &cfg = _storageMgr.getConfig();
        if (!cfg.users[0].active) return false;
        String hashed = _storageMgr.hashPassword(
            String(cfg.users[0].username), attempt);
        return (hashed == String(cfg.users[0].password));
    });

    if (!fsOk) LOG_ERR("APP", "Storage Critical Failure! Check Flash FS.");

    uint32_t lastTs = _storageMgr.getLastRecordedTimestamp();
    _netMgr.setProvisionalTime(lastTs);
    _netMgr.setTimeSyncCallback([](uint32_t bootTs, int32_t delta) {


        app._timeSyncBootTs = bootTs;
        app._timeSyncDelta = delta;
        app._pendingTimeSync = true;
    });

    SystemConfig &cfg = _storageMgr.getConfig();
    _displayMgr.setBootStatus("Loading Theme & Language...");
    loadTheme(cfg.themeIndex);
    _displayMgr.refreshTheme();
    _displayMgr.setLanguage(cfg.displayLang);


    _soundMgr.begin();
    {
        const SoundConfigData* sndCfg = reinterpret_cast<const SoundConfigData*>(
            cfg.reserved + sizeof(TouchCalData));
        _soundMgr.loadConfig(sndCfg);
    }


    {
        const TouchCalData* cal = reinterpret_cast<const TouchCalData*>(cfg.reserved);
        _displayMgr.loadTouchCalibration(cal);
        if (!_displayMgr.isTouchCalibrated()) {
            LOG_WRN("APP", "No touch calibration found — launching calibration screen.");
            _displayMgr.setBootStatus("Touch calibration required...");
            delay(600);
            _displayMgr.showTouchCalibration();


            while (!_displayMgr.isTouchCalibrated()) {
                TRACE_BEAT(0);

                UiEvent calEv;
                if (_displayMgr.getUiEvent(calEv)) {
                    if (calEv.type == UiEvent::EVT_APPLY_TOUCH_CAL) {
                        TouchCalData* calOut = reinterpret_cast<TouchCalData*>(cfg.reserved);
                        _displayMgr.fillCalData(calOut);
                        _storageMgr.saveConfiguration();
                        LOG_INF("APP", "Initial touch calibration saved.");
                    }
                }
                delay(50);
            }
        }
    }

    _displayMgr.setBootStatus("Loading Peripherals & Sensors...");
    _sensorMgr.begin();
    loadAndCalibrateSensors();
    _sensorMgr.setDs18Resolution((DS18B20PIO::Resolution)cfg.ds18Resolution);

    if (forceAP) {
        LOG_WRN("APP", "User triggered CONFIG MODE (AP)");
        _displayMgr.setBootStatus("Starting Access Point (AP)...");
        _displayMgr.setBootStatus("Connect to network SIMUT_SETUP");
        _displayMgr.setBootStatus("Access on mobile: 192.168.4.1");
        _netMgr.beginAP(cfg.deviceName);
        for (int i = 0; i < 35; i++) { delay(100); watchdog_update(); TRACE_BEAT(0); }
    } else {
        _displayMgr.setBootStatus("Starting Wi-Fi Interface...");
        _netMgr.begin(cfg);

        unsigned long netWait = millis();
        unsigned long lastMsg = 0;
        bool skipped = false;

        int dotCount = 0;
        int waitState = 0;

        while (!_netMgr.isConnected() || !_netMgr.isTimeSynced()) {
            TRACE_BEAT(0);
            _netMgr.update();

            if (_displayMgr.isSkipPressed()) {
                _displayMgr.setBootStatus("Connection Skipped by User.");
                skipped = true;
                delay(1000);
                break;
            }

            if (millis() - lastMsg > 800) {
                dotCount++;
                if (dotCount > 4) dotCount = 0;
                String dots = "";
                for (int i = 0; i < dotCount; i++) dots += ".";

                if (!_netMgr.isConnected()) {
                    if (waitState != 1) {
                        waitState = 1; dotCount = 0;
                        _displayMgr.setBootStatus("Waiting for router", true);
                    } else {
                        _displayMgr.replaceBootStatus("Waiting for router" + dots, true);
                    }
                } else if (!_netMgr.isTimeSynced()) {
                    if (waitState != 2) {
                        waitState = 2; dotCount = 0;
                        _displayMgr.setBootStatus("Syncing Global Clock", true);
                    } else {
                        _displayMgr.replaceBootStatus("Syncing Global Clock" + dots, true);
                    }
                }
                lastMsg = millis();
            }

            if (millis() - netWait > 30000) {
                 _displayMgr.setBootStatus("Network timeout. Starting Offline...");
                 delay(1000);
                 break;
            }
            delay(50);
        }

        if (!skipped && _netMgr.isConnected()) {
            _displayMgr.setBootStatus("Network Connected & Synced!");
            delay(500);
        }
    }

    _displayMgr.setBootStatus("Starting Telemetry Server...");
    _telemetryMgr.begin(&_storageMgr, &_netMgr);

    LogManager::instance().setEpochSource([]() -> time_t { return time(nullptr); });

    _displayMgr.setBootStatus("Starting Web Server...");
    _webMgr.begin(&_storageMgr, &_sensorMgr, &_netMgr, &_displayMgr, &_telemetryMgr, &_soundMgr);

    _displayMgr.setBootStatus("Registering Callbacks...");
    _webMgr.setYieldCallback([this]() { this->core0Yield(); });
    _webMgr.setLightYieldCallback([this]() {
        watchdog_update();
        TRACE_BEAT(0);


        static uint32_t lastLiveUpdate = 0;
        uint32_t now = millis();
        if (now - lastLiveUpdate > 3000) {
            lastLiveUpdate = now;
            _sensorMgr.update();
            updateLiveDisplay();
        }
    });


    _webMgr.setTouchPriorityChecker([]() -> bool {
        return app.isUserInteracting();
    });

    if (forceAP) {
        _isApMode = true;
        _displayMgr.setBootStatus("AP Active! Reboot board to exit.", false);
        LOG_INF("APP", "System Ready in Config Mode.");
    } else {


        _displayMgr.setBootStatus("Loading daily Min/Max cache...");
        delay(80);
        preloadMinMax();


        _displayMgr.setBootStatus("Warming up sensors...");
        {
            unsigned long warmStart = millis();


            while (millis() - warmStart < 2000) {
                watchdog_update();
                TRACE_BEAT(0);
                _sensorMgr.update();


                if (millis() - warmStart >= 900) break;

                delay(10);
            }


            updateLiveDisplay();
            refreshSelectedSlot();
        }


        if (_pendingTimeSync) {
            _displayMgr.setBootStatus("Correcting timestamps (NTP)...");
            delay(80);
            handleTimeSync(_timeSyncBootTs, _timeSyncDelta);


            _displayMgr.setBootStatus("Reloading Min/Max cache...");
            delay(80);
            for (int i = 0; i < 11; i++) {
                _cachedMin[i] = 1000.0f; _cachedMax[i] = -1000.0f;
                _preloadMin[i] = 1000.0f; _preloadMax[i] = -1000.0f;
            }
            _cachedHumMin = 1000.0f; _cachedHumMax = -1000.0f;
            _preloadHumMin = 1000.0f; _preloadHumMax = -1000.0f;
            preloadMinMax();
        }


        _displayMgr.setBootStatus("Preparing dashboard data...");
        _sensorMgr.update();
        updateLiveDisplay();
        refreshSelectedSlot();

        _displayMgr.setBootStatus("All subsystems initialized.");
        _displayMgr.setBootStatus("System Ready! Entering Dashboard.");
        delay(800);
        LOG_INF("APP", "System Ready.");
        _displayMgr.endBoot();
        _bootCompletedAt = millis();


        _soundMgr.play(SND_CONFIRM);
    }

    TRACE_MOD(0, MOD_IDLE);
    _cmdMgr.printPrompt();
}

/* =========================================================================== */
/*                MAIN LOOP — PRIORITY-BASED TASK SCHEDULING                 */
/* =========================================================================== */
/**
 * @brief Main application loop with cross-core health monitoring.
 *
 * Execution order (every cycle):
 *   1. Cross-core health check + pause watchdog
 *   2. CLI input processing
 *   3. Network keepalive (always runs)
 *   4. Web server request handling
 *   5. Telemetry upload (deferred during menu/touch/heavy tasks)
 *   6. Sensor auto-heal check (every 3s)
 *   7. NTP timestamp correction (if pending)
 *   8. History CSV logging (every 60s)
 *   9. UI event dispatch + sound processing
 */
void AppManager::loop() {
    TRACE_BEAT(0);
    LogManager::instance().checkCrossCoreHealth();


    {
        uint32_t pauseTs = _displayMgr.getPauseStartTime();
        if (pauseTs > 0 && (millis() - pauseTs > 5000)) {
            LOG_ERR("APP", "SAFETY: Display pause stuck >5s! Forcing unlock.");
            _displayMgr.forceUnpause();
        }
    }


    {
        static uint32_t _lastCore1RestartCheck = 0;
        if (millis() - _lastCore1RestartCheck > 5000) {
            _lastCore1RestartCheck = millis();
            if (_displayMgr.isCore1Ready() && _displayMgr.getPauseStartTime() == 0) {
                uint32_t beat = _displayMgr.getHeartbeat();
                if (beat > 0 && (millis() - beat > 10000)) {
                    LOG_ERR("APP", "CRITICAL: Core 1 heartbeat dead >10s. Restarting display core.");
                    _displayMgr.restartCore1();
                }
            }
        }
    }

    CliDemand cmd;
    TRACE_MOD(0, MOD_CLI);
    if (_cmdMgr.processInput(cmd)) {
        if (cmd.type != CMD_UNKNOWN) executeCommand(cmd);
        if (!_appWaitingScan) _cmdMgr.printPrompt();
    }


    TRACE_MOD(0, MOD_WIFI);
    _netMgr.update();

    bool heavyRendering = _displayMgr.isHeavyRendering();


    TRACE_MOD(0, MOD_WEB_SERVER);
    _webMgr.update();

    bool menuActive = _displayMgr.isMenuActive();

    TRACE_MOD(0, MOD_STORAGE_WRITE);
    _storageMgr.update();

    if (_isApMode) {
        TRACE_MOD(0, MOD_IDLE);
        return;
    }

    if (!menuActive) {
        TRACE_MOD(0, MOD_TELEMETRY);
        if (!heavyRendering && !isUserInteracting()) {
            _telemetryMgr.update();

            /* Notify display about the last telemetry send result */
            bool telSuccess;
            if (_telemetryMgr.consumeLastSendResult(telSuccess)) {
                _displayMgr.setTelemetrySendStatus(telSuccess);
            }
        }
    }

    TRACE_MOD(0, MOD_SENSOR_READ);
    if (millis() - _lastSensorCheck > 3000) {
        if (!isUserInteracting()) {
            _lastSensorCheck = millis();
            checkAndAutoHealSensors();
        }
    }


    if (_pendingTimeSync && !isUserInteracting()) {
        handleTimeSync(_timeSyncBootTs, _timeSyncDelta);
    }

    TRACE_MOD(0, MOD_STORAGE_WRITE);


    if (millis() - _lastHistoryTime >= 60000) {
        if (!_storageMgr.isHeavyTaskLocked() && !isUserInteracting()) {
            processHistoryLogging();
        }
    }

    if (_appWaitingScan && !isUserInteracting()) {
        processBackgroundScan();
    }

    core0Yield();

    TRACE_MOD(0, MOD_IDLE);
}

/* =========================================================================== */
/*                           CLI COMMAND EXECUTION                           */
/* =========================================================================== */
/** @brief Execute a parsed CLI command and apply changes to configuration. */
void AppManager::executeCommand(CliDemand cmd) {
    SystemConfig &cfg = _storageMgr.getConfig();
    bool changed = false;

    switch (cmd.type) {
        case CMD_HELP:
            _cmdMgr.printHelp(); break;

        case CMD_SHOW_THEMES:
            _cmdMgr.consolePrintln("\n--- Available Themes ---");
            for(int i=0; i<getThemeCount(); i++) {
                _cmdMgr.consolePrintf(" %2d | %-15s | %s\n", i, getThemeId(i).c_str(), availableThemes[i].displayName);
            }
            _cmdMgr.consolePrintln("------------------------");
            break;

        case CMD_SET_THEME: {
            int idx = getThemeIndexByName(cmd.strVal1);
            if (idx == -1) idx = cmd.strVal1.toInt();
            if (idx >= 0 && idx < getThemeCount()) {
                cfg.themeIndex = idx;
                loadTheme(idx);
                _displayMgr.refreshTheme();
                changed = true;
                LOG_INF("CFG", "Theme applied: " + String(availableThemes[idx].displayName));
            } else { LOG_WRN("CFG", "Theme not found."); }
            break;
        }

        case CMD_SHOW_LOGS: {
            _cmdMgr.consolePrintln("\n--- SYSTEM LOG START ---");
            int logCount = 0;
            auto streamLogFile = [&](const char* path) {

                _storageMgr.enterFlashReadLock();
                bool exists = LittleFS.exists(path);
                File f;
                if (exists) f = LittleFS.open(path, "r");
                _storageMgr.exitFlashReadLock();
                if (exists && f) {

                    char lineBuf[256];
                    while (f.available() && logCount < 2000) {
                        watchdog_update();
                        TRACE_BEAT(0);
                        size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
                        if (len == 0) continue;
                        lineBuf[len] = '\0';
                        _cmdMgr.printLogEntry(String(lineBuf));
                        logCount++;
                    }
                    f.close();
                }
            };
            streamLogFile("/system.old");
            streamLogFile("/system.log");
            _cmdMgr.consolePrintln("--- SYSTEM LOG END ---\n");
            break;
        }

        case CMD_SHOW_SENSORS: _cmdMgr.renderSensorTable(cfg.sensors, MAX_SENSORS); break;
        case CMD_SHOW_STORAGE: LOG_INF("STO", _storageMgr.getStatsReport()); break;
        case CMD_SHOW_SYSINFO: _cmdMgr.renderSystemInfo(cfg); break;
        case CMD_SHOW_NET: LOG_INF("NET", "IP: " + _netMgr.getIpAddress()); break;

        case CMD_SET_DS_RES:
            if (cmd.intVal1 >= 9 && cmd.intVal1 <= 12 && _sensorMgr.setDs18Resolution((DS18B20PIO::Resolution)cmd.intVal1)) {
                cfg.ds18Resolution = cmd.intVal1; changed = true;
            }
            break;

        case CMD_SET_SYS_NAME: strncpy(cfg.deviceName, cmd.strVal1.c_str(), 31); changed = true; break;
        case CMD_SET_WIFI_SSID: strncpy(cfg.wifiSsid, cmd.strVal1.c_str(), 31); changed = true; break;
        case CMD_SET_WIFI_PASS: strncpy(cfg.wifiPass, cmd.strVal1.c_str(), 31); changed = true; break;
        case CMD_SET_TIMEZONE: cfg.timezoneOffset = (int8_t)cmd.intVal1; changed = true; break;

        case CMD_SET_TEL_SERVER: strncpy(cfg.telServer, cmd.strVal1.c_str(), 63); changed = true; break;
        case CMD_SET_TEL_PORT: cfg.telPort = cmd.intVal1; changed = true; break;
        case CMD_SET_TEL_PATH: strncpy(cfg.telPath, cmd.strVal1.c_str(), 31); changed = true; break;
        case CMD_SET_TEL_BATCH: cfg.telBatchSize = cmd.intVal1; changed = true; break;
        case CMD_SET_TEL_INTERVAL: cfg.telInterval = cmd.intVal1; changed = true; break;
        case CMD_SET_TEL_CRYPTO: cfg.telEncryption = cmd.boolVal; changed = true; break;
        case CMD_SET_TEL_MODE: cfg.telMode = cmd.intVal1; changed = true; break;

        case CMD_RESET_ADMIN: {
            String hashed = _storageMgr.hashPassword("admin", "8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918");
            strncpy(cfg.users[0].password, hashed.c_str(), 31);
            cfg.users[0].password[31] = '\0';
            cfg.users[0].mustChangePassword = true;
            _cmdMgr.printInfo("Admin Password reset. Use 'simut' via web.");
            changed = true;
            break;
        }

        case CMD_DEFINE_SENSOR:
            if (cmd.intVal1 < MAX_SENSORS) {
                SensorRecord &r = cfg.sensors[cmd.intVal1];
                r.active = true;
                r.gpio = cmd.intVal1;
                memcpy(r.rom, cmd.rom, 8);
                strncpy(r.hwId, cmd.strVal1.c_str(), 15);
                strncpy(r.friendlyName, cmd.strVal2.c_str(), 31);
                _cmdMgr.printSuccess("Sensor mapped in RAM.");
            }
            break;

        case CMD_WIPE_SENSOR:
            if (cmd.intVal1 >= 0 && cmd.intVal1 < MAX_SENSORS) {
                cfg.sensors[cmd.intVal1].provisionEpoch = _netMgr.getEpoch();
                changed = true;
                _cmdMgr.printSuccess("Sensor history context wiped for Slot " + String(cmd.intVal1));
            }
            break;

        case CMD_ACCEPT_SENSOR: {
            uint8_t gpio = cmd.intVal1;
            if (gpio < MAX_SENSORS) {
                uint8_t foundRom[8];
                if (_sensorMgr.identifyPhysicalSensor(gpio, foundRom)) {
                    if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) {
                        _cmdMgr.printError("Invalid physical sensor on GPIO " + String(gpio));
                    } else {
                        String dbId; float dbOffset = 0.0f; String dbName;
                        _storageMgr.getCalibrationData(foundRom, dbId, dbOffset, dbName);

                        String currentId = String(cfg.sensors[gpio].hwId);

                        cfg.sensors[gpio].active = true;
                        cfg.sensors[gpio].gpio = gpio;
                        memcpy(cfg.sensors[gpio].rom, foundRom, 8);

                        if (dbId.length() > 0) { strncpy(cfg.sensors[gpio].hwId, dbId.c_str(), 15); cfg.sensors[gpio].hwId[15] = '\0'; }
                        else { strncpy(cfg.sensors[gpio].hwId, "LIB_SENS", 15); }

                        if (dbName.length() > 0) { strncpy(cfg.sensors[gpio].friendlyName, dbName.c_str(), 31); }
                        else { strncpy(cfg.sensors[gpio].friendlyName, "Recognized Sensor", 31); }
                        cfg.sensors[gpio].friendlyName[31] = '\0';

                        if (currentId != String(cfg.sensors[gpio].hwId)) {
                            cfg.sensors[gpio].provisionEpoch = _netMgr.getEpoch();
                            _cmdMgr.printInfo("New Hardware Context Detected. Epoch updated.");
                        }

                        _storageMgr.saveConfiguration();
                        loadAndCalibrateSensors();
                        _cmdMgr.printSuccess("Sensor accepted and bound to Slot " + String(gpio));
                    }
                } else {
                    _cmdMgr.printError("No physical sensor detected on GPIO " + String(gpio));
                }
            }
            break;
        }

        case CMD_SCAN_SENSORS:
            if (!_sensorMgr.isScanning()) { _sensorMgr.startScan(); _appWaitingScan = true; }
            break;

        case CMD_WRITE_MEMORY:
            if (_storageMgr.saveConfiguration()) {
                loadAndCalibrateSensors();
                _cmdMgr.printSuccess("Config saved to Flash!");
            }
            break;

        case CMD_CLEAR_LOGS:


            _storageMgr.enterFlashSafeMode();
            LittleFS.remove("/system.log"); LittleFS.remove("/system.old");
            _storageMgr.exitFlashSafeMode();
            LogManager::instance().begin(true, LOG_DEBUG);
            _cmdMgr.printSuccess("Logs cleared.");
            break;

        case CMD_RELOAD: rp2040.reboot(); break;
        case CMD_TEL_SYNC: _telemetryMgr.forceSync(); _cmdMgr.printSuccess("Telemetry sync triggered."); break;

        case CMD_UNKNOWN:
        default: LOG_WRN("CLI", "Unknown command."); break;
    }

    if (changed) _cmdMgr.printInfo("Settings updated in RAM. Use 'write memory' to persist.");
}

/* =========================================================================== */
/*             CORE 0 YIELD — UI EVENTS + SOUND + SENSOR UPDATE              */
/* =========================================================================== */
/**
 * @brief Process pending UI events, sound signals, and sensor readings.
 * Called from the main loop and from web server light-yield callbacks.
 * Protected against re-entrancy with a static guard flag.
 */
void AppManager::core0Yield() {
    static bool _isRenderingGraph = false;
    static bool _inYield = false;

    if (_inYield) return;
    _inYield = true;

    UiEvent uiEv;
    if (!_isRenderingGraph) {
        while (_displayMgr.getUiEvent(uiEv)) {
            if (uiEv.type == UiEvent::EVT_SLOT_SELECT) { _currentSensorIndex = uiEv.id; refreshSelectedSlot(); }
            else if (uiEv.type == UiEvent::EVT_OPEN_GRAPH) {
                if (uiEv.param == 99) openStatsScreen(uiEv.id);
                else {

                    _displayMgr.requestLoadingScreen();


                    uint32_t waitStart = millis();
                    while (!_displayMgr.isLoadingDrawn() && (millis() - waitStart < 500)) {
                        watchdog_update();
                        TRACE_BEAT(0);
                        delay(5);
                    }

                    _isRenderingGraph = true;
                    renderGraphOptimized(uiEv.id, uiEv.param);
                    _isRenderingGraph = false;
                }
            }
            else if (uiEv.type == UiEvent::EVT_OPEN_SETTINGS) {
                SystemConfig &cfg = _storageMgr.getConfig();
                String authPin = String(cfg.displayPin);
                if (authPin.length() == 0) authPin = "1234";
                _displayMgr.showAuthScreen(authPin);
            }
            else if (uiEv.type == UiEvent::EVT_AUTH_SUCCESS) {
                _soundMgr.play(SND_CONFIRM);


                if (_pendingAlarmDeactivate) {
                    _pendingAlarmDeactivate = false;

                    SystemConfig &cfg = _storageMgr.getConfig();
                    cfg.ambientSensor.alarmsActive = false;
                    for (int i = 0; i < MAX_SENSORS; i++) {
                        cfg.sensors[i].alarmsActive = false;
                    }

                    _soundMgr.stopAlarm();
                    _displayMgr.setAlarmState(0, -1, false, false);
                    _displayMgr.setAlarmSilenced(false, 0);
                    _displayMgr.setAlarmDeactivated(true);
                    _displayMgr.forceDashboard();
                    LOG_WRN("APP", "All alarms DEACTIVATED via UI (RAM only, reboot restores).");
                } else {
                    _displayMgr.showSettingsMain();
                }
            }
            else if (uiEv.type == UiEvent::EVT_MENU_SELECT) {
                if (uiEv.id == 0) {
                    _displayMgr.showSettingsThemes(_storageMgr.getConfig().themeIndex);
                }
                else if (uiEv.id == 1) {
                    _displayMgr.showSettingsAlarms(&_storageMgr.getConfig());
                }
                else if (uiEv.id == 2) {

                    _displayMgr.showSettingsSounds(_soundMgr.getSettingsState());
                }
                else if (uiEv.id == 3) {
                    _displayMgr.showSettingsLang(_storageMgr.getConfig().displayLang);
                }
                else if (uiEv.id == 4) {
                    _displayMgr.showSettingsPassword();
                }
                else if (uiEv.id == 5) {
                    _displayMgr.showTouchCalibration();
                }
                else if (uiEv.id == 6) {
                    _displayMgr.showSettingsLicense();
                }
            }
            else if (uiEv.type == UiEvent::EVT_APPLY_THEME) {
                SystemConfig &cfg = _storageMgr.getConfig();
                cfg.themeIndex = uiEv.id;
                loadTheme(cfg.themeIndex);
                _storageMgr.saveConfiguration();
                _displayMgr.refreshTheme();
                _soundMgr.play(SND_CONFIRM);
                LOG_INF("APP", "Theme changed via Dashboard UI.");
            }
            else if (uiEv.type == UiEvent::EVT_APPLY_LANG) {
                SystemConfig &cfg = _storageMgr.getConfig();
                cfg.displayLang = uiEv.id;
                _displayMgr.setLanguage(cfg.displayLang);
                _storageMgr.saveConfiguration();
                _soundMgr.play(SND_CONFIRM);
                _displayMgr.forceDashboard();
                LOG_INF("APP", "System language changed via UI.");
            }
            else if (uiEv.type == UiEvent::EVT_SAVE_ALARMS) {
                _storageMgr.saveConfiguration();

                _sensorMgr.syncAlarmLimits(_storageMgr.getConfig());

                checkAlarmConditions();
                _soundMgr.play(SND_CONFIRM);
                _displayMgr.showSettingsAlarms(&_storageMgr.getConfig());
                LOG_INF("APP", "Alarm limits saved via UI.");
            }

            else if (uiEv.type == UiEvent::EVT_APPLY_TOUCH_CAL) {
                SystemConfig &cfg = _storageMgr.getConfig();
                TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
                _displayMgr.fillCalData(cal);
                _storageMgr.saveConfiguration();
                _soundMgr.play(SND_CONFIRM);
                LOG_INF("APP", "Touch calibration saved to flash.");
            }

            else if (uiEv.type == UiEvent::EVT_SAVE_PASSWORD) {
                SystemConfig &cfg = _storageMgr.getConfig();
                char newPwd[9];
                _displayMgr.getNewPassword(newPwd, sizeof(newPwd));
                if (strlen(newPwd) >= 4 && strlen(newPwd) <= 7) {
                    strncpy(cfg.displayPin, newPwd, 7);
                    cfg.displayPin[7] = '\0';
                    _storageMgr.saveConfiguration();
                    _soundMgr.play(SND_CONFIRM);
                    LOG_INF("APP", "Display PIN changed via UI.");
                } else {
                    _soundMgr.play(SND_ERROR);
                }
            }

            else if (uiEv.type == UiEvent::EVT_SAVE_SOUNDS) {
                SoundSettingsState sndState = _displayMgr.getSoundSettings();
                _soundMgr.applySettingsState(sndState);

                SystemConfig &cfg = _storageMgr.getConfig();
                SoundConfigData* sndCfg = reinterpret_cast<SoundConfigData*>(
                    cfg.reserved + sizeof(TouchCalData));
                _soundMgr.fillConfig(sndCfg);
                _storageMgr.saveConfiguration();

                _soundMgr.play(SND_CONFIRM);
                _displayMgr.showSettingsMain();
                LOG_INF("APP", "Sound settings saved via UI.");
            }


            else if (uiEv.type == UiEvent::EVT_ALARM_SILENCE) {
                uint32_t silenceSec = (uiEv.param > 0) ? uiEv.param : 120;
                _soundMgr.stopAlarm();
                _displayMgr.setAlarmSilenced(true, millis() + (silenceSec * 1000));
                _displayMgr.forceDashboard();
                LOG_WRN("APP", "Alarm silenced for 120s via UI.");
            }


            else if (uiEv.type == UiEvent::EVT_ALARM_DEACTIVATE) {

                _pendingAlarmDeactivate = true;
                SystemConfig &cfg = _storageMgr.getConfig();
                _displayMgr.showAuthScreen(String(cfg.displayPin));
            }
        }
    }


    {
        uint8_t volPreview;
        if (_displayMgr.consumeVolumePreview(volPreview)) {
            _soundMgr.setVolume(volPreview);
            _soundMgr.play(SND_TOUCH_CLICK);
            _displayMgr.consumeTouchSound();
        }
    }


    {
        uint8_t alarmVolPreview;
        if (_displayMgr.consumeAlarmVolumePreview(alarmVolPreview)) {
            _soundMgr.setAlarmVolume(alarmVolPreview);

            SoundSettingsState sndState = _displayMgr.getSoundSettings();
            _soundMgr.playPreview(SND_ALARM_START, sndState.alarmMelody);
            _displayMgr.consumeTouchSound();
        }
    }


    if (_displayMgr.consumeTouchSound()) {
        _soundMgr.play(SND_TOUCH_CLICK);
    }


    if (_displayMgr.consumeErrorSound()) {
        _soundMgr.play(SND_ERROR);
    }


    {
        SoundEvent prevEvt;
        uint8_t prevIdx;
        if (_displayMgr.consumePreviewSound(prevEvt, prevIdx)) {
            _soundMgr.playPreview(prevEvt, prevIdx);
        }
    }

    _soundMgr.update();

    _sensorMgr.update();
    updateLiveDisplay();


    if (_bootCompletedAt > 0 && (millis() - _bootCompletedAt > 5000)) {


        if (_displayMgr.isAlarmSilenced()) {
            uint32_t silEnd = _displayMgr.getAlarmSilenceEnd();
            if (silEnd > 0 && millis() >= silEnd) {
                _displayMgr.setAlarmSilenced(false, 0);
                LOG_INF("APP", "Alarm silence expired — re-evaluating conditions.");
            }
        }

        checkAlarmConditions();
    }


    _soundMgr.update();

    _inYield = false;
}

void AppManager::pauseDisplayForFlash(bool lock) { _displayMgr.pauseRendering(lock); }

void AppManager::refreshSelectedSlot() {
    SystemConfig &cfg = _storageMgr.getConfig();
    const auto& sensors = _sensorMgr.getRuntimeSensors();
    bool found = false;

    if (_currentSensorIndex < 10) {
        if (cfg.sensors[_currentSensorIndex].active) {
            uint8_t targetGpio = cfg.sensors[_currentSensorIndex].gpio;
            for (const auto &s : sensors) {
                if (s.config.gpio != 10 && s.config.gpio == targetGpio) {
                    _displayMgr.setSlotData(s.avgValue1, !s.inErrorState, _currentSensorIndex, String(s.config.friendlyName));
                    found = true; break;
                }
            }
        }
    } else if (_currentSensorIndex == 10) {
        _displayMgr.setSlotData(analogReadTemp(), true, 10, "Board (Internal)"); found = true;
    }

    if (!found) _displayMgr.setSlotData(NAN, false, _currentSensorIndex, "Empty / Inactive");
}

/**
 * @brief Push current sensor data and system status to the display shared state.
 * System status (time, RSSI, pending count) updates every cycle.
 * Sensor data updates only when new readings are available.
 */
void AppManager::updateLiveDisplay() {


    {
        String dateStr = _netMgr.getFormattedDate();
        dateStr.replace("/20", "/");
        String fullStatus = dateStr + " - " + _netMgr.getFormattedTime();
        _displayMgr.setSystemStatus(_netMgr.getRssi(), false, fullStatus);


        static uint32_t lastPendingRefresh = 0;
        if (millis() - lastPendingRefresh > 10000) {
            _telemetryMgr.refreshPendingCount();
            lastPendingRefresh = millis();
        }
        _displayMgr.setTelemetryPending(_telemetryMgr.getPendingEstimate());

        /* Daily min/max (preload CSV + accumulated real-time readings) */
        float ambMinT = (_cachedMin[10] < 999.0f)  ? _cachedMin[10] : NAN;
        float ambMaxT = (_cachedMax[10] > -999.0f)  ? _cachedMax[10] : NAN;
        float ambMinH = (_cachedHumMin  < 999.0f)   ? _cachedHumMin  : NAN;
        float ambMaxH = (_cachedHumMax  > -999.0f)   ? _cachedHumMax  : NAN;
        _displayMgr.setAmbientMinMax(ambMinT, ambMaxT, ambMinH, ambMaxH);

        /* Min/max do slot ativo */
        int slotIdx = _currentSensorIndex;
        if (slotIdx >= 0 && slotIdx < 10) {
            float sMinT = (_cachedMin[slotIdx] < 999.0f)  ? _cachedMin[slotIdx] : NAN;
            float sMaxT = (_cachedMax[slotIdx] > -999.0f)  ? _cachedMax[slotIdx] : NAN;
            _displayMgr.setSlotMinMax(sMinT, sMaxT);
        }
    }


    if (_sensorMgr.hasNewReadings()) {
        const auto& sensors = _sensorMgr.getRuntimeSensors();
        SystemConfig &cfg = _storageMgr.getConfig();

        for (const auto &s : sensors) {
            if (s.config.gpio == 10) _displayMgr.setAmbientData(s.avgValue1, s.avgValue2, !s.inErrorState);
            else if (_currentSensorIndex < 10 && cfg.sensors[_currentSensorIndex].active && cfg.sensors[_currentSensorIndex].gpio == s.config.gpio) {
                _displayMgr.setSlotData(s.avgValue1, !s.inErrorState, _currentSensorIndex, String(s.config.friendlyName));
            }
        }

        if (_currentSensorIndex == 10) _displayMgr.setSlotData(analogReadTemp(), true, 10, "Board (Internal)");
    }
}

/**
 * @brief Pre-load daily Min/Max values from history CSV for fast display.
 * Runs during boot to avoid flash I/O competition with the dashboard.
 * Uses ReadLock (no Core 1 pause) with 5-second budget limit.
 */
void AppManager::preloadMinMax() {
    time_t now = _netMgr.getEpoch();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char path[40];
    snprintf(path, sizeof(path), "/history/%04d%02d%02d.csv", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);


    File f;
    _storageMgr.enterFlashReadLock();
    bool fileExists = LittleFS.exists(path);
    if (fileExists) f = LittleFS.open(path, "r");
    _storageMgr.exitFlashReadLock();

    if (fileExists && f) {
        char lineBuffer[256];
        uint32_t _preloadBudget = millis();
        bool hasMore = true;

        while (hasMore) {
            if (millis() - _preloadBudget > 5000) {
                LOG_WRN("APP", "preloadMinMax aborted — 5s budget exceeded.");
                _storageMgr.enterFlashReadLock();
                f.close();
                _storageMgr.exitFlashReadLock();
                LOG_INF("APP", "Min/Max cache loaded (partial).");
                return;
            }


            _storageMgr.enterFlashReadLock();
            int linesInBatch = 0;
            while (f.available() && linesInBatch < 20) {
                size_t len = f.readBytesUntil('\n', lineBuffer, sizeof(lineBuffer) - 1);
                if (len == 0) continue;
                lineBuffer[len] = '\0';
                if (len > 0 && lineBuffer[len - 1] == '\r') lineBuffer[len - 1] = '\0';
                linesInBatch++;

                const char* ptr = lineBuffer; char* endPtr;
                strtoul(ptr, &endPtr, 10);
                if (*endPtr == ';') ptr = endPtr + 1; else ptr = nullptr;

                int token = 1;
                while (ptr && *ptr) {
                    float val = strtof(ptr, &endPtr);
                    if (ptr != endPtr && !isnan(val)) {
                        if (token == 1) {
                            if (val < _cachedMin[10]) _cachedMin[10] = val;
                            if (val > _cachedMax[10]) _cachedMax[10] = val;
                        } else if (token == 2) {
                            if (val < _cachedHumMin) _cachedHumMin = val;
                            if (val > _cachedHumMax) _cachedHumMax = val;
                        } else {
                            int idx = token - 3;
                            if (idx >= 0 && idx < 10) {
                                if (val < _cachedMin[idx]) _cachedMin[idx] = val;
                                if (val > _cachedMax[idx]) _cachedMax[idx] = val;
                            }
                        }
                    }
                    ptr = strchr(endPtr, ';');
                    if (ptr) ptr++;
                    token++;
                }
            }
            hasMore = f.available();
            _storageMgr.exitFlashReadLock();


            watchdog_update();
            TRACE_BEAT(0);
            delay(2);
        }

        _storageMgr.enterFlashReadLock();
        f.close();
        _storageMgr.exitFlashReadLock();
    }

    /* Save preload snapshot (CSV data only, no real-time readings) */
    for (int i = 0; i < 11; i++) {
        _preloadMin[i] = _cachedMin[i];
        _preloadMax[i] = _cachedMax[i];
    }
    _preloadHumMin = _cachedHumMin;
    _preloadHumMax = _cachedHumMax;

    LOG_INF("APP", "Min/Max Cache Loaded for Fast Display.");
}

void AppManager::processHistoryLogging() {
    _lastHistoryTime = millis();
    time_t now = _netMgr.getEpoch();

    if (now > 1600000000) {
        const auto& sensors = _sensorMgr.getRuntimeSensors();
        SystemConfig &cfg = _storageMgr.getConfig();

        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer), "%lu", (unsigned long)now);

        float ambT = NAN, ambH = NAN;
        for (const auto &s : sensors) {
            if (s.config.gpio == 10 && !s.inErrorState) {
                ambT = s.avgValue1; ambH = s.avgValue2;
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

        char tempStr[16];
        if (!isnan(ambT)) snprintf(tempStr, sizeof(tempStr), ";%.2f", ambT);
        else snprintf(tempStr, sizeof(tempStr), ";");
        strcat(logBuffer, tempStr);

        if (!isnan(ambH)) snprintf(tempStr, sizeof(tempStr), ";%.1f", ambH);
        else snprintf(tempStr, sizeof(tempStr), ";");
        strcat(logBuffer, tempStr);

        for (int i = 0; i < MAX_SENSORS; i++) {
            strcat(logBuffer, ";");
            if (cfg.sensors[i].active) {
                for (const auto &s : sensors) {
                    if (s.config.gpio == cfg.sensors[i].gpio && !s.inErrorState) {
                        float v = s.avgValue1;
                        if (!isnan(v)) {
                            snprintf(tempStr, sizeof(tempStr), "%.2f", v);
                            strcat(logBuffer, tempStr);
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

        if (_storageMgr.writeHistoryEntry(String(logBuffer))) LOG_INF("HIST", "Saved periodic log.");
    }
}

void AppManager::openStatsScreen(int sensorId) {
    GraphDataPackage pkg;
    pkg.sensorIdx = sensorId;
    pkg.timeRange = 3;
    pkg.count = 0;

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

    SystemConfig &cfg = _storageMgr.getConfig();
    pkg.hasHumidity = (sensorId == -1);

    if (sensorId == -1) {
        snprintf(pkg.title, sizeof(pkg.title), "Ambient");
        snprintf(pkg.hwId, sizeof(pkg.hwId), "AMB");
        snprintf(pkg.rom, sizeof(pkg.rom), "INTERNAL-DHT");
    } else if (sensorId == 10) {
        snprintf(pkg.title, sizeof(pkg.title), "Board Temp");
        snprintf(pkg.hwId, sizeof(pkg.hwId), "SYS");
        snprintf(pkg.rom, sizeof(pkg.rom), "RP2040-ADC");
    } else {
        if (cfg.sensors[sensorId].active) {
            strncpy(pkg.title, cfg.sensors[sensorId].friendlyName, 31);
            strncpy(pkg.hwId, cfg.sensors[sensorId].hwId, 15);
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

    _displayMgr.showStats(pkg, humMin, humMax);
}

/* =========================================================================== */
/*                     GRAPH RENDERING FROM CSV HISTORY                      */
/* =========================================================================== */
/**
 * @brief Load and render a temperature/humidity graph from CSV history files.
 *
 * Uses a static GraphDataPackage to avoid stack overflow (~2.1KB).
 * Reads CSV files with lightweight ReadLock (Core 1 continues rendering).
 * Supports 5 time ranges with decimation for longer periods.
 * 6-second budget limit prevents watchdog timeout.
 */
void AppManager::renderGraphOptimized(int sensorId, int range) {
    if (!_storageMgr.lockHeavyTask()) {
        LOG_WRN("APP", "Task collision: Web server is using Flash storage.");
        _displayMgr.forceDashboard();
        return;
    }
    LOG_INF("APP", "Optimized Graph Loading...");

    uint32_t _graphBudgetStart = millis();
    const uint32_t GRAPH_BUDGET_MS = 6000;


    static GraphDataPackage pkg;
    memset(&pkg, 0, sizeof(GraphDataPackage));
    pkg.sensorIdx = sensorId;
    pkg.timeRange = range;
    pkg.count = 0;

    pkg.minVal = 1000.0f;
    pkg.maxVal = -1000.0f;
    float localHumMin = 1000.0f;
    float localHumMax = -1000.0f;

    pkg.hasHumidity = (sensorId == -1);

    SystemConfig &cfg = _storageMgr.getConfig();
    uint32_t epochLimit = 0;

    if (sensorId == -1) {
        snprintf(pkg.title, sizeof(pkg.title), "Ambient");
        snprintf(pkg.hwId, sizeof(pkg.hwId), "AMB");
        snprintf(pkg.rom, sizeof(pkg.rom), "INTERNAL-DHT");
    } else if (sensorId == 10) {
        snprintf(pkg.title, sizeof(pkg.title), "Board Temp");
        snprintf(pkg.hwId, sizeof(pkg.hwId), "SYS");
        snprintf(pkg.rom, sizeof(pkg.rom), "RP2040-ADC");
    } else {
        if (sensorId < 10 && cfg.sensors[sensorId].active) {
            strncpy(pkg.title, cfg.sensors[sensorId].friendlyName, 31);
            strncpy(pkg.hwId, cfg.sensors[sensorId].hwId, 15);
            epochLimit = cfg.sensors[sensorId].provisionEpoch;
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
    int decimation = 1;

    if (range == 0) { cutoff = now - 3600; decimation = 1; }
    else if (range == 1) { cutoff = now - 21600; decimation = 1; }
    else if (range == 2) { cutoff = now - 43200; decimation = 3; }
    else if (range == 3) { cutoff = now - 86400; decimation = 5; }
    else if (range == 4) { cutoff = now - 604800; decimation = 35; daysToLoad = 7; }

    if (range <= 3) daysToLoad = 2;

    char lineBuffer[256];

    for (int d = daysToLoad - 1; d >= 0; d--) {
        if (pkg.count >= GRAPH_WIDTH) break;

        time_t targetDay = now - (d * 86400);
        struct tm timeinfo;
        localtime_r(&targetDay, &timeinfo);

        char path[40];
        snprintf(path, sizeof(path), "/history/%04d%02d%02d.csv", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

        File f;


        _storageMgr.enterFlashReadLock();
        bool fileExists = LittleFS.exists(path);
        if (fileExists) f = LittleFS.open(path, "r");
        _storageMgr.exitFlashReadLock();

        if (fileExists && f) {
            int lineIdx = 0;
            bool hasMore = true;
            bool budgetExceeded = false;

            while (hasMore && pkg.count < GRAPH_WIDTH && !budgetExceeded) {

                if (millis() - _graphBudgetStart > GRAPH_BUDGET_MS) {
                    LOG_WRN("APP", "renderGraph aborted — 6s budget exceeded.");
                    budgetExceeded = true;
                    break;
                }


                _storageMgr.enterFlashReadLock();
                int linesInBatch = 0;
                int emptyLines = 0;
                while (f.available() && linesInBatch < 20 && pkg.count < GRAPH_WIDTH) {
                    size_t len = f.readBytesUntil('\n', lineBuffer, sizeof(lineBuffer) - 1);
                    if (len == 0) {
                        emptyLines++;
                        if (emptyLines >= 40) break;
                        continue;
                    }
                    emptyLines = 0;
                    lineBuffer[len] = '\0';
                    linesInBatch++;

                    lineIdx++;
                    if (lineIdx % decimation != 0) continue;

                    const char* ptr = lineBuffer;
                    char* endPtr;
                    time_t ts = strtoul(ptr, &endPtr, 10);

                    if (ts < cutoff) continue;
                    if (*endPtr == ';') ptr = endPtr + 1; else ptr = nullptr;

                    int targetToken = (sensorId == -1) ? 1 : (3 + sensorId);
                    int currentToken = 1;
                    float valRead = NAN, humRead = NAN;

                    while (ptr && *ptr) {
                        if (currentToken == targetToken) {
                            valRead = strtof(ptr, &endPtr);
                            if (ts < epochLimit) valRead = NAN;
                        }
                        if (sensorId == -1 && currentToken == 2) humRead = strtof(ptr, &endPtr);
                        ptr = strchr(ptr, ';'); if (ptr) ptr++;
                        currentToken++;
                        if (sensorId != -1 && currentToken > targetToken) break;
                    }

                    if (!isnan(valRead)) {
                        pkg.pointsV1[pkg.count] = valRead;
                        if (valRead < pkg.minVal) pkg.minVal = valRead;
                        if (valRead > pkg.maxVal) pkg.maxVal = valRead;
                        if (pkg.hasHumidity && !isnan(humRead)) {
                            pkg.pointsV2[pkg.count] = humRead;
                            if (humRead < localHumMin) localHumMin = humRead;
                            if (humRead > localHumMax) localHumMax = humRead;
                        }
                        pkg.count++;
                    }
                }
                hasMore = f.available();
                _storageMgr.exitFlashReadLock();


                watchdog_update();
                TRACE_BEAT(0);
                delay(1);
            }


            _storageMgr.enterFlashReadLock();
            f.close();
            _storageMgr.exitFlashReadLock();

            if (budgetExceeded) {
                _storageMgr.unlockHeavyTask();
                _displayMgr.forceDashboard();
                return;
            }
        }

        watchdog_update();
        yield();
    }

    if (pkg.count > 0) {
        if (pkg.maxVal - pkg.minVal < 1.0f) {
            pkg.maxVal += 0.5f;
            pkg.minVal -= 0.5f;
        } else {
            float rangeDelta = pkg.maxVal - pkg.minVal;
            pkg.maxVal += rangeDelta * 0.10f;
            pkg.minVal -= rangeDelta * 0.10f;
        }

        if (pkg.hasHumidity && localHumMax > -1000.0f) {
            if (localHumMax - localHumMin < 5.0f) {
                localHumMax += 2.5f;
                localHumMin -= 2.5f;
            } else {
                float humRange = localHumMax - localHumMin;
                localHumMax += humRange * 0.10f;
                localHumMin -= humRange * 0.10f;
            }
            if (localHumMax > 100.0f) localHumMax = 100.0f;
            if (localHumMin < 0.0f) localHumMin = 0.0f;
        } else {
            localHumMin = 0.0f;
            localHumMax = 100.0f;
        }
    } else {
        pkg.minVal = 0.0f;
        pkg.maxVal = 40.0f;
        localHumMin = 0.0f;
        localHumMax = 100.0f;
    }

    _storageMgr.unlockHeavyTask();

    _displayMgr.showGraphPlot(pkg, localHumMin, localHumMax);
}

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
        }
    }
}

void AppManager::processBackgroundScan() {
    std::vector<ScanResult> results;
    if (_sensorMgr.getScanResults(results)) {
        _appWaitingScan = false;
        _cmdMgr.renderScanResults(results);
        loadAndCalibrateSensors();
        _cmdMgr.printPrompt();
    }
}

bool AppManager::isDisplayAlive() {


    if (_storageMgr.lockHeavyTask() == false) return true;
    _storageMgr.unlockHeavyTask();

    uint32_t now = millis();
    uint32_t beat = _displayMgr.getHeartbeat();

    if (beat >= now) return true;
    return (now - beat < 5000);
}

void AppManager::restartDisplayCore() { _displayMgr.startCore1(); }


void AppManager::handleTimeSync(uint32_t bootTs, int32_t delta) {
    if (!_storageMgr.lockHeavyTask()) {

        return;
    }
    _pendingTimeSync = false;
    LOG_INF("APP", "NTP Correction: Adjusting offline records by " + String(delta) + "s...");
    _storageMgr.correctProvisionalTimestamps(bootTs, delta);
    LOG_INF("APP", "History successfully corrected!");
    _storageMgr.unlockHeavyTask();
}

void AppManager::loadAndCalibrateSensors() {
    SystemConfig &cfg = _storageMgr.getConfig();
    _sensorMgr.initRuntimeSensors(cfg);

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].active && cfg.sensors[i].gpio != PIN_DHT_DEFAULT) {
            String dbId; float dbOffset = 0.0f; String dbName;
            if (_storageMgr.getCalibrationData(cfg.sensors[i].rom, dbId, dbOffset, dbName)) {
                _sensorMgr.applyCalibration(cfg.sensors[i].gpio, dbId, dbOffset, dbName);
                if (dbId.length() > 0) { strncpy(cfg.sensors[i].hwId, dbId.c_str(), 15); cfg.sensors[i].hwId[15] = '\0'; }
                if (dbName.length() > 0) { strncpy(cfg.sensors[i].friendlyName, dbName.c_str(), 31); cfg.sensors[i].friendlyName[31] = '\0'; }
            }
        }
    }
    LOG_INF("APP", "Sensors aligned with calibration table.");
}


/* =========================================================================== */
/*                        ALARM CONDITION MONITORING                         */
/* =========================================================================== */
/**
 * @brief Check all active sensors against configured alarm thresholds.
 *
 * Builds a bitmask of alarming slots and detects ambient sensor alarms
 * (temperature and humidity separately). Manages sound start/stop
 * transitions and respects silence/deactivation states.
 */
void AppManager::checkAlarmConditions() {
    const auto& sensors = _sensorMgr.getRuntimeSensors();
    SystemConfig &cfg = _storageMgr.getConfig();
    bool anyAlarm    = false;
    uint16_t mask    = 0;
    int8_t firstSlot = -1;


    bool ambTempAlarm = false;
    bool ambHumAlarm  = false;
    if (cfg.ambientSensor.alarmsActive) {
        for (const auto &s : sensors) {
            if (s.config.gpio != 10 || s.inErrorState) continue;
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
        uint8_t targetGpio = cfg.sensors[i].gpio;

        for (const auto &s : sensors) {
            if (s.config.gpio != targetGpio || s.inErrorState) continue;

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


    bool silenced = _displayMgr.isAlarmSilenced();

    if (anyAlarm && !_soundMgr.isAlarming() && !silenced) {

        _soundMgr.startAlarm();
        if (firstSlot >= 0) {
            _currentSensorIndex = firstSlot;
            refreshSelectedSlot();
        }
        _displayMgr.setAlarmState(mask, firstSlot, ambTempAlarm, ambHumAlarm);
        LOG_WRN("APP", "Alarm triggered: sensor value out of range.");
    } else if (anyAlarm && (_soundMgr.isAlarming() || silenced)) {

        _displayMgr.setAlarmState(mask, -1, ambTempAlarm, ambHumAlarm);
    } else if (!anyAlarm && (_soundMgr.isAlarming() || silenced)) {

        _soundMgr.stopAlarm();
        _displayMgr.setAlarmState(0, -1, false, false);

        if (silenced) {
            _displayMgr.setAlarmSilenced(false, 0);
            LOG_INF("APP", "Alarm silence cancelled — conditions cleared.");
        }
        LOG_INF("APP", "Alarm cleared: all sensors within range.");
    }
}


/**
 * @brief Check if the user recently touched the display.
 * Returns true if last touch was within TOUCH_PRIORITY_MS (2s).
 * During this window, flash I/O that would pause Core 1 is deferred.
 */
bool AppManager::isUserInteracting() const {
    uint32_t lastTouch = _displayMgr.getLastTouchTimestamp();
    if (lastTouch == 0) return false;
    return (millis() - lastTouch) < TOUCH_PRIORITY_MS;
}
