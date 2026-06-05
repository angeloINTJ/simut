/**
 * @file LogManager.cpp
 * @brief Implementation of LogManager — log output, flash persistence, and crash forensics.
 * @details Implements dual-format logging (syslog-style serial + CSV flash),
 * automatic log rotation (500 lines max), ring buffer for logs during
 * heavy tasks or touch interactions, and cross-core health monitoring
 * with configurable timeout thresholds and watchdog-triggered reboot.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "LogManager.h"
#include "DisplayManager.h" /* logcodeLookup / trlLookup from .lng */
#include "TouchPriority.h"
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

/* Snapshot of scratch[3] from previous boot, captured on the first call
 * to setModule (before overwriting). Used by autopsy to recover
 * the active module at the time of HW WATCHDOG even after AppManager::setup
 * has called `TRACE_MOD(0, MOD_BOOT)` at startup. */
static volatile uint32_t _preBootScratch4 = 0;
static volatile bool _preBootSnapshotTaken = false;

/* One-shot guard: performCrashAutopsy( ) runs exactly once per session.
 * begin( ) is called multiple times at runtime (clear log, web clear logs) —
 * without this guard, the 2nd autopsy would re-trigger HW WATCHDOG because
 * watchdog_caused_reboot( ) stays true for the entire session. */
static volatile bool _autopsyPerformed = false;

const char* MOD_NAMES[] = {"BOOT", "IDLE", "WIFI", "WEB_SERVER", "STORAGE_RD", "STORAGE_WR", "SENSOR", "TELEMETRY", "DISPLAY", "CLI",
 "SAVE_CFG", "LOG_FLASH", "HIST_FLASH", "CORE1_LOCK"};
static constexpr uint8_t MOD_NAMES_MAX = 13; /* last valid index */

volatile bool LogManager::_wdtActive = false;
volatile uint32_t LogManager::_wdtCtxMs = WATCHDOG_TIMEOUT_MS;

LogManager::LogManager( ) {
 mutex_init(&_logMutex);
 _saveToFile = false;
 _minSerialLevel = LOG_INFO;
 _currentLineCount = 0;
 _epochFn = nullptr;
}

void LogManager::setLockCallback(FlashLockCallback cb) { _lockCb = cb; }

void LogManager::setConsoleSink(ConsoleSink sink) { _consoleSink = sink; }

void LogManager::setConsoleStream(bool enabled) { _consoleStreamEnabled = enabled; }

/* Emit a line to console.
 * If CONFIG mode (stream OFF): silent, flash keeps recording normally.
 * If sink installed (CommandManager): mirrors USB+BT via consolePrintln.
 * Otherwise: fallback to direct Serial (pre-boot, before _cmdMgr.begin( )). */
void LogManager::emitLine(const char* line) {
 if (!_consoleStreamEnabled) return;
 if (_consoleSink) _consoleSink(line);
 else Serial.println(line);
}

void LogManager::writeConsole(const char* line) {
 /* BT streams silently discard large writes (buffer ~256 B).
 * Chunks long lines into pieces that fit in the BT transmission window.
 * Each chunk goes out as its own line — long output becomes multiple lines
 * on the receiver, but no byte is lost. */
 constexpr size_t MAX_CHUNK = 200;
 const size_t len = strlen(line);
 if (len <= MAX_CHUNK) {
 if (_consoleSink) _consoleSink(line);
 else Serial.println(line);
 return;
 }

 char buf[MAX_CHUNK + 1];
 size_t off = 0;
 while (off < len) {
 const size_t n = (len - off > MAX_CHUNK) ? MAX_CHUNK : (len - off);
 memcpy(buf, line + off, n);
 buf[n] = '\0';
 if (_consoleSink) _consoleSink(buf);
 else Serial.println(buf);
 off += n;
 /* Short pause to give BT tx buffer time to drain. */
 delay(2);
 }
}


void LogManager::setHeavyTaskChecker(bool (*fn)( )) {
 _isHeavyTaskFn = fn;
 _heavyTaskCheckEnabled = (fn != nullptr);
}


void LogManager::setEpochSource(time_t (*fn)( )) { _epochFn = fn; }

time_t LogManager::getEpochNow( ) {
 if (_epochFn) return _epochFn( );
 time_t t = time(nullptr);
 if (t > 1600000000) return t;
 return 0;
}

String LogManager::uptimeString( ) {
 uint32_t sec = millis( ) / 1000;
 uint32_t d = sec / 86400; sec %= 86400;
 uint32_t h = sec / 3600; sec %= 3600;
 uint32_t m = sec / 60; sec %= 60;
 char buf[24];
 if (d > 0) snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu", d, h, m, sec);
 else snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, sec);
 return String(buf);
}


void LogManager::requestFsLock(bool lock) {
 if (_lockCb) _lockCb(lock);
 if (lock) delay(1);
}

void LogManager::setForceBuffer(bool force) {
 _forceBuffer = force;
}

void LogManager::captureBootSnapshot( ) {
 if (!_preBootSnapshotTaken) {
 _preBootScratch4 = watchdog_hw->scratch[3];
 _preBootSnapshotTaken = true;
 }
}

void LogManager::begin(bool saveToFile, LogLevel minSerialLevel) {
 /* Capture previous boot module BEFORE any TRACE_MOD overwrites
 * scratch[3]. Idempotent: subsequent calls (ex: CMD_CLEAR_LOGS) are no-op. */
 captureBootSnapshot( );

 _saveToFile = saveToFile;
 _minSerialLevel = minSerialLevel;

 if (_saveToFile) {
 requestFsLock(true);

 /* Migration: remove old CSV logs from previous format */
 if (LittleFS.exists("/system.log")) LittleFS.remove("/system.log");
 if (LittleFS.exists("/system.old")) LittleFS.remove("/system.old");

 if (LittleFS.exists(LOG_FILE_CURRENT)) {
 _currentLineCount = countFileRecords(LOG_FILE_CURRENT);
 } else {
 _currentLineCount = 0;
 }
 requestFsLock(false);
 }
 performCrashAutopsy( );
}

