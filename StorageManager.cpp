/**
 * @file    StorageManager.cpp
 * @brief   Implementation of StorageManager — config I/O, history logging, and flash locks.
 * @details Implements atomic config save (tmp→rename), storage limit enforcement
 * with budget-limited cleanup, provisional timestamp correction across
 * history files, HMAC-SHA256 password hashing with board serial pepper,
 * and calibration CSV parsing.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "StorageManager.h"
#include "TouchPriority.h"
#include <time.h>
#include <algorithm>
#include "LogManager.h"
#include "MetricsManager.h"
#include "pico/unique_id.h"
#include <hardware/watchdog.h>
#include <stdio.h>
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_hmac.h>

const uint32_t CONFIG_MAGIC = 0xCAFEBABE;

/* F13.4/BUG-003 — chunk de flash safe mode, file-scope.
 * Expansão inline: trace MOD_CORE1_LOCK no enter, enterFlashSafeMode,
 * watchdog feed, BLOCK, watchdog feed, exitFlashSafeMode. Usada em
 * saveConfiguration (7 sites) e writeHistoryEntryFlash (até 4 sites).
 * Entre chunks, Core 1 sai do multicore_lockout e renderiza 1 frame. */
#define FLASH_OP(BLOCK) do { \
    { LogManager::TraceScope _trLock(0, MOD_CORE1_LOCK); \
      enterFlashSafeMode(); } \
    watchdog_update(); \
    BLOCK; \
    watchdog_update(); \
    exitFlashSafeMode(); \
} while (0)

const uint16_t CONFIG_VERSION = 15;

/* -------------------------------------------------------------------------- */
/*  Layout legado de UserAccount (v14 e anteriores) — usado APENAS pelo      */
/*  migrador loadAndMigrateV14. Não confundir com UserAccount (v15, 62 B).   */
/* -------------------------------------------------------------------------- */
struct __attribute__((packed)) UserAccount_v14 {
    bool active;
    char username[16];
    char password[32];
    uint16_t permissions;
    bool mustChangePassword;
};
static_assert(sizeof(UserAccount_v14) == 52, "UserAccount_v14 deve ter 52 bytes");

/* #5: keystream derivation via SHA-256(chip_id + domain + counter).
 * Gera keystream de tamanho arbitrário iterando o contador e expandindo
 * o hash. Equivalente a HKDF-Expand em construção e segurança. */
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

/* Aplica XOR in-place em um buffer com keystream derivado do par (chip_id, domain).
 * Como XOR é involutivo, a mesma função encripta e descriptografa. */
static void xorWithDerivedKey(uint8_t* buf, size_t len, const char* domain) {
    uint8_t keystream[64];   /* tamanho máximo atual é telApiKey[64] */
    if (len > sizeof(keystream)) return;   /* proteção contra overrun futuro */
    deriveKeystream(keystream, len, domain);
    for (size_t i = 0; i < len; i++) buf[i] ^= keystream[i];
}

void StorageManager::obfuscateSensitiveFields(SystemConfig& cfg) {
    xorWithDerivedKey((uint8_t*)cfg.wifiPass,  sizeof(cfg.wifiPass),  "wifi");
    xorWithDerivedKey((uint8_t*)cfg.mqttPass,  sizeof(cfg.mqttPass),  "mqtt");
    xorWithDerivedKey((uint8_t*)cfg.telApiKey, sizeof(cfg.telApiKey), "telapi");
}

/* Tamanhos de blob por versão histórica, derivados do sizeof(SystemConfig)
 * atual (v15) subtraindo os deltas conhecidos. Usados para dispatch baseado
 * em file size em attemptLoad.
 *
 *  v15: sizeof(SystemConfig)                                     — atual
 *  v14: sizeof(SystemConfig) - MAX_USERS*(62-52)                 — user growth
 *  v13: mesmo tamanho de v14 (só muda obfuscação, não layout)
 *  v12: v14 - (64-24)                                            — reserved growth
 */
static constexpr size_t CONFIG_V14_USER_DELTA =
    MAX_USERS * (sizeof(UserAccount) - sizeof(UserAccount_v14));   /* 50 bytes */
static constexpr size_t CONFIG_V14_BLOB_SIZE =
    sizeof(SystemConfig) - CONFIG_V14_USER_DELTA;
static constexpr size_t CONFIG_V12_BLOB_SIZE =
    CONFIG_V14_BLOB_SIZE - (64 - CONFIG_V12_RESERVED_SIZE);

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
    /* F-LOCKOUT-STUCK: se estamos dentro de saveConfiguration com quiet mode
     * ativo, Core 1 já está congelado em loop RAM-only — pular o lockout
     * IRQ-based para evitar stucks/cascatas. */
    if (_inBigSave) return;
    if (_lockCb) _lockCb(true);
}


