/**
 * @file AppManager_Core.cpp
 * @brief Core infrastructure: constructor, display health, time sync, user interaction.
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
#include "NetworkManager.h"
#include "SensorManager.h"
#include "SoundManager.h"
#include "StorageManager.h"
#include "SyslogManager.h"
#include "SystemDefs.h"
#include "TelemetryManager.h"
#include "WebManager.h"
#include <hardware/watchdog.h>
#include <memory>

extern AppManager app;



AppManager::AppManager( )
 : _sensorMgr(std::make_unique<SensorManager>( ))
 , _storageMgr(std::make_unique<StorageManager>( ))
 , _cmdMgr(std::make_unique<CommandManager>( ))
 , _displayMgr(std::make_unique<DisplayManager>( ))
 , _netMgr(std::make_unique<NetworkManager>( ))
 , _webMgr(std::make_unique<WebManager>( ))
 , _telemetryMgr(std::make_unique<TelemetryManager>( ))
 , _soundMgr(std::make_unique<SoundManager>( ))
 , _syslogMgr(std::make_unique<SyslogManager>( ))
{
 for(int i = 0; i < MINMAX_SLOT_COUNT; i++) {
 _cachedMin[i] = 1000.0f;
 _cachedMax[i] = -1000.0f;
 _preloadMin[i] = 1000.0f;
 _preloadMax[i] = -1000.0f;
 }
 for(int i = 0; i < MINMAX_SLOT_COUNT; i++) {
 _cachedHumMin[i] = 1000.0f;
 _cachedHumMax[i] = -1000.0f;
 _preloadHumMin[i] = 1000.0f;
 _preloadHumMax[i] = -1000.0f;
 }
}

/* Destructor defined here so that std::unique_ptr<T> can instantiate
 * the dtor of each T in a TU where T is complete (all 8 manager headers
 * are included above). Required for compilation with forward decl in header. */
AppManager::~AppManager( ) = default;

/* =========================================================================== */
/* BOOT SEQUENCE */
/* =========================================================================== */
bool AppManager::isDisplayAlive( ) {


 if (_storageMgr->lockHeavyTask( ) == false) return true;
 _storageMgr->unlockHeavyTask( );

 uint32_t now = millis( );
 uint32_t beat = _displayMgr->getHeartbeat( );

 if (beat >= now) return true;
 return (now - beat < 5000);
}

void AppManager::restartDisplayCore( ) { _displayMgr->startCore1( ); }


void AppManager::handleTimeSync(uint32_t bootTs, int32_t delta) {
 if (!_storageMgr->lockHeavyTask( )) {

 return;
 }
 _pendingTimeSync = false;
 /* Level, not a new code: the persisted record carries only code + context, so
  * a WARN is what survives into `show system log` without costing a string in
  * five tables. Context saturates at int16 — a pegged value is itself the
  * signal that the correction was enormous. The record's own epoch is already
  * corrected, so the seed that caused it is (epoch - delta). */
 const bool suspect = (delta > NTP_SUSPECT_DELTA_S) || (delta < -NTP_SUSPECT_DELTA_S);
 const int32_t ctxClamped = delta > 32767 ? 32767 : (delta < -32768 ? -32768 : delta);
 LOG_CODE(suspect ? LOG_WARN : LOG_INFO, "APP", APP_NTP_CORRECTING, ctxClamped,
          String(TRL("NTP correction: ")) + delta + "s");

 /* V5 makes this possible again. Under V4 the records were a
  * variable-length stream with no addressable timestamps, so the
  * correction was announced and then did nothing: everything written
  * before NTP came up kept the provisional clock forever.
  *
  * In V5 the only thing that carries absolute time is t0 in each DATA
  * header — a block's interior is relative to it and SCHEMA has no time
  * at all (§7.3). So the fix is a stream-rewrite that touches 4 bytes and
  * a CRC per block, bounded to blocks this boot wrote: records from an
  * earlier session had a clock that was already right. */
 int32_t blocks = 0;
 if (delta != 0 && bootTs > 0) {
  blocks = _storageMgr->shiftHistoryTimeV5(delta, "", bootTs);
  /* A block that straddles midnight lands in yesterday's file. */
  const String yesterday =
      _storageMgr->getHistoryFileNameV5((uint32_t)(bootTs > 86400 ? bootTs - 86400 : 0));
  if (blocks >= 0 && yesterday != _storageMgr->getHistoryFileNameV5( )) {
   const int32_t more = _storageMgr->shiftHistoryTimeV5(delta, yesterday, bootTs);
   if (more > 0) blocks += more;
  }
 }
 LOG_CODE(blocks < 0 ? LOG_WARN : LOG_INFO, "APP", APP_NTP_CORRECTED, (int)blocks, "");
 _storageMgr->unlockHeavyTask( );

 LOG_CODE(LOG_INFO, "APP", APP_CACHE_INVALIDATED, 0, "");
}
bool AppManager::isUserInteracting( ) const {
 uint32_t lastTouch = _displayMgr->getLastTouchTimestamp( );
 if (lastTouch == 0) return false;
 return !timeSince(lastTouch, TOUCH_PRIORITY_MS);
}

/**
 * Called once on the touch-active→touch-free transition. Closes the
 * exposure window for RAM buffers (log/hist/cursor) by triggering a
 * coordinated flush. Order matters: logs first (smallest, most critical
 * for audit), then history, then cursor (telemetry coalescing).
 * An extended WDT window covers the 3 writes in series.
 */
void AppManager::onTouchReleased( ) {
 LogManager::WdtWindow _wdt(30000);
 LogManager::instance( ).flushPendingIfAny( );
 watchdog_update( );
 _storageMgr->flushHistoryBatchIfDue( );
 watchdog_update( );
 _storageMgr->flushCursorIfDirty( );
}
