/**
 * @file AppManager.h
 * @brief Application orchestrator — top-level coordinator for all subsystems.
 * @details Owns all manager instances (Sensor, Storage, Command, Display,
 * Network, Web, Telemetry, Sound) and coordinates their lifecycle.
 * Handles boot sequence, main loop scheduling, UI event dispatch,
 * alarm monitoring, sensor calibration, and NTP time correction.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <memory>
#include "SystemDefs.h"

/* EXT-005 / F17 (BISECT Plan C — INSTRUMENTAÇÃO): TODOS 8 managers em heap
 * (igual Plan A v3.28.6 que reproduziu a regressão), MAS com stages de progresso
 * em DisplayManager::loopCore1 pra mapear exatamente onde Core 1 trava. */
class SensorManager;
class StorageManager;
class CommandManager;
class DisplayManager;
class NetworkManager;
class WebManager;
class TelemetryManager;
class SoundManager;

class AppManager {
public:
 AppManager( );
 ~AppManager( );
 void setup( );
 void loop( );

 bool isDisplayAlive( );
 void restartDisplayCore( );
 void pauseDisplayForFlash(bool lock);
 /** F-LOCKOUT-STUCK: bridges StorageManager BigSaveQuietCallback → DisplayManager. */
 bool requestDisplayQuietMode(bool enable);
 void core0Yield( );


 bool isUserInteracting( ) const;

 /* F-CALIB-UI — público para WebManager_Calib chamar após
 * /api/calib POST e /api/sensor_accept (mesmo padrão do CLI). */
 void loadAndCalibrateSensors( );

private:
 std::unique_ptr<SensorManager> _sensorMgr;
 std::unique_ptr<StorageManager> _storageMgr;
 std::unique_ptr<CommandManager> _cmdMgr;
 std::unique_ptr<DisplayManager> _displayMgr;
 std::unique_ptr<NetworkManager> _netMgr;
 std::unique_ptr<WebManager> _webMgr;
 std::unique_ptr<TelemetryManager> _telemetryMgr;
 std::unique_ptr<SoundManager> _soundMgr;

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
 bool _waitingScan = false;
 int _currentSensorIdx = 0;
 bool _isApMode = false;

 void preloadMinMax( );
 void openStatsScreen(int sensorId);

 void updateLiveDisplay( );
 void refreshSelectedSlot( );
 void renderGraphOptimized(int sensorId, int range, bool showAfterLoad = true, int navOffset = 0, time_t forceEndEpoch = 0);

 void processHistoryLogging( );
 void processBackgroundScan( );

 void executeCommand(CliDemand cmd);

 /* handlers extraídos dos cases mais longos do switch
 * de executeCommand (>=30 linhas). Cada um recebe o cmd, o cfg em
 * uso e o flag `changed` por referência (modificado se RAM mudou).
 * Implementações em AppManager_CmdHandlers.cpp. */
 void cmdHandleSensorField(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleAcceptSensor(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleUserAdd(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleResetAdmin(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleSetTime(const CliDemand& cmd);
 void cmdHandleIpCfg(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleDnsCfg(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleUserPass(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 /* cmdHandleDbgSensorHistoryAll removido. */

 void checkAndAutoHealSensors( );
 void handleTimeSync(uint32_t bootTs, int32_t delta);
 void checkAlarmConditions( );

 /* graph caches eliminados completamente para
 * liberar heap permanentemente para telemetria.
 * • _graphCache[12] (cross-sensor 7D, ~31 KB): REMOVIDO
 * • _sensorCache[5] (per-sensor time ranges, ~13 KB): REMOVIDO
 * Resultado: cada open de gráfico OU switch de range/sensor faz
 * um read direto do flash (~500ms-2s). Heap permanentemente livre
 * de ~45 KB de pressure. */
 int _graphNavOffset = 0; /**< Offset de navegação temporal (≤ 0) */
 int _lastGraphRange = 3; /**< Último range renderizado (para nav) */
 time_t _graphAnchorEnd = 0; /**< Âncora do fim da janela (0 = usar now) */


 bool _pendingAlarmDeactivate = false;

 /* warn-once quando processHistoryLogging tem que
 * pular save por falta de time ref (sem NTP + sem provisional). Reseta
 * automaticamente quando time ref volta. Sem isso, ficaria minutos/horas
 * silenciosamente perdendo records pós-NTP-fail + factory-reset boot. */
 bool _histTimeRefWarned = false;

 static constexpr uint32_t TOUCH_PRIORITY_MS = 5000;

 /* ── CLI deferral durante touch priority ─────────────────────
 * Comandos CLI (USB+BT) executados durante isUserInteracting( ) podem
 * tocar flash/heap e competir pelo WDT. Enfileiramos até 2 comandos e
 * drenamos após o touch liberar. Overflow (3º+) é descartado com
 * aviso pelo console. `processInput` continua rodando — só a execução
 * é deferida, então echo e acumulação de bytes seguem normais. */
 static constexpr uint8_t CLI_QUEUE_CAP = 2;
 CliDemand _cliQueue[CLI_QUEUE_CAP];
 uint8_t _cliQueueHead = 0;
 uint8_t _cliQueueCount = 0;
 bool _cliDropNotified = false; /**< Evita spam de "CLI busy" em rajada */

 /* ── orquestração de flush pós-interação ────────────────────
 * Detecta a transição `isUserInteracting( ) true → false` e dispara
 * flush explícito de todos os buffers que deferiram durante o touch:
 * 1. Log pendentes (_pendingLogs em LogManager)
 * 2. Histórico pendente (_pendingHistRec em StorageManager)
 * 3. Cursor dirty (flushCursorIfDirty)
 * CLI queue já drena 1-por-loop ( ), então não precisa de chamada
 * explícita aqui. */
 bool _wasInteracting = false;
 void onTouchReleased( );
};
