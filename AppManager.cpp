/**
 * @file    AppManager.cpp
 * @brief   Implementation of AppManager — boot sequence, main loop, and event dispatch.
 * @details Contains the complete boot flow (filesystem, sensors, network, web server),
 * the main loop with priority-based task scheduling, CLI command execution,
 * graph rendering from CSV history, alarm condition checking, and
 * provisional timestamp correction via NTP synchronization.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "MetricsManager.h"
#include "SystemDefs.h"
#include "Themes.h"
#include "TouchPriority.h"
#include <LittleFS.h>
#include <time.h>
#include <math.h>
#include <hardware/watchdog.h>
#include <string.h>

extern AppManager app;



AppManager::AppManager() {
    for(int i = 0; i < MINMAX_SLOT_COUNT; i++) {
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

    /*
     * NÃO chamar TRACE_MOD aqui — scratch[4] precisa conter o módulo do
     * crash anterior até a autópsia ler (em LogManager::begin abaixo).
     * TRACE_BEAT(0) é OK: só mexe em RAM (_coreHeartbeat), não no scratch.
     */
    TRACE_BEAT(0);

    _displayMgr.begin();
    _displayMgr.startCore1();
    LOG_CODE(LOG_INFO, "APP", APP_DISPLAY_LAUNCHED, 0, TRL("Display UI Launched on Core 1.", "UI do display iniciada no Core 1."));

    delay(BOOT_STEP_DELAY_MS);

    bool forceAP = false;
    _displayMgr.setBootStatus("Hold screen for AP Mode...");
    unsigned long waitStart = millis();

    while (millis() - waitStart < AP_DETECT_WINDOW_MS) {
        TRACE_BEAT(0);

        if (_displayMgr.isScreenTouched()) {
            unsigned long holdStart = millis();
            bool held = true;
            int missedTouches = 0;

            while (millis() - holdStart < AP_HOLD_DURATION_MS) {
                TRACE_BEAT(0);
                if (!_displayMgr.isScreenTouched()) {
                    missedTouches++;
                    if (missedTouches > AP_HOLD_MAX_MISSED) {
                        held = false;
                        _displayMgr.setApProgress(-1);
                        _displayMgr.setBootStatus("AP Mode Cancelled.", false);
                        delay(800);
                        break;
                    }
                } else {
                    missedTouches = 0;
                }
                int pct = map(millis() - holdStart, 0, AP_HOLD_DURATION_MS, 0, 100);
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

    /* F-LOCKOUT-STUCK: wire quiet mode cooperativo para saveConfiguration.
     * Core 0 sinaliza, Core 1 congela em loop RAM-only, Core 0 faz flash
     * ops sem cascatas de lockout IRQ stuck. Retorna true só se Core 1 ACKed. */
    _storageMgr.setBigSaveQuietCallback([](bool enable) -> bool {
        return app.requestDisplayQuietMode(enable);
    });

    _displayMgr.setBootStatus("Mounting File System...");
    bool fsOk = _storageMgr.begin();

    _displayMgr.setBootStatus("Starting Log Manager...");
    LogManager::instance().begin(fsOk, LOG_DEBUG);

    /* Autópsia já leu scratch[4]. Agora pode setar MOD_BOOT para rastrear
     * estalls que aconteçam durante o restante do setup. */
    TRACE_MOD(0, MOD_BOOT);

    LogManager::instance().setHeavyTaskChecker([]() -> bool {
        return app._storageMgr.isHeavyTaskLocked();
    });


    /* REF-004: provider único em vez de 3 setters duplicados (Log/Storage/Web).
     * Setado aqui, antes que qualquer manager possa consultar via
     * TouchPriority::isActive() durante o boot. */
    TouchPriority::setProvider([]() -> bool {
        return app.isUserInteracting();
    });

    _displayMgr.setBootStatus("Starting Command Interface...");
    _cmdMgr.begin();


    _cmdMgr.setBtValidator([this](String attempt) -> bool {
        SystemConfig &cfg = _storageMgr.getConfig();
        if (!cfg.users[0].active) return false;
        /* Frontend envia SHA256(plaintext) antes do hashPassword;
         * sha256Hex espelha esse comportamento (UTF-8 → Latin-1). */
        String preHash = _storageMgr.sha256Hex(attempt);
        String hashed = _storageMgr.hashPassword(
            String(cfg.users[0].username), preHash);
        return (hashed == String(cfg.users[0].password));
    });

    if (!fsOk) LOG_CODE(LOG_ERROR, "APP", APP_STORAGE_CRITICAL, 0, TRL("Storage Critical Failure!", "Falha critica de storage!"));

    /* SEC-003/F12.3: se o dispositivo subiu em factory defaults (config
     * inexistente ou corrompida nos dois bancos), exibe a senha inicial
     * aleatória no Serial — exige acesso físico USB. Também loga no FS via
     * LOG_CODE pra trilha de auditoria. Plaintext nunca persiste em flash. */
    if (_storageMgr.isFactoryDefaults()) {
        const char* pw = _storageMgr.getInitialAdminPassword();
        if (pw && pw[0] != '\0') {
            Serial.println(F("\n=============================================="));
            Serial.println(F("  SEC-003: FACTORY DEFAULTS ATIVADO"));
            Serial.print  (F("  Senha ADMIN inicial: "));
            Serial.println(pw);
            Serial.println(F("  Trocar no primeiro login (forcado)."));
            Serial.println(F("=============================================="));
            LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, 0,
                     TRL("Factory defaults active; initial admin pass on USB/serial.",
                         "Factory defaults ativos; senha admin via USB/serial."));
        } else {
            /* Caso raro: factory detectado mas plaintext não está em RAM
             * (loadConfiguration limpou após fallback). Avisa sem vazar. */
            LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, 0,
                     TRL("Factory defaults active; password regen required.",
                         "Factory defaults ativos; reset admin para gerar senha."));
        }
    }

    uint32_t lastTs = _storageMgr.getLastRecordedTimestamp();
    _netMgr.setProvisionalTime(lastTs);
    _netMgr.setTimeSyncCallback([](uint32_t bootTs, int32_t delta) {


        app._timeSyncBootTs = bootTs;
        app._timeSyncDelta = delta;
        __dmb();  /* Memory barrier: garante que bootTs/delta são visíveis antes da flag */
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

    /* Offset de posicionamento do display — aplicado antes de qualquer tela
     * subsequente para que boot statuses já reflitam o alinhamento salvo. */
    {
        const DisplayOffsetData* ofs = reinterpret_cast<const DisplayOffsetData*>(
            cfg.reserved + sizeof(TouchCalData) + sizeof(SoundConfigData));
        _displayMgr.loadDisplayOffset(ofs);
    }

    /* B4: modo CLI (debug/config). Default = CONFIG (debug OFF) se magic inválido. */
    {
        const CliConfigData* cli = reinterpret_cast<const CliConfigData*>(
            cfg.reserved + CLI_CONFIG_OFFSET);
        bool debugOn = (cli->magic == CLI_CONFIG_MAGIC) && (cli->debugMode != 0);
        LogManager::instance().setConsoleStream(debugOn);
        _cmdMgr.setDebugMode(debugOn);
    }

    /* #2: idioma da CLI reutiliza cfg.displayLang (single source of truth).
     *     Propaga também para LogManager (labels de translateCode). */
    _cmdMgr.setCliLang(cfg.displayLang);
    LogManager::instance().setLanguage(cfg.displayLang);


    {
        const TouchCalData* cal = reinterpret_cast<const TouchCalData*>(cfg.reserved);
        _displayMgr.loadTouchCalibration(cal);
        if (!_displayMgr.isTouchCalibrated()) {
            LOG_CODE(LOG_WARN, "APP", APP_TOUCH_CAL_REQUIRED, 0, TRL("Touch calibration required.", "Calibracao do touch necessaria."));
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
                        LOG_CODE(LOG_INFO, "APP", APP_TOUCH_CAL_INITIAL, 0, TRL("Initial touch calibration saved.", "Calibracao inicial do touch salva."));
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
        LOG_CODE(LOG_WARN, "APP", APP_AP_MODE_TRIGGERED, 0, TRL("User triggered AP mode.", "Usuario ativou modo AP."));
        _displayMgr.setBootStatus("Starting Access Point (AP)...");
        _displayMgr.setBootStatus("Connect to network SIMUT_SETUP");
        _displayMgr.setBootStatus("Access on mobile: 192.168.4.1");
        _netMgr.beginAP(cfg.deviceName);
        for (int i = 0; i < 35; i++) { delay(100); watchdog_update(); TRACE_BEAT(0); }
    } else {
        _displayMgr.setBootStatus("Starting Wi-Fi Interface...");
        _netMgr.begin(cfg,
                      _storageMgr.isDnsAuto(),
                      _storageMgr.isNtpEnabled(),
                      _storageMgr.getSecondaryDns());

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

            if (timeSince(lastMsg, BOOT_WAIT_DOT_INTERVAL_MS)) {
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

            if (timeSince(netWait, 30000)) {
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


    /* REF-004: _webMgr.setTouchPriorityChecker removido — usa TouchPriority singleton. */

    if (forceAP) {
        _isApMode = true;
        _displayMgr.setBootStatus("AP Active! Reboot board to exit.", false);
        LOG_CODE(LOG_INFO, "APP", APP_READY_AP, 0, TRL("System ready (AP mode).", "Sistema pronto (modo AP)."));
    } else {

        /* Carrega min/max do dia a partir do arquivo de histórico */
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


                if (timeSince(warmStart, 900)) break;

                delay(10);
            }


            updateLiveDisplay();
            refreshSelectedSlot();
        }


        if (_pendingTimeSync) {
            _displayMgr.setBootStatus("Correcting timestamps (NTP)...");
            delay(80);
            handleTimeSync(_timeSyncBootTs, _timeSyncDelta);

            /* Recarrega min/max com timestamps corrigidos */
            _displayMgr.setBootStatus("Reloading Min/Max cache...");
            delay(80);
            for (int i = 0; i < MINMAX_SLOT_COUNT; i++) {
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
        LOG_CODE(LOG_INFO, "APP", APP_READY, 0, TRL("System ready.", "Sistema pronto."));
        _displayMgr.endBoot();
        _bootCompletedAt = millis();


        _soundMgr.play(SND_CONFIRM);
    }

    /*
     * Habilita monitoramento cross-core APÓS boot completo.
     * Força refresh de heartbeats de ambos os cores para evitar
     * detecção falsa de heartbeat estagnado durante o boot.
     * O grace period de 5s começa a contar a partir daqui.
     */
    LogManager::instance().enableHealthCheck();

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
    watchdog_update();

    /* Fase 5: edge detection touch-released → orchestrated flush. */
    bool isNow = isUserInteracting();
    if (_wasInteracting && !isNow) {
        onTouchReleased();
    }
    _wasInteracting = isNow;

    LogManager::instance().checkCrossCoreHealth();

    /* #8 + U3/5.5: heap/HWM + largest contiguous block a cada 10s.
     * sampleLargestBlock faz ~16 malloc/free (imediatamente freed). */
    {
        static uint32_t _lastHeapSample = 0;
        if (timeSince(_lastHeapSample, 10000)) {
            _lastHeapSample = millis();
            MetricsManager::instance().sampleHeap();
            MetricsManager::instance().sampleLargestBlock();
        }
    }


    {
        uint32_t pauseTs = _displayMgr.getPauseStartTime();
        if (pauseTs > 0 && timeSince(pauseTs, 5000)) {
            LOG_CODE(LOG_ERROR, "APP", APP_DISPLAY_PAUSE_STUCK, 0, TRL("Display pause stuck >5s!", "Pause do display preso >5s!"));
            _displayMgr.forceUnpause();
        }
    }


    {
        static uint32_t _lastCore1RestartCheck = 0;
        if (_lastCore1RestartCheck == 0) _lastCore1RestartCheck = millis(); /* Boot guard */
        if (timeSince(_lastCore1RestartCheck, 5000)) {
            _lastCore1RestartCheck = millis();
            if (_displayMgr.isCore1Ready() && _displayMgr.getPauseStartTime() == 0) {
                uint32_t beat = _displayMgr.getHeartbeat();
                /* Patch C: signed cast para tolerar cross-core race (beat
                 * levemente adiantado em relacao a millis() local). */
                if (beat > 0 && timeSince(beat, 10000)) {
                    LOG_CODE(LOG_ERROR, "APP", APP_CORE1_DEAD, 0, TRL("Core 1 dead >10s. Restarting.", "Core 1 travado >10s. Reiniciando."));
                    _displayMgr.restartCore1();
                }
            }
        }
    }

    CliDemand cmd;
    TRACE_MOD(0, MOD_CLI);

    /* Fase 4: drain de 1 comando enfileirado por loop quando touch livre.
     * Executamos antes do processInput pra não atrasar o prompt caso o
     * user acabe de digitar. */
    if (_cliQueueCount > 0 && !isUserInteracting()) {
        CliDemand queued = _cliQueue[_cliQueueHead];
        _cliQueue[_cliQueueHead] = CliDemand();  /* limpa Strings — libera heap */
        _cliQueueHead = (_cliQueueHead + 1) % CLI_QUEUE_CAP;
        _cliQueueCount--;
        if (queued.type != CMD_UNKNOWN) executeCommand(queued);
        if (!_waitingScan) _cmdMgr.printPrompt();
        _cliDropNotified = false;
    }

    if (_cmdMgr.processInput(cmd)) {
        if (cmd.type != CMD_UNKNOWN) {
            if (isUserInteracting()) {
                if (_cliQueueCount < CLI_QUEUE_CAP) {
                    uint8_t tail = (_cliQueueHead + _cliQueueCount) % CLI_QUEUE_CAP;
                    _cliQueue[tail] = cmd;
                    _cliQueueCount++;
                } else if (!_cliDropNotified) {
                    _cmdMgr.printError(_cmdMgr.isPt()
                        ? String("CLI ocupada (display em uso). Comando descartado.")
                        : String("CLI busy (display in use). Command dropped."));
                    _cliDropNotified = true;
                }
            } else {
                executeCommand(cmd);
            }
        }
        if (!_waitingScan) _cmdMgr.printPrompt();
    }

    watchdog_update();

    TRACE_MOD(0, MOD_WIFI);
    _netMgr.update();

    watchdog_update();

    bool heavyRendering = _displayMgr.isHeavyRendering();


    TRACE_MOD(0, MOD_WEB_SERVER);
    _webMgr.update();

    watchdog_update();

    /*
     * Processa som de toque ANTES de tarefas pesadas (telemetria, storage).
     * Garante que o bip toca em <10ms após o toque em vez de esperar
     * o final do loop (~100-500ms com telemetria/TLS ativa).
     */
    if (_displayMgr.consumeTouchSound()) {
        _soundMgr.play(SND_TOUCH_CLICK);
        _soundMgr.update();
    }
    if (_displayMgr.consumeErrorSound()) {
        _soundMgr.play(SND_ERROR);
        _soundMgr.update();
    }

    bool menuActive = _displayMgr.isMenuActive();

    TRACE_MOD(0, MOD_STORAGE_WRITE);
    _storageMgr.update();
    _storageMgr.flushCursorIfDirty();

    watchdog_update();

    if (_isApMode) {
        TRACE_MOD(0, MOD_IDLE);
        return;
    }

    if (!menuActive) {
        TRACE_MOD(0, MOD_TELEMETRY);
        if (!heavyRendering && !isUserInteracting()) {
            /*
             * Invalida cache de ranges do sensor antes da telemetria.
             * Libera ~11KB de heap para o payload + TLS. Será reconstruído
             * automaticamente na próxima abertura de gráfico.
             */
            if (_sensorCacheId != -99) {
                for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;
                _sensorCacheId = -99;
            }

            _telemetryMgr.update();

            /* Notificar o display sobre o resultado do último envio */
            bool telSuccess;
            if (_telemetryMgr.consumeLastSendResult(telSuccess)) {
                _displayMgr.setTelemetrySendStatus(telSuccess);
            }
        }
    }

    watchdog_update();

    TRACE_MOD(0, MOD_SENSOR_READ);
    if (timeSince(_lastSensorCheck, 3000)) {
        if (!isUserInteracting()) {
            _lastSensorCheck = millis();
            checkAndAutoHealSensors();
        }
    }

    watchdog_update();

    if (_pendingTimeSync && !isUserInteracting()) {
        handleTimeSync(_timeSyncBootTs, _timeSyncDelta);
    }

    TRACE_MOD(0, MOD_STORAGE_WRITE);

    watchdog_update();


    if (timeSince(_lastHistoryTime, 60000)) {
        if (!_storageMgr.isHeavyTaskLocked() && !isUserInteracting()) {
            processHistoryLogging();
        }
    }

    watchdog_update();

    /* ── Status do sistema: atualiza dados a cada 1s quando tela ativa ── */
    {
        static uint32_t lastStatusPush = 0;
        if (_displayMgr.getUiMode() == MODE_SETTINGS_STATUS
            && timeSince(lastStatusPush, 1000)) {
            lastStatusPush = millis();

            static SystemStatusData sd;
            memset(&sd, 0, sizeof(sd));

            /* Sistema */
            sd.uptimeSec  = millis() / 1000;
            sd.heapFree   = rp2040.getFreeHeap();
            sd.heapTotal  = rp2040.getTotalHeap();
            sd.boardTemp  = analogReadTemp();

            /* Flash: usa cache do WebManager (atualizado a cada 10s) */
            sd.flashUsed  = _webMgr.getCachedFlashUsed();
            sd.flashTotal = _webMgr.getCachedFlashTotal();

            SystemConfig& cfg = _storageMgr.getConfig();
            safeCopy(sd.deviceName, cfg.deviceName, sizeof(sd.deviceName));
            snprintf(sd.fwVersion, sizeof(sd.fwVersion), "%s", SIMUT_VERSION);
            sd.timezone = cfg.timezoneOffset;

            /* Rede */
            sd.wifiConnected = _netMgr.isConnected();
            sd.rssi          = _netMgr.getRssi();
            sd.ntpSynced     = _netMgr.isTimeSynced();
            safeCopy(sd.ip, _netMgr.getIpAddress().c_str(), sizeof(sd.ip));
            safeCopy(sd.mac, _netMgr.getMacAddress().c_str(), sizeof(sd.mac));
            safeCopy(sd.ssid, cfg.wifiSsid, sizeof(sd.ssid));
            safeCopy(sd.ntpServer, cfg.ntpServer, sizeof(sd.ntpServer));

            /* Telemetria */
            sd.telPending    = _telemetryMgr.getPendingEstimate();
            sd.mqttConnected = _telemetryMgr.isMqttConnected();
            sd.telTransport  = cfg.telTransport;
            sd.telInterval   = cfg.telInterval;
            safeCopy(sd.telServer, cfg.telServer, sizeof(sd.telServer));

            /* Sensores */
            sd.activeSensors = 0;
            for (int i = 0; i < MAX_SENSORS; i++) {
                if (cfg.sensors[i].active) sd.activeSensors++;
            }
            /* Ambient: lê do runtime dos sensores */
            sd.ambientValid = false;
            const auto& sensors = _sensorMgr.getRuntimeSensors();
            for (const auto& s : sensors) {
                if (s.config.gpio == 10 && !s.inErrorState) {
                    sd.ambientTemp  = s.avgValue1;
                    sd.ambientHum   = s.avgValue2;
                    sd.ambientValid = true;
                    break;
                }
            }

            _displayMgr.updateSystemStatus(sd);
        }
    }

    watchdog_update();

    watchdog_update();

    if (_waitingScan && !isUserInteracting()) {
        processBackgroundScan();
    }

    watchdog_update();

    core0Yield();

    watchdog_update();

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
            _cmdMgr.consolePrintln("");
            _cmdMgr.consolePrintln("--- Available Themes ---");
            for(int i=0; i<getThemeCount(); i++) {
                _cmdMgr.consolePrintf(" %2d %-12s %s\n", i, getThemeId(i).c_str(), availableThemes[i].displayName);
            }
            _cmdMgr.consolePrintln("-------------------------------------------");
            break;

        case CMD_SET_THEME: {
            /* CON-005b: funções aceitam String; envolve o char[] em temporário. */
            int idx = getThemeIndexByName(String(cmd.strVal1));
            if (idx == -1) {
                /* Não bateu como nome — tenta como índice numérico, mas só se
                 * for número bem-formado (evita "abc".toInt()==0 aplicar tema 0). */
                int numericIdx = 0;
                if (!parseIntStrict(String(cmd.strVal1), numericIdx)) idx = -1;
                else idx = numericIdx;
            }
            if (idx >= 0 && idx < getThemeCount()) {
                cfg.themeIndex = idx;
                loadTheme(idx);
                _displayMgr.refreshTheme();
                changed = true;
                LOG_CODE(LOG_INFO, "CFG", CFG_THEME_APPLIED, idx, String(availableThemes[idx].displayName));
                _cmdMgr.printSuccess(String(_cmdMgr.isPt() ? "Tema: " : "Theme: ")
                                     + availableThemes[idx].displayName);
            } else {
                LOG_CODE(LOG_WARN, "CFG", CFG_THEME_NOT_FOUND, 0, "");
                _cmdMgr.printError(_cmdMgr.isPt()
                    ? "Tema nao encontrado. Veja 'show themes'."
                    : "Theme not found. Try 'show themes'.");
            }
            break;
        }

        case CMD_SHOW_LOGS: {
            _cmdMgr.consolePrintln("");
            _cmdMgr.consolePrintln("--- SYSTEM LOG START ---");
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
            _cmdMgr.consolePrintln("--- SYSTEM LOG END ---");
            _cmdMgr.consolePrintln("");
            break;
        }

        case CMD_SHOW_SENSORS: _cmdMgr.renderSensorTable(cfg.sensors, MAX_SENSORS); break;
        case CMD_SHOW_METRICS: _cmdMgr.renderMetrics(); break;
        case CMD_SHOW_STORAGE: {
            String rep = _storageMgr.getStatsReport();
            LOG_CODE(LOG_INFO, "STO", STO_STATS_REPORT, 0, rep);
            _cmdMgr.consolePrintln("");
            _cmdMgr.consolePrintln(_cmdMgr.isPt()
                ? "--- Estatisticas do Flash ---"
                : "--- Storage Stats ---");
            _cmdMgr.consolePrintln(rep);
            _cmdMgr.printDivider();
            break;
        }
        case CMD_SHOW_SYSINFO: _cmdMgr.renderSystemInfo(cfg); break;
        case CMD_SHOW_NET: {
            String ip = _netMgr.getIpAddress();
            LOG_CODE(LOG_INFO, "NET", NET_SHOW_IP, 0, ip);
            _cmdMgr.consolePrintln("");
            _cmdMgr.consolePrintln(_cmdMgr.isPt()
                ? "--- Status da Rede ---"
                : "--- Network Status ---");
            _cmdMgr.consolePrintf (" IP:   %s\n", ip.c_str());
            _cmdMgr.consolePrintf (" RSSI: %ld dBm\n", (long)_netMgr.getRssi());
            _cmdMgr.printDivider();
            break;
        }

        case CMD_SET_DS_RES: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para resolucao"
                                      : "Invalid number for resolution");
                break;
            }
            if (cmd.intVal1 < 9 || cmd.intVal1 > 12) {
                _cmdMgr.printError(pt ? "Resolucao fora de range (9-12)"
                                      : "Resolution out of range (9-12)");
                break;
            }
            if (!_sensorMgr.setDs18Resolution((DS18B20PIO::Resolution)cmd.intVal1)) {
                _cmdMgr.printError(pt ? "Falha ao aplicar resolucao no sensor"
                                      : "Failed to apply resolution");
                break;
            }
            cfg.ds18Resolution = cmd.intVal1;
            changed = true;
            break;
        }

        case CMD_SET_SYS_NAME: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidName(cmd.strVal1, sizeof(cfg.deviceName) - 1)) {
                _cmdMgr.printError(pt ? "Nome invalido (1-31 chars, sem ctrl chars)"
                                      : "Invalid name (1-31 chars, no ctrl chars)");
                break;
            }
            safeCopy(cfg.deviceName, cmd.strVal1, sizeof(cfg.deviceName));
            changed = true;
            break;
        }
        case CMD_SET_WIFI_SSID: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.wifiSsid) - 1)) {
                _cmdMgr.printError(pt ? "SSID invalido (max 31, sem ctrl chars)"
                                      : "Invalid SSID (max 31, no ctrl chars)");
                break;
            }
            safeCopy(cfg.wifiSsid, cmd.strVal1, sizeof(cfg.wifiSsid));
            changed = true;
            break;
        }
        case CMD_SET_WIFI_PASS: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.wifiPass) - 1)) {
                _cmdMgr.printError(pt ? "Senha invalida (max 31, sem ctrl chars)"
                                      : "Invalid pass (max 31, no ctrl chars)");
                break;
            }
            safeCopy(cfg.wifiPass, cmd.strVal1, sizeof(cfg.wifiPass));
            changed = true;
            break;
        }
        case CMD_SET_TIMEZONE: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para timezone"
                                      : "Invalid number for timezone");
                break;
            }
            if (cmd.intVal1 < -12 || cmd.intVal1 > 14) {
                _cmdMgr.printError(pt ? "Timezone fora de range (-12 a +14)"
                                      : "Timezone out of range (-12 to +14)");
                break;
            }
            cfg.timezoneOffset = (int8_t)cmd.intVal1;
            NetworkManager::applyTimezone(cfg.timezoneOffset);
            changed = true;
            break;
        }

        case CMD_SET_NTP: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.ntpServer) - 1)) {
                _cmdMgr.printError(pt ? "NTP invalido (max 31, sem ctrl chars)"
                                      : "Invalid NTP (max 31, no ctrl chars)");
                break;
            }
            safeCopy(cfg.ntpServer, cmd.strVal1, sizeof(cfg.ntpServer));
            cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
            changed = true;
            break;
        }

        case CMD_SET_TEL_SERVER: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.telServer) - 1)) {
                _cmdMgr.printError(pt ? "URL invalida (max 63, sem ctrl chars)"
                                      : "Invalid URL (max 63, no ctrl chars)");
                break;
            }
            safeCopy(cfg.telServer, cmd.strVal1, sizeof(cfg.telServer));
            changed = true;
            break;
        }
        case CMD_SET_TEL_PORT: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para porta"
                                      : "Invalid number for port");
                break;
            }
            if (cmd.intVal1 < 1 || cmd.intVal1 > 65535) {
                _cmdMgr.printError(pt ? "Porta fora de range (1-65535)"
                                      : "Port out of range (1-65535)");
                break;
            }
            cfg.telPort = (uint16_t)cmd.intVal1;
            changed = true;
            break;
        }
        case CMD_SET_TEL_PATH: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.telPath) - 1)) {
                _cmdMgr.printError(pt ? "Path invalido (max 31, sem ctrl chars)"
                                      : "Invalid path (max 31, no ctrl chars)");
                break;
            }
            safeCopy(cfg.telPath, cmd.strVal1, sizeof(cfg.telPath));
            changed = true;
            break;
        }
        case CMD_SET_TEL_BATCH: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para batch"
                                      : "Invalid number for batch");
                break;
            }
            if (cmd.intVal1 < 1 || cmd.intVal1 > 50) {
                _cmdMgr.printError(pt ? "Batch fora de range (1-50)"
                                      : "Batch out of range (1-50)");
                break;
            }
            cfg.telBatchSize = (uint8_t)cmd.intVal1;
            changed = true;
            break;
        }
        case CMD_SET_TEL_INTERVAL: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para intervalo"
                                      : "Invalid number for interval");
                break;
            }
            if (cmd.intVal1 < 0) {
                _cmdMgr.printError(pt ? "Intervalo deve ser >= 0 (0 = off)"
                                      : "Interval must be >= 0 (0 = off)");
                break;
            }
            cfg.telInterval = (uint32_t)cmd.intVal1;
            changed = true;
            break;
        }
        case CMD_SET_TEL_CRYPTO: {
            const bool pt = _cmdMgr.isPt();
            if (strcmp(cmd.strVal1, "on") != 0 && strcmp(cmd.strVal1, "off") != 0) {
                _cmdMgr.printError(pt ? "Use 'on' ou 'off'" : "Use 'on' or 'off'");
                break;
            }
            cfg.telEncryption = cmd.boolVal;
            changed = true;
            break;
        }
        case CMD_SET_TEL_MODE: {
            const bool pt = _cmdMgr.isPt();
            if (cmd.intVal1 < 0) {
                _cmdMgr.printError(pt ? "Modo desconhecido (use json|csv|custom)"
                                      : "Unknown mode (use json|csv|custom)");
                break;
            }
            cfg.telMode = cmd.intVal1;
            changed = true;
            break;
        }

        case CMD_RESET_ADMIN: {
            if (!cmd.confirmed) {
                const bool pt = _cmdMgr.isPt();
                _cmdMgr.printInfo(pt ? "ATENCAO: reseta senha do admin p/ aleatoria."
                                     : "WARN: resets admin password to random 8-char.");
                _cmdMgr.printInfo(pt ? "Use 'conf system admin reset confirm'."
                                     : "Run 'conf system admin reset confirm'.");
                break;
            }
            /* SEC-003/F12.3: gera senha random no reset CLI também (em vez de "simut"
             * hardcoded, que era tão vulnerável quanto o "admin" pré-patch). Mostra
             * no próprio CLI — quem pode rodar o comando já tem acesso USB. */
            char newPlain[9];
            _storageMgr.generateInitialAdminPassword(newPlain, sizeof(newPlain));
            String preHash = _storageMgr.sha256Hex(String(newPlain));
            String hashed = _storageMgr.hashPassword("admin", preHash);
            safeCopy(cfg.users[0].password, hashed.c_str(), sizeof(cfg.users[0].password));
            /* Nota: safeCopy já null-termina; removido `password[31]='\0'` antigo
             * que assumia buffer[32] e truncaria hashes de 32 hex (v1). */
            cfg.users[0].mustChangePassword = true;
            const bool pt = _cmdMgr.isPt();
            _cmdMgr.printInfo(pt ? "Senha admin resetada. Nova senha (unica vez):"
                                 : "Admin password reset. New password (shown once):");
            _cmdMgr.printInfo(String("  ") + newPlain);
            _cmdMgr.printInfo(pt ? "Trocar no 1o login via web (forcado)."
                                 : "Change on 1st web login (forced).");
            /* Zera plaintext local após log; storage mantém seu próprio buffer RAM
             * também atualizado para que o banner do Serial/isFactoryDefaults
             * reflita a senha atual. */
            // Atualiza o buffer interno do storage (se getter exposto, usa):
            // Como não há setter público, cria via loadDefaults seria destrutivo.
            // Alternativa: expor setter, ou deixar que o próximo boot mostre nada.
            // Decisão: só mostrar no CLI aqui e não persistir em RAM. Admin que
            // rodou o comando pode anotar; se perdeu, roda de novo (é idempotente).
            volatile char* v = newPlain;
            for (size_t i = 0; i < sizeof(newPlain); i++) v[i] = 0;
            changed = true;
            break;
        }

        case CMD_RESET_TOUCH_CAL: {
            if (!cmd.confirmed) {
                const bool pt = _cmdMgr.isPt();
                _cmdMgr.printInfo(pt ? "ATENCAO: reseta calibracao do touch."
                                     : "WARN: resets touch calibration.");
                _cmdMgr.printInfo(pt ? "Use 'conf system touch reset confirm'."
                                     : "Run 'conf system touch reset confirm'.");
                break;
            }
            /* Limpa calibração do touch na config (invalida magic) */
            TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
            memset(cal, 0, sizeof(TouchCalData));
            _displayMgr.resetTouchCalibration();
            _cmdMgr.printInfo(_cmdMgr.isPt()
                ? "Calibracao do touch resetada p/ default."
                : "Touch calibration reset to factory defaults.");
            changed = true;
            break;
        }

        case CMD_FACTORY_RESET: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.confirmed) {
                _cmdMgr.printInfo(pt ? "ATENCAO: factory reset APAGA TODA config + reboot."
                                     : "WARN: factory reset WIPES ALL config + reboots.");
                _cmdMgr.printInfo(pt ? "Use 'conf system factory confirm'."
                                     : "Run 'conf system factory confirm'.");
                break;
            }
            LOG_CODE(LOG_WARN, "SYS", SYS_REBOOT_USER, 0, TRL("Factory reset", "Factory reset"));
            _storageMgr.resetToFactory();
            delay(100);
            LogManager::instance().markCleanReboot();
            rp2040.reboot();
        }

        case CMD_SET_NTP_ENABLED: {
            const bool pt = _cmdMgr.isPt();
            bool en = (cmd.intVal1 != 0);
            _storageMgr.setNtpEnabled(en);
            _cmdMgr.printSuccess(en ? (pt ? "NTP: habilitado" : "NTP: enabled")
                                    : (pt ? "NTP: desabilitado" : "NTP: disabled"));
            changed = true;
            break;
        }

        case CMD_SET_DNS_CFG: {
            const bool pt = _cmdMgr.isPt();
            if (cmd.intVal1 == 0) {  /* auto */
                _storageMgr.setDnsAuto(true);
                _cmdMgr.printSuccess(pt ? "DNS: automatico (DHCP)" : "DNS: auto (DHCP)");
                changed = true;
            } else {  /* manual */
                if (!isValidIpv4(cmd.strVal1)) {
                    _cmdMgr.printError(pt ? "IPv4 invalido para DNS primario"
                                          : "Invalid IPv4 for primary DNS");
                    break;
                }
                /* Secundário opcional: "" aceito (limpa o secundário). */
                if (cmd.strVal2[0] != '\0' && !isValidIpv4(cmd.strVal2)) {
                    _cmdMgr.printError(pt ? "IPv4 invalido para DNS secundario"
                                          : "Invalid IPv4 for secondary DNS");
                    break;
                }
                _storageMgr.setDnsAuto(false);
                safeCopy(cfg.staticDns, cmd.strVal1, sizeof(cfg.staticDns));
                _storageMgr.setSecondaryDns(cmd.strVal2);
                if (cmd.strVal2[0] != '\0') {
                    _cmdMgr.printSuccess(String(pt ? "DNS: manual; dns1=" : "DNS: manual; dns1=")
                                         + cmd.strVal1 + ", dns2=" + cmd.strVal2);
                } else {
                    _cmdMgr.printSuccess(String(pt ? "DNS: manual; dns1=" : "DNS: manual; dns1=")
                                         + cmd.strVal1);
                }
                changed = true;
            }
            break;
        }

        case CMD_SET_TIME: {
            const bool pt = _cmdMgr.isPt();
            int y, mo, d, h, mi, s;
            if (sscanf(cmd.strVal1, "%4d-%2d-%2d", &y, &mo, &d) != 3
                || sscanf(cmd.strVal2, "%2d:%2d:%2d", &h, &mi, &s) != 3) {
                _cmdMgr.printError(pt ? "Formato invalido. Use: conf time AAAA-MM-DD HH:MM:SS"
                                      : "Invalid format. Use: conf time YYYY-MM-DD HH:MM:SS");
                break;
            }
            if (y < 2026 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31
                || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) {
                _cmdMgr.printError(pt ? "Valores fora de range (ano >= 2026)"
                                      : "Values out of range (year >= 2026)");
                break;
            }
            struct tm tmLocal = {0};
            tmLocal.tm_year = y - 1900;
            tmLocal.tm_mon  = mo - 1;
            tmLocal.tm_mday = d;
            tmLocal.tm_hour = h;
            tmLocal.tm_min  = mi;
            tmLocal.tm_sec  = s;
            /* mktime() usa TZ env var (setado em applyTimezone no boot) para
             * converter local time → epoch UTC. */
            time_t epoch = mktime(&tmLocal);
            if (epoch <= 1600000000) {
                _cmdMgr.printError(pt ? "Falha na conversao de tempo"
                                      : "Time conversion failed");
                break;
            }
            _netMgr.setManualTime(epoch);
            _cmdMgr.printSuccess(pt ? "Hora aplicada (imediato, nao persiste em reboot)"
                                    : "Time applied (immediate; not persisted across reboot)");
            /* Sem changed=true: ação imediata, não vai pro flash. */
            break;
        }

        case CMD_DEFINE_SENSOR: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para GPIO"
                                      : "Invalid number for GPIO");
                break;
            }
            if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                _cmdMgr.printError(pt ? "Slot fora de range (0-9)"
                                      : "Slot out of range (0-9)");
                break;
            }
            SensorRecord &r = cfg.sensors[cmd.intVal1];
            r.active = true;
            r.gpio = cmd.intVal1;
            memcpy(r.rom, cmd.rom, 8);
            safeCopy(r.hwId, cmd.strVal1, sizeof(r.hwId));
            safeCopy(r.friendlyName, cmd.strVal2, sizeof(r.friendlyName));
            _cmdMgr.printSuccess(pt ? "Sensor mapeado em RAM."
                                    : "Sensor mapped in RAM.");
            break;
        }

        /* ===================================================================
         * TEST-ONLY — REMOVE BEFORE PRODUCTION
         * Zera `provisionEpoch` do(s) sensor(es) para recuperar visualização
         * do histórico pré factory reset. Uso:
         *   conf sensor <N> history all
         *   conf sensor all history all
         * Para remover: excluir este case + enum CMD_DBG_SENSOR_HISTORY_ALL
         * em SystemDefs.h + parser em CommandManager.cpp.
         * ================================================================= */
        case CMD_DBG_SENSOR_HISTORY_ALL: {
            const bool pt = _cmdMgr.isPt();
            if (cmd.intVal1 == -1) {
                /* Todos os slots ativos. */
                int n = 0;
                for (int i = 0; i < MAX_SENSORS; i++) {
                    if (cfg.sensors[i].active) {
                        cfg.sensors[i].provisionEpoch = 0;
                        n++;
                    }
                }
                cfg.ambientSensor.provisionEpoch = 0;
                changed = true;
                _cmdMgr.printSuccess((pt ? "provisionEpoch zerado em "
                                         : "provisionEpoch zeroed for ") +
                                     String(n) + (pt ? " slots + ambient. Use 'write memory'."
                                                     : " slots + ambient. Run 'write memory'."));
            } else {
                if (!cmd.intVal1Valid || cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                    _cmdMgr.printError(pt ? "Slot fora de range (0-9) ou 'all'"
                                          : "Slot out of range (0-9) or 'all'");
                    break;
                }
                cfg.sensors[cmd.intVal1].provisionEpoch = 0;
                changed = true;
                _cmdMgr.printSuccess((pt ? "provisionEpoch zerado no Slot "
                                         : "provisionEpoch zeroed for Slot ") +
                                     String(cmd.intVal1) +
                                     (pt ? ". Use 'write memory' e reload."
                                         : ". Run 'write memory' and reload."));
            }
            break;
        }

        case CMD_WIPE_SENSOR: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.confirmed) {
                _cmdMgr.printInfo(pt ? "ATENCAO: reseta historico do sensor."
                                     : "WARN: resets sensor history epoch.");
                _cmdMgr.printInfo(pt ? "Use 'sensor wipe <gpio> confirm'."
                                     : "Run 'sensor wipe <gpio> confirm'.");
                break;
            }
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para GPIO"
                                      : "Invalid number for GPIO");
                break;
            }
            if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                _cmdMgr.printError(pt ? "Slot fora de range (0-9)"
                                      : "Slot out of range (0-9)");
                break;
            }
            cfg.sensors[cmd.intVal1].provisionEpoch = _netMgr.getEpoch();
            changed = true;
            _cmdMgr.printSuccess((pt ? "Historico resetado no Slot "
                                     : "Sensor history wiped for Slot ") + String(cmd.intVal1));
            break;
        }

        case CMD_ACCEPT_SENSOR: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para GPIO"
                                      : "Invalid number for GPIO");
                break;
            }
            if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                _cmdMgr.printError(pt ? "Slot fora de range (0-9)"
                                      : "Slot out of range (0-9)");
                break;
            }
            uint8_t gpio = (uint8_t)cmd.intVal1;
            if (gpio < MAX_SENSORS) {
                uint8_t foundRom[8];
                if (_sensorMgr.identifyPhysicalSensor(gpio, foundRom)) {
                    if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) {
                        _cmdMgr.printError((pt ? "Sensor invalido no GPIO "
                                               : "Invalid physical sensor on GPIO ") + String(gpio));
                    } else {
                        String dbId; float dbOffset = 0.0f; String dbName;
                        _storageMgr.getCalibrationData(foundRom, dbId, dbOffset, dbName);

                        String currentId = String(cfg.sensors[gpio].hwId);

                        cfg.sensors[gpio].active = true;
                        cfg.sensors[gpio].gpio = gpio;
                        memcpy(cfg.sensors[gpio].rom, foundRom, 8);

                        if (dbId.length() > 0) { safeCopy(cfg.sensors[gpio].hwId, dbId.c_str(), sizeof(cfg.sensors[gpio].hwId)); }
                        else { safeCopy(cfg.sensors[gpio].hwId, "LIB_SENS", sizeof(cfg.sensors[gpio].hwId)); }

                        if (dbName.length() > 0) { safeCopy(cfg.sensors[gpio].friendlyName, dbName.c_str(), sizeof(cfg.sensors[gpio].friendlyName)); }
                        else { safeCopy(cfg.sensors[gpio].friendlyName, pt ? "Sensor Reconhecido" : "Recognized Sensor",
                                        sizeof(cfg.sensors[gpio].friendlyName)); }
                        cfg.sensors[gpio].friendlyName[31] = '\0';

                        if (currentId != String(cfg.sensors[gpio].hwId)) {
                            cfg.sensors[gpio].provisionEpoch = _netMgr.getEpoch();
                            _cmdMgr.printInfo(pt ? "Novo hardware detectado. Epoch atualizado."
                                                 : "New Hardware Context Detected. Epoch updated.");
                        }

                        /* F-LOCKOUT-STUCK: wrappa save+reload no mesmo quiet mode (idem CMD_WRITE_MEMORY). */
                        _displayMgr.requestQuietMode();   /* default 15s timeout */
                        _storageMgr.saveConfiguration();
                        loadAndCalibrateSensors();
                        _displayMgr.releaseQuietMode();
                        _cmdMgr.printSuccess((pt ? "Sensor aceito e vinculado ao Slot "
                                                 : "Sensor accepted and bound to Slot ") + String(gpio));
                    }
                } else {
                    _cmdMgr.printError((pt ? "Nenhum sensor no GPIO "
                                           : "No physical sensor detected on GPIO ") + String(gpio));
                }
            }
            break;
        }

        case CMD_SCAN_SENSORS:
            if (!_sensorMgr.isScanning()) { _sensorMgr.startScan(); _waitingScan = true; }
            break;

        case CMD_WRITE_MEMORY: {
            /* F-LOCKOUT-STUCK: wrappa save + reload de sensores no mesmo
             * quiet mode (re-entrant). loadAndCalibrateSensors emite
             * APP_SENSORS_CALIBRATED via LOG_CODE → LogManager.requestFsLock
             * que, fora de quiet mode, caía em lockout IRQ-based e stuck. */
            _displayMgr.requestQuietMode();   /* default 15s timeout */
            bool saved = _storageMgr.saveConfiguration();
            if (saved) {
                loadAndCalibrateSensors();
            }
            _displayMgr.releaseQuietMode();
            if (saved) {
                _cmdMgr.printSuccess(_cmdMgr.isPt()
                    ? "Config salva no Flash!"
                    : "Config saved to Flash!");
            }
            break;
        }

        case CMD_CLEAR_LOGS:
            if (!cmd.confirmed) {
                const bool pt = _cmdMgr.isPt();
                _cmdMgr.printInfo(pt ? "ATENCAO: apaga todos os logs."
                                     : "WARN: deletes all system logs.");
                _cmdMgr.printInfo(pt ? "Use 'clear log confirm' para prosseguir."
                                     : "Run 'clear log confirm' to proceed.");
                break;
            }
            _storageMgr.enterFlashSafeMode();
            LittleFS.remove("/system.log"); LittleFS.remove("/system.old");
            _storageMgr.exitFlashSafeMode();
            LogManager::instance().begin(true, LOG_DEBUG);
            _cmdMgr.printSuccess(_cmdMgr.isPt() ? "Logs apagados." : "Logs cleared.");
            break;

        case CMD_RELOAD:
            if (!cmd.confirmed) {
                const bool pt = _cmdMgr.isPt();
                _cmdMgr.printInfo(pt ? "ATENCAO: vai reiniciar o dispositivo."
                                     : "WARN: will reboot the device.");
                _cmdMgr.printInfo(pt ? "Use 'reload confirm' para prosseguir."
                                     : "Run 'reload confirm' to proceed.");
                break;
            }
            LOG_CODE(LOG_WARN, "SYS", SYS_REBOOT_USER, 0, TRL("Reboot via CLI", "Reboot via CLI"));
            delay(100);     /* Garante flush do log para flash */
            LogManager::instance().markCleanReboot();
            rp2040.reboot();
            break;
        case CMD_TEL_SYNC:
            /* Silencioso por design: usuario ve o log natural
             * "Telemetria enviada: ..." quando ha dados para enviar. */
            _telemetryMgr.forceSync();
            break;

        case CMD_TEL_DUMP:
            _telemetryMgr.armPayloadDump();
            _telemetryMgr.forceSync();
            /* Se tinha dados, _dumpPayloadNext foi consumido (dump ja saiu).
             * Se nao tinha, a flag esta armada e dispara no proximo sync. */
            if (_telemetryMgr.isPayloadDumpArmed()) {
                _cmdMgr.printSuccess(_cmdMgr.isPt()
                    ? "Sem dados pendentes; dump armado para o proximo sync."
                    : "No pending data; dump armed for next sync.");
            }
            break;

        case CMD_DEBUG: {
            CliConfigData* cli = reinterpret_cast<CliConfigData*>(
                cfg.reserved + CLI_CONFIG_OFFSET);
            const bool pt = _cmdMgr.isPt();
            if (cmd.intVal1 == 1 || cmd.intVal1 == 0) {
                bool on = (cmd.intVal1 == 1);
                cli->magic = CLI_CONFIG_MAGIC;
                cli->debugMode = on ? 1 : 0;
                LogManager::instance().setConsoleStream(on);
                _cmdMgr.setDebugMode(on);
                _cmdMgr.printSuccess(on ? (pt ? "Debug: LIGADO" : "Debug: ON")
                                        : (pt ? "Debug: DESLIGADO" : "Debug: OFF"));
                changed = true;
            } else {
                _cmdMgr.printInfo(_cmdMgr.isDebugMode()
                    ? (pt ? "Debug: LIGADO" : "Debug: ON")
                    : (pt ? "Debug: DESLIGADO" : "Debug: OFF"));
            }
            break;
        }

        case CMD_LANGUAGE: {
            if (cmd.intVal1 == LANG_PT || cmd.intVal1 == LANG_EN) {
                cfg.displayLang = (uint8_t)cmd.intVal1;
                _displayMgr.setLanguage(cfg.displayLang);
                _cmdMgr.setCliLang(cfg.displayLang);
                LogManager::instance().setLanguage(cfg.displayLang);
                LOG_CODE(LOG_INFO, "APP", APP_UI_LANG_CHANGED, cmd.intVal1, "");
                _cmdMgr.printSuccess(cmd.intVal1 == LANG_PT
                    ? "Idioma: Portugues (BR)"
                    : "Language: English");
                changed = true;
            } else {
                _cmdMgr.printInfo(_cmdMgr.isPt()
                    ? "Idioma atual: Portugues (BR)"
                    : "Current language: English");
            }
            break;
        }

        case CMD_IP_CFG: {
            const bool pt = _cmdMgr.isPt();
            switch (cmd.intVal1) {
                case 0:  /* dhcp */
                    cfg.useDhcp = true;
                    _cmdMgr.printSuccess(pt ? "Modo IP: DHCP" : "IP mode: DHCP");
                    changed = true;
                    break;
                case 1:  /* static */
                    cfg.useDhcp = false;
                    _cmdMgr.printSuccess(pt ? "Modo IP: estatico" : "IP mode: static");
                    changed = true;
                    break;
                case 2: case 3: case 4: case 5: {
                    if (!isValidIpv4(cmd.strVal1)) {
                        _cmdMgr.printError(pt ? "IPv4 invalido (ex: 192.168.1.100)"
                                              : "Invalid IPv4 (e.g. 192.168.1.100)");
                        break;
                    }
                    char* dst = nullptr; size_t dstSize = 0;
                    const char* label = "";
                    if (cmd.intVal1 == 2) { dst = cfg.staticIp;      dstSize = sizeof(cfg.staticIp);      label = "addr"; }
                    else if (cmd.intVal1 == 3) { dst = cfg.staticMask;  dstSize = sizeof(cfg.staticMask);  label = "mask"; }
                    else if (cmd.intVal1 == 4) { dst = cfg.staticGateway; dstSize = sizeof(cfg.staticGateway); label = "gateway"; }
                    else                       { dst = cfg.staticDns;     dstSize = sizeof(cfg.staticDns);     label = "dns"; }
                    safeCopy(dst, cmd.strVal1, dstSize);
                    _cmdMgr.printSuccess((pt ? "IP " : "IP ") + String(label) + ": " + cmd.strVal1);
                    changed = true;
                    break;
                }
                default:
                    _cmdMgr.printError(pt ? "Subcomando IP invalido" : "Invalid IP subcommand");
                    break;
            }
            break;
        }

        case CMD_SENSOR_FIELD: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para GPIO" : "Invalid number for GPIO");
                break;
            }
            if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                _cmdMgr.printError(pt ? "Slot fora de range (0-9)" : "Slot out of range (0-9)");
                break;
            }
            if (cmd.strVal2[0] == '\0') {
                _cmdMgr.printError(pt ? "Valor ausente" : "Missing value");
                break;
            }
            SensorRecord &r = cfg.sensors[cmd.intVal1];
            const char* field = cmd.strVal1;   /* CON-005b: strVal1 agora char[] */
            if (strcmp(field, "alarm") == 0) {
                String v = cmd.strVal2; v.toLowerCase();
                if (v != "on" && v != "off") {
                    _cmdMgr.printError(pt ? "Use 'on' ou 'off'" : "Use 'on' or 'off'");
                    break;
                }
                r.alarmsActive = (v == "on");
                _cmdMgr.printSuccess((pt ? "Alarme slot " : "Alarm slot ") + String(cmd.intVal1) + ": " + v);
                changed = true;
            } else {
                /* Valores numéricos (float) com range sensato para temperaturas/umidade. */
                float val = atof(cmd.strVal2);
                /* toFloat retorna 0.0 para input inválido — distinguir "0.0" legítimo. */
                if (val == 0.0f && strcmp(cmd.strVal2, "0") != 0 && strcmp(cmd.strVal2, "0.0") != 0
                                 && strcmp(cmd.strVal2, "-0") != 0 && strcmp(cmd.strVal2, "-0.0") != 0) {
                    _cmdMgr.printError(pt ? "Valor numerico invalido" : "Invalid numeric value");
                    break;
                }
                if (field == "tmin" || field == "tmax") {
                    if (val < -50.0f || val > 150.0f) {
                        _cmdMgr.printError(pt ? "Temp fora de range (-50 a 150)"
                                              : "Temp out of range (-50 to 150)");
                        break;
                    }
                    if (field == "tmin") r.tempMin = val; else r.tempMax = val;
                } else if (field == "hmin" || field == "hmax") {
                    if (val < 0.0f || val > 100.0f) {
                        _cmdMgr.printError(pt ? "Umid fora de range (0-100)"
                                              : "Hum out of range (0-100)");
                        break;
                    }
                    if (field == "hmin") r.humMin = val; else r.humMax = val;
                } else {
                    _cmdMgr.printError(pt ? "Campo desconhecido" : "Unknown field");
                    break;
                }
                _cmdMgr.printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1)
                                      + " " + field + "=" + cmd.strVal2);
                changed = true;
            }
            break;
        }

        case CMD_USER_ADD: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidName(cmd.strVal1, 31)) {
                _cmdMgr.printError(pt ? "Username invalido (1-31, sem ctrl chars)"
                                      : "Invalid username (1-31, no ctrl chars)");
                break;
            }
            if (cmd.strVal2[0] == '\0' || strlen(cmd.strVal2) > 64) {
                _cmdMgr.printError(pt ? "Senha ausente ou muito longa (1-64)"
                                      : "Password missing or too long (1-64)");
                break;
            }
            if (!isValidCfgString(cmd.strVal2, 64)) {
                _cmdMgr.printError(pt ? "Senha tem chars de controle"
                                      : "Password has control chars");
                break;
            }
            /* Nome não pode colidir com usuário existente. */
            bool exists = false;
            int freeSlot = -1;
            for (int i = 0; i < MAX_USERS; i++) {
                if (cfg.users[i].active) {
                    if (strcasecmp(cmd.strVal1, cfg.users[i].username) == 0) {
                        exists = true; break;
                    }
                } else if (freeSlot < 0 && i >= 1) {  /* slot 0 = admin, protegido */
                    freeSlot = i;
                }
            }
            if (exists) {
                _cmdMgr.printError(pt ? "Usuario ja existe" : "User already exists");
                break;
            }
            if (freeSlot < 0) {
                _cmdMgr.printError(pt ? "Sem slot livre (max usuarios)"
                                      : "No free slot (max users)");
                break;
            }
            safeCopy(cfg.users[freeSlot].username, cmd.strVal1, sizeof(cfg.users[freeSlot].username));
            {
                /* CON-005b: sha256Hex/hashPassword aceitam String; wraps temporários. */
                String preHash = _storageMgr.sha256Hex(String(cmd.strVal2));
                String hashed = _storageMgr.hashPassword(String(cmd.strVal1), preHash);
                safeCopy(cfg.users[freeSlot].password, hashed.c_str(), sizeof(cfg.users[freeSlot].password));
            }
            cfg.users[freeSlot].active = true;
            cfg.users[freeSlot].permissions = (PERM_DASHBOARD | PERM_HISTORY);
            cfg.users[freeSlot].mustChangePassword = false;
            _cmdMgr.printSuccess(String(pt ? "Usuario criado: " : "User created: ") + cmd.strVal1);
            LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, freeSlot,
                     String(TRL("CLI created user: ", "CLI criou usuario: ")) + cmd.strVal1);
            changed = true;
            break;
        }

        case CMD_USER_DEL: {
            const bool pt = _cmdMgr.isPt();
            if (strcasecmp(cmd.strVal1, "admin") == 0) {
                _cmdMgr.printError(pt ? "Nao e permitido deletar 'admin'"
                                      : "Cannot delete 'admin'");
                break;
            }
            bool found = false;
            for (int i = 1; i < MAX_USERS; i++) {
                if (cfg.users[i].active && strcasecmp(cmd.strVal1, cfg.users[i].username) == 0) {
                    cfg.users[i].active = false;
                    memset(cfg.users[i].password, 0, sizeof(cfg.users[i].password));
                    _cmdMgr.printSuccess(String(pt ? "Usuario removido: " : "User deleted: ") + cmd.strVal1);
                    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, i,
                             String(TRL("CLI deleted user: ", "CLI apagou usuario: ")) + cmd.strVal1);
                    changed = true;
                    found = true;
                    break;
                }
            }
            if (!found) _cmdMgr.printError(pt ? "Usuario nao encontrado" : "User not found");
            break;
        }

        case CMD_USER_PASS: {
            const bool pt = _cmdMgr.isPt();
            if (cmd.strVal2[0] == '\0' || strlen(cmd.strVal2) > 64
                || !isValidCfgString(cmd.strVal2, 64)) {
                _cmdMgr.printError(pt ? "Nova senha invalida (1-64, sem ctrl chars)"
                                      : "Invalid new password (1-64, no ctrl chars)");
                break;
            }
            bool found = false;
            for (int i = 0; i < MAX_USERS; i++) {
                if (cfg.users[i].active && strcasecmp(cmd.strVal1, cfg.users[i].username) == 0) {
                    /* CON-005b: sha256Hex aceita String; wrap temporário. */
                    String preHash = _storageMgr.sha256Hex(String(cmd.strVal2));
                    String hashed = _storageMgr.hashPassword(String(cfg.users[i].username), preHash);
                    safeCopy(cfg.users[i].password, hashed.c_str(), sizeof(cfg.users[i].password));
                    cfg.users[i].mustChangePassword = false;
                    _cmdMgr.printSuccess(String(pt ? "Senha atualizada: " : "Password updated: ") + cmd.strVal1);
                    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, i,
                             String(TRL("CLI reset password: ", "CLI resetou senha: ")) + cmd.strVal1);
                    changed = true;
                    found = true;
                    break;
                }
            }
            if (!found) _cmdMgr.printError(pt ? "Usuario nao encontrado" : "User not found");
            break;
        }

        case CMD_SET_WEB_PORT: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid || cmd.intVal1 < 1 || cmd.intVal1 > 65535) {
                _cmdMgr.printError(pt ? "Porta invalida (1..65535)"
                                      : "Invalid port (1..65535)");
                break;
            }
            WebConfigData* w = reinterpret_cast<WebConfigData*>(
                cfg.reserved + WEB_CONFIG_OFFSET);
            w->port = (uint16_t)cmd.intVal1;
            char buf[64];
            snprintf(buf, sizeof(buf),
                pt ? "Porta web: %d (aplica apos reload)"
                   : "Web port: %d (applies after reload)",
                cmd.intVal1);
            _cmdMgr.printSuccess(buf);
            changed = true;
            break;
        }

        case CMD_UNKNOWN:
        default:
            LOG_CODE(LOG_WARN, "CLI", CLI_UNKNOWN_CMD, 0, "");
            _cmdMgr.printError(_cmdMgr.isPt()
                ? "Comando desconhecido. Digite 'help'."
                : "Unknown command. Type 'help'.");
            break;
    }

    if (changed) _cmdMgr.printInfo(_cmdMgr.isPt()
        ? "RAM OK. Use 'write memory' para salvar."
        : "RAM updated. Run 'write memory' to persist.");
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
    static uint32_t _yieldEntryTime = 0;

    /* Safety: reseta guard se preso há >10s (crash parcial) */
    if (_inYield && timeSince(_yieldEntryTime, 10000)) {
        _inYield = false;
        _isRenderingGraph = false;
        LOG_CODE(LOG_WARN, "APP", APP_YIELD_STUCK, 0, TRL("Yield stuck >10s, force reset.", "Yield preso >10s, reset forcado."));
    }

    if (_inYield) return;
    _inYield = true;
    _yieldEntryTime = millis();

    /*
     * WdtWindow context-aware: core0Yield pode ser chamado de dentro de
     * web handlers (via _lightYieldCb). Processa UI events que podem
     * disparar graph preloads (5x renderGraphOptimized, cada um 6s budget).
     * Sem janela aqui, cumulativo cabe só em 15s default — insuficiente
     * para rajadas. 30s cobre sessão típica.
     */
    LogManager::WdtWindow _wdtYield(30000);

    /*
     * Prioridade máxima: processa som de toque ANTES de qualquer
     * outro processamento. Reduz latência do bip de ~50ms para ~5ms.
     */
    if (_displayMgr.consumeTouchSound()) {
        _soundMgr.play(SND_TOUCH_CLICK);
        _soundMgr.update();  /* Executa imediatamente, sem esperar fim do yield */
    }
    if (_displayMgr.consumeErrorSound()) {
        _soundMgr.play(SND_ERROR);
        _soundMgr.update();
    }

    UiEvent uiEv;
    if (!_isRenderingGraph) {
        while (_displayMgr.getUiEvent(uiEv)) {
            if (uiEv.type == UiEvent::EVT_SLOT_SELECT) { _currentSensorIdx = uiEv.id; refreshSelectedSlot(); }
            else if (uiEv.type == UiEvent::EVT_OPEN_GRAPH) {
                if (uiEv.param == 99) openStatsScreen(uiEv.id);
                else {
                    int sensorId = uiEv.id;
                    int range    = uiEv.param;

                    /*
                     * Preserva a âncora se já estamos navegando no passado.
                     * Zoom muda o intervalo mas mantém o ponto final fixo.
                     * Ex: dia 2 em 24H → zoom para 12H → mostra 12:00-23:59 do dia 2.
                     *
                     * Reseta apenas se abrindo um gráfico novo do dashboard
                     * (sensor diferente ou sem âncora prévia).
                     */
                    bool hasAnchor = (_graphAnchorEnd != 0);

                    if (!hasAnchor) {
                        _graphNavOffset = 0;
                        _displayMgr.setGraphNavOffset(0);
                    }

                    /* Sensor diferente do cacheado: invalida cache de ranges */
                    if (_sensorCacheId != sensorId) {
                        for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;
                        _sensorCacheId = sensorId;
                        /* Sensor novo = reset âncora */
                        _graphAnchorEnd = 0;
                        hasAnchor = false;
                        _graphNavOffset = 0;
                        _displayMgr.setGraphNavOffset(0);
                    }

                    _lastGraphRange = range;

                    if (hasAnchor) {
                        /*
                         * Zoom com âncora ativa: usa forceEndEpoch para manter
                         * o fim da janela no mesmo ponto. Não usa cache.
                         */
                        for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;

                        _isRenderingGraph = true;
                        renderGraphOptimized(sensorId, range, true, 0, _graphAnchorEnd);
                        _isRenderingGraph = false;
                    }
                    else if (_sensorCache[range].valid) {
                        /*
                         * Cache hit (sem âncora = visualização "agora").
                         * Verifica staleness para 7D.
                         */
                        bool stale = false;
                        if (range == 4) {
                            time_t age = time(nullptr) - _sensorCache[4].lastRefresh;
                            if (age > 1800) stale = true;
                        }

                        if (!stale) {
                            _displayMgr.showGraphPlot(
                                _sensorCache[range].pkg,
                                _sensorCache[range].humMin,
                                _sensorCache[range].humMax);
                        } else {
                            appendToGraphCache(_sensorCache[4], sensorId);
                            _displayMgr.showGraphPlot(
                                _sensorCache[4].pkg,
                                _sensorCache[4].humMin,
                                _sensorCache[4].humMax);
                        }
                    } else {
                        /*
                         * Cache miss: carrega do flash.
                         */
                        if (range == 4 && !_graphCache[graphCacheIdx(sensorId)].valid) {
                            _displayMgr.requestLoadingScreen();
                            uint32_t waitStart = millis();
                            while (!_displayMgr.isLoadingDrawn() && (millis() - waitStart < 500)) {
                                watchdog_update();
                                TRACE_BEAT(0);
                                delay(5);
                            }
                        }

                        _isRenderingGraph = true;
                        renderGraphOptimized(sensorId, range, true, 0);
                        _isRenderingGraph = false;

                        /*
                         * Pré-carrega ranges restantes apenas sem âncora.
                         */
                        if (_graphNavOffset == 0) {
                            _isRenderingGraph = true;
                            preloadSensorRanges(sensorId, range);
                            _isRenderingGraph = false;
                        }
                    }
                }
            }
            /* ── Navegação temporal do gráfico (setas ◀▶) ── */
            else if (uiEv.type == UiEvent::EVT_GRAPH_NAV) {
                static const time_t rangeDur[] = { 3600, 21600, 43200, 86400, 604800 };
                time_t step = (_lastGraphRange >= 0 && _lastGraphRange <= 4)
                              ? rangeDur[_lastGraphRange] : 86400;

                /*
                 * Se não há âncora (gráfico aberto sem calendário),
                 * ancora no now arredondado para o fim do step atual.
                 */
                if (_graphAnchorEnd == 0) {
                    _graphAnchorEnd = time(nullptr);
                }

                /* Desloca a âncora por exatamente 1 step */
                _graphAnchorEnd += (time_t)uiEv.param * step;

                /* Não permite ver o futuro */
                time_t now = time(nullptr);
                if (_graphAnchorEnd > now) _graphAnchorEnd = now;

                /* Offset derivado da posição: negativo = passado (▶ habilitado) */
                _graphNavOffset = (_graphAnchorEnd < now) ? -1 : 0;
                _displayMgr.setGraphNavOffset(_graphNavOffset);

                /* Invalida cache de ranges (dados com offset são únicos) */
                for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;

                _isRenderingGraph = true;
                renderGraphOptimized(uiEv.id, _lastGraphRange, true, 0, _graphAnchorEnd);
                _isRenderingGraph = false;
            }
            /* ── Abertura do calendário ── */
            else if (uiEv.type == UiEvent::EVT_OPEN_CALENDAR) {
                time_t now = _netMgr.getEpoch();
                struct tm nowTm;
                localtime_r(&now, &nowTm);
                int year  = nowTm.tm_year + 1900;
                int month = nowTm.tm_mon + 1;

                uint32_t mask = _storageMgr.getHistoryDaysMask(year, month);
                _displayMgr.showCalendar(year, month, mask);
            }
            /* ── Seleção de dia no calendário ── */
            else if (uiEv.type == UiEvent::EVT_CALENDAR_DAY) {
                int sensorId = uiEv.id;
                int dayNum   = uiEv.param;

                /* Meia-noite do dia selecionado */
                struct tm selTm = {};
                selTm.tm_year = _displayMgr.getCalYear() - 1900;
                selTm.tm_mon  = _displayMgr.getCalMonth() - 1;
                selTm.tm_mday = dayNum;
                time_t selMidnight = mktime(&selTm);

                /*
                 * Âncora = meia-noite do dia seguinte.
                 * Janela 24H será exatamente [00:00, 23:59] do dia selecionado.
                 * Navegação ◀▶ desloca a partir desta âncora, não de now.
                 */
                _graphAnchorEnd = selMidnight + 86400;

                /*
                 * Offset para controle do botão ▶:
                 * negativo = estamos no passado (▶ habilitado),
                 * zero = presente (▶ desabilitado).
                 */
                time_t now = time(nullptr);
                _graphNavOffset = (_graphAnchorEnd < now) ? -1 : 0;
                _displayMgr.setGraphNavOffset(_graphNavOffset);

                /* Invalida cache e carrega gráfico 24H com janela fixa */
                for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;
                _sensorCacheId = sensorId;
                _lastGraphRange = RANGE_24H;

                _isRenderingGraph = true;
                renderGraphOptimized(sensorId, RANGE_24H, true, 0, _graphAnchorEnd);
                _isRenderingGraph = false;
            }
            /* ── Mudança de mês no calendário ── */
            else if (uiEv.type == UiEvent::EVT_CALENDAR_MONTH) {
                int newMonth = _displayMgr.getCalMonth() + uiEv.param;
                int newYear  = _displayMgr.getCalYear();

                if (newMonth < 1)  { newMonth = 12; newYear--; }
                if (newMonth > 12) { newMonth = 1;  newYear++; }

                uint32_t mask = _storageMgr.getHistoryDaysMask(newYear, newMonth);
                _displayMgr.showCalendar(newYear, newMonth, mask);
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
                    LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_DEACTIVATED, 0, "");
                } else if (_storageMgr.mustChangePin()) {
                    /* SEC-004/F12.4: PIN ainda é o default factory "1234";
                     * força a tela de troca antes de permitir menu principal. */
                    _displayMgr.showSettingsPassword();
                    LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, 0,
                             TRL("Default PIN detected; forcing change.",
                                 "PIN padrao detectado; forcando troca."));
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
                else if (uiEv.id == 7) {
                    _displayMgr.showSystemStatus();
                }
                else if (uiEv.id == 8) {
                    _displayMgr.showSettingsDisplayOffset();
                }
            }
            else if (uiEv.type == UiEvent::EVT_APPLY_THEME) {
                SystemConfig &cfg = _storageMgr.getConfig();
                cfg.themeIndex = uiEv.id;
                loadTheme(cfg.themeIndex);
                _storageMgr.saveConfiguration();
                _displayMgr.refreshTheme();
                _soundMgr.play(SND_CONFIRM);
                LOG_CODE(LOG_INFO, "APP", APP_UI_THEME_CHANGED, 0, "");
            }
            else if (uiEv.type == UiEvent::EVT_APPLY_LANG) {
                SystemConfig &cfg = _storageMgr.getConfig();
                cfg.displayLang = uiEv.id;
                _displayMgr.setLanguage(cfg.displayLang);
                _storageMgr.saveConfiguration();
                _soundMgr.play(SND_CONFIRM);
                _displayMgr.forceDashboard();
                LOG_CODE(LOG_INFO, "APP", APP_UI_LANG_CHANGED, 0, "");
            }
            else if (uiEv.type == UiEvent::EVT_SAVE_ALARMS) {
                _storageMgr.saveConfiguration();

                _sensorMgr.syncAlarmLimits(_storageMgr.getConfig());

                checkAlarmConditions();
                _soundMgr.play(SND_CONFIRM);
                _displayMgr.showSettingsAlarms(&_storageMgr.getConfig());
                LOG_CODE(LOG_INFO, "APP", APP_UI_ALARM_SAVED, 0, "");
            }

            else if (uiEv.type == UiEvent::EVT_APPLY_TOUCH_CAL) {
                SystemConfig &cfg = _storageMgr.getConfig();
                TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
                _displayMgr.fillCalData(cal);
                _storageMgr.saveConfiguration();
                _soundMgr.play(SND_CONFIRM);
                LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_CAL_SAVED, 0, "");
            }

            else if (uiEv.type == UiEvent::EVT_SAVE_TOUCH_CAL) {
                /* Salva threshold de sensibilidade calibrado */
                SystemConfig &cfg = _storageMgr.getConfig();
                TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
                _displayMgr.fillCalData(cal);
                _storageMgr.saveConfiguration();
                _soundMgr.play(SND_CONFIRM);
                LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_SENS_SAVED, 0, "");
            }

            else if (uiEv.type == UiEvent::EVT_APPLY_DISPLAY_OFFSET) {
                /*
                 * Aplica o novo offset ao TFT (já foi aplicado pela tela em preview,
                 * mas os valores vêm do evento para garantir consistência), persiste
                 * em reserved[] e reinicia a calibração do touch: o mapeamento
                 * raw→pixel depende diretamente da posição da imagem no LCD, então
                 * não faz sentido manter a calibração antiga após o deslocamento.
                 */
                SystemConfig &cfg = _storageMgr.getConfig();

                int8_t ox = (int8_t)uiEv.id;
                int8_t oy = (int8_t)uiEv.param;
                DisplayOffsetData* ofs = reinterpret_cast<DisplayOffsetData*>(
                    cfg.reserved + sizeof(TouchCalData) + sizeof(SoundConfigData));
                ofs->magic    = 0xD0;
                ofs->offsetX  = ox;
                ofs->offsetY  = oy;
                ofs->reserved = 0;

                /* Invalida calibração de touch: magic=0 força recalibração; o restante
                 * do bloco pode permanecer como lixo — o magic é o único critério de
                 * validade em loadTouchCalibration(). */
                TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
                cal->magic = 0;

                _storageMgr.saveConfiguration();
                _displayMgr.resetTouchCalibration();
                _soundMgr.play(SND_CONFIRM);
                LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_CAL_SAVED, 0,
                         "Display offset applied; touch calibration reset.");

                /* Abre imediatamente a calibração do touch para o usuário remapear
                 * o toque com a nova posição da imagem do LCD. */
                _displayMgr.showTouchCalibration();
            }

            else if (uiEv.type == UiEvent::EVT_SAVE_PASSWORD) {
                SystemConfig &cfg = _storageMgr.getConfig();
                char newPwd[9];
                _displayMgr.getNewPassword(newPwd, sizeof(newPwd));
                if (strlen(newPwd) >= 4 && strlen(newPwd) <= 7) {
                    /* SEC-004/F12.4: só limpa mustChangePin se o usuário
                     * escolheu PIN != default "1234". Se setou "1234" de novo,
                     * mantém flag ativa — não resolve nada trocar para o mesmo.
                     * Note: "1234" ainda é aceito como valor; a política só
                     * impede que conte como "troca real". */
                    safeCopy(cfg.displayPin, newPwd, sizeof(cfg.displayPin));
                    cfg.displayPin[7] = '\0';
                    if (strcmp(newPwd, "1234") != 0) {
                        _storageMgr.clearMustChangePin();
                    }
                    _storageMgr.saveConfiguration();
                    _soundMgr.play(SND_CONFIRM);
                    LOG_CODE(LOG_INFO, "APP", APP_UI_PIN_CHANGED, 0, "");
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
                LOG_CODE(LOG_INFO, "APP", APP_UI_SOUND_SAVED, 0, "");
            }


            else if (uiEv.type == UiEvent::EVT_ALARM_SILENCE) {
                uint32_t silenceSec = (uiEv.param > 0) ? uiEv.param : 120;
                _soundMgr.stopAlarm();
                _displayMgr.setAlarmSilenced(true, millis() + (silenceSec * 1000));
                _displayMgr.forceDashboard();
                LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_SILENCED, 120, "");
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


    if (_bootCompletedAt > 0 && timeSince(_bootCompletedAt, 5000)) {


        if (_displayMgr.isAlarmSilenced()) {
            uint32_t silEnd = _displayMgr.getAlarmSilenceEnd();
            if (silEnd > 0 && millis() >= silEnd) {
                _displayMgr.setAlarmSilenced(false, 0);
                LOG_CODE(LOG_INFO, "APP", APP_UI_ALARM_SILENCE_EXP, 0, "");
            }
        }

        checkAlarmConditions();
    }


    _soundMgr.update();

    _inYield = false;
}

