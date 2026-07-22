/**
 * @file StorageManager.cpp
 * @brief Implementation of StorageManager — config I/O, history logging, and flash locks.
 * @details Implements atomic config save (tmp→rename), storage limit enforcement
 * with budget-limited cleanup, provisional timestamp correction across
 * history files, HMAC-SHA256 password hashing with board serial pepper,
 * and calibration CSV parsing.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "StorageManager.h"
#include "ParseFloat.h"
#include "TouchPriority.h"
#include <hardware/uart.h>
#include "ota/metadata.h"
#include "ota/config_snapshot.h"
#include <time.h>
#include <algorithm>
#include "LogManager.h"
#include "MetricsManager.h"
#include "pico/unique_id.h"
#include <hardware/watchdog.h>
#include <stdio.h>
#include <new>
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_hmac.h>

const uint32_t CONFIG_MAGIC = 0xCAFEBABE;

/* Chunked flash operation wrapper.
 * Acquires the FS mutex with timeout + watchdog feed to prevent
 * main-loop stalls when a long-running read (e.g. web API history)
 * holds the mutex. After 5 seconds of waiting, gives up silently —
 * the write will be retried on the next history interval.
 * LittleFS internally handles multicore_lockout via flash_safe_execute. */
#define FLASH_OP(BLOCK) do { \
 uint32_t _fopStart = millis( ); \
 while (!mutex_enter_timeout_ms(&_fsReadMutex, 100)) { \
  watchdog_update( ); \
  if (timeSince(_fopStart, 5000)) break; \
 } \
 if (timeSince(_fopStart, 5000)) { \
  LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "fs_mutex_timeout"); \
 } else { \
  watchdog_update( ); \
  BLOCK; \
  watchdog_update( ); \
  mutex_exit(&_fsReadMutex); \
 } \
} while (0)

const uint16_t CONFIG_VERSION = 17;

/* -------------------------------------------------------------------------- */
/* Legacy UserAccount layout (v14 and earlier) — used ONLY by the */
/* loadAndMigrateV14 migrator. Not to be confused with UserAccount (v15, 62 B). */
/* -------------------------------------------------------------------------- */
struct __attribute__((packed)) UserAccount_v14 {
 bool active;
 char username[16];
 char password[32];
 uint16_t permissions;
 bool mustChangePassword;
};
static_assert(sizeof(UserAccount_v14) == 52, "UserAccount_v14 must be 52 bytes");

/* Legacy v15 SensorRecord (79 bytes) — used by v14 and v15 migrators. */
struct __attribute__((packed)) SensorRecord_v15 {
 bool active;
 uint8_t gpio;
 uint8_t rom[8];
 char hwId[16];
 char friendlyName[32];
 uint32_t provisionEpoch;
 float tempMin;
 float tempMax;
 float humMin;
 float humMax;
 bool alarmsActive;
};
static_assert(sizeof(SensorRecord_v15) == 79, "SensorRecord_v15 must be 79 bytes");

/* Keystream derivation via SHA-256(chip_id + domain + counter).
 * Generates arbitrary-size keystream iterating the counter and expanding
 * the hash. Equivalent to HKDF-Expand in construction and security. */
static void deriveKeystream(uint8_t* out, size_t outLen, const char* domain) {
 pico_unique_board_id_t board_id;
 pico_get_unique_board_id(&board_id);

 size_t generated = 0;
 uint32_t counter = 0;
 while (generated < outLen) {
 br_sha256_context ctx;
 br_sha256_init(&ctx);
 br_sha256_update(&ctx, board_id.id, sizeof(board_id.id));
 br_sha256_update(&ctx, ":", 1);
 br_sha256_update(&ctx, domain, strlen(domain));
 br_sha256_update(&ctx, ":", 1);
 br_sha256_update(&ctx, &counter, sizeof(counter));
 uint8_t hash[32];
 br_sha256_out(&ctx, hash);
 size_t take = (outLen - generated < 32) ? (outLen - generated) : 32;
 memcpy(out + generated, hash, take);
 generated += take;
 counter++;
 }
}

/* Applies XOR in-place to a buffer with keystream derived from the (chip_id, domain) pair.
 * Since XOR is involutive, the same function encrypts and decrypts. */
static void xorWithDerivedKey(uint8_t* buf, size_t len, const char* domain) {
 uint8_t keystream[64]; /* current max size is telApiKey[64] */
 if (len > sizeof(keystream)) return; /* protection against future overrun */
 deriveKeystream(keystream, len, domain);
 for (size_t i = 0; i < len; i++) buf[i] ^= keystream[i];
}

void StorageManager::obfuscateSensitiveFields(SystemConfig& cfg) {
 xorWithDerivedKey((uint8_t*)cfg.wifiPass, sizeof(cfg.wifiPass), "wifi");
 xorWithDerivedKey((uint8_t*)cfg.mqttPass, sizeof(cfg.mqttPass), "mqtt");
 xorWithDerivedKey((uint8_t*)cfg.telApiKey, sizeof(cfg.telApiKey), "telapi");
}

/* Blob sizes per historical version, derived from known record counts.
 * sizeof(SystemConfig) = CURRENT v17 size. Older sizes are computed
 * by subtracting the deltas introduced in each version.
 *
 * Record counts per version (sensor area):
 *   v17: 16 SensorRecords (sensors[16], no ambientSensor)
 *   v16: 11 SensorRecords (sensors[10] + ambientSensor)
 *   v15: 11 SensorRecord_v15 (79 B each)
 *
 * v15→v16: SensorRecord grew 4 B × 11 = +44 B
 * v16→v17: +5 SensorRecords (removed ambientSensor, added 6 slots) = +415 B
 */
static constexpr size_t V16_RECORD_COUNT = 11; /* 10 sensors + 1 ambientSensor */
static constexpr size_t V16_TO_V17_DELTA = (MAX_SENSORS - V16_RECORD_COUNT) * sizeof(SensorRecord); /* 5*83=415 */
static constexpr size_t CONFIG_V16_BLOB_SIZE = sizeof(SystemConfig) - V16_TO_V17_DELTA;
static constexpr size_t CONFIG_V15_BLOB_SIZE = CONFIG_V16_BLOB_SIZE - V16_RECORD_COUNT * 4; /* -44 */
static constexpr size_t CONFIG_V14_USER_DELTA =
 MAX_USERS * (sizeof(UserAccount) - sizeof(UserAccount_v14)); /* 50 bytes */
static constexpr size_t CONFIG_V14_BLOB_SIZE =
 CONFIG_V15_BLOB_SIZE - CONFIG_V14_USER_DELTA;
static constexpr size_t CONFIG_V12_BLOB_SIZE =
 CONFIG_V14_BLOB_SIZE - (64 - CONFIG_V12_RESERVED_SIZE);

StorageManager::StorageManager( ) {
 mutex_init(&_fsReadMutex);
 loadDefaults( );
}


/* =========================================================================== */
/* FLASH READ LOCK (LIGHTWEIGHT — NO CORE 1 PAUSE) */
/* =========================================================================== */
/**
 * @brief Acquire lightweight read lock for LittleFS operations.
 * Uses a mutex to serialize filesystem reads without pausing Core 1.
 * The RP2040 flash is accessed via QSPI (not SPI0/SPI1), so there
 * is no bus conflict between flash reads and display SPI traffic.
 */
void StorageManager::enterFlashReadLock( ) {
 mutex_enter_blocking(&_fsReadMutex);
}

void StorageManager::exitFlashReadLock( ) {
 mutex_exit(&_fsReadMutex);
}

/* timeout variant — caller decides whether to abort. */
bool StorageManager::enterFlashReadLockTimeout(uint32_t timeout_ms) {
 return mutex_enter_timeout_ms(&_fsReadMutex, timeout_ms);
}


/* =========================================================================== */
/* FLASH SAFE MODE (HEAVY — PAUSES CORE 1) */
/* =========================================================================== */
/**
 * @brief Acquire exclusive flash access for write/delete operations.
 * Pauses Core 1 via multicore_lockout to protect XIP during erase/program.
 * NEVER use for read-only operations — use enterFlashReadLock( ) instead.
 */
void StorageManager::enterFlashSafeMode( ) {
 /* If we're inside saveConfiguration with quiet mode
 * active, Core 1 is already frozen in a RAM-only loop — skip the
 * IRQ-based lockout to avoid stucks/cascades. */
 if (_inBigSave) return;
 if (_lockCb) _lockCb(true);
}


void StorageManager::exitFlashSafeMode( ) {
 if (_inBigSave) return;
 if (_lockCb) _lockCb(false);
}


bool StorageManager::lockHeavyTask( ) { bool expected = false; return __atomic_compare_exchange_n(&_heavyTaskLocked, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED); }
void StorageManager::unlockHeavyTask( ) { __atomic_store_n(&_heavyTaskLocked, false, __ATOMIC_RELEASE); }


bool StorageManager::isHeavyTaskLocked( ) const { return __atomic_load_n(&_heavyTaskLocked, __ATOMIC_ACQUIRE); }

bool StorageManager::begin( ) {
 /* REMOVED enterFlashSafeMode wrap of mkdirs.
 * LittleFS internally already uses flash_safe_execute → multicore_lockout. Wrap
 * external creates reentrant deadlock. UART markers showed boot
 * hanging consistently in '012' (post-enterFSM, before mkdir).
 * LFS protects its own writes — do not wrap. */
 uart_putc_raw(uart1, '0');
 if (!mountFS( )) return false;
 uart_putc_raw(uart1, '1');
 if (!LittleFS.exists(DIR_CONFIG)) LittleFS.mkdir(DIR_CONFIG);
 uart_putc_raw(uart1, '3');
 if (!LittleFS.exists(DIR_HISTORY)) LittleFS.mkdir(DIR_HISTORY);
 uart_putc_raw(uart1, '4');
 if (!LittleFS.exists(DIR_LANG)) LittleFS.mkdir(DIR_LANG);
 uart_putc_raw(uart1, '6');
 /* Removed boot-time README.md placeholders for sysDirs.
 * The writes inside enterFlashSafeMode caused a reentrant deadlock
 * (LittleFS.open+write+close internally already use
 * flash_safe_execute → multicore_lockout, and we were already in another
 * lockout via enterFlashSafeMode).
 *
 * Trade-off: /lang and /themes are invisible in /api/ls root
 * while empty. handleApiLs lists via dir.next( ) —
 * empty dirs disappear from listing. /config has system.bin and /history
 * has logs, so they're automatically visible. handleApiMkdir
 * still creates README.md in custom dirs — that path is
 * outside wrap and safe. */

 /* Config restore after OTA apply.
 *
 * If metadata.state == APPLYING (real apply, not test stub) and there is a
 * CRC-valid snapshot in the metadata partition, restore `system.bin`
 * BEFORE loadConfiguration — so the pre-apply config
 * is loaded normally instead of falling to loadDefaults.
 *
 * Restore failure is non-fatal: loadConfiguration will fail to
 * find the file, saveConfiguration writes defaults, device boots
 * in factory mode (user restores via .bkp downloaded by browser).
 *
 * Metadata partition cleanup continues in AppManager_Boot.cpp
 * (existing path) — kept idempotent here.
 *
 * Do NOT wrap in enterFlashSafeMode. LittleFS.write internally already
 * uses multicore_lockout via flash_safe_execute; wrapping in another
 * lockout creates reentrant deadlock (Core 0 holding lockout +
 * LittleFS trying to obtain again). LFS handles protection alone.
 */
 {
 uart_putc_raw(uart1, '7');
 ota::UpdateMetadata m;
 if (ota::ota_metadata_read(m) &&
 m.state == ota::STATE_APPLYING &&
 ota::ota_snapshot_present( )) {
 uart_putc_raw(uart1, '8');
 (void)ota::ota_snapshot_restore_to_lfs( );
 uart_putc_raw(uart1, '9');
 }
 uart_putc_raw(uart1, 'A');
 }

 uart_putc_raw(uart1, 'B');
 if (!loadConfiguration( )) {
 uart_putc_raw(uart1, 'C');
 saveConfiguration( );
 uart_putc_raw(uart1, 'D');
 }
 uart_putc_raw(uart1, 'E');
 return true;
}

bool StorageManager::mountFS( ) {
 if (LittleFS.begin( )) { _isMounted = true; return true; }
 LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_FORMAT, 0, TRL("Formatting Flash FS..."));
 enterFlashSafeMode( );
 bool formatted = LittleFS.format( );
 if (formatted) { bool mounted = LittleFS.begin( ); exitFlashSafeMode( ); _isMounted = mounted; return mounted; }
 exitFlashSafeMode( );
 return false;
}

void StorageManager::update( ) {}