void LogManager::resetAfterExternalWipe( ) {
 if (_saveToFile) {
 requestFsLock(true);
 if (LittleFS.exists(LOG_FILE_CURRENT)) {
 _currentLineCount = countFileRecords(LOG_FILE_CURRENT);
 } else {
 _currentLineCount = 0;
 }
 requestFsLock(false);
 }
}

int LogManager::getCoreID( ) { return get_core_num( ); }

uint16_t LogManager::countFileRecords(const char* filename) {
 File f = LittleFS.open(filename, "r");
 if (!f) return 0;
 size_t sz = f.size( );
 f.close( );
 return (uint16_t)(sz / LOG_RECORD_SIZE);
}

const char* LogManager::getLevelString(LogLevel level) {
 switch (level) {
 case LOG_DEBUG: return "DBG";
 case LOG_INFO: return "INF";
 case LOG_WARN: return "WRN";
 case LOG_ERROR: return "ERR";
 case LOG_FATAL: return "FTL";
 default: return "---";
 }
}


/* =========================================================================== */
/* LOG OUTPUT — SERIAL + FLASH CSV */
/* =========================================================================== */
/**
 * @brief Log a structured event with code, context value, and optional message.
 * Serial format: [timestamp][UP uptime][Core][Level][Tag] [Code] Message (ctx:N)
 * Flash CSV: epoch;millis;core;level;tag;code;ctx;message
 */
void LogManager::logCode(LogLevel level, const char* tag, LogCode code, int contextVal, String extraMsg) {
 if (level < _minSerialLevel && level < LOG_WARN) return;

 /* Format into buffer inside mutex, Serial I/O outside */
 char serialBuf[192];
 int spos = 0;

 mutex_enter_blocking(&_logMutex);

 time_t epoch = getEpochNow( );
 int core = get_core_num( );

 if (epoch > 1600000000) {
 struct tm ti; localtime_r(&epoch, &ti);
 spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
 } else {
 spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[BOOT+%lus]", millis( )/1000);
 }

 const char* desc = translateCode((uint16_t)code);
 spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[UP %s][C%d][%s][%s] %s",
 uptimeString( ).c_str( ), core, getLevelString(level), tag, desc);
 if (extraMsg.length( ) > 0) spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, ": %s", extraMsg.c_str( ));
 if (contextVal != 0) spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, " (%d)", contextVal);

 if (_saveToFile) {
 CompactLogRecord rec;
 rec.epoch = (uint32_t)epoch;
 rec.uptimeHr = (uint16_t)(millis( ) / 3600000UL);
 rec.code = (uint16_t)code;
 rec.context = (int16_t)constrain(contextVal, -32767, 32767);
 rec.flags = CompactLogRecord::packFlags((uint8_t)level, (uint8_t)core, tagStringToId(tag));
 rec.reserved = 0;
 writeCompactToFlash(rec);
 }
 mutex_exit(&_logMutex);

 emitLine(serialBuf);
}


void LogManager::log(LogLevel level, const char* tag, LogCode code, String msg) {
 if (level < _minSerialLevel && level < LOG_WARN) return;

 char serialBuf[192];
 int spos = 0;

 mutex_enter_blocking(&_logMutex);

 time_t epoch = getEpochNow( );
 int core = get_core_num( );

 if (epoch > 1600000000) {
 struct tm ti; localtime_r(&epoch, &ti);
 spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
 } else {
 spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[BOOT+%lus]", millis( )/1000);
 }
 spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[UP %s][C%d][%s][%s] %s",
 uptimeString( ).c_str( ), core, getLevelString(level), tag, msg.c_str( ));

 if (_saveToFile && level >= LOG_INFO) {
 CompactLogRecord rec;
 rec.epoch = (uint32_t)epoch;
 rec.uptimeHr = (uint16_t)(millis( ) / 3600000UL);
 rec.code = (uint16_t)code;
 rec.context = 0;
 rec.flags = CompactLogRecord::packFlags((uint8_t)level, (uint8_t)core, tagStringToId(tag));
 rec.reserved = 0;
 writeCompactToFlash(rec);
 }
 mutex_exit(&_logMutex);

 emitLine(serialBuf);
}


/**
 * @brief Write a CSV log line to LittleFS with intelligent buffering.
 *
 * During touch interactions or heavy tasks, logs are buffered in RAM
 * instead of written to flash (which would pause Core 1). The buffer
 * is flushed automatically on the next non-critical write.
 */
