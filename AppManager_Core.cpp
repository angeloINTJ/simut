/**
 * @file    AppManager_Core.cpp
 * @brief   Core infrastructure: constructor, display health, time sync, user interaction.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "SystemDefs.h"
#include <hardware/watchdog.h>

extern AppManager app;



AppManager::AppManager() {
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

/* =========================================================================== */
/*                               BOOT SEQUENCE                               */
/* =========================================================================== */
/**
 * @brief Complete system initialization in deterministic order.
 *
 * Boot flow:
 *   1. Display + Core 1 launch
 *   2. AP mode hold detection (3.5s touch)
 *   3. Filesystem mount + log manager
 *   4. CLI + Bluetooth authentication
 *   5. Theme, language, sound, touch calibration
 *   6. Sensor initialization + calibration
 *   7. WiFi connection (or AP mode)
 *   8. Telemetry + web server
 *   9. Sensor warm-up + NTP correction
 *  10. Dashboard launch
bool AppManager::isDisplayAlive() {


    if (_storageMgr.lockHeavyTask() == false) return true;
    _storageMgr.unlockHeavyTask();

    uint32_t now = millis();
    uint32_t beat = _displayMgr.getHeartbeat();

    if (beat >= now) return true;
    return (now - beat < 5000);
}

void AppManager::restartDisplayCore() { _displayMgr.startCore1(); }


void AppManager::handleTimeSync(uint32_t bootTs, int32_t delta) {
    if (!_storageMgr.lockHeavyTask()) {

        return;
    }
    _pendingTimeSync = false;
    LOG_CODE(LOG_INFO, "APP", APP_NTP_CORRECTING, delta, String(TRL("NTP correction: ", "Correcao NTP: ")) + delta + "s");
    _storageMgr.correctProvisionalTimestamps(bootTs, delta);
    LOG_CODE(LOG_INFO, "APP", APP_NTP_CORRECTED, 0, "");
    _storageMgr.unlockHeavyTask();

    /*
     * Invalida todo o cache de gráficos 7d pré-carregado no boot.
     * Os timestamps dos registros foram corrigidos pelo delta NTP,
     * mas os gráficos em cache ainda usam os dados antigos.
     * Serão recarregados sob demanda ou no próximo refresh de 6h.
     */
    for (int i = 0; i < MAX_SENSORS + 2; i++) {
        _graphCache[i].valid = false;
    }
    for (int r = 0; r < 5; r++) {
        _sensorCache[r].valid = false;
    }
    _sensorCacheId = -99;
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_INVALIDATED, 0, "");
}
bool AppManager::isUserInteracting() const {
    uint32_t lastTouch = _displayMgr.getLastTouchTimestamp();
    if (lastTouch == 0) return false;
    return !timeSince(lastTouch, TOUCH_PRIORITY_MS);
}

/**
 * Fase 5: Chamado uma vez na transição touch-active→touch-free. Fecha a
 * janela de exposição dos buffers em RAM (log/hist/cursor) disparando um
 * flush coordenado. Ordem importa: logs primeiro (menor, mais crítico
 * para auditoria), depois hist, depois cursor (coalesce de telemetria).
 * Um WDT window estendido cobre os 3 writes em série.
 */
void AppManager::onTouchReleased() {
    LogManager::WdtWindow _wdt(30000);
    LogManager::instance().flushPendingIfAny();
    watchdog_update();
    _storageMgr.flushPendingHist();
    watchdog_update();
    _storageMgr.flushCursorIfDirty();
}