void AppManager::pauseDisplayForFlash(bool lock) {
    /* F-LOCKOUT-STUCK: durante quiet mode cooperativo, Core 1 está congelado
     * em loop RAM-only com IRQs OFF. Tentar multicore_lockout IRQ-based aqui
     * trava para sempre (IRQ nunca é handled) até WDT matar Core 0.
     * Lockout é desnecessário neste cenário porque Core 1 já não toca flash.
     * Early-return torna requestFsLock (LogManager) e enterFlashSafeMode
     * (StorageManager) no-ops quando já estamos dentro do quiet mode. */
    if (_displayMgr.isInQuietMode()) return;
    _displayMgr.pauseRendering(lock);
}

bool AppManager::requestDisplayQuietMode(bool enable) {
    if (enable) return _displayMgr.requestQuietMode();   /* default 15s */
    _displayMgr.releaseQuietMode();
    return true;
}

void AppManager::refreshSelectedSlot() {
    SystemConfig &cfg = _storageMgr.getConfig();
    const auto& sensors = _sensorMgr.getRuntimeSensors();
    bool found = false;

    if (_currentSensorIdx < 10) {
        if (cfg.sensors[_currentSensorIdx].active) {
            uint8_t targetGpio = cfg.sensors[_currentSensorIdx].gpio;
            for (const auto &s : sensors) {
                if (s.config.gpio != 10 && s.config.gpio == targetGpio) {
                    _displayMgr.setSlotData(s.avgValue1, !s.inErrorState, _currentSensorIdx, String(s.config.friendlyName));
                    found = true; break;
                }
            }
        }
    } else if (_currentSensorIdx == 10) {
        _displayMgr.setSlotData(analogReadTemp(), true, 10, "Board (Internal)"); found = true;
    }

    if (!found) _displayMgr.setSlotData(NAN, false, _currentSensorIdx, "Empty / Inactive");
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
        if (timeSince(lastPendingRefresh, 10000)) {
            _telemetryMgr.refreshPendingCount();
            lastPendingRefresh = millis();
        }
        _displayMgr.setTelemetryPending(_telemetryMgr.getPendingEstimate());

        /* Min/max do dia (preload CSV + leituras acumuladas em tempo real) */
        float ambMinT = (_cachedMin[10] < 999.0f)  ? _cachedMin[10] : NAN;
        float ambMaxT = (_cachedMax[10] > -999.0f)  ? _cachedMax[10] : NAN;
        float ambMinH = (_cachedHumMin  < 999.0f)   ? _cachedHumMin  : NAN;
        float ambMaxH = (_cachedHumMax  > -999.0f)   ? _cachedHumMax  : NAN;
        _displayMgr.setAmbientMinMax(ambMinT, ambMaxT, ambMinH, ambMaxH);

        /* Min/max do slot ativo */
        int slotIdx = _currentSensorIdx;
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
            else if (_currentSensorIdx < 10 && cfg.sensors[_currentSensorIdx].active && cfg.sensors[_currentSensorIdx].gpio == s.config.gpio) {
                _displayMgr.setSlotData(s.avgValue1, !s.inErrorState, _currentSensorIdx, String(s.config.friendlyName));
            }
        }

        if (_currentSensorIdx == 10) _displayMgr.setSlotData(analogReadTemp(), true, 10, "Board (Internal)");
    }
}

