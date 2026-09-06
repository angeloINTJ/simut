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
#include "HistoryV5.h"
#include "sensors/SensorHelpers.h"
#include "sensors/CalibCurve.h"

#define DIR_CONFIG "/config"
#define FILE_CONFIG "/config/system.bin"
#define FILE_BACKUP "/config/system.bak"
#define FILE_TMP "/config/system.tmp"
#define FILE_TCURSOR "/config/t_cursor.bin"
#define DIR_HISTORY "/history"
/** V5 crash-recovery snapshot: exactly one sealed PARTIAL DATA chunk
 *  holding the block still open in RAM (§7.2). */
#define FILE_H5_WIP DIR_HISTORY "/.wip"
#define DIR_LANG "/lang"
#define DIR_THEMES "/themes"
#define DIR_WEB "/web"

/** Filesystem manual written at the root by the firmware. */
#define FILE_FS_README "/README.txt"
/** Per-folder note. Doubles as the entry that keeps an empty folder listed. */
#define FS_DIR_NOTE_NAME "README.txt"

/** True for paths the web file manager must refuse to delete.
 *
 * Enforced in handleDelete, NOT only in the page: /files hides the checkbox
 * on a protected row, but a hand-made POST to /api/delete reaches the same
 * handler and has to be turned away there.
 *
 * Covers the root manual and the per-folder notes. The notes are protected
 * because they are load-bearing, not decorative: LittleFS drops a directory
 * with no entries from the parent listing, so deleting the note makes the
 * folder itself vanish from /files — and an invisible folder cannot be
 * uploaded into.
 *
 * Both the listing (which flags the row) and the delete path read this one
 * function, so they cannot drift. */
inline bool isProtectedFsPath(const String& path) {
 if (path.equalsIgnoreCase(FILE_FS_README)) return true;
 return path.equalsIgnoreCase(String(DIR_THEMES) + "/" + FS_DIR_NOTE_NAME)
     || path.equalsIgnoreCase(String(DIR_WEB)    + "/" + FS_DIR_NOTE_NAME)
     || path.equalsIgnoreCase(String(DIR_LANG)   + "/" + FS_DIR_NOTE_NAME);
}

/* isSecretFsPath (the /config download guard, finding A-4) lives in its own
 * pure header so `pio test -e native` can exercise it — this file pulls
 * LittleFS and pico/mutex and will not compile on the host. DIR_CONFIG is
 * already defined above, so the include below reuses it. */
#include "FsSecretPath.h"

typedef void (*FlashLockCallback)(bool);

/** Callback for cooperative "quiet mode" in large saves.
 * enable=true: Core 0 asks Core 1 to freeze in a RAM-only loop (IRQs off).
 * Returns true if Core 1 ACKed, false if Core 1 did not respond.
 * enable=false: releases Core 1 from quiet mode. Return value ignored. */
typedef bool (*BigSaveQuietCallback)(bool);
/** @return true when real time is in force, false under the provisional clock. */
typedef bool (*ClockTrustedCallback)( );

class StorageManager {
public:
 StorageManager( );
 bool begin( );
 void update( );

 void setLockCallback(FlashLockCallback cb) { _lockCb = cb; }
 /**
  * @brief Asks whether the clock stamping records is real time or the seed.
  *
  * Answered by NetworkManager, reached through a callback so storage keeps
  * knowing nothing about the network. The .wip writer records the answer in
  * the snapshot's flags: at the next boot, whether a t0 may be believed
  * depends on where it came from, and this is the only moment that knows.
  * Unset reads as "not trusted", which is the safe default — a build that
  * forgets to wire it gets the old, stricter gate rather than a free pass.
  */
 void setClockTrustedCallback(ClockTrustedCallback cb) { _clockTrustedCb = cb; }
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

 /** Drain the V4 history batch, but only if it already came due while a
  * touch held the flush back. Called by AppManager on the
  * touch-active→touch-free transition. No-op otherwise. */
 bool flushHistoryBatchIfDue( );

