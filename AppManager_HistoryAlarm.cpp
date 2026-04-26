/**
 * @file    AppManager_HistoryAlarm.cpp
 * @brief   History logging, min/max computation, alarm monitoring, live display updates.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "SystemDefs.h"
#include <LittleFS.h>
#include <time.h>

void AppManager::pauseDisplayForFlash(bool lock) {
    /* F-LOCKOUT-STUCK: durante quiet mode cooperativo, Core 1 está congelado
     * em loop RAM-only com IRQs OFF. Tentar multicore_lockout IRQ-based aqui
     * trava para sempre (IRQ nunca é handled) até WDT matar Core 0.
     * Lockout é desnecessário neste cenário porque Core 1 já não toca flash.
     * Early-return torna requestFsLock (LogManager) e enterFlashSafeMode
     * (StorageManager) no-ops quando já estamos dentro do quiet mode. */
    if (_displayMgr.isInQuietMode()) return;
    _displayMgr.pauseRendering(lock);
}

bool AppManager::requestDisplayQuietMode(bool enable) {
    if (enable) return _displayMgr.requestQuietMode();   /* default 15s */
    _displayMgr.releaseQuietMode();
    return true;
}

void AppManager::refreshSelectedSlot() {
    SystemConfig &cfg = _storageMgr.getConfig();
    const auto& sensors = _sensorMgr.getRuntimeSensors();
    bool found = false;

    if (_currentSensorIdx < 10) {
        if (cfg.sensors[_currentSensorIdx].active) {
            uint8_t targetGpio = cfg.sensors[_currentSensorIdx].gpio;
            for (const auto &s : sensors) {
                if (s.config.gpio != 10 && s.config.gpio == targetGpio) {
                    _displayMgr.setSlotData(s.avgValue1, !s.inErrorState, _currentSensorIdx, String(s.config.friendlyName));
                    found = true; break;
                }
            }
        }
    } else if (_currentSensorIdx == 10) {
        _displayMgr.setSlotData(analogReadTemp(), true, 10, "Board (Internal)"); found = true;
    }

    if (!found) _displayMgr.setSlotData(NAN, false, _currentSensorIdx, "Empty / Inactive");
}

/**
 * @brief Push current sensor data and system status to the display shared state.
 * System status (time, RSSI, pending count) updates every cycle.
 * Sensor data updates only when new readings are available.
 */
void AppManager::updateLiveDisplay() {


    {
        String dateStr = _netMgr.getFormattedDate();
        dateStr.replace("/20", "/");
        String fullStatus = dateStr + " - " + _netMgr.getFormattedTime();
        _displayMgr.setSystemStatus(_netMgr.getRssi(), false, fullStatus);


        static uint32_t lastPendingRefresh = 0;
        if (timeSince(lastPendingRefresh, 10000)) {
            _telemetryMgr.refreshPendingCount();
            lastPendingRefresh = millis();
        }
        _displayMgr.setTelemetryPending(_telemetryMgr.getPendingEstimate());

        /* Min/max do dia (preload CSV + leituras acumuladas em tempo real) */
        float ambMinT = (_cachedMin[10] < 999.0f)  ? _cachedMin[10] : NAN;
        float ambMaxT = (_cachedMax[10] > -999.0f)  ? _cachedMax[10] : NAN;
        float ambMinH = (_cachedHumMin  < 999.0f)   ? _cachedHumMin  : NAN;
        float ambMaxH = (_cachedHumMax  > -999.0f)   ? _cachedHumMax  : NAN;
        _displayMgr.setAmbientMinMax(ambMinT, ambMaxT, ambMinH, ambMaxH);

        /* Min/max do slot ativo */
        int slotIdx = _currentSensorIdx;
        if (slotIdx >= 0 && slotIdx < 10) {
            float sMinT = (_cachedMin[slotIdx] < 999.0f)  ? _cachedMin[slotIdx] : NAN;
            float sMaxT = (_cachedMax[slotIdx] > -999.0f)  ? _cachedMax[slotIdx] : NAN;
            _displayMgr.setSlotMinMax(sMinT, sMaxT);
        }
    }


    if (_sensorMgr.hasNewReadings()) {
        const auto& sensors = _sensorMgr.getRuntimeSensors();
        SystemConfig &cfg = _storageMgr.getConfig();

        for (const auto &s : sensors) {
            if (s.config.gpio == 10) _displayMgr.setAmbientData(s.avgValue1, s.avgValue2, !s.inErrorState);
            else if (_currentSensorIdx < 10 && cfg.sensors[_currentSensorIdx].active && cfg.sensors[_currentSensorIdx].gpio == s.config.gpio) {
                _displayMgr.setSlotData(s.avgValue1, !s.inErrorState, _currentSensorIdx, String(s.config.friendlyName));
            }
        }

        if (_currentSensorIdx == 10) _displayMgr.setSlotData(analogReadTemp(), true, 10, "Board (Internal)");
    }
}