/**
 * @brief Pre-load daily Min/Max values from binary history for fast display.
 * Runs during boot to avoid flash I/O competition with the dashboard.
 * Uses ReadLock (no Core 1 pause) with 5-second budget limit.
 */
void AppManager::preloadMinMax() {
    time_t now = _netMgr.getEpoch();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char path[40];
    snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

    File f;
    _storageMgr.enterFlashReadLock();
    bool fileExists = LittleFS.exists(path);
    if (fileExists) f = LittleFS.open(path, "r");
    _storageMgr.exitFlashReadLock();

    if (fileExists && f) {
        uint32_t _preloadBudget = millis();
        bool hasMore = true;

        while (hasMore) {
            if (timeSince(_preloadBudget, 5000)) {
                LOG_CODE(LOG_WARN, "APP", APP_PRELOAD_BUDGET, 0, "");
                _storageMgr.enterFlashReadLock();
                f.close();
                _storageMgr.exitFlashReadLock();
                LOG_CODE(LOG_INFO, "APP", APP_CACHE_MINMAX_PARTIAL, 0, "");
                return;
            }

            /* Lê batch de 20 registros binários */
            _storageMgr.enterFlashReadLock();
            BinaryHistoryRecord batch[20];
            int count = 0;
            while (f.available() >= HISTORY_RECORD_SIZE && count < 20) {
                if (f.read((uint8_t*)&batch[count], HISTORY_RECORD_SIZE)
                    == HISTORY_RECORD_SIZE)
                {
                    count++;
                }
            }
            hasMore = (f.available() >= HISTORY_RECORD_SIZE);
            _storageMgr.exitFlashReadLock();

            /* Processa batch fora do lock */
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

            watchdog_update();
            TRACE_BEAT(0);
            delay(2);
        }

        _storageMgr.enterFlashReadLock();
        f.close();
        _storageMgr.exitFlashReadLock();
    }

    /* Salvar snapshot do preload (somente dados do CSV, sem leitura em tempo real) */
    for (int i = 0; i < MINMAX_SLOT_COUNT; i++) {
        _preloadMin[i] = _cachedMin[i];
        _preloadMax[i] = _cachedMax[i];
    }
    _preloadHumMin = _cachedHumMin;
    _preloadHumMax = _cachedHumMax;

    LOG_CODE(LOG_INFO, "APP", APP_CACHE_MINMAX_FULL, 0, "");
}