void StorageManager::exitFlashSafeMode() {
    if (_inBigSave) return;
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
    LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_FORMAT, 0, TRL("Formatting Flash FS...", "Formatando FS do flash..."));
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

    /* SEC-003/F12.3: gera senha admin random em vez de "admin" hardcoded.
     * Hash SHA-256 de "admin" (`8c6976e5...a918`) é público em rainbow tables,
     * expondo janela entre boot em factory e troca obrigatória de senha.
     * Plaintext vai pra RAM apenas; Serial+display mostram pra quem tem
     * acesso físico; `mustChangePassword=true` força troca no 1º login web. */
    generateInitialAdminPassword(_initialAdminPassword, sizeof(_initialAdminPassword));

    /* Frontend JS envia SHA256(plaintext) como `pass`, então o hash persistido
     * deve ser `hashPassword(user, SHA256(plaintext))`. */
    String preHash = sha256Hex(String(_initialAdminPassword));
    String defaultAdminHash = hashPassword("admin", preHash);
    safeCopy(_currentConfig.users[0].password, defaultAdminHash.c_str(), sizeof(_currentConfig.users[0].password));
    _currentConfig.users[0].permissions = PERM_FULL_ADMIN;
    _currentConfig.users[0].mustChangePassword = true;

    _currentConfig.users[1].active = true;
    safeCopy(_currentConfig.users[1].username, "viewer", sizeof(_currentConfig.users[1].username));
    String defaultViewerHash = hashPassword("viewer", "0b58331da2913b41e21b7b04938632e1858a729e28cf6914b4334380f339b6f1");
    safeCopy(_currentConfig.users[1].password, defaultViewerHash.c_str(), sizeof(_currentConfig.users[1].password));
    _currentConfig.users[1].permissions = (PERM_DASHBOARD | PERM_HISTORY);
    _currentConfig.users[1].mustChangePassword = true;

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
    _currentConfig.ds18Resolution = 12;
    _currentConfig.themeIndex = 0;

    safeCopy(_currentConfig.displayPin, "1234", sizeof(_currentConfig.displayPin));
    /* SEC-004/F12.4: força troca do PIN padrão "1234" no primeiro acesso ao
     * menu de config. Overlay em reserved[26..27] — limpo quando usuário
     * salva um PIN != "1234". Setado aqui (loadDefaults = factory reset). */
    setMustChangePin();
    _currentConfig.displayLang = LANG_PT;

    /* F-NET-TIME.1: inicializa overlay NetworkTimeData via helper (magic
     * ainda 0 após o memset inicial → helper popula defaults). */
    (void)ensureNetworkTimeOverlay();

    for (int i = 0; i < MAX_SENSORS; i++) {
        _currentConfig.sensors[i].active = false;
        _currentConfig.sensors[i].gpio = 0;
        memset(_currentConfig.sensors[i].rom, 0, 8);
        safeCopy(_currentConfig.sensors[i].hwId, "", sizeof(_currentConfig.sensors[i].hwId));
        safeCopy(_currentConfig.sensors[i].friendlyName, "Empty Slot", sizeof(_currentConfig.sensors[i].friendlyName));
        _currentConfig.sensors[i].tempMin = 0.0f;
        _currentConfig.sensors[i].tempMax = 40.0f;
        _currentConfig.sensors[i].humMin = 20.0f;
        _currentConfig.sensors[i].humMax = 80.0f;
        _currentConfig.sensors[i].alarmsActive = true;
    }

    _currentConfig.ambientSensor.active = true;
    _currentConfig.ambientSensor.gpio = 10;
    memset(_currentConfig.ambientSensor.rom, 0, 8);
    safeCopy(_currentConfig.ambientSensor.hwId, "AMB", sizeof(_currentConfig.ambientSensor.hwId));
    safeCopy(_currentConfig.ambientSensor.friendlyName, "Ambiente Central", sizeof(_currentConfig.ambientSensor.friendlyName));
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

/* Lê config no formato atual. Aceita v13 (plaintext, pré-#5) ou v14 (fields
 * sensíveis criptografados via XOR+KDF). Decripta in-place quando v14.
 * O caller (attemptLoad) é responsável por detectar v13 e marcar _didMigrate
 * para que seja re-salvo como v14 encryptado. */
bool StorageManager::loadCurrentBlob(File& f, SystemConfig& outCfg) {
    size_t bytesRead = f.read((uint8_t*)&outCfg, sizeof(SystemConfig));
    uint32_t readCrc = 0;
    size_t crcRead = f.read((uint8_t*)&readCrc, sizeof(readCrc));
    if (bytesRead != sizeof(SystemConfig)) return false;
    if (outCfg.magic != CONFIG_MAGIC) return false;
    /* v15 é o único formato nativo aceito aqui — v13/v14 caem em loadAndMigrateV14
     * (file size menor por UserAccount[52] em vez de [62]). */
    if (outCfg.version != CONFIG_VERSION) return false;
    if (crcRead == sizeof(readCrc)) {
        uint32_t calcCrc = calculateCRC32((uint8_t*)&outCfg, sizeof(SystemConfig));
        if (calcCrc != readCrc) return false;
    }
    /* v15 sempre grava com campos sensíveis obfuscated (XOR keystream). */
    obfuscateSensitiveFields(outCfg);
    return true;
}

/* Lê config no formato legado v12 (reserved[24], UserAccount_v14[52]) e migra
 * para v15 (reserved[64], UserAccount[62]). Campos sensíveis em v12 são
 * plaintext — sem deobfuscação necessária; serão criptografados na primeira
 * saveConfiguration pós-migração.
 *
 * Em F15.2.a: além da expansão de reserved[24]→[64] (delta 40 ao final),
 * também expande UserAccount de 52→62 no meio do blob (delta 50) — requer
 * leitura em buffer e reconstrução por seção. */
