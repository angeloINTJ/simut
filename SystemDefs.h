/**
 * @file    SystemDefs.h
 * @brief   Global type definitions, enumerations, structs, and shared utilities.
 * @details Central header shared by all modules. Defines hardware limits,
 * permission bitmasks, sensor/config/UI structures, CSV parser,
 * input validation helpers, and the CLI command architecture.
 * All packed structs use __attribute__((packed)) for binary storage.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <string.h>

/* Hardware and system limits */
#define MAX_SENSORS 10                  /* Maximum number of configurable sensor slots */
#define MAX_USERS 5                     /* Maximum user accounts (Flash/RAM budget) */
#define MOVING_AVG_WINDOW 10            /* Samples in the trimmed-mean sliding window */
#define SIMUT_VERSION "v3.8.1"          /* Firmware version string */

#define GRAPH_WIDTH 200                 /* Maximum data points on the TFT graph */

/**
 * @brief Índices nomeados para o array de cache min/max (sensores + board temp).
 *
 * Os slots 0–9 correspondem aos sensores configuráveis (MAX_SENSORS).
 * O slot 10 é reservado para a temperatura interna da placa (board temp).
 * MINMAX_SLOT_COUNT define o tamanho total dos arrays de cache.
 */
enum MinMaxSlot {
    MINMAX_SLOT_BOARD_TEMP = MAX_SENSORS,   /**< Índice da board temp no cache   */
    MINMAX_SLOT_COUNT      = MAX_SENSORS + 1 /**< Tamanho total do array de cache */
};

/* =========================================================================== */
/*                          BOOT TIMING CONSTANTS                            */
/* =========================================================================== */

/** Tempo que o usuário precisa manter o toque para entrar em AP Mode (ms). */
constexpr uint32_t AP_HOLD_DURATION_MS      = 3000;

/** Janela de espera para detectar início do toque no boot (ms). */
constexpr uint32_t AP_DETECT_WINDOW_MS      = 3500;

/** Delay entre etapas do boot para feedback visual (ms). */
constexpr uint32_t BOOT_STEP_DELAY_MS       = 800;

/** Delay de polling durante loops de espera no boot (ms). */
constexpr uint32_t BOOT_POLL_INTERVAL_MS    = 50;

/**
 * Timeout do hardware watchdog em milissegundos.
 * Dimensionado para cobrir o pior caso de write em Flash + scan WiFi
 * sem disparar falso reset durante operações legítimas de I/O.
 */
constexpr uint32_t WATCHDOG_TIMEOUT_MS      = 8300;

/** Toques perdidos tolerados antes de cancelar AP hold. */
constexpr int      AP_HOLD_MAX_MISSED       = 5;


/* =========================================================================== */
/*                     NETWORK RESILIENCE CONSTANTS                          */
/* =========================================================================== */

/**
 * Timeout de socket para operações TCP/TLS (ms).
 * Garante que nenhuma chamada bloqueante de rede ultrapasse o watchdog.
 * Deve ser significativamente menor que WATCHDOG_TIMEOUT_MS.
 */
constexpr uint32_t NET_SOCKET_TIMEOUT_MS    = 4000;

/**
 * RSSI mínimo aceitável para operações de rede pesadas (dBm).
 * Abaixo desse limiar, telemetria e uploads são adiados para evitar
 * timeouts que congelam o main loop. Dashboard e sensores continuam.
 */
constexpr int32_t  RSSI_MIN_THRESHOLD       = -78;

/**
 * Intervalo mínimo entre chamadas a MDNS.update() (ms).
 * mDNS não precisa de polling a cada loop — throttle evita overhead
 * desnecessário em rede degradada.
 */
constexpr uint32_t MDNS_UPDATE_INTERVAL_MS  = 2000;

/**
 * Máximo de ciclos de reconexão WiFi consecutivos antes de entrar
 * em dormência longa (backoff de 10 minutos). Resetado após sucesso.
 */
constexpr uint8_t  WIFI_MAX_CONNECT_CYCLES  = 5;

/** Backoff de dormência longa após esgotar tentativas WiFi (ms). */
constexpr uint32_t WIFI_DORMANT_DELAY_MS    = 600000;