void AppManager::processHistoryLogging() {
    _lastHistoryTime = millis();
    time_t now = _netMgr.getEpoch();

    if (now > 1600000000) {
        const auto& sensors = _sensorMgr.getRuntimeSensors();
        SystemConfig &cfg = _storageMgr.getConfig();

        /* ── Monta registro binário ── */
        BinaryHistoryRecord rec;
        rec.clear();
        rec.epoch = (uint32_t)now;

        /* Sensor ambiente (DHT22 no GPIO 10) */
        float ambT = NAN, ambH = NAN;
        for (const auto &s : sensors) {
            if (s.config.gpio == 10 && !s.inErrorState) {
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
        rec.ambientHum  = BinaryHistoryRecord::floatToI16(ambH);

        /* Sensores DS18B20 (slots 0..9) */
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (cfg.sensors[i].active) {
                for (const auto &s : sensors) {
                    if (s.config.gpio == cfg.sensors[i].gpio && !s.inErrorState) {
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

        if (_storageMgr.writeHistoryEntry(rec)) {
            LOG_CODE(LOG_INFO, "HIST", APP_HISTORY_SAVED, 0, "");
            _telemetryMgr.notifyNewRecord();
        }
    }

    /* #10: Log periódico de heap — só quando baixa ou 1x/hora (evita rotação prematura) */
    {
        uint32_t heapFree = rp2040.getFreeHeap();
        static uint32_t lastFullHeapLog = 0;

        if (heapFree < 32768 || timeSince(lastFullHeapLog, 3600000)) {
            char heapMsg[48];
            snprintf(heapMsg, sizeof(heapMsg), "Heap: %lu free / %lu total",
                     (unsigned long)heapFree,
                     (unsigned long)rp2040.getTotalHeap());
            LOG_CODE(LOG_INFO, "SYS", APP_HEAP_REPORT, (int)(heapFree/1024), heapMsg);
            lastFullHeapLog = millis();
        }

        /* Alerta quando heap cai abaixo de 16KB (margem para WiFi+TLS) */
        if (heapFree < 16384) {
            LOG_CODE(LOG_WARN, "SYS", SYS_HEAP_LOW, (int)(heapFree/1024), "");
        }
    }
}

void AppManager::openStatsScreen(int sensorId) {
    /**
     * IMPORTANTE: pkg deve ser static para evitar stack overflow.
     * GraphDataPackage tem ~3.2KB — excede a stack do RP2040 (~4KB).
     * Mesmo padrão usado em renderGraphOptimized().
     */
    static GraphDataPackage pkg;
    memset(&pkg, 0, sizeof(GraphDataPackage));
    pkg.sensorIdx = sensorId;
    pkg.timeRange = 3;
    pkg.count = 0;
    pkg.idxMinTemp = -1;
    pkg.idxMaxTemp = -1;
    pkg.avgTemp  = NAN;
    pkg.stdTemp  = NAN;
    pkg.deltaTemp = NAN;
    pkg.avgHum   = NAN;
    pkg.stdHum   = NAN;
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

    SystemConfig &cfg = _storageMgr.getConfig();
    pkg.hasHumidity = (sensorId == -1);

    if (sensorId == -1) {
        snprintf(pkg.title, sizeof(pkg.title), "%s", _displayMgr.tr(TR_AMBIENT));
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

    _displayMgr.showStats(pkg, humMin, humMax);
}

/* =========================================================================== */

/* ─────────────────────────────────────────────────────────────────────────── */
/*  readRecordValue() — helper para extrair valor de um registro binário      */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief  Extrai o valor de temperatura/umidade de um registro binário
 *         conforme o sensor selecionado.
 *
 * @param  rec        Registro binário.
 * @param  sensorId   -1 = ambiente, 0..9 = slot DS18B20.
 * @param  humOut     [out] Se sensorId == -1, recebe a umidade ambiente.
 * @return Temperatura como float, ou NAN se inválida.
 */
static inline float readRecordValue(const BinaryHistoryRecord& rec,
                                     int sensorId, float& humOut)
{
    humOut = NAN;

    if (sensorId == -1) {
        humOut = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);
        return BinaryHistoryRecord::i16ToFloat(rec.ambientTemp);
    }

    if (sensorId >= 0 && sensorId < MAX_SENSORS) {
        return BinaryHistoryRecord::i16ToFloat(rec.sensors[sensorId]);
    }

    return NAN;
}

/*                     GRAPH RENDERING FROM BINARY HISTORY                   */
/* =========================================================================== */
/**
 * @brief Load and render a temperature/humidity graph from binary history.
 *
 * Uses fixed-size records for exact seek (offset = recordIndex * 28).
 * Eliminates CSV parsing, seek fallback, and line realignment.
 * 6-second budget limit prevents watchdog timeout.
 */
void AppManager::renderGraphOptimized(int sensorId, int range, bool showAfterLoad, int navOffset, time_t forceEndEpoch) {
    if (!_storageMgr.lockHeavyTask()) {
        LOG_CODE(LOG_WARN, "APP", APP_FLASH_BUSY, 0, "");
        _displayMgr.forceDashboard();
        return;
    }
    /*
     * WdtWindow context-aware: renderGraph pode ser chamado de UI event
     * (main loop), de core0Yield (dentro de web handler), ou de
     * preloadSensorRanges (5x por 6s = até 30s). 30s cobre qualquer caso.
     * Aninhado dentro de telemetria (120s) ou web handler, mantém o outer.
     */
    LogManager::WdtWindow _wdt(30000);
    LOG_CODE(LOG_INFO, "APP", APP_GRAPH_LOADING, 0, "");

    uint32_t _graphBudgetStart = millis();
    const uint32_t GRAPH_BUDGET_MS = 6000;

    static GraphDataPackage pkg;
    memset(&pkg, 0, sizeof(GraphDataPackage));
    pkg.sensorIdx = sensorId;
    pkg.timeRange = range;
    pkg.count = 0;

    pkg.minVal = 1000.0f;
    pkg.maxVal = -1000.0f;
    pkg.idxMinTemp = -1;
    pkg.idxMaxTemp = -1;
    pkg.tsMaxHum = 0;
    pkg.tsMinHum = 0;
    float localHumMin = 1000.0f;
    float localHumMax = -1000.0f;

    pkg.hasHumidity = (sensorId == -1);

    SystemConfig &cfg = _storageMgr.getConfig();
    uint32_t epochLimit = 0;

    if (sensorId == -1) {
        snprintf(pkg.title, sizeof(pkg.title), "%s", _displayMgr.tr(TR_AMBIENT));
        snprintf(pkg.hwId, sizeof(pkg.hwId), "AMB");
        snprintf(pkg.rom, sizeof(pkg.rom), "INTERNAL-DHT");
    } else if (sensorId == 10) {
        snprintf(pkg.title, sizeof(pkg.title), "Board Temp");
        snprintf(pkg.hwId, sizeof(pkg.hwId), "SYS");
        snprintf(pkg.rom, sizeof(pkg.rom), "RP2040-ADC");
    } else {
        if (sensorId < 10 && cfg.sensors[sensorId].active) {
            safeCopy(pkg.title, cfg.sensors[sensorId].friendlyName, sizeof(pkg.title));
            safeCopy(pkg.hwId, cfg.sensors[sensorId].hwId, sizeof(pkg.hwId));
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

    /*
     * Tabela de duração e passo por range:
     *   1H  → 3600s     6H  → 21600s    12H → 43200s
     *   24H → 86400s    7D  → 604800s
     *
     * navOffset desloca a janela temporal em passos do range.
     * ex: range=24H, navOffset=-2 → mostra 2 dias atrás.
     */
    static const time_t rangeDuration[] = { 3600, 21600, 43200, 86400, 604800 };
    time_t step = (range >= 0 && range <= 4) ? rangeDuration[range] : 86400;
    time_t effectiveEnd;

    if (forceEndEpoch > 0) {
        /* Modo calendário: janela fixa meia-noite a meia-noite */
        effectiveEnd = forceEndEpoch;
    } else {
        effectiveEnd = now + (time_t)navOffset * step;
        if (effectiveEnd > now) effectiveEnd = now; /* Não permite ver o futuro */
    }

    if (range == 0) { cutoff = effectiveEnd - 3600;   decimation = 1;  }
    else if (range == 1) { cutoff = effectiveEnd - 21600;  decimation = 2;  }
    else if (range == 2) { cutoff = effectiveEnd - 43200;  decimation = 4;  }
    else if (range == 3) { cutoff = effectiveEnd - 86400;  decimation = 8;  }
    else if (range == 4) { cutoff = effectiveEnd - 604800; decimation = 51; daysToLoad = 7; }

    if (range <= 3) {
        struct tm todayTm;
        localtime_r(&effectiveEnd, &todayTm);
        todayTm.tm_hour = 0; todayTm.tm_min = 0; todayTm.tm_sec = 0;
        time_t todayMidnight = mktime(&todayTm);
        daysToLoad = (cutoff < todayMidnight) ? 2 : 1;
    }

    int lineIdx = decimation - 1;

    /*
     * Armazena a janela temporal no pacote para que o renderer
     * posicione os pontos proporcionalmente ao tempo (não ao índice).
     */
    pkg.tsCutoff = cutoff;
    pkg.tsEnd    = effectiveEnd;

    /* Pré-popula timestamps para header (mostra período mesmo sem dados) */
    pkg.tsFirst = cutoff;
    pkg.tsLast  = effectiveEnd;
    pkg.tsMid   = cutoff + (effectiveEnd - cutoff) / 2;

    /* Min/max reais: rastreados de TODOS os registros, não apenas decimados */
    pkg.realMinVal = 1000.0f;
    pkg.realMaxVal = -1000.0f;
    pkg.tsRealMin  = 0;
    pkg.tsRealMax  = 0;

    for (int d = daysToLoad - 1; d >= 0; d--) {
        if (pkg.count >= GRAPH_WIDTH) break;

        time_t targetDay = effectiveEnd - (d * 86400);
        struct tm timeinfo;
        localtime_r(&targetDay, &timeinfo);

        char path[40];
        snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

        File f;
        _storageMgr.enterFlashReadLock();
        bool fileExists = LittleFS.exists(path);
        if (fileExists) f = LittleFS.open(path, "r");
        _storageMgr.exitFlashReadLock();

        if (fileExists && f) {
            size_t fileSize = f.size();
            size_t totalRecords = fileSize / HISTORY_RECORD_SIZE;

            /* Seek otimizado para o cutoff */
            if (totalRecords > 50 && cutoff > 0) {
                struct tm fileTm = timeinfo;
                fileTm.tm_hour = 0; fileTm.tm_min = 0; fileTm.tm_sec = 0;
                time_t fileMidnight = mktime(&fileTm);

                /*
                 * Seek só quando o cutoff está DENTRO do dia deste arquivo.
                 * Se cutoff < fileMidnight, precisamos do arquivo inteiro
                 * (ex: 24H lendo arquivo de hoje, cutoff é ontem).
                 */
                if (cutoff > fileMidnight) {
                    /*
                     * Duas estratégias — usa a mais avançada:
                     *
                     * 1) Midnight-based: assume ~1 registro/minuto desde 00:00.
                     *    Preciso se o arquivo não tem lacunas.
                     *
                     * 2) End-based: recua N registros do fim do arquivo.
                     *    Robusto contra lacunas (reboots, boot loops).
                     */
                    int seekFromMidnight = max(0, (int)((cutoff - fileMidnight) / 60) - 10);

                    /*
                     * Registros BRUTOS necessários por range (pré-decimação).
                     * duração_em_minutos + margem de 20.
                     * 1H=80, 6H=380, 12H=740, 24H=1460, 7D=1460
                     * Para 24H/7D, seekFromEnd será 0 (arquivo inteiro).
                     */
                    static const int maxRecordsNeeded[] = { 80, 380, 740, 1460, 1460 };
                    int needed = (range >= 0 && range <= 4) ? maxRecordsNeeded[range] : 200;
                    int seekFromEnd = max(0, (int)totalRecords - needed);

                    /*
                     * Usa o MENOR dos dois (mais conservador = mais longe do fim).
                     * Se o arquivo tem lacunas, midnight-based pode overshoot.
                     * min() garante que nunca pulamos dados válidos.
                     */
                    int seekRecord;
                    if (seekFromMidnight < (int)totalRecords) {
                        seekRecord = min(seekFromMidnight, seekFromEnd);
                    } else {
                        seekRecord = seekFromEnd;
                    }

                    if (seekRecord > 0 && seekRecord < (int)totalRecords) {
                        _storageMgr.enterFlashReadLock();
                        f.seek((size_t)seekRecord * HISTORY_RECORD_SIZE);
                        _storageMgr.exitFlashReadLock();
                    }
                }
                /* Se cutoff <= fileMidnight: sem seek, lê o arquivo inteiro */
            }

            bool hasMore = true;
            bool budgetExceeded = false;

            while (hasMore && pkg.count < GRAPH_WIDTH && !budgetExceeded) {
                if (timeSince(_graphBudgetStart, GRAPH_BUDGET_MS)) {
                    LOG_CODE(LOG_WARN, "APP", APP_GRAPH_BUDGET, 0, "");
                    budgetExceeded = true;
                    break;
                }

                _storageMgr.enterFlashReadLock();
                BinaryHistoryRecord batch[20];
                int batchCount = 0;
                while (f.available() >= HISTORY_RECORD_SIZE
                       && batchCount < 20
                       && pkg.count < GRAPH_WIDTH)
                {
                    if (f.read((uint8_t*)&batch[batchCount], HISTORY_RECORD_SIZE)
                        == HISTORY_RECORD_SIZE)
                    {
                        batchCount++;
                    }
                }
                hasMore = (f.available() >= HISTORY_RECORD_SIZE);
                _storageMgr.exitFlashReadLock();

                bool pastWindow = false;

                for (int bi = 0; bi < batchCount && pkg.count < GRAPH_WIDTH; bi++) {
                    const BinaryHistoryRecord& rec = batch[bi];

                    time_t ts = (time_t)rec.epoch;
                    if (ts < cutoff) continue;

                    /*
                     * Registros são cronológicos: se este ultrapassou effectiveEnd,
                     * todos os seguintes também ultrapassarão. Break imediato
                     * em vez de continue evita ler o resto do arquivo inutilmente.
                     * Crítico para 1H: sem isso, lê ~1380 registros a mais num
                     * arquivo de 1440 → estoura budget de 6s.
                     */
                    if (ts > effectiveEnd) { pastWindow = true; break; }

                    float humRead = NAN;
                    float valRead = readRecordValue(rec, sensorId, humRead);
                    if (ts < epochLimit) valRead = NAN;

                    /*
                     * Min/max REAIS: rastreados de CADA registro na janela,
                     * independente da decimação. Garante que o eixo Y e os
                     * badges mostrem os valores extremos verdadeiros.
                     */
                    if (!isnan(valRead)) {
                        if (valRead < pkg.realMinVal) { pkg.realMinVal = valRead; pkg.tsRealMin = ts; }
                        if (valRead > pkg.realMaxVal) { pkg.realMaxVal = valRead; pkg.tsRealMax = ts; }
                    }

                    /* Decimação: pula registros intermediários para caber na tela */
                    lineIdx++;
                    if (lineIdx % decimation != 0) continue;

                    /*
                     * SEMPRE adiciona o ponto ao array, mesmo se NAN.
                     * Pontos NAN preservam a posição temporal no eixo X,
                     * criando buracos visíveis no gráfico onde o sensor
                     * estava em erro. O renderer pula segmentos com NAN.
                     */
                    pkg.pointsV1[pkg.count] = valRead;
                    pkg.tsPoints[pkg.count] = (uint32_t)ts;

                    if (pkg.hasHumidity) {
                        pkg.pointsV2[pkg.count] = humRead;
                    }

                    if (pkg.count == 0) pkg.tsFirst = ts;

                    /* Estatísticas dos pontos exibidos (para marcadores no gráfico) */
                    if (!isnan(valRead)) {
                        if (valRead < pkg.minVal) {
                            pkg.minVal = valRead;
                            pkg.idxMinTemp = pkg.count;
                            pkg.tsMinTemp = ts;
                        }
                        if (valRead > pkg.maxVal) {
                            pkg.maxVal = valRead;
                            pkg.idxMaxTemp = pkg.count;
                            pkg.tsMaxTemp = ts;
                        }
                    }

                    if (pkg.hasHumidity && !isnan(humRead)) {
                        if (humRead < localHumMin) {
                            localHumMin = humRead;
                            pkg.tsMinHum = ts;
                        }
                        if (humRead > localHumMax) {
                            localHumMax = humRead;
                            pkg.tsMaxHum = ts;
                        }
                    }

                    pkg.tsLast = ts;
                    pkg.count++;
                }

                /* Saiu da janela temporal: interrompe leitura deste arquivo */
                if (pastWindow) break;

                watchdog_update();
                TRACE_BEAT(0);
                yield();
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

        watchdog_update(); TRACE_BEAT(0);
        yield();
    }

    if (pkg.count > 0) {
        pkg.tsMid = pkg.tsFirst + (pkg.tsLast - pkg.tsFirst) / 2;

        {
            float sumT = 0.0f;
            float sumH = 0.0f;
            int   tempCount = 0;
            int   humCount = 0;

            for (int i = 0; i < pkg.count; i++) {
                if (!isnan(pkg.pointsV1[i])) {
                    sumT += pkg.pointsV1[i];
                    tempCount++;
                }
                if (pkg.hasHumidity && !isnan(pkg.pointsV2[i])) {
                    sumH += pkg.pointsV2[i];
                    humCount++;
                }
            }
            pkg.avgTemp = (tempCount > 0) ? (sumT / (float)tempCount) : NAN;
            pkg.avgHum  = (humCount > 0) ? (sumH / (float)humCount) : NAN;

            float sqSumT = 0.0f;
            float sqSumH = 0.0f;
            for (int i = 0; i < pkg.count; i++) {
                if (!isnan(pkg.pointsV1[i]) && !isnan(pkg.avgTemp)) {
                    float diffT = pkg.pointsV1[i] - pkg.avgTemp;
                    sqSumT += diffT * diffT;
                }
                if (pkg.hasHumidity && !isnan(pkg.pointsV2[i]) && !isnan(pkg.avgHum)) {
                    float diffH = pkg.pointsV2[i] - pkg.avgHum;
                    sqSumH += diffH * diffH;
                }
            }
            pkg.stdTemp = (tempCount > 1) ? sqrtf(sqSumT / (float)(tempCount - 1)) : 0.0f;
            pkg.stdHum  = (humCount > 2) ? sqrtf(sqSumH / (float)(humCount - 1)) : NAN;

            /* Delta: busca primeiro e último valores VÁLIDOS */
            float firstValid = NAN, lastValid = NAN;
            float firstValidH = NAN, lastValidH = NAN;
            for (int i = 0; i < pkg.count; i++) {
                if (!isnan(pkg.pointsV1[i])) {
                    if (isnan(firstValid)) firstValid = pkg.pointsV1[i];
                    lastValid = pkg.pointsV1[i];
                }
                if (pkg.hasHumidity && !isnan(pkg.pointsV2[i])) {
                    if (isnan(firstValidH)) firstValidH = pkg.pointsV2[i];
                    lastValidH = pkg.pointsV2[i];
                }
            }
            pkg.deltaTemp = (!isnan(firstValid) && !isnan(lastValid)) ? (lastValid - firstValid) : NAN;
            pkg.deltaHum  = (!isnan(firstValidH) && !isnan(lastValidH)) ? (lastValidH - firstValidH) : NAN;
        }

        if (pkg.hasHumidity && localHumMax > -1000.0f) {
            if (localHumMax > 100.0f) localHumMax = 100.0f;
            if (localHumMin < 0.0f) localHumMin = 0.0f;
        } else {
            localHumMin = 0.0f;
            localHumMax = 100.0f;
        }
    } else {
        pkg.minVal = 0.0f;
        pkg.maxVal = 40.0f;
        pkg.realMinVal = 0.0f;
        pkg.realMaxVal = 40.0f;
        pkg.avgTemp = NAN;
        pkg.stdTemp = NAN;
        pkg.deltaTemp = NAN;
        pkg.avgHum = NAN;
        pkg.stdHum = NAN;
        pkg.deltaHum = NAN;
        localHumMin = 0.0f;
        localHumMax = 100.0f;

        /*
         * Mesmo sem dados, preenche tsFirst/tsLast com a janela temporal
         * solicitada para que o header exiba o período de referência.
         */
        pkg.tsFirst = cutoff;
        pkg.tsLast  = effectiveEnd;
        pkg.tsMid   = cutoff + (effectiveEnd - cutoff) / 2;
    }

    /*
     * Modo calendário (forceEndEpoch > 0): o header e eixo X devem
     * sempre mostrar o período COMPLETO do dia selecionado (00:00–23:59),
     * independente de onde os dados reais começam/terminam.
     * Ajusta tsLast para 23:59 (effectiveEnd - 60s) para evitar que
     * o display mostre "08/04 00:00" (meia-noite do dia seguinte).
     */
    if (forceEndEpoch > 0) {
        pkg.tsFirst = cutoff;                                /* 00:00 do dia */
        pkg.tsLast  = forceEndEpoch - 60;                   /* 23:59 do dia */
        pkg.tsMid   = cutoff + (forceEndEpoch - cutoff) / 2; /* ~12:00      */
    }

    /* ── Salva no cache 7d de background se aplicável ── */
    if (range == 4 && pkg.count > 0) {
        int ci = graphCacheIdx(sensorId);
        _graphCache[ci].pkg         = pkg;
        _graphCache[ci].humMin      = localHumMin;
        _graphCache[ci].humMax      = localHumMax;
        _graphCache[ci].lastRefresh = time(nullptr);
        _graphCache[ci].valid       = true;
    }

    /* ── Salva no cache do sensor ativo (todos os ranges) ── */
    if (sensorId == _sensorCacheId && range >= 0 && range < 5) {
        _sensorCache[range].pkg         = pkg;
        _sensorCache[range].humMin      = localHumMin;
        _sensorCache[range].humMax      = localHumMax;
        _sensorCache[range].lastRefresh = time(nullptr);
        _sensorCache[range].valid       = true;
    }

    _storageMgr.unlockHeavyTask();

    if (showAfterLoad) {
        _displayMgr.showGraphPlot(pkg, localHumMin, localHumMax);
    }
}

/* =========================================================================== */
/*                      GRAPH CACHE — PRE-LOADING SYSTEM                     */
/* =========================================================================== */

/**
 * @brief Converte sensorId para índice no array _graphCache[].
 *
 * Mapeamento:
 *   sensorId -1      → slot 11 (ambient/DHT)
 *   sensorId 0..9    → slot 0..9 (DS18B20)
 *   sensorId 10      → slot 10 (board temp / RP2040 ADC)
 */
int AppManager::graphCacheIdx(int sensorId) {
    if (sensorId == -1) return MAX_SENSORS + 1;  /* 11 = ambient */
    if (sensorId == 10) return MAX_SENSORS;       /* 10 = board   */
    if (sensorId >= 0 && sensorId < MAX_SENSORS) return sensorId;
    return 0;
}

/**
 * @brief Pré-carrega o cache 7d de todos os sensores ativos.
 *
 * Chamado durante o boot e pode ser re-chamado periodicamente.
 * Cada sensor usa lockHeavyTask (exclusão mútua com o web server),
 * liberando entre sensores para não bloquear watchdog e WiFi.
 */
void AppManager::preloadGraphCaches() {
    SystemConfig &cfg = _storageMgr.getConfig();

    /* Ambient (sempre presente) */
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_GRAPH_AMBIENT, 0, "");
    renderGraphOptimized(-1, 4, false);
    watchdog_update(); TRACE_BEAT(0);

    /* Board temp */
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_GRAPH_BOARD, 0, "");
    renderGraphOptimized(10, 4, false);
    watchdog_update(); TRACE_BEAT(0);

    /* DS18B20 ativos */
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].active) {
            char _logBuf[40];
            snprintf(_logBuf, sizeof(_logBuf), "Graph cache: loading sensor %d...", i);
            LOG_CODE(LOG_INFO, "APP", APP_GRAPH_LOADING, 0, String(_logBuf));
            renderGraphOptimized(i, 4, false);
            watchdog_update(); TRACE_BEAT(0);
        }
    }

    _lastGraphCacheRefresh = millis();
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_PRELOAD_DONE, 0, "");
}

/**
 * @brief Pré-carrega todos os ranges restantes do sensor ativo.
 *
 * Chamado imediatamente após exibir o gráfico solicitado pelo usuário.
 * Para o range 7D, copia do cache de background se disponível (zero I/O).
 * Para os demais, usa renderGraphOptimized com seek otimizado (~10-150ms cada).
 *
 * @param sensorId  ID do sensor (-1 = ambient, 0-9 = DS18B20, 10 = board).
 * @param skipRange Range que já foi carregado e exibido (não recarregar).
 */
void AppManager::preloadSensorRanges(int sensorId, int skipRange) {
    for (int r = 0; r < 5; r++) {
        if (r == skipRange || _sensorCache[r].valid) continue;

        /* 7D: usa cache de background com atualização incremental */
        if (r == 4) {
            int ci = graphCacheIdx(sensorId);
            if (_graphCache[ci].valid) {
                _sensorCache[4] = _graphCache[ci];
                /* Se stale, faz append incremental (sem loading screen) */
                time_t age = time(nullptr) - _sensorCache[4].lastRefresh;
                if (age >= 1800) {
                    appendToGraphCache(_sensorCache[4], sensorId);
                }
                continue;
            }
        }

        /* Carrega do flash com seek otimizado */
        renderGraphOptimized(sensorId, r, false);
        watchdog_update(); TRACE_BEAT(0);
    }
}


/**
 * @brief Atualiza incrementalmente o cache 7D de um sensor.
 *
 * Em vez de recarregar todos os 7 dias do flash (~2-4s + loading screen),
 * lê APENAS os registros novos desde entry.pkg.tsLast e os anexa ao array
 * existente, removendo pontos antigos que saíram da janela de 7 dias.
 *
 * Fluxo:
 * 1. Calcula quantos pontos novos existem desde tsLast (com decimation=51)
 * 2. Remove pontos antigos que ultrapassaram a janela de 7 dias
 * 3. Lê novos registros do CSV (seek direto para tsLast)
 * 4. Anexa ao array e recalcula estatísticas
 *
 * @return true se o cache foi atualizado, false se não há dados novos.
 */
bool AppManager::appendToGraphCache(GraphCacheEntry& entry, int sensorId) {
    GraphDataPackage& pkg = entry.pkg;
    time_t now = time(nullptr);

    if (now - pkg.tsLast < 120) return true;

    time_t cutoff = now - 604800;
    const int decimation = 51;

    /* ── Fase 1: Remover pontos antigos que saíram da janela ── */
    if (pkg.count >= 2 && pkg.tsFirst < cutoff) {
        float dtPerPoint = (float)(pkg.tsLast - pkg.tsFirst) / (float)(pkg.count - 1);
        if (dtPerPoint < 1.0f) dtPerPoint = 51.0f * 60.0f;

        int discard = (int)((float)(cutoff - pkg.tsFirst) / dtPerPoint);
        if (discard < 0) discard = 0;
        if (discard > pkg.count) discard = pkg.count;

        if (discard > 0) {
            int remaining = pkg.count - discard;
            if (remaining > 0) {
                memmove(pkg.pointsV1, pkg.pointsV1 + discard, remaining * sizeof(float));
                memmove(pkg.tsPoints, pkg.tsPoints + discard, remaining * sizeof(uint32_t));
                if (pkg.hasHumidity) {
                    memmove(pkg.pointsV2, pkg.pointsV2 + discard, remaining * sizeof(float));
                }
            }
            pkg.count = remaining;
            pkg.tsFirst = pkg.tsFirst + (time_t)(discard * dtPerPoint);
        }
    }

    /* ── Fase 2: Ler novos registros do arquivo binário ── */
    if (!_storageMgr.lockHeavyTask()) return false;

    SystemConfig& cfg = _storageMgr.getConfig();
    uint32_t epochLimit = 0;
    if (sensorId >= 0 && sensorId < MAX_SENSORS && cfg.sensors[sensorId].active) {
        epochLimit = cfg.sensors[sensorId].provisionEpoch;
    }

    struct tm todayTm;
    localtime_r(&now, &todayTm);
    todayTm.tm_hour = 0; todayTm.tm_min = 0; todayTm.tm_sec = 0;
    time_t todayMidnight = mktime(&todayTm);
    int daysToLoad = (pkg.tsLast < todayMidnight) ? 2 : 1;

    int lineIdx = decimation - 1;
    int newPoints = 0;
    float localHumMin = entry.humMin;
    float localHumMax = entry.humMax;

    for (int d = daysToLoad - 1; d >= 0; d--) {
        if (pkg.count >= GRAPH_WIDTH) break;

        time_t targetDay = now - (d * 86400);
        struct tm ti;
        localtime_r(&targetDay, &ti);

        char path[40];
        snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);

        _storageMgr.enterFlashReadLock();
        bool exists = LittleFS.exists(path);
        File f;
        if (exists) f = LittleFS.open(path, "r");
        _storageMgr.exitFlashReadLock();

        if (!exists || !f) continue;

        /* Seek exato para tsLast */
        size_t fileSize = f.size();
        size_t totalRecords = fileSize / HISTORY_RECORD_SIZE;
        if (totalRecords > 20 && pkg.tsLast > 0) {
            struct tm fileTm = ti;
            fileTm.tm_hour = 0; fileTm.tm_min = 0; fileTm.tm_sec = 0;
            time_t fileMidnight = mktime(&fileTm);

            if (pkg.tsLast > fileMidnight) {
                int minPast = (int)((pkg.tsLast - fileMidnight) / 60);
                int seekRecord = max(0, minPast - 5);
                if (seekRecord < (int)totalRecords) {
                    _storageMgr.enterFlashReadLock();
                    f.seek((size_t)seekRecord * HISTORY_RECORD_SIZE);
                    _storageMgr.exitFlashReadLock();
                }
            }
        }

        bool hasMore = true;
        while (hasMore && pkg.count < GRAPH_WIDTH) {
            _storageMgr.enterFlashReadLock();
            BinaryHistoryRecord batch[20];
            int batchCount = 0;
            while (f.available() >= HISTORY_RECORD_SIZE
                   && batchCount < 20
                   && pkg.count < GRAPH_WIDTH)
            {
                if (f.read((uint8_t*)&batch[batchCount], HISTORY_RECORD_SIZE)
                    == HISTORY_RECORD_SIZE)
                {
                    batchCount++;
                }
            }
            hasMore = (f.available() >= HISTORY_RECORD_SIZE);
            _storageMgr.exitFlashReadLock();

            for (int bi = 0; bi < batchCount && pkg.count < GRAPH_WIDTH; bi++) {
                const BinaryHistoryRecord& rec = batch[bi];

                time_t ts = (time_t)rec.epoch;
                if (ts <= pkg.tsLast) continue;

                float humRead = NAN;
                float valRead = readRecordValue(rec, sensorId, humRead);
                if (ts < epochLimit) valRead = NAN;

                /* Real min/max de todos os registros (pré-decimação) */
                if (!isnan(valRead)) {
                    if (valRead < pkg.realMinVal) { pkg.realMinVal = valRead; pkg.tsRealMin = ts; }
                    if (valRead > pkg.realMaxVal) { pkg.realMaxVal = valRead; pkg.tsRealMax = ts; }
                }

                lineIdx++;
                if (lineIdx % decimation != 0) continue;

                /* Inclui NAN para preservar buracos no gráfico */
                pkg.pointsV1[pkg.count] = valRead;
                pkg.tsPoints[pkg.count] = (uint32_t)ts;
                if (pkg.hasHumidity) {
                    pkg.pointsV2[pkg.count] = humRead;
                    if (!isnan(humRead)) {
                        if (humRead < localHumMin) localHumMin = humRead;
                        if (humRead > localHumMax) localHumMax = humRead;
                    }
                }
                pkg.tsLast = ts;
                pkg.count++;
                newPoints++;
            }

            watchdog_update(); TRACE_BEAT(0); yield();
        }

        _storageMgr.enterFlashReadLock();
        f.close();
        _storageMgr.exitFlashReadLock();
    }

    _storageMgr.unlockHeavyTask();

    /* ── Fase 3: Recalcular estatísticas (ignorando NANs) ── */
    if (newPoints > 0 && pkg.count >= 2) {
        pkg.tsMid = pkg.tsFirst + (pkg.tsLast - pkg.tsFirst) / 2;

        pkg.minVal = 1000.0f; pkg.maxVal = -1000.0f;
        pkg.idxMinTemp = -1;  pkg.idxMaxTemp = -1;
        float sumT = 0, sqSumT = 0;
        float sumH = 0, sqSumH = 0;
        int tempCount = 0;
        int humCount = 0;

        for (int i = 0; i < pkg.count; i++) {
            float v = pkg.pointsV1[i];
            if (!isnan(v)) {
                sumT += v;
                tempCount++;
                if (v < pkg.minVal) { pkg.minVal = v; pkg.idxMinTemp = i; }
                if (v > pkg.maxVal) { pkg.maxVal = v; pkg.idxMaxTemp = i; }
            }
            if (pkg.hasHumidity && !isnan(pkg.pointsV2[i])) {
                sumH += pkg.pointsV2[i]; humCount++;
            }
        }
        pkg.avgTemp = (tempCount > 0) ? (sumT / (float)tempCount) : NAN;
        pkg.avgHum  = (humCount > 0) ? (sumH / (float)humCount) : NAN;

        for (int i = 0; i < pkg.count; i++) {
            if (!isnan(pkg.pointsV1[i]) && !isnan(pkg.avgTemp)) {
                float dT = pkg.pointsV1[i] - pkg.avgTemp;
                sqSumT += dT * dT;
            }
            if (pkg.hasHumidity && !isnan(pkg.pointsV2[i]) && !isnan(pkg.avgHum)) {
                float dH = pkg.pointsV2[i] - pkg.avgHum;
                sqSumH += dH * dH;
            }
        }
        pkg.stdTemp = (tempCount > 1) ? sqrtf(sqSumT / (float)(tempCount - 1)) : 0.0f;
        pkg.stdHum  = (humCount > 2) ? sqrtf(sqSumH / (float)(humCount - 1)) : NAN;

        /* Delta: primeiro e último valores VÁLIDOS */
        float firstValid = NAN, lastValid = NAN;
        float firstValidH = NAN, lastValidH = NAN;
        for (int i = 0; i < pkg.count; i++) {
            if (!isnan(pkg.pointsV1[i])) {
                if (isnan(firstValid)) firstValid = pkg.pointsV1[i];
                lastValid = pkg.pointsV1[i];
            }
            if (pkg.hasHumidity && !isnan(pkg.pointsV2[i])) {
                if (isnan(firstValidH)) firstValidH = pkg.pointsV2[i];
                lastValidH = pkg.pointsV2[i];
            }
        }
        pkg.deltaTemp = (!isnan(firstValid) && !isnan(lastValid)) ? (lastValid - firstValid) : NAN;
        pkg.deltaHum  = (!isnan(firstValidH) && !isnan(lastValidH)) ? (lastValidH - firstValidH) : NAN;

        /* Timestamps de extremos: usa tsPoints real em vez de interpolação linear */
        if (pkg.idxMinTemp >= 0) pkg.tsMinTemp = (time_t)pkg.tsPoints[pkg.idxMinTemp];
        if (pkg.idxMaxTemp >= 0) pkg.tsMaxTemp = (time_t)pkg.tsPoints[pkg.idxMaxTemp];

        /*
         * Cache 7D: sincroniza realMinVal/realMaxVal com os pontos no array.
         * Após descartar pontos antigos + recalcular, os extremos anteriores
         * podem ser de registros que já saíram da janela.
         */
        pkg.realMinVal = pkg.minVal;
        pkg.realMaxVal = pkg.maxVal;
        pkg.tsRealMin  = pkg.tsMinTemp;
        pkg.tsRealMax  = pkg.tsMaxTemp;

        entry.humMin = localHumMin;
        entry.humMax = localHumMax;
        entry.lastRefresh = now;
    }

    return (newPoints > 0);
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
    LOG_CODE(LOG_INFO, "APP", APP_NTP_CORRECTING, delta, String(TRL("NTP correction: ", "Correcao NTP: ")) + delta + "s");
    _storageMgr.correctProvisionalTimestamps(bootTs, delta);
    LOG_CODE(LOG_INFO, "APP", APP_NTP_CORRECTED, 0, "");
    _storageMgr.unlockHeavyTask();

    /*
     * Invalida todo o cache de gráficos 7d pré-carregado no boot.
     * Os timestamps dos registros foram corrigidos pelo delta NTP,
     * mas os gráficos em cache ainda usam os dados antigos.
     * Serão recarregados sob demanda ou no próximo refresh de 6h.
     */
    for (int i = 0; i < MAX_SENSORS + 2; i++) {
        _graphCache[i].valid = false;
    }
    for (int r = 0; r < 5; r++) {
        _sensorCache[r].valid = false;
    }
    _sensorCacheId = -99;
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_INVALIDATED, 0, "");
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
            _currentSensorIdx = firstSlot;
            refreshSelectedSlot();
        }
        _displayMgr.setAlarmState(mask, firstSlot, ambTempAlarm, ambHumAlarm);
        LOG_CODE(LOG_WARN, "APP", APP_ALARM_TRIGGERED, 0, "");
    } else if (anyAlarm && (_soundMgr.isAlarming() || silenced)) {

        _displayMgr.setAlarmState(mask, -1, ambTempAlarm, ambHumAlarm);
    } else if (!anyAlarm && (_soundMgr.isAlarming() || silenced)) {

        _soundMgr.stopAlarm();
        _displayMgr.setAlarmState(0, -1, false, false);

        if (silenced) {
            _displayMgr.setAlarmSilenced(false, 0);
            LOG_CODE(LOG_INFO, "APP", APP_ALARM_SILENCE_CANCEL, 0, "");
        }
        LOG_CODE(LOG_INFO, "APP", APP_ALARM_CLEARED, 0, "");
    }
}


