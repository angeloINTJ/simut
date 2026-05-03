/**
 * @file    SystemDefs_Records.h
 * @brief   Persistent + runtime structs: SystemConfig, sensors, UI events, history (EXT-003 split).
 * @details Enums de domínio (SensorType, TelemetryMode, UiMode, GraphRange),
 *          structs persistidas em flash (SensorRecord, UserAccount, SystemConfig
 *          e overlays em reserved[]), structs runtime (UiEvent, GraphDataPackage,
 *          SystemStatusData, ScanResult, SensorReading) e BinaryHistoryRecord.
 *          Forward decls de utilidades compartilhadas (dallasCrc8, isValidHistoryFileName).
 *          Sub-header de SystemDefs.h (facade). EXT-003 / F17 etapa 4.
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <math.h>
#include <time.h>
#include "SystemDefs_Limits.h"

/** Physical sensor type detected during hardware scan. */
enum SensorType {
    TYPE_NONE,
    TYPE_DS18B20,
    TYPE_DHT22,
    TYPE_UNKNOWN_ACTIVITY
};

/** Payload format for telemetry uploads. */
enum TelemetryMode {
    TEL_MODE_JSON = 0,
    TEL_MODE_CSV  = 1,
    TEL_MODE_CUSTOM = 2
};

/** Network transport protocol for telemetry. */
enum TelemetryTransport {
    TEL_TRANSPORT_HTTP = 0,
    TEL_TRANSPORT_MQTT = 1
};

/**
 * Display language selection (indexes into i18n dictionary).
 *
 * CON-002: LANG_ES removido (F-I18N-TRIM.1 deletou DICTIONARY/LICENSE_ES
 * em v3.22.0 — só EN e PT persistem). LANG_COUNT é sentinela que amarra
 * o enum ao tamanho real dos arrays em DisplayManager.cpp via static_assert,
 * forçando atualização conjunta se idiomas futuros voltarem.
 */
enum LanguageCode {
    LANG_EN    = 0,
    LANG_PT    = 1,
    LANG_COUNT = 2   /**< Sentinela — total de idiomas suportados. */
};

/** Active UI screen on the TFT display (Core 1 state). */
enum UiMode {
    MODE_DASHBOARD,
    MODE_STATS_VIEW,
    MODE_GRAPH_LOADING,
    MODE_GRAPH_VIEW,
    MODE_GRAPH_DETAIL,          /**< Tela numérica de detalhes do gráfico   */
    MODE_AUTH,
    MODE_SETTINGS_MAIN,
    MODE_SETTINGS_THEMES,
    MODE_SETTINGS_ALARMS,
    MODE_SETTINGS_ALARM_EDIT,
    MODE_SETTINGS_LANG,
    MODE_SETTINGS_PASSWORD,
    MODE_SETTINGS_TOUCH_CAL,
    MODE_SETTINGS_TOUCH_SENS,   /**< Calibração de sensibilidade do touch */
    MODE_SETTINGS_SOUNDS,
    MODE_SETTINGS_LICENSE,
    MODE_SETTINGS_STATUS,       /**< Tela de status do sistema em tempo real */
    MODE_SETTINGS_DISPLAY_OFFSET, /**< Ajuste de posicionamento do LCD (±4H/±4V) */
    MODE_ALARM_ACTION,
    MODE_CALENDAR,              /**< Calendário de histórico                */
    MODE_CONFIRM_MUTE_ALL       /**< Confirma ativação do Mudo Global       */
};

/** Time range selection for graph rendering. */
enum GraphRange {
    RANGE_1H = 0,
    RANGE_6H,
    RANGE_12H,
    RANGE_24H,
    RANGE_1W
};