/**
 * Teto para alimentação do watchdog em guards de operações longas (ms).
 *
 * Aplica-se aos *repeating timers* `SendGuard` (WebManager) e
 * `TelemetryGuard` (TelemetryManager). Enquanto uma operação bloqueante
 * está em curso (POST TLS, envio de payload grande), o guard alimenta o
 * watchdog a cada 2 s — até este teto. Se ultrapassado, para de alimentar
 * (watchdog age como *safety net* contra deadlocks reais) E sinaliza
 * *aborto limpo* via flag compartilhada, para que o handler retorne
 * com erro em vez de ser morto pelo watchdog.
 *
 * Dimensionamento: valor deve cobrir o pior caso de operação legítima
 * (TLS handshake + envio de 230 KB em link 2G) — 60 s com folga.
 * Deve ser muito maior que NET_SOCKET_TIMEOUT_MS para evitar falso
 * positivo e muito menor que uptime-ms-wrap (~49 d) por definição.
 */
constexpr uint32_t WDT_FEED_MAX_WINDOW_MS   = 60000;

/**
 * Teto do backoff exponencial de retry do NTP (ms).
 *
 * Sequência aplicada em NET_CONNECTED_WAIT_NTP: 20 s → 60 s → 5 min → 15 min.
 * Após 3 falhas consecutivas, faz-se fallback automático para pool.ntp.org.
 * Reset a zero (volta para 20 s) após primeira sincronização bem-sucedida.
 */
constexpr uint32_t NTP_MAX_RETRY_DELAY_MS   = 900000;

/**
 * Número de falhas consecutivas no NTP antes de acionar fallback para
 * pool.ntp.org. Se o servidor configurado já for pool.ntp.org, o fallback
 * é silenciosamente ignorado.
 */
constexpr uint8_t  NTP_FAILS_BEFORE_FALLBACK = 3;

/* ── Rate-limiter (WebManager) ── */

/** Número de slots no rate-limiter por IP. */
constexpr uint8_t  RATE_LIMIT_SLOTS          = 16;

/** TTL de uma entrada no rate-limiter (ms). Slot expirado é tratado como livre. */
constexpr uint32_t RATE_LIMIT_TTL_MS         = 900000;

/* ── Login state ── */

/** Número de slots para rastreamento de estado de login (IP → failCount). */
constexpr uint8_t  LOGIN_STATE_SLOTS         = 8;

/* ── Bluetooth auth ── */

/** Tamanho máximo do buffer de entrada de senha via Bluetooth. */
constexpr uint8_t  BT_AUTH_BUFFER_MAX        = 64;

/* ── Web handlers ── */

/** Deadline para handlers longos (history, logs, screenshot) em ms. */
constexpr uint32_t WEB_LONG_HANDLER_DEADLINE_MS = 10000;

/* ── AP mode ── */

/** Timeout do AP mode sem clientes antes de reboot para STA (ms). */
constexpr uint32_t AP_MODE_TIMEOUT_MS           = 900000;

/* ── Telemetry cursor ── */

/** Tempo mínimo entre writes do cursor de telemetria no flash (ms).
 *  Múltiplos setLastSentTimestamp dentro dessa janela consolidam em 1 write. */
constexpr uint32_t CURSOR_COALESCE_MS           = 5000;

/* =========================================================================== */
/*                       SAFE STRING COPY UTILITY                            */
/* =========================================================================== */

/**
 * @brief  Copia uma string para um buffer de tamanho fixo com null-termination garantida.
 *
 * Substitui o padrão inseguro de strncpy sem terminador.
 * Uso típico: safeCopy(cfg.deviceName, source, sizeof(cfg.deviceName));
 *
 * @param  dst      Buffer de destino.
 * @param  src      String de origem (pode ser nullptr — resulta em string vazia).
 * @param  dstSize  Tamanho total do buffer de destino (incluindo o '\0').
 */