bool StorageManager::loadAndMigrateV12(File& f, SystemConfig& outCfg) {
    constexpr size_t HEAD_SIZE           = offsetof(SystemConfig, users);
    constexpr size_t V14_USER_BLOCK      = MAX_USERS * sizeof(UserAccount_v14);
    constexpr size_t V15_USER_BLOCK      = MAX_USERS * sizeof(UserAccount);
    constexpr size_t MIDDLE_SIZE         = offsetof(SystemConfig, reserved) -
                                           (HEAD_SIZE + V15_USER_BLOCK);
    constexpr size_t V12_MIDDLE_START    = HEAD_SIZE + V14_USER_BLOCK;
    constexpr size_t V12_RESERVED_START  = V12_MIDDLE_START + MIDDLE_SIZE;
    constexpr size_t V12_RESERVED_BYTES  = CONFIG_V12_RESERVED_SIZE;   /* 24 */

    uint8_t buf[CONFIG_V12_BLOB_SIZE];
    uint32_t readCrc = 0;

    size_t bytesRead = f.read(buf, CONFIG_V12_BLOB_SIZE);
    size_t crcRead   = f.read((uint8_t*)&readCrc, sizeof(readCrc));

    if (bytesRead != CONFIG_V12_BLOB_SIZE) return false;

    uint32_t fileMagic = 0;
    uint16_t fileVersion = 0;
    memcpy(&fileMagic,   buf + 0,                 sizeof(fileMagic));
    memcpy(&fileVersion, buf + sizeof(fileMagic), sizeof(fileVersion));
    if (fileMagic != CONFIG_MAGIC || fileVersion != 12) return false;

    if (crcRead == sizeof(readCrc)) {
        uint32_t calcCrc = calculateCRC32(buf, CONFIG_V12_BLOB_SIZE);
        if (calcCrc != readCrc) return false;
    }

    memset(&outCfg, 0, sizeof(SystemConfig));

    /* Head (magic..useHttps). */
    memcpy(&outCfg, buf, HEAD_SIZE);

    /* users_v14 → users_v15 (salt={0}, hashVersion=0 → modo legado). */
    for (size_t i = 0; i < MAX_USERS; i++) {
        UserAccount_v14 u;
        memcpy(&u, buf + HEAD_SIZE + i * sizeof(UserAccount_v14), sizeof(UserAccount_v14));
        outCfg.users[i].active             = u.active;
        memcpy(outCfg.users[i].username, u.username, sizeof(u.username));
        memcpy(outCfg.users[i].password, u.password, sizeof(u.password));
        outCfg.users[i].password[sizeof(u.password)] = '\0';
        outCfg.users[i].permissions        = u.permissions;
        outCfg.users[i].mustChangePassword = u.mustChangePassword;
        memset(outCfg.users[i].salt, 0, sizeof(outCfg.users[i].salt));
        outCfg.users[i].hashVersion = 0;
    }

    /* Middle (telServer..ntpServer) — layout inalterado, só shifted. */
    memcpy(((uint8_t*)&outCfg) + HEAD_SIZE + V15_USER_BLOCK,
           buf + V12_MIDDLE_START,
           MIDDLE_SIZE);

    /* reserved[0..23] do v12; reserved[24..63] permanece zero do memset. */
    memcpy(outCfg.reserved, buf + V12_RESERVED_START, V12_RESERVED_BYTES);

    outCfg.version = CONFIG_VERSION;
    return true;
}

/* F15.2.a: lê config em formato v13 (plaintext) ou v14 (obfuscated) — ambos
 * com UserAccount_v14[52] — e promove para o schema v15 (UserAccount[62] com
 * salt={0}/hashVersion=0, indicando modo legado). O hash stored continua
 * válido e verificável via hashPassword() (algoritmo inalterado em F15.2.a).
 *
 * Layout v14: [head (até users)] [5 × UserAccount_v14] [tail (telServer..reserved)]
 * Layout v15: [head (igual)]     [5 × UserAccount_v15] [tail shifted por +50]
 *
 * srcVersion (out): versão original lida do arquivo (13 ou 14) para telemetria. */
bool StorageManager::loadAndMigrateV14(File& f, SystemConfig& outCfg, uint16_t& srcVersion) {
    constexpr size_t HEAD_SIZE        = offsetof(SystemConfig, users);
    constexpr size_t V14_USER_BLOCK   = MAX_USERS * sizeof(UserAccount_v14);
    constexpr size_t V15_USER_BLOCK   = MAX_USERS * sizeof(UserAccount);
    constexpr size_t V14_TAIL_OFFSET  = HEAD_SIZE + V14_USER_BLOCK;
    constexpr size_t V15_TAIL_OFFSET  = HEAD_SIZE + V15_USER_BLOCK;
    constexpr size_t TAIL_SIZE        = CONFIG_V14_BLOB_SIZE - V14_TAIL_OFFSET;

    uint8_t buf[CONFIG_V14_BLOB_SIZE];
    uint32_t readCrc = 0;

    size_t bytesRead = f.read(buf, CONFIG_V14_BLOB_SIZE);
    size_t crcRead   = f.read((uint8_t*)&readCrc, sizeof(readCrc));

    if (bytesRead != CONFIG_V14_BLOB_SIZE) return false;

    /* Magic + version via aliasing (SystemConfig começa com uint32_t magic
     * + uint16_t version em todas as versões desde v12). */
    uint32_t fileMagic = 0;
    uint16_t fileVersion = 0;
    memcpy(&fileMagic,   buf + 0,                  sizeof(fileMagic));
    memcpy(&fileVersion, buf + sizeof(fileMagic),  sizeof(fileVersion));

    if (fileMagic != CONFIG_MAGIC) return false;
    if (fileVersion != 13 && fileVersion != 14) return false;

    /* CRC32 calculado sobre o blob v14 gravado. */
    if (crcRead == sizeof(readCrc)) {
        uint32_t calcCrc = calculateCRC32(buf, CONFIG_V14_BLOB_SIZE);
        if (calcCrc != readCrc) return false;
    }

    /* Zera outCfg e copia head (campos antes de users[]). */
    memset(&outCfg, 0, sizeof(SystemConfig));
    memcpy(&outCfg, buf, HEAD_SIZE);

    /* Expande cada UserAccount_v14 → UserAccount v15. */
    for (size_t i = 0; i < MAX_USERS; i++) {
        UserAccount_v14 u14;
        memcpy(&u14, buf + HEAD_SIZE + i * sizeof(UserAccount_v14), sizeof(UserAccount_v14));
        outCfg.users[i].active             = u14.active;
        memcpy(outCfg.users[i].username, u14.username, sizeof(u14.username));
        memcpy(outCfg.users[i].password, u14.password, sizeof(u14.password));
        outCfg.users[i].password[sizeof(u14.password)] = '\0';   /* null-term no novo [33] */
        outCfg.users[i].permissions        = u14.permissions;
        outCfg.users[i].mustChangePassword = u14.mustChangePassword;
        memset(outCfg.users[i].salt, 0, sizeof(outCfg.users[i].salt));
        outCfg.users[i].hashVersion = 0;   /* legacy */
    }

    /* Copia tail (campos após users[]) para offset shifted em outCfg. */
    memcpy(((uint8_t*)&outCfg) + V15_TAIL_OFFSET,
           buf + V14_TAIL_OFFSET,
           TAIL_SIZE);

    /* v14 tem campos sensíveis obfuscated — desofusca. v13 é plaintext. */
    if (fileVersion == 14) {
        obfuscateSensitiveFields(outCfg);
    }

    srcVersion = fileVersion;
    outCfg.version = CONFIG_VERSION;   /* promove para v15 */
    return true;
}

