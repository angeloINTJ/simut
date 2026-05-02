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
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include "pico/mutex.h"
#include "SystemDefs.h"
#include "HistoryCodec.h"

#define DIR_CONFIG      "/config"
#define FILE_CONFIG     "/config/system.bin"
#define FILE_BACKUP     "/config/system.bak"
#define FILE_TMP        "/config/system.tmp"
#define FILE_TCURSOR    "/config/t_cursor.bin"
#define DIR_HISTORY     "/history"
#define DIR_LANG        "/lang"

typedef void (*FlashLockCallback)(bool);

/** F-LOCKOUT-STUCK: callback para o modo "quiet cooperativo" em saves grandes.
 *  enable=true: Core 0 pede Core 1 congelar em loop RAM-only (IRQs off).
 *              Retorna true se Core 1 ACKed, false se Core 1 não respondeu.
 *  enable=false: libera Core 1 do quiet mode. Retorno ignorado. */
typedef bool (*BigSaveQuietCallback)(bool);

class StorageManager {
public:
    StorageManager();
    bool begin();
    void update();

    void setLockCallback(FlashLockCallback cb) { _lockCb = cb; }
    /** F-LOCKOUT-STUCK: quando setado, saveConfiguration substitui a
     *  sequência de multicore_lockout IRQ-based por um único quiet mode
     *  cooperativo, evitando cascatas de lockout stuck. */
    void setBigSaveQuietCallback(BigSaveQuietCallback cb) { _bigSaveQuietCb = cb; }


    void enterFlashReadLock();
    void exitFlashReadLock();
    void enterFlashSafeMode();
    void exitFlashSafeMode();

    /** RAII guard para flash read lock. Uso:
     *  { ReadGuard rg(&storageMgr); ... } // lock liberado no scope exit. */
    struct ReadGuard {
        StorageManager* _sto;
        ReadGuard(StorageManager* s) : _sto(s) { if (_sto) _sto->enterFlashReadLock(); }
        ~ReadGuard() { if (_sto) _sto->exitFlashReadLock(); }
    };

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

    /* REF-004: setTouchPriorityChecker removido — usa TouchPriority::isActive()
     * do header TouchPriority.h. */

    /** @return true se há record HIST pendente esperando flush.
     *  AppManager pode chamar após interação terminar para forçar flush. */
    bool hasPendingHist() const { return _pendingHistValid; }

    /** Fase 5: força flush do record HIST pendente bufferizado durante touch
     *  priority. Chamado por AppManager na transição touch-active→touch-free.
     *  No-op se não há pendente; bypassa o checker de touch pra não re-deferir. */
    bool flushPendingHist();

    SystemConfig& getConfig();
    SensorRecord* getSensorByGpio(uint8_t gpio);

    String getStatsReport();
    bool canWriteHistory(size_t sizeToWrite);

    bool writeHistoryEntry(const BinaryHistoryRecord& rec);
    String getHistoryFileName();
    void   getHistoryFileName(char* buf, size_t len);  /**< Buffer version (MEM-001). */

    uint32_t getLastRecordedTimestamp();
    uint32_t getHistoryDaysMask(int year, int month);
    void correctProvisionalTimestamps(uint32_t bootTs, int32_t delta);

    uint32_t getLastSentTimestamp();
    void setLastSentTimestamp(uint32_t ts);

    static String getBoardSerialNumber();
    bool getCalibrationData(const uint8_t* rom, String& outId, float& outOffset, String& outName);
    long getCalibrationVersion(String path);
    bool processCalibrationUpload();

    bool lockHeavyTask();
    void unlockHeavyTask();
    bool isHeavyTaskLocked() const;

    /** Hash v1 (novo padrão): username-salt, PASSWORD_HMAC_ROUNDS rounds,
     *  32 hex chars (128 bits). Usado para criar/alterar senhas. */
    String hashPassword(const String& username, const String& plainPassword);

    /** Hash legacy: username-salt, 2500 rounds, 30 hex chars (120 bits).
     *  Usado APENAS na migração transparente de login (SEC-007). */
    String hashPasswordLegacy(const String& username, const String& plainPassword);