inline void safeCopy(char* dst, const char* src, size_t dstSize) {
    if (dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}


/* =========================================================================== */
/*                      WRAP-SAFE MILLIS() COMPARISON                        */
/* =========================================================================== */

/**
 * @brief  Verifica de forma segura se um deadline (baseado em millis()) foi atingido.
 *
 * SEMPRE usar esta função em vez de `millis() > deadline` ou `millis() < deadline`.
 * O contador millis() é um uint32_t que sofre wraparound a cada ~49,7 dias; a
 * comparação direta inverte o resultado após o wrap, causando timeouts eternos
 * (lockouts que nunca expiram, handlers que travam, etc.).
 *
 * A subtração em aritmética signed trata o wraparound corretamente:
 *   - retorna >= 0 quando now já atingiu/passou deadline
 *   - retorna  < 0 quando ainda não chegou
 *
 * Uso típico:
 *   if (timeReached(_lockoutUntil))   forceDashboard();   // destrava
 *   if (!timeReached(_deadline))      _pending = true;    // ainda esperando
 *
 * @param  deadline  Valor absoluto de millis() a comparar com o "agora".
 * @return true se millis() já atingiu ou passou deadline (wrap-safe).
 */
inline bool timeReached(uint32_t deadline) {
    return (int32_t)(millis() - deadline) >= 0;
}

/**
 * @brief  Tempo restante até deadline, em milissegundos. Wrap-safe.
 *
 * Retorna 0 se o deadline já passou. Substitui o padrão inseguro
 * `deadline - millis()`, que sofre underflow (retorna valor enorme) após
 * o wrap de millis() e produz "segundos restantes" absurdos no UI.
 *
 * @param  deadline  Valor absoluto de millis() a comparar com o "agora".
 * @return millissegundos até deadline, ou 0 se já atingido.
 */
inline uint32_t timeRemaining(uint32_t deadline) {
    int32_t diff = (int32_t)(deadline - millis());
    return (diff > 0) ? (uint32_t)diff : 0;
}


/* Permission bitmasks for role-based access control (RBAC) */
#define PERM_DASHBOARD   0x0001
#define PERM_HISTORY     0x0002
#define PERM_LOGS        0x0004
#define PERM_SYS_CONFIG  0x0008
#define PERM_NET_CONFIG  0x0010
#define PERM_FILE_READ   0x0020
#define PERM_FILE_UPLOAD 0x0040
#define PERM_FILE_DELETE 0x0080
#define PERM_USER_MGR    0x0100

#define PERM_FULL_ADMIN  0xFFFF


/* =========================================================================== */
/*                   BLACK BOX PROFILER — MODULE TRACKING                    */
/* =========================================================================== */

/** Identifiers for the per-core module profiler (crash forensics). */
enum TraceModule {
    MOD_BOOT = 0,
    MOD_IDLE = 1,
    MOD_WIFI = 2,
    MOD_WEB_SERVER = 3,
    MOD_STORAGE_READ = 4,
    MOD_STORAGE_WRITE = 5,
    MOD_SENSOR_READ = 6,
    MOD_TELEMETRY = 7,
    MOD_DISPLAY = 8,
    MOD_CLI = 9
};

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

/** Display language selection (indexes into i18n dictionary). */
enum LanguageCode {
    LANG_EN = 0,
    LANG_PT = 1,
    LANG_ES = 2
};

/** Structured log event codes for machine-parseable system logging. */
enum LogCode {
    SYS_OK              = 0,
    SYS_BOOT            = 1,
    SYS_REBOOT_USER     = 2,
    SYS_HEAP_LOW        = 3,
    SYS_UPTIME_MARK     = 4,

    SYS_WIFI_CONNECT    = 10,
    SYS_WIFI_DISCONNECT = 11,
    SYS_WIFI_SCAN       = 12,
    SYS_NTP_SYNC        = 13,
    SYS_IP_ACQUIRED     = 14,
    SYS_AP_START        = 15,

    SYS_STORAGE_FAIL    = 20,
    SYS_STORAGE_SAVE    = 21,
    SYS_STORAGE_ROTATE  = 22,
    SYS_STORAGE_FORMAT  = 23,
    SYS_STORAGE_RECOVER = 24,
    SYS_STORAGE_MIGRATED = 25,

    SYS_TEL_SENT        = 30,
    SYS_TEL_FAIL        = 31,
    SYS_TEL_RETRY       = 32,
    SYS_TEL_QUEUE       = 33,
    SYS_TEL_SSL         = 34,
    SYS_TEL_MQTT_CONN   = 35,
    SYS_TEL_MQTT_DISC   = 36,
    SYS_TEL_MQTT_PUB    = 37,

    LOG_SENSOR_REC      = 100,
    ERR_SENSOR_TIMEOUT  = 101,
    ERR_SENSOR_CHECKSUM = 102,
    ERR_SENSOR_CRC      = 103,
    ERR_SENSOR_RANGE    = 104,
    ERR_SENSOR_MISMATCH = 105,
    ERR_SENSOR_MISSING  = 106,

    EVT_UI_TOUCH        = 200,
    EVT_DISPLAY_RESTART = 201,
    EVT_GRAPH_RENDER    = 202,

    SEC_LOGIN_SUCCESS   = 300,
    SEC_LOGIN_FAIL      = 301,
    SEC_UNAUTHORIZED    = 302,
    SEC_CONFIG_CHANGED  = 303,
    SEC_SESSION_EXPIRE  = 304,
    SEC_FILE_UPLOAD     = 305,
    SEC_FILE_DELETE     = 306,


    /* ── Application lifecycle (400–439) ── */
    APP_DISPLAY_LAUNCHED    = 400,
    APP_TOUCH_CAL_INITIAL   = 401,
    APP_TOUCH_CAL_REQUIRED  = 402,
    APP_AP_MODE_TRIGGERED   = 403,
    APP_READY               = 404,
    APP_READY_AP            = 405,
    APP_STORAGE_CRITICAL    = 406,
    APP_SENSORS_CALIBRATED  = 407,
    APP_NTP_CORRECTING      = 408,
    APP_NTP_CORRECTED       = 409,
    APP_CACHE_INVALIDATED   = 410,

    /* ── Application UI events (440–469) ── */
    APP_UI_THEME_CHANGED    = 440,
    APP_UI_LANG_CHANGED     = 441,
    APP_UI_ALARM_SAVED      = 442,
    APP_UI_TOUCH_CAL_SAVED  = 443,
    APP_UI_TOUCH_SENS_SAVED = 444,
    APP_UI_PIN_CHANGED      = 445,
    APP_UI_SOUND_SAVED      = 446,
    APP_UI_ALARM_SILENCED   = 447,
    APP_UI_ALARM_SILENCE_EXP= 448,
    APP_UI_ALARM_DEACTIVATED= 449,

    /* ── Alarm state (470–479) ── */
    APP_ALARM_TRIGGERED     = 470,
    APP_ALARM_CLEARED       = 471,
    APP_ALARM_SILENCE_CANCEL= 472,

    /* ── Cache/graph (480–499) ── */
    APP_CACHE_MINMAX_FULL   = 480,
    APP_CACHE_MINMAX_PARTIAL= 481,
    APP_CACHE_GRAPH_STARTED = 482,
    APP_CACHE_GRAPH_DONE    = 483,
    APP_CACHE_GRAPH_AMBIENT = 484,
    APP_CACHE_GRAPH_BOARD   = 485,
    APP_CACHE_PRELOAD_DONE  = 486,
    APP_GRAPH_LOADING       = 487,
    APP_GRAPH_BUDGET        = 488,
    APP_PRELOAD_BUDGET      = 489,

    /* ── Safety watchdogs (500–509) ── */
    APP_DISPLAY_PAUSE_STUCK = 500,
    APP_YIELD_STUCK         = 501,
    APP_CORE1_DEAD          = 502,
    APP_FLASH_BUSY          = 503,

    /* ── History (510–514) ── */
    APP_HISTORY_SAVED       = 510,
    APP_HEAP_REPORT         = 511,

    /* ── Network extended (520–539) ── */
    NET_DHCP_MODE           = 520,
    NET_STATIC_MODE         = 521,
    NET_STARTING            = 522,
    NET_SSID_MISSING        = 523,
    NET_PROVISIONAL_TIME    = 524,
    NET_CONNECT_TIMEOUT     = 525,
    NET_DORMANT_MODE        = 526,
    NET_SHOW_IP             = 527,

    /* ── Telemetry extended (540–559) ── */
    TEL_HTTP_INIT           = 540,
    TEL_MQTT_INIT           = 541,
    TEL_MQTT_CONNECTING     = 542,
    TEL_CERT_EMPTY          = 543,
    TEL_CERT_READ_ERR       = 544,
    TEL_CERT_MISSING        = 545,
    TEL_FORCE_SYNC          = 546,
    TEL_BACKOFF_SUPPRESSED  = 547,

    /* ── Storage extended (560–569) ── */
    STO_WRITE_FAILED        = 560,
    STO_CORRECT_BUDGET      = 561,
    STO_ENFORCE_BUDGET      = 562,
    STO_ENFORCE_SKIP_ACTIVE = 563,
    STO_STATS_REPORT        = 564,
    STO_CONFIG_REPORT       = 565,

    /* ── Web server (570–579) ── */
    WEB_SERVER_STARTED      = 570,
    WEB_DISCONNECT_FILE     = 571,
    WEB_DISCONNECT_HISTORY  = 572,
    WEB_SCREENSHOT_ABORTED  = 573,
    WEB_UPLOAD              = 574,

    /* ── Config (580–584) ── */
    CFG_THEME_APPLIED       = 580,
    CFG_THEME_NOT_FOUND     = 581,

    /* ── CLI (585–589) ── */
    CLI_UNKNOWN_CMD         = 585,

    /* ── Sensors extended (590–599) ── */
    SENSOR_RUNTIME_LOADED   = 590,

    /* ── Display (600–604) ── */
    DSP_FORCE_UNPAUSE       = 600,

    ERR_UNKNOWN         = 999
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
    MODE_CALENDAR               /**< Calendário de histórico                */
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

/** User account for web interface authentication (packed for Flash storage). */
struct __attribute__((packed)) UserAccount {
    bool active;
    char username[16];
    char password[32];
    uint16_t permissions;
    bool mustChangePassword;
};


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
     *  [24..63] livre para expansão futura
     * Expandido de 24→64 em CONFIG_VERSION 13 (v3.8.0) com migração transparente
     * de v12 via StorageManager::attemptLoad.
     */
    uint8_t reserved[64];
};

/** Tamanho do campo reserved[] nas configs v12 (pré v3.8.0) — usado na migração. */
#define CONFIG_V12_RESERVED_SIZE 24


/* =========================================================================== */
/*                             SHARED UTILITIES                              */
/* =========================================================================== */

/** Compute CRC8 Dallas/Maxim checksum for 1-Wire ROM validation. */
uint8_t dallasCrc8(const uint8_t *addr, uint8_t len);


/** Validate history filename format (YYYYMMDD.csv, 12 chars). */
bool isValidHistoryFileName(const char* name);

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
/*                     COMPACT BINARY LOG RECORD — 12 bytes                  */
/* =========================================================================== */

/**
 * @brief Identificadores de módulo/tag para log compacto (4 bits, max 16).
 *
 * Cada tag de log ("APP", "NET", etc.) é mapeada para um ID numérico
 * armazenado no campo flags do CompactLogRecord.
 */
enum LogTagId : uint8_t {
    TAG_APP    = 0,
    TAG_NET    = 1,
    TAG_TEL    = 2,
    TAG_STO    = 3,
    TAG_WEB    = 4,
    TAG_CFG    = 5,
    TAG_CLI    = 6,
    TAG_SENSOR = 7,
    TAG_HIST   = 8,
    TAG_SYS    = 9,
    TAG_DSP    = 10,
    TAG_SEC    = 11,
    TAG_UNKNOWN= 15
};

/** @brief Converte string de tag para LogTagId. */
inline LogTagId tagStringToId(const char* tag) {
    if (!tag) return TAG_UNKNOWN;
    switch (tag[0]) {
        case 'A': return TAG_APP;
        case 'N': return TAG_NET;
        case 'T': return TAG_TEL;
        case 'W': return TAG_WEB;
        case 'C': return (tag[1] == 'F') ? TAG_CFG : TAG_CLI;
        case 'S': return (tag[1] == 'T') ? TAG_STO :
                         (tag[1] == 'E') ? TAG_SENSOR :
                         (tag[1] == 'Y') ? TAG_SYS : TAG_SEC;
        case 'H': return TAG_HIST;
        case 'D': return TAG_DSP;
        default:  return TAG_UNKNOWN;
    }
}

/** @brief Converte LogTagId de volta para string (para display). */
inline const char* tagIdToString(uint8_t id) {
    static const char* const TAG_NAMES[] = {
        "APP", "NET", "TEL", "STO", "WEB", "CFG", "CLI",
        "SENSOR", "HIST", "SYS", "DSP", "SEC",
        "?", "?", "?", "?"
    };
    return (id < 16) ? TAG_NAMES[id] : "?";
}

/**
 * @brief  Registro binário de log — 12 bytes, packed.
 *
 * Substitui as linhas CSV (~100-200 bytes cada) por um formato fixo
 * que armazena apenas código estruturado + contexto numérico.
 * A mensagem legível é reconstruída sob demanda via tabela de tradução.
 *
 * Layout:
 *   [0..3]   epoch      uint32_t   Timestamp Unix
 *   [4..5]   uptimeHr   uint16_t   Uptime em horas (cobre ~7,5 anos — wrap-safe)
 *   [6..7]   code       uint16_t   LogCode enum
 *   [8..9]   context    int16_t    Valor contextual (GPIO, delta, count...)
 *   [10]     flags      uint8_t    [level:3 | core:1 | tagId:4]
 *   [11]     reserved   uint8_t    Reservado (padding)
 *
 * Economia: ~90-95% vs formato CSV anterior.
 */
struct __attribute__((packed)) CompactLogRecord {
    uint32_t epoch;
    uint16_t uptimeHr;   /**< Uptime em horas (millis()/3600000); 65535h ≈ 7,5 anos. */
    uint16_t code;
    int16_t  context;
    uint8_t  flags;
    uint8_t  reserved;

    /** @brief Empacota level, core e tagId no campo flags. */
    static inline uint8_t packFlags(uint8_t level, uint8_t core, uint8_t tagId) {
        return ((level & 0x07) << 5) | ((core & 0x01) << 4) | (tagId & 0x0F);
    }

    inline uint8_t getLevel() const { return (flags >> 5) & 0x07; }
    inline uint8_t getCore()  const { return (flags >> 4) & 0x01; }
    inline uint8_t getTagId() const { return  flags       & 0x0F; }
};
static_assert(sizeof(CompactLogRecord) == 12, "CompactLogRecord must be 12 bytes!");

/** @brief Tamanho de cada registro de log no flash (bytes). */
#define LOG_RECORD_SIZE  12


/* =========================================================================== */
/*                         CLI COMMAND ARCHITECTURE                          */
/* =========================================================================== */

/** CLI command types parsed from USB/Bluetooth input. */
enum DemandType {
    CMD_NONE = 0,
    CMD_UNKNOWN,
    CMD_HELP,
    CMD_SHOW_THEMES,
    CMD_SET_THEME,
    CMD_SHOW_LOGS,
    CMD_SHOW_SENSORS,
    CMD_SHOW_STORAGE,
    CMD_SHOW_SYSINFO,
    CMD_SHOW_NET,
    CMD_SET_DS_RES,
    CMD_SET_SYS_NAME,
    CMD_SET_WIFI_SSID,
    CMD_SET_WIFI_PASS,
    CMD_SET_TIMEZONE,
    CMD_SET_NTP,
    CMD_SET_TEL_SERVER,
    CMD_SET_TEL_PORT,
    CMD_SET_TEL_PATH,
    CMD_SET_TEL_BATCH,
    CMD_SET_TEL_INTERVAL,
    CMD_SET_TEL_CRYPTO,
    CMD_SET_TEL_MODE,
    CMD_RESET_ADMIN,
    CMD_RESET_TOUCH_CAL,
    CMD_DEFINE_SENSOR,
    CMD_WIPE_SENSOR,
    CMD_ACCEPT_SENSOR,
    CMD_SCAN_SENSORS,
    CMD_WRITE_MEMORY,
    CMD_CLEAR_LOGS,
    CMD_RELOAD,
    CMD_TEL_SYNC,
    CMD_DEBUG
};

/** Parsed CLI command with typed payload fields. */
struct CliDemand {
    DemandType type;
    String strVal1;
    String strVal2;
    int intVal1;
    bool boolVal;
    uint8_t rom[8];
    bool confirmed = false;  /**< true se sufixo 'confirm' presente — gate p/ comandos destrutivos */
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


/* =========================================================================== */
/*                         INPUT VALIDATION HELPERS                          */
/* =========================================================================== */

/** Validate names (device, username): no control chars, no quotes/backslash, 1-31 chars. */
inline bool isValidName(const char* name, size_t maxLen = 31) {
    if (!name) return false;
    size_t len = strlen(name);
    if (len == 0 || len > maxLen) return false;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)name[i] < 32 || name[i] == '"' || name[i] == '\\') return false;
    }
    return true;
}


/** Validate IPv4 address format (e.g., "192.168.1.100"). */
inline bool isValidIpv4(const char* ip) {
    if (!ip || strlen(ip) < 7 || strlen(ip) > 15) return false;
    int parts = 0;
    int val = 0;
    bool hasDigit = false;
    for (const char* p = ip; ; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) return false;
            hasDigit = true;
        } else if (*p == '.' || *p == '\0') {
            if (!hasDigit) return false;
            parts++;
            val = 0;
            hasDigit = false;
            if (*p == '\0') break;
        } else {
            return false;
        }
    }
    return (parts == 4);
}


/** Check if a numeric value falls within [minVal, maxVal]. */
inline bool isInRange(int value, int minVal, int maxVal) {
    return (value >= minVal && value <= maxVal);
}
