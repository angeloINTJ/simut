/**
 * @file    LogManager.cpp
 * @brief   Implementation of LogManager — log output, flash persistence, and crash forensics.
 * @details Implements dual-format logging (syslog-style serial + CSV flash),
 *          automatic log rotation (500 lines max), ring buffer for logs during
 *          heavy tasks or touch interactions, and cross-core health monitoring
 *          with configurable timeout thresholds and watchdog-triggered reboot.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
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
        if (LittleFS.exists(LOG_FILE_CURRENT)) {
            _currentLineCount = countFileLines(LOG_FILE_CURRENT);
        } else {
            _currentLineCount = 0;
        }
        requestFsLock(false);
    }
    performCrashAutopsy();
}

int LogManager::getCoreID() { return get_core_num(); }

uint16_t LogManager::countFileLines(const char* filename) {
    File f = LittleFS.open(filename, "r");
    if (!f) return 0;
    uint16_t lines = 0;
    while (f.available()) {
        char c = f.read();
        if (c == '\n') lines++;
    }
    f.close();
    return lines;
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
        struct tm ti; gmtime_r(&epoch, &ti);
        Serial.printf("[%04d-%02d-%02d %02d:%02d:%02d]", ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        Serial.printf("[BOOT+%lus]", millis()/1000);
    }
    Serial.printf("[UP %s][C%d][%s][%s] [%d] %s",
        uptimeString().c_str(), core, getLevelString(level), tag, (int)code,
        extraMsg.length() > 0 ? extraMsg.c_str() : "");
    if (contextVal != 0) Serial.printf(" (ctx:%d)", contextVal);
    Serial.println();


    if (_saveToFile) {
        char csvBuf[256];
        snprintf(csvBuf, sizeof(csvBuf), "%lu;%lu;%d;%d;%.10s;%d;%d;%.120s",
            (unsigned long)epoch, (unsigned long)millis(), core, (int)level,
            tag, (int)code, contextVal,
            extraMsg.length() > 0 ? extraMsg.c_str() : "");
        writeToFlash(csvBuf);
    }
    mutex_exit(&_logMutex);
}


void LogManager::log(LogLevel level, const char* tag, LogCode code, String msg) {
    if (level < _minSerialLevel && level < LOG_WARN) return;
    mutex_enter_blocking(&_logMutex);

    time_t epoch = getEpochNow();
    int core = get_core_num();

    if (epoch > 1600000000) {
        struct tm ti; gmtime_r(&epoch, &ti);
        Serial.printf("[%04d-%02d-%02d %02d:%02d:%02d]", ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        Serial.printf("[BOOT+%lus]", millis()/1000);
    }
    Serial.printf("[UP %s][C%d][%s][%s] %s\n",
        uptimeString().c_str(), core, getLevelString(level), tag, msg.c_str());

    if (_saveToFile && level >= LOG_INFO) {
        char csvBuf[256];
        snprintf(csvBuf, sizeof(csvBuf), "%lu;%lu;%d;%d;%.10s;%d;0;%.140s",
            (unsigned long)epoch, (unsigned long)millis(), core, (int)level,
            tag, (int)code, msg.c_str());
        writeToFlash(csvBuf);
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
void LogManager::writeToFlash(const char* csvLine) {


    if (_isTouchPriorityFn && _isTouchPriorityFn()) {
        int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
        if (idx < LOG_PENDING_MAX) {
            strncpy(_pendingLogs[idx], csvLine, 255);
            _pendingLogs[idx][255] = '\0';
            __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
        }
        return;
    }


    if (_heavyTaskCheckEnabled && _isHeavyTaskFn && _isHeavyTaskFn()) {
        int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
        if (idx < LOG_PENDING_MAX) {
            strncpy(_pendingLogs[idx], csvLine, 255);
            _pendingLogs[idx][255] = '\0';
            __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
        }

        return;
    }


    flushPendingLogs();

    requestFsLock(true);

    if (_currentLineCount >= MAX_LINES_PER_FILE) {
        if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
        if (LittleFS.exists(LOG_FILE_CURRENT)) LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
        _currentLineCount = 0;
    }

    File f = LittleFS.open(LOG_FILE_CURRENT, "a");
    if (f) {
        f.println(csvLine);
        f.close();
        _currentLineCount++;
    }

    requestFsLock(false);
}


/** @brief Flush buffered log entries that accumulated during heavy tasks. */
void LogManager::flushPendingLogs() {
    int count = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
    if (count == 0) return;

    requestFsLock(true);

    for (int i = 0; i < count; i++) {
        if (_currentLineCount >= MAX_LINES_PER_FILE) {
            if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
            if (LittleFS.exists(LOG_FILE_CURRENT)) LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
            _currentLineCount = 0;
        }

        File f = LittleFS.open(LOG_FILE_CURRENT, "a");
        if (f) {
            f.println(_pendingLogs[i]);
            f.close();
            _currentLineCount++;
        }
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
 * @brief Monitor the other core's heartbeat and trigger reboot if frozen.
 * Phase 1 (4s): Warning only — may be a slow flash operation.
 * Phase 2 (8s): Reboot with crash data in watchdog scratch registers.
 */
void LogManager::checkCrossCoreHealth() {
    uint32_t now = millis();
    int thisCore = get_core_num();
    int otherCore = (thisCore == 0) ? 1 : 0;

    if (now < 15000) return;
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