void StorageManager::loadDefaults( ) {
 memset(&_currentConfig, 0, sizeof(SystemConfig));

 _currentConfig.magic = CONFIG_MAGIC;
 _currentConfig.version = CONFIG_VERSION;
 safeCopy(_currentConfig.deviceName, "simut", sizeof(_currentConfig.deviceName));

 safeCopy(_currentConfig.wifiSsid, "", sizeof(_currentConfig.wifiSsid));
 safeCopy(_currentConfig.wifiPass, "", sizeof(_currentConfig.wifiPass));
 _currentConfig.useDhcp = true;
 safeCopy(_currentConfig.staticIp, "192.168.1.100", sizeof(_currentConfig.staticIp));
 safeCopy(_currentConfig.staticMask, "255.255.255.0", sizeof(_currentConfig.staticMask));
 safeCopy(_currentConfig.staticGateway, "192.168.1.1", sizeof(_currentConfig.staticGateway));
 safeCopy(_currentConfig.staticDns, "8.8.8.8", sizeof(_currentConfig.staticDns));
 _currentConfig.useHttps = false;

 for(int i = 0; i < MAX_USERS; i++) _currentConfig.users[i].active = false;

 _currentConfig.users[0].active = true;
 safeCopy(_currentConfig.users[0].username, "admin", sizeof(_currentConfig.users[0].username));

 /* Generate random admin password instead of hardcoded "admin".
 * SHA-256 hash of "admin" (`8c6976e5...a918`) is public in rainbow tables,
 * exposing the window between factory boot and mandatory password change.
 * Plaintext goes to RAM only; Serial+display show it for those with
 * physical access; `mustChangePassword=true` forces change on 1st web login. */
 generateInitialAdminPassword(_initialAdminPassword, sizeof(_initialAdminPassword));

 /* Frontend JS sends SHA256(plaintext) as `pass`, so the persisted hash
 * must be `hashPassword(user, SHA256(plaintext))`.
 * Factory defaults use v1 format (random salt,
 * PASSWORD_HMAC_ROUNDS rounds, 32 hex chars / 128 bits). */
 String preHash = sha256Hex(String(_initialAdminPassword));
 generateSalt(_currentConfig.users[0].salt);
 String defaultAdminHash = hashPasswordV1("admin", preHash, _currentConfig.users[0].salt);
 safeCopy(_currentConfig.users[0].password, defaultAdminHash.c_str( ), sizeof(_currentConfig.users[0].password));
 _currentConfig.users[0].permissions = PERM_FULL_ADMIN;
 _currentConfig.users[0].mustChangePassword = true;
 _currentConfig.users[0].hashVersion = 1;

 _currentConfig.users[1].active = true;
 safeCopy(_currentConfig.users[1].username, "viewer", sizeof(_currentConfig.users[1].username));
 generateSalt(_currentConfig.users[1].salt);
 String defaultViewerHash = hashPasswordV1("viewer", "0b58331da2913b41e21b7b04938632e1858a729e28cf6914b4334380f339b6f1", _currentConfig.users[1].salt);
 safeCopy(_currentConfig.users[1].password, defaultViewerHash.c_str( ), sizeof(_currentConfig.users[1].password));
 _currentConfig.users[1].permissions = (PERM_DASHBOARD | PERM_HISTORY);
 _currentConfig.users[1].mustChangePassword = true;
 _currentConfig.users[1].hashVersion = 1;

 safeCopy(_currentConfig.telServer, "", sizeof(_currentConfig.telServer));
 _currentConfig.telPort = 80;
 safeCopy(_currentConfig.telPath, "/api.php", sizeof(_currentConfig.telPath));
 safeCopy(_currentConfig.telApiKey, "", sizeof(_currentConfig.telApiKey));
 _currentConfig.telInterval = 0;
 _currentConfig.telBatchSize = 10;
 _currentConfig.telEncryption = false;
 _currentConfig.telMode = TEL_MODE_JSON;

 safeCopy(_currentConfig.telGlobalTemplate, "{\"dev\":\"{DEV}\",\"mac\":\"{MAC}\",\"data\":[{DATA}]}", sizeof(_currentConfig.telGlobalTemplate));
 safeCopy(_currentConfig.telLineTemplate, "{\"ts\":{TS},\"tAmb\":{tAMB},\"hAmb\":{uAMB}}", sizeof(_currentConfig.telLineTemplate));
 safeCopy(_currentConfig.telLineSeparator, ",", sizeof(_currentConfig.telLineSeparator));

 _currentConfig.telTransport = TEL_TRANSPORT_HTTP;
 safeCopy(_currentConfig.mqttTopic, "simut/data", sizeof(_currentConfig.mqttTopic));
 safeCopy(_currentConfig.mqttUser, "", sizeof(_currentConfig.mqttUser));
 safeCopy(_currentConfig.mqttPass, "", sizeof(_currentConfig.mqttPass));
 _currentConfig.mqttQos = 0;
 _currentConfig.mqttRetain = false;
 safeCopy(_currentConfig.mqttClientId, "", sizeof(_currentConfig.mqttClientId));
 _currentConfig.mqttKeepAlive = 60;

 _currentConfig.timezoneOffset = -3;
 _currentConfig.sampleIntervalMs = 2000;
 _currentConfig.loggingEnabled = true;
 #if SIMUT_SENSOR_DS18B20
 _currentConfig.ds18Resolution = 12;
#endif
 _currentConfig.themeIndex = 0;

 safeCopy(_currentConfig.displayPin, "1234", sizeof(_currentConfig.displayPin));
 /* Force change of default PIN "1234" on first access to the
 * config menu. Overlay in reserved[26..27] — cleared when user
 * saves a PIN != "1234". Set here (loadDefaults = factory reset). */
 setMustChangePin( );
 _currentConfig.displayLang = LANG_PT;

 /* Initialize NetworkTimeData overlay via helper (magic
 * still 0 after initial memset → helper populates defaults). */
 (void)ensureNetworkTimeOverlay( );

 /* v1.4.1: 16 universal slots (GPIO0–GPIO15). Each slot starts inactive with
  * pin[i]=i as default GPIO. Slots 0 and 10 are pre-configured as DHT22,
  * slots 1-9 as DS18B20 (factory defaults matching the protoboard layout). */
 for (int i = 0; i < MAX_SENSORS; i++) {
 _currentConfig.sensors[i].active = false;
 _currentConfig.sensors[i].sensorType = TYPE_NONE;
 memset(_currentConfig.sensors[i].pins, 255, sizeof(_currentConfig.sensors[i].pins));
 _currentConfig.sensors[i].pins[0] = i; /* Default: pin = slot index */
 memset(_currentConfig.sensors[i].rom, 0, 8);
 safeCopy(_currentConfig.sensors[i].hwId, "", sizeof(_currentConfig.sensors[i].hwId));
 safeCopy(_currentConfig.sensors[i].friendlyName, "Empty Slot", sizeof(_currentConfig.sensors[i].friendlyName));
 _currentConfig.sensors[i].tempMin = 0.0f;
 _currentConfig.sensors[i].tempMax = 40.0f;
 _currentConfig.sensors[i].humMin = 20.0f;
 _currentConfig.sensors[i].humMax = 80.0f;
 _currentConfig.sensors[i].alarmsActive = true;
 }

 /* Slots 0 e 10: DHT22 (temperatura + umidade) */
 _currentConfig.sensors[0].active = true;
 _currentConfig.sensors[0].sensorType = TYPE_DHT22;
 _currentConfig.sensors[0].pins[0] = 0;
 safeCopy(_currentConfig.sensors[0].hwId, "DHT0", sizeof(_currentConfig.sensors[0].hwId));
 safeCopy(_currentConfig.sensors[0].friendlyName, "DHT22 Externo", sizeof(_currentConfig.sensors[0].friendlyName));
 _currentConfig.sensors[0].tempMin = 0.0f;
 _currentConfig.sensors[0].tempMax = 40.0f;
 _currentConfig.sensors[0].humMin = 20.0f;
 _currentConfig.sensors[0].humMax = 80.0f;

 _currentConfig.sensors[10].active = true;
 _currentConfig.sensors[10].sensorType = TYPE_DHT22;
 _currentConfig.sensors[10].pins[0] = 10;
 safeCopy(_currentConfig.sensors[10].hwId, "AMB", sizeof(_currentConfig.sensors[10].hwId));
 safeCopy(_currentConfig.sensors[10].friendlyName, "Ambiente Central", sizeof(_currentConfig.sensors[10].friendlyName));
 _currentConfig.sensors[10].tempMin = 15.0f;
 _currentConfig.sensors[10].tempMax = 35.0f;
 _currentConfig.sensors[10].humMin = 30.0f;
 _currentConfig.sensors[10].humMax = 70.0f;

 /* Slots 1-9: DS18B20 (temperatura) */
 for (int i = 1; i <= 9; i++) {
 _currentConfig.sensors[i].active = true;
 _currentConfig.sensors[i].sensorType = TYPE_DS18B20;
 _currentConfig.sensors[i].pins[0] = i;
 char hwId[8]; snprintf(hwId, sizeof(hwId), "DS%d", i);
 char name[32]; snprintf(name, sizeof(name), "DS18B20 #%d", i);
 safeCopy(_currentConfig.sensors[i].hwId, hwId, sizeof(_currentConfig.sensors[i].hwId));
 safeCopy(_currentConfig.sensors[i].friendlyName, name, sizeof(_currentConfig.sensors[i].friendlyName));
 _currentConfig.sensors[i].tempMin = -10.0f;
 _currentConfig.sensors[i].tempMax = 50.0f;
 _currentConfig.sensors[i].alarmsActive = true;
 }
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

/* Read config in current format. Accepts v13 (plaintext, pre-obfuscation) or v14 (fields
 * encrypted via XOR+KDF). Decrypts in-place when v14.
 * The caller (attemptLoad) is responsible for detecting v13 and marking _didMigrate
 * so it is re-saved as v14 encrypted. */
bool StorageManager::loadCurrentBlob(File& f, SystemConfig& outCfg) {
 size_t bytesRead = f.read((uint8_t*)&outCfg, sizeof(SystemConfig));
 uint32_t readCrc = 0;
 size_t crcRead = f.read((uint8_t*)&readCrc, sizeof(readCrc));
 if (bytesRead != sizeof(SystemConfig)) return false;
 if (outCfg.magic != CONFIG_MAGIC) return false;
 /* v15 is the only native format accepted here — v13/v14 fall to loadAndMigrateV14
 * (file size smaller due to UserAccount[52] instead of [62]). */
 if (outCfg.version != CONFIG_VERSION) return false;
 if (crcRead == sizeof(readCrc)) {
 uint32_t calcCrc = calculateCRC32((uint8_t*)&outCfg, sizeof(SystemConfig));
 if (calcCrc != readCrc) return false;
 }
 /* v16 always writes with sensitive fields obfuscated (XOR keystream). */
 obfuscateSensitiveFields(outCfg);
 return true;
}

/* Read config in legacy v12 format (reserved[24], UserAccount_v14[52]) and migrate
 * to v15 (reserved[64], UserAccount[62]). Sensitive fields in v12 are
 * plaintext — no deobfuscation needed; they will be encrypted on the first
 * saveConfiguration post-migration.
 *
 * In addition to expanding reserved[24]→[64] (delta 40 at end),
 * also expands UserAccount from 52→62 in the middle of the blob (delta 50) — requires
 * buffered read and reconstruction by section. */
bool StorageManager::loadAndMigrateV12(File& f, SystemConfig& outCfg) {
 constexpr size_t HEAD_SIZE = offsetof(SystemConfig, users);
 constexpr size_t V14_USER_BLOCK = MAX_USERS * sizeof(UserAccount_v14);
 constexpr size_t V15_USER_BLOCK = MAX_USERS * sizeof(UserAccount);
 constexpr size_t MIDDLE_SIZE = offsetof(SystemConfig, reserved) -
 (HEAD_SIZE + V15_USER_BLOCK);
 constexpr size_t V12_MIDDLE_START = HEAD_SIZE + V14_USER_BLOCK;
 constexpr size_t V12_RESERVED_START = V12_MIDDLE_START + MIDDLE_SIZE;
 constexpr size_t V12_RESERVED_BYTES = CONFIG_V12_RESERVED_SIZE; /* 24 */

 uint8_t buf[CONFIG_V12_BLOB_SIZE];
 uint32_t readCrc = 0;

 size_t bytesRead = f.read(buf, CONFIG_V12_BLOB_SIZE);
 size_t crcRead = f.read((uint8_t*)&readCrc, sizeof(readCrc));

 if (bytesRead != CONFIG_V12_BLOB_SIZE) return false;

 uint32_t fileMagic = 0;
 uint16_t fileVersion = 0;
 memcpy(&fileMagic, buf + 0, sizeof(fileMagic));
 memcpy(&fileVersion, buf + sizeof(fileMagic), sizeof(fileVersion));
 if (fileMagic != CONFIG_MAGIC || fileVersion != 12) return false;

 if (crcRead == sizeof(readCrc)) {
 uint32_t calcCrc = calculateCRC32(buf, CONFIG_V12_BLOB_SIZE);
 if (calcCrc != readCrc) return false;
 }

 memset(&outCfg, 0, sizeof(SystemConfig));

 /* Head (magic..useHttps). */
 memcpy(&outCfg, buf, HEAD_SIZE);

 /* users_v14 → users_v15 (salt={0}, hashVersion=0 → legacy mode). */
 for (size_t i = 0; i < MAX_USERS; i++) {
 UserAccount_v14 u;
 memcpy(&u, buf + HEAD_SIZE + i * sizeof(UserAccount_v14), sizeof(UserAccount_v14));
 outCfg.users[i].active = u.active;
 memcpy(outCfg.users[i].username, u.username, sizeof(u.username));
 memcpy(outCfg.users[i].password, u.password, sizeof(u.password));
 outCfg.users[i].password[sizeof(u.password)] = '\0';
 outCfg.users[i].permissions = u.permissions;
 outCfg.users[i].mustChangePassword = u.mustChangePassword;
 memset(outCfg.users[i].salt, 0, sizeof(outCfg.users[i].salt));
 outCfg.users[i].hashVersion = 0;
 }

 /* Middle (telServer..ntpServer) — layout unchanged, only shifted. */
 memcpy(((uint8_t*)&outCfg) + HEAD_SIZE + V15_USER_BLOCK,
 buf + V12_MIDDLE_START,
 MIDDLE_SIZE);

 /* reserved[0..23] from v12; reserved[24..63] stays zero from memset. */
 memcpy(outCfg.reserved, buf + V12_RESERVED_START, V12_RESERVED_BYTES);

 outCfg.version = CONFIG_VERSION;
 return true;
}

/* Read config in v15 format (UserAccount[62], SensorRecord[79] with single gpio)
 * and migrate directly to v17 (16 universal slots).
 *
 * v15 had: sensors[10] + ambientSensor = 11 SensorRecord_v15 (79 B each)
 * v17 has: sensors[16] = 16 SensorRecord (83 B each)
 *
 * Migration: v15 sensors[0..9] → v17 sensors[0..9], v15 ambientSensor → v17 sensors[10],
 * v17 sensors[11..15] = inactive defaults. Tail copied to new offset. */
bool StorageManager::loadAndMigrateV15(File& f, SystemConfig& outCfg) {
 static constexpr size_t V15_SLOT_COUNT = 10; /* v15 had 10 configurable slots */
 constexpr size_t V15_SENSOR_SIZE = sizeof(SensorRecord_v15);
 constexpr size_t V16_SENSOR_SIZE = sizeof(SensorRecord);

 uint8_t* buf = new (std::nothrow) uint8_t[CONFIG_V15_BLOB_SIZE];
 if (!buf) return false;
 uint32_t readCrc = 0;

 size_t bytesRead = f.read(buf, CONFIG_V15_BLOB_SIZE);
 size_t crcRead = f.read((uint8_t*)&readCrc, sizeof(readCrc));

 if (bytesRead != CONFIG_V15_BLOB_SIZE) { delete[] buf; return false; }

 uint32_t fileMagic = 0;
 uint16_t fileVersion = 0;
 memcpy(&fileMagic, buf + 0, sizeof(fileMagic));
 memcpy(&fileVersion, buf + sizeof(fileMagic), sizeof(fileVersion));

 if (fileMagic != CONFIG_MAGIC || fileVersion != 15) { delete[] buf; return false; }

 if (crcRead == sizeof(readCrc)) {
 uint32_t calcCrc = calculateCRC32(buf, CONFIG_V15_BLOB_SIZE);
 if (calcCrc != readCrc) { delete[] buf; return false; }
 }

 memset(&outCfg, 0, sizeof(SystemConfig));

 /* Head: copy everything before sensors[] (magic..ds18Resolution). */
 constexpr size_t HEAD_SIZE = offsetof(SystemConfig, sensors);
 memcpy(&outCfg, buf, HEAD_SIZE);

 constexpr size_t V15_SENSORS_OFFSET = HEAD_SIZE;
 constexpr size_t V17_SENSORS_OFFSET = HEAD_SIZE;

 /* v15 sensors[0..9] → v17 sensors[0..9] */
 for (size_t i = 0; i < V15_SLOT_COUNT; i++) {
 SensorRecord_v15 s15;
 const uint8_t* src = buf + V15_SENSORS_OFFSET + i * V15_SENSOR_SIZE;
 memcpy(&s15, src, V15_SENSOR_SIZE);

 bool isDs18 = false;
 for (int k = 0; k < 8; k++) if (s15.rom[k] != 0) isDs18 = true;

 uint8_t* dst = ((uint8_t*)&outCfg) + V17_SENSORS_OFFSET + i * V16_SENSOR_SIZE;
 memcpy(dst + offsetof(SensorRecord, active), &s15.active, sizeof(s15.active));
 uint8_t st = (uint8_t)(isDs18 ? TYPE_DS18B20 : TYPE_DHT22);
 memcpy(dst + offsetof(SensorRecord, sensorType), &st, sizeof(st));
 uint8_t pns[4] = {s15.gpio, 255, 255, 255};
 memcpy(dst + offsetof(SensorRecord, pins), pns, sizeof(pns));
 memcpy(dst + offsetof(SensorRecord, rom), s15.rom, sizeof(s15.rom));
 memcpy(dst + offsetof(SensorRecord, hwId), s15.hwId, sizeof(s15.hwId));
 memcpy(dst + offsetof(SensorRecord, friendlyName), s15.friendlyName, sizeof(s15.friendlyName));
 memcpy(dst + offsetof(SensorRecord, provisionEpoch), &s15.provisionEpoch, sizeof(s15.provisionEpoch));
 memcpy(dst + offsetof(SensorRecord, tempMin), &s15.tempMin, 4 * sizeof(float));
 memcpy(dst + offsetof(SensorRecord, alarmsActive), &s15.alarmsActive, sizeof(s15.alarmsActive));
 }

 /* v15 ambientSensor → v17 sensors[10] */
 {
 SensorRecord_v15 s15;
 const uint8_t* src = buf + V15_SENSORS_OFFSET + V15_SLOT_COUNT * V15_SENSOR_SIZE;
 memcpy(&s15, src, V15_SENSOR_SIZE);

 uint8_t* dst = ((uint8_t*)&outCfg) + V17_SENSORS_OFFSET + V15_SLOT_COUNT * V16_SENSOR_SIZE;

 memcpy(dst + offsetof(SensorRecord, active), &s15.active, sizeof(s15.active));
 uint8_t st = (uint8_t)TYPE_DHT22; /* ambient was always DHT22 */
 memcpy(dst + offsetof(SensorRecord, sensorType), &st, sizeof(st));
 uint8_t pns[4] = {s15.gpio, 255, 255, 255};
 memcpy(dst + offsetof(SensorRecord, pins), pns, sizeof(pns));
 memcpy(dst + offsetof(SensorRecord, rom), s15.rom, sizeof(s15.rom));
 memcpy(dst + offsetof(SensorRecord, hwId), s15.hwId, sizeof(s15.hwId));
 memcpy(dst + offsetof(SensorRecord, friendlyName), s15.friendlyName, sizeof(s15.friendlyName));
 memcpy(dst + offsetof(SensorRecord, provisionEpoch), &s15.provisionEpoch, sizeof(s15.provisionEpoch));
 memcpy(dst + offsetof(SensorRecord, tempMin), &s15.tempMin, 4 * sizeof(float));
 memcpy(dst + offsetof(SensorRecord, alarmsActive), &s15.alarmsActive, sizeof(s15.alarmsActive));
 }

 /* Initialize v17 sensors[11..15] as inactive */
 for (size_t i = V15_SLOT_COUNT + 1; i < (size_t)MAX_SENSORS; i++) {
 uint8_t* dst = ((uint8_t*)&outCfg) + V17_SENSORS_OFFSET + i * V16_SENSOR_SIZE;
 bool active = false;
 memcpy(dst + offsetof(SensorRecord, active), &active, sizeof(active));
 uint8_t st = (uint8_t)TYPE_NONE;
 memcpy(dst + offsetof(SensorRecord, sensorType), &st, sizeof(st));
 uint8_t pns[4] = {255, 255, 255, 255};
 memcpy(dst + offsetof(SensorRecord, pins), pns, sizeof(pns));
 }

 /* Tail: v15 ambientSensor was at V15_SENSORS_OFFSET + 11*V15_SENSOR_SIZE.
 * v17 sensors[15] end at V17_SENSORS_OFFSET + 16*V16_SENSOR_SIZE. */
 constexpr size_t V15_TAIL_OFFSET =
 V15_SENSORS_OFFSET + (V15_SLOT_COUNT + 1) * V15_SENSOR_SIZE; /* 11 v15 records */
 constexpr size_t V17_TAIL_OFFSET =
 V17_SENSORS_OFFSET + (size_t)MAX_SENSORS * V16_SENSOR_SIZE; /* 16 v17 records */
 constexpr size_t TAIL_SIZE = CONFIG_V15_BLOB_SIZE - V15_TAIL_OFFSET;

 memcpy(((uint8_t*)&outCfg) + V17_TAIL_OFFSET,
 buf + V15_TAIL_OFFSET,
 TAIL_SIZE);

 obfuscateSensitiveFields(outCfg);
 outCfg.version = CONFIG_VERSION;
 return true;
}

/* Read config in v13 (plaintext) or v14 (obfuscated) format — both
 * with UserAccount_v14[52] — and upgrade to v16 schema (UserAccount[62] with
 * salt={0}/hashVersion=0, indicating legacy mode). The stored hash remains
 * valid and verifiable via hashPassword( ) (algorithm unchanged).
 *
 * Layout v14: [head (up to users)] [5 × UserAccount_v14] [tail (telServer..reserved)]
 * Layout v16: [head (same)] [5 × UserAccount_v16] [tail shifted by +50]
 *
 * srcVersion (out): original version read from file (13 or 14) for telemetry. */
bool StorageManager::loadAndMigrateV14(File& f, SystemConfig& outCfg, uint16_t& srcVersion) {
 constexpr size_t HEAD_SIZE = offsetof(SystemConfig, users);
 constexpr size_t V14_USER_BLOCK = MAX_USERS * sizeof(UserAccount_v14);
 constexpr size_t V15_USER_BLOCK = MAX_USERS * sizeof(UserAccount);
 constexpr size_t V14_TAIL_OFFSET = HEAD_SIZE + V14_USER_BLOCK;
 constexpr size_t V15_TAIL_OFFSET = HEAD_SIZE + V15_USER_BLOCK;
 constexpr size_t TAIL_SIZE = CONFIG_V14_BLOB_SIZE - V14_TAIL_OFFSET;

 uint8_t buf[CONFIG_V14_BLOB_SIZE];
 uint32_t readCrc = 0;

 size_t bytesRead = f.read(buf, CONFIG_V14_BLOB_SIZE);
 size_t crcRead = f.read((uint8_t*)&readCrc, sizeof(readCrc));

 if (bytesRead != CONFIG_V14_BLOB_SIZE) return false;

 /* Magic + version via aliasing (SystemConfig starts with uint32_t magic
 * + uint16_t version in all versions since v12). */
 uint32_t fileMagic = 0;
 uint16_t fileVersion = 0;
 memcpy(&fileMagic, buf + 0, sizeof(fileMagic));
 memcpy(&fileVersion, buf + sizeof(fileMagic), sizeof(fileVersion));

 if (fileMagic != CONFIG_MAGIC) return false;
 if (fileVersion != 13 && fileVersion != 14) return false;

 /* CRC32 calculated over the v14 blob as written. */
 if (crcRead == sizeof(readCrc)) {
 uint32_t calcCrc = calculateCRC32(buf, CONFIG_V14_BLOB_SIZE);
 if (calcCrc != readCrc) return false;
 }

 /* Zero outCfg and copy head (fields before users[]). */
 memset(&outCfg, 0, sizeof(SystemConfig));
 memcpy(&outCfg, buf, HEAD_SIZE);

 /* Expand each UserAccount_v14 → UserAccount v15. */
 for (size_t i = 0; i < MAX_USERS; i++) {
 UserAccount_v14 u14;
 memcpy(&u14, buf + HEAD_SIZE + i * sizeof(UserAccount_v14), sizeof(UserAccount_v14));
 outCfg.users[i].active = u14.active;
 memcpy(outCfg.users[i].username, u14.username, sizeof(u14.username));
 memcpy(outCfg.users[i].password, u14.password, sizeof(u14.password));
 outCfg.users[i].password[sizeof(u14.password)] = '\0'; /* null-term in new [33] */
 outCfg.users[i].permissions = u14.permissions;
 outCfg.users[i].mustChangePassword = u14.mustChangePassword;
 memset(outCfg.users[i].salt, 0, sizeof(outCfg.users[i].salt));
 outCfg.users[i].hashVersion = 0; /* legacy */
 }

 /* Copy tail in three parts: mid-section (telServer..ds18Resolution),
 * sensor area (expand 11 × v15 → 16 × v17), and post-sensor fields. */
 constexpr size_t MID_SIZE = offsetof(SystemConfig, sensors) - offsetof(SystemConfig, telServer);
 constexpr size_t V15_SENSOR_SIZE_T = sizeof(SensorRecord_v15);
 constexpr size_t V17_SENSOR_SIZE_T = sizeof(SensorRecord);

 /* 1. Mid-section: telServer..ds18Resolution (same in all versions) */
 memcpy(((uint8_t*)&outCfg) + offsetof(SystemConfig, telServer),
 buf + V14_TAIL_OFFSET,
 MID_SIZE);

 /* 2. Expand sensors: 11 v15 records → 16 v17 records */
 {
 static constexpr size_t V15_SLOTS = 10;
 const uint8_t* srcSensors = buf + V14_TAIL_OFFSET + MID_SIZE;
 uint8_t* dstSensors = ((uint8_t*)&outCfg) + offsetof(SystemConfig, sensors);

 for (size_t i = 0; i < V15_SLOTS; i++) {
 SensorRecord_v15 s15;
 memcpy(&s15, srcSensors + i * V15_SENSOR_SIZE_T, V15_SENSOR_SIZE_T);
 bool isDs18 = false;
 for (int k = 0; k < 8; k++) if (s15.rom[k] != 0) isDs18 = true;
 uint8_t* dst = dstSensors + i * V17_SENSOR_SIZE_T;
 memcpy(dst + offsetof(SensorRecord, active), &s15.active, sizeof(s15.active));
 uint8_t st = (uint8_t)(isDs18 ? TYPE_DS18B20 : TYPE_DHT22);
 memcpy(dst + offsetof(SensorRecord, sensorType), &st, sizeof(st));
 uint8_t pns[4] = {s15.gpio, 255, 255, 255};
 memcpy(dst + offsetof(SensorRecord, pins), pns, sizeof(pns));
 memcpy(dst + offsetof(SensorRecord, rom), s15.rom, sizeof(s15.rom));
 memcpy(dst + offsetof(SensorRecord, hwId), s15.hwId, sizeof(s15.hwId));
 memcpy(dst + offsetof(SensorRecord, friendlyName), s15.friendlyName, sizeof(s15.friendlyName));
 memcpy(dst + offsetof(SensorRecord, provisionEpoch), &s15.provisionEpoch, sizeof(s15.provisionEpoch));
 memcpy(dst + offsetof(SensorRecord, tempMin), &s15.tempMin, 4 * sizeof(float));
 memcpy(dst + offsetof(SensorRecord, alarmsActive), &s15.alarmsActive, sizeof(s15.alarmsActive));
 }
 /* v15 ambientSensor → v17 sensors[10] */
 {
 SensorRecord_v15 s15;
 memcpy(&s15, srcSensors + V15_SLOTS * V15_SENSOR_SIZE_T, V15_SENSOR_SIZE_T);
 uint8_t* dst = dstSensors + V15_SLOTS * V17_SENSOR_SIZE_T;
 memcpy(dst + offsetof(SensorRecord, active), &s15.active, sizeof(s15.active));
 uint8_t st = (uint8_t)TYPE_DHT22;
 memcpy(dst + offsetof(SensorRecord, sensorType), &st, sizeof(st));
 uint8_t pns[4] = {s15.gpio, 255, 255, 255};
 memcpy(dst + offsetof(SensorRecord, pins), pns, sizeof(pns));
 memcpy(dst + offsetof(SensorRecord, rom), s15.rom, sizeof(s15.rom));
 memcpy(dst + offsetof(SensorRecord, hwId), s15.hwId, sizeof(s15.hwId));
 memcpy(dst + offsetof(SensorRecord, friendlyName), s15.friendlyName, sizeof(s15.friendlyName));
 memcpy(dst + offsetof(SensorRecord, provisionEpoch), &s15.provisionEpoch, sizeof(s15.provisionEpoch));
 memcpy(dst + offsetof(SensorRecord, tempMin), &s15.tempMin, 4 * sizeof(float));
 memcpy(dst + offsetof(SensorRecord, alarmsActive), &s15.alarmsActive, sizeof(s15.alarmsActive));
 }
 /* sensors[11..15] = inactive */
 for (size_t i = V15_SLOTS + 1; i < (size_t)MAX_SENSORS; i++) {
 uint8_t* dst = dstSensors + i * V17_SENSOR_SIZE_T;
 bool active = false; uint8_t st = (uint8_t)TYPE_NONE; uint8_t pns[4] = {255,255,255,255};
 memcpy(dst + offsetof(SensorRecord, active), &active, sizeof(active));
 memcpy(dst + offsetof(SensorRecord, sensorType), &st, sizeof(st));
 memcpy(dst + offsetof(SensorRecord, pins), pns, sizeof(pns));
 }
 }

 /* 3. Post-sensor tail: themeIndex..reserved[64] */
 {
 constexpr size_t V14_POST_OFFSET = V14_TAIL_OFFSET + MID_SIZE + (10 + 1) * V15_SENSOR_SIZE_T;
 constexpr size_t V17_POST_OFFSET = offsetof(SystemConfig, themeIndex);
 constexpr size_t POST_SIZE = sizeof(SystemConfig) - offsetof(SystemConfig, themeIndex)
 - sizeof(SystemConfig::reserved) + sizeof(SystemConfig::reserved); /* themeIndex..reserved */
 memcpy(((uint8_t*)&outCfg) + V17_POST_OFFSET,
 buf + V14_POST_OFFSET,
 POST_SIZE);
 }

 /* v14 has sensitive fields obfuscated — deobfuscate. v13 is plaintext. */
 if (fileVersion == 14) {
 obfuscateSensitiveFields(outCfg);
 }

 srcVersion = fileVersion;
 outCfg.version = CONFIG_VERSION;
 return true;
}

/* Read config in v16 format (10 slots + ambientSensor) and migrate
 * to v17 (16 universal slots). AmbientSensor becomes sensors[10];
 * slots 11..15 initialized as inactive. Tail shifted by +5*sizeof(SensorRecord). */
bool StorageManager::loadAndMigrateV16(File& f, SystemConfig& outCfg) {
 constexpr size_t HEAD_SIZE = offsetof(SystemConfig, sensors);
 constexpr size_t V16_REC_SIZE = sizeof(SensorRecord);
 static constexpr size_t V16_SLOTS = 10;

 uint8_t* buf = new (std::nothrow) uint8_t[CONFIG_V16_BLOB_SIZE];
 if (!buf) return false;
 uint32_t readCrc = 0;

 size_t bytesRead = f.read(buf, CONFIG_V16_BLOB_SIZE);
 size_t crcRead = f.read((uint8_t*)&readCrc, sizeof(readCrc));

 if (bytesRead != CONFIG_V16_BLOB_SIZE) { delete[] buf; return false; }

 uint32_t fileMagic = 0;
 uint16_t fileVersion = 0;
 memcpy(&fileMagic, buf + 0, sizeof(fileMagic));
 memcpy(&fileVersion, buf + sizeof(fileMagic), sizeof(fileVersion));

 if (fileMagic != CONFIG_MAGIC || fileVersion != 16) { delete[] buf; return false; }

 if (crcRead == sizeof(readCrc)) {
 uint32_t calcCrc = calculateCRC32(buf, CONFIG_V16_BLOB_SIZE);
 if (calcCrc != readCrc) { delete[] buf; return false; }
 }

 memset(&outCfg, 0, sizeof(SystemConfig));

 /* Head (magic..ds18Resolution) — same in v16 and v17 */
 memcpy(&outCfg, buf, HEAD_SIZE);

 /* sensors[0..9] — copy directly (same format) */
 constexpr size_t V16_SENSORS_OFFSET = HEAD_SIZE;
 constexpr size_t V17_SENSORS_OFFSET = HEAD_SIZE;
 for (size_t i = 0; i < V16_SLOTS; i++) {
 memcpy(((uint8_t*)&outCfg) + V17_SENSORS_OFFSET + i * V16_REC_SIZE,
 buf + V16_SENSORS_OFFSET + i * V16_REC_SIZE,
 V16_REC_SIZE);
 }

 /* v16 ambientSensor → v17 sensors[10] */
 memcpy(((uint8_t*)&outCfg) + V17_SENSORS_OFFSET + V16_SLOTS * V16_REC_SIZE,
 buf + V16_SENSORS_OFFSET + V16_SLOTS * V16_REC_SIZE,
 V16_REC_SIZE);

 /* sensors[11..15] = inactive defaults */
 for (size_t i = V16_SLOTS + 1; i < (size_t)MAX_SENSORS; i++) {
 uint8_t* dst = ((uint8_t*)&outCfg) + V17_SENSORS_OFFSET + i * V16_REC_SIZE;
 bool active = false; uint8_t st = (uint8_t)TYPE_NONE; uint8_t pns[4] = {255,255,255,255};
 uint8_t rom[8] = {0}; uint32_t pe = 0; float fdef = 0.0f;
 memcpy(dst + offsetof(SensorRecord, active), &active, sizeof(active));
 memcpy(dst + offsetof(SensorRecord, sensorType), &st, sizeof(st));
 memcpy(dst + offsetof(SensorRecord, pins), pns, sizeof(pns));
 memcpy(dst + offsetof(SensorRecord, rom), rom, sizeof(rom));
 memcpy(dst + offsetof(SensorRecord, provisionEpoch), &pe, sizeof(pe));
 memcpy(dst + offsetof(SensorRecord, tempMin), &fdef, sizeof(fdef));
 memcpy(dst + offsetof(SensorRecord, tempMax), &fdef, sizeof(fdef));
 memcpy(dst + offsetof(SensorRecord, humMin), &fdef, sizeof(fdef));
 memcpy(dst + offsetof(SensorRecord, humMax), &fdef, sizeof(fdef));
 memcpy(dst + offsetof(SensorRecord, alarmsActive), &active, sizeof(active));
 }

 /* Tail (themeIndex..reserved) — shifted by +5 records */
 constexpr size_t V16_TAIL_OFFSET = V16_SENSORS_OFFSET + (V16_SLOTS + 1) * V16_REC_SIZE; /* 11 records */
 constexpr size_t V17_TAIL_OFFSET = V17_SENSORS_OFFSET + (size_t)MAX_SENSORS * V16_REC_SIZE; /* 16 records */
 constexpr size_t TAIL_SIZE = CONFIG_V16_BLOB_SIZE - V16_TAIL_OFFSET;

 memcpy(((uint8_t*)&outCfg) + V17_TAIL_OFFSET,
 buf + V16_TAIL_OFFSET,
 TAIL_SIZE);

 obfuscateSensitiveFields(outCfg);
 outCfg.version = CONFIG_VERSION;
 return true;
}

bool StorageManager::attemptLoad(const char* path, SystemConfig& outCfg) {
 File f = LittleFS.open(path, "r");
 if (!f) return false;
 size_t fileSize = f.size( );

 /* Current format (v17 — 16 universal sensor slots, no ambientSensor). */
 if (fileSize == sizeof(SystemConfig) + sizeof(uint32_t)) {
 bool ok = loadCurrentBlob(f, outCfg);
 f.close( );
 return ok;
 }

 /* v16 (10 slots + ambientSensor) → migrate to v17. */
 if (fileSize == CONFIG_V16_BLOB_SIZE + sizeof(uint32_t)) {
 bool ok = loadAndMigrateV16(f, outCfg);
 f.close( );
 if (ok) { _didMigrate = true; _migrationFromVersion = 16; }
 return ok;
 }

 /* v15 (SensorRecord grew +4 bytes per sensor = +44 bytes total). */
 if (fileSize == CONFIG_V15_BLOB_SIZE + sizeof(uint32_t)) {
 bool ok = loadAndMigrateV15(f, outCfg);
 f.close( );
 if (ok) { _didMigrate = true; _migrationFromVersion = 15; }
 return ok;
 }

 /* v13 plaintext or v14 obfuscated (same size, layout with
 * UserAccount_v14[52]) → migrate to v16. */
 if (fileSize == CONFIG_V14_BLOB_SIZE + sizeof(uint32_t)) {
 uint16_t srcVersion = 0;
 bool ok = loadAndMigrateV14(f, outCfg, srcVersion);
 f.close( );
 if (ok) {
 _didMigrate = true;
 _migrationFromVersion = srcVersion; /* 13 or 14 */
 }
 return ok;
 }

 /* v12 (pre reserved[] expansion) → migrate via dedicated path. */
 if (fileSize == CONFIG_V12_BLOB_SIZE + sizeof(uint32_t)) {
 bool ok = loadAndMigrateV12(f, outCfg);
 f.close( );
 if (ok) { _didMigrate = true; _migrationFromVersion = 12; }
 return ok;
 }

 f.close( );
 return false;
}

bool StorageManager::loadConfiguration( ) {

 _didMigrate = false;

 enterFlashReadLock( );
 SystemConfig* tempConfig = new (std::nothrow) SystemConfig;
 bool loaded = false;
 bool fromBackup = false;
 if (tempConfig) {
 if (LittleFS.exists(FILE_CONFIG) && attemptLoad(FILE_CONFIG, *tempConfig)) {
 _currentConfig = *tempConfig;
 loaded = true;
 } else if (LittleFS.exists(FILE_BACKUP) && attemptLoad(FILE_BACKUP, *tempConfig)) {
 _currentConfig = *tempConfig;
 loaded = true;
 fromBackup = true;
 }
 delete tempConfig;
 }
 exitFlashReadLock( );

 if (!loaded) {
 loadDefaults( );
 return false;
 }

 /* Valid config was loaded from flash — the random password generated
 * by the constructor (via loadDefaults) is now garbage. Zero it to avoid leaking
 * via logs/display. If the loaded config STILL has mustChangePassword,
 * it means a previous factory was never changed; but the original plaintext
 * was lost on some reboot — unrecoverable. */
 clearInitialAdminPassword( );

 /* Limit TOTAL_LANGS from 8 to 2 (EN+PT). Devices that
 * had displayLang=ES..ZH in flash fall to PT (default) on next
 * boot to avoid out-of-bounds in DICTIONARY[]/LICENSE_TEXT[]. */
 if (_currentConfig.displayLang > LANG_PT) {
 _currentConfig.displayLang = LANG_PT;
 }

 if (fromBackup) {
 LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_RECOVER, 0, TRL("Primary config corrupt, recovered from backup"));
 }

 /* Schema migration (v12/v13/v14/v15 → v16): persist in new format before
 * handing over control. saveConfiguration( ) marks magic/version,
 * encrypts sensitive fields and writes via atomic tmp→rename. */
 if (_didMigrate) {
 int fromVer = _migrationFromVersion;
 _didMigrate = false;
 _migrationFromVersion = 0;
 LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_MIGRATED, fromVer,
 TRL("Config schema migrated"));
 saveConfiguration( );
 } else if (fromBackup) {
 saveConfiguration( );
 }
 return true;
}