bool StorageManager::attemptLoad(const char* path, SystemConfig& outCfg) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    size_t fileSize = f.size();

    /* Formato atual (v15 — UserAccount[62], obfuscated). */
    if (fileSize == sizeof(SystemConfig) + sizeof(uint32_t)) {
        bool ok = loadCurrentBlob(f, outCfg);
        f.close();
        return ok;
    }

    /* v13 plaintext ou v14 obfuscated (mesmo tamanho, layout com
     * UserAccount_v14[52]) → migra para v15. */
    if (fileSize == CONFIG_V14_BLOB_SIZE + sizeof(uint32_t)) {
        uint16_t srcVersion = 0;
        bool ok = loadAndMigrateV14(f, outCfg, srcVersion);
        f.close();
        if (ok) {
            _didMigrate = true;
            _migrationFromVersion = srcVersion;   /* 13 ou 14 */
        }
        return ok;
    }

    /* v12 (pré-expansão reserved[]) → migra via path dedicado. */
    if (fileSize == CONFIG_V12_BLOB_SIZE + sizeof(uint32_t)) {
        bool ok = loadAndMigrateV12(f, outCfg);
        f.close();
        if (ok) { _didMigrate = true; _migrationFromVersion = 12; }
        return ok;
    }

    f.close();
    return false;
}

bool StorageManager::loadConfiguration() {

    _didMigrate = false;

    enterFlashReadLock();
    SystemConfig tempConfig;
    bool loaded = false;
    bool fromBackup = false;
    if (LittleFS.exists(FILE_CONFIG) && attemptLoad(FILE_CONFIG, tempConfig)) {
        _currentConfig = tempConfig;
        loaded = true;
    } else if (LittleFS.exists(FILE_BACKUP) && attemptLoad(FILE_BACKUP, tempConfig)) {
        _currentConfig = tempConfig;
        loaded = true;
        fromBackup = true;
    }
    exitFlashReadLock();

    if (!loaded) {
        loadDefaults();
        return false;
    }

    /* SEC-003/F12.3: config válida foi carregada do flash — o random gerado
     * pelo constructor (via loadDefaults) é lixo agora. Zera pra não vazar
     * via logs/display. Se a config carregada AINDA tem mustChangePassword,
     * significa factory anterior que nunca foi trocada; mas o plaintext
     * original perdeu-se em algum reboot — não dá pra recuperar. */
    clearInitialAdminPassword();

    /* F-I18N-TRIM.1: limitamos TOTAL_LANGS de 8 para 2 (EN+PT). Devices que
     * tinham displayLang=ES..ZH no flash caem para PT (default) no próximo
     * boot para evitar out-of-bounds em DICTIONARY[]/LICENSE_TEXT[]. */
    if (_currentConfig.displayLang > LANG_PT) {
        _currentConfig.displayLang = LANG_PT;
    }

    if (fromBackup) {
        LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_RECOVER, 0, TRL("Primary config corrupt, recovered from backup", "Config primaria corrompida, recuperada do backup"));
    }

    /* Migração de schema (v12/v13/v14 → v15): persistir no novo formato antes
     * de entregar o controle. saveConfiguration() marca magic/version,
     * criptografa campos sensíveis (#5) e escreve via tmp→rename atômico. */
    if (_didMigrate) {
        int fromVer = _migrationFromVersion;
        _didMigrate = false;
        _migrationFromVersion = 0;
        LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_MIGRATED, fromVer,
                 TRL("Config schema migrated", "Schema de config migrado"));
        saveConfiguration();
    } else if (fromBackup) {
        saveConfiguration();
    }
    return true;
}

/**
 * @brief Atomic configuration save: write to temp file, then rename.
 * Maintains a backup copy for recovery if the primary is corrupted.
 * CRC32 appended after the binary blob for integrity verification.
 */