/** Persistent sensor configuration stored in Flash (binary, packed). */
struct __attribute__((packed)) SensorRecord {
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

/**
 * User account for web interface authentication (packed for Flash storage).
 *
 * Layout v15 (F15.2.a / CONFIG_VERSION 15):
 *   Adicionados `salt[8]` e `hashVersion` para suportar o esquema de
 *   hashing novo (SEC-007..009) com migração transparente. Configs v14 e
 *   anteriores são migradas via `StorageManager::loadAndMigrateV14`: users
 *   ficam em modo legado (`salt = {0}`, `hashVersion = 0`), mantendo o
 *   hash armazenado válido. `password` cresceu de [32] para [33] para
 *   caber 32 hex chars + null (128 bits) do esquema v1.
 */
struct __attribute__((packed)) UserAccount {
    bool active;
    char username[16];
    char password[33];           /**< Hex string do hash; ≤32 chars + null. */
    uint16_t permissions;
    bool mustChangePassword;
    uint8_t salt[8];             /**< SEC-009: salt random por usuário. {0} = modo legado. */
    uint8_t hashVersion;         /**< SEC-008: 0=legacy (2500r/120b/username-salt), 1=v1 (PASSWORD_HMAC_ROUNDS/128b/random-salt). */
};
static_assert(sizeof(UserAccount) == 62, "UserAccount v15 deve ter 62 bytes (packed)");


/**
 * Touchscreen calibration data stored in SystemConfig::reserved[0..9].
 * magic == 0xCA indicates valid calibration.
 */
struct __attribute__((packed)) TouchCalData {
    uint8_t  magic;
    uint8_t  flags;
    int16_t  xMin;
    int16_t  xMax;
    int16_t  yMin;
    int16_t  yMax;
    uint16_t zThreshold;   /**< Limiar de pressão (default 400, calibrável) */
};
static_assert(sizeof(TouchCalData) <= 24, "TouchCalData excede reserved[]!");

/**
 * Display alignment offset persistido em SystemConfig::reserved[sizeof(TouchCalData)+sizeof(SoundConfigData) .. +3].
 * magic == 0xD0 indica offset salvo; caso contrário, usa default (0,0).
 * Cada eixo é limitado a [-4, +4] pixels — compensa pequenos desalinhamentos
 * do viewing window do TFT ILI9341 sem exigir recorte significativo de conteúdo.
 */
struct __attribute__((packed)) DisplayOffsetData {
    uint8_t magic;       /**< 0xD0 = válido; outro valor = default */
    int8_t  offsetX;     /**< -4..+4 pixels (horizontal)            */
    int8_t  offsetY;     /**< -4..+4 pixels (vertical)              */
    uint8_t reserved;    /**< padding; reservado p/ extensão futura */
};
static_assert(sizeof(DisplayOffsetData) == 4, "DisplayOffsetData deve ter 4 bytes!");

/**
 * CLI session config persistido em SystemConfig::reserved[22..23].
 * magic == 0xDB indica dado válido; outro valor = default (debug OFF).
 * Fica após TouchCalData(12) + SoundConfigData(6) + DisplayOffsetData(4) = offset 22.
 */
struct __attribute__((packed)) CliConfigData {
    uint8_t magic;       /**< 0xDB = válido; outro valor = default (debug OFF) */
    uint8_t debugMode;   /**< 0 = CONFIG (silencioso), 1 = DEBUG (log stream) */
};
static_assert(sizeof(CliConfigData) == 2, "CliConfigData deve ter 2 bytes!");
#define CLI_CONFIG_MAGIC    0xDB
#define CLI_CONFIG_OFFSET   22  /* sizeof(TouchCalData)+sizeof(SoundConfigData)+sizeof(DisplayOffsetData) */

/**
 * Master system configuration — persisted to Flash as a binary blob
 * with CRC32 integrity check and dual-bank backup.
 */
struct __attribute__((packed)) SystemConfig {
    uint32_t magic;
    uint16_t version;
    char deviceName[32];

    char wifiSsid[32];
    char wifiPass[32];
    bool useDhcp;
    char staticIp[16];
    char staticMask[16];
    char staticGateway[16];
    char staticDns[16];

    bool useHttps;
    UserAccount users[MAX_USERS];

    char telServer[64];
    uint16_t telPort;
    char telPath[32];
    char telApiKey[64];
    uint32_t telInterval;
    uint8_t telBatchSize;
    bool telEncryption;
    uint8_t telMode;
    char telGlobalTemplate[256];
    char telLineTemplate[512];
    char telLineSeparator[8];


    uint8_t telTransport;
    char mqttTopic[64];
    char mqttUser[32];
    char mqttPass[32];
    uint8_t mqttQos;
    bool mqttRetain;
    char mqttClientId[24];
    uint16_t mqttKeepAlive;

