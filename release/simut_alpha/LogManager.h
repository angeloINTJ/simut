/**
 * @file LogManager.h
 * @brief System logger with flash persistence, ring buffer, and cross-core watchdog.
 * @details Singleton logger supporting multiple severity levels with both serial
 * and LittleFS CSV output. Features a pending-log ring buffer for
 * heavy task periods, touch-priority-aware buffering, and a black-box
 * profiler that tracks per-core module execution and performs crash
 * autopsies via watchdog scratch registers.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <functional>
#include "pico/mutex.h"
#include <hardware/watchdog.h>
#include "SystemDefs.h"
#include "FlashIrqProbe.h"  /* core1StallSample( ), called from feedWdt( ) below */
#include "LogPolicy.h"      /* edge-triggered filter applied before every flash write */

#define LOG_FILE_CURRENT "/system.blog"
#define LOG_FILE_OLD "/system.old.blog"
#define MAX_RECORDS_PER_FILE 800

typedef void (*FlashLockCallback)(bool);

/** Console log output sink (USB+BT) — one line per call, no '\n'. */
typedef std::function<void(const char*)> ConsoleSink;

/**
 * @brief One log record, in the raw fields a syslog forwarder needs.
 *
 * Handed to the SyslogSink from inside logCode()/log() while _logMutex is
 * held, which is exactly why `tag` and `desc` (a pointer into the shared
 * translateCode buffer) are valid to read: another core cannot be mid-log and
 * cannot have reused that buffer yet. The sink must copy what it keeps and
 * must NOT log (it runs under _logMutex — logging would deadlock).
 */
struct SyslogEvent {
 uint8_t level;      /**< LogLevel 0..4 */
 const char* tag;    /**< APP-NAME, e.g. "NET" */
 uint16_t code;      /**< MSGID (numeric log code) */
 int16_t context;    /**< ctx (already clamped to int16) */
 uint8_t core;       /**< originating core 0/1 */
 uint32_t uptimeSec; /**< seconds since boot */
 long epoch;         /**< getEpochNow() at the record; <sync → NILVALUE ts */
 const char* desc;   /**< translated code description (MSG) */
 const char* extra;  /**< extra message text, "" if none */
};

/** Syslog forwarding sink — enqueues one RFC 5424 line. See SyslogEvent. */
typedef std::function<void(const SyslogEvent&)> SyslogSink;

enum LogLevel {
 LOG_DEBUG = 0,
 LOG_INFO = 1,
 LOG_WARN = 2,
 LOG_ERROR = 3,
 LOG_FATAL = 4,
 LOG_NONE = 5
};

class LogManager {
public:
 static LogManager& instance( ) {
 static LogManager _instance;
 return _instance;
 }

 void setLockCallback(FlashLockCallback cb);
 void setHeavyTaskChecker(bool (*fn)( ));

 /** Last chance to persist RAM state, run at the very top of safeReboot( )
  * while flash is still writable and the console still attached. AppManager
  * registers the V5 history snapshot here: the open block lives in RAM, so a
  * voluntary reboot used to discard every record taken since the last
  * snapshot — deterministically, on a path that had time to spare. */
 void setPreRebootHook(void (*fn)( ));

 /** Cancel the hook for a reboot that must NOT persist: the caller has just
  * erased or replaced the filesystem the hook would write into, and a
  * snapshot from the pre-erase RAM block would resurrect it on next boot. */
 void suppressPreRebootHook( );
 void setConsoleSink(ConsoleSink sink);
 /** Install the syslog forwarding sink. Called with each qualifying record
  * (level >= LOG_INFO or WARN+) from inside the log mutex; see SyslogEvent
  * for the contract. nullptr (default) = no forwarding. */
 void setSyslogSink(SyslogSink sink);
 void setConsoleStream(bool enabled); /**< false = CONFIG mode (silent console) */

 /** Force RAM buffering for all log writes (writeCompactToFlash).
 * Used to defer flash during sensitive operations (ex: BT login)
 * without competing with multicore_lockout. Flush happens on next normal
 * write or via flushPendingIfAny( ). */
 void setForceBuffer(bool force);

 /** Write a line directly to console (USB+BT via sink; fallback Serial).
 * Ignores `setConsoleStream(false)` — intended for user-requested output
 * (ex: payload dump via `tel dump`), not automatic logs. */
 void writeConsole(const char* line);

 /** Mark that the system WDT is active (called by SIMUT.ino on first
 * loop). Flash write paths use this flag to decide whether to extend
 * the WDT window — do NOT extend during setup to avoid WDT armed too early.
 *
 * Also stamps the ARMED magic in scratch[5]. This is what lets the next
 * boot's autopsy tell a genuine HW-watchdog stall (WDT was armed and fired)
 * from an external reset such as a picotool upload (WDT never armed). It is
 * stamped HERE, not at the end of performCrashAutopsy( ), because the autopsy
 * runs inside setup( ) — before the WDT exists — so a magic written there
 * says nothing about whether the watchdog was ever running. Soft panic and
 * markCleanReboot( ) overwrite this magic with their own. */
 static void markWdtActive( ) {
  _wdtActive = true;
  watchdog_hw->scratch[5] = 0xA11FA1E5;
 }
 static bool isWdtActive( ) { return _wdtActive; }

