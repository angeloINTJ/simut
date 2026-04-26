/**
 * @file    AppManager_Boot.cpp
 * @brief   Boot sequence: filesystem, sensors, network, web server initialization.
 * @project SIMUT
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

extern AppManager app;

void AppManager::setup() {
    Serial.begin(115200);
    delay(1000);

    /* Log da versão ANTES de qualquer init que possa travar — garante que
     * o user sempre saiba qual firmware está rodando, mesmo se o boot
     * trancar logo depois. */
    Serial.println();
    Serial.println(F("=============================================="));
    Serial.print  (F("  SIMUT firmware "));
    Serial.println(SIMUT_VERSION);
    Serial.println(F("=============================================="));

    /*
     * NÃO chamar TRACE_MOD aqui — scratch[4] precisa conter o módulo do
     * crash anterior até a autópsia ler (em LogManager::begin abaixo).
     * TRACE_BEAT(0) é OK: só mexe em RAM (_coreHeartbeat), não no scratch.
     */
    TRACE_BEAT(0);

    _displayMgr.begin();
    _displayMgr.startCore1();
    LOG_CODE(LOG_INFO, "APP", APP_DISPLAY_LAUNCHED, 0, TRL("Display UI Launched on Core 1."));

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
        String storedHash = String(cfg.users[0].password);
        /* SEC-007: suporta tanto legacy (30 chars) quanto v1 (32 chars). */
        if (cfg.users[0].hashVersion == 0 && storedHash.length() == 30) {
            String legacyHash = _storageMgr.hashPasswordLegacy(
                String(cfg.users[0].username), preHash);
            return (legacyHash == storedHash);
        } else {
            String hashed = _storageMgr.hashPasswordV1(
                String(cfg.users[0].username), preHash, cfg.users[0].salt);
            return (hashed == storedHash);
        }
    });

    if (!fsOk) LOG_CODE(LOG_ERROR, "APP", APP_STORAGE_CRITICAL, 0, TRL("Storage Critical Failure!"));

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
                     TRL("Factory defaults active; initial admin pass on USB/serial."));
        } else {
            /* Caso raro: factory detectado mas plaintext não está em RAM
             * (loadConfiguration limpou após fallback). Avisa sem vazar. */
            LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, 0,
                     TRL("Factory defaults active; password regen required."));
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
    /* F-LANGPACK Etapa 2: scan /lang/ ANTES de setLanguage para que o
     * lookup já esteja pronto quando tr() for chamado. Sem .lng, cai EN. */
    DisplayManager::findAndLoadLangFile();
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
            LOG_CODE(LOG_WARN, "APP", APP_TOUCH_CAL_REQUIRED, 0, TRL("Touch calibration required."));
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
                        LOG_CODE(LOG_INFO, "APP", APP_TOUCH_CAL_INITIAL, 0, TRL("Initial touch calibration saved."));
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
        LOG_CODE(LOG_WARN, "APP", APP_AP_MODE_TRIGGERED, 0, TRL("User triggered AP mode."));
        _displayMgr.setBootStatus("Starting Access Point (AP)...");
        _displayMgr.setBootStatus("Connect to network SIMUT_SETUP");
        _displayMgr.setBootStatus("Access on mobile: 192.168.4.1");
        _netMgr.beginAP(cfg.deviceName);
        for (int i = 0; i < 35; i++) { delay(100); feedWdt(); }
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
        feedWdt();


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
        LOG_CODE(LOG_INFO, "APP", APP_READY_AP, 0, TRL("System ready (AP mode)."));
    } else {

        /* Carrega min/max do dia a partir do arquivo de histórico */
        _displayMgr.setBootStatus("Loading daily Min/Max cache...");
        delay(80);
        preloadMinMax();

        _displayMgr.setBootStatus("Warming up sensors...");
        {
            unsigned long warmStart = millis();


            while (millis() - warmStart < 2000) {
                feedWdt();
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
        LOG_CODE(LOG_INFO, "APP", APP_READY, 0, TRL("System ready."));
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
