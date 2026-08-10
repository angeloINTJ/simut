/**
 * @file AppManager_Loop.cpp
 * @brief Main loop with priority-based task scheduling.
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "CommandManager.h"
#include "FlashIrqProbe.h"   /* g_core1WaitAlarm — W_WFE timer fingerprint */
#include <hardware/timer.h>
#include "DisplayManager.h"
#include "LogManager.h"
#include "MetricsManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "SoundManager.h"
#include "StorageManager.h"
#include "TelemetryManager.h"
#include "TouchPriority.h"
#include "WebManager.h"
#include <hardware/watchdog.h>

void AppManager::loop( ) {
 /* First marker of the iteration. Everything from here to the MOD_CLI below —
  * the cross-core health check and the Core-1 restart it can trigger — used to
  * trace as whatever the previous iteration left behind. */
 TRACE_MOD(0, MOD_LOOP);
 TRACE_BEAT(0);
 core1StallSample( );  /* covers the idle stretches between web handlers */
 watchdog_update( );

 /* edge detection touch-released → orchestrated flush. */
 bool isNow = isUserInteracting( );
 if (_wasInteracting && !isNow) {
 onTouchReleased( );
 }
 _wasInteracting = isNow;

 LogManager::instance( ).checkCrossCoreHealth( );
 watchdog_update( );

 /* Heap/HWM + largest contiguous block every 10s.
 * sampleLargestBlock does ~16 malloc/free (immediately freed). */
 {
 static uint32_t _lastHeapSample = 0;
 if (timeSince(_lastHeapSample, 10000)) {
 _lastHeapSample = millis( );
 MetricsManager::instance( ).sampleHeap( );
 MetricsManager::instance( ).sampleLargestBlock( );
 }
 }
	watchdog_update( );


 {
 uint32_t pauseTs = _displayMgr->getPauseStartTime( );
 if (pauseTs > 0 && timeSince(pauseTs, 5000)) {
 LOG_CODE(LOG_ERROR, "APP", APP_DISPLAY_PAUSE_STUCK, 0, TRL("Display pause stuck >5s!"));
 _displayMgr->forceUnpause( );
 }
 }
	watchdog_update( );


 {
 static uint32_t _lastCore1RestartCheck = 0;
 if (_lastCore1RestartCheck == 0) _lastCore1RestartCheck = millis( ); /* Boot guard */
 if (timeSince(_lastCore1RestartCheck, 5000)) {
 _lastCore1RestartCheck = millis( );
 if (_displayMgr->isCore1Ready( ) && _displayMgr->getPauseStartTime( ) == 0) {
 uint32_t beat = _displayMgr->getHeartbeat( );
 /* Patch C: signed cast to tolerate cross-core race (beat
 * slightly ahead of local millis( )). */
 if (beat > 0 && timeSince(beat, 10000)) {
 /* W_WFE fingerprint: the state of Core 1's wait alarm at the moment
  * of the verdict, packed for the persisted log. ctx = deltaMs*10 +
  * flags (INTE*4 | INTR*2 | ARMED*1), flags sign-matched to delta so
  * |ctx|%10 recovers them. ARMED with the target in the past =
  * comparator missed its equality tick; INTR raw set = the tick fired
  * and Core 1's NVIC never took it; ARMED clear = the wait was not
  * asleep on this alarm at all. One line decides three theories. */
 if (g_core1WaitAlarm < 4) {
  /* The log's ctx is 16-bit: pack as sign(delta) * (flags*1000 +
   * min(|delta| in SECONDS, 999)). First run clamped at -32767 and
   * cost the flags — worth exactly one line of arithmetic to never
   * lose a bit to the record format again. */
  const uint8_t n = (uint8_t)g_core1WaitAlarm;
  const int32_t deltaUs = (int32_t)(timer_hw->alarm[n] - timer_hw->timerawl);
  int32_t deltaS = deltaUs / 1000000;
  if (deltaS > 999) deltaS = 999;
  if (deltaS < -999) deltaS = -999;
  const int32_t flags = (int32_t)(((timer_hw->inte >> n) & 1u) * 4u +
                                  ((timer_hw->intr >> n) & 1u) * 2u +
                                  ((timer_hw->armed >> n) & 1u));
  const int32_t mag = flags * 1000 + (deltaS < 0 ? -deltaS : deltaS);
  LOG_CODE(LOG_ERROR, "APP", APP_CORE1_DEAD,
           (deltaUs < 0) ? -mag : mag,
           "W_WFE timer fingerprint");
 }
 LOG_CODE(LOG_ERROR, "APP", APP_CORE1_DEAD, 0, TRL("Core 1 dead >10s. Restarting."));
 _displayMgr->restartCore1( );
 }
 }
 }
 }
	watchdog_update( );

 /* T1.5 (stability wave 1): quiet-mode leak watchdog. Every caller of
  * requestQuietMode( ) must releaseQuietMode( ) — if an error path ever
  * skips the release, Core 1 stays dead forever and the heartbeat
  * restart above can't act (it requires isCore1Ready). 15 s is far
  * beyond any legitimate save; drain the refcount and relaunch. */
 {
  static uint32_t _lastQuietCheck = 0;
  if (timeSince(_lastQuietCheck, 5000)) {
   _lastQuietCheck = millis( );
   uint32_t qs = _displayMgr->quietSinceMs( );
   if (_displayMgr->isInQuietMode( ) && qs != 0 && timeSince(qs, 15000)) {
    LOG_CODE(LOG_ERROR, "APP", APP_DISPLAY_PAUSE_STUCK, 1,
             TRL("Quiet mode leak >15s. Forcing release."));
    for (int i = 0; i < 4 && _displayMgr->isInQuietMode( ); i++) {
     _displayMgr->releaseQuietMode( );
    }
   }
  }
 }
	watchdog_update( );

 CliDemand cmd;
 TRACE_MOD(0, MOD_CLI);

 /* Drain 1 queued command per loop when touch is free.
 * Execute before processInput so we don't delay the prompt if the
 * user just finished typing. */
 if (_cliQueueCount > 0 && !isUserInteracting( )) {
 CliDemand queued = _cliQueue[_cliQueueHead];
 _cliQueue[_cliQueueHead] = CliDemand( ); /* clears Strings — frees heap */
 _cliQueueHead = (_cliQueueHead + 1) % CLI_QUEUE_CAP;
 _cliQueueCount--;
 /* CMD_UNKNOWN is dispatched too. It used to be filtered here and at the
  * direct call below, which made the "unknown command" branch in
  * executeCommand dead code — a typo returned silently to the prompt. That
  * was survivable while nearly everything parsed; with the configuration
  * commands moved to the web it is not, because every one of them now parses
  * to CMD_UNKNOWN and silence reads as a hung device. Empty input cannot
  * reach here: processInput only returns true on a non-empty buffer. */
 executeCommand(queued);
 if (!_waitingScan) _cmdMgr->printPrompt( );
 _cliDropNotified = false;
 }

 if (_cmdMgr->processInput(cmd)) {
 {
 if (isUserInteracting( )) {
 if (_cliQueueCount < CLI_QUEUE_CAP) {
 uint8_t tail = (_cliQueueHead + _cliQueueCount) % CLI_QUEUE_CAP;
 _cliQueue[tail] = cmd;
 _cliQueueCount++;
 } else if (!_cliDropNotified) {
 _cmdMgr->printError(_cmdMgr->isPt( )
 ? String("CLI ocupada (display em uso). Comando descartado.")
 : String("CLI busy (display in use). Command dropped."));
 _cliDropNotified = true;
 }
 } else {
 executeCommand(cmd);
 }
 }
 if (!_waitingScan) _cmdMgr->printPrompt( );
 }

 watchdog_update( );

 TRACE_MOD(0, MOD_WIFI);
 _netMgr->update( );

 watchdog_update( );

 bool heavyRendering = _displayMgr->isHeavyRendering( );


 TRACE_MOD(0, MOD_WEB_SERVER);
 _webMgr->update( );

 watchdog_update( );

 /*
 * Process touch sound BEFORE heavy tasks (telemetry, storage).
 * Ensures the beep plays in <10ms after touch instead of waiting
 * for loop end (~100-500ms with telemetry/TLS active).
 */
 if (_displayMgr->consumeTouchSound( )) {
 _soundMgr->play(SND_TOUCH_CLICK);
 _soundMgr->update( );
 }
 if (_displayMgr->consumeErrorSound( )) {
 _soundMgr->play(SND_ERROR);
 _soundMgr->update( );
 }

 bool menuActive = _displayMgr->isMenuActive( );

 TRACE_MOD(0, MOD_STORAGE_WRITE);
 _storageMgr->update( );
 _storageMgr->flushCursorIfDirty( );

 watchdog_update( );

 if (_isApMode) {
 TRACE_MOD(0, MOD_IDLE);
 return;
 }

 if (!menuActive) {
 TRACE_MOD(0, MOD_TELEMETRY);
 if (!heavyRendering && !isUserInteracting( )) {
 /* No graph caches, nothing to invalidate. */

 _telemetryMgr->update( );

 /* Notify the display about the last send result */
 bool telSuccess;
 if (_telemetryMgr->consumeLastSendResult(telSuccess)) {
 _displayMgr->setTelemetrySendStatus(telSuccess);
 }
 }
 }

 watchdog_update( );

 TRACE_MOD(0, MOD_SENSOR_READ);
 if (timeSince(_lastSensorCheck, 3000)) {
 if (!isUserInteracting( )) {
 _lastSensorCheck = millis( );
 checkAndAutoHealSensors( );
 }
 }

 watchdog_update( );

 if (_pendingTimeSync && !isUserInteracting( )) {
 handleTimeSync(_timeSyncBootTs, _timeSyncDelta);
 }

 TRACE_MOD(0, MOD_STORAGE_WRITE);

 watchdog_update( );


 /* Sampling is NOT gated. It used to be, and a gate held across a minute
  * boundary meant that minute was never measured at all — a hole no snapshot
  * can fill, because there was nothing to snapshot. The record now always
  * lands in the RAM encoder (a memcpy, safe under any gate) and only the
  * flash snapshot inside defers.
  *
  * Of the two gates that used to sit here only ONE could ever fire, which is
  * worth writing down because the removal reads like belt-and-braces
  * otherwise. isUserInteracting( ) is real: getLastTouchTimestamp( ) is set by
  * Core 1 and read here, so it can be true while this line runs.
  * isHeavyTaskLocked( ) could not: every holder — _webMgr->update( ),
  * _telemetryMgr->update( ), the graph via UI events — runs earlier in THIS
  * loop on Core 0, strictly sequential with this call, and nothing on the
  * Core 1 path takes the lock at all. Measured: hammering the heavy lock to a
  * 57% duty cycle for six minutes deferred exactly zero snapshots. */
 {
 bool due = timeSince(_lastHistoryTime,
                      _storageMgr->getHistoryIntervalMin( ) * 60000UL);

 /* The FIRST record of a boot does not wait a whole interval.
  *
  * _lastHistoryTime starts at 0, so the timer above only fires once millis( )
  * passes the interval — one full minute after boot, on top of the ~20 s the
  * boot itself takes. Every reboot therefore cost a measurement even with the
  * .wip snapshot preserving the block perfectly: measured across a web
  * commit_all, 108 s between the last record before and the first after, where
  * the interval is 60 s. The block lost nothing; the minute the device spent
  * restarting was never sampled, and roughly 40 s of that was pure waiting.
  *
  * The gate is the RAW system clock, deliberately not getEpoch( ) and not
  * isTimeSynced( ): getEpoch( ) seeds a provisional clock from
  * SIMUT_BUILD_EPOCH (2025-09-20) and returns it, which is above the
  * HIST_EPOCH_MIN threshold, so both would report a good clock on a device
  * that has none — and the record would be filed two years in the past, which
  * poisons the day file far worse than a missing minute. time(nullptr) passes
  * only once NTP or a manual `time` has really set the clock; with neither,
  * behaviour is exactly what it was before.
  *
  * Rate-limited because a failed attempt leaves the flag clear: without it
  * this calls processHistoryLogging( ) every loop iteration while a sensor or
  * the schema is not ready yet. */
 if (!due && !_histFirstDone && time(nullptr) > (time_t)HIST_EPOCH_MIN
     && timeSince(_histFirstTryMs, 2000)) {
  _histFirstTryMs = millis( );
  due = true;
 }
 if (due) processHistoryLogging( );
 }

 watchdog_update( );

 /* ── V5 history: the deferred .wip snapshot (§7.2) ──────────────────────
  * writeHistoryEntryV5 snapshots each record inline, so R8's bound is one
  * record, not a clock interval. This is only the catch-up path: the inline
  * attempt is refused while the user is touching the screen or a heavy task
  * holds the flash (a window here would freeze Core 1 mid-gesture), and the
  * record then waits here instead of until the next sample.
  *
  * Gated on h5WipPending( ) so the common case — snapshot already written
  * inline — costs one load and no flash. */
 {
  static uint32_t lastWipMs = 0;
  if (_storageMgr->h5WipPending( ) && timeSince(lastWipMs, H5_WIP_RETRY_MS)) {
   if (!_storageMgr->isHeavyTaskLocked( ) && !isUserInteracting( )
       && _storageMgr->isH5Active( )) {
    lastWipMs = millis( );
    _storageMgr->flushWipV5( );
   }
  }
 }

 watchdog_update( );

 /* ── System status: update data every 1s when screen is active ── */
 {
 static uint32_t lastStatusPush = 0;
 if (_displayMgr->getUiMode( ) == MODE_SETTINGS_STATUS
 && timeSince(lastStatusPush, 1000)) {
 lastStatusPush = millis( );

 static SystemStatusData sd;
 memset(&sd, 0, sizeof(sd));

 /* System */
 sd.uptimeSec = millis( ) / 1000;
 sd.heapFree = rp2040.getFreeHeap( );
 sd.heapTotal = rp2040.getTotalHeap( );
 sd.boardTemp = analogReadTemp( );

 /* Flash: uses WebManager cache (updated every 10s) */
 sd.flashUsed = _webMgr->getCachedFlashUsed( );
 sd.flashTotal = _webMgr->getCachedFlashTotal( );

 SystemConfig& cfg = _storageMgr->getConfig( );
 safeCopy(sd.deviceName, cfg.deviceName, sizeof(sd.deviceName));
 snprintf(sd.fwVersion, sizeof(sd.fwVersion), "%s", SIMUT_VERSION);
 sd.timezone = cfg.timezoneOffset;

 /* Network */
 sd.wifiConnected = _netMgr->isConnected( );
 sd.rssi = _netMgr->getRssi( );
 sd.ntpSynced = _netMgr->isTimeSynced( );
 { char _b[16]; _netMgr->getIpAddress(_b, sizeof(_b)); safeCopy(sd.ip, _b, sizeof(sd.ip)); }
 { char _b[18]; _netMgr->getMacAddress(_b, sizeof(_b)); safeCopy(sd.mac, _b, sizeof(sd.mac)); }
 safeCopy(sd.ssid, cfg.wifiSsid, sizeof(sd.ssid));
 safeCopy(sd.ntpServer, cfg.ntpServer, sizeof(sd.ntpServer));

 /* Telemetry */
 sd.telPending = _telemetryMgr->getPendingEstimate( );
 sd.mqttConnected = _telemetryMgr->isMqttConnected( );
 sd.telTransport = cfg.telTransport;
 sd.telInterval = cfg.telInterval;
 safeCopy(sd.telServer, cfg.telServer, sizeof(sd.telServer));

 /* Sensors */
 sd.activeSensors = 0;
 const auto& sensors = _sensorMgr->getRuntimeSensors( );
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfg.sensors[i].active) sd.activeSensors++;
 }

 _displayMgr->updateSystemStatus(sd);
 }
 }

 watchdog_update( );

 if (_waitingScan && !isUserInteracting( )) {
 processBackgroundScan( );
 }

 watchdog_update( );

 core0Yield( );

 watchdog_update( );

 TRACE_MOD(0, MOD_IDLE);
}

/* =========================================================================== */
/* CLI COMMAND EXECUTION */
/* =========================================================================== */
