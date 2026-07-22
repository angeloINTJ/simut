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
 TRACE_BEAT(0);
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
 if (queued.type != CMD_UNKNOWN) executeCommand(queued);
 if (!_waitingScan) _cmdMgr->printPrompt( );
 _cliDropNotified = false;
 }

 if (_cmdMgr->processInput(cmd)) {
 if (cmd.type != CMD_UNKNOWN) {
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


 if (timeSince(_lastHistoryTime, _storageMgr->getHistoryIntervalMin( ) * 60000UL)) {
 if (!_storageMgr->isHeavyTaskLocked( ) && !isUserInteracting( )) {
 processHistoryLogging( );
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
