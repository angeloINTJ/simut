/**
 * @file    AppManager.h
 * @brief   Application orchestrator — top-level coordinator for all subsystems.
 * @details Owns all manager instances (Sensor, Storage, Command, Display,
 * Network, Web, Telemetry, Sound) and coordinates their lifecycle.
 * Handles boot sequence, main loop scheduling, UI event dispatch,
 * alarm monitoring, sensor calibration, and NTP time correction.
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
    /** F-LOCKOUT-STUCK: bridges StorageManager BigSaveQuietCallback → DisplayManager. */
    bool requestDisplayQuietMode(bool enable);
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

    float _cachedMin[MINMAX_SLOT_COUNT];
    float _cachedMax[MINMAX_SLOT_COUNT];
    float _cachedHumMin;
    float _cachedHumMax;

    /* Min/max vindos exclusivamente do preload (CSV do dia) */
    float _preloadMin[MINMAX_SLOT_COUNT];
    float _preloadMax[MINMAX_SLOT_COUNT];
    float _preloadHumMin;
    float _preloadHumMax;

    /* Estado de scan de sensores (antes eram static globals no .cpp) */
    bool _waitingScan       = false;
    int  _currentSensorIdx  = 0;
    bool _isApMode          = false;

    void preloadMinMax();
    void openStatsScreen(int sensorId);

    void updateLiveDisplay();
    void refreshSelectedSlot();
    void renderGraphOptimized(int sensorId, int range, bool showAfterLoad = true, int navOffset = 0, time_t forceEndEpoch = 0);

    void processHistoryLogging();
    void processBackgroundScan();

    void executeCommand(CliDemand cmd);

    void checkAndAutoHealSensors();
    void loadAndCalibrateSensors();
    void handleTimeSync(uint32_t bootTs, int32_t delta);
    void checkAlarmConditions();

    /* ── Cache de gráficos 7d pré-carregados ── */
    /**
     * Cada entrada armazena um GraphDataPackage completo para o range 7d.
     * Slots: [0-9] = sensores DS18B20, [10] = board temp, [11] = ambient.
     * Carregado no boot e atualizado a cada 6 horas em background.
     */
    struct GraphCacheEntry {
        GraphDataPackage pkg;
        float humMin, humMax;
        time_t lastRefresh;
        bool valid;
    };

    GraphCacheEntry _graphCache[MAX_SENSORS + 2];
    uint32_t _lastGraphCacheRefresh = 0;
    int  _bgCacheNextSensor = -2;   /**< -2 = idle, -1 = ambient, 0-9 = sensors, 10 = board */
    bool _bgCacheRunning    = false;

    /**
     * Cache de todos os 5 ranges do sensor atualmente visualizado.
     * Quando o usuário abre um sensor, todos os ranges são carregados
     * para que a troca entre 1H/6H/12H/24H/7D seja instantânea.
     * Ao abrir outro sensor, o cache é invalidado e recarregado.
     */
    GraphCacheEntry _sensorCache[5];  /**< [0]=1H [1]=6H [2]=12H [3]=24H [4]=7D */
    int _sensorCacheId = -99;         /**< sensorId cacheado (-99 = nenhum)      */
    int _graphNavOffset = 0;          /**< Offset de navegação temporal (≤ 0)    */
    int _lastGraphRange = 3;          /**< Último range renderizado (para nav)   */
    time_t _graphAnchorEnd = 0;       /**< Âncora do fim da janela (0 = usar now) */

    void preloadGraphCaches();
    void preloadSensorRanges(int sensorId, int skipRange);
    int  graphCacheIdx(int sensorId);
    bool appendToGraphCache(GraphCacheEntry& entry, int sensorId);


    bool _pendingAlarmDeactivate = false;

    static constexpr uint32_t TOUCH_PRIORITY_MS = 5000;

    /* ── Fase 4: CLI deferral durante touch priority ─────────────────────
     * Comandos CLI (USB+BT) executados durante isUserInteracting() podem
     * tocar flash/heap e competir pelo WDT. Enfileiramos até 2 comandos e
     * drenamos após o touch liberar. Overflow (3º+) é descartado com
     * aviso pelo console. `processInput` continua rodando — só a execução
     * é deferida, então echo e acumulação de bytes seguem normais. */
    static constexpr uint8_t CLI_QUEUE_CAP = 2;
    CliDemand _cliQueue[CLI_QUEUE_CAP];
    uint8_t   _cliQueueHead  = 0;
    uint8_t   _cliQueueCount = 0;
    bool      _cliDropNotified = false;  /**< Evita spam de "CLI busy" em rajada */

    /* ── Fase 5: orquestração de flush pós-interação ────────────────────
     * Detecta a transição `isUserInteracting() true → false` e dispara
     * flush explícito de todos os buffers que deferiram durante o touch:
     *   1. Log pendentes (_pendingLogs em LogManager)
     *   2. Histórico pendente (_pendingHistRec em StorageManager)
     *   3. Cursor dirty (flushCursorIfDirty)
     * CLI queue já drena 1-por-loop (Fase 4), então não precisa de chamada
     * explícita aqui. */
    bool _wasInteracting = false;
    void onTouchReleased();
};
