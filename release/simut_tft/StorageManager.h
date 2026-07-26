/**
 * @file StorageManager.h
 * @brief LittleFS storage layer with dual-bank CRC32 configuration and flash safety.
 * @details Manages all persistent data: system configuration (binary with CRC32
 * and backup), CSV history files, telemetry cursor, and calibration
 * data. Provides two-tier flash locking: lightweight mutex for reads
 * and multicore_lockout for writes (protects XIP during erase/program).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include "pico/mutex.h"
#include "SystemDefs.h"
#include "HistoryCodec.h"
#include "HistoryV4.h"
#include "SensorHelpers.h"

#define DIR_CONFIG "/config"
#define FILE_CONFIG "/config/system.bin"
#define FILE_BACKUP "/config/system.bak"
#define FILE_TMP "/config/system.tmp"
#define FILE_TCURSOR "/config/t_cursor.bin"
#define DIR_HISTORY "/history"
#define DIR_LANG "/lang"

typedef void (*FlashLockCallback)(bool);

/** Callback for cooperative "quiet mode" in large saves.
 * enable=true: Core 0 asks Core 1 to freeze in a RAM-only loop (IRQs off).
 * Returns true if Core 1 ACKed, false if Core 1 did not respond.
 * enable=false: releases Core 1 from quiet mode. Return value ignored. */
typedef bool (*BigSaveQuietCallback)(bool);

class StorageManager {
public:
 StorageManager( );
 bool begin( );
 void update( );

 void setLockCallback(FlashLockCallback cb) { _lockCb = cb; }
 /** When set, saveConfiguration replaces the
 * IRQ-based multicore_lockout sequence with a single cooperative
 * quiet mode, avoiding cascading lockout stuck. */
 void setBigSaveQuietCallback(BigSaveQuietCallback cb) { _bigSaveQuietCb = cb; }


 void enterFlashReadLock( );
 void exitFlashReadLock( );
 /** Variant with timeout for paths that can
 * abandon gracefully instead of blocking indefinitely. Returns
 * true if acquired, false if timeout. Caller decides what to do.
 * Useful in web/CLI handlers when Core 1 is in a long flash heavy op
 * — instead of hanging the request, returns 503 Busy. */
 bool enterFlashReadLockTimeout(uint32_t timeout_ms);

 void enterFlashSafeMode( );
 void exitFlashSafeMode( );

 /** RAII guard for flash read lock. Usage:
 * { ReadGuard rg(&storageMgr); ... } // lock released on scope exit. */
 struct ReadGuard {
 StorageManager* _sto;
 ReadGuard(StorageManager* s) : _sto(s) { if (_sto) _sto->enterFlashReadLock( ); }
 ~ReadGuard( ) { if (_sto) _sto->exitFlashReadLock( ); }
 };

 bool loadConfiguration( );
 bool saveConfiguration( );
 void resetToFactory( );

 /** @return true if the last call to `saveConfiguration( )` skipped
 * writing because CRC matched the last save. Callers use this to avoid
 * redundant audit logs after bursts of "Save" clicks with no change. */
 bool lastSaveWasNoOp( ) const { return _lastSaveWasNoOp; }

 /** @return true if enough time has passed since the last real save
 * to allow another. Server-side rate-limit against save bursts
 * that overload LittleFS GC. Handlers should reject with 429 if
 * it returns false. Default: 1 save / 1s. */
 bool canSaveNow( ) const;

 /* Uses TouchPriority::isActive( ) from TouchPriority.h. */

 /** @return true if there is a pending HIST record waiting for flush.
 * AppManager can call after interaction ends to force flush. */
 bool hasPendingHist( ) const { return _pendingHistValid; }

 /** Force flush of the pending HIST record buffered during touch
 * priority. Called by AppManager on touch-active→touch-free transition.
 * No-op if nothing pending; bypasses touch checker to not re-defer. */
 bool flushPendingHist( );

 SystemConfig& getConfig( );
 SensorRecord* getSensorByGpio(uint8_t gpio);

 String getStatsReport( );
 bool canWriteHistory(size_t sizeToWrite);

 bool writeHistoryEntry(const BinaryHistoryRecord& rec);
 String getHistoryFileName( );
 void getHistoryFileName(char* buf, size_t len); /**< Buffer version. */

