/**
 * @file    LogManager.cpp
 * @brief   Implementation of LogManager — log output, flash persistence, and crash forensics.
 * @details Implements dual-format logging (syslog-style serial + CSV flash),
 * automatic log rotation (500 lines max), ring buffer for logs during
 * heavy tasks or touch interactions, and cross-core health monitoring
 * with configurable timeout thresholds and watchdog-triggered reboot.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.4.8
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "LogManager.h"
#include <LittleFS.h>
#include <time.h>
#include "pico/multicore.h"
#include <hardware/watchdog.h>
#include <stdio.h>

/* Black-box profiler state — tracks per-core activity for crash forensics. */
volatile uint32_t _coreHeartbeat[2] = {0, 0};
volatile uint8_t _coreModule[2] = {0, 0};
volatile uint32_t _moduleStartTime[2] = {0, 0};
volatile bool _corePaused[2] = {false, false};
volatile uint32_t _healthCheckEnabledAt = 0;

const char* MOD_NAMES[] = {"BOOT", "IDLE", "WIFI", "WEB_SERVER", "STORAGE_RD", "STORAGE_WR", "SENSOR", "TELEMETRY", "DISPLAY", "CLI"};

LogManager::LogManager() {
    mutex_init(&_logMutex);
    _saveToFile = false;
    _minSerialLevel = LOG_INFO;
    _currentLineCount = 0;
    _epochFn = nullptr;
}

void LogManager::setLockCallback(FlashLockCallback cb) { _lockCb = cb; }


void LogManager::setHeavyTaskChecker(bool (*fn)()) {
    _isHeavyTaskFn = fn;
    _heavyTaskCheckEnabled = (fn != nullptr);
}


void LogManager::setTouchPriorityChecker(bool (*fn)()) {
    _isTouchPriorityFn = fn;
}

void LogManager::setEpochSource(time_t (*fn)()) { _epochFn = fn; }

time_t LogManager::getEpochNow() {
    if (_epochFn) return _epochFn();
    time_t t = time(nullptr);
    if (t > 1600000000) return t;
    return 0;
}

String LogManager::uptimeString() {
    uint32_t sec = millis() / 1000;
    uint32_t d = sec / 86400; sec %= 86400;
    uint32_t h = sec / 3600;  sec %= 3600;
    uint32_t m = sec / 60;    sec %= 60;
    char buf[24];
    if (d > 0) snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu", d, h, m, sec);
    else       snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, sec);
    return String(buf);
}


void LogManager::requestFsLock(bool lock) {
    if (_lockCb) _lockCb(lock);
    if (lock) delay(1);
}

void LogManager::begin(bool saveToFile, LogLevel minSerialLevel) {
    _saveToFile = saveToFile;
    _minSerialLevel = minSerialLevel;

    if (_saveToFile) {
        requestFsLock(true);

        /* Migração: remove logs CSV antigos do formato anterior */
        if (LittleFS.exists("/system.log")) LittleFS.remove("/system.log");
        if (LittleFS.exists("/system.old")) LittleFS.remove("/system.old");

        if (LittleFS.exists(LOG_FILE_CURRENT)) {
            _currentLineCount = countFileRecords(LOG_FILE_CURRENT);
        } else {
            _currentLineCount = 0;
        }
        requestFsLock(false);
    }
    performCrashAutopsy();
}

int LogManager::getCoreID() { return get_core_num(); }

uint16_t LogManager::countFileRecords(const char* filename) {
    File f = LittleFS.open(filename, "r");
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return (uint16_t)(sz / LOG_RECORD_SIZE);
}

const char* LogManager::getLevelString(LogLevel level) {
    switch (level) {
        case LOG_DEBUG: return "DBG";
        case LOG_INFO:  return "INF";
        case LOG_WARN:  return "WRN";
        case LOG_ERROR: return "ERR";
        case LOG_FATAL: return "FTL";
        default:        return "---";
    }
}


/* =========================================================================== */
/*                      LOG OUTPUT — SERIAL + FLASH CSV                      */
/* =========================================================================== */
/**
 * @brief Log a structured event with code, context value, and optional message.
 * Serial format: [timestamp][UP uptime][Core][Level][Tag] [Code] Message (ctx:N)
 * Flash CSV: epoch;millis;core;level;tag;code;ctx;message
 */