/**
 * @brief Pre-load daily Min/Max values from binary history for fast display.
 * Runs during boot to avoid flash I/O competition with the dashboard.
 * Uses ReadLock (no Core 1 pause) with 5-second budget limit.
 */
void AppManager::preloadMinMax() {
    time_t now = _netMgr.getEpoch();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char path[40];
    snprintf(path, sizeof(path), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

    File f;
    _storageMgr.enterFlashReadLock();
    bool fileExists = LittleFS.exists(path);
    if (fileExists) f = LittleFS.open(path, "r");
    _storageMgr.exitFlashReadLock();

    if (fileExists && f) {
        uint32_t _preloadBudget = millis();
        bool hasMore = true;

        while (hasMore) {
            if (timeSince(_preloadBudget, 5000)) {
                LOG_CODE(LOG_WARN, "APP", APP_PRELOAD_BUDGET, 0, "");
                { StorageManager::ReadGuard rg(&_storageMgr); f.close(); }
                LOG_CODE(LOG_INFO, "APP", APP_CACHE_MINMAX_PARTIAL, 0, "");
                return;
            }

            /* Lê batch de 20 registros binários */
            _storageMgr.enterFlashReadLock();
            BinaryHistoryRecord batch[20];
            int count = 0;
            while (f.available() >= HISTORY_RECORD_SIZE && count < 20) {
                if (f.read((uint8_t*)&batch[count], HISTORY_RECORD_SIZE)
                    == HISTORY_RECORD_SIZE)
                {
                    count++;
                }
            }
            hasMore = (f.available() >= HISTORY_RECORD_SIZE);
            _storageMgr.exitFlashReadLock();

            /* Processa batch fora do lock */
            for (int b = 0; b < count; b++) {
                const BinaryHistoryRecord& rec = batch[b];

                float ambT = BinaryHistoryRecord::i16ToFloat(rec.ambientTemp);
                if (!isnan(ambT)) {
                    if (ambT < _cachedMin[10]) _cachedMin[10] = ambT;
                    if (ambT > _cachedMax[10]) _cachedMax[10] = ambT;
                }

                float ambH = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);
                if (!isnan(ambH)) {
                    if (ambH < _cachedHumMin) _cachedHumMin = ambH;
                    if (ambH > _cachedHumMax) _cachedHumMax = ambH;
                }

                for (int i = 0; i < MAX_SENSORS; i++) {
                    float v = BinaryHistoryRecord::i16ToFloat(rec.sensors[i]);
                    if (!isnan(v)) {
                        if (v < _cachedMin[i]) _cachedMin[i] = v;
                        if (v > _cachedMax[i]) _cachedMax[i] = v;
                    }
                }
            }

            feedWdt();
            delay(2);
        }

        _storageMgr.enterFlashReadLock();
        f.close();
        _storageMgr.exitFlashReadLock();
    }

    /* Salvar snapshot do preload (somente dados do CSV, sem leitura em tempo real) */
    for (int i = 0; i < MINMAX_SLOT_COUNT; i++) {
        _preloadMin[i] = _cachedMin[i];
        _preloadMax[i] = _cachedMax[i];
    }
    _preloadHumMin = _cachedHumMin;
    _preloadHumMax = _cachedHumMax;

    LOG_CODE(LOG_INFO, "APP", APP_CACHE_MINMAX_FULL, 0, "");
}