 /* ── V4 history API ─────────────────────────────────────────── */

 /** Write one V4 record. Delegates touch-priority buffering and flash I/O. */
 bool writeHistoryEntryV4(const int64_t *values, uint8_t measureCount, uint32_t epoch);
 /** T2.1: drain the RAM history batch to flash (one Core-1 pause for
  * the whole drain). Called on batch-full/age, before reboots and on
  * write memory. Safe to call with an empty batch. */
 bool flushHistoryBatch( );
 /** Invalidate the in-RAM V4 codec state after an EXTERNAL mutation of
  * the current history file (e.g. web delete). Without this the writer
  * keeps appending to a recreated HEADERLESS file until reboot and all
  * those records are unreadable. Next write rescans/recreates. */
 void invalidateV4Codec( ) { _histV4CodecValid = false; }

 /** @return today's V4 history file path (e.g. /history/20260721.sim4). */
 String getHistoryFileNameV4( );
 void getHistoryFileNameV4(char* buf, size_t len);

 /**
  * @brief Caminho do arquivo V4 do dia a que pertence @p epoch.
  * @details A2: o dreno do batch grava entradas bufferizadas que podem ser
  * de ONTEM. Resolver o caminho por "agora" as colocava no arquivo de hoje.
  * A sobrecarga sem argumento continua existindo e é o caminho de "agora".
  * @param epoch Timestamp Unix da amostra.
  * @return Caminho absoluto (e.g. /history/20260721.sim4).
  */
 String getHistoryFileNameV4(uint32_t epoch);

 /** @return pointer to the current V4 schema (read-only, valid while file is open). */
 const HistV4State* getV4Schema( ) const { return _histV4CodecValid ? &_histV4State : nullptr; }

 /** @return number of measurements in the current V4 schema, or 0 if not valid. */
 uint8_t getV4MeasureCount( ) const { return _histV4CodecValid ? _histV4State.measureCount : 0; }

 /** @return true if a V4 file is currently open and its schema is ready. */
 bool isV4Active( ) const { return _histV4CodecValid; }

 /** Bootstrap the V4 history codec on first use.
  * If the codec is not yet valid (_histV4CodecValid=false), this
  * creates today's .sim4 file with the schema header and populates
  * _histV4State. Idempotent: safe to call multiple times.
  * Must be called before the first writeHistoryEntryV4() if
  * getV4Schema() returns nullptr (chicken-and-egg bootstrap). */
 void ensureV4Schema( );

 /** Rebuild the day's V4 schema from the CURRENT sensor config, in place.
  *
  * A .sim4 stores its measurement schema in the header, and ensureV4Schema
  * restores that header from the existing file rather than rebuilding it. So
  * once a sensor identity changes, every value stops matching
  * (strcmp(schemaHwId, cfg hwId)) and the rest of the day records nothing —
  * invalidating the codec alone does not help, it just re-reads the same
  * stale header.
  *
  * DESTRUCTIVE: records are bit-packed against the header, so the schema
  * cannot be swapped under them — the day's file is recreated and today's
  * earlier records are lost. The CLI demands `confirm` because of it.
  *
  * PREFER migrateV4Schema( ), which carries the day's records over instead of
  * discarding them. This one is the fallback for the case migration cannot
  * handle — a source file too damaged to decode — and stays reachable from the
  * CLI (`sensor reschema confirm`) and from the web with ?force=1.
  *
  * One file per day is preserved either way.
  *
  * @param outMeasures receives the new measurement count (may be nullptr).
  * @return false if the schema would be empty or the file could not be
  *         written; the codec is left invalid so the next tick re-bootstraps.
  */
 bool rebindV4Schema(uint8_t* outMeasures = nullptr);

 /** Rewrite today's .sim4 against the current slots, KEEPING the day's
  *  records. Streaming (constant ~5.6 KB of RAM), verified against the
  *  source before the swap, original intact on any failure.
  *  Returns false when the source is unreadable — rebindV4Schema is then
  *  the destructive fallback. */
 bool migrateV4Schema(uint8_t* outMeasures = nullptr, uint32_t* outRecords = nullptr,
                      uint8_t* outCarried = nullptr);