 SystemConfig& getConfig( );
 SensorRecord* getSensorByGpio(uint8_t gpio);

 String getStatsReport( );
 bool canWriteHistory(size_t sizeToWrite);

 /** Write /README.txt if it is missing or stale. Called from begin( )
  *  after the directories exist. No-op on the common boot. */
 void ensureFsReadme( );

 /* ── V4 history API — the only history format ────────────────
  * writeHistoryEntry(BinaryHistoryRecord) and the .bin filename builders
  * lived here until v2/v3 were removed. Nothing had called the writer for
  * releases; it kept a whole delta codec alive to serve no one. */

   /** T2.1: drain the RAM history batch to flash (one Core-1 pause for
   * the whole drain). Called on batch-full/age, before reboots and on
   * write memory. Safe to call with an empty batch. */
  bool flushHistoryBatch( );
  /** Invalidate the reader after an EXTERNAL mutation of a history file
   * (e.g. a web delete). V5 re-reads the SCHEMA on every open, so this only
   * has to drop the open reader. */
  void invalidateHistoryCodec( ) { h5CloseDay( ); }

  /** Rebind the history schema to the current sensor config (V5 semantics:
   *  a SCHEMA chunk is appended to the day's .h5 — nothing is lost).
   *  @param outMeasures receives the new measurement count (may be nullptr).
   *  @return false if the schema would be empty. */
  bool rebindSchema(uint8_t* outMeasures = nullptr);

  /** "Migrate" the schema (V5 semantics: same as rebindSchema — every record
   *  is carried because none moves). Keeps the outRecords/outCarried shape
   *  for the web rebind endpoint. */
  bool migrateSchema(uint8_t* outMeasures = nullptr, uint32_t* outRecords = nullptr,
                     uint8_t* outCarried = nullptr);

/* ── V5 history API — the format from 2.0.1-alpha on ───────────────────
  * Written in RAM by the minute, put on flash by the hour. See
  * docs/HistoryV5_Instrucoes_Implementacao.md and HistoryV5.h.
  *
  * The V4 entry points above are still here and still work: V4 files that
  * predate the update are gone (§11 purges them at first boot), but the
  * functions stay so nothing that called them stops compiling. */

 /** @return today's V5 file path (e.g. /history/20260731.h5). */
 String getHistoryFileNameV5( );
 /** @return the V5 path for the day @p epoch belongs to. */
 String getHistoryFileNameV5(uint32_t epoch);

 /**
  * @brief Build the V5 channel schema from the provisioned sensor slots.
  * @details One descriptor per (slot, channel) the slot's type reports, in
  *          slot then channel order. `id` is slot*4 + channel: stable while
  *          the sensor keeps its slot, and never recycled inside a schema.
  *          A sensor moved to another GPIO is a different slot, which is a
  *          reconfiguration and therefore a new SCHEMA chunk (§3.7-2).
  * @param out Receives up to @p cap descriptors.
  * @return channels written (0 when no slot is active).
  */
 uint8_t buildH5Schema(H5ChannelDesc* out, uint8_t cap) const;

 /** @return the schema the open block is being encoded against, or nullptr. */
 const H5ChannelDesc* getH5Schema( ) const { return _h5Valid ? _h5Schema : nullptr; }
 uint8_t getH5ChannelCount( ) const { return _h5Valid ? _h5NCh : 0; }
 bool    isH5Active( ) const { return _h5Valid; }

 /** Bind the encoder to the current sensor set. Idempotent. */
 void ensureH5Schema( );

 /**
  * @brief Record one sample. Hot path: RAM only, no flash, no heap.
  * @details Seals and appends only when the block fills, the day rolls
  *          over, or the sensor set changed under it.
  * @param values @p nCh values in schema order, H5_NAN_SENTINEL for missing.
  * @return false when the record was refused (schema mismatch, no schema).
  */
 bool writeHistoryEntryV5(const int16_t* values, uint8_t nCh, uint32_t epoch);