void LogManager::logCode(LogLevel level, const char* tag, LogCode code, int contextVal, String extraMsg) {
    if (level < _minSerialLevel && level < LOG_WARN) return;
    mutex_enter_blocking(&_logMutex);


    time_t epoch = getEpochNow();
    int core = get_core_num();

    if (epoch > 1600000000) {
        struct tm ti; localtime_r(&epoch, &ti);
        Serial.printf("[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        Serial.printf("[BOOT+%lus]", millis()/1000);
    }

    const char* desc = translateCode((uint16_t)code);
    Serial.printf("[UP %s][C%d][%s][%s] %s",
        uptimeString().c_str(), core, getLevelString(level), tag, desc);
    if (extraMsg.length() > 0) Serial.printf(": %s", extraMsg.c_str());
    if (contextVal != 0) Serial.printf(" (%d)", contextVal);
    Serial.println();


    if (_saveToFile) {
        CompactLogRecord rec;
        rec.epoch     = (uint32_t)epoch;
        rec.uptimeHr  = (uint16_t)(millis() / 3600000UL);
        rec.code      = (uint16_t)code;
        rec.context   = (int16_t)constrain(contextVal, -32767, 32767);
        rec.flags     = CompactLogRecord::packFlags((uint8_t)level, (uint8_t)core, tagStringToId(tag));
        rec.reserved  = 0;
        writeCompactToFlash(rec);
    }
    mutex_exit(&_logMutex);
}


void LogManager::log(LogLevel level, const char* tag, LogCode code, String msg) {
    if (level < _minSerialLevel && level < LOG_WARN) return;
    mutex_enter_blocking(&_logMutex);

    time_t epoch = getEpochNow();
    int core = get_core_num();

    if (epoch > 1600000000) {
        struct tm ti; localtime_r(&epoch, &ti);
        Serial.printf("[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        Serial.printf("[BOOT+%lus]", millis()/1000);
    }
    Serial.printf("[UP %s][C%d][%s][%s] %s\n",
        uptimeString().c_str(), core, getLevelString(level), tag, msg.c_str());

    if (_saveToFile && level >= LOG_INFO) {
        CompactLogRecord rec;
        rec.epoch     = (uint32_t)epoch;
        rec.uptimeHr  = (uint16_t)(millis() / 3600000UL);
        rec.code      = (uint16_t)code;
        rec.context   = 0;
        rec.flags     = CompactLogRecord::packFlags((uint8_t)level, (uint8_t)core, tagStringToId(tag));
        rec.reserved  = 0;
        writeCompactToFlash(rec);
    }
    mutex_exit(&_logMutex);
}


/**
 * @brief Write a CSV log line to LittleFS with intelligent buffering.
 *
 * During touch interactions or heavy tasks, logs are buffered in RAM
 * instead of written to flash (which would pause Core 1). The buffer
 * is flushed automatically on the next non-critical write.
 */
void LogManager::writeCompactToFlash(const CompactLogRecord& rec) {

    /* Durante interação de toque: bufferiza em RAM */
    if (_isTouchPriorityFn && _isTouchPriorityFn()) {
        int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
        if (idx < LOG_PENDING_MAX) {
            _pendingLogs[idx] = rec;
            __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
        } else {
            _pendingOverflow++;
        }
        return;
    }

    /* Durante tarefa pesada: bufferiza em RAM */
    if (_heavyTaskCheckEnabled && _isHeavyTaskFn && _isHeavyTaskFn()) {
        int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
        if (idx < LOG_PENDING_MAX) {
            _pendingLogs[idx] = rec;
            __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
        } else {
            _pendingOverflow++;
        }
        return;
    }

    flushPendingLogs();

    requestFsLock(true);

    if (_currentLineCount >= MAX_RECORDS_PER_FILE) {
        /* Fechar handle antes de rotacionar */
        if (_logFile) _logFile.close();

        if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
        if (LittleFS.exists(LOG_FILE_CURRENT)) LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
        _currentLineCount = 0;

        /* Registra a rotação como primeiro entry do novo arquivo (serial + flash) */
        Serial.println("[LOG] Log file rotated.");
        CompactLogRecord rotRec;
        rotRec.epoch     = (uint32_t)getEpochNow();
        rotRec.uptimeHr  = (uint16_t)(millis() / 3600000UL);
        rotRec.code      = SYS_STORAGE_ROTATE;
        rotRec.context   = MAX_RECORDS_PER_FILE;
        rotRec.flags     = CompactLogRecord::packFlags(LOG_INFO, get_core_num(), TAG_STO);
        rotRec.reserved  = 0;

        _logFile = LittleFS.open(LOG_FILE_CURRENT, "a");
        if (_logFile) { _logFile.write((const uint8_t*)&rotRec, LOG_RECORD_SIZE); _currentLineCount++; }
    }

    /* Handle persistente: abrir 1x, manter aberto entre writes */
    if (!_logFile) _logFile = LittleFS.open(LOG_FILE_CURRENT, "a");
    if (_logFile) {
        _logFile.write((const uint8_t*)&rec, LOG_RECORD_SIZE);
        _currentLineCount++;
    }

    requestFsLock(false);
}


/** @brief Flush buffered log entries that accumulated during heavy tasks. */
void LogManager::flushPendingLogs() {
    int count = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
    if (count == 0 && _pendingOverflow == 0) return;

    requestFsLock(true);

    for (int i = 0; i < count; i++) {
        if (_currentLineCount >= MAX_RECORDS_PER_FILE) {
            if (_logFile) _logFile.close();
            if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
            if (LittleFS.exists(LOG_FILE_CURRENT)) LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
            _currentLineCount = 0;

            CompactLogRecord rotRec;
            rotRec.epoch     = (uint32_t)getEpochNow();
            rotRec.uptimeHr  = (uint16_t)(millis() / 3600000UL);
            rotRec.code      = SYS_STORAGE_ROTATE;
            rotRec.context   = MAX_RECORDS_PER_FILE;
            rotRec.flags     = CompactLogRecord::packFlags(LOG_INFO, get_core_num(), TAG_STO);
            rotRec.reserved  = 0;

            _logFile = LittleFS.open(LOG_FILE_CURRENT, "a");
            if (_logFile) { _logFile.write((const uint8_t*)&rotRec, LOG_RECORD_SIZE); _currentLineCount++; }
        }

        if (!_logFile) _logFile = LittleFS.open(LOG_FILE_CURRENT, "a");
        if (_logFile) {
            _logFile.write((const uint8_t*)&_pendingLogs[i], LOG_RECORD_SIZE);
            _currentLineCount++;
        }
    }

    /* Registrar overflow se houve perda de entries */
    if (_pendingOverflow > 0) {
        Serial.printf("[LOG] WARN: %u log entries dropped (buffer full)\n", _pendingOverflow);
        _pendingOverflow = 0;
    }

    requestFsLock(false);
    __atomic_store_n(&_pendingCount, 0, __ATOMIC_RELEASE);
}


void LogManager::info(const char* tag, String msg)  { log(LOG_INFO,  tag, SYS_OK, msg); }
void LogManager::warn(const char* tag, String msg)  { log(LOG_WARN,  tag, SYS_OK, msg); }
void LogManager::error(const char* tag, String msg) { log(LOG_ERROR, tag, SYS_OK, msg); }
void LogManager::debug(const char* tag, String msg) { log(LOG_DEBUG, tag, SYS_OK, msg); }

void LogManager::setSaveToFile(bool enable) { _saveToFile = enable; }
void LogManager::setMinSerialLevel(LogLevel level) { _minSerialLevel = level; }


/* =========================================================================== */
/*                            BLACK BOX PROFILER                             */
/* =========================================================================== */
/** @brief Set the currently executing module for crash forensics. */
void LogManager::setModule(int core, uint8_t mod) {
    _coreModule[core] = mod;
    _moduleStartTime[core] = millis();
    _coreHeartbeat[core] = millis();
}

void LogManager::heartbeat(int core) {
    _coreHeartbeat[core] = millis();
}

void LogManager::setCorePaused(int core, bool paused) {
    _corePaused[core] = paused;
    if (!paused) {
        uint32_t now = millis();
        _coreHeartbeat[core] = now;
        _moduleStartTime[core] = now;
    }
}

/**
 * @brief Habilita o monitoramento cross-core após boot completo.
 *
 * Deve ser chamado no final do setup(), após todos os subsistemas
 * estarem inicializados e ambos os cores estarem em operação normal.
 * Força refresh de heartbeats para evitar detecção falsa de heartbeat
 * estagnado durante o boot.
 */
void LogManager::enableHealthCheck() {
    uint32_t now = millis();
    /* Força heartbeats frescos para ambos os cores */
    _coreHeartbeat[0] = now;
    _coreHeartbeat[1] = now;
    _moduleStartTime[0] = now;
    _moduleStartTime[1] = now;
    _healthCheckEnabledAt = now;
}

/**
 * @brief Monitor the other core's heartbeat and trigger reboot if frozen.
 *
 * Grace period: skips monitoring during the first 5 seconds after boot
 * completes (_bootReady), instead of using a fixed millis() threshold.
 * This covers boots of any duration (30-60s with graph preloading).
 *
 * Phase 1 (>8s stale): Reboot with crash data in watchdog scratch registers.
 */

void LogManager::checkCrossCoreHealth() {
    uint32_t now = millis();
    int thisCore = get_core_num();
    int otherCore = (thisCore == 0) ? 1 : 0;

    /* Grace period dinâmico: desativado até 5s após habilitação explícita */
    if (_healthCheckEnabledAt == 0) return;
    if (now - _healthCheckEnabledAt < 5000) return;

    if (_corePaused[otherCore]) return;

    uint32_t lastBeat = _coreHeartbeat[otherCore];

    if (now >= lastBeat) {
        uint32_t elapsed = now - lastBeat;


        if (elapsed > 8000) {
            watchdog_hw->scratch[5] = 0xCA11B007;
            watchdog_hw->scratch[6] = (otherCore << 24) | (_coreModule[0] << 16) | (_coreModule[1] << 8);

            uint32_t startT = _moduleStartTime[otherCore];
            watchdog_hw->scratch[7] = (now >= startT) ? (now - startT) : 0;

            watchdog_reboot(0, 0, 0);
            while(1);
        }
    }
}

/**
 * @brief Analyze watchdog scratch registers after a crash-triggered reboot.
 * Logs the dead core, module, and duration of the freeze.
 */
void LogManager::performCrashAutopsy() {
    if (watchdog_hw->scratch[5] == 0xCA11B007) {
        uint32_t data = watchdog_hw->scratch[6];
        uint32_t stuckTime = watchdog_hw->scratch[7];
        int deadCore = (data >> 24) & 0xFF;
        int mod0 = (data >> 16) & 0xFF;
        int mod1 = (data >> 8) & 0xFF;

        char msg[200];
        snprintf(msg, sizeof(msg), "WATCHDOG PANIC! Core %d frozen in [%s] for %lums. C0=[%s] C1=[%s]",
                 deadCore,
                 deadCore == 0 ? (mod0 <= 9 ? MOD_NAMES[mod0] : "UNK") : (mod1 <= 9 ? MOD_NAMES[mod1] : "UNK"),
                 stuckTime,
                 mod0 <= 9 ? MOD_NAMES[mod0] : "UNK",
                 mod1 <= 9 ? MOD_NAMES[mod1] : "UNK");

        logCode(LOG_FATAL, "SYS", SYS_BOOT, deadCore, String(msg));
        watchdog_hw->scratch[5] = 0;
    }
}


/* =========================================================================== */
/*              TRANSLATION TABLE — LogCode → Human-Readable Text            */
/* =========================================================================== */
/**
 * @brief Traduz um LogCode numérico para texto legível.
 *
 * Usada pela serial (quando extraMsg é vazio) e pela API /api/logs
 * para reconstruir mensagens a partir dos registros binários compactos.
 *
 * @param  code  Código de log (LogCode enum).
 * @return Ponteiro para string constante com a descrição.
 */
const char* LogManager::translateCode(uint16_t code) {
    switch ((LogCode)code) {
        /* ── System (0–9) ── */
        case SYS_OK:              return "OK";
        case SYS_BOOT:            return "System boot";
        case SYS_REBOOT_USER:     return "User-requested reboot";
        case SYS_HEAP_LOW:        return "Heap memory low";
        case SYS_UPTIME_MARK:     return "Uptime milestone";

        /* ── WiFi (10–15) ── */
        case SYS_WIFI_CONNECT:    return "WiFi connecting";
        case SYS_WIFI_DISCONNECT: return "WiFi disconnected";
        case SYS_WIFI_SCAN:       return "WiFi scanning";
        case SYS_NTP_SYNC:        return "NTP synced";
        case SYS_IP_ACQUIRED:     return "IP acquired";
        case SYS_AP_START:        return "AP mode started";

        /* ── Storage (20–24) ── */
        case SYS_STORAGE_FAIL:    return "Storage failure";
        case SYS_STORAGE_SAVE:    return "Config saved";
        case SYS_STORAGE_ROTATE:  return "Storage rotated";
        case SYS_STORAGE_FORMAT:  return "Flash formatting";
        case SYS_STORAGE_RECOVER: return "Storage recovered";

        /* ── Telemetry (30–37) ── */
        case SYS_TEL_SENT:        return "Telemetry sent";
        case SYS_TEL_FAIL:        return "Telemetry failed";
        case SYS_TEL_RETRY:       return "Telemetry retry";
        case SYS_TEL_QUEUE:       return "Telemetry queued";
        case SYS_TEL_SSL:         return "SSL cert loaded";
        case SYS_TEL_MQTT_CONN:   return "MQTT connected";
        case SYS_TEL_MQTT_DISC:   return "MQTT disconnected";
        case SYS_TEL_MQTT_PUB:    return "MQTT published";

        /* ── Sensor (100–106) ── */
        case LOG_SENSOR_REC:      return "Sensor recovered";
        case ERR_SENSOR_TIMEOUT:  return "Sensor timeout";
        case ERR_SENSOR_CHECKSUM: return "Sensor checksum error";
        case ERR_SENSOR_CRC:      return "Sensor CRC error";
        case ERR_SENSOR_RANGE:    return "Sensor out of range";
        case ERR_SENSOR_MISMATCH: return "Hardware mismatch";
        case ERR_SENSOR_MISSING:  return "Sensor missing";

        /* ── UI events (200–202) ── */
        case EVT_UI_TOUCH:        return "Touch event";
        case EVT_DISPLAY_RESTART: return "Display restarted";
        case EVT_GRAPH_RENDER:    return "Graph rendered";

        /* ── Security (300–306) ── */
        case SEC_LOGIN_SUCCESS:   return "Login success";
        case SEC_LOGIN_FAIL:      return "Login failed";
        case SEC_UNAUTHORIZED:    return "Unauthorized access";
        case SEC_CONFIG_CHANGED:  return "Config changed";
        case SEC_SESSION_EXPIRE:  return "Session expired";
        case SEC_FILE_UPLOAD:     return "File uploaded";
        case SEC_FILE_DELETE:     return "File deleted";

        /* ── App lifecycle (400–410) ── */
        case APP_DISPLAY_LAUNCHED:    return "Display launched on Core 1";
        case APP_TOUCH_CAL_INITIAL:   return "Initial touch cal saved";
        case APP_TOUCH_CAL_REQUIRED:  return "Touch calibration required";
        case APP_AP_MODE_TRIGGERED:   return "AP mode triggered by user";
        case APP_READY:               return "System ready";
        case APP_READY_AP:            return "System ready (AP mode)";
        case APP_STORAGE_CRITICAL:    return "Storage critical failure";
        case APP_SENSORS_CALIBRATED:  return "Sensors calibrated";
        case APP_NTP_CORRECTING:      return "NTP correcting timestamps";
        case APP_NTP_CORRECTED:       return "Timestamps corrected";
        case APP_CACHE_INVALIDATED:   return "Graph caches invalidated";

        /* ── App UI (440–449) ── */
        case APP_UI_THEME_CHANGED:    return "Theme changed via UI";
        case APP_UI_LANG_CHANGED:     return "Language changed via UI";
        case APP_UI_ALARM_SAVED:      return "Alarm limits saved via UI";
        case APP_UI_TOUCH_CAL_SAVED:  return "Touch cal saved to flash";
        case APP_UI_TOUCH_SENS_SAVED: return "Touch sensitivity saved";
        case APP_UI_PIN_CHANGED:      return "Display PIN changed";
        case APP_UI_SOUND_SAVED:      return "Sound settings saved";
        case APP_UI_ALARM_SILENCED:   return "Alarm silenced via UI";
        case APP_UI_ALARM_SILENCE_EXP:return "Alarm silence expired";
        case APP_UI_ALARM_DEACTIVATED:return "All alarms deactivated (RAM)";

        /* ── Alarm state (470–472) ── */
        case APP_ALARM_TRIGGERED:     return "Alarm triggered";
        case APP_ALARM_CLEARED:       return "Alarm cleared";
        case APP_ALARM_SILENCE_CANCEL:return "Alarm silence cancelled";

        /* ── Cache (480–489) ── */
        case APP_CACHE_MINMAX_FULL:   return "Min/Max cache loaded";
        case APP_CACHE_MINMAX_PARTIAL:return "Min/Max cache partial";
        case APP_CACHE_GRAPH_STARTED: return "Graph cache refresh started";
        case APP_CACHE_GRAPH_DONE:    return "Graph cache refresh done";
        case APP_CACHE_GRAPH_AMBIENT: return "Graph cache: ambient";
        case APP_CACHE_GRAPH_BOARD:   return "Graph cache: board temp";
        case APP_CACHE_PRELOAD_DONE:  return "Graph cache preload done";
        case APP_GRAPH_LOADING:       return "Graph loading";
        case APP_GRAPH_BUDGET:        return "Graph render budget exceeded";
        case APP_PRELOAD_BUDGET:      return "Preload budget exceeded";

        /* ── Safety (500–503) ── */
        case APP_DISPLAY_PAUSE_STUCK: return "Display pause stuck >5s";
        case APP_YIELD_STUCK:         return "Yield stuck >10s";
        case APP_CORE1_DEAD:          return "Core 1 dead >10s, restarting";
        case APP_FLASH_BUSY:          return "Flash busy collision";

        /* ── History (510–511) ── */
        case APP_HISTORY_SAVED:       return "History record saved";
        case APP_HEAP_REPORT:         return "Heap status report";

        /* ── Network extended (520–527) ── */
        case NET_DHCP_MODE:           return "DHCP mode enabled";
        case NET_STATIC_MODE:         return "Static IP mode enabled";
        case NET_STARTING:            return "WiFi manager starting";
        case NET_SSID_MISSING:        return "WiFi SSID not configured";
        case NET_PROVISIONAL_TIME:    return "Provisional time set from flash";
        case NET_CONNECT_TIMEOUT:     return "WiFi connect timeout";
        case NET_DORMANT_MODE:        return "WiFi dormant mode";
        case NET_SHOW_IP:             return "Show IP";

        /* ── Telemetry extended (540–547) ── */
        case TEL_HTTP_INIT:           return "HTTP transport initialized";
        case TEL_MQTT_INIT:           return "MQTT transport initialized";
        case TEL_MQTT_CONNECTING:     return "MQTT connecting";
        case TEL_CERT_EMPTY:          return "cert.pem empty, insecure mode";
        case TEL_CERT_READ_ERR:       return "cert.pem read error";
        case TEL_CERT_MISSING:        return "No cert.pem, insecure mode";
        case TEL_FORCE_SYNC:          return "Forcing telemetry sync";
        case TEL_BACKOFF_SUPPRESSED:  return "Retry logs suppressed";

        /* ── Storage extended (560–565) ── */
        case STO_WRITE_FAILED:        return "History write failed";
        case STO_CORRECT_BUDGET:      return "Timestamp correction budget exceeded";
        case STO_ENFORCE_BUDGET:      return "Storage limit budget exceeded";
        case STO_ENFORCE_SKIP_ACTIVE: return "Skipping active log file";
        case STO_STATS_REPORT:        return "Storage stats report";
        case STO_CONFIG_REPORT:       return "Config report";

        /* ── Web (570–574) ── */
        case WEB_SERVER_STARTED:      return "Web server started on port 80";
        case WEB_DISCONNECT_FILE:     return "Client disconnected (file)";
        case WEB_DISCONNECT_HISTORY:  return "Client disconnected (history)";
        case WEB_SCREENSHOT_ABORTED:  return "Screenshot aborted by client";
        case WEB_UPLOAD:              return "File uploaded";

        /* ── Config (580–581) ── */
        case CFG_THEME_APPLIED:       return "Theme applied";
        case CFG_THEME_NOT_FOUND:     return "Theme not found";

        /* ── CLI (585) ── */
        case CLI_UNKNOWN_CMD:         return "Unknown command";

        /* ── Sensor (590) ── */
        case SENSOR_RUNTIME_LOADED:   return "Runtime sensors loaded";

        /* ── Display (600) ── */
        case DSP_FORCE_UNPAUSE:       return "Force unpause";

        case ERR_UNKNOWN:             return "Unknown error";
        default:                      return "?";
    }
}
