/**
 * @file    StorageManager.cpp
 * @brief   Implementation of StorageManager — config I/O, history logging, and flash locks.
 * @details Implements atomic config save (tmp→rename), storage limit enforcement
 *          with budget-limited cleanup, provisional timestamp correction across
 *          history files, HMAC-SHA256 password hashing with board serial pepper,
 *          and calibration CSV parsing.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "StorageManager.h"
#include <time.h>
#include "LogManager.h"
#include "pico/unique_id.h"
#include <hardware/watchdog.h>
#include <stdio.h>
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_hmac.h>

const uint32_t CONFIG_MAGIC = 0xCAFEBABE;
const uint16_t CONFIG_VERSION = 12;

StorageManager::StorageManager() {
    mutex_init(&_fsReadMutex);
    loadDefaults();
}


/* =========================================================================== */
/*              FLASH READ LOCK (LIGHTWEIGHT — NO CORE 1 PAUSE)              */
/* =========================================================================== */
/**
 * @brief Acquire lightweight read lock for LittleFS operations.
 * Uses a mutex to serialize filesystem reads without pausing Core 1.
 * The RP2040 flash is accessed via QSPI (not SPI0/SPI1), so there
 * is no bus conflict between flash reads and display SPI traffic.
 */
void StorageManager::enterFlashReadLock() {
    mutex_enter_blocking(&_fsReadMutex);
}

void StorageManager::exitFlashReadLock() {
    mutex_exit(&_fsReadMutex);
}


/* =========================================================================== */
/*                  FLASH SAFE MODE (HEAVY — PAUSES CORE 1)                  */
/* =========================================================================== */
/**
 * @brief Acquire exclusive flash access for write/delete operations.
 * Pauses Core 1 via multicore_lockout to protect XIP during erase/program.
 * NEVER use for read-only operations — use enterFlashReadLock() instead.
 */
void StorageManager::enterFlashSafeMode() {
    if (_lockCb) _lockCb(true);
}


void StorageManager::exitFlashSafeMode() {
    if (_lockCb) _lockCb(false);
}


bool StorageManager::lockHeavyTask() { bool expected = false; return __atomic_compare_exchange_n(&_heavyTaskLocked, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED); }
void StorageManager::unlockHeavyTask() { __atomic_store_n(&_heavyTaskLocked, false, __ATOMIC_RELEASE); }


bool StorageManager::isHeavyTaskLocked() const { return __atomic_load_n(&_heavyTaskLocked, __ATOMIC_ACQUIRE); }

bool StorageManager::begin() {
    if (!mountFS()) return false;
    enterFlashSafeMode();
    if (!LittleFS.exists(DIR_CONFIG)) LittleFS.mkdir(DIR_CONFIG);
    if (!LittleFS.exists(DIR_HISTORY)) LittleFS.mkdir(DIR_HISTORY);
    exitFlashSafeMode();
    if (!loadConfiguration()) saveConfiguration();
    return true;
}

bool StorageManager::mountFS() {
    if (LittleFS.begin()) { _isMounted = true; return true; }
    LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_FORMAT, 0, "Formatting Flash FS...");
    enterFlashSafeMode();
    bool formatted = LittleFS.format();
    if (formatted) { bool mounted = LittleFS.begin(); exitFlashSafeMode(); _isMounted = mounted; return mounted; }
    exitFlashSafeMode();
    return false;
}

void StorageManager::update() {}