/**
 * @brief Atomic configuration save: write to temp file, then rename.
 * Maintains a backup copy for recovery if the primary is corrupted.
 * CRC32 appended after the binary blob for integrity verification.
 */
bool StorageManager::saveConfiguration( ) {
 /* Granular instrumentation — autopsy distinguishes if stuck here
 * vs in LOG_CODE audit or webMgr handler. */
 LogManager::TraceScope _tr(0, MOD_SAVE_CONFIG);

 /* Flush pending cursor before saving config — ensures consistency */
 if (_cursorDirty) { _cursorDirty = false; /* force flush below */ }

 _currentConfig.magic = CONFIG_MAGIC;
 _currentConfig.version = CONFIG_VERSION;

 /* If admin changed password (mustChangePassword = false),
 * the initial RAM password lost value — zero before save to ensure
 * it doesn't leak in logs/display/CLI from here on. */
 if (_initialAdminPassword[0] != '\0' && !_currentConfig.users[0].mustChangePassword) {
 clearInitialAdminPassword( );
 }

 /*
 * Skip no-op: user clicks "Save" multiple times without changing fields →
 * burst of identical saves that pressures LittleFS GC without real gain.
 * If RAM content matches last saved, skip the write.
 * _lastSavedCrc is now a private class member (was previously
 * static local) — aligns with the pattern of other _last* fields.
 */
 uint32_t currentCrc = calculateCRC32((uint8_t*)&_currentConfig, sizeof(SystemConfig));
 if (currentCrc == _lastSavedCrc && _lastSavedCrc != 0) {
 _lastSaveWasNoOp = true;
 MetricsManager::instance( ).data( ).configSaves++; /* still counts as requested save */
 /* No LOG_CODE here: log file write pressures GC on click bursts. */
 return true;
 }
 _lastSaveWasNoOp = false;

 /*
 * RAII context-aware: extends WDT ctx to 30s (or keeps outer if larger,
 * ex: telemetry at 120s). Auto-restore on any exit path.
 */
 LogManager::WdtWindow _wdt(30000);

 /* Enter cooperative quiet mode. Core 1 is signaled
 * to freeze in a RAM-only loop with IRQs off; Core 0 then does all
 * flash ops without attempting IRQ-based multicore_lockout per chunk (which
 * could stuck and cascade). RAII releases on any return path. */
 struct BigSaveGuard {
 bool& inBigSaveRef;
 BigSaveQuietCallback cb;
 bool entered;
 BigSaveGuard(bool& r, BigSaveQuietCallback c) : inBigSaveRef(r), cb(c), entered(false) {
 if (cb) {
 entered = cb(true);
 inBigSaveRef = entered;
 }
 }
 ~BigSaveGuard( ) {
 if (entered && cb) cb(false);
 inBigSaveRef = false;
 }
 } _bigSave(_inBigSave, _bigSaveQuietCb);

 /* With _inBigSave active, enterFlashSafeMode/exitFlashSafeMode skip the
 * IRQ-based lockCb (see method in StorageManager.cpp), so each
 * FLASH_OP becomes just watchdog_update + BLOCK + watchdog_update — without
 * pausing Core 1 per op (Core 1 is already frozen in the quiet loop). */

 /* Flush cursor to flash (if pending) */
 if (_cachedLastSent > 0) {
 FLASH_OP({
 File cf = LittleFS.open(FILE_TCURSOR, "w");
 if (cf) { cf.write((uint8_t*)&_cachedLastSent, sizeof(_cachedLastSent)); cf.close( ); }
 });
 }

 /* Open TMP and write encrypted config + CRC */
 File f;
 FLASH_OP(f = LittleFS.open(FILE_TMP, "w"));
 if (!f) {
 LOG_CODE(LOG_ERROR, "STO", SYS_STORAGE_FAIL, 0, "open FILE_TMP failed");
 return false;
 }

 /* Copy with sensitive fields encrypted. _currentConfig in
 * RAM stays plaintext. CRC over the encrypted version. */
 SystemConfig* encBuf = new (std::nothrow) SystemConfig;
 if (!encBuf) { f.close( ); return false; }
 *encBuf = _currentConfig;
 obfuscateSensitiveFields(*encBuf);
 uint32_t crc = calculateCRC32((uint8_t*)encBuf, sizeof(SystemConfig));

 size_t bytesWritten = 0, crcWritten = 0;
 FLASH_OP({
 bytesWritten = f.write((uint8_t*)encBuf, sizeof(SystemConfig));
 crcWritten = f.write((uint8_t*)&crc, sizeof(crc));
 f.close( );
 });
 delete encBuf;

 if (bytesWritten != sizeof(SystemConfig) || crcWritten != sizeof(crc)) {
 FLASH_OP(LittleFS.remove(FILE_TMP));
 LOG_CODE(LOG_ERROR, "STO", SYS_STORAGE_FAIL, (int)bytesWritten, "Config save failed");
 return false;
 }

 /* Atomic rename: backup, rename, rename. Each in its own lockout. */
 if (LittleFS.exists(FILE_CONFIG)) {
 FLASH_OP(LittleFS.remove(FILE_BACKUP));
 FLASH_OP(LittleFS.rename(FILE_CONFIG, FILE_BACKUP));
 }
 FLASH_OP(LittleFS.rename(FILE_TMP, FILE_CONFIG));

 MetricsManager::instance( ).data( ).configSaves++;
 _lastSavedCrc = currentCrc; /* Mark persisted content for future no-op skip */
 _lastSaveMs = millis( ); /* Timestamp for server-side rate-limit */
 LOG_CODE(LOG_INFO, "STO", SYS_STORAGE_SAVE, (int)(sizeof(SystemConfig)), "");
 return true;
}

