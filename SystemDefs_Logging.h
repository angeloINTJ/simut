/**
 * @file    SystemDefs_Logging.h
 * @brief   Trace/log subsystem: TraceModule, LogCode, LogTagId, CompactLogRecord (EXT-003 split).
 * @details Profiler black-box (TraceModule), tabela de códigos estruturados
 *          de log (LogCode), tags de log (LogTagId + helpers), e o registro
 *          binário compacto de 12 bytes. Sub-header de SystemDefs.h (facade).
 *          EXT-003 / F17 etapa 4.
 *
 * @project SIMUT
 * @license MIT License
 */

#pragma once
#include <Arduino.h>

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
    MOD_CLI = 9,
    /* Fine-grained flash-path trace (U23) — autópsia de travamentos
     * durante save. Setados dentro das funções; restore automático via
     * TraceScope RAII. Permitem distinguir onde exatamente Core 0 parou:
     * no save do config, na escrita do log de audit, no hist, ou no
     * lockout do Core 1. */
    MOD_SAVE_CONFIG = 10,   /**< dentro de StorageManager::saveConfiguration */
    MOD_LOG_FLASH   = 11,   /**< dentro de LogManager::writeCompactToFlash / flushPendingLogs */
    MOD_HIST_FLASH  = 12,   /**< dentro de StorageManager::writeHistoryEntryFlash */
    MOD_CORE1_LOCK  = 13    /**< esperando multicore_lockout ackear (Core 1 responder) */
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
