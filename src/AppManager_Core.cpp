/**
 * @file AppManager_Core.cpp
 * @brief Core infrastructure: constructor, display health, time sync, user interaction.
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
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
{
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

/* EXT-005: dtor definido aqui para que std::unique_ptr<T> consiga instanciar
 * o dtor de cada T num TU onde T é completo (todos os 8 manager headers
 * estão acima). Necessário para compilação com forward decl no header. */
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
 LOG_CODE(LOG_INFO, "APP", APP_NTP_CORRECTING, delta, String(TRL("NTP correction: ")) + delta + "s");
 _storageMgr->correctProvisionalTimestamps(bootTs, delta);
 LOG_CODE(LOG_INFO, "APP", APP_NTP_CORRECTED, 0, "");
 _storageMgr->unlockHeavyTask( );

 /* graph caches eliminados em alpha14 — nada a invalidar.
 * Próximo render lerá do flash com o timestamp corrigido. */
 LOG_CODE(LOG_INFO, "APP", APP_CACHE_INVALIDATED, 0, "");
}
bool AppManager::isUserInteracting( ) const {
 uint32_t lastTouch = _displayMgr->getLastTouchTimestamp( );
 if (lastTouch == 0) return false;
 return !timeSince(lastTouch, TOUCH_PRIORITY_MS);
}

/**
 * Chamado uma vez na transição touch-active→touch-free. Fecha a
 * janela de exposição dos buffers em RAM (log/hist/cursor) disparando um
 * flush coordenado. Ordem importa: logs primeiro (menor, mais crítico
 * para auditoria), depois hist, depois cursor (coalesce de telemetria).
 * Um WDT window estendido cobre os 3 writes em série.
 */
void AppManager::onTouchReleased( ) {
 LogManager::WdtWindow _wdt(30000);
 LogManager::instance( ).flushPendingIfAny( );
 watchdog_update( );
 _storageMgr->flushPendingHist( );
 watchdog_update( );
 _storageMgr->flushCursorIfDirty( );
}