bool StorageManager::saveConfiguration() {
    /* U23: instrumentação granular — autópsia distingue se travou aqui
     * vs no LOG_CODE de audit ou no webMgr handler. */
    LogManager::TraceScope _tr(0, MOD_SAVE_CONFIG);

    /* Flush cursor pendente antes de salvar config — garante consistência */
    if (_cursorDirty) { _cursorDirty = false; /* forçar flush abaixo */ }

    _currentConfig.magic = CONFIG_MAGIC;
    _currentConfig.version = CONFIG_VERSION;

    /* SEC-003/F12.3: se admin trocou a senha (mustChangePassword = false),
     * a senha inicial em RAM perdeu valor — zera antes do save pra garantir
     * que não vaze em logs/display/CLI a partir daqui. */
    if (_initialAdminPassword[0] != '\0' && !_currentConfig.users[0].mustChangePassword) {
        clearInitialAdminPassword();
    }

    /*
     * Skip no-op: usuário clica em "Save" várias vezes sem mudar campos →
     * rajada de saves idênticos que pressiona o LittleFS GC sem ganho real.
     * Se o conteúdo em RAM bate com o último salvo, pula a gravação.
     * CON-004: _lastSavedCrc agora é membro privado da classe (antes era
     * static local) — alinha com o padrão dos outros _last* fields.
     */
    uint32_t currentCrc = calculateCRC32((uint8_t*)&_currentConfig, sizeof(SystemConfig));
    if (currentCrc == _lastSavedCrc && _lastSavedCrc != 0) {
        _lastSaveWasNoOp = true;
        MetricsManager::instance().data().configSaves++;  /* ainda conta como save solicitado */
        /* Sem LOG_CODE aqui: log file write pressiona GC em rajadas de clicks. */
        return true;
    }
    _lastSaveWasNoOp = false;

    /*
     * RAII context-aware: estende WDT ctx para 30s (ou mantém outer se maior,
     * ex: telemetria em 120s). Auto-restore em qualquer exit path.
     */
    LogManager::WdtWindow _wdt(30000);

    /* F-LOCKOUT-STUCK: entra em quiet mode cooperativo. Core 1 é sinalizado
     * a congelar num loop RAM-only com IRQs off; Core 0 então faz todas as
     * flash ops sem tentar multicore_lockout IRQ-based a cada chunk (que
     * poderia fazer stuck e cascatear). RAII libera em qualquer return path. */
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
        ~BigSaveGuard() {
            if (entered && cb) cb(false);
            inBigSaveRef = false;
        }
    } _bigSave(_inBigSave, _bigSaveQuietCb);

    /* Com _inBigSave ativo, enterFlashSafeMode/exitFlashSafeMode pulam o
     * lockCb IRQ-based (ver método em StorageManager.cpp), então cada
     * FLASH_OP vira só watchdog_update + BLOCK + watchdog_update — sem
     * pausar Core 1 por op (Core 1 já está congelado no quiet loop). */

    /* Flush cursor para flash (se pendente) */
    if (_cachedLastSent > 0) {
        FLASH_OP({
            File cf = LittleFS.open(FILE_TCURSOR, "w");
            if (cf) { cf.write((uint8_t*)&_cachedLastSent, sizeof(_cachedLastSent)); cf.close(); }
        });
    }

    /* Abre TMP e escreve config criptografado + CRC */
    File f;
    FLASH_OP(f = LittleFS.open(FILE_TMP, "w"));
    if (!f) {
        LOG_CODE(LOG_ERROR, "STO", SYS_STORAGE_FAIL, 0, "open FILE_TMP failed");
        return false;
    }

    /* #5: cópia com campos sensíveis criptografados. _currentConfig em
     * RAM permanece plaintext. CRC sobre a versão criptografada. */
    SystemConfig encBuf = _currentConfig;
    obfuscateSensitiveFields(encBuf);
    uint32_t crc = calculateCRC32((uint8_t*)&encBuf, sizeof(SystemConfig));

    size_t bytesWritten = 0, crcWritten = 0;
    FLASH_OP({
        bytesWritten = f.write((uint8_t*)&encBuf, sizeof(SystemConfig));
        crcWritten = f.write((uint8_t*)&crc, sizeof(crc));
        f.close();
    });

    if (bytesWritten != sizeof(SystemConfig) || crcWritten != sizeof(crc)) {
        FLASH_OP(LittleFS.remove(FILE_TMP));
        LOG_CODE(LOG_ERROR, "STO", SYS_STORAGE_FAIL, (int)bytesWritten, "Config save failed");
        return false;
    }

    /* Rename atômico: backup, rename, rename. Cada um em seu próprio lockout. */
    if (LittleFS.exists(FILE_CONFIG)) {
        FLASH_OP(LittleFS.remove(FILE_BACKUP));
        FLASH_OP(LittleFS.rename(FILE_CONFIG, FILE_BACKUP));
    }
    FLASH_OP(LittleFS.rename(FILE_TMP, FILE_CONFIG));

    MetricsManager::instance().data().configSaves++;
    _lastSavedCrc = currentCrc;  /* Marca conteúdo persistido para skip no-op futuro */
    _lastSaveMs = millis();       /* Timestamp para rate-limit server-side */
    LOG_CODE(LOG_INFO, "STO", SYS_STORAGE_SAVE, (int)(sizeof(SystemConfig)), "");
    return true;
}