 /** Build a V4 schema (sensor + measurement table + string pool) from SystemConfig.
  * Used when creating a new V4 history file. The schema is stored in the file header
  * so any reader can interpret the data without external configuration.
  *
  * @param sensors     Output sensor definitions (caller-provided buffer).
  * @param sensorCount Output count of active sensors.
  * @param measures    Output measurement definitions.
  * @param measureCount Output count of measurements (sum of channels across sensors).
  * @param strPool     Output string pool bytes.
  * @param strPoolSize Output pool size.
  * @return true on success. */
 bool buildMeasureSchema(HistV4SensorDef *sensors, uint8_t &sensorCount,
                         HistV4MeasureDef *measures, uint8_t &measureCount,
                         uint8_t *strPool, uint8_t &strPoolSize);

 uint32_t getLastRecordedTimestamp( );
 uint32_t getHistoryDaysMask(int year, int month);

 uint32_t getLastSentTimestamp( );
 void setLastSentTimestamp(uint32_t ts);
 void resetTelemetryCursor( ); /**< CMD_TEL_RESET: invalidates RAM cache + deletes flash file. */

 static String getBoardSerialNumber( );
 bool getCalibrationData(const uint8_t* rom, String& outId, float& outOffset, String& outName);
 /* Ambient (DHT22) lookup in calib.csv. Key = picoUID 16 hex.
 * `prefix` must be 't' (temperature) or 'u' (humidity) — the discriminator
 * is in the ID field (second column), ex: `t01` or `uA1`. outId is returned
 * WITHOUT the prefix (ex: `01` or `A1`). */
 bool getCalibrationDataAmbient(char prefix, String& outId, float& outOffset, String& outName);
 long getCalibrationVersion(String path);
 bool processCalibrationUpload( );

 bool lockHeavyTask( );
 void unlockHeavyTask( );
 bool isHeavyTaskLocked( ) const;

 /** Hash v1 (new standard): username-salt, PASSWORD_HMAC_ROUNDS rounds,
 * 32 hex chars (128 bits). Used for creating/changing passwords. */
 String hashPassword(const String& username, const String& plainPassword);

 /** Hash legacy: username-salt, 2500 rounds, 30 hex chars (120 bits).
 * Used ONLY in transparent login migration. */
 String hashPasswordLegacy(const String& username, const String& plainPassword);

 /** Hash v1 with random salt: userSalt[8], PASSWORD_HMAC_ROUNDS, 32 hex chars.
 * Used in login verification with hashVersion >= 1. */
 String hashPasswordV1(const String& username, const String& plainPassword,
 const uint8_t* userSalt);

 /** Fill buf[8] with random values from ROSC (rp2040.hwrand32). */
 void generateSalt(uint8_t* buf);

 String sha256Hex(const String& input);
 void flushCursorIfDirty( );
 void invalidateOldestFileCache( ) { _cachedOldestFile = ""; }

 /**
 * @brief Generate random initial admin password.
 *
 * Alphabet [A-Z2-9] of 32 chars (excludes O/0/I/1 for clarity).
 * Entropy: 32^8 ≈ 1.1 × 10^12 combinations. Uses `rp2040.hwrand32( )`,
 * which on RP2040 is backed by ROSC (ring oscillator) — hardware entropy.
 *
 * @param outPlain Output buffer (null-terminated).
 * @param bufSize Buffer size (needs ≥ 9 for 8 chars + '\0').
 */
 void generateInitialAdminPassword(char* outPlain, size_t bufSize);

 /** @return true if current config is in factory defaults —
 * i.e., admin[0] active with `mustChangePassword=true`. Calculated in real time. */
 bool isFactoryDefaults( ) const;

 /** @return plaintext of the random admin password generated by `loadDefaults( )`
 * in this session. Empty string if already changed OR if loadDefaults didn't run
 * (valid config loaded from flash). NEVER persisted. */
 const char* getInitialAdminPassword( ) const { return _initialAdminPassword; }

 /** Zeros `_initialAdminPassword` in RAM. Called automatically by
 * `saveConfiguration( )` when `admin.mustChangePassword` becomes false,
 * and by `loadConfiguration( )` when loading valid config from flash. */
 void clearInitialAdminPassword( );

