/**
 * @file    StorageManager.h
 * @brief   LittleFS storage layer with dual-bank CRC32 configuration and flash safety.
 * @details Manages all persistent data: system configuration (binary with CRC32
 *          and backup), CSV history files, telemetry cursor, and calibration
 *          data. Provides two-tier flash locking: lightweight mutex for reads
 *          and multicore_lockout for writes (protects XIP during erase/program).
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

    SystemConfig& getConfig();
    SensorRecord* getSensorByGpio(uint8_t gpio);

    String getStatsReport();
    bool canWriteHistory(size_t sizeToWrite);

    bool writeHistoryEntry(String line);
    String getHistoryFileName();

    uint32_t getLastRecordedTimestamp();
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

private:
    SystemConfig _currentConfig;
    bool _isMounted = false;
    FlashLockCallback _lockCb = nullptr;
    mutex_t _fsReadMutex;

    bool _heavyTaskLocked = false;
    uint32_t _cachedLastSent = 0;


    String _cachedOldestFile = "";
    bool _storageDirty = true;

    File _currentLogFile;
    String _currentLogFileName = "";

    bool mountFS();
    void loadDefaults();
    void enforceStorageLimit();

    uint32_t calculateCRC32(const uint8_t *data, size_t length);
    bool attemptLoad(const char* path, SystemConfig& outCfg);
};