void LogManager::writeCompactToFlash(const CompactLogRecord& rec) {

 /* Temporary forced buffer (ex: BT login): avoids flash + synchronous
 * lockout in sensitive paths that can't tolerate GC latency. */
 if (_forceBuffer) {
 int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
 if (idx < LOG_PENDING_MAX) {
 _pendingLogs[idx] = rec;
 __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
 } else {
 _pendingOverflow++;
 }
 return;
 }

 /* During touch interaction: buffer in RAM */
 if (TouchPriority::isActive( )) {
 /* (buffer in RAM — does not touch flash, no TraceScope) */
 int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
 if (idx < LOG_PENDING_MAX) {
 _pendingLogs[idx] = rec;
 __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
 } else {
 _pendingOverflow++;
 }
 return;
 }

 /* During heavy task: buffer in RAM */
 if (_heavyTaskCheckEnabled && _isHeavyTaskFn && _isHeavyTaskFn( )) {
 int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
 if (idx < LOG_PENDING_MAX) {
 _pendingLogs[idx] = rec;
 __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
 } else {
 _pendingOverflow++;
 }
 return;
 }

 flushPendingLogs( );

 /* Autopsy instrumentation. If stuck here we're in LOG_FLASH.
 * If stuck waiting for Core 1 to ack the lockout, we're in CORE1_LOCK. */
 TraceScope _tr(0, MOD_LOG_FLASH);

 /* RAII context-aware: extends WDT ctx to 30s (or keeps outer if larger).
 * Covers requestFsLock (multicore_lockout wait) + flash ops under GC.
 * Auto-restores in destructor. */
 WdtWindow _wdt(30000);
 {
 TraceScope _trLock(0, MOD_CORE1_LOCK);
 requestFsLock(true);
 }
 watchdog_update( );

 if (_currentLineCount >= MAX_RECORDS_PER_FILE) {
 if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
 watchdog_update( );
 if (LittleFS.exists(LOG_FILE_CURRENT)) LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
 watchdog_update( );
 _currentLineCount = 0;

 /* Log the rotation as first entry of the new file (console + flash) */
 emitLine("[LOG] Log file rotated.");
 CompactLogRecord rotRec;
 rotRec.epoch = (uint32_t)getEpochNow( );
 rotRec.uptimeHr = (uint16_t)(millis( ) / 3600000UL);
 rotRec.code = SYS_STORAGE_ROTATE;
 rotRec.context = MAX_RECORDS_PER_FILE;
 rotRec.flags = CompactLogRecord::packFlags(LOG_INFO, get_core_num( ), TAG_STO);
 rotRec.reserved = 0;

 File rf = LittleFS.open(LOG_FILE_CURRENT, "a");
 watchdog_update( );
 if (rf) { rf.write((const uint8_t*)&rotRec, LOG_RECORD_SIZE); rf.close( ); _currentLineCount++; }
 watchdog_update( );
 }

 File f = LittleFS.open(LOG_FILE_CURRENT, "a");
 watchdog_update( );
 if (f) {
 f.write((const uint8_t*)&rec, LOG_RECORD_SIZE);
 f.close( );
 _currentLineCount++;
 }
 watchdog_update( );

 requestFsLock(false);
 /* WdtWindow destructor auto-restores WDT ctx */
}


/** @brief Public wrapper: flush pending logs on demand. */
void LogManager::flushPendingIfAny( ) {
 int count = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
 if (count == 0 && _pendingOverflow == 0) return;
 flushPendingLogs( );
}


/** @brief Flush buffered log entries that accumulated during heavy tasks. */
void LogManager::flushPendingLogs( ) {
 int count = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
 if (count == 0 && _pendingOverflow == 0) return;

 TraceScope _tr(0, MOD_LOG_FLASH);

 /* RAII context-aware (same as writeCompactToFlash). */
 WdtWindow _wdt(30000);
 {
 TraceScope _trLock(0, MOD_CORE1_LOCK);
 requestFsLock(true);
 }
 watchdog_update( );

 /* Batch write: open once, write N entries, close — all within the lock */
 File f = LittleFS.open(LOG_FILE_CURRENT, "a");
 watchdog_update( );

 for (int i = 0; i < count; i++) {
 if (_currentLineCount >= MAX_RECORDS_PER_FILE) {
 if (f) f.close( );
 if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
 watchdog_update( );
 if (LittleFS.exists(LOG_FILE_CURRENT)) LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
 watchdog_update( );
 _currentLineCount = 0;

 CompactLogRecord rotRec;
 rotRec.epoch = (uint32_t)getEpochNow( );
 rotRec.uptimeHr = (uint16_t)(millis( ) / 3600000UL);
 rotRec.code = SYS_STORAGE_ROTATE;
 rotRec.context = MAX_RECORDS_PER_FILE;
 rotRec.flags = CompactLogRecord::packFlags(LOG_INFO, get_core_num( ), TAG_STO);
 rotRec.reserved = 0;

 f = LittleFS.open(LOG_FILE_CURRENT, "a");
 if (f) { f.write((const uint8_t*)&rotRec, LOG_RECORD_SIZE); _currentLineCount++; }
 watchdog_update( );
 }

 if (!f) f = LittleFS.open(LOG_FILE_CURRENT, "a");
 if (f) {
 f.write((const uint8_t*)&_pendingLogs[i], LOG_RECORD_SIZE);
 _currentLineCount++;
 }
 /* Feed every 8 entries for large batch (pending max 32). */
 if ((i & 7) == 7) watchdog_update( );
 }

 if (f) f.close( );
 watchdog_update( );

 /* Log overflow if entries were lost */
 if (_pendingOverflow > 0) {
 char buf[64];
 snprintf(buf, sizeof(buf), "[LOG] WARN: %u log entries dropped (buffer full)", _pendingOverflow);
 emitLine(buf);
 _pendingOverflow = 0;
 }

 requestFsLock(false);
 __atomic_store_n(&_pendingCount, 0, __ATOMIC_RELEASE);
 /* WdtWindow auto-restores WDT ctx */
}


void LogManager::info(const char* tag, String msg) { log(LOG_INFO, tag, SYS_OK, msg); }
void LogManager::warn(const char* tag, String msg) { log(LOG_WARN, tag, SYS_OK, msg); }
void LogManager::error(const char* tag, String msg) { log(LOG_ERROR, tag, SYS_OK, msg); }
void LogManager::debug(const char* tag, String msg) { log(LOG_DEBUG, tag, SYS_OK, msg); }

void LogManager::setSaveToFile(bool enable) { _saveToFile = enable; }
void LogManager::setMinSerialLevel(LogLevel level) { _minSerialLevel = level; }