 /** Seal the open block and append it to its day file. No-op when empty. */
 bool sealHourV5(bool partial = true);

 /** Snapshot the open block to /history/.wip (§7.2). No-op when empty. */
 bool flushWipV5( );

 /**
  * @brief True when the open block holds records the .wip does not.
  * @details writeHistoryEntryV5 snapshots inline, so this is normally false.
  *          It latches when the inline attempt was refused — touch priority
  *          or a heavy task was holding the flash — and the loop sweeps it as
  *          soon as the gate opens. Without the latch a deferred record would
  *          wait for the next sample to carry it, which is the window R8 is
  *          supposed to close.
  */
 bool h5WipPending( ) const { return _h5WipDirty; }

 /** Boot recovery: adopt a valid .wip into its day file, discard a bad one. */
 void recoverWipV5( );

 /** §3.7-2: the sensor set changed — seal PARTIAL and start a new SCHEMA. */
 void onSensorSetChangedV5( );

 /**
  * @brief §7.3 retroactive clock correction over the V5 history.
  * @details Only DATA t0 moves; a block's interior is relative to it and
  *          SCHEMA carries no time. Stream-rewrites each affected file to
  *          .tmp and renames, the same shape as the config write.
  * @param deltaS seconds to add to every timestamp.
  * @param path   file to fix; empty means today's.
  * @param fromEpoch only blocks whose t0 is at or after this move. The NTP
  *        correction must not touch records an earlier boot wrote with a
  *        clock that was already right.
  * @return blocks rewritten, or -1 on failure.
  */
 int32_t shiftHistoryTimeV5(int32_t deltaS, const String& path = "",
                            uint32_t fromEpoch = 0);

 /** §11: delete everything in /history that is not a V5 file. */
 uint16_t purgeNonV5History( );
 /** @return files §11 purged on this boot, for the one-time UI notice. */
 uint16_t getPurgedLegacyCount( ) const { return _h5PurgedLegacy; }

 /* --- Sequential reader, shared by web, telemetry, graph and CSV --- */

 /** Open a day file and position after its first SCHEMA. */
 bool h5OpenDay(const String& path, bool verifyPayload = true);
 /** Next record of the open file, crossing block boundaries. */
 bool h5NextRecord(uint32_t& epoch, int16_t* v);
 /**
  * @brief Bring the next DATA block off flash into the reader's buffer.
  * @details The only half of h5NextRecord( ) that touches the filesystem,
  *          split out so a caller can hold the read lock once per BLOCK
  *          instead of once per record (§10). Callers MUST hold it here.
  * @return false at end of file.
  */
 bool h5LoadNextBlock( );
 /**
  * @brief Next record out of the block already in RAM.
  * @details Pure memory: no flash, no lock. Returns false when the block
  *          is exhausted — the caller then calls h5LoadNextBlock( ).
  */
 bool h5DecodeNext(uint32_t& epoch, int16_t* v);
 /** Next block header without decoding it — the envelope path (§10). */
 bool h5NextBlock(H5DataHeader& hdr, const int16_t*& mn, const int16_t*& mx);
 /** Position the reader on the block containing @p epoch. */
 bool h5SeekTo(uint32_t epoch);
 /** Schema in force at the reader's position. */
 const H5ChannelDesc* h5ReaderSchema( ) const { return _h5RdSchema; }
 uint8_t h5ReaderChannels( ) const { return _h5RdNCh; }