void AppManager::processHistoryLogging() {
    _lastHistoryTime = millis();
    time_t now = _netMgr.getEpoch();

    if (now > 1600000000) {
        const auto& sensors = _sensorMgr.getRuntimeSensors();
        SystemConfig &cfg = _storageMgr.getConfig();

        /* ── Monta registro binário ── */
        BinaryHistoryRecord rec;
        rec.clear();
        rec.epoch = (uint32_t)now;

        /* Sensor ambiente (DHT22 no GPIO 10) */
        float ambT = NAN, ambH = NAN;
        for (const auto &s : sensors) {
            if (s.config.gpio == 10 && !s.inErrorState) {
                ambT = s.avgValue1;
                ambH = s.avgValue2;

                if (!isnan(ambT)) {
                    if (ambT < _cachedMin[10]) _cachedMin[10] = ambT;
                    if (ambT > _cachedMax[10]) _cachedMax[10] = ambT;
                    if (ambT < _preloadMin[10]) _preloadMin[10] = ambT;
                    if (ambT > _preloadMax[10]) _preloadMax[10] = ambT;
                }
                if (!isnan(ambH)) {
                    if (ambH < _cachedHumMin) _cachedHumMin = ambH;
                    if (ambH > _cachedHumMax) _cachedHumMax = ambH;
                    if (ambH < _preloadHumMin) _preloadHumMin = ambH;
                    if (ambH > _preloadHumMax) _preloadHumMax = ambH;
                }
                break;
            }
        }

        rec.ambientTemp = BinaryHistoryRecord::floatToI16(ambT);
        rec.ambientHum  = BinaryHistoryRecord::floatToI16(ambH);

        /* Sensores DS18B20 (slots 0..9) */
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (cfg.sensors[i].active) {
                for (const auto &s : sensors) {
                    if (s.config.gpio == cfg.sensors[i].gpio && !s.inErrorState) {
                        float v = s.avgValue1;
                        if (!isnan(v)) {
                            rec.sensors[i] = BinaryHistoryRecord::floatToI16(v);
                            if (v < _cachedMin[i]) _cachedMin[i] = v;
                            if (v > _cachedMax[i]) _cachedMax[i] = v;
                            if (v < _preloadMin[i]) _preloadMin[i] = v;
                            if (v > _preloadMax[i]) _preloadMax[i] = v;
                        }
                        break;
                    }
                }
            }
        }

        if (_storageMgr.writeHistoryEntry(rec)) {
            LOG_CODE(LOG_INFO, "HIST", APP_HISTORY_SAVED, 0, "");
            _telemetryMgr.notifyNewRecord();
        }
    }

    /* #10: Log periódico de heap — só quando baixa ou 1x/hora (evita rotação prematura) */
    {
        uint32_t heapFree = rp2040.getFreeHeap();
        static uint32_t lastFullHeapLog = 0;

        if (heapFree < 32768 || timeSince(lastFullHeapLog, 3600000)) {
            char heapMsg[48];
            snprintf(heapMsg, sizeof(heapMsg), "Heap: %lu free / %lu total",
                     (unsigned long)heapFree,
                     (unsigned long)rp2040.getTotalHeap());
            LOG_CODE(LOG_INFO, "SYS", APP_HEAP_REPORT, (int)(heapFree/1024), heapMsg);
            lastFullHeapLog = millis();
        }

        /* Alerta quando heap cai abaixo de 16KB (margem para WiFi+TLS) */
        if (heapFree < 16384) {
            LOG_CODE(LOG_WARN, "SYS", SYS_HEAP_LOW, (int)(heapFree/1024), "");
        }
    }
}