/* =========================================================================== */
/* BLACK BOX PROFILER */
/* ===========================================================================
 * SCRATCH REGISTER MAP (watchdog_hw->scratch[0..7])
 * ---------------------------------------------------------------------------
 * The watchdog scratch registers survive WDT reset and reboot via
 * `watchdog_reboot( )`, but are zeroed on power cycle / physical reset. They are
 * the post-crash forensic channel of this firmware (see performCrashAutopsy).
 *
 * scratch[0..2] — reserved by Pico SDK (boot/runtime). Do not touch.
 * scratch[3] — module trace (Core 0 + Core 1) via TRACE_MOD/setModule.
 * Packing:
 * bits 0..7 = Core 0 current mod
 * bits 8..15 = 0x80 (magic "valid") if Core 0 has set
 * bits 16..23 = Core 1 current mod
 * bits 24..31 = 0x80 (magic "valid") if Core 1 has set
 * Magic byte avoids false-positive from zeroed scratch.
 * scratch[4] — DOCUMENTED by SDK as overwritten by
 * `watchdog_reboot(pc, ...)` passing PC. Do NOT use as
 * data channel — the value passed to watchdog_reboot appears
 * here post-reset.
 * scratch[5] — DOCUMENTED by SDK as overwritten by
 * `watchdog_reboot(, sp, )` passing SP, however
 * empirically persists through `watchdog_reboot(0,0,0)`.
 * Used as discrimination magic by the autopsy:
 * 0xCA11B007 → SOFT PANIC (Core 1 heartbeat stuck).
 * 0xC1EA8007 → clean reboot (markCleanReboot).
 * If a future SDK stops preserving scratch[5], migrate
 * the magic to scratch[3] (share with trace via
 * reserving an additional bitfield).
 * scratch[6] — SOFT PANIC payload: (deadCore<<24)|(mod0<<16)|(mod1<<8).
 * scratch[7] — SOFT PANIC payload: elapsed_ms since last heartbeat.
 * ========================================================================= */

/** @brief Set the currently executing module for crash forensics.
 * Precondition: LogManager::begin( ) (or explicit captureBootSnapshot( )) has already run —
 * otherwise the previous boot's scratch[3] is lost before the autopsy. */
void LogManager::setModule(int core, uint8_t mod) {
 _coreModule[core] = mod;
 _moduleStartTime[core] = millis( );
 _coreHeartbeat[core] = millis( );
 /* Persist current module to scratch[3] (see SCRATCH REGISTER MAP above
 * for packing and use in post-HW WDT autopsy). */
 if (core == 0) {
 watchdog_hw->scratch[3] = (watchdog_hw->scratch[3] & 0xFFFF0000u) | 0x8000u | (mod & 0xFFu);
 } else if (core == 1) {
 watchdog_hw->scratch[3] = (watchdog_hw->scratch[3] & 0x0000FFFFu) | 0x80000000u | (((uint32_t)mod & 0xFFu) << 16);
 }
}

uint8_t LogManager::getModule(int core) {
 if (core < 0 || core > 1) return 0;
 return _coreModule[core];
}

void LogManager::heartbeat(int core) {
 _coreHeartbeat[core] = millis( );
}

void LogManager::setCorePaused(int core, bool paused) {
 _corePaused[core] = paused;
 if (!paused) {
 uint32_t now = millis( );
 _coreHeartbeat[core] = now;
 _moduleStartTime[core] = now;
 }
}

/**
 * @brief Enable cross-core monitoring after boot complete.
 *
 * Must be called at the end of setup( ), after all subsystems
 * are initialized and both cores are in normal operation.
 * Forces heartbeat refresh to avoid false detection of stale
 * heartbeat during boot.
 */
