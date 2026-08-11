/**
 * @file AppManager.h
 * @brief Application orchestrator — top-level coordinator for all subsystems.
 * @details Owns all manager instances (Sensor, Storage, Command, Display,
 * Network, Web, Telemetry, Sound) and coordinates their lifecycle.
 * Handles boot sequence, main loop scheduling, UI event dispatch,
 * alarm monitoring, sensor calibration, and NTP time correction.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <memory>
#include "SystemDefs.h"

/* Forward declarations for all manager subsystems — owned as unique_ptr on heap. */
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
 /** Bridges StorageManager BigSaveQuietCallback to DisplayManager. */
 bool requestDisplayQuietMode(bool enable);
 void core0Yield( );


 bool isUserInteracting( ) const;

 /* Public so WebManager_Calib can call after /api/calib POST
	 * and /api/sensor_accept (same pattern as the CLI). */
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
 /** A record has been written since this boot (see the first-sample note in
  *  AppManager_Loop.cpp). */
 bool     _histFirstDone = false;
 uint32_t _histFirstTryMs = 0;
 uint32_t _lastSensorCheck = 0;
 uint32_t _bootCompletedAt = 0;


 volatile bool _pendingTimeSync = false;
 uint32_t _timeSyncBootTs = 0;
 int32_t _timeSyncDelta = 0;

 float _cachedMin[MINMAX_SLOT_COUNT];
 float _cachedMax[MINMAX_SLOT_COUNT];
 float _cachedHumMin[MINMAX_SLOT_COUNT];
 float _cachedHumMax[MINMAX_SLOT_COUNT];

 /* Min/max from preload (today's binary history file) */
 float _preloadMin[MINMAX_SLOT_COUNT];
 float _preloadMax[MINMAX_SLOT_COUNT];
 float _preloadHumMin[MINMAX_SLOT_COUNT];
 float _preloadHumMax[MINMAX_SLOT_COUNT];

 /* Sensor scan state */
 bool _waitingScan = false;
 int _currentSensorIdx = 0;
 bool _isApMode = false;
 uint32_t _lastSlotChangeTime = 0;
 int8_t _lastSavedTopIdx = -1;
 int8_t _lastSavedSlotIdx = -1;
 
 void preloadMinMax( );
 void openStatsScreen(int sensorId);

 void updateLiveDisplay( );
 void refreshSelectedSlot( );
 void renderGraphOptimized(int sensorId, int range, bool showAfterLoad = true, int navOffset = 0, time_t forceEndEpoch = 0);

 void processHistoryLogging( );
 void processBackgroundScan( );

 void executeCommand(CliDemand cmd);

 /* Handlers extracted from the longest cases in executeCommand's switch
	 * (>=30 lines each). Each receives the command, the active config,
	 * and a 'changed' flag by reference (set to true if RAM was modified).
	 * Implementations in AppManager_CmdHandlers.cpp. */
 /* Only the admin-password reset survives into the emergency image; the rest
  * are reachable exclusively from commands that compile out. */
 void cmdHandleResetAdmin(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
#if SIMUT_CLI_FULL
 void cmdHandleSensorField(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleAcceptSensor(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleUserAdd(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleUserPerm(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleSetTime(const CliDemand& cmd);
 void cmdHandleIpCfg(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleDnsCfg(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
 void cmdHandleUserPass(const CliDemand& cmd, SystemConfig& cfg, bool& changed);
#endif

 void checkAndAutoHealSensors( );
 void handleTimeSync(uint32_t bootTs, int32_t delta);
 void checkAlarmConditions( );

 /* Graph caches removed — direct flash reads free ~45 KB of heap
	 * for telemetry. Every graph open or range/sensor switch reads
	 * directly from flash (~500ms-2s). */
 int _graphNavOffset = 0; /**< Temporal navigation offset (≤ 0) */
 int _lastGraphRange = 3; /**< Last rendered range (used for navigation) */
 time_t _graphAnchorEnd = 0; /**< Window end anchor (0 = use now) */


 bool _pendingAlarmDeactivate = false;

 /* Warn-once when processHistoryLogging skips saving due to missing
	 * time reference (no NTP and no provisional). Resets automatically
	 * when time reference returns. Prevents silent record loss after
	 * NTP failure or factory-reset boot. */
 bool _histTimeRefWarned = false;
 bool _histSchemaEmptyWarned = false;
 bool _histSchemaMismatchWarned = false;

 static constexpr uint32_t TOUCH_PRIORITY_MS = 5000;

 /* CLI deferral during touch priority: up to 2 commands are
	 * queued while the display is in use, then drained once touch
	 * releases. Overflow (3rd+) is dropped with a console warning.
	 * `processInput` still runs — only execution is deferred. */
 static constexpr uint8_t CLI_QUEUE_CAP = 2;
 CliDemand _cliQueue[CLI_QUEUE_CAP];
 uint8_t _cliQueueHead = 0;
 uint8_t _cliQueueCount = 0;
 bool _cliDropNotified = false; /**< Evita spam de "CLI busy" em rajada */

 /* Post-interaction flush orchestration: detects the transition
	 * `isUserInteracting() true -> false` and triggers explicit flush
	 * of all buffers that deferred during touch:
	 * 1. Pending logs (_pendingLogs in LogManager)
	 * 2. Pending history (_pendingHistRec in StorageManager)
	 * 3. Dirty cursor (flushCursorIfDirty)
	 * The CLI queue drains 1-per-loop, so it needs no explicit call. */
 bool _wasInteracting = false;
 void onTouchReleased( );
};