    int8_t timezoneOffset;
    uint32_t sampleIntervalMs;
    bool loggingEnabled;
    uint8_t ds18Resolution;

    SensorRecord sensors[MAX_SENSORS];
    SensorRecord ambientSensor;

    int8_t themeIndex;
    char displayPin[8];
    uint8_t displayLang;

    char ntpServer[32];             /**< Servidor NTP configurável (default: pool.ntp.org) */
    /* reserved[] layout:
     *  [ 0..11] TouchCalData        (12 B)
     *  [12..17] SoundConfigData     (6 B)
     *  [18..21] DisplayOffsetData   (4 B)
     *  [22..23] CliConfigData       (2 B, Fase B v3.7.0)
     *  [24..25] WebConfigData       (2 B, U3+ — porta do servidor web)
     *  [26..27] SetupFlagsData      (2 B, F12.4 — mustChangePin)
     *  [28..47] NetworkTimeData     (20 B, F-NET-TIME.1 — DNS auto/manual + NTP toggle + DNS secundário)
     *  [48..63] livre para expansão futura
     * Expandido de 24→64 em CONFIG_VERSION 13 (v3.8.0) com migração transparente
     * de v12 via StorageManager::attemptLoad.
     */
    uint8_t reserved[64];
};

/** Overlay em reserved[24..25]: configuração do servidor web. */
struct __attribute__((packed)) WebConfigData {
    uint16_t port;  /**< Porta TCP do web server. 0 = usar default (80). */
};
constexpr size_t WEB_CONFIG_OFFSET = 24;
constexpr uint16_t WEB_DEFAULT_PORT = 80;

/**
 * @brief SEC-004/F12.3: Overlay em `reserved[26..27]` com flags de setup.
 *
 * Atualmente usado só para `FLAG_MUST_CHANGE_PIN`, que força o usuário a trocar
 * o PIN padrão `1234` do display ao primeiro acesso ao menu de configurações.
 * `magic == SETUP_FLAGS_MAGIC` valida que o overlay foi inicializado (distinto
 * de bytes zerados legados).
 *
 * Bits livres em `flags` para futuras flags de setup (ex: mustChangeBtPin).
 */
struct __attribute__((packed)) SetupFlagsData {
    uint8_t magic;          /**< 0xBE = inicializado; 0x00/outro = legado. */
    uint8_t flags;          /**< bitmask de FLAG_MUST_* */
};
constexpr size_t SETUP_FLAGS_OFFSET = 26;
constexpr uint8_t SETUP_FLAGS_MAGIC = 0xBE;
constexpr uint8_t FLAG_MUST_CHANGE_PIN = 0x01;

static_assert(sizeof(SetupFlagsData) == 2, "SetupFlagsData deve ser 2 bytes");

/**
 * @brief F-NET-TIME.1: Overlay em `reserved[28..47]` com flags de rede/tempo.
 *
 * Separa DNS do DHCP e habilita toggle de NTP sem precisar bump de
 * CONFIG_VERSION. Configs legadas (magic ausente) retornam defaults
 * retrocompatíveis (DNS auto + NTP ON) — comportamento idêntico ao anterior.
 *
 * `flags` usa o padrão "bit=1 significa AUTO/default": assim configs
 * com bytes zerados (sem magic) caem no path do magic-check antes de ler
 * flags, garantindo defaults corretos.
 *
 * `dns2[16]` guarda o DNS secundário manual (quando `DNS_AUTO=false`).
 * `dns1` reaproveita o campo existente `SystemConfig::staticDns`.
 */
struct __attribute__((packed)) NetworkTimeData {
    uint8_t magic;       /**< 0xCE = válido; outro = legado (retorna defaults). */
    uint8_t flags;       /**< bitmask de FLAG_DNS_AUTO, FLAG_NTP_ENABLED. */
    char    dns2[16];    /**< DNS secundário manual; "" se não configurado. */
    uint8_t pad[2];      /**< Reservado para extensão futura. */
};
constexpr size_t  NETTIME_OFFSET      = 28;
constexpr uint8_t NETTIME_MAGIC       = 0xCE;
constexpr uint8_t FLAG_DNS_AUTO       = 0x01;  /**< 1 = DNS via DHCP (default). */
constexpr uint8_t FLAG_NTP_ENABLED    = 0x02;  /**< 1 = NTP sync ativo (default). */

static_assert(sizeof(NetworkTimeData) == 20, "NetworkTimeData deve ter 20 bytes");

/**
 * @brief Overlay em `reserved[48..51]`: intervalo de gravacao de historico.
 *
 * Granularidade em minutos (1..1440 = 1 min..24 h). Configs legados sem
 * magic retornam default 1 min — comportamento identico ao hardcoded
 * anterior. uint16 cabe ate 65535 min (~45 dias) com folga.
 */
struct __attribute__((packed)) HistoryConfigData {
    uint8_t  magic;        /**< 0xDC = inicializado; outro = legado (default 1 min). */
    uint8_t  pad;
    uint16_t intervalMin;  /**< 1..1440. */
};
constexpr size_t  HISTORY_CONFIG_OFFSET = 48;
constexpr uint8_t HISTORY_CONFIG_MAGIC  = 0xDC;
constexpr uint16_t HISTORY_INTERVAL_DEFAULT_MIN = 1;
constexpr uint16_t HISTORY_INTERVAL_MIN_MIN     = 1;
constexpr uint16_t HISTORY_INTERVAL_MAX_MIN     = 1440;
static_assert(sizeof(HistoryConfigData) == 4, "HistoryConfigData deve ter 4 bytes");

/** Tamanho do campo reserved[] nas configs v12 (pré v3.8.0) — usado na migração. */
#define CONFIG_V12_RESERVED_SIZE 24

/* =========================================================================== */
/*                             SHARED UTILITIES                              */
/* =========================================================================== */

/** Compute CRC8 Dallas/Maxim checksum for 1-Wire ROM validation. */
uint8_t dallasCrc8(const uint8_t *addr, uint8_t len);


/** Validate history filename format (YYYYMMDD.bin, 12 chars). */
bool isValidHistoryFileName(const char* name);

/* CRC32-IEEE-802.3 incremental (poly reverso 0xEDB88320). Compatível com
 * StorageManager::calculateCRC32 (mesma matemática) mas exposto em 3 fases
 * para permitir streaming sem materializar o blob inteiro em RAM. Uso:
 *   uint32_t c = crc32_init();
 *   c = crc32_update(c, chunk1, len1);
 *   c = crc32_update(c, chunk2, len2);
 *   uint32_t final = crc32_final(c);
 */
uint32_t crc32_init();
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);
uint32_t crc32_final(uint32_t crc);

