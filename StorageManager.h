/**
 * @file    StorageManager.h
 * @brief   LittleFS storage layer with dual-bank CRC32 configuration and flash safety.
 * @details Manages all persistent data: system configuration (binary with CRC32
 * and backup), CSV history files, telemetry cursor, and calibration
 * data. Provides two-tier flash locking: lightweight mutex for reads
 * and multicore_lockout for writes (protects XIP during erase/program).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include "pico/mutex.h"
#include "SystemDefs.h"

#define DIR_CONFIG      "/config"
#define FILE_CONFIG     "/config/system.bin"
#define FILE_BACKUP     "/config/system.bak"
#define FILE_TMP        "/config/system.tmp"
#define FILE_TCURSOR    "/config/t_cursor.bin"
#define DIR_HISTORY     "/history"

typedef void (*FlashLockCallback)(bool);

class StorageManager {
public:
    StorageManager();
    bool begin();
    void update();

    void setLockCallback(FlashLockCallback cb) { _lockCb = cb; }


    void enterFlashReadLock();
    void exitFlashReadLock();
    void enterFlashSafeMode();
    void exitFlashSafeMode();

    bool loadConfiguration();
    bool saveConfiguration();
    void resetToFactory();

    /** @return true se a última chamada a `saveConfiguration()` pulou a
     *  gravação por CRC idêntico ao último salvo. Callers usam pra evitar
     *  audit logs redundantes após rajadas de clicks "Save" sem mudança. */
    bool lastSaveWasNoOp() const { return _lastSaveWasNoOp; }

    /** @return true se já passou tempo suficiente desde o último save real
     *  para permitir outro. Rate-limit server-side contra rajadas de saves
     *  que sobrecarregam LittleFS GC. Handlers devem rejeitar com 429 se
     *  retornar false. Default: 1 save / 1s. */
    bool canSaveNow() const;

    /** Registra callback que retorna true quando usuário está interagindo
     *  com display (touch priority). Flash writes não-urgentes são deferidos
     *  (buffer em RAM) para manter display/touch responsivos. */
    void setTouchPriorityChecker(bool (*fn)()) { _isTouchPriorityFn = fn; }

    /** @return true se há record HIST pendente esperando flush.
     *  AppManager pode chamar após interação terminar para forçar flush. */
    bool hasPendingHist() const { return _pendingHistValid; }

    SystemConfig& getConfig();
    SensorRecord* getSensorByGpio(uint8_t gpio);

    String getStatsReport();
    bool canWriteHistory(size_t sizeToWrite);

    bool writeHistoryEntry(const BinaryHistoryRecord& rec);
    String getHistoryFileName();

    uint32_t getLastRecordedTimestamp();
    uint32_t getHistoryDaysMask(int year, int month);
    void correctProvisionalTimestamps(uint32_t bootTs, int32_t delta);

    uint32_t getLastSentTimestamp();
    void setLastSentTimestamp(uint32_t ts);

    String getBoardSerialNumber();
    bool getCalibrationData(const uint8_t* rom, String& outId, float& outOffset, String& outName);
    long getCalibrationVersion(String path);
    bool processCalibrationUpload();

    bool lockHeavyTask();
    void unlockHeavyTask();
    bool isHeavyTaskLocked() const;

    String hashPassword(const String& username, const String& plainPassword);
    String sha256Hex(const String& input);
    void   flushCursorIfDirty();
    void   invalidateOldestFileCache() { _cachedOldestFile = ""; }

private:
    SystemConfig _currentConfig;
    bool _isMounted = false;
    FlashLockCallback _lockCb = nullptr;
    mutex_t _fsReadMutex;

    bool _heavyTaskLocked = false;
    uint32_t _cachedLastSent = 0;
    bool     _cursorDirty = false;
    uint32_t _cursorCoalesceTime = 0;
    bool     _lastSaveWasNoOp = false;  /**< True se saveConfiguration pulou por CRC idêntico */
    volatile uint32_t _lastSaveMs = 0;  /**< millis() do último save real (0 = nunca) */

    bool (*_isTouchPriorityFn)() = nullptr;  /**< Callback: user interagindo? */
    BinaryHistoryRecord _pendingHistRec;     /**< Record HIST deferido durante touch */
    volatile bool _pendingHistValid = false; /**< True se _pendingHistRec tem dados */

    /** Worker interno: grava UM record HIST direto em flash (sem checar touch
     *  nem flush pending). Chamado por writeHistoryEntry no path não-deferido. */
    bool writeHistoryEntryFlash(const BinaryHistoryRecord& rec);


    String _cachedOldestFile = "";
    bool _storageDirty = true;
    String _correctWatermark = "";       /**< Último arquivo corrigido (retomada) */
    int32_t _correctLastDelta = 0;       /**< Delta da última correção (reset)    */
    bool _didMigrate = false;            /**< Set por attemptLoad quando detectou schema antigo */
    uint16_t _migrationFromVersion = 0;  /**< Versão do blob original antes da migração */

    File _currentLogFile;
    String _currentLogFileName = "";

    bool mountFS();
    void loadDefaults();
    void enforceStorageLimit();

    static uint32_t calculateCRC32(const uint8_t *data, size_t length);
    static bool loadCurrentBlob(File& f, SystemConfig& outCfg);
    static bool loadAndMigrateV12(File& f, SystemConfig& outCfg);
    bool attemptLoad(const char* path, SystemConfig& outCfg);

    /** #5: ofusca/desofusca os 3 campos sensíveis da config com keystream
     *  derivado de SHA-256(chip_id + domain). XOR é simétrico — a mesma
     *  chamada criptografa (save) ou descriptografa (load). */
    static void obfuscateSensitiveFields(SystemConfig& cfg);
};