bool StorageManager::canSaveNow() const {
    constexpr uint32_t MIN_SAVE_INTERVAL_MS = 1000;
    if (_lastSaveMs == 0) return true;
    return timeSince(_lastSaveMs, MIN_SAVE_INTERVAL_MS);
}

void StorageManager::resetToFactory() { loadDefaults(); saveConfiguration(); }
SystemConfig& StorageManager::getConfig() { return _currentConfig; }

/* SEC-003/F12.3 — helpers para senha admin aleatória inicial. */
void StorageManager::generateInitialAdminPassword(char* outPlain, size_t bufSize) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; /* 32 chars, sem O/0/I/1 */
    const size_t alphabetLen = sizeof(alphabet) - 1;                    /* -1 por '\0' */
    const size_t len = (bufSize > 9) ? 8 : (bufSize > 0 ? bufSize - 1 : 0);
    for (size_t i = 0; i < len; i++) {
        outPlain[i] = alphabet[rp2040.hwrand32() % alphabetLen];
    }
    if (bufSize > 0) outPlain[len] = '\0';
}

bool StorageManager::isFactoryDefaults() const {
    if (!_currentConfig.users[0].active) return false;
    if (strcmp(_currentConfig.users[0].username, "admin") != 0) return false;
    return _currentConfig.users[0].mustChangePassword;
}

void StorageManager::clearInitialAdminPassword() {
    /* volatile para evitar que otimizador elimine memset "morto". */
    volatile char* p = _initialAdminPassword;
    for (size_t i = 0; i < sizeof(_initialAdminPassword); i++) p[i] = 0;
}

/* SEC-004/F12.4 — flag mustChangePin em reserved[26..27]. */
bool StorageManager::mustChangePin() const {
    const SetupFlagsData* sf = reinterpret_cast<const SetupFlagsData*>(
        _currentConfig.reserved + SETUP_FLAGS_OFFSET);
    /* Overlay sem magic = config legada (v13/v14 sem setup flags) — não força
     * troca para não quebrar upgrades de firmware de usuários existentes. */
    if (sf->magic != SETUP_FLAGS_MAGIC) return false;
    return (sf->flags & FLAG_MUST_CHANGE_PIN) != 0;
}

void StorageManager::clearMustChangePin() {
    SetupFlagsData* sf = reinterpret_cast<SetupFlagsData*>(
        _currentConfig.reserved + SETUP_FLAGS_OFFSET);
    if (sf->magic != SETUP_FLAGS_MAGIC) {
        /* Inicializa overlay se ainda não foi. */
        sf->magic = SETUP_FLAGS_MAGIC;
        sf->flags = 0;
    } else {
        sf->flags &= ~FLAG_MUST_CHANGE_PIN;
    }
}

void StorageManager::setMustChangePin() {
    SetupFlagsData* sf = reinterpret_cast<SetupFlagsData*>(
        _currentConfig.reserved + SETUP_FLAGS_OFFSET);
    sf->magic = SETUP_FLAGS_MAGIC;
    sf->flags |= FLAG_MUST_CHANGE_PIN;
}

/* ===========================================================================
 * F-NET-TIME.1 — overlay NetworkTimeData em reserved[28..47]
 * =========================================================================== */

NetworkTimeData* StorageManager::ensureNetworkTimeOverlay() {
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

bool StorageManager::isDnsAuto() const {
    const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
        _currentConfig.reserved + NETTIME_OFFSET);
    if (nt->magic != NETTIME_MAGIC) return true;  /* legado = default AUTO */
    return (nt->flags & FLAG_DNS_AUTO) != 0;
}

void StorageManager::setDnsAuto(bool auto_) {
    NetworkTimeData* nt = ensureNetworkTimeOverlay();
    if (auto_) nt->flags |= FLAG_DNS_AUTO;
    else       nt->flags &= ~FLAG_DNS_AUTO;
}

bool StorageManager::isNtpEnabled() const {
    const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
        _currentConfig.reserved + NETTIME_OFFSET);
    if (nt->magic != NETTIME_MAGIC) return true;
    return (nt->flags & FLAG_NTP_ENABLED) != 0;
}

void StorageManager::setNtpEnabled(bool enabled) {
    NetworkTimeData* nt = ensureNetworkTimeOverlay();
    if (enabled) nt->flags |= FLAG_NTP_ENABLED;
    else         nt->flags &= ~FLAG_NTP_ENABLED;
}

const char* StorageManager::getSecondaryDns() const {
    const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
        _currentConfig.reserved + NETTIME_OFFSET);
    if (nt->magic != NETTIME_MAGIC) return "";
    return nt->dns2;
}

void StorageManager::setSecondaryDns(const char* ip) {
    NetworkTimeData* nt = ensureNetworkTimeOverlay();
    safeCopy(nt->dns2, ip ? ip : "", sizeof(nt->dns2));
}

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
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buff[40]; snprintf(buff, sizeof(buff), "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return String(buff);
}

bool StorageManager::flushPendingHist() {
    if (!_isMounted || !_pendingHistValid) return false;
    BinaryHistoryRecord rec = _pendingHistRec;
    _pendingHistValid = false;
    return writeHistoryEntryFlash(rec);
}

bool StorageManager::writeHistoryEntry(const BinaryHistoryRecord& rec) {
    if (!_isMounted) return false;

    /* Touch priority: se user está interagindo, bufferiza e retorna. Só o
     * record mais recente sobrevive (slot único) — aceitável já que é
     * amostragem 1x/min e interação típica é <15 s. */
    if (TouchPriority::isActive()) {
        _pendingHistRec = rec;
        _pendingHistValid = true;
        return true;
    }

    /* Flush pendente (se existir) antes de gravar o current */
    if (_pendingHistValid) {
        _pendingHistValid = false;
        writeHistoryEntryFlash(_pendingHistRec);
    }

    return writeHistoryEntryFlash(rec);
}