bool StorageManager::canSaveNow( ) const {
 constexpr uint32_t MIN_SAVE_INTERVAL_MS = 1000;
 if (_lastSaveMs == 0) return true;
 return timeSince(_lastSaveMs, MIN_SAVE_INTERVAL_MS);
}

void StorageManager::resetToFactory( ) { loadDefaults( ); saveConfiguration( ); }
SystemConfig& StorageManager::getConfig( ) { return _currentConfig; }

/* Helpers for random initial admin password. */
void StorageManager::generateInitialAdminPassword(char* outPlain, size_t bufSize) {
 static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; /* 32 chars, without O/0/I/1 */
 const size_t alphabetLen = sizeof(alphabet) - 1; /* -1 for '\0' */
 const size_t len = (bufSize > 9) ? 8 : (bufSize > 0 ? bufSize - 1 : 0);
 for (size_t i = 0; i < len; i++) {
 outPlain[i] = alphabet[rp2040.hwrand32( ) % alphabetLen];
 }
 if (bufSize > 0) outPlain[len] = '\0';
}

bool StorageManager::isFactoryDefaults( ) const {
 if (!_currentConfig.users[0].active) return false;
 if (strcmp(_currentConfig.users[0].username, "admin") != 0) return false;
 return _currentConfig.users[0].mustChangePassword;
}