/** Result of a hardware sensor scan on a GPIO pin. */
struct ScanResult {
    uint8_t pin;
    SensorType type;
    uint8_t rom[8];
};

/** Single sensor reading result (temperature and optional humidity). */
struct SensorReading {
    float value1;
    float value2;
    bool isValid;
    char typeName[10];
};

/** UI event passed from Core 1 (display) to Core 0 (app logic) via queue. */
struct UiEvent {
    enum EventType {
        EVT_NONE,
        EVT_CHANGE_PAGE,
        EVT_SLOT_SELECT,
        EVT_OPEN_GRAPH,
        EVT_OPEN_STATS,
        EVT_OPEN_SETTINGS,
        EVT_APPLY_THEME,
        EVT_AUTH_SUCCESS,
        EVT_MENU_SELECT,
        EVT_APPLY_LANG,
        EVT_SAVE_ALARMS,
        EVT_APPLY_TOUCH_CAL,
        EVT_SAVE_PASSWORD,
        EVT_SAVE_SOUNDS,
        EVT_ALARM_SILENCE,
        EVT_ALARM_DEACTIVATE,
        EVT_ALARM_OPEN_MINMAX,
        EVT_SAVE_TOUCH_CAL,
        EVT_APPLY_DISPLAY_OFFSET,   /**< Aplica ajuste de posição do LCD e reinicia calibração do touch */
        EVT_OPEN_CALENDAR,          /**< Solicita abertura do calendário        */
        EVT_GRAPH_NAV,              /**< Navega gráfico: param = -1 (◀) ou +1 (▶) */
        EVT_CALENDAR_DAY,           /**< Dia selecionado: param = dia (1-31)    */
        EVT_CALENDAR_MONTH          /**< Mudança de mês: param = -1 ou +1       */
    };
    EventType type;
    int id;
    int param;
};