void LogManager::enableHealthCheck( ) {
 uint32_t now = millis( );
 /* Force fresh heartbeats for both cores */
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
 * completes (_bootReady), instead of using a fixed millis( ) threshold.
 * This covers boots of any duration (30-60s with graph preloading).
 *
 * Phase 1 (>8s stale): Reboot with crash data in watchdog scratch registers.
 */

void LogManager::checkCrossCoreHealth( ) {
 uint32_t now = millis( );
 int thisCore = get_core_num( );
 int otherCore = (thisCore == 0) ? 1 : 0;

 /* Dynamic grace period: disabled until 5s after explicit enable */
 if (_healthCheckEnabledAt == 0) return;
 if (now - _healthCheckEnabledAt < 5000) return;

 if (_corePaused[otherCore]) return;

 uint32_t lastBeat = _coreHeartbeat[otherCore];

 /* Use signed cast to avoid false-positive in cross-core race.
 * Scenario: Core 0 reads now=T, Core 1 writes heartbeat=T+δ (δ>0, race
 * between the two loads), Core 0 reads lastBeat=T+δ. With unsigned subtract,
 * elapsed = T - (T+δ) = UINT32_MAX - δ + 1 ≈ 4e9 ms — triggers false panic.
 * Signed subtract (wrap-safe up to 24 days) treats small δ as small
 * negative elapsed, which does not enter the panic branch. */
 int32_t elapsed = (int32_t)(now - lastBeat);

 /* Threshold 15s: larger than normal WDT (8.3s) and tolerates bursts where
 * saveConfiguration extends WDT to 30s + multicore_lockout blocks
 * Core 1 for several cumulative seconds between consecutive saves.
 * HW WDT on Core 0 remains the backstop for real hangs. */
 if (elapsed > 15000) {
 watchdog_hw->scratch[5] = 0xCA11B007;
 watchdog_hw->scratch[6] = (otherCore << 24) | (_coreModule[0] << 16) | (_coreModule[1] << 8);

 /* Guard the real elapsed (time since last heartbeat). */
 watchdog_hw->scratch[7] = (uint32_t)elapsed;

 watchdog_reboot(0, 0, 0);
 while(1);
 }
}

void LogManager::markCleanReboot( ) {
 /* Arduino-Pico implements rp2040.reboot( ) via watchdog_reboot, so
 * watchdog_caused_reboot( ) returns true even on intentional reboot.
 * We mark scratch[5] with a magic distinct from soft panic — autopsy reads,
 * recognizes, and skips the FATAL emission (see SCRATCH REGISTER MAP above). */
 watchdog_hw->scratch[5] = 0xC1EA8007;
}

void LogManager::safeReboot( ) {
 /* Applies the correct reboot pattern.
 * Root cause "reload confirm bricks 100%": watchdog_enable
 * (500, 1) leaves ENABLE bit set in watchdog ctrl. After reset fires,
 * watchdog HW is NOT reset by PSM (not in PSM_WDSEL_BITS — only
 * physical power cycle zeros it). LOAD=500ms persists; ENABLE persists. Boot
 * post-reset has ~150ms BootROM + boot2 + arduino-pico init ≈ 500ms+
 * — right in the firing window. Watchdog fires before runtime calls
 * watchdog_update → infinite reset loop until power cycle.
 *
 * Correct pattern (same as SDK pico-sdk hardware_watchdog/watchdog.c::_watchdog_enable with
 * delay_ms=0):
 * 1. PSM_WDSEL: all peripherals except ROSC/XOSC (keeps PLLs)
 * 2. Clear ENABLE explicitly (via CLR alias)
 * 3. scratch[4] = 0 (boot mode = normal, not stage2)
 * 4. LOAD = 0xFFFFFF (24-bit max ≈ 8s) — time for firmware to reach
 * the first watchdog_update in loop( )
 * 5. TRIGGER only (NOT ENABLE) — forces immediate reset without arming
 * timer post-reset
 *
 * Full sequence:
 * 1. markCleanReboot — autopsy doesn't log FATAL on next boot
 * 2. Serial.flush + Serial.end — clean USB CDC DETACH
 * 3. delay 100ms — host processes disconnect
 * 4-8. MMIO sequence above
 * 9. while(1) — wait for reset to arrive */
 markCleanReboot( );
 Serial.println("[SYS] Rebooting...");
 Serial.flush( );
 delay(50);
 Serial.end( );
 delay(100);

 /* RP2040 MMIO addresses — prefix LM_ to avoid clash with #defines from
 * pico-sdk (hardware/regs/watchdog.h defines WATCHDOG_CTRL_OFFSET etc).
 * Same values as src/ota/applier.cpp. */
 constexpr uint32_t LM_WD_BASE = 0x40058000u;
 constexpr uint32_t LM_WD_CTRL_OFF = 0x00u;
 constexpr uint32_t LM_WD_LOAD_OFF = 0x04u;
 constexpr uint32_t LM_WD_SCRATCH4 = 0x1Cu;
 constexpr uint32_t LM_WD_SET_ALIAS = 0x2000u;
 constexpr uint32_t LM_WD_CLR_ALIAS = 0x3000u;
 constexpr uint32_t LM_WD_ENABLE_BIT = (1u << 30);
 constexpr uint32_t LM_WD_TRIG_BIT = (1u << 31);
 constexpr uint32_t LM_PSM_BASE = 0x40010000u;
 constexpr uint32_t LM_PSM_WDSEL_OFF = 0x18u;
 constexpr uint32_t LM_PSM_BITS_ALL = 0x0001FFFFu;
 constexpr uint32_t LM_PSM_ROSC_BIT = 0x00000001u;
 constexpr uint32_t LM_PSM_XOSC_BIT = 0x00000002u;
 constexpr uint32_t LM_PSM_RESET_MASK = LM_PSM_BITS_ALL & ~(LM_PSM_ROSC_BIT | LM_PSM_XOSC_BIT);

 /* (1) PSM_WDSEL: all peripherals except ROSC/XOSC. */
 *(volatile uint32_t*)(LM_PSM_BASE + LM_PSM_WDSEL_OFF) = LM_PSM_RESET_MASK;

 /* (2) Clear ENABLE explicitly via CLR alias — disables the timer
 * before trigger so it doesn't persist post-reset. */
 *(volatile uint32_t*)(LM_WD_BASE + LM_WD_CLR_ALIAS + LM_WD_CTRL_OFF) = LM_WD_ENABLE_BIT;

 /* (3) scratch[4] = 0 (boot mode = normal). */
 *(volatile uint32_t*)(LM_WD_BASE + LM_WD_SCRATCH4) = 0;

 /* (4) LOAD = max (24-bit = 0xFFFFFF ≈ 8s). Watchdog HW persists post-reset
 * (not in PSM_WDSEL); large LOAD gives boot time to complete. */
 *(volatile uint32_t*)(LM_WD_BASE + LM_WD_LOAD_OFF) = 0xFFFFFFu;

 /* (5) TRIGGER via SET alias — TRIGGER only, NOT ENABLE. Immediate reset. */
 *(volatile uint32_t*)(LM_WD_BASE + LM_WD_SET_ALIAS + LM_WD_CTRL_OFF) = LM_WD_TRIG_BIT;

 __asm volatile("dsb");
 while (1) tight_loop_contents( );
}

/**
 * @brief Analyze watchdog scratch registers after a crash-triggered reboot.
 * Logs the dead core, module, and duration of the freeze.
 */
void LogManager::performCrashAutopsy( ) {
 /* Idempotence: autopsy runs exactly once per session. Subsequent
 * calls (ex: begin( ) re-called by clear log) are no-op — avoids
 * false HW WATCHDOG, since watchdog_caused_reboot( ) remains true
 * for the entire session post-reboot. */
 if (_autopsyPerformed) return;
 _autopsyPerformed = true;

 /* Possible scenarios:
 * (1) scratch[5] == 0xCA11B007: our soft panic (Core 1 stuck).
 * (2) scratch[5] == 0xC1EA8007: clean reboot (markCleanReboot was called).
 * (3) scratch[5] == 0xA11FA1E5: previous session was ALIVE (autopsy passed)
 * but ended with wdReset — EXTERNAL cause (picotool upload, hard
 * fault, hardware reset). NOT an application code crash since
 * SIMUT never enables WDT in normal operation (markWdtActive never
 * called → WdtWindow is no-op). Demote to INFO.
 * (4) watchdog_caused_reboot( ) && no magic: WDT REASON set by
 * external reset WITHOUT passing through previous autopsy (rare: 1st boot
 * post-pio upload if previous session crashed before autopsy).
 * (5) none of the above: power cycle / reset button. */

 /* Safety net for refactors: captureBootSnapshot( ) should have
 * run before (via begin( )). If it didn't, scratch[3] may already have been
 * overwritten by some prior setModule and the modTrace below will be the live
 * state, not the pre-boot one. */
 if (!_preBootSnapshotTaken) {
 Serial.println("[LOG] performCrashAutopsy called before captureBootSnapshot!");
 }

 bool wdReset = watchdog_caused_reboot( );
 uint32_t mark = watchdog_hw->scratch[5];

 if (mark == 0xCA11B007) {
 uint32_t data = watchdog_hw->scratch[6];
 uint32_t stuckTime = watchdog_hw->scratch[7];
 int deadCore = (data >> 24) & 0xFF;
 int mod0 = (data >> 16) & 0xFF;
 int mod1 = (data >> 8) & 0xFF;

 char msg[200];
 snprintf(msg, sizeof(msg), "SOFT PANIC: Core %d heartbeat stuck in [%s] for %lums. C0=[%s] C1=[%s]",
 deadCore,
 deadCore == 0 ? (mod0 <= MOD_NAMES_MAX ? MOD_NAMES[mod0] : "UNK") : (mod1 <= MOD_NAMES_MAX ? MOD_NAMES[mod1] : "UNK"),
 stuckTime,
 mod0 <= MOD_NAMES_MAX ? MOD_NAMES[mod0] : "UNK",
 mod1 <= MOD_NAMES_MAX ? MOD_NAMES[mod1] : "UNK");

 logCode(LOG_FATAL, "SYS", SYS_BOOT, deadCore, String(msg));
 watchdog_hw->scratch[5] = 0;
 } else if (wdReset && mark == 0xC1EA8007) {
 /* Intentional reboot via markCleanReboot( ). Silent. */
 watchdog_hw->scratch[5] = 0;
 } else if (wdReset && mark == 0xA11FA1E5) {
 /* "Alive" magic set at end of previous autopsy — means the
 * past session ran normally. Since SIMUT never enables
 * WDT in operation (markWdtActive never called, WdtWindow no-op),
 * wdReset+alive = EXTERNAL cause: picotool upload, hard fault, reset
 * pin (which clears REASON, but if it reaches here via another path), etc.
 * NOT an application code crash. */
 uint32_t modTrace = _preBootSnapshotTaken ? _preBootScratch4
 : watchdog_hw->scratch[3];
 uint8_t c0Mod = (modTrace >> 0) & 0xFF;
 uint8_t c1Mod = (modTrace >> 16) & 0xFF;
 const char* c0Name = (c0Mod <= MOD_NAMES_MAX) ? MOD_NAMES[c0Mod] : "UNK";
 const char* c1Name = (c1Mod <= MOD_NAMES_MAX) ? MOD_NAMES[c1Mod] : "UNK";
 char msg[200];
 snprintf(msg, sizeof(msg),
 "Boot after external reset (likely picotool upload). C0 last=[%s] C1 last=[%s]",
 c0Name, c1Name);
 logCode(LOG_INFO, "SYS", SYS_BOOT, 0, String(msg));
 /* Don't clear scratch[5] here — the set below (alive) overwrites. */
 } else if (wdReset) {
 /* Hardware watchdog fired (8.3s) without our soft panic having triggered
 * and without clean reboot mark. Core 0 didn't call watchdog_update( )
 * — loop stuck on Core 0. RAM was zeroed; context comes from scratch[4]
 * which setModule( ) updates in real time.
 * Uses the pre-boot snapshot (captured in captureBootSnapshot( ), called
 * by begin( ) before the 1st TRACE_MOD) to see the PREVIOUS crash module. */
 uint32_t modTrace = _preBootSnapshotTaken ? _preBootScratch4
 : watchdog_hw->scratch[3];
 uint8_t c0Valid = (modTrace >> 8) & 0xFF;
 uint8_t c0Mod = (modTrace >> 0) & 0xFF;
 uint8_t c1Valid = (modTrace >> 24) & 0xFF;
 uint8_t c1Mod = (modTrace >> 16) & 0xFF;

 char msg[200];
 if (c0Valid == 0x80) {
 /* Real crash: Core 0 was active (called TRACE_MOD) and hung.
 * Trace identifies the module that was executing at the time. */
 const char* c0Name = (c0Mod <= MOD_NAMES_MAX) ? MOD_NAMES[c0Mod] : "UNK";
 if (c1Valid == 0x80) {
 const char* c1Name = (c1Mod <= MOD_NAMES_MAX) ? MOD_NAMES[c1Mod] : "UNK";
 snprintf(msg, sizeof(msg),
 "HW WATCHDOG: Core 0 loop stalled (no feed in WDT window). C0=[%s] C1=[%s] sc3=0x%08lx",
 c0Name, c1Name, (unsigned long)modTrace);
 } else {
 snprintf(msg, sizeof(msg),
 "HW WATCHDOG: Core 0 loop stalled (no feed in WDT window). C0=[%s] sc3=0x%08lx",
 c0Name, (unsigned long)modTrace);
 }
 logCode(LOG_FATAL, "SYS", SYS_BOOT, 0, String(msg));
 } else {
 /* No Core 0 trace (c0Valid != 0x80) → previous boot NEVER called
 * TRACE_MOD(0,...). Most likely cause: picotool restart post-upload
 * uses watchdog reset as mechanism, leaving WATCHDOG.REASON set
 * (register only clears on POR/external reset, RP2040 datasheet).
 * Real pre-TRACE_MOD crash exists but is rare. Demote to INFO
 * to avoid alarm — repeats suppressed via scratch[5] magic. */
 snprintf(msg, sizeof(msg),
 "Boot after watchdog reset (no trace — likely post-flash by picotool; sc3=0x%08lx)",
 (unsigned long)modTrace);
 logCode(LOG_INFO, "SYS", SYS_BOOT, 0, String(msg));
 /* scratch[5] will be set as ALIVE at end of autopsy. */
 }
 watchdog_hw->scratch[3] = 0; /* Clear for next autopsy */
 } else {
 /* Power cycle / physical reset: clear scratch[4] to not contaminate
 * subsequent autopsy if the register has initial garbage. */
 watchdog_hw->scratch[3] = 0;
 }

 /* Mark the current session as "alive". On next boot,
 * if wdReset is true AND mark is this magic, we know the past
 * session was running normally when interrupted — almost
 * always external cause (picotool upload, reset pin via certain paths,
 * hard fault), not application code crash. markCleanReboot and
 * soft panic overwrite this magic with their own. */
 watchdog_hw->scratch[5] = 0xA11FA1E5;
}


/* =========================================================================== */
/* TRANSLATION TABLE — LogCode → Human-Readable Text */
/* =========================================================================== */
/**
 * @brief Translate a numeric LogCode to readable text.
 *
 * Used by serial (when extraMsg is empty) and by the /api/logs API
 * to reconstruct messages from compact binary records.
 *
 * @param code Log code (LogCode enum).
 * @return Pointer to constant string with the description.
 */
static const char* translateCodeEn(uint16_t code) {
 switch ((LogCode)code) {
 /* ── System (0–9) ── */
 case SYS_OK: return "OK";
 case SYS_BOOT: return "System boot";
 case SYS_REBOOT_USER: return "User-requested reboot";
 case SYS_HEAP_LOW: return "Heap memory low";
 case SYS_UPTIME_MARK: return "Uptime milestone";

 /* ── WiFi (10–15) ── */
 case SYS_WIFI_CONNECT: return "WiFi connecting";
 case SYS_WIFI_DISCONNECT: return "WiFi disconnected";
 case SYS_WIFI_SCAN: return "WiFi scanning";
 case SYS_NTP_SYNC: return "NTP synced";
 case SYS_IP_ACQUIRED: return "IP acquired";
 case SYS_AP_START: return "AP mode started";

 /* ── Storage (20–25) ── */
 case SYS_STORAGE_FAIL: return "Storage failure";
 case SYS_STORAGE_SAVE: return "Config saved";
 case SYS_STORAGE_ROTATE: return "Storage rotated";
 case SYS_STORAGE_FORMAT: return "Flash formatting";
 case SYS_STORAGE_RECOVER: return "Storage recovered";
 case SYS_STORAGE_MIGRATED:return "Config migrated";

 /* ── Telemetry (30–37) ── */
 case SYS_TEL_SENT: return "Telemetry sent";
 case SYS_TEL_FAIL: return "Telemetry failed";
 case SYS_TEL_RETRY: return "Telemetry retry";
 case SYS_TEL_QUEUE: return "Telemetry queued";
 case SYS_TEL_SSL: return "SSL cert loaded";
 case SYS_TEL_MQTT_CONN: return "MQTT connected";
 case SYS_TEL_MQTT_DISC: return "MQTT disconnected";
 case SYS_TEL_MQTT_PUB: return "MQTT published";

 /* ── Sensor (100–106) ── */
 case LOG_SENSOR_REC: return "Sensor recovered";
 case ERR_SENSOR_TIMEOUT: return "Sensor timeout";
 case ERR_SENSOR_CHECKSUM: return "Sensor checksum error";
 case ERR_SENSOR_CRC: return "Sensor CRC error";
 case ERR_SENSOR_RANGE: return "Sensor out of range";
 case ERR_SENSOR_MISMATCH: return "Hardware mismatch";
 case ERR_SENSOR_MISSING: return "Sensor missing";

 /* ── UI events (200–202) ── */
 case EVT_UI_TOUCH: return "Touch event";
 case EVT_DISPLAY_RESTART: return "Display restarted";
 case EVT_GRAPH_RENDER: return "Graph rendered";

 /* ── Security (300–306) ── */
 case SEC_LOGIN_SUCCESS: return "Login success";
 case SEC_LOGIN_FAIL: return "Login failed";
 case SEC_UNAUTHORIZED: return "Unauthorized access";
 case SEC_CONFIG_CHANGED: return "Config changed";
 case SEC_SESSION_EXPIRE: return "Session expired";
 case SEC_FILE_UPLOAD: return "File uploaded";
 case SEC_FILE_DELETE: return "File deleted";

 /* ── App lifecycle (400–410) ── */
 case APP_DISPLAY_LAUNCHED: return "Display launched on Core 1";
 case APP_TOUCH_CAL_INITIAL: return "Initial touch cal saved";
 case APP_TOUCH_CAL_REQUIRED: return "Touch calibration required";
 case APP_AP_MODE_TRIGGERED: return "AP mode triggered by user";
 case APP_READY: return "System ready";
 case APP_READY_AP: return "System ready (AP mode)";
 case APP_STORAGE_CRITICAL: return "Storage critical failure";
 case APP_SENSORS_CALIBRATED: return "Sensors calibrated";
 case APP_NTP_CORRECTING: return "NTP correcting timestamps";
 case APP_NTP_CORRECTED: return "Timestamps corrected";
 case APP_CACHE_INVALIDATED: return "Graph caches invalidated";

 /* ── App UI (440–449) ── */
 case APP_UI_THEME_CHANGED: return "Theme changed via UI";
 case APP_UI_LANG_CHANGED: return "Language changed via UI";
 case APP_UI_ALARM_SAVED: return "Alarm limits saved via UI";
 case APP_UI_TOUCH_CAL_SAVED: return "Touch cal saved to flash";
 case APP_UI_TOUCH_SENS_SAVED: return "Touch sensitivity saved";
 case APP_UI_PIN_CHANGED: return "Display PIN changed";
 case APP_UI_SOUND_SAVED: return "Sound settings saved";
 case APP_UI_ALARM_SILENCED: return "Alarm silenced via UI";
 case APP_UI_ALARM_SILENCE_EXP:return "Alarm silence expired";
 case APP_UI_ALARM_DEACTIVATED:return "All alarms deactivated (RAM)";

 /* ── Alarm state (470–472) ── */
 case APP_ALARM_TRIGGERED: return "Alarm triggered";
 case APP_ALARM_CLEARED: return "Alarm cleared";
 case APP_ALARM_SILENCE_CANCEL:return "Alarm silence cancelled";

 /* ── Cache (480–489) ── */
 case APP_CACHE_MINMAX_FULL: return "Min/Max cache loaded";
 case APP_CACHE_MINMAX_PARTIAL:return "Min/Max cache partial";
 case APP_CACHE_GRAPH_STARTED: return "Graph cache refresh started";
 case APP_CACHE_GRAPH_DONE: return "Graph cache refresh done";
 case APP_CACHE_GRAPH_AMBIENT: return "Graph cache: ambient";
 case APP_CACHE_GRAPH_BOARD: return "Graph cache: board temp";
 case APP_CACHE_PRELOAD_DONE: return "Graph cache preload done";
 case APP_GRAPH_LOADING: return "Graph loading";
 case APP_GRAPH_BUDGET: return "Graph render budget exceeded";
 case APP_PRELOAD_BUDGET: return "Preload budget exceeded";

 /* ── Safety (500–503) ── */
 case APP_DISPLAY_PAUSE_STUCK: return "Display pause stuck >5s";
 case APP_YIELD_STUCK: return "Yield stuck >10s";
 case APP_CORE1_DEAD: return "Core 1 dead >10s, restarting";
 case APP_FLASH_BUSY: return "Flash busy collision";

 /* ── History (510–513) ── */
 case APP_HISTORY_SAVED: return "History record saved";
 case APP_HEAP_REPORT: return "Heap status report";
 case APP_HIST_NO_TIME_REF: return "History skip: no time reference";
 case APP_HIST_TIME_REF_RECOVERED: return "History resumed: time reference acquired";

 /* ── Network extended (520–527) ── */
 case NET_DHCP_MODE: return "DHCP mode enabled";
 case NET_STATIC_MODE: return "Static IP mode enabled";
 case NET_STARTING: return "WiFi manager starting";
 case NET_SSID_MISSING: return "WiFi SSID not configured";
 case NET_PROVISIONAL_TIME: return "Provisional time set from flash";
 case NET_CONNECT_TIMEOUT: return "WiFi connect timeout";
 case NET_DORMANT_MODE: return "WiFi dormant mode";
 case NET_SHOW_IP: return "Show IP";

 /* ── Telemetry extended (540–547) ── */
 case TEL_HTTP_INIT: return "HTTP transport initialized";
 case TEL_MQTT_INIT: return "MQTT transport initialized";
 case TEL_MQTT_CONNECTING: return "MQTT connecting";
 case TEL_CERT_EMPTY: return "cert.pem empty, insecure mode";
 case TEL_CERT_READ_ERR: return "cert.pem read error";
 case TEL_CERT_MISSING: return "No cert.pem, insecure mode";
 case TEL_FORCE_SYNC: return "Forcing telemetry sync";
 case TEL_BACKOFF_SUPPRESSED: return "Retry logs suppressed";

 /* ── Storage extended (560–565) ── */
 case STO_WRITE_FAILED: return "History write failed";
 case STO_CORRECT_BUDGET: return "Timestamp correction budget exceeded";
 case STO_ENFORCE_BUDGET: return "Storage limit budget exceeded";
 case STO_ENFORCE_SKIP_ACTIVE: return "Skipping active log file";
 case STO_STATS_REPORT: return "Storage stats report";
 case STO_CONFIG_REPORT: return "Config report";

 /* ── Web (570–574) ── */
 case WEB_SERVER_STARTED: return "Web server started";
 case WEB_DISCONNECT_FILE: return "Client disconnected (file)";
 case WEB_DISCONNECT_HISTORY: return "Client disconnected (history)";
 case WEB_SCREENSHOT_ABORTED: return "Screenshot aborted by client";
 case WEB_UPLOAD: return "File uploaded";

 /* ── Config (580–581) ── */
 case CFG_THEME_APPLIED: return "Theme applied";
 case CFG_THEME_NOT_FOUND: return "Theme not found";

 /* ── CLI (585) ── */
 case CLI_UNKNOWN_CMD: return "Unknown command";

 /* ── Sensor (590) ── */
 case SENSOR_RUNTIME_LOADED: return "Runtime sensors loaded";

 /* ── Display (600) ── */
 case DSP_FORCE_UNPAUSE: return "Force unpause";

 case ERR_UNKNOWN: return "Unknown error";
 default: return "?";
 }
}


const char* LogManager::translateCode(uint16_t code) {
 /* PT (and any other languages) come from .lng via @LOGCODES.
 * Without .lng loaded or in EN: uses translateCodeEn inline.
 * Lookup miss (untranslated entry): also falls back to EN. */
 if (_language != LANG_EN) {
 const char* t = DisplayManager::logcodeLookup(code);
 if (t) return t;
 }
 return translateCodeEn(code);
}

/* TRL via hash. In EN or without .lng, returns the EN literal
 * passed by the caller (zero-cost). In non-EN, does binary search in
 * @TRL of .lng by FNV-1a hash of EN; miss falls back to EN too. */
const char* LogManager::tr(const char* en) const {
 if (_language == LANG_EN || !en) return en;
 const char* t = DisplayManager::trlLookup(en);
 return t ? t : en;
}
