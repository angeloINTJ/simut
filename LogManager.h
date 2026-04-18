/**
 * @file    LogManager.h
 * @brief   System logger with flash persistence, ring buffer, and cross-core watchdog.
 * @details Singleton logger supporting multiple severity levels with both serial
 * and LittleFS CSV output. Features a pending-log ring buffer for
 * heavy task periods, touch-priority-aware buffering, and a black-box
 * profiler that tracks per-core module execution and performs crash
 * autopsies via watchdog scratch registers.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.8.0
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <functional>
#include "pico/mutex.h"
#include "SystemDefs.h"

#define LOG_FILE_CURRENT "/system.blog"
#define LOG_FILE_OLD     "/system.old.blog"
#define MAX_RECORDS_PER_FILE 800

typedef void (*FlashLockCallback)(bool);

/** Sink de saída de log no console (USB+BT) — uma linha por chamada, sem '\n'. */
typedef std::function<void(const char*)> ConsoleSink;

enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4,
    LOG_NONE  = 5
};

class LogManager {
public:
    static LogManager& instance() {
        static LogManager _instance;
        return _instance;
    }

    void setLockCallback(FlashLockCallback cb);
    void setHeavyTaskChecker(bool (*fn)());
    void setTouchPriorityChecker(bool (*fn)());
    void setConsoleSink(ConsoleSink sink);
    void setConsoleStream(bool enabled);   /**< false = modo CONFIG (console silencioso) */
    bool isConsoleStream() const { return _consoleStreamEnabled; }
    void begin(bool saveToFile = false, LogLevel minSerialLevel = LOG_INFO);


    void log(LogLevel level, const char* tag, LogCode code, String msg);
    void logCode(LogLevel level, const char* tag, LogCode code, int contextVal = 0, String extraMsg = "");

    void info(const char* tag, String msg);
    void warn(const char* tag, String msg);
    void error(const char* tag, String msg);
    void debug(const char* tag, String msg);

    void setSaveToFile(bool enable);
    void setMinSerialLevel(LogLevel level);

    const char* getLevelString(LogLevel level);
    static const char* translateCode(uint16_t code);


    void setEpochSource(time_t (*fn)());


    static String uptimeString();


    void setModule(int core, uint8_t mod);
    void heartbeat(int core);
    void checkCrossCoreHealth();
    void enableHealthCheck();        /**< Habilita o monitoramento cross-core (chamar após boot) */
    void performCrashAutopsy();
    void setCorePaused(int core, bool paused);

private:
    LogManager();

    mutex_t _logMutex;
    bool _saveToFile;
    LogLevel _minSerialLevel;
    uint16_t _currentLineCount;

    FlashLockCallback _lockCb = nullptr;
    ConsoleSink _consoleSink = nullptr;
    bool _consoleStreamEnabled = true;     /**< true durante boot; AppManager aplica preferência do user após load */
    void emitLine(const char* line);
    void requestFsLock(bool lock);


    static const int LOG_PENDING_MAX = 32;
    CompactLogRecord _pendingLogs[LOG_PENDING_MAX];
    volatile int _pendingCount = 0;
    uint16_t _pendingOverflow = 0;
    bool _heavyTaskCheckEnabled = false;


    bool (*_isHeavyTaskFn)() = nullptr;


    bool (*_isTouchPriorityFn)() = nullptr;

    void writeCompactToFlash(const CompactLogRecord& rec);
    void flushPendingLogs();
    int getCoreID();

    uint16_t countFileRecords(const char* filename);

    time_t (*_epochFn)() = nullptr;
    time_t getEpochNow();
};


#define LOG_DBG(tag, msg) LogManager::instance().debug(tag, msg)
#define LOG_INF(tag, msg) LogManager::instance().info(tag, msg)
#define LOG_WRN(tag, msg) LogManager::instance().warn(tag, msg)
#define LOG_ERR(tag, msg) LogManager::instance().error(tag, msg)
#define LOG_CODE(lvl, tag, code, ctx, msg) LogManager::instance().logCode(lvl, tag, code, ctx, msg)

#define TRACE_MOD(core, mod) LogManager::instance().setModule(core, mod)
#define TRACE_BEAT(core) LogManager::instance().heartbeat(core)