 /*
 * Nested WDT context: the outer caller (ex: TelemetryManager::update
 * with 120s) sets the context timeout. Inner callers (writeCompactToFlash
 * with 30s during LOG_CODE from audit) call setWdtCtxMs to extend,
 * then restoreWdtCtx to return to the outer timeout — not to the short
 * default (8.3s), which would destroy the outer window.
 */
 static void setWdtCtxMs(uint32_t ms) { _wdtCtxMs = ms; }
 static uint32_t getWdtCtxMs( ) { return _wdtCtxMs; }

 /*
 * RAII: extends the WDT window to `ms` in the construction scope,
 * restores the outer context in the destructor. max(outerCtx, ms) to
 * never reduce window when nested inside a larger outer one.
 * No-op if _wdtActive=false (setup).
 *
 * IT CANNOT EXTEND PAST 8388 ms — see WATCHDOG_TIMEOUT_MS. RP2040 clamps the
 * watchdog load register, so every WdtWindow in this codebase that asks for
 * more (30 s around a graph render, 120 s around telemetry) gets exactly the
 * same ceiling as the default and buys NOTHING. The callers' comments reason
 * about budgets that never existed: "30s covers any case" is 8.4 s, so a 6 s
 * graph render has 2.4 s of headroom, not 24 s.
 *
 * The class stays — nesting a SHORTER window inside a longer one still works
 * and the save/restore is correct — but size long operations by FEEDING the
 * watchdog, never by widening the window.
 */
 class WdtWindow {
 public:
 explicit WdtWindow(uint32_t ms) {
 _saved = LogManager::getWdtCtxMs( );
 uint32_t target = (ms > _saved) ? ms : _saved;
 LogManager::setWdtCtxMs(target);
 if (LogManager::isWdtActive( )) watchdog_enable(target, 1);
 }
 ~WdtWindow( ) {
 LogManager::setWdtCtxMs(_saved);
 if (LogManager::isWdtActive( )) watchdog_enable(_saved, 1);
 }
 WdtWindow(const WdtWindow&) = delete;
 WdtWindow& operator=(const WdtWindow&) = delete;
 private:
 uint32_t _saved;
 };
 bool isConsoleStream( ) const { return _consoleStreamEnabled; }

 /** Capture one-shot snapshot of watchdog_hw->scratch[3] (previous boot module)
 * before setModule( ) overwrites it. Idempotent. begin( ) calls this before
 * performCrashAutopsy( ); call explicitly if you need to run autopsy in another
 * flow without going through begin( ). */
 void captureBootSnapshot( );

 void begin(bool saveToFile = false, LogLevel minSerialLevel = LOG_INFO);

 /** Reset logger state after external wipe of log files
 * (ex: handleApiClearLogs). Re-counts records without re-initializing
 * the entire log system. Does not re-capture boot snapshot nor re-run
 * autopsy — only zeros counters and reopens handles if needed. */
 void resetAfterExternalWipe( );


 void log(LogLevel level, const char* tag, LogCode code, String msg);
 void logCode(LogLevel level, const char* tag, LogCode code, int contextVal = 0, String extraMsg = "");

 void info(const char* tag, String msg);
 void warn(const char* tag, String msg);
 void error(const char* tag, String msg);
 void debug(const char* tag, String msg);

 void setSaveToFile(bool enable);
 void setMinSerialLevel(LogLevel level);

 const char* getLevelString(LogLevel level);
 const char* translateCode(uint16_t code);

 /** Language for log code labels. Synced with cfg.displayLang. */
 void setLanguage(uint8_t lang) { _language = lang; }

 /** Select string according to current language. EN is
 * inline; non-EN does lookup in DisplayManager::trlLookup( ) via
 * FNV-1a hash of EN. Implementation out-of-line in LogManager.cpp
 * to avoid including DisplayManager.h here. */
 const char* tr(const char* en) const;


 void setEpochSource(time_t (*fn)( ));


 static String uptimeString( );


 void setModule(int core, uint8_t mod);
 uint8_t getModule(int core);
 void heartbeat(int core);