bool StorageManager::writeHistoryEntryFlash(const BinaryHistoryRecord& rec) {
    if (!_isMounted) return false;
    String path = getHistoryFileName();

    LogManager::TraceScope _tr(0, MOD_HIST_FLASH);
    /* RAII context-aware igual saveConfiguration. */
    LogManager::WdtWindow _wdt(30000);

    /* F13.4/BUG-003: chunks granulares via macro FLASH_OP (file-scope).
     * Entre chunks Core 1 sai do multicore_lockout e renderiza 1 frame —
     * antes todo o path (incluindo enforceStorageLimit até 4 s) rodava em
     * 1 único lockout. File handle nunca sobrevive entre chunks. */

    /* Chunk 1: enforce storage limit (apenas na rolagem diária). */
    if (path != _currentLogFileName) {
        FLASH_OP(enforceStorageLimit());
        _currentLogFileName = path;
    }

    /* Chunk 2: open + write + close atômicos (file handle curto). */
    bool ok = false;
    FLASH_OP({
        File f = LittleFS.open(path, "a");
        if (f) {
            f.write((const uint8_t*)&rec, HISTORY_RECORD_SIZE);
            f.close();
            ok = true;
        }
    });

    if (ok) {
        _storageDirty = true;
        return true;
    }

    /* Fallback: write falhou → force enforce + retry. */
    LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "");
    _storageDirty = true;
    FLASH_OP(enforceStorageLimit());
    FLASH_OP({
        File f = LittleFS.open(path, "a");
        if (f) {
            f.write((const uint8_t*)&rec, HISTORY_RECORD_SIZE);
            f.close();
            ok = true;
        }
    });
    return ok;
    /* WdtWindow destrutor auto-restaura */
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
        if (timeSince(_budgetStart, 4000)) {
            LOG_CODE(LOG_WARN, "STO", STO_ENFORCE_BUDGET, 0, "");
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


                if (fileName.endsWith(HISTORY_FILE_EXT) && isValidHistoryFileName(fileName.c_str())) {
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
    _cursorDirty = true;
    _cursorCoalesceTime = millis();
}

void StorageManager::flushCursorIfDirty() {
    if (!_cursorDirty) return;
    if (!timeSince(_cursorCoalesceTime, CURSOR_COALESCE_MS)) return;

    /* Touch priority: se user está interagindo, cursor fica dirty e flush
     * acontece na próxima call após interaction terminar. */
    if (TouchPriority::isActive()) return;

    _cursorDirty = false;
    LogManager::WdtWindow _wdt(30000);  /* context-aware */
    enterFlashSafeMode();
    watchdog_update();
    File f = LittleFS.open(FILE_TCURSOR, "w");
    watchdog_update();
    if (f) {
        f.write((uint8_t*)&_cachedLastSent, sizeof(_cachedLastSent));
        f.close();
        watchdog_update();
    }
    exitFlashSafeMode();
    /* WdtWindow auto-restaura */
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
        if (fn.endsWith(HISTORY_FILE_EXT) && fn > newestFile) newestFile = fn;
    }
    uint32_t lastTs = 0;
    if (newestFile != "") {
        File f = LittleFS.open(String(DIR_HISTORY) + "/" + newestFile, "r");
        if (f) {
            size_t fSize = f.size();
            /* Seek direto para o último registro completo (28 bytes) */
            if (fSize >= HISTORY_RECORD_SIZE) {
                size_t lastRecordOffset = fSize - HISTORY_RECORD_SIZE;
                /* Se o arquivo não é múltiplo exato, alinha para baixo */
                if (fSize % HISTORY_RECORD_SIZE != 0) {
                    lastRecordOffset = (fSize / HISTORY_RECORD_SIZE - 1) * HISTORY_RECORD_SIZE;
                }
                f.seek(lastRecordOffset);
                f.read((uint8_t*)&lastTs, sizeof(lastTs));
            }
            f.close();
        }
    }
    exitFlashReadLock();
    return lastTs;
}


/* ─────────────────────────────────────────────────────────────────────────── */
/*  getHistoryDaysMask() — bitmask dos dias com arquivo de histórico          */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief  Retorna bitmask dos dias de um mês que possuem arquivo .bin.
 *
 * Bit N setado = dia N tem dados (bit 1 = dia 1, bit 31 = dia 31).
 * Usado pela tela de calendário no display TFT.
 *
 * @param  year   Ano (ex: 2026).
 * @param  month  Mês (1-12).
 * @return Bitmask de 32 bits com dias disponíveis.
 */
uint32_t StorageManager::getHistoryDaysMask(int year, int month) {
    uint32_t mask = 0;

    /* Monta prefixo esperado: "YYYYMM" */
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%04d%02d", year, month);

    enterFlashReadLock();
    Dir dir = LittleFS.openDir(DIR_HISTORY);
    while (dir.next()) {
        watchdog_update(); TRACE_BEAT(0);
        String fn = dir.fileName();
        if (!fn.endsWith(HISTORY_FILE_EXT)) continue;

        /* Arquivo: "YYYYMMDD.bin" — verifica prefixo do mês */
        if (fn.length() >= 8 && fn.startsWith(prefix)) {
            int day = fn.substring(6, 8).toInt();
            if (day >= 1 && day <= 31) {
                mask |= (1UL << day);
            }
        }
    }
    exitFlashReadLock();

    return mask;
}


