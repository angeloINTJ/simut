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
#include "MemoryPool.h"   /* F-MEM-SHAREDPOOL */
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
    LOG_CODE(LOG_INFO, "APP", APP_NTP_CORRECTING, delta, String(TRL("NTP correction: ")) + delta + "s");
    _storageMgr.correctProvisionalTimestamps(bootTs, delta);
    LOG_CODE(LOG_INFO, "APP", APP_NTP_CORRECTED, 0, "");
    _storageMgr.unlockHeavyTask();

    /*
     * Invalida todo o cache de gráficos pós-correção NTP. Sob a nova
     * arquitetura lazy, caches só existem se o user já abriu graph.
     * Sem alocação: nada a invalidar, log no-op.
     */
    if (_graphCachesAllocated) {
        for (int i = 0; i < MAX_SENSORS + 2; i++) _graphCache[i].valid = false;
        for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;
        _sensorCacheId = -99;
    }
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_INVALIDATED, 0, "");
}

/* F-MEM-SHAREDPOOL: aloca graph caches no MemoryPool compartilhado
 * (BSS, endereço fixo). Pool serve graph OU telemetry, mutuamente
 * exclusivos. Sem fragmentação possível — alloc só falha se pool
 * está sendo usado pela telemetria neste exato momento. */
bool AppManager::ensureGraphCachesAllocated() {
    if (_graphCachesAllocated) return true;

    constexpr size_t TOTAL = (MAX_SENSORS + 2 + 5) * sizeof(GraphCacheEntry);
    static_assert(TOTAL <= MemoryPool::SIZE, "graph caches > pool size");

    uint8_t* buf = MemoryPool::tryClaim(MemoryPool::OWNER_GRAPH);
    if (!buf) {
        /* Pool busy com telemetria (POST in-flight). Render direto. */
        LOG_CODE(LOG_WARN, "APP", SYS_HEAP_LOW,
                 (int)(rp2040.getFreeHeap() / 1024),
                 "Pool busy (telemetry); render direto");
        return false;
    }
    memset(buf, 0, TOTAL);
    _graphCache  = (GraphCacheEntry*)buf;
    _sensorCache = _graphCache + (MAX_SENSORS + 2);
    _sensorCacheId = -99;
    _graphCachesAllocated = true;
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_GRAPH_STARTED,
             (int)(rp2040.getFreeHeap() / 1024),
             "Graph caches no pool");
    return true;
}

/* Libera caches se em DASHBOARD AND lastTouch > 5s atrás. */
void AppManager::freeGraphCachesIfIdle() {
    if (!_graphCachesAllocated) return;
    if (_displayMgr.getUiMode() != MODE_DASHBOARD) return;
    uint32_t lastTouch = _displayMgr.getLastTouchTimestamp();
    /* lastTouch == 0 (boot, sem touch ainda) também conta como idle. */
    if (lastTouch != 0 && (millis() - lastTouch) < 5000) return;

    _graphCache = nullptr;
    _sensorCache = nullptr;
    _sensorCacheId = -99;
    _graphCachesAllocated = false;
    MemoryPool::release(MemoryPool::OWNER_GRAPH);
    LOG_CODE(LOG_INFO, "APP", APP_CACHE_GRAPH_DONE,
             (int)(rp2040.getFreeHeap() / 1024),
             "Graph caches liberados (dashboard idle)");
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