 /* ── Hour still open in RAM ──
  * A V5 block reaches the day file only when it seals, which at one record a
  * minute is once an hour. Everything that reads .h5 therefore trails the
  * present by up to that hour — telemetry included, which is why a fresh
  * device sent nothing for its first 60 minutes. These expose the open block
  * so a reader can carry on past the newest sealed record.
  *
  * Same core as the history writer, so no lock is needed; do not yield in the
  * middle of a walk, or the block can seal underneath it. */
 uint8_t h5RamCount( ) const { return _h5Valid ? _h5Enc.count( ) : 0; }
 bool h5RamRecord(uint8_t i, uint32_t& epoch, int16_t* vals) const {
 return _h5Valid && _h5Enc.sample(i, epoch, vals);
 }
 /**
  * @brief Serialize the open block as a standalone V5 stream (§3).
  * @details A SCHEMA chunk followed by the block sealed PARTIAL — byte for
  *          byte what a one-block .h5 file looks like. That is the whole
  *          point: a consumer that already decodes day files decodes this
  *          with no second format and no special case, which is how the CSV
  *          export reaches the open hour without the browser learning
  *          anything new. It is the same bytes flushWipV5( ) writes to the
  *          snapshot, plus the schema a standalone stream has to carry.
  *
  *          Reads the encoder, never disturbs it: sealStream( ) only emits,
  *          which is why the .wip can be rewritten once per record without
  *          costing the block. No flash and no lock — same core as the
  *          writer, so do not yield mid-stream.
  * @return bytes written, or 0 when nothing is open or the seal failed.
  */
 size_t h5StreamOpenBlock(H5WriteFn sink, void* ctx);
 uint16_t h5ReaderRejected( ) const { return _h5Scan.rejected( ); }
 void h5CloseDay( );

 uint32_t getLastRecordedTimestamp( );
 uint32_t getHistoryDaysMask(int year, int month);

 uint32_t getLastSentTimestamp( );
 void setLastSentTimestamp(uint32_t ts);
 void resetTelemetryCursor( ); /**< CMD_TEL_RESET: invalidates RAM cache + deletes flash file. */

 static String getBoardSerialNumber( );
 /* Both readers answer a CalibCurve. A 4-column row (the only shape older
  * firmware ever wrote) decodes as the constant offset it always was; the
  * cells after the name carry up to CALIB_MAX_POINTS raw,ref pairs — one
  * number per CSV column, so a spreadsheet reads the file directly. */
 bool getCalibrationData(const uint8_t* rom, String& outId, CalibCurve& outCurve, String& outName);
 /* Lookup for sensors with no 1-Wire ROM (DHT22, BMP280) in calib.csv.
  * Key column = picoUID 16 hex; ID column = `prefix` + the sensor's hwId,
  * `t` for temperature and `u` for humidity (ex: `tDHT2202`, `uDHT2202`).
  *
  * Matches the FULL id. It used to take no hwId and return the first row
  * with the right prefix, which made the pair device-wide: a board with two
  * DHT22s could only ever hold one calibration. */
 bool getCalibrationByHwId(char prefix, const char* hwId, CalibCurve& outCurve, String& outName);
 /* Pair a DS18B20 to its silicon: writes/replaces the calib.csv row keyed by
  * this ROM and drops the board-serial temperature row that carried the
  * sensor while it was unpaired — its curve arrives via `curve` and moves
  * into the ROM row, so pairing never loses a calibration. Atomic through
  * /calib.tmp + the version gate. */
 bool bindDs18Identity(const uint8_t* rom, const char* hwId, const char* name, const CalibCurve& curve);
 long getCalibrationVersion(String path);
 bool processCalibrationUpload( );
 bool recoverCalibrationTmp( );

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
 /** Write the telemetry cursor to flash if it moved.
  *
  * Normally the write is coalesced (CURSOR_COALESCE_MS) and deferred while the
  * user is touching the screen, because a device that sends every few seconds
  * would otherwise write the same file constantly.
  *
  * @param force  Skip both gates. For the path into deep sleep, where there is
  *               no "later": SRAM is lost and the next boot re-reads the file,
  *               so a deferred write is a lost one. */
 void flushCursorIfDirty(bool force = false);
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

 /** Size of a config file that was refused for having the wrong schema, or 0.
  *  Clears on read: the caller logs it once, after LogManager is up. Config
  *  loading happens before the logger exists, which is why this is not simply
  *  reported where it is detected. */
 size_t takeRejectedConfigSize( ) {
  size_t s = _rejectedConfigSize;
  _rejectedConfigSize = 0;
  return s;
 }

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