void StorageManager::clearInitialAdminPassword( ) {
 /* volatile to prevent optimizer from eliminating "dead" memset. */
 volatile char* p = _initialAdminPassword;
 for (size_t i = 0; i < sizeof(_initialAdminPassword); i++) p[i] = 0;
}

/* mustChangePin flag in reserved[26..27]. */
bool StorageManager::mustChangePin( ) const {
 const SetupFlagsData* sf = reinterpret_cast<const SetupFlagsData*>(
 _currentConfig.reserved + SETUP_FLAGS_OFFSET);
 /* Overlay without magic = legacy config (v13/v14 without setup flags) — don't force
 * change to not break firmware upgrades for existing users. */
 if (sf->magic != SETUP_FLAGS_MAGIC) return false;
 return (sf->flags & FLAG_MUST_CHANGE_PIN) != 0;
}

void StorageManager::clearMustChangePin( ) {
 SetupFlagsData* sf = reinterpret_cast<SetupFlagsData*>(
 _currentConfig.reserved + SETUP_FLAGS_OFFSET);
 if (sf->magic != SETUP_FLAGS_MAGIC) {
 /* Initialize overlay if not yet. */
 sf->magic = SETUP_FLAGS_MAGIC;
 sf->flags = 0;
 } else {
 sf->flags &= ~FLAG_MUST_CHANGE_PIN;
 }
}

void StorageManager::setMustChangePin( ) {
 SetupFlagsData* sf = reinterpret_cast<SetupFlagsData*>(
 _currentConfig.reserved + SETUP_FLAGS_OFFSET);
 sf->magic = SETUP_FLAGS_MAGIC;
 sf->flags |= FLAG_MUST_CHANGE_PIN;
}

/* ===========================================================================
 * NetworkTimeData overlay in reserved[28..47]
 * =========================================================================== */

NetworkTimeData* StorageManager::ensureNetworkTimeOverlay( ) {
 NetworkTimeData* nt = reinterpret_cast<NetworkTimeData*>(
 _currentConfig.reserved + NETTIME_OFFSET);
 if (nt->magic != NETTIME_MAGIC) {
 nt->magic = NETTIME_MAGIC;
 nt->flags = FLAG_DNS_AUTO | FLAG_NTP_ENABLED;
 nt->dns2[0] = '\0';
 nt->pad[0] = nt->pad[1] = 0;
 }
 return nt;
}

bool StorageManager::isDnsAuto( ) const {
 const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
 _currentConfig.reserved + NETTIME_OFFSET);
 if (nt->magic != NETTIME_MAGIC) return true; /* legacy = default AUTO */
 return (nt->flags & FLAG_DNS_AUTO) != 0;
}

void StorageManager::setDnsAuto(bool auto_) {
 NetworkTimeData* nt = ensureNetworkTimeOverlay( );
 if (auto_) nt->flags |= FLAG_DNS_AUTO;
 else nt->flags &= ~FLAG_DNS_AUTO;
}

bool StorageManager::isNtpEnabled( ) const {
 const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
 _currentConfig.reserved + NETTIME_OFFSET);
 if (nt->magic != NETTIME_MAGIC) return true;
 return (nt->flags & FLAG_NTP_ENABLED) != 0;
}

void StorageManager::setNtpEnabled(bool enabled) {
 NetworkTimeData* nt = ensureNetworkTimeOverlay( );
 if (enabled) nt->flags |= FLAG_NTP_ENABLED;
 else nt->flags &= ~FLAG_NTP_ENABLED;
}

const char* StorageManager::getSecondaryDns( ) const {
 const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
 _currentConfig.reserved + NETTIME_OFFSET);
 if (nt->magic != NETTIME_MAGIC) return "";
 return nt->dns2;
}

void StorageManager::setSecondaryDns(const char* ip) {
 NetworkTimeData* nt = ensureNetworkTimeOverlay( );
 safeCopy(nt->dns2, ip ? ip : "", sizeof(nt->dns2));
}

uint16_t StorageManager::getHistoryIntervalMin( ) const {
 const HistoryConfigData* hc = reinterpret_cast<const HistoryConfigData*>(
 _currentConfig.reserved + HISTORY_CONFIG_OFFSET);
 if (hc->magic != HISTORY_CONFIG_MAGIC) return HISTORY_INTERVAL_DEFAULT_MIN;
 uint16_t v = hc->intervalMin;
 if (v < HISTORY_INTERVAL_MIN_MIN) return HISTORY_INTERVAL_DEFAULT_MIN;
 if (v > HISTORY_INTERVAL_MAX_MIN) return HISTORY_INTERVAL_MAX_MIN;
 return v;
}

