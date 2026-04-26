/**
 * @file    AppManager_Loop.cpp
 * @brief   Main loop with priority-based task scheduling.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "MetricsManager.h"
#include "TouchPriority.h"
#include <hardware/watchdog.h>

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
            LOG_CODE(LOG_ERROR, "APP", APP_DISPLAY_PAUSE_STUCK, 0, TRL("Display pause stuck >5s!"));
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
                    LOG_CODE(LOG_ERROR, "APP", APP_CORE1_DEAD, 0, TRL("Core 1 dead >10s. Restarting."));
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
            { char _b[16]; _netMgr.getIpAddress(_b, sizeof(_b)); safeCopy(sd.ip, _b, sizeof(sd.ip)); }
            { char _b[18]; _netMgr.getMacAddress(_b, sizeof(_b)); safeCopy(sd.mac, _b, sizeof(sd.mac)); }
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