 /** RAII for TRACE_MOD — saves current mod on construction, applies
 * new mod; on destructor restores the previous one. Allows instrumenting
 * internal functions (saveConfiguration, writeCompactToFlash) without
 * "leaking" the module to the rest of the caller handler. */
 class TraceScope {
 public:
 explicit TraceScope(int core, uint8_t newMod) : _core(core) {
 _saved = LogManager::instance( ).getModule(core);
 LogManager::instance( ).setModule(core, newMod);
 }
 ~TraceScope( ) { LogManager::instance( ).setModule(_core, _saved); }
 TraceScope(const TraceScope&) = delete;
 TraceScope& operator=(const TraceScope&) = delete;
 private:
 int _core;
 uint8_t _saved;
 };
 void checkCrossCoreHealth( );
 void enableHealthCheck( ); /**< Enable cross-core monitoring (call after boot) */
 void performCrashAutopsy( );
 void setCorePaused(int core, bool paused);
 void markCleanReboot( ); /**< Call BEFORE rp2040.reboot( ) to avoid HW WDT autopsy trigger */

 /** Defensive reboot that gives USB CDC time to cleanly disconnect
 * on the host. Resolves "ttyACM0 doesn't reappear after reload" on Linux,
 * caused by watchdog_reboot(0,0,10) interrupting USB mid-transmission.
 * Does: markCleanReboot + Serial.flush + Serial.end + delays +
 * watchdog_enable(500ms). Does NOT return. */
 [[noreturn]] void safeReboot( );

 /** Immediate flush of pending logs buffered during touch
 * priority. AppManager calls right after `isUserInteracting( )` transitions
 * to false, to close the "data in RAM, not flash" window. No-op
 * if no pending logs. */
 void flushPendingIfAny( );

 /** Periodic tick for the edge-triggered filter — emits the hourly
  * SYS_LOG_SUPPRESSED accounting record. Called from AppManager::loop, next
  * to checkCrossCoreHealth( ).
  *
  * It cannot live inside logCode( ): emitting a record from within the
  * decision would re-enter logCode( ) while it holds _logMutex, which is not
  * recursive. Hence a separate tick from the main loop. */
 void policyTick( );

private:
 LogManager( );

 static volatile bool _wdtActive;
 static volatile uint32_t _wdtCtxMs;

 mutex_t _logMutex;
 bool _saveToFile;
 LogLevel _minSerialLevel;
 uint16_t _currentLineCount;

 FlashLockCallback _lockCb = nullptr;
 ConsoleSink _consoleSink = nullptr;
 SyslogSink _syslogSink = nullptr;
 bool _consoleStreamEnabled = true; /**< true during boot; AppManager applies user preference after load */
 uint8_t _language = LANG_EN; /**< language for log labels (translateCode) */
 void emitLine(const char* line);
 void requestFsLock(bool lock);


 static const int LOG_PENDING_MAX = 32;
 CompactLogRecord _pendingLogs[LOG_PENDING_MAX];
 volatile int _pendingCount = 0;
 uint16_t _pendingOverflow = 0;
 bool _heavyTaskCheckEnabled = false;
 bool _forceBuffer = false; /**< Temporary forced buffer (ex: BT login) */

 /** Decides which records earn a slot in the 1600-record flash window.
  * Guarded by _logMutex: shouldPersist( ) runs inside logCode( )'s critical
  * section, and policyTick( ) takes the same mutex to drain the counter. */
 LogPolicy _policy;


 bool (*_isHeavyTaskFn)( ) = nullptr;
 void (*_preRebootFn)( ) = nullptr;

 /* Uses TouchPriority::isActive( ) directly. */

 void writeCompactToFlash(const CompactLogRecord& rec);
 void flushPendingLogs( );
 int getCoreID( );

 uint16_t countFileRecords(const char* filename);

 time_t (*_epochFn)( ) = nullptr;
 time_t getEpochNow( );
};


#define LOG_DBG(tag, msg) LogManager::instance( ).debug(tag, msg)
#define LOG_INF(tag, msg) LogManager::instance( ).info(tag, msg)
#define LOG_WRN(tag, msg) LogManager::instance( ).warn(tag, msg)
#define LOG_ERR(tag, msg) LogManager::instance( ).error(tag, msg)
#define LOG_CODE(lvl, tag, code, ctx, msg) LogManager::instance( ).logCode(lvl, tag, code, ctx, msg)

/** Select EN inline or translation from .lng (via hash). */
#define TRL(en) (LogManager::instance( ).tr(en))

#define TRACE_MOD(core, mod) LogManager::instance( ).setModule(core, mod)
#define TRACE_BEAT(core) LogManager::instance( ).heartbeat(core)

/** Feed hardware watchdog + trace heartbeat on Core 0.
 * Replaces the pair watchdog_update( ); TRACE_BEAT(0); in critical paths.
 *
 * core1StallSample( ) rides along because this is already the densest Core-0
 * hook there is — every loop that walks or reads flash calls it, which is
 * exactly the load that freezes Core 1. Sampling from here means the worst
 * Core-1 stall is recorded by the core that is still running, instead of by the
 * core that is stuck (see FlashIrqProbe.h). */
inline void feedWdt( ) { watchdog_update( ); TRACE_BEAT(0); core1StallSample( ); }