void StorageManager::setHistoryIntervalMin(uint16_t minutes) {
 if (minutes < HISTORY_INTERVAL_MIN_MIN) minutes = HISTORY_INTERVAL_MIN_MIN;
 if (minutes > HISTORY_INTERVAL_MAX_MIN) minutes = HISTORY_INTERVAL_MAX_MIN;
 HistoryConfigData* hc = reinterpret_cast<HistoryConfigData*>(
 _currentConfig.reserved + HISTORY_CONFIG_OFFSET);
 hc->magic = HISTORY_CONFIG_MAGIC;
 hc->pad = 0;
 hc->intervalMin = minutes;
}

SensorRecord* StorageManager::getSensorByGpio(uint8_t gpio) {
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (_currentConfig.sensors[i].active && _currentConfig.sensors[i].pins[0] == gpio) {
 return &_currentConfig.sensors[i];
 }
 }
 return nullptr;
}

bool StorageManager::canWriteHistory(size_t sizeToWrite) { return _isMounted; }

String StorageManager::getHistoryFileName( ) {
 time_t now = time(nullptr);
 struct tm timeinfo;
 localtime_r(&now, &timeinfo);
 char buff[40]; snprintf(buff, sizeof(buff), "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
 return String(buff);
}

void StorageManager::getHistoryFileName(char* buf, size_t len) {
 time_t now = time(nullptr);
 struct tm timeinfo;
 localtime_r(&now, &timeinfo);
 snprintf(buf, len, "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

/* ── V4 history file name ──────────────────────────────────── */

String StorageManager::getHistoryFileNameV4( ) {
	 time_t now = time(nullptr);
	 struct tm timeinfo;
	 localtime_r(&now, &timeinfo);
	 char buff[42];
	 snprintf(buff, sizeof(buff), "%s/%04d%02d%02d" HISTORY_V4_FILE_EXT, DIR_HISTORY,
	          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
	 return String(buff);
}

void StorageManager::getHistoryFileNameV4(char* buf, size_t len) {
	 time_t now = time(nullptr);
	 struct tm timeinfo;
	 localtime_r(&now, &timeinfo);
	 snprintf(buf, len, "%s/%04d%02d%02d" HISTORY_V4_FILE_EXT, DIR_HISTORY,
	          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

/* ── V4 Schema Builder ──────────────────────────────────────── */

bool StorageManager::buildMeasureSchema(
     HistV4SensorDef *sensors, uint8_t &sensorCount,
     HistV4MeasureDef *measures, uint8_t &measureCount,
     uint8_t *strPool, uint8_t &strPoolSize)
{
	 sensorCount   = 0;
	 measureCount  = 0;
	 strPoolSize   = 0;

	 for (int slot = 0; slot < MAX_SENSORS; slot++) {
	 if (!_currentConfig.sensors[slot].active) continue;

	 const auto &sr = _currentConfig.sensors[slot];
	 SensorFormat fmt = SensorFormat::forType((SensorType)sr.sensorType);

	 /* ── Add sensor to sensor table ── */
	 uint8_t hwIdOff = strPoolSize;
	 uint8_t hwIdLen = (uint8_t)strlen(sr.hwId);
	 if (hwIdLen > 0 && strPoolSize + hwIdLen <= HIST_V4_MAX_STRPOOL) {
	 memcpy(strPool + strPoolSize, sr.hwId, hwIdLen);
	 strPoolSize += hwIdLen;
	 } else {
	 continue;
	 }

	 uint8_t nameOff = strPoolSize;
	 uint8_t nameLen = (uint8_t)strlen(sr.friendlyName);
	 if (strPoolSize + nameLen <= HIST_V4_MAX_STRPOOL) {
	 memcpy(strPool + strPoolSize, sr.friendlyName, nameLen);
	 strPoolSize += nameLen;
	 } else {
	 nameLen = 0;
	 }

	 uint8_t chMask = 0;
	 for (uint8_t ch = 0; ch < MAX_SENSOR_CHANNELS; ch++) {
	 if (sensorHasChannel((SensorType)sr.sensorType, ch)) {
	 chMask |= (1 << ch);
	 }
	 }

	 sensors[sensorCount].hwIdOffset  = hwIdOff;
	 sensors[sensorCount].hwIdLen     = hwIdLen;
	 sensors[sensorCount].nameOffset  = nameOff;
	 sensors[sensorCount].nameLen     = nameLen;
	 sensors[sensorCount].sensorType  = sr.sensorType;
	 sensors[sensorCount].channelMask = chMask;
	 sensors[sensorCount].flags       = 0;
	 memset(sensors[sensorCount].reserved, 0, 2);

	 /* ── Add measurements (one per channel of this sensor) ── */
	 for (uint8_t ch = 0; ch < MAX_SENSOR_CHANNELS; ch++) {
	 if (!sensorHasChannel((SensorType)sr.sensorType, ch)) continue;

	 const auto &vf = fmt.values[ch];

	 uint8_t unitOff = strPoolSize;
	 uint8_t unitLen = (uint8_t)strlen(vf.unit);
	 if (strPoolSize + unitLen > HIST_V4_MAX_STRPOOL) break;
	 memcpy(strPool + strPoolSize, vf.unit, unitLen);
	 strPoolSize += unitLen;

	 /* Use user-configured bit width if set, else default for channel */
	 uint8_t bw = sr.channelBitWidth[ch];
	 if (bw == 0) bw = histV4DefaultBitWidth(ch);

	 measures[measureCount].sensorIdx  = sensorCount;
	 measures[measureCount].channel    = ch;
	 measures[measureCount].bitWidth   = bw;
	 measures[measureCount].decimals   = vf.decimals;
	 measures[measureCount].unitOffset = unitOff;
	 measures[measureCount].unitLen    = unitLen;
	 measures[measureCount].scale      = histV4DefaultScale(ch);
	 measureCount++;
	 if (measureCount >= HIST_V4_MAX_MEASUREMENTS) break;
	 }

	 sensorCount++;
	 if (sensorCount >= HIST_V4_MAX_SENSORS) break;
	 if (measureCount >= HIST_V4_MAX_MEASUREMENTS) break;
	 }

	 return sensorCount > 0 && measureCount > 0;
}


bool StorageManager::flushPendingHist( ) {
 if (!_isMounted || !_pendingHistValid) return false;
 BinaryHistoryRecord rec = _pendingHistRec;
 _pendingHistValid = false;
 return writeHistoryEntryFlash(rec);
}

bool StorageManager::writeHistoryEntry(const BinaryHistoryRecord& rec) {
 if (!_isMounted) return false;

 /* Defensive: reject absurd timestamps (epoch < 2023-11 or in the future).
 * Avoids creating files like /history/19691231.bin when the clock hasn't
 * synced via NTP yet — those files confuse the telemetry
 * cursor later (absurd epochs in the payload). */
 {
 const uint32_t EPOCH_MIN = 1700000000UL;
 uint32_t nowEpoch = (uint32_t)time(nullptr);
 if (rec.epoch < EPOCH_MIN) return false;
 if (nowEpoch > EPOCH_MIN && rec.epoch > nowEpoch + 86400UL) return false;
 }

 /* Touch priority: if user is interacting, buffer and return. Only the
 * most recent record survives (single slot) — acceptable since it's
 * 1x/min sampling and typical interaction is <15 s. */
 if (TouchPriority::isActive( )) {
 _pendingHistRec = rec;
 _pendingHistValid = true;
 return true;
 }

 /* Flush pending (if exists) before writing current */
 if (_pendingHistValid) {
 _pendingHistValid = false;
 writeHistoryEntryFlash(_pendingHistRec);
 }

 return writeHistoryEntryFlash(rec);
}

/* Helper: scan existing v2 file from beginning to end, reconstructing the
 * codec state. Returns true if valid header + scan ok; false if
 * header invalid (caller should delete and recreate). Leaves file pos at
 * end of last successfully decoded record. */
static bool scanHistoryFileForState(File& f, HistoryCodecState& s) {
 historyCodecReset(s);
 if (f.size( ) < HIST_V2_HEADER_SIZE) return false;
 f.seek(0);
 HistoryFileHeaderV2 hdr;
 if (f.read((uint8_t*)&hdr, HIST_V2_HEADER_SIZE) != HIST_V2_HEADER_SIZE) return false;
 if (memcmp(hdr.magic, HIST_V2_MAGIC, 4) != 0 ||
 (hdr.version != HIST_V2_VERSION && hdr.version != HIST_V3_VERSION)) return false;
 /* Set codec file version from header — v2 (40B anchor) or v3 (74B anchor) */
 s.fileVersion = hdr.version;

 uint8_t buf[256];
 size_t filled = 0;
 BinaryHistoryRecord tmp;
 size_t goodPos = HIST_V2_HEADER_SIZE;

 while (true) {
 /* Refill buffer */
 if (filled < HIST_V2_MAX_DELTA_SIZE && f.available( ) > 0) {
 int r = f.read(buf + filled, sizeof(buf) - filled);
 if (r > 0) filled += (size_t)r;
 }
 if (filled == 0) break;
 bool isAnchor = (s.recordsSinceAnchor == 0) ||
 (s.recordsSinceAnchor == HIST_V2_ANCHOR_PERIOD);
 size_t consumed = historyDecodeRecord(buf, filled, s, tmp, isAnchor);
 if (consumed == 0) break; /* truncated / corrupt: stop here */
 goodPos += consumed;
 memmove(buf, buf + consumed, filled - consumed);
 filled -= consumed;
 }

 /* Position at end of last valid record (discard corrupted tail). */
 f.seek(goodPos);
 return true;
}

bool StorageManager::writeHistoryEntryFlash(const BinaryHistoryRecord& rec) {
 if (!_isMounted) return false;

 /* Reject epoch < valid minimum. The caller
 * (processHistoryLogging) already has the gate, but writeHistory is exposed via
 * StorageManager.h and can be called by new callers in the future. Without
 * this check, a caller forgetting the gate would pollute history with
 * epoch=0 records that break telemetry, codec V2 anchor logic, and
 * generate "19700101.bin" filenames. Silent reject (there's already a
 * warn-once in the caller). */
 if (rec.epoch <= 1600000000UL) return false;

 String path = getHistoryFileName( );

 LogManager::TraceScope _tr(0, MOD_HIST_FLASH);
 LogManager::WdtWindow _wdt(30000);

 /* Chunk 1: enforce storage limit (only on daily rollover). */
 if (path != _currentLogFileName) {
 FLASH_OP(enforceStorageLimit( ));
 _currentLogFileName = path;
 _histCodecValid = false; /* force state reload on file change */
 }

 /* Chunk 2: prepare state if needed (boot or rollover). */
 if (!_histCodecValid) {
 FLASH_OP({
 bool created = false;
 if (LittleFS.exists(path)) {
 File f = LittleFS.open(path, "r+");
 if (f) {
 if (!scanHistoryFileForState(f, _histCodec)) {
 f.close( );
 LittleFS.remove(path);
 created = true;
 } else {
 f.close( );
 }
 }
 } else {
 created = true;
 }
 if (created) {
 File f = LittleFS.open(path, "w");
 if (f) {
 HistoryFileHeaderV2 hdr;
 memcpy(hdr.magic, HIST_V2_MAGIC, 4);
 hdr.version = HIST_V3_VERSION;
 hdr.anchorPeriod = HIST_V2_ANCHOR_PERIOD;
 hdr.flags = 0;
 hdr.recordCount = 0;
 f.write((const uint8_t*)&hdr, HIST_V2_HEADER_SIZE);
 f.close( );
 }
 historyCodecReset(_histCodec);
 /* Set codec to v3 mode for new files (after reset which clears to 0) */
 _histCodec.fileVersion = HIST_V3_VERSION;
 }
 });
 _histCodecValid = true;
 }

 /* Chunk 3: encode record (anchor or delta) and append.
  * Buffer must hold max(sizeof(BinaryHistoryRecord), HIST_V2_MAX_DELTA_SIZE)
  * = max(74, 120) = 120. Round to 128 for safety. */
 uint8_t encBuf[128];
 bool wasAnchor = false;
 size_t encLen = historyEncodeRecord(rec, _histCodec, encBuf, sizeof(encBuf), &wasAnchor);
 if (encLen == 0) {
 LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "encode_fail");
 return false;
 }

 bool ok = false;
 FLASH_OP({
 File f = LittleFS.open(path, "a");
 if (f) {
 f.write(encBuf, encLen);
 f.close( );
 ok = true;
 }
 });

 if (ok) { _storageDirty = true; return true; }

 /* Fallback: force enforce + retry. */
 LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "");
 _storageDirty = true;
 FLASH_OP(enforceStorageLimit( ));
 FLASH_OP({
 File f = LittleFS.open(path, "a");
 if (f) {
 f.write(encBuf, encLen);
 f.close( );
 ok = true;
 }
 });
 return ok;
}

/**
 * @brief Delete oldest history files to keep flash usage below 86%.
 * Uses dirty-flag caching and a 4-second budget timer to avoid
 * blocking the main loop during extensive cleanup.
 */
void StorageManager::enforceStorageLimit( ) {
 FSInfo info; LittleFS.info(info);
 if (info.totalBytes == 0) return;


 if (!_storageDirty && ((info.usedBytes * 100) / info.totalBytes) <= 86) return;

 int maxIter = 30;
 uint32_t _budgetStart = millis( );
 while (maxIter-- > 0 && ((info.usedBytes * 100) / info.totalBytes) > 86) {
 feedWdt( );
 if (timeSince(_budgetStart, 4000)) {
 LOG_CODE(LOG_WARN, "STO", STO_ENFORCE_BUDGET, 0, "");
 break;
 }


 String oldestFile = _cachedOldestFile;
 if (oldestFile.length( ) == 0) {
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 int dirCount = 0;
 while (dir.next( )) {
 feedWdt( );
 if (++dirCount % 20 == 0) delay(1);
 String fileName = dir.fileName( );


 if ((fileName.endsWith(HISTORY_FILE_EXT) || fileName.endsWith(HISTORY_V4_FILE_EXT)) && isValidHistoryFileName(fileName.c_str( ))) {
 if (oldestFile == "" || fileName < oldestFile) oldestFile = fileName;
 }
 }
 }

 if (oldestFile != "") {
 String fullPath = String(DIR_HISTORY) + "/" + oldestFile;

 if (fullPath == _currentLogFileName) {
 LOG_CODE(LOG_WARN, "STO", STO_ENFORCE_SKIP_ACTIVE, 0, "");
 break;
 }
 LittleFS.remove(fullPath);
 _cachedOldestFile = "";
 LittleFS.info(info);
 } else break;
 }


 if (((info.usedBytes * 100) / info.totalBytes) <= 86) _storageDirty = false;
}

uint32_t StorageManager::getLastSentTimestamp( ) {
 if (_cachedLastSent > 0) return _cachedLastSent;

 enterFlashReadLock( );
 if (!LittleFS.exists(FILE_TCURSOR)) { exitFlashReadLock( ); return 0; }
 File f = LittleFS.open(FILE_TCURSOR, "r");
 uint32_t ts = 0;
 if (f) { f.read((uint8_t*)&ts, sizeof(ts)); f.close( ); }
 exitFlashReadLock( );
 _cachedLastSent = ts;
 return ts;
}

void StorageManager::setLastSentTimestamp(uint32_t ts) {
 _cachedLastSent = ts;
 _cursorDirty = true;
 _cursorCoalesceTime = millis( );
}

/**
 * @brief CMD_TEL_RESET: reset telemetry cursor without needing reboot.
 * Invalidates RAM cache (_cachedLastSent=0), clears pending coalescer,
 * and removes the flash file. Next getLastSentTimestamp returns 0; next
 * collectBatch applies the "lastRecorded - 30 days" fallback.
 *
 * Use cases:
 * - Operations: re-send data after prolonged server outage (cursor
 * got well beyond what the server has).
 * - Maintenance: move data to another destination and want to re-send from scratch.
 * - Testing: tools/stress_test/run_stress_test.sh calls this path
 * (previously used /api/delete + reboot via Serial).
 */
void StorageManager::resetTelemetryCursor( ) {
 _cachedLastSent = 0;
 _cursorDirty = false;
 _cursorCoalesceTime = 0;

 LogManager::WdtWindow _wdt(15000);
 enterFlashSafeMode( );
 if (LittleFS.exists(FILE_TCURSOR)) {
 LittleFS.remove(FILE_TCURSOR);
 }
 exitFlashSafeMode( );
 /* TelemetryManager._pendingDirty is set by update( ) post-send; we don't
 * touch it from here to avoid circular dependency. Next tick redoes the
 * count with the zeroed cursor. */
}

void StorageManager::flushCursorIfDirty( ) {
 if (!_cursorDirty) return;
 if (!timeSince(_cursorCoalesceTime, CURSOR_COALESCE_MS)) return;

 /* Touch priority: if user is interacting, cursor stays dirty and flush
 * happens on next call after interaction ends. */
 if (TouchPriority::isActive( )) return;

 _cursorDirty = false;
 LogManager::WdtWindow _wdt(30000); /* context-aware */
 enterFlashSafeMode( );
 watchdog_update( );
 File f = LittleFS.open(FILE_TCURSOR, "w");
 watchdog_update( );
 if (f) {
 f.write((uint8_t*)&_cachedLastSent, sizeof(_cachedLastSent));
 f.close( );
 watchdog_update( );
 }
 exitFlashSafeMode( );
 /* WdtWindow auto-restores */
}

String StorageManager::getStatsReport( ) {
 if (!_isMounted) return String("FS Not Mounted");

 enterFlashReadLock( );
 FSInfo info; LittleFS.info(info);
 exitFlashReadLock( );

 size_t available = (size_t)(info.totalBytes - info.usedBytes);
 String s = "=== Storage Stats ===\n";
 s += "Total: " + String((unsigned long)info.totalBytes) + " B\n";
 s += "Used: " + String((unsigned long)info.usedBytes) + " B\n";
 s += "Free: " + String((unsigned long)available) + " B";
 return s;
}

uint32_t StorageManager::getLastRecordedTimestamp( ) {

 enterFlashReadLock( );
 Dir dir = LittleFS.openDir(DIR_HISTORY); String newestFile = "";
 while (dir.next( )) {
 feedWdt( );
 String fn = dir.fileName( );
 if (fn.endsWith(HISTORY_FILE_EXT) && fn > newestFile) newestFile = fn;
 }
 uint32_t lastTs = 0;
 if (newestFile != "") {
 File f = LittleFS.open(String(DIR_HISTORY) + "/" + newestFile, "r");
 if (f) {
 /* v2: full file scan via codec — records are variable, we cannot
 * seek to end. Acceptable cost (once per boot). */
 HistoryCodecState st;
 HistoryFileHeaderV2 hdr;
 if (f.size( ) >= HIST_V2_HEADER_SIZE) {
 f.seek(0);
 if (f.read((uint8_t*)&hdr, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE &&
 memcmp(hdr.magic, HIST_V2_MAGIC, 4) == 0 &&
 (hdr.version == HIST_V2_VERSION || hdr.version == HIST_V3_VERSION)) {
 st.fileVersion = hdr.version;
 historyCodecReset(st);
 uint8_t buf[256];
 size_t filled = 0;
 BinaryHistoryRecord rec;
 while (true) {
 if (filled < HIST_V2_MAX_DELTA_SIZE && f.available( ) > 0) {
 int rN = f.read(buf + filled, sizeof(buf) - filled);
 if (rN > 0) filled += (size_t)rN;
 }
 if (filled == 0) break;
 bool isAnc = (st.recordsSinceAnchor == 0) ||
 (st.recordsSinceAnchor == hdr.anchorPeriod);
 size_t consumed = historyDecodeRecord(buf, filled, st, rec, isAnc);
 if (consumed == 0) break;
 lastTs = rec.epoch;
 memmove(buf, buf + consumed, filled - consumed);
 filled -= consumed;
 }
 }
 }
 f.close( );
 }
 }
 exitFlashReadLock( );
 return lastTs;
}


/* ─────────────────────────────────────────────────────────────────────────── */
/* getHistoryDaysMask( ) — bitmask of days with history file */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief Returns bitmask of days in a month that have a .bin file.
 *
 * Bit N set = day N has data (bit 1 = day 1, bit 31 = day 31).
 * Used by the calendar screen on the TFT display.
 *
 * @param year Year (ex: 2026).
 * @param month Month (1-12).
 * @return 32-bit bitmask with available days.
 */
uint32_t StorageManager::getHistoryDaysMask(int year, int month) {
 uint32_t mask = 0;

 /* Build expected prefix: "YYYYMM" */
 char prefix[8];
 snprintf(prefix, sizeof(prefix), "%04d%02d", year, month);

 enterFlashReadLock( );
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 feedWdt( );
 String fn = dir.fileName( );
 if (!fn.endsWith(HISTORY_FILE_EXT) && !fn.endsWith(HISTORY_V4_FILE_EXT)) continue;

 /* File: "YYYYMMDD.bin" — check month prefix */
 if (fn.length( ) >= 8 && fn.startsWith(prefix)) {
 int day = fn.substring(6, 8).toInt( );
 if (day >= 1 && day <= 31) {
 mask |= (1UL << day);
 }
 }
 }
 exitFlashReadLock( );

 return mask;
}


/* =========================================================================== */
/* PROVISIONAL TIMESTAMP CORRECTION (NTP SYNC) */
/* =========================================================================== */
String StorageManager::getBoardSerialNumber( ) {
 pico_unique_board_id_t board_id; pico_get_unique_board_id(&board_id);
 char hex[17]; snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X%02X%02X", board_id.id[0], board_id.id[1], board_id.id[2], board_id.id[3], board_id.id[4], board_id.id[5], board_id.id[6], board_id.id[7]);
 return String(hex);
}

long StorageManager::getCalibrationVersion(String path) {
 if (!LittleFS.exists(path)) return -1;

 enterFlashReadLock( );
 File f = LittleFS.open(path, "r"); long ver = -1;
 if (f) {
 char lineBuf[64]; size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
 lineBuf[len] = '\0'; String line = String(lineBuf); line.trim( );
 if (line.startsWith("VERSION,")) ver = line.substring(8).toInt( );
 f.close( );
 }
 exitFlashReadLock( );
 return ver;
}

bool StorageManager::getCalibrationData(const uint8_t* rom, String& outId, float& outOffset, String& outName) {
 char romStr[17]; snprintf(romStr, sizeof(romStr), "%02X%02X%02X%02X%02X%02X%02X%02X", rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
 if (!LittleFS.exists("/calib.csv")) return false;


 enterFlashReadLock( );
 File f = LittleFS.open("/calib.csv", "r"); bool found = false;
 if (f) {
 char lineBuf[256];
 while (f.available( )) {
 feedWdt( );
 size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
 if (len == 0) continue;
 lineBuf[len] = '\0'; if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
 String line = String(lineBuf); line.trim( );
 if (line.length( ) >= 16 && line.substring(0, 16).equalsIgnoreCase(romStr)) {
 int p1 = line.indexOf(','); int p2 = line.indexOf(',', p1 + 1); int p3 = line.indexOf(',', p2 + 1);
 if (p1 > 0 && p2 > p1) {
 outId = line.substring(p1 + 1, p2);
 if (p3 > p2) { outOffset = parseFloat(line.substring(p2 + 1, p3).c_str( )); outName = line.substring(p3 + 1); outName.replace("\"", ""); }
 else { outOffset = parseFloat(line.substring(p2 + 1).c_str( )); outName = ""; }
 found = true; break;
 }
 }
 }
 f.close( );
 }
 exitFlashReadLock( );
 return found;
}

/* Ambient (DHT22) lookup in calib.csv. Key = picoUID 16 hex
 * (same format as DS18B20 ROM). Discriminator between the 2 ambient
 * lines is in the ID field prefix (column 2): `t<id>` for temperature,
 * `u<id>` for humidity. outId is returned WITHOUT the prefix (ex: line
 * `<picoUID>,t01,-0.4,Sala` → outId = "01"). */
bool StorageManager::getCalibrationDataAmbient(char prefix, String& outId, float& outOffset, String& outName) {
 if (prefix != 't' && prefix != 'u') return false;
 String picoUID = getBoardSerialNumber( ); /* 16 hex without separator */
 if (!LittleFS.exists("/calib.csv")) return false;

 enterFlashReadLock( );
 File f = LittleFS.open("/calib.csv", "r"); bool found = false;
 if (f) {
 char lineBuf[256];
 while (f.available( )) {
 feedWdt( );
 size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
 if (len == 0) continue;
 lineBuf[len] = '\0'; if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
 String line = String(lineBuf); line.trim( );
 if (line.length( ) < 16) continue;
 if (!line.substring(0, 16).equalsIgnoreCase(picoUID)) continue;
 int p1 = line.indexOf(','); int p2 = line.indexOf(',', p1 + 1); int p3 = line.indexOf(',', p2 + 1);
 if (p1 <= 0 || p2 <= p1) continue;
 String idCol = line.substring(p1 + 1, p2); idCol.trim( );
 if (idCol.length( ) == 0 || idCol.charAt(0) != prefix) continue;
 outId = idCol.substring(1); /* strip prefix */
 if (p3 > p2) {
 outOffset = parseFloat(line.substring(p2 + 1, p3).c_str( ));
 outName = line.substring(p3 + 1); outName.replace("\"", "");
 } else {
 outOffset = parseFloat(line.substring(p2 + 1).c_str( ));
 outName = "";
 }
 found = true; break;
 }
 f.close( );
 }
 exitFlashReadLock( );
 return found;
}

bool StorageManager::processCalibrationUpload( ) {
 if (!LittleFS.exists("/calib.tmp")) return false;
 long currentVer = getCalibrationVersion("/calib.csv");
 long newVer = getCalibrationVersion("/calib.tmp");

 enterFlashSafeMode( );
 if (newVer > currentVer) {
 LittleFS.remove("/calib.csv"); LittleFS.rename("/calib.tmp", "/calib.csv");
 exitFlashSafeMode( ); return true;
 } else {
 LittleFS.remove("/calib.tmp"); exitFlashSafeMode( ); return false;
 }
}

/**
 * @brief Simple SHA256 — returns 64-char hex digest.
 *
 * Mirrors the JavaScript SHA256 behavior of the frontend:
 * each character is treated as 1 byte by its Unicode code point
 * (Latin-1), not by UTF-8 encoding. This is relevant for
 * characters like ç (U+00E7): JS processes as byte 0xE7,
 * but UTF-8 encodes as 0xC3 0xA7 (2 bytes).
 */
String StorageManager::sha256Hex(const String& input) {
 br_sha256_context ctx;
 br_sha256_init(&ctx);

 /* Decode UTF-8 → code points → Latin-1 bytes (like JS charCodeAt). */
 const uint8_t* s = (const uint8_t*)input.c_str( );
 size_t len = input.length( );
 for (size_t i = 0; i < len; ) {
 uint8_t c = s[i];
 uint8_t byte;
 if (c < 0x80) {
 byte = c;
 i += 1;
 } else if ((c & 0xE0) == 0xC0 && i + 1 < len) {
 /* 2-byte UTF-8 → code point 0x80–0x7FF (Latin-1 covers up to 0xFF) */
 byte = (uint8_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F));
 i += 2;
 } else {
 /* 3+ byte UTF-8 or invalid byte → skip (JS returns undefined for >255) */
 i += (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 1;
 continue;
 }
 br_sha256_update(&ctx, &byte, 1);
 }

 unsigned char hash[32];
 br_sha256_out(&ctx, hash);
 char hex[65];
 for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", hash[i]);
 hex[64] = '\0';
 return String(hex);
}

/**
 * @brief Core HMAC-SHA256 password hashing — explicit parameters.
 *
 * @param username Username (used as salt if salt==nullptr).
 * @param plainPassword SHA-256 hex of password (64 chars, from frontend).
 * @param salt Buffer with salt bytes (nullptr → derive from username).
 * @param saltLen Salt length in bytes.
 * @param rounds Number of HMAC-SHA256 iterations.
 * @param outputBytes Hash bytes to emit in hex (15 → 30 chars, 16 → 32 chars).
 * @return Hex hash string (outputBytes*2 chars).
 */
static String hashPasswordCore(const String& username, const String& plainPassword,
 const uint8_t* salt, size_t saltLen,
 uint16_t rounds, int outputBytes) {
 String pepper = StorageManager::getBoardSerialNumber( );
 String keyData = plainPassword + pepper;
 br_hmac_key_context kc; br_hmac_context ctx;
 br_hmac_key_init(&kc, &br_sha256_vtable, keyData.c_str( ), keyData.length( ));
 unsigned char currentHash[32];
 br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, salt, saltLen); br_hmac_out(&ctx, currentHash);
 for (int r = 0; r < rounds; r++) {
 if (r % 50 == 0) watchdog_update( );
 br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, currentHash, 32); br_hmac_out(&ctx, currentHash);
 }

 char hashHex[65];
 for (int i = 0; i < outputBytes; i++) snprintf(hashHex + (i * 2), 3, "%02x", currentHash[i]);
 hashHex[outputBytes * 2] = '\0';
 return String(hashHex);
}

String StorageManager::hashPassword(const String& username, const String& plainPassword) {
 String saltStr = username; saltStr.toLowerCase( );
 return hashPasswordCore(username, plainPassword,
 (const uint8_t*)saltStr.c_str( ), saltStr.length( ),
 PASSWORD_HMAC_ROUNDS, 16);
}

String StorageManager::hashPasswordLegacy(const String& username, const String& plainPassword) {
 String saltStr = username; saltStr.toLowerCase( );
 return hashPasswordCore(username, plainPassword,
 (const uint8_t*)saltStr.c_str( ), saltStr.length( ),
 2500, 15);
}

String StorageManager::hashPasswordV1(const String& username, const String& plainPassword,
 const uint8_t* userSalt) {
 return hashPasswordCore(username, plainPassword,
 userSalt, 8, PASSWORD_HMAC_ROUNDS, 16);
}

void StorageManager::generateSalt(uint8_t* buf) {
 for (int i = 0; i < 8; i += 4) {
 uint32_t r = rp2040.hwrand32( );
 memcpy(buf + i, &r, (8 - i >= 4) ? 4 : (8 - i));
 }
}

/* ============================================================================
 * V4 HISTORY — write, scan, build schema
 * ============================================================================ */

void StorageManager::ensureV4Schema( ) {
 if (_histV4CodecValid) return;  /* already initialized */
 if (!_isMounted) return;

 LogManager::TraceScope _tr(0, MOD_HIST_FLASH);
 LogManager::WdtWindow _wdt(30000);

 String path = getHistoryFileNameV4( );
 _currentLogFileName = path;

 /* Build schema from current config */
 HistV4SensorDef s[HIST_V4_MAX_SENSORS];
 HistV4MeasureDef m[HIST_V4_MAX_MEASUREMENTS];
 uint8_t pool[HIST_V4_MAX_STRPOOL];
 uint8_t sc = 0, mc = 0, sp = 0;
 if (!buildMeasureSchema(s, sc, m, mc, pool, sp)) {
  LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "schema_empty");
  return;
 }

 static uint8_t hdrBuf[2048];
 size_t hdrLen = histV4WriteHeaderBuf(hdrBuf, sizeof(hdrBuf), s, sc, m, mc, pool, sp);
 if (hdrLen == 0) return;

 FLASH_OP({
  File f = LittleFS.open(path, "w");
  if (f) { f.write(hdrBuf, hdrLen); f.close(); }
 });

 /* Read header back to populate codec state */
 histV4Reset(_histV4State);
 FLASH_OP({
  File f = LittleFS.open(path, "r");
  if (f) {
   static uint8_t rdBuf[HIST_V4_MAX_HEADER];
   int n = f.read(rdBuf, sizeof(rdBuf));
   f.close();
   if (n >= (int)HIST_V4_HEADER_FIXED) {
    histV4ReadHeaderBuf(rdBuf, (size_t)n, _histV4State);
   }
  }
 });
 _histV4CodecValid = true;
 LOG_CODE(LOG_INFO, "STO", SYS_OK, 0, "V4 schema bootstrapped");
}

bool StorageManager::writeHistoryEntryV4(const int64_t *values, uint8_t measureCount, uint32_t epoch) {
	if (!_isMounted) return false;

	/* Defensive: reject absurd timestamps */
	{
		const uint32_t EPOCH_MIN = 1700000000UL;
		uint32_t nowEpoch = (uint32_t)time(nullptr);
		if (epoch < EPOCH_MIN) return false;
		if (nowEpoch > EPOCH_MIN && epoch > nowEpoch + 86400UL) return false;
	}

	/* Touch priority: buffer and return */
	if (TouchPriority::isActive()) {
		if (measureCount <= HIST_V4_MAX_MEASUREMENTS) {
			memcpy(_pendingValuesV4, values, measureCount * sizeof(int64_t));
			_pendingMeasureCountV4 = measureCount;
			_pendingEpochV4 = epoch;
			_pendingHistV4Valid = true;
		}
		return true;
	}

	/* Flush pending before writing current */
	if (_pendingHistV4Valid) {
		_pendingHistV4Valid = false;
		writeHistoryEntryFlashV4(_pendingValuesV4, _pendingMeasureCountV4, _pendingEpochV4);
	}

	return writeHistoryEntryFlashV4(values, measureCount, epoch);
}

bool StorageManager::writeHistoryEntryFlashV4(const int64_t *values, uint8_t measureCount, uint32_t epoch) {
	if (!_isMounted) return false;

	String path = getHistoryFileNameV4();

	LogManager::TraceScope _tr(0, MOD_HIST_FLASH);
	LogManager::WdtWindow _wdt(30000);

	/* Chunk 1: enforce on rollover */
	if (path != _currentLogFileName) {
		FLASH_OP(enforceStorageLimit());
		_currentLogFileName = path;
		_histV4CodecValid = false;
	}

	/* Chunk 2: prepare V4 state (boot or rollover) — split into simple ops */
	if (!_histV4CodecValid) {
		bool needCreate = false;

		/* 2a: open and check existing file */
		FLASH_OP({
			if (LittleFS.exists(path)) {
				File f = LittleFS.open(path, "r+");
				if (f) {
					if (!scanHistoryFileV4(f, _histV4State)) {
						f.close();
						LittleFS.remove(path);
						needCreate = true;
					} else {
						f.close();
					}
				}
			} else {
				needCreate = true;
			}
		});

		/* 2b: create new file with schema header if needed */
		if (needCreate) {
			/* Static buffers — avoid 4KB+ stack usage in this function
			 * (hdrBuf 2KB + sensor/measure arrays + pool = ~3.5KB).
			 * RP2040 stack is ~4KB; this would overflow with call chain. */
			static uint8_t hdrBuf[2048]; size_t hdrLen = 0;
			{
				HistV4SensorDef s[HIST_V4_MAX_SENSORS];
				HistV4MeasureDef m[HIST_V4_MAX_MEASUREMENTS];
				uint8_t pool[HIST_V4_MAX_STRPOOL];
				uint8_t sc = 0, mc = 0, sp = 0;
				if (buildMeasureSchema(s, sc, m, mc, pool, sp)) {
					hdrLen = histV4WriteHeaderBuf(hdrBuf, sizeof(hdrBuf),
						s, sc, m, mc, pool, sp);
				}
			}
			FLASH_OP({
				File f = LittleFS.open(path, "w");
				if (f) {
					if (hdrLen > 0) f.write(hdrBuf, hdrLen);
					f.close();
				}
			});

			/* 2c: re-read header to populate state schema */
			histV4Reset(_histV4State);
			FLASH_OP({
				File f = LittleFS.open(path, "r");
				if (f) {
					static uint8_t rdBuf[HIST_V4_MAX_HEADER];
					int n = f.read(rdBuf, sizeof(rdBuf));
					f.close();
					if (n >= (int)HIST_V4_HEADER_FIXED) {
						histV4ReadHeaderBuf(rdBuf, (size_t)n, _histV4State);
					}
				}
			});
		}
		_histV4CodecValid = true;
	}

	/* Chunk 3: encode + append */
	uint8_t encBuf[HIST_V4_MAX_DELTA];
	bool wasAnchor = false;
	size_t encLen = histV4Encode(values, measureCount, _histV4State,
	                             encBuf, sizeof(encBuf), epoch, &wasAnchor);
	if (encLen == 0) {
		LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "v4_encode_fail");
		return false;
	}

	bool ok = false;
	FLASH_OP({
		File f = LittleFS.open(path, "a");
		if (f) { f.write(encBuf, encLen); f.close(); ok = true; }
	});

	if (ok) { _storageDirty = true; return true; }

	/* Fallback */
	LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "v4_fallback");
	_storageDirty = true;
	FLASH_OP(enforceStorageLimit());
	FLASH_OP({
		File f = LittleFS.open(path, "a");
		if (f) { f.write(encBuf, encLen); f.close(); ok = true; }
	});
	return ok;
}