 /** @return true if Home Assistant MQTT Discovery is enabled (overlay; legacy = OFF). */
 /** Web server HTTP keep-alive (SetupFlagsData overlay). Default ON: a
  * legacy overlay (no magic) or a clear FLAG_WEB_KEEPALIVE_OFF bit both
  * mean enabled — the bit stores the opt-out. */
 bool isWebKeepAliveEnabled( ) const;
 void setWebKeepAliveEnabled(bool enabled);

 bool isHaDiscoveryEnabled( ) const;

 /** Set the HA Discovery flag in overlay. Populates magic if not yet present. */
 void setHaDiscoveryEnabled(bool enabled);

 /** @return true if retained discovery configs are known to sit on the broker. */
 bool wasHaDiscoveryPublished( ) const;

 /** Record (and persist — must survive the commit_all reboot) whether the
 * retained discovery configs are on the broker. No-op if unchanged. */
 void markHaDiscoveryPublished(bool published);

 /** @return true if syslog forwarding is enabled AND a server IP is set
  * (overlay reserved[56..63]; legacy = OFF). This is the EFFECTIVE state the
  * runtime uses: a forwarder with no destination is off. */
 bool isSyslogEnabled( ) const;
 /** @return the raw enable bit, ignoring whether a server is set. The config
  * write path reads intent with this so "enable now, set server next commit"
  * does not silently clear the toggle. */
 bool getSyslogEnabledFlag( ) const;
 /** @return syslog collector IPv4 as an IPAddress uint32 (0 = unset). */
 uint32_t getSyslogServerIp( ) const;
 /** @return syslog UDP port (SYSLOG_DEFAULT_PORT if unset). */
 uint16_t getSyslogPort( ) const;
 /** @return minimum LogLevel to forward (0..4; default LOG_INFO=1). */
 uint8_t getSyslogMinLevel( ) const;
 /** Write the whole syslog overlay (populates magic). serverIp 0 disables. */
 void setSyslogConfig(bool enabled, uint32_t serverIp, uint16_t port, uint8_t minLevel);

 SystemConfig _currentConfig;
 bool _isMounted = false;
 FlashLockCallback _lockCb = nullptr;
 BigSaveQuietCallback _bigSaveQuietCb = nullptr;
 ClockTrustedCallback _clockTrustedCb = nullptr;
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

 /* T2.1's RAM batch (4 x HistV4State-shaped entries) and the ~2 KB of
  * HistV4State that went with it are gone. They existed to amortise V4's
  * one-flash-write-per-sample; the V5 hot path writes no flash at all, so
  * the batch had nothing left to amortise. */

 /* ── V5 state ──────────────────────────────────────────────────────────
  * The encoder holds the hour in RAM (~2.1 KiB of samples); the reader
  * holds one block (2118 B) so every consumer can share one buffer instead
  * of each carrying its own HistV4State the way the V4 readers did. */
 HistoryV5Encoder _h5Enc;
 H5ChannelDesc    _h5Schema[H5_MAX_CHANNELS];
 uint8_t          _h5NCh = 0;
 uint8_t          _h5SchemaSeq = 0;
 bool             _h5Valid = false;
 /** Records held in RAM that the .wip on flash does not carry yet. */
 bool             _h5WipDirty = false;
 /** Consecutive records refused because a seal keeps failing (§H5_SEAL_MAX_FAILS). */
 uint8_t          _h5SealFails = 0;
 /** Day file the open block belongs to; a change of day forces a seal. */
 String           _h5CurrentDay = "";
 uint16_t         _h5PurgedLegacy = 0;
 /**
  * t0 of the block recoverWipV5( ) adopted on this boot, 0 if none.
  *
  * That block is the only data from an EARLIER session that reaches a day
  * file during this boot, and its timestamps came from that session's clock —
  * they are already right. The NTP shift bounds itself with the provisional
  * base, which is sound only while the seed that produced it is; when the
  * seed came out low the bound sank below the adopted block and the
  * correction rewrote it. Naming the block outright does not depend on the
  * clock being trustworthy.
  */
 uint32_t         _h5AdoptedT0 = 0;