void AppManager::openStatsScreen(int sensorId) {
    /**
     * IMPORTANTE: pkg deve ser static para evitar stack overflow.
     * GraphDataPackage tem ~3.2KB — excede a stack do RP2040 (~4KB).
     * Mesmo padrão usado em renderGraphOptimized().
     */
    static GraphDataPackage pkg;
    memset(&pkg, 0, sizeof(GraphDataPackage));
    pkg.sensorIdx = sensorId;
    pkg.timeRange = 3;
    pkg.count = 0;
    pkg.idxMinTemp = -1;
    pkg.idxMaxTemp = -1;
    pkg.avgTemp  = NAN;
    pkg.stdTemp  = NAN;
    pkg.deltaTemp = NAN;
    pkg.avgHum   = NAN;
    pkg.stdHum   = NAN;
    pkg.deltaHum = NAN;

    int cacheIdx = (sensorId == -1) ? 10 : sensorId;
    if (cacheIdx < 0 || cacheIdx > 10) cacheIdx = 10;

    pkg.minVal = _cachedMin[cacheIdx];
    pkg.maxVal = _cachedMax[cacheIdx];

    if (pkg.minVal == 1000.0f) pkg.minVal = 0.0f;
    if (pkg.maxVal == -1000.0f) pkg.maxVal = 0.0f;

    float humMin = _cachedHumMin;
    float humMax = _cachedHumMax;
    if (humMin == 1000.0f) humMin = 0.0f;
    if (humMax == -1000.0f) humMax = 0.0f;

    SystemConfig &cfg = _storageMgr.getConfig();
    pkg.hasHumidity = (sensorId == -1);

    if (sensorId == -1) {
        snprintf(pkg.title, sizeof(pkg.title), "%s", _displayMgr.tr(TR_AMBIENT));
        snprintf(pkg.hwId, sizeof(pkg.hwId), "AMB");
        snprintf(pkg.rom, sizeof(pkg.rom), "INTERNAL-DHT");
    } else if (sensorId == 10) {
        snprintf(pkg.title, sizeof(pkg.title), "Board Temp");
        snprintf(pkg.hwId, sizeof(pkg.hwId), "SYS");
        snprintf(pkg.rom, sizeof(pkg.rom), "RP2040-ADC");
    } else {
        if (cfg.sensors[sensorId].active) {
            safeCopy(pkg.title, cfg.sensors[sensorId].friendlyName, sizeof(pkg.title));
            safeCopy(pkg.hwId, cfg.sensors[sensorId].hwId, sizeof(pkg.hwId));
            snprintf(pkg.rom, sizeof(pkg.rom), "%02X%02X%02X%02X%02X%02X%02X%02X",
                cfg.sensors[sensorId].rom[0], cfg.sensors[sensorId].rom[1],
                cfg.sensors[sensorId].rom[2], cfg.sensors[sensorId].rom[3],
                cfg.sensors[sensorId].rom[4], cfg.sensors[sensorId].rom[5],
                cfg.sensors[sensorId].rom[6], cfg.sensors[sensorId].rom[7]);
        } else {
            snprintf(pkg.title, sizeof(pkg.title), "Sensor %d", sensorId + 1);
            snprintf(pkg.hwId, sizeof(pkg.hwId), "--");
            snprintf(pkg.rom, sizeof(pkg.rom), "N/A");
        }
    }
    pkg.title[31] = '\0'; pkg.hwId[15] = '\0'; pkg.rom[23] = '\0';

    _displayMgr.showStats(pkg, humMin, humMax);
}
void AppManager::checkAlarmConditions() {
    const auto& sensors = _sensorMgr.getRuntimeSensors();
    SystemConfig &cfg = _storageMgr.getConfig();
    bool anyAlarm    = false;
    uint16_t mask    = 0;
    int8_t firstSlot = -1;


    bool ambTempAlarm = false;
    bool ambHumAlarm  = false;
    if (cfg.ambientSensor.alarmsActive) {
        for (const auto &s : sensors) {
            if (s.config.gpio != 10 || s.inErrorState) continue;
            if (!isnan(s.avgValue1)) {
                if (s.avgValue1 < cfg.ambientSensor.tempMin ||
                    s.avgValue1 > cfg.ambientSensor.tempMax) {
                    ambTempAlarm = true;
                    anyAlarm = true;
                }
            }
            if (s.type == TYPE_DHT22 && !isnan(s.avgValue2)) {
                if (s.avgValue2 < cfg.ambientSensor.humMin ||
                    s.avgValue2 > cfg.ambientSensor.humMax) {
                    ambHumAlarm = true;
                    anyAlarm = true;
                }
            }
            break;
        }
    }


    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!cfg.sensors[i].active || !cfg.sensors[i].alarmsActive) continue;
        uint8_t targetGpio = cfg.sensors[i].gpio;

        for (const auto &s : sensors) {
            if (s.config.gpio != targetGpio || s.inErrorState) continue;

            bool tripped = false;

            if (!isnan(s.avgValue1)) {
                if (s.avgValue1 < cfg.sensors[i].tempMin ||
                    s.avgValue1 > cfg.sensors[i].tempMax) {
                    tripped = true;
                }
            }

            if (!tripped && s.type == TYPE_DHT22 && !isnan(s.avgValue2)) {
                if (s.avgValue2 < cfg.sensors[i].humMin ||
                    s.avgValue2 > cfg.sensors[i].humMax) {
                    tripped = true;
                }
            }

            if (tripped) {
                mask |= (1 << i);
                anyAlarm = true;
                if (firstSlot < 0) firstSlot = i;
            }
            break;
        }
    }


    bool silenced = _displayMgr.isAlarmSilenced();

    if (anyAlarm && !_soundMgr.isAlarming() && !silenced) {

        _soundMgr.startAlarm();
        if (firstSlot >= 0) {
            _currentSensorIdx = firstSlot;
            refreshSelectedSlot();
        }
        _displayMgr.setAlarmState(mask, firstSlot, ambTempAlarm, ambHumAlarm);
        LOG_CODE(LOG_WARN, "APP", APP_ALARM_TRIGGERED, 0, "");
    } else if (anyAlarm && (_soundMgr.isAlarming() || silenced)) {

        _displayMgr.setAlarmState(mask, -1, ambTempAlarm, ambHumAlarm);
    } else if (!anyAlarm && (_soundMgr.isAlarming() || silenced)) {

        _soundMgr.stopAlarm();
        _displayMgr.setAlarmState(0, -1, false, false);

        if (silenced) {
            _displayMgr.setAlarmSilenced(false, 0);
            LOG_CODE(LOG_INFO, "APP", APP_ALARM_SILENCE_CANCEL, 0, "");
        }
        LOG_CODE(LOG_INFO, "APP", APP_ALARM_CLEARED, 0, "");
    }
}