void StorageManager::loadDefaults() {
    memset(&_currentConfig, 0, sizeof(SystemConfig));

    _currentConfig.magic = CONFIG_MAGIC;
    _currentConfig.version = CONFIG_VERSION;
    strncpy(_currentConfig.deviceName, "simut", 31);

    strncpy(_currentConfig.wifiSsid, "", 31);
    strncpy(_currentConfig.wifiPass, "", 31);
    _currentConfig.useDhcp = true;
    strncpy(_currentConfig.staticIp, "192.168.1.100", 15);
    strncpy(_currentConfig.staticMask, "255.255.255.0", 15);
    strncpy(_currentConfig.staticGateway, "192.168.1.1", 15);
    strncpy(_currentConfig.staticDns, "8.8.8.8", 15);
    _currentConfig.useHttps = false;

    for(int i = 0; i < MAX_USERS; i++) _currentConfig.users[i].active = false;

    _currentConfig.users[0].active = true;
    strncpy(_currentConfig.users[0].username, "admin", 15);
    String defaultAdminHash = hashPassword("admin", "8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918");
    strncpy(_currentConfig.users[0].password, defaultAdminHash.c_str(), 31);
    _currentConfig.users[0].permissions = PERM_FULL_ADMIN;
    _currentConfig.users[0].mustChangePassword = true;

    _currentConfig.users[1].active = true;
    strncpy(_currentConfig.users[1].username, "viewer", 15);
    String defaultViewerHash = hashPassword("viewer", "0b58331da2913b41e21b7b04938632e1858a729e28cf6914b4334380f339b6f1");
    strncpy(_currentConfig.users[1].password, defaultViewerHash.c_str(), 31);
    _currentConfig.users[1].permissions = (PERM_DASHBOARD | PERM_HISTORY);
    _currentConfig.users[1].mustChangePassword = false;

    strncpy(_currentConfig.telServer, "", 63);
    _currentConfig.telPort = 80;
    strncpy(_currentConfig.telPath, "/api.php", 31);
    strncpy(_currentConfig.telApiKey, "", 63);
    _currentConfig.telInterval = 0;
    _currentConfig.telBatchSize = 10;
    _currentConfig.telEncryption = false;
    _currentConfig.telMode = TEL_MODE_JSON;

    strncpy(_currentConfig.telGlobalTemplate, "{\"dev\":\"{DEV}\",\"mac\":\"{MAC}\",\"data\":[{DATA}]}", 255);
    strncpy(_currentConfig.telLineTemplate, "{\"ts\":{TS},\"tAmb\":{tAMB},\"hAmb\":{uAMB}}", 511);
    strncpy(_currentConfig.telLineSeparator, ",", 7);

    _currentConfig.telTransport = TEL_TRANSPORT_HTTP;
    strncpy(_currentConfig.mqttTopic, "simut/data", 63);
    strncpy(_currentConfig.mqttUser, "", 31);
    strncpy(_currentConfig.mqttPass, "", 31);
    _currentConfig.mqttQos = 0;
    _currentConfig.mqttRetain = false;
    strncpy(_currentConfig.mqttClientId, "", 23);
    _currentConfig.mqttKeepAlive = 60;

    _currentConfig.timezoneOffset = -3;
    _currentConfig.sampleIntervalMs = 2000;
    _currentConfig.loggingEnabled = true;
    _currentConfig.ds18Resolution = 12;
    _currentConfig.themeIndex = 0;

    strncpy(_currentConfig.displayPin, "1234", 7);
    _currentConfig.displayPin[7] = '\0';
    _currentConfig.displayLang = LANG_PT;

    for (int i = 0; i < MAX_SENSORS; i++) {
        _currentConfig.sensors[i].active = false;
        _currentConfig.sensors[i].gpio = 0;
        memset(_currentConfig.sensors[i].rom, 0, 8);
        strncpy(_currentConfig.sensors[i].hwId, "", 15);
        strncpy(_currentConfig.sensors[i].friendlyName, "Empty Slot", 31);
        _currentConfig.sensors[i].tempMin = 0.0f;
        _currentConfig.sensors[i].tempMax = 40.0f;
        _currentConfig.sensors[i].humMin = 20.0f;
        _currentConfig.sensors[i].humMax = 80.0f;
        _currentConfig.sensors[i].alarmsActive = true;
    }

    _currentConfig.ambientSensor.active = true;
    _currentConfig.ambientSensor.gpio = 10;
    memset(_currentConfig.ambientSensor.rom, 0, 8);
    strncpy(_currentConfig.ambientSensor.hwId, "AMB", 15);
    strncpy(_currentConfig.ambientSensor.friendlyName, "Ambiente Central", 31);
    _currentConfig.ambientSensor.tempMin = 15.0f;
    _currentConfig.ambientSensor.tempMax = 35.0f;
    _currentConfig.ambientSensor.humMin = 30.0f;
    _currentConfig.ambientSensor.humMax = 70.0f;
    _currentConfig.ambientSensor.alarmsActive = true;
}

uint32_t StorageManager::calculateCRC32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return ~crc;
}