    /** Hash v1 com salt random: userSalt[8], PASSWORD_HMAC_ROUNDS, 32 hex chars.
     *  Usado na verificação de login com hashVersion >= 1 (SEC-007/009). */
    String hashPasswordV1(const String& username, const String& plainPassword,
                          const uint8_t* userSalt);

    /** Preenche buf[8] com valores random do ROSC (rp2040.hwrand32). */
    void generateSalt(uint8_t* buf);

    String sha256Hex(const String& input);
    void   flushCursorIfDirty();
    void   invalidateOldestFileCache() { _cachedOldestFile = ""; }

    /**
     * @brief SEC-003/F12.3: Gera senha admin inicial aleatória.
     *
     * Alfabeto [A-Z2-9] de 32 chars (exclui O/0/I/1 para não confundir).
     * Entropia: 32^8 ≈ 1.1 × 10^12 combinações. Usa `rp2040.hwrand32()`,
     * que no RP2040 é backed pelo ROSC (ring oscillator) — entropia de hw.
     *
     * @param  outPlain  Buffer de saída (null-terminated).
     * @param  bufSize   Tamanho do buffer (precisa ≥ 9 para 8 chars + '\0').
     */
    void generateInitialAdminPassword(char* outPlain, size_t bufSize);

    /** @return true se a config atual está em factory defaults —
     *  i.e., admin[0] ativo com `mustChangePassword=true`. Calculado em tempo real. */
    bool isFactoryDefaults() const;

    /** @return plaintext da senha admin random gerada por `loadDefaults()`
     *  nesta sessão. String vazia se já trocada OU se loadDefaults não rodou
     *  (config válida carregada do flash). NUNCA persistida. */
    const char* getInitialAdminPassword() const { return _initialAdminPassword; }

    /** Zera `_initialAdminPassword` em RAM. Chamado automaticamente por
     *  `saveConfiguration()` quando `admin.mustChangePassword` vira false,
     *  e por `loadConfiguration()` ao carregar config válida do flash. */
    void clearInitialAdminPassword();

    /**
     * @brief SEC-004/F12.4: True se o PIN do display ainda é o default
     *  (factory defaults) e precisa ser trocado antes do usuário operar
     *  livremente o menu de configurações.
     *
     * Overlay em `reserved[26..27]` (SetupFlagsData). Configs v13-v14 legadas
     * sem magic retornam false (assume que já estão configuradas — evita
     * forçar troca para quem só fez upgrade de firmware).
     */
    bool mustChangePin() const;

    /** Limpa `FLAG_MUST_CHANGE_PIN` no overlay SetupFlagsData.
     *  Chamado quando o usuário salva um PIN != "1234". */
    void clearMustChangePin();

    /** Seta `FLAG_MUST_CHANGE_PIN` no overlay SetupFlagsData.
     *  Chamado em `loadDefaults()` (factory reset). */
    void setMustChangePin();

    /* =====================================================================
     * F-NET-TIME.1 — overlay NetworkTimeData em reserved[28..47]
     * =====================================================================
     * Defaults retrocompatíveis: configs legadas sem magic retornam
     * DNS auto + NTP habilitado (idêntico ao comportamento pré-feature).
     * Qualquer set* popula o magic antes de atualizar as flags. */

    /** @return true se DNS deve ser obtido via DHCP (default).
     *  false = DNS primário em `staticDns` + secundário em overlay `dns2`. */
    bool isDnsAuto() const;

    /** Seta a flag DNS_AUTO no overlay. Popula magic se ainda não estava. */
    void setDnsAuto(bool auto_);

    /** @return true se NTP sync está habilitado (default). false = RTC manual. */
    bool isNtpEnabled() const;

    /** Seta a flag NTP_ENABLED no overlay. Popula magic se ainda não estava. */
    void setNtpEnabled(bool enabled);

    /** @return DNS secundário manual (string null-terminated). "" se não configurado. */
    const char* getSecondaryDns() const;