 /**
 * @brief True if the display PIN is still the default
 * (factory defaults) and needs to be changed before the user can freely
 * operate the settings menu.
 *
 * Overlay in `reserved[26..27]` (SetupFlagsData). Legacy v13-v14 configs
 * without magic return false (assumes already configured — avoids
 * forcing change for those who only upgraded firmware).
 */
 bool mustChangePin( ) const;

 /** Clears `FLAG_MUST_CHANGE_PIN` in the SetupFlagsData overlay.
 * Called when user saves a PIN != "1234". */
 void clearMustChangePin( );

 /** Sets `FLAG_MUST_CHANGE_PIN` in the SetupFlagsData overlay.
 * Called in `loadDefaults( )` (factory reset). */
 void setMustChangePin( );

 /* =====================================================================
 * Network time overlay in reserved[28..47]
 * =====================================================================
 * Backward-compatible defaults: legacy configs without magic return
 * DNS auto + NTP enabled (identical to pre-feature behavior).
 * Any set* populates magic before updating flags. */

 /** @return true if DNS should be obtained via DHCP (default).
 * false = primary DNS in `staticDns` + secondary in overlay `dns2`. */
 bool isDnsAuto( ) const;

 /** Set the DNS_AUTO flag in overlay. Populates magic if not yet present. */
 void setDnsAuto(bool auto_);

 /** @return true if NTP sync is enabled (default). false = manual RTC. */
 bool isNtpEnabled( ) const;

 /** Set the NTP_ENABLED flag in overlay. Populates magic if not yet present. */
 void setNtpEnabled(bool enabled);

 /** @return Manual secondary DNS (null-terminated string). "" if not configured. */
 const char* getSecondaryDns( ) const;

 /** Set the secondary DNS in overlay (max 15 chars + '\0'). Populates magic. */
 void setSecondaryDns(const char* ip);

 /** @return History recording interval in minutes. Default 1 min if legacy overlay. */
 uint16_t getHistoryIntervalMin( ) const;

 /** Set the history interval (clamped to [HISTORY_INTERVAL_MIN_MIN, HISTORY_INTERVAL_MAX_MIN]). */
 void setHistoryIntervalMin(uint16_t minutes);
 SystemConfig _currentConfig;
 bool _isMounted = false;
 FlashLockCallback _lockCb = nullptr;
 BigSaveQuietCallback _bigSaveQuietCb = nullptr;
 /* true during saveConfiguration with active quiet mode;
 * enterFlashSafeMode/exitFlashSafeMode skip lockCb when set. */
 bool _inBigSave = false;
 mutex_t _fsReadMutex;

 bool _heavyTaskLocked = false;
 uint32_t _cachedLastSent = 0;
 bool _cursorDirty = false;
 uint32_t _cursorCoalesceTime = 0;
 bool _lastSaveWasNoOp = false; /**< True if saveConfiguration skipped due to identical CRC */
 volatile uint32_t _lastSaveMs = 0; /**< millis( ) of last real save (0 = never) */
 uint32_t _lastSavedCrc = 0; /**< CRC32 of last persisted save; skip-no-op when equal. */

 /* Uses TouchPriority::isActive( ). */

 /** Admin password plaintext in RAM (NEVER persisted to flash).
 * Populated by `generateInitialAdminPassword` during `loadDefaults( )`.
 * Zeroed by `clearInitialAdminPassword( )` when admin changes password OR
 * when valid config is loaded from flash (i.e., not factory). */
 char _initialAdminPassword[9] = {0};
 BinaryHistoryRecord _pendingHistRec; /**< HIST record deferred during touch (legacy) */
 volatile bool _pendingHistValid = false; /**< True if _pendingHistRec has data */

 /* ── V4 RAM batch + codec state (T2.1) ──────────────────────── */
 /* Samples accumulate in RAM and hit flash in ONE Core-1 pause every
  * HIST_BATCH_N samples or HIST_BATCH_MAX_MS, whichever first —
  * ~N× fewer program/erase IRQ-off windows (plan R2). Power loss
  * costs at most the buffered samples (plan-accepted trade-off).
  * Generalizes the old 1-slot touch-priority pending buffer. */
 static constexpr uint8_t  HIST_BATCH_N = 4;
 static constexpr uint32_t HIST_BATCH_MAX_MS = 300000; /* 5 min */
 struct HistBatchEntry {
  int64_t values[HIST_V4_MAX_MEASUREMENTS];
  uint32_t epoch;
  uint8_t count;
 };
 HistBatchEntry _histBatch[HIST_BATCH_N];
 uint8_t  _histBatchLen = 0;
 uint32_t _histBatchFirstMs = 0;