bool StorageManager::attemptLoad(const char* path, SystemConfig& outCfg) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    size_t bytesRead = f.read((uint8_t*)&outCfg, sizeof(SystemConfig));
    uint32_t readCrc = 0;
    size_t crcRead = f.read((uint8_t*)&readCrc, sizeof(readCrc));
    f.close();
    if (bytesRead != sizeof(SystemConfig)) return false;
    if (outCfg.magic != CONFIG_MAGIC || outCfg.version != CONFIG_VERSION) return false;
    if (crcRead == sizeof(readCrc)) {
        uint32_t calcCrc = calculateCRC32((uint8_t*)&outCfg, sizeof(SystemConfig));
        if (calcCrc != readCrc) return false;
    }
    return true;
}

bool StorageManager::loadConfiguration() {


    enterFlashReadLock();
    SystemConfig tempConfig;
    if (LittleFS.exists(FILE_CONFIG) && attemptLoad(FILE_CONFIG, tempConfig)) {
        _currentConfig = tempConfig;
        exitFlashReadLock();
        return true;
    }
    if (LittleFS.exists(FILE_BACKUP) && attemptLoad(FILE_BACKUP, tempConfig)) {
        _currentConfig = tempConfig;
        exitFlashReadLock();
        saveConfiguration();
        return true;
    }
    exitFlashReadLock();
    loadDefaults();
    return false;
}

/**
 * @brief Atomic configuration save: write to temp file, then rename.
 * Maintains a backup copy for recovery if the primary is corrupted.
 * CRC32 appended after the binary blob for integrity verification.
 */
bool StorageManager::saveConfiguration() {
    _currentConfig.magic = CONFIG_MAGIC;
    _currentConfig.version = CONFIG_VERSION;

    enterFlashSafeMode();
    File f = LittleFS.open(FILE_TMP, "w");
    if (!f) { exitFlashSafeMode(); return false; }

    uint32_t crc = calculateCRC32((uint8_t*)&_currentConfig, sizeof(SystemConfig));
    size_t bytesWritten = f.write((uint8_t*)&_currentConfig, sizeof(SystemConfig));
    size_t crcWritten = f.write((uint8_t*)&crc, sizeof(crc));
    f.close();

    if (bytesWritten == sizeof(SystemConfig) && crcWritten == sizeof(crc)) {
        if (LittleFS.exists(FILE_CONFIG)) {
            LittleFS.remove(FILE_BACKUP);
            LittleFS.rename(FILE_CONFIG, FILE_BACKUP);
        }
        LittleFS.rename(FILE_TMP, FILE_CONFIG);
        exitFlashSafeMode();
        return true;
    } else {
        LittleFS.remove(FILE_TMP);
        exitFlashSafeMode();
        return false;
    }
}

void StorageManager::resetToFactory() { loadDefaults(); saveConfiguration(); }
SystemConfig& StorageManager::getConfig() { return _currentConfig; }

SensorRecord* StorageManager::getSensorByGpio(uint8_t gpio) {
    if (gpio == 10) return &_currentConfig.ambientSensor;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (_currentConfig.sensors[i].active && _currentConfig.sensors[i].gpio == gpio) {
            return &_currentConfig.sensors[i];
        }
    }
    return nullptr;
}

bool StorageManager::canWriteHistory(size_t sizeToWrite) { return _isMounted; }