 /** Snapshot now unless a gate is holding the flash; leaves the flag set. */
 void flushWipUnlessBlocked( );

 /** H5_FLAG_CLOCK_SYNCED, or 0 while the provisional clock is in force. */
 uint8_t h5ClockFlag( ) const;

 File             _h5RdFile;
 HistoryV5Scan    _h5Scan;
 HistoryV5Decoder _h5Dec;
 const H5ChannelDesc* _h5RdSchema = nullptr;
 uint8_t          _h5RdNCh = 0;
 bool             _h5RdBlockOpen = false;
 /** Verify the payload CRC when a block is read (§3.4), set by h5OpenDay. */
 bool             _h5RdVerify = true;
 uint8_t          _h5Chunk[H5_BLOCK_MAX_BYTES];

 /** Append @p len bytes of a sealed chunk to @p path, creating with SCHEMA. */
 bool h5AppendChunk(const String& path, uint8_t extraFlags);
 /** Write the SCHEMA chunk that must open every file (§3). */
 bool h5WriteSchemaTo(File& f, uint8_t seq);
 /**
  * @brief @return true when @p path opens with a valid SCHEMA chunk.
  * @param outMatches set when the schema in FORCE — the last one in the
  *        file, not the opening one — equals the schema being written.
  * @param outLastSeq schemaSeq of that last SCHEMA, so an appended one
  *        numbers on from it instead of from a member a reboot reset.
  */
 bool h5FileHasSchema(const String& path, bool* outMatches,
                      uint8_t* outLastSeq = nullptr);

 

 /** Internal worker: writes ONE HIST record directly to flash (without checking touch
 * nor pending flush). Called by writeHistoryEntry on the non-deferred path. */
 bool writeHistoryEntryFlash(const BinaryHistoryRecord& rec);


 String _cachedOldestFile = "";
 bool _storageDirty = true;
 /** Size of a config file attemptLoad refused, or 0. 2.0.0 accepts one schema
  *  and migrates nothing, so a refusal has to be reportable — otherwise the
  *  user sees settings vanish with no stated reason. Read via
  *  takeRejectedConfigSize( ) once the logger is up. */
 size_t _rejectedConfigSize = 0;
 /** v21: true when the config in RAM came from a v20 blob (migrated).
  * loadConfiguration( ) saves it back once to persist the new schema. */
 bool _migratedFromV20 = false;

 File _currentLogFile;
 String _currentLogFileName = "";

 /** Codec state v2 of the active file. Valid only for the file
 * whose path == _currentLogFileName. Reconstructed by scan on file
 * change (boot or day rollover). */

 bool mountFS( );
 void loadDefaults( );
 /** v21: preenche AlarmTelConfig com os defaults de fábrica (linha de
  * alarmes DESLIGADA, JSON, fila 32, templates §3.3 da proposta).
  * Usada por loadDefaults( ) e pela migração v20→v21. */
 static void applyAlarmTelDefaults(AlarmTelConfig& a);
 /** v21: lê um blob v20 (tamanho = offsetof(SystemConfig, alarmTel) + CRC)
  * direto para a cabeça do struct, deofusca os campos sensíveis e preenche a
  * cauda alarmTel com defaults. A cauda só pode ser anexada — travado por
  * static_assert em SystemDefs_Records.h. */
 bool loadMigrateV20Blob(File& f, SystemConfig& outCfg);
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
 bool attemptLoad(const char* path, SystemConfig& outCfg);

 /** Obfuscate/deobfuscate the 3 sensitive config fields with keystream
 * derived from SHA-256(chip_id + domain). XOR is symmetric — the same
 * call encrypts (save) or decrypts (load). */
 static void obfuscateSensitiveFields(SystemConfig& cfg);
};