bool StorageManager::scanHistoryFileV4(File &f, HistV4State &state) {
	histV4Reset(state);
	if (f.size() < HIST_V4_HEADER_FIXED) return false;
	f.seek(0);

	/* Read entire header into buffer, then parse */
	uint8_t hdrBuf[HIST_V4_MAX_HEADER];
	size_t hdrSize = f.size();
	if (hdrSize > HIST_V4_MAX_HEADER) hdrSize = HIST_V4_MAX_HEADER;
	if (f.read(hdrBuf, hdrSize) < HIST_V4_HEADER_FIXED) return false;
	if (histV4ReadHeaderBuf(hdrBuf, hdrSize, state) == 0) return false;

	/* Scan all records to rebuild codec state */
	uint8_t buf[HIST_V4_READ_BUF];
	size_t filled = 0;
	size_t goodPos = hdrSize; /* bytes consumed by header */
	histV4Reset(state);
	histV4ReadHeaderBuf(hdrBuf, hdrSize, state);
	int64_t values[HIST_V4_MAX_MEASUREMENTS];
	uint32_t epoch;

	while (true) {
		if (filled < state.anchorByteSize && f.available() > 0) {
			size_t toRead = HIST_V4_READ_BUF - filled;
			if (toRead > (size_t)f.available()) toRead = f.available();
			int r = f.read(buf + filled, toRead);
			if (r > 0) filled += (size_t)r;
		}
		if (filled == 0) break;

		bool isAnchor = (state.recordsSinceAnchor == 0 ||
		                 state.recordsSinceAnchor >= state.anchorPeriod);
		if (!state.initialized) isAnchor = true;

		size_t consumed = histV4DecodeNext(buf, filled, state, values, &epoch);
		if (consumed == 0) break; /* truncated / corrupt tail */

		goodPos += consumed;
		memmove(buf, buf + consumed, filled - consumed);
		filled -= consumed;
	}

	f.seek(goodPos);
	return true;
}