 HistV4State _histV4State;
 bool _histV4CodecValid = false;

 bool writeHistoryEntryFlashV4(const int64_t *values, uint8_t measureCount, uint32_t epoch);
 /** Rebuild codec state from an existing file. If the file ends mid-record
  * (torn tail after power loss), returns true and reports the last good
  * byte offset in *tornAt so the caller can repair before appending. */
 static bool scanHistoryFileV4(File &f, HistV4State &state, size_t *tornAt = nullptr);

 /** Lockstep re-decode of source and replacement: every carried column must
  *  come back bit-identical, epochs must match, and the record count must be
  *  exactly what was written. Gate for the swap in migrateV4Schema. */
 bool verifyMigratedV4(const String &srcPath, const String &tmpPath,
                       const int8_t *colMap, uint32_t expectRecords);
 /** Create/recreate a day file with a fresh schema header; returns true
  * only if the header read back from flash parses with measureCount > 0. */
 bool createHistoryFileV4WithSchema(const String &path);
 /** Cut a torn tail: rewrite the file keeping only [0, goodPos) bytes. */
 bool repairHistoryTailV4(const String &path, size_t goodPos);


 /** Internal worker: writes ONE HIST record directly to flash (without checking touch
 * nor pending flush). Called by writeHistoryEntry on the non-deferred path. */
 bool writeHistoryEntryFlash(const BinaryHistoryRecord& rec);


 String _cachedOldestFile = "";
 bool _storageDirty = true;
 bool _didMigrate = false; /**< Set by attemptLoad when old schema detected */
 uint16_t _migrationFromVersion = 0; /**< Version of original blob before migration */

 File _currentLogFile;
 String _currentLogFileName = "";
 String _v4CurrentLogFileName = ""; /**< V4: current .sim4 file path (separate from legacy). */

 /** Codec state v2 of the active file. Valid only for the file
 * whose path == _currentLogFileName. Reconstructed by scan on file
 * change (boot or day rollover). */
 HistoryCodecState _histCodec;
 bool _histCodecValid = false;

 bool mountFS( );
 void loadDefaults( );
 void enforceStorageLimit( );
 /** T1.4: set when enforceStorageLimit( ) hits its per-call deletion cap
  * with usage still above the limit; drained by update( ) in slices. */
 bool _cleanupPending = false;

 /** Returns pointer to the NetworkTimeData overlay in
 * `reserved[28..47]`. If magic absent, initializes with backward-compatible
 * defaults (DNS auto + NTP ON) before returning. */
 NetworkTimeData* ensureNetworkTimeOverlay( );

 /* Granular flash safe mode chunking.
 * Implemented as a file-scope macro in StorageManager.cpp (not a
 * template in the header) to avoid pulling `#include "LogManager.h"`
 * into all StorageManager.h clients, bloating multiple
 * translation units and overflowing flash. Identical behavior:
 * each LittleFS op has its own enterFlashSafeMode/exitFlashSafeMode;
 * between chunks Core 1 renders. */

 static uint32_t calculateCRC32(const uint8_t *data, size_t length);
 static bool loadCurrentBlob(File& f, SystemConfig& outCfg);
 static bool loadAndMigrateV12(File& f, SystemConfig& outCfg);
	static bool loadAndMigrateV15(File& f, SystemConfig& outCfg);
	static bool loadAndMigrateV16(File& f, SystemConfig& outCfg);
 /** Migrate v13 (plaintext) or v14 (obfuscated) to v15
 * (UserAccount expanded with salt+hashVersion). srcVersion outputs 13 or 14. */
 static bool loadAndMigrateV14(File& f, SystemConfig& outCfg, uint16_t& srcVersion);
 bool attemptLoad(const char* path, SystemConfig& outCfg);

 /** Obfuscate/deobfuscate the 3 sensitive config fields with keystream
 * derived from SHA-256(chip_id + domain). XOR is symmetric — the same
 * call encrypts (save) or decrypts (load). */
 static void obfuscateSensitiveFields(SystemConfig& cfg);
};