/** Data package for rendering a temperature/humidity graph on the TFT. */
struct GraphDataPackage {
    int sensorIdx;
    int timeRange;
    char title[32];
    char hwId[16];
    char rom[24];
    float pointsV1[GRAPH_WIDTH];
    float pointsV2[GRAPH_WIDTH];
    uint32_t tsPoints[GRAPH_WIDTH];     /**< Epoch de cada ponto (posição temporal no eixo X) */
    int count;
    float minVal;                       /**< Min dos pontos exibidos (decimados)   */
    float maxVal;                       /**< Max dos pontos exibidos (decimados)   */
    bool hasHumidity;

    /* Min/max REAIS — calculados de TODOS os registros na janela,
     * não apenas dos pontos decimados para exibição no display.
     * Usados para escala do eixo Y e badges de extremos. */
    float realMinVal;                   /**< Temperatura mínima real na janela     */
    float realMaxVal;                   /**< Temperatura máxima real na janela     */
    time_t tsRealMin;                   /**< Epoch da temperatura mínima real      */
    time_t tsRealMax;                   /**< Epoch da temperatura máxima real      */

    /* Índices dos pontos extremos EXIBIDOS para marcadores no gráfico */
    int idxMinTemp;                     /**< Índice do ponto de temperatura mínima */
    int idxMaxTemp;                     /**< Índice do ponto de temperatura máxima */

    /* Janela temporal solicitada — define a largura total do eixo X */
    time_t tsCutoff;                    /**< Epoch do início da janela (cutoff)    */
    time_t tsEnd;                       /**< Epoch do fim da janela (effectiveEnd) */

    /**
     * Timestamps pontuais para display.
     * tsFirst/tsLast agora são derivados de tsPoints[0] e tsPoints[count-1].
     */
    time_t tsFirst;                     /**< Epoch do primeiro ponto              */
    time_t tsMid;                       /**< Epoch do ponto central               */
    time_t tsLast;                      /**< Epoch do último ponto                */
    time_t tsMaxTemp;                   /**< Epoch da temperatura máxima exibida  */
    time_t tsMinTemp;                   /**< Epoch da temperatura mínima exibida  */
    time_t tsMaxHum;                    /**< Epoch da umidade máxima              */
    time_t tsMinHum;                    /**< Epoch da umidade mínima              */

    /* Estatísticas calculadas durante o parsing dos dados */
    float avgTemp;                      /**< Média aritmética da temperatura       */
    float stdTemp;                      /**< Desvio padrão da temperatura          */
    float deltaTemp;                    /**< Variação (último - primeiro ponto)    */
    float avgHum;                       /**< Média aritmética da umidade           */
    float stdHum;                       /**< Desvio padrão da umidade              */
    float deltaHum;                     /**< Variação da umidade                   */
};


/** Dados de status do sistema para exibição em tempo real no display. */
struct SystemStatusData {
    /* Sistema */
    uint32_t uptimeSec;
    uint32_t heapFree;
    uint32_t heapTotal;
    float    boardTemp;
    uint32_t flashUsed;
    uint32_t flashTotal;
    char     fwVersion[16];
    char     deviceName[32];

    /* Rede */
    bool     wifiConnected;
    int32_t  rssi;
    char     ip[16];
    char     mac[18];
    char     ssid[33];
    bool     ntpSynced;
    char     ntpServer[48];
    int8_t   timezone;

    /* Telemetria */
    uint16_t telPending;
    bool     mqttConnected;
    uint8_t  telTransport;
    uint8_t  telFails;
    uint32_t telInterval;
    char     telServer[64];

    /* Sensores */
    uint8_t  activeSensors;
    float    ambientTemp;
    float    ambientHum;
    bool     ambientValid;
};


/* =========================================================================== */
/*          BINARY HISTORY RECORD — 28 bytes por registro, packed             */
/* =========================================================================== */

/** @brief Tamanho fixo de cada registro binário de histórico (28 bytes). */
#define HISTORY_RECORD_SIZE   28