/**
 * @brief Check if the user recently touched the display.
 * Returns true if last touch was within TOUCH_PRIORITY_MS (5s).
 * Durante essa janela, flash I/O não urgente é deferido (buffer em RAM)
 * para manter display + touch fluidos. Afeta:
 *   - TelemetryManager::update (já) — skip ciclo
 *   - StorageManager::writeHistoryEntry — bufferiza em _pendingHistRec
 *   - StorageManager::flushCursorIfDirty — skip, cursor fica dirty
 *   - LogManager::writeCompactToFlash — bufferiza em _pendingLogs
 */
bool AppManager::isUserInteracting() const {
    uint32_t lastTouch = _displayMgr.getLastTouchTimestamp();
    if (lastTouch == 0) return false;
    return !timeSince(lastTouch, TOUCH_PRIORITY_MS);
}

/**
 * Fase 5: Chamado uma vez na transição touch-active→touch-free. Fecha a
 * janela de exposição dos buffers em RAM (log/hist/cursor) disparando um
 * flush coordenado. Ordem importa: logs primeiro (menor, mais crítico
 * para auditoria), depois hist, depois cursor (coalesce de telemetria).
 * Um WDT window estendido cobre os 3 writes em série.
 */
void AppManager::onTouchReleased() {
    LogManager::WdtWindow _wdt(30000);
    LogManager::instance().flushPendingIfAny();
    watchdog_update();
    _storageMgr.flushPendingHist();
    watchdog_update();
    _storageMgr.flushCursorIfDirty();
}