/* =========================================================================== */
/*                PROVISIONAL TIMESTAMP CORRECTION (NTP SYNC)                */
/* =========================================================================== */
/**
 * @brief Correct timestamps written during Virtual RTC operation.
 *
 * Com formato binário, a correção é feita in-place: lê o epoch de cada
 * registro, aplica o delta se estiver no range provisório, e reescreve
 * apenas os 4 bytes do epoch. Não precisa de arquivo temporário.
 */
void StorageManager::correctProvisionalTimestamps(uint32_t bootTs, int32_t delta) {
    if (delta == 0) return;

    /* U10: reset watermark se delta mudou (nova correção NTP) */
    if (delta != _correctLastDelta) {
        _correctWatermark = "";
        _correctLastDelta = delta;
    }

    _currentLogFileName = "";
    std::vector<String> files;

    enterFlashSafeMode();
    Dir dir = LittleFS.openDir(DIR_HISTORY);
    while (dir.next()) {
        watchdog_update(); TRACE_BEAT(0);
        if (dir.fileName().endsWith(HISTORY_FILE_EXT)) files.push_back(dir.fileName());
    }
    exitFlashSafeMode();

    std::sort(files.begin(), files.end());

    uint32_t _budgetStart = millis();
    bool budgetExceeded = false;
    int totalCorrected = 0;

    for (const String& fn : files) {
        if (budgetExceeded) break;

        /* U10: pular arquivos já processados na chamada anterior */
        if (_correctWatermark.length() > 0 && fn <= _correctWatermark) continue;

        if (timeSince(_budgetStart, 6000)) {
            LOG_CODE(LOG_WARN, "STO", STO_CORRECT_BUDGET, 0, "");
            break;
        }

        String path = String(DIR_HISTORY) + "/" + fn;

        enterFlashSafeMode();
        File f = LittleFS.open(path, "r+");  /* Abrir para leitura E escrita */
        if (!f) {
            exitFlashSafeMode();
            continue;
        }

        size_t fSize = f.size();
        size_t totalRecords = fSize / HISTORY_RECORD_SIZE;
        int recCount = 0;

        for (size_t i = 0; i < totalRecords; i++) {
            /* Pausa periódica para watchdog e cooperação */
            if (++recCount % 50 == 0) {
                exitFlashSafeMode();
                watchdog_update();
                TRACE_BEAT(0);
                delay(2);
                if (timeSince(_budgetStart, 6000)) {
                    enterFlashSafeMode();
                    f.close();
                    exitFlashSafeMode();
                    budgetExceeded = true;
                    break;
                }
                enterFlashSafeMode();
            }

            /* Posição do epoch deste registro */
            size_t recOffset = i * HISTORY_RECORD_SIZE;

            /* Lê apenas o epoch (4 bytes) */
            uint32_t ts;
            f.seek(recOffset);
            if (f.read((uint8_t*)&ts, sizeof(ts)) != sizeof(ts)) continue;

            /* Aplica delta apenas a timestamps provisórios */
            if (ts >= bootTs && ts < (bootTs + 86400UL * 30)) {
                ts += delta;
                f.seek(recOffset);
                f.write((const uint8_t*)&ts, sizeof(ts));
                totalCorrected++;
            }
        }

        if (!budgetExceeded) {
            f.close();
            exitFlashSafeMode();
            _correctWatermark = fn;  /* U10: marcar arquivo como completo */
        }

        watchdog_update(); TRACE_BEAT(0);
    }

    if (totalCorrected > 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Correcao temporal concluida: %d timestamps corrigidos (delta=%ld)",
                 totalCorrected, (long)delta);
        LOG_CODE(LOG_INFO, "STO", STO_CONFIG_REPORT, 0, msg);
    }
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
 * @brief SHA256 simples — retorna hex digest de 64 chars.
 *
 * Espelha o comportamento do JavaScript SHA256 do frontend:
 * cada caractere é tratado como 1 byte pelo seu code point Unicode
 * (Latin-1), não pela codificação UTF-8. Isso é relevante para
 * caracteres como ç (U+00E7): JS processa como byte 0xE7,
 * mas UTF-8 codifica como 0xC3 0xA7 (2 bytes).
 */
String StorageManager::sha256Hex(const String& input) {
    br_sha256_context ctx;
    br_sha256_init(&ctx);

    /* Decodificar UTF-8 → code points → bytes Latin-1 (como JS charCodeAt). */
    const uint8_t* s = (const uint8_t*)input.c_str();
    size_t len = input.length();
    for (size_t i = 0; i < len; ) {
        uint8_t c = s[i];
        uint8_t byte;
        if (c < 0x80) {
            byte = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < len) {
            /* 2-byte UTF-8 → code point 0x80–0x7FF (Latin-1 cobre até 0xFF) */
            byte = (uint8_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F));
            i += 2;
        } else {
            /* 3+ byte UTF-8 ou byte inválido → skip (JS retorna undefined p/ >255) */
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
        if (r % 50 == 0) watchdog_update();  /* U5: feed mais frequente */
        br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, currentHash, 32); br_hmac_out(&ctx, currentHash);
    }


    char hashHex[32];
    for (int i = 0; i < 15; i++) snprintf(hashHex + (i * 2), 3, "%02x", currentHash[i]);
    hashHex[30] = '\0';
    return String(hashHex);
}