/** @brief Sentinela para campos sem leitura válida (equivale a NAN no float). */
#define HIST_NAN_SENTINEL     INT16_MIN   /* -32768 */

/** @brief Extensão dos arquivos de histórico binário. */
#define HISTORY_FILE_EXT      ".bin"

/**
 * @brief  Registro binário de histórico — 28 bytes, packed.
 *
 * Layout fixo por registro:
 *   [0..3]   epoch        uint32_t    timestamp Unix
 *   [4..5]   ambientTemp  int16_t     ×100 (ex: 2345 = 23.45°C)
 *   [6..7]   ambientHum   int16_t     ×100 (ex: 6120 = 61.20%)
 *   [8..27]  sensors[10]  int16_t×10  ×100 (ex: -1850 = -18.50°C)
 *
 * Valores inválidos (sem leitura) usam HIST_NAN_SENTINEL (-32768).
 * Range suportado: -327.67 a +327.67 — cobre DS18B20 e DHT22.
 */
struct __attribute__((packed)) BinaryHistoryRecord {
    uint32_t epoch;                       /* Timestamp Unix (segundos)       */
    int16_t  ambientTemp;                 /* Temperatura ambiente × 100      */
    int16_t  ambientHum;                  /* Umidade ambiente × 100          */
    int16_t  sensors[MAX_SENSORS];        /* Temperaturas dos slots × 100    */

    /* ── Helpers de conversão ── */

    /**
     * @brief  Converte float → int16 com escala ×100.
     * @param  v  Valor float (NAN retorna HIST_NAN_SENTINEL).
     * @return Valor escalado, clampado no range do int16.
     */
    static inline int16_t floatToI16(float v) {
        if (isnan(v)) return HIST_NAN_SENTINEL;
        float scaled = v * 100.0f;
        if (scaled >  32767.0f) return  32767;
        if (scaled < -32767.0f) return -32767;  /* -32768 reservado para NAN */
        return (int16_t)roundf(scaled);
    }

    /**
     * @brief  Converte int16 escalado → float.
     * @param  v  Valor int16 (HIST_NAN_SENTINEL retorna NAN).
     * @return Valor float com 2 casas decimais de precisão.
     */
    static inline float i16ToFloat(int16_t v) {
        if (v == HIST_NAN_SENTINEL) return NAN;
        return (float)v / 100.0f;
    }

    /** @brief Inicializa todos os campos com valores inválidos. */
    void clear() {
        epoch       = 0;
        ambientTemp = HIST_NAN_SENTINEL;
        ambientHum  = HIST_NAN_SENTINEL;
        for (int i = 0; i < MAX_SENSORS; i++) {
            sensors[i] = HIST_NAN_SENTINEL;
        }
    }

    /**
     * @brief  Converte o registro para uma linha CSV legível (para telemetria).
     *
     * Formato: "epoch;ambT;ambH;s0;s1;...;s9"
     * Campos inválidos ficam vazios (compatível com o formato de upload).
     *
     * @param  buf      Buffer de destino.
     * @param  bufSize  Tamanho do buffer.
     * @return Ponteiro para buf (conveniência para encadeamento).
     */
    char* toCsvLine(char* buf, size_t bufSize) const {
        if (bufSize == 0) return buf;

        int pos = snprintf(buf, bufSize, "%lu", (unsigned long)epoch);
        if ((size_t)pos >= bufSize) return buf;     /* Buffer esgotado */

        auto appendField = [&](bool valid, const char* fmt, float val) {
            if ((size_t)pos >= bufSize) return;
            if (valid) {
                pos += snprintf(buf + pos, bufSize - (size_t)pos, fmt, val);
            } else {
                pos += snprintf(buf + pos, bufSize - (size_t)pos, ";");
            }
        };

        appendField(ambientTemp != HIST_NAN_SENTINEL,
                     ";%.2f", (float)ambientTemp / 100.0f);
        appendField(ambientHum  != HIST_NAN_SENTINEL,
                     ";%.1f", (float)ambientHum  / 100.0f);

        for (int i = 0; i < MAX_SENSORS; i++) {
            appendField(sensors[i] != HIST_NAN_SENTINEL,
                         ";%.2f", (float)sensors[i] / 100.0f);
        }

        return buf;
    }
};
