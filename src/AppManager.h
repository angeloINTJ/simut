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
#if SIMUT_AIR
#include "air/AirConfig.h"
#endif

/* Forward declarations for all manager subsystems — owned as unique_ptr on heap. */
class SensorManager;
class StorageManager;
class CommandManager;
class DisplayManager;
class NetworkManager;
class WebManager;
class TelemetryManager;
class SoundManager;
class SyslogManager;

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
 std::unique_ptr<SyslogManager> _syslogMgr;

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
 /** Starts Access Point mode at runtime (192.168.4.1) — the 'ap' CLI/BT command. */
 void startApMode( );

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
 /** Slot da tela de ação cujo Desativar foi pedido — usado para o registro
 * de ação (err_off/off) após a confirmação do PIN. */
 int8_t _alarmDeactivateSlot = -1;

 /* ── 2ª linha de telemetria (v21): detecção de borda de alarme/erro ──────
  * Estado por slot, em RAM (~20 B). trip = canal fora do limite (borda já
  * confirmada e enfileirada); cand = candidato visto em 1 ciclo (debounce de
  * 2 ciclos, ~10 s); err = sensor em falha (sem debounce — a histerese de
  * 3 erros já acontece no SensorManager). Zerado no boot; consumido por
  * checkAlarmConditions( ). */
 uint8_t _alarmTripBits[MAX_SENSORS] = {0};
 uint8_t _alarmCandBits[MAX_SENSORS] = {0};
 uint16_t _alarmErrBits = 0;
 /** Borda confirmada → enfileira em _telemetryMgr->pushAlarm( ). */
 void handleAlarmTelemetryEdges( );
 /** Registro de AÇÃO (silenciar/desativar) na 2ª linha de telemetria — o
 * o registro carrega o código do domínio (alarm* ou err*) com sufixo
 * escolhido entre os dois códigos pelo estado de erro do slot. */
 void pushAlarmAction(int8_t slot, uint8_t errCodeErr, uint8_t errCodeLim);

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

#if SIMUT_AIR
 /* ────────────────────────────────────────────────────────────────────────
  * SIMUT Air — headless hibernating build (M0 operacional / M1 dormant).
  * M0 = Alpha-like headless boot (web + serial + BT config, sensors, telemetry).
  * M1 = dormant cycle: wake on RTC -> read sensors until stable while the WiFi
  *       connects in parallel -> always save history -> if online, flush pending
  *       telemetry (non-blocking) -> sleep for max(history interval, backoff).
  *       Led stays ON while awake, OFF while dormant.
  * Air config lives in /config/air.bin (NOT SystemConfig -> CONFIG_VERSION frozen).
  * ──────────────────────────────────────────────────────────────────────── */
 enum AirPhase {
  AIR_PHASE_OFF = 0,   /* M0 (operational, not hibernating) */
  AIR_PHASE_WARMUP,    /* M1: power sensors, settle */
  AIR_PHASE_SAMPLE,    /* M1: pump sensors until stable + connect WiFi (parallel) */
  AIR_PHASE_DECIDE,    /* M1: always save history, pick CONNECT vs SLEEP */
  AIR_PHASE_PERSIST,   /* M1: legacy no-op (history now saved in DECIDE) */
  AIR_PHASE_CONNECT,   /* M1: wait for NTP/time sync */
  AIR_PHASE_FLUSH,     /* M1: flush pending telemetry (non-blocking) */
  AIR_PHASE_SLEEP      /* M1: power down + dormant */
 };
 AirPhase _airPhase = AIR_PHASE_OFF;
 uint32_t _airPhaseTimer = 0;
 bool     _airActive = false;   /* true = this boot is a dormant wake (M1) */
 /* True only while this boot really began as an RTC wake. Separate from
  * _airActive, which airStartHibernate( ) also sets: the wake alarm is
  * compensated by the awake time (so the period equals the history interval),
  * and that compensation is only meaningful when millis( ) measures this
  * cycle rather than however long an operator left the device in M0. */
 bool     _airWokeFromSleep = false;
 /** Seconds the last sleep really lasted, as the RTC measured it (0 = unknown). */
 uint32_t _airSleptSec = 0;
 /** True once the wake gave up on the WiFi: stops pumping the network so a
  *  missing SSID cannot keep the device awake past its sensor reading. */
 bool     _airNetGaveUp = false;
 /** This wake is due to send telemetry, so it raises the radio. False on a
  *  reading-only wake, which never initialises the CYW43 at all. Always true
  *  in M0, where an operator is talking to the device. */
 bool     _airRadioWake = true;
 /** The CYW43 was actually brought up this boot. Guards everything that lives
  *  on the wireless chip — the onboard LED included, since LED_BUILTIN on the
  *  Pico W is one of its GPIOs and writing it would power the radio back up. */
 bool     _airRadioUp = true;
 /** Wakes since the last one that sent telemetry; the telemetry schedule.
  *  Carried across the sleep in scratch[1]. */
 uint8_t  _airWakesSinceRadio = 0;
 uint32_t _airLastActivityMs = 0; /* M0 idle timer */
 /** Short M0 window before a cycle that a reset interrupted resumes itself
  *  (plan F25). 0 = no interrupted cycle, or the crash-loop guard tripped, and
  *  the configured idle timeout applies instead. */
 uint16_t _airResumeGraceSec = 0;
 AirConfig _airCfg;                 /* loaded from /config/air.bin */

 void airLoop( );             /* M1 pump, called from loop( ) */
 void airStartHibernate( );   /* M0 -> M1 transition (command or idle timeout) */
 void airEnterDormant( );     /* M1 final step: power off + dormant */
 void airMarkActivity( );     /* reset M0 idle timer on any command/web hit */
 void airSetLed(bool on);     /* onboard LED — only while the CYW43 is up */
 /** Is this wake the one that sends? Compares the wakes accumulated since the
  *  last send against the configured telemetry interval. Must be answered
  *  before the network is started, because its answer is whether to start it. */
 bool airTelemetryDue( ) const;
 void airSensorPower(uint8_t pin, bool on); /* sensor power-gating GPIO (high = awake) */
 bool airLoadConfig(struct AirConfig& out);   /* read /config/air.bin */
 bool airSaveConfig(const struct AirConfig& c); /* write /config/air.bin (atomic) */
#endif /* SIMUT_AIR */
};