String StorageManager::getHistoryFileName() {
    time_t now = time(nullptr);
    int32_t offsetSecs = _currentConfig.timezoneOffset * 3600;
    if ((int32_t)now + offsetSecs < 0) now = 0; else now += offsetSecs;
    struct tm timeinfo; gmtime_r(&now, &timeinfo);
    char buff[40]; snprintf(buff, sizeof(buff), "%s/%04d%02d%02d.csv", DIR_HISTORY, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return String(buff);
}

bool StorageManager::writeHistoryEntry(String line) {
    if (!_isMounted) return false;
    String path = getHistoryFileName();

    enterFlashSafeMode();
    if (path != _currentLogFileName) { enforceStorageLimit(); _currentLogFileName = path; }
    File f = LittleFS.open(path, "a");
    if (f) { f.println(line); f.close(); exitFlashSafeMode(); _storageDirty = true; return true; }


    LOG_WRN("STO", "History write failed — attempting storage cleanup");
    _storageDirty = true;
    enforceStorageLimit();
    f = LittleFS.open(path, "a");
    if (f) { f.println(line); f.close(); exitFlashSafeMode(); return true; }

    exitFlashSafeMode();
    return false;
}

/**
 * @brief Delete oldest history files to keep flash usage below 86%.
 * Uses dirty-flag caching and a 4-second budget timer to avoid
 * blocking the main loop during extensive cleanup.
 */
void StorageManager::enforceStorageLimit() {
    FSInfo info; LittleFS.info(info);
    if (info.totalBytes == 0) return;


    if (!_storageDirty && ((info.usedBytes * 100) / info.totalBytes) <= 86) return;

    int maxIter = 30;
    uint32_t _budgetStart = millis();
    while (maxIter-- > 0 && ((info.usedBytes * 100) / info.totalBytes) > 86) {
        watchdog_update(); TRACE_BEAT(0);
        if (millis() - _budgetStart > 4000) {
            LOG_WRN("STO", "enforceStorageLimit aborted — 4s budget exceeded.");
            break;
        }


        String oldestFile = _cachedOldestFile;
        if (oldestFile.length() == 0) {
            Dir dir = LittleFS.openDir(DIR_HISTORY);
            int dirCount = 0;
            while (dir.next()) {
                watchdog_update(); TRACE_BEAT(0);
                if (++dirCount % 20 == 0) delay(1);
                String fileName = dir.fileName();


                if (fileName.endsWith(".csv") && isValidHistoryFileName(fileName.c_str())) {
                    if (oldestFile == "" || fileName < oldestFile) oldestFile = fileName;
                }
            }
        }

        if (oldestFile != "") {
            String fullPath = String(DIR_HISTORY) + "/" + oldestFile;

            if (fullPath == _currentLogFileName) {
                LOG_WRN("STO", "enforceStorageLimit: skipping active log file.");
                break;
            }
            LittleFS.remove(fullPath);
            _cachedOldestFile = "";
            LittleFS.info(info);
        } else break;
    }


    if (((info.usedBytes * 100) / info.totalBytes) <= 86) _storageDirty = false;
}

uint32_t StorageManager::getLastSentTimestamp() {
    if (_cachedLastSent > 0) return _cachedLastSent;

    enterFlashReadLock();
    if (!LittleFS.exists(FILE_TCURSOR)) { exitFlashReadLock(); return 0; }
    File f = LittleFS.open(FILE_TCURSOR, "r");
    uint32_t ts = 0;
    if (f) { f.read((uint8_t*)&ts, sizeof(ts)); f.close(); }
    exitFlashReadLock();
    _cachedLastSent = ts;
    return ts;
}

void StorageManager::setLastSentTimestamp(uint32_t ts) {
    _cachedLastSent = ts;
    enterFlashSafeMode();
    File f = LittleFS.open(FILE_TCURSOR, "w");
    if (f) { f.write((uint8_t*)&ts, sizeof(ts)); f.close(); }
    exitFlashSafeMode();
}

String StorageManager::getStatsReport() {
    if (!_isMounted) return String("FS Not Mounted");

    enterFlashReadLock();
    FSInfo info; LittleFS.info(info);
    exitFlashReadLock();

    size_t available = (size_t)(info.totalBytes - info.usedBytes);
    String s = "=== Storage Stats ===\n";
    s += "Total: " + String((unsigned long)info.totalBytes) + " B\n";
    s += "Used:  " + String((unsigned long)info.usedBytes) + " B\n";
    s += "Free:  " + String((unsigned long)available) + " B";
    return s;
}

uint32_t StorageManager::getLastRecordedTimestamp() {

    enterFlashReadLock();
    Dir dir = LittleFS.openDir(DIR_HISTORY); String newestFile = "";
    while (dir.next()) {
        watchdog_update(); TRACE_BEAT(0);
        String fn = dir.fileName();
        if (fn.endsWith(".csv") && fn > newestFile) newestFile = fn;
    }
    uint32_t lastTs = 0;
    if (newestFile != "") {
        File f = LittleFS.open(String(DIR_HISTORY) + "/" + newestFile, "r");
        if (f) {
            size_t fSize = f.size(); const size_t TAIL_SIZE = 256; char buf[TAIL_SIZE + 1];
            if (fSize > TAIL_SIZE) f.seek(fSize - TAIL_SIZE);
            size_t n = f.read((uint8_t*)buf, TAIL_SIZE); buf[n] = '\0'; f.close();
            char* lastNl = strrchr(buf, '\n');
            if (lastNl && lastNl > buf) { *lastNl = '\0'; char* prevNl = strrchr(buf, '\n'); char* lastLine = prevNl ? (prevNl + 1) : buf; lastTs = strtoul(lastLine, nullptr, 10); }
            else if (n > 0) { lastTs = strtoul(buf, nullptr, 10); }
        }
    }
    exitFlashReadLock();
    return lastTs;
}

/* =========================================================================== */
/*                PROVISIONAL TIMESTAMP CORRECTION (NTP SYNC)                */
/* =========================================================================== */
/**
 * @brief Correct timestamps written during Virtual RTC operation.
 *
 * Two-phase atomic approach:
 *   Phase 1: Create .tmp files with corrected timestamps (safe to abort).
 *   Phase 2: Rename .tmp -> original (only if Phase 1 completed fully).
 * If the 6-second budget is exceeded, no original files are modified.
 */
void StorageManager::correctProvisionalTimestamps(uint32_t bootTs, int32_t delta) {
    if (delta == 0) return;
    _currentLogFileName = "";
    std::vector<String> files;

    enterFlashSafeMode();
    Dir dir = LittleFS.openDir(DIR_HISTORY);
    while (dir.next()) {
        watchdog_update(); TRACE_BEAT(0);
        if (dir.fileName().endsWith(".csv")) files.push_back(dir.fileName());
    }
    exitFlashSafeMode();

    uint32_t _budgetStart = millis();


    std::vector<String> modifiedFiles;
    bool budgetExceeded = false;

    for (const String& fn : files) {
        if (millis() - _budgetStart > 6000) {
            LOG_WRN("STO", "correctProvisionalTimestamps aborted — 6s budget exceeded in Phase 1.");
            budgetExceeded = true;
            break;
        }

        String path = String(DIR_HISTORY) + "/" + fn;
        String tmpPath = path + ".tmp";

        enterFlashSafeMode();
        File fIn = LittleFS.open(path, "r");
        File fOut = LittleFS.open(tmpPath, "w");
        if (!fIn || !fOut) {
            if (fIn) fIn.close();
            if (fOut) fOut.close();
            exitFlashSafeMode();
            continue;
        }

        bool modified = false;
        int lineCount = 0;
        char lineBuf[256];
        bool fileAborted = false;

        while (fIn.available()) {


            if (++lineCount % 25 == 0) {
                exitFlashSafeMode();
                watchdog_update();
                TRACE_BEAT(0);
                delay(2);
                if (millis() - _budgetStart > 6000) {
                    LOG_WRN("STO", "correctProvisional aborted mid-file — budget exceeded (Phase 1).");
                    enterFlashSafeMode();
                    fIn.close();
                    fOut.close();
                    LittleFS.remove(tmpPath);
                    exitFlashSafeMode();
                    fileAborted = true;
                    budgetExceeded = true;
                    break;
                }
                enterFlashSafeMode();
            }

            size_t len = fIn.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
            if (len == 0) continue;
            lineBuf[len] = '\0';
            if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';

            char* linePtr = lineBuf;
            while (*linePtr == ' ' || *linePtr == '\t') linePtr++;
            if (*linePtr == '\0') continue;

            char* endPtr; char* semicolon = strchr(linePtr, ';');
            if (semicolon) {
                uint32_t ts = strtoul(linePtr, &endPtr, 10);
                if (ts >= bootTs && ts < (bootTs + 86400 * 30)) {
                    ts += delta;
                    char newLine[256];
                    snprintf(newLine, sizeof(newLine), "%lu%s", (unsigned long)ts, semicolon);
                    fOut.println(newLine);
                    modified = true;
                } else {
                    fOut.println(linePtr);
                }
            } else {
                fOut.println(linePtr);
            }
        }

        if (!fileAborted) {
            fIn.close();
            fOut.close();

            if (modified) {
                modifiedFiles.push_back(fn);
            } else {
                LittleFS.remove(tmpPath);
            }
            exitFlashSafeMode();
        }

        if (budgetExceeded) break;
    }


    if (budgetExceeded) {
        enterFlashSafeMode();
        for (const String& fn : modifiedFiles) {
            String tmpPath = String(DIR_HISTORY) + "/" + fn + ".tmp";
            LittleFS.remove(tmpPath);
            watchdog_update();
        }
        exitFlashSafeMode();
        LOG_WRN("STO", "Phase 1 incomplete — no original files were modified.");
        return;
    }


    enterFlashSafeMode();
    for (const String& fn : modifiedFiles) {
        watchdog_update(); TRACE_BEAT(0);
        String path = String(DIR_HISTORY) + "/" + fn;
        String tmpPath = path + ".tmp";
        LittleFS.remove(path);
        LittleFS.rename(tmpPath, path);
    }
    exitFlashSafeMode();

    LOG_INF("STO", "Timestamp correction complete: " + String(modifiedFiles.size()) + " files corrected.");
}

String StorageManager::getBoardSerialNumber() {
    pico_unique_board_id_t board_id; pico_get_unique_board_id(&board_id);
    char hex[17]; snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X%02X%02X", board_id.id[0], board_id.id[1], board_id.id[2], board_id.id[3], board_id.id[4], board_id.id[5], board_id.id[6], board_id.id[7]);
    return String(hex);
}

long StorageManager::getCalibrationVersion(String path) {
    if (!LittleFS.exists(path)) return -1;

    enterFlashReadLock();
    File f = LittleFS.open(path, "r"); long ver = -1;
    if (f) {
        char lineBuf[64]; size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
        lineBuf[len] = '\0'; String line = String(lineBuf); line.trim();
        if (line.startsWith("VERSION,")) ver = line.substring(8).toInt();
        f.close();
    }
    exitFlashReadLock();
    return ver;
}

bool StorageManager::getCalibrationData(const uint8_t* rom, String& outId, float& outOffset, String& outName) {
    char romStr[17]; snprintf(romStr, sizeof(romStr), "%02X%02X%02X%02X%02X%02X%02X%02X", rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
    if (!LittleFS.exists("/calib.csv")) return false;


    enterFlashReadLock();
    File f = LittleFS.open("/calib.csv", "r"); bool found = false;
    if (f) {
        char lineBuf[256];
        while (f.available()) {
            watchdog_update(); TRACE_BEAT(0);
            size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
            if (len == 0) continue;
            lineBuf[len] = '\0'; if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
            String line = String(lineBuf); line.trim();
            if (line.length() >= 16 && line.substring(0, 16).equalsIgnoreCase(romStr)) {
                int p1 = line.indexOf(','); int p2 = line.indexOf(',', p1 + 1); int p3 = line.indexOf(',', p2 + 1);
                if (p1 > 0 && p2 > p1) {
                    outId = line.substring(p1 + 1, p2);
                    if (p3 > p2) { outOffset = line.substring(p2 + 1, p3).toFloat(); outName = line.substring(p3 + 1); outName.replace("\"", ""); }
                    else { outOffset = line.substring(p2 + 1).toFloat(); outName = ""; }
                    found = true; break;
                }
            }
        }
        f.close();
    }
    exitFlashReadLock();
    return found;
}

bool StorageManager::processCalibrationUpload() {
    if (!LittleFS.exists("/calib.tmp")) return false;
    long currentVer = getCalibrationVersion("/calib.csv");
    long newVer = getCalibrationVersion("/calib.tmp");

    enterFlashSafeMode();
    if (newVer > currentVer) {
        LittleFS.remove("/calib.csv"); LittleFS.rename("/calib.tmp", "/calib.csv");
        exitFlashSafeMode(); return true;
    } else {
        LittleFS.remove("/calib.tmp"); exitFlashSafeMode(); return false;
    }
}

/**
 * @brief HMAC-SHA256 password hashing with board serial pepper and 2500 rounds.
 * Salt: lowercase username. Pepper: unique board serial number.
 * Output: 30 hex chars (120 bits of effective entropy).
 */
String StorageManager::hashPassword(const String& username, const String& plainPassword) {
    String pepper = getBoardSerialNumber();
    String salt = username; salt.toLowerCase();
    String keyData = plainPassword + pepper;
    br_hmac_key_context kc; br_hmac_context ctx;
    br_hmac_key_init(&kc, &br_sha256_vtable, keyData.c_str(), keyData.length());
    unsigned char currentHash[32];
    br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, salt.c_str(), salt.length()); br_hmac_out(&ctx, currentHash);
    for (int r = 0; r < 2500; r++) {
        if (r % 100 == 0) watchdog_update();
        br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, currentHash, 32); br_hmac_out(&ctx, currentHash);
    }


    char hashHex[32];
    for (int i = 0; i < 15; i++) snprintf(hashHex + (i * 2), 3, "%02x", currentHash[i]);
    hashHex[30] = '\0';
    return String(hashHex);
}