    /** Seta o DNS secundário no overlay (máx 15 chars + '\0'). Popula magic. */
    void setSecondaryDns(const char* ip);

    /** @return Intervalo de gravacao de historico em minutos. Default 1 min se overlay legado. */
    uint16_t getHistoryIntervalMin() const;

    /** Seta o intervalo de historico (clampa em [HISTORY_INTERVAL_MIN_MIN, HISTORY_INTERVAL_MAX_MIN]). */
    void setHistoryIntervalMin(uint16_t minutes);
    SystemConfig _currentConfig;
    bool _isMounted = false;
    FlashLockCallback    _lockCb          = nullptr;
    BigSaveQuietCallback _bigSaveQuietCb  = nullptr;
    /* F-LOCKOUT-STUCK: true durante saveConfiguration com quiet mode ativo;
     * enterFlashSafeMode/exitFlashSafeMode pulam o lockCb quando setado. */
    bool _inBigSave = false;
    mutex_t _fsReadMutex;

    bool _heavyTaskLocked = false;
    uint32_t _cachedLastSent = 0;
    bool     _cursorDirty = false;
    uint32_t _cursorCoalesceTime = 0;
    bool     _lastSaveWasNoOp = false;  /**< True se saveConfiguration pulou por CRC idêntico */
    volatile uint32_t _lastSaveMs = 0;  /**< millis() do último save real (0 = nunca) */
    uint32_t _lastSavedCrc = 0;         /**< CON-004: CRC32 do último save persistido; skip-no-op quando igual. */

    /* REF-004: _isTouchPriorityFn removido — usa TouchPriority::isActive(). */

    /** SEC-003/F12.3: senha admin plaintext em RAM (NUNCA persistida em flash).
     *  Populada por `generateInitialAdminPassword` durante `loadDefaults()`.
     *  Zerada por `clearInitialAdminPassword()` quando admin troca senha OU
     *  quando config válida é carregada do flash (i.e., não factory). */
    char _initialAdminPassword[9] = {0};
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

    /** Estado do codec v2 do arquivo ativo. Valido somente para o arquivo
     *  cujo path == _currentLogFileName. Reconstruido por scan ao mudar de
     *  arquivo (boot ou day rollover). */
    HistoryCodecState _histCodec;
    bool _histCodecValid = false;

    bool mountFS();
    void loadDefaults();
    void enforceStorageLimit();

    /** F-NET-TIME.1: retorna ponteiro ao overlay NetworkTimeData em
     *  `reserved[28..47]`. Se magic ausente, inicializa com defaults
     *  retrocompatíveis (DNS auto + NTP ON) antes de retornar. */
    NetworkTimeData* ensureNetworkTimeOverlay();

    /* F13.4/BUG-003 — chunking granular de flash safe mode.
     * Implementado como macro file-scope em StorageManager.cpp (não
     * template no header) para evitar que `#include "LogManager.h"` seja
     * arrastado por todos os clientes de StorageManager.h, inchando várias
     * unidades de tradução e estourando flash. Comportamento idêntico:
     * cada op LittleFS tem seu próprio enterFlashSafeMode/exitFlashSafeMode;
     * entre chunks Core 1 renderiza. */

    static uint32_t calculateCRC32(const uint8_t *data, size_t length);
    static bool loadCurrentBlob(File& f, SystemConfig& outCfg);
    static bool loadAndMigrateV12(File& f, SystemConfig& outCfg);
    /** F15.2.a: migra v13 (plaintext) ou v14 (obfuscated) para v15
     *  (UserAccount expandido com salt+hashVersion). srcVersion sai com 13 ou 14. */
    static bool loadAndMigrateV14(File& f, SystemConfig& outCfg, uint16_t& srcVersion);
    bool attemptLoad(const char* path, SystemConfig& outCfg);

    /** #5: ofusca/desofusca os 3 campos sensíveis da config com keystream
     *  derivado de SHA-256(chip_id + domain). XOR é simétrico — a mesma
     *  chamada criptografa (save) ou descriptografa (load). */
    static void obfuscateSensitiveFields(SystemConfig& cfg);
};
