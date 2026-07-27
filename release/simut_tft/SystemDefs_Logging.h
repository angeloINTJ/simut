/**
 * @file SystemDefs_Logging.h
 * @brief Trace/log subsystem: TraceModule, LogCode, LogTagId, CompactLogRecord.
 * @details Profiler black-box (TraceModule), structured log code table
 * (LogCode), log tags (LogTagId + helpers), and the 12-byte compact
 * binary record. Sub-header of SystemDefs.h (facade).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>

/* =========================================================================== */
/* BLACK BOX PROFILER — MODULE TRACKING */
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
 /* Fine-grained flash-path trace — crash autopsy during save.
 * Set inside functions; automatic restore via TraceScope RAII.
 * Allows distinguishing exactly where Core 0 stopped:
 * in config save, audit log write, history, or Core 1 lockout. */
 MOD_SAVE_CONFIG = 10, /**< inside StorageManager::saveConfiguration */
 MOD_LOG_FLASH = 11, /**< inside LogManager::writeCompactToFlash / flushPendingLogs */
 MOD_HIST_FLASH = 12, /**< inside StorageManager::writeHistoryEntryFlash */
 MOD_CORE1_LOCK = 13, /**< waiting for multicore_lockout ack (Core 1 responding) */
 /* Kill-path steps. Every SDK call below can block WITHOUT a timeout, and a
  * Core-0 hang is erased from RAM by the HW watchdog that follows — so the
  * marker has to live in watchdog scratch[3], which is exactly what TRACE_MOD
  * writes and what the boot autopsy prints back as C0=[...]. Every [FTL] in the
  * log carries ctx=0 (Core 0 stalled) right after an APP_CORE1_DEAD, so the
  * stall is inside restartCore1( ); these three names say which call. */
 MOD_C1_ENDLOCK = 14, /**< inside multicore_lockout_end_blocking (untimed mutex) */
 MOD_C1_RESET = 15,   /**< inside multicore_reset_core1 (ends in untimed fifo pop) */
 MOD_C1_LAUNCH = 16,  /**< inside multicore_launch_core1 (untimed echo handshake) */
 /* The kill path returned, and the main loop has not reached its next marker.
  * Before these two, that whole window traced as whatever the caller left
  * behind, so "hung while killing Core 1" and "hung right after" were the same
  * reading. MOD_LOOP also separates a stall in loop( ) from one in setup( ),
  * which MOD_BOOT alone could not do. */
 MOD_C1_KILLED = 17,  /**< Core 1 killed and relaunched; back in the caller */
 MOD_LOOP = 18,       /**< top of AppManager::loop, before the first task marker */
 /* Inside MOD_WEB_SERVER. The first trustworthy autopsy put the Core-0 stall
  * here — not in the Core-1 kill path — and MOD_WEB_SERVER covers the whole of
  * WebManager::update( ), which is four handleClient( ) calls plus whatever
  * handler they dispatch. These three split that: server/lwIP plumbing, our
  * history handler, and the send itself. */
 MOD_WEB_POLL = 19,   /**< inside WebServer::handleClient — accept/parse/dispatch */
 MOD_WEB_HIST = 20,   /**< inside handleApiHistoryMulti (read + decimate) */
 MOD_WEB_SEND = 21,   /**< inside sendContent, under the SendGuard */
 MOD_WEB_HSCAN = 22,  /**< history file list + decimation estimate, before any send */
 /* Inside MOD_TELEMETRY. Naming the whole cycle is not actionable: it is a
  * history scan, a payload build and a network POST, with different fixes. */
 MOD_TEL_COLLECT = 23, /**< inside collectBatch — the per-file history scan */
 MOD_TEL_BUILD = 24,   /**< inside buildPayload */
 MOD_TEL_SEND = 25     /**< inside attemptHttpUpload / attemptMqttPublish */
};

/** Structured log event codes for machine-parseable system logging. */
enum LogCode {
 SYS_OK = 0,
 SYS_BOOT = 1,
 SYS_REBOOT_USER = 2,
 SYS_HEAP_LOW = 3,
 SYS_UPTIME_MARK = 4,

 SYS_WIFI_CONNECT = 10,
 SYS_WIFI_DISCONNECT = 11,
 SYS_WIFI_SCAN = 12,
 SYS_NTP_SYNC = 13,
 SYS_IP_ACQUIRED = 14,
 SYS_AP_START = 15,

 SYS_STORAGE_FAIL = 20,
 SYS_STORAGE_SAVE = 21,
 SYS_STORAGE_ROTATE = 22,
 SYS_STORAGE_FORMAT = 23,
 SYS_STORAGE_RECOVER = 24,
 SYS_STORAGE_MIGRATED = 25,

 SYS_TEL_SENT = 30,
 SYS_TEL_FAIL = 31,
 SYS_TEL_RETRY = 32,
 SYS_TEL_QUEUE = 33,
 SYS_TEL_SSL = 34,
 SYS_TEL_MQTT_CONN = 35,
 SYS_TEL_MQTT_DISC = 36,
 SYS_TEL_MQTT_PUB = 37,

 LOG_SENSOR_REC = 100,
 ERR_SENSOR_TIMEOUT = 101,
 ERR_SENSOR_CHECKSUM = 102,
 ERR_SENSOR_CRC = 103,
 ERR_SENSOR_RANGE = 104,
 ERR_SENSOR_MISMATCH = 105,
 ERR_SENSOR_MISSING = 106,

 EVT_UI_TOUCH = 200,
 EVT_DISPLAY_RESTART = 201,
 EVT_GRAPH_RENDER = 202,

 SEC_LOGIN_SUCCESS = 300,
 SEC_LOGIN_FAIL = 301,
 SEC_UNAUTHORIZED = 302,
 SEC_CONFIG_CHANGED = 303,
 SEC_SESSION_EXPIRE = 304,
 SEC_FILE_UPLOAD = 305,
 SEC_FILE_DELETE = 306,


 /* ── Application lifecycle (400–439) ── */
 APP_DISPLAY_LAUNCHED = 400,
 APP_TOUCH_CAL_INITIAL = 401,
 APP_TOUCH_CAL_REQUIRED = 402,
 APP_AP_MODE_TRIGGERED = 403,
 APP_READY = 404,
 APP_READY_AP = 405,
 APP_STORAGE_CRITICAL = 406,
 APP_SENSORS_CALIBRATED = 407,
 APP_NTP_CORRECTING = 408,
 APP_NTP_CORRECTED = 409,
 APP_CACHE_INVALIDATED = 410,

 /* ── Application UI events (440–469) ── */
 APP_UI_THEME_CHANGED = 440,
 APP_UI_LANG_CHANGED = 441,
 APP_UI_ALARM_SAVED = 442,
 APP_UI_TOUCH_CAL_SAVED = 443,
 APP_UI_TOUCH_SENS_SAVED = 444,
 APP_UI_PIN_CHANGED = 445,
 APP_UI_SOUND_SAVED = 446,
 APP_UI_ALARM_SILENCED = 447,
 APP_UI_ALARM_SILENCE_EXP= 448,
 APP_UI_ALARM_DEACTIVATED= 449,

 /* ── Alarm state (470–479) ── */
 APP_ALARM_TRIGGERED = 470,
 APP_ALARM_CLEARED = 471,
 APP_ALARM_SILENCE_CANCEL= 472,

 /* ── Cache/graph (480–499) ── */
 APP_CACHE_MINMAX_FULL = 480,
 APP_CACHE_MINMAX_PARTIAL= 481,
 APP_CACHE_GRAPH_STARTED = 482,
 APP_CACHE_GRAPH_DONE = 483,
 APP_CACHE_GRAPH_AMBIENT = 484,
 APP_CACHE_GRAPH_BOARD = 485,
 APP_CACHE_PRELOAD_DONE = 486,
 APP_GRAPH_LOADING = 487,
 APP_GRAPH_BUDGET = 488,
 APP_PRELOAD_BUDGET = 489,

 /* ── Safety watchdogs (500–509) ── */
 APP_DISPLAY_PAUSE_STUCK = 500,
 APP_YIELD_STUCK = 501,
 APP_CORE1_DEAD = 502,
 APP_FLASH_BUSY = 503,

 /* ── History (510–514) ── */
 APP_HISTORY_SAVED = 510,
 APP_HEAP_REPORT = 511,
 APP_HIST_NO_TIME_REF = 512, /* skip due to missing NTP/provisional */
 APP_HIST_TIME_REF_RECOVERED = 513, /* time ref returned, resuming saves */
 APP_HIST_NO_SCHEMA = 514, /* V4 schema has zero measurements — no active sensors? */
 APP_HIST_SCHEMA_MISMATCH = 515, /* schema covers none of the configured sensors */

 /* ── Network extended (520–539) ── */
 NET_DHCP_MODE = 520,
 NET_STATIC_MODE = 521,
 NET_STARTING = 522,
 NET_SSID_MISSING = 523,
 NET_PROVISIONAL_TIME = 524,
 NET_CONNECT_TIMEOUT = 525,
 NET_DORMANT_MODE = 526,
 NET_SHOW_IP = 527,
 NET_MDNS_FAIL = 528,   /* was logged as LOG_ERROR + SYS_OK, i.e. persisted as "OK" */

 /* ── Telemetry extended (540–559) ── */
 TEL_HTTP_INIT = 540,
 TEL_MQTT_INIT = 541,
 TEL_MQTT_CONNECTING = 542,
 TEL_CERT_EMPTY = 543,
 TEL_CERT_READ_ERR = 544,
 TEL_CERT_MISSING = 545,
 TEL_FORCE_SYNC = 546,
 TEL_BACKOFF_SUPPRESSED = 547,

 /* ── Storage extended (560–569) ── */
 STO_WRITE_FAILED = 560,
 STO_CORRECT_BUDGET = 561,
 STO_ENFORCE_BUDGET = 562,
 STO_ENFORCE_SKIP_ACTIVE = 563,
 STO_STATS_REPORT = 564,
 STO_CONFIG_REPORT = 565,

 /* ── Web server (570–579) ── */
 WEB_SERVER_STARTED = 570,
 WEB_DISCONNECT_FILE = 571,
 WEB_DISCONNECT_HISTORY = 572,
 WEB_SCREENSHOT_ABORTED = 573,
 WEB_UPLOAD = 574,
 WEB_CLIENT_DISCONNECT = 575, /* client closed connection during safeSend — silent broken pipe now visible */

 /* ── Config (580–584) ── */
 CFG_THEME_APPLIED = 580,
 CFG_THEME_NOT_FOUND = 581,

 /* ── CLI (585–589) ── */
 CLI_UNKNOWN_CMD = 585,

 /* ── Sensors extended (590–599) ── */
 SENSOR_RUNTIME_LOADED = 590,

 /* ── Display (600–604) ── */
 DSP_FORCE_UNPAUSE = 600,

 ERR_UNKNOWN = 999
};

/* =========================================================================== */
/* COMPACT BINARY LOG RECORD — 12 bytes */
/* =========================================================================== */

/**
 * @brief Module/tag identifiers for compact logging (4 bits, max 16).
 *
 * Each log tag ("APP", "NET", etc.) maps to a numeric ID
 * stored in the flags field of CompactLogRecord.
 */
enum LogTagId : uint8_t {
 TAG_APP = 0,
 TAG_NET = 1,
 TAG_TEL = 2,
 TAG_STO = 3,
 TAG_WEB = 4,
 TAG_CFG = 5,
 TAG_CLI = 6,
 TAG_SENSOR = 7,
 TAG_HIST = 8,
 TAG_SYS = 9,
 TAG_DSP = 10,
 TAG_SEC = 11,
 TAG_OTA = 12,   /* OTA records used to land on TAG_UNKNOWN: tagStringToId had no 'O' case */
 TAG_UNKNOWN= 15
};

/** @brief Convert tag string to LogTagId. */
inline LogTagId tagStringToId(const char* tag) {
 if (!tag) return TAG_UNKNOWN;
 switch (tag[0]) {
 case 'A': return TAG_APP;
 case 'N': return TAG_NET;
 case 'T': return TAG_TEL;
 case 'W': return TAG_WEB;
 case 'O': return TAG_OTA;
 case 'C': return (tag[1] == 'F') ? TAG_CFG : TAG_CLI;
 case 'S':
 /* 'S' is the crowded prefix: STO, SYS, SEC, SENSOR. "SEC" and "SENSOR"
  * both have tag[1] == 'E', and the old chain answered TAG_SENSOR for
  * both — so every security/audit record was persisted under the sensor
  * tag, and TAG_SEC was unreachable. The third character separates them;
  * reading tag[2] is safe because both literals are longer than that. */
 if (tag[1] == 'T') return TAG_STO;
 if (tag[1] == 'Y') return TAG_SYS;
 if (tag[1] == 'E') return (tag[2] == 'C') ? TAG_SEC : TAG_SENSOR;
 return TAG_UNKNOWN;
 case 'H': return TAG_HIST;
 case 'D': return TAG_DSP;
 default: return TAG_UNKNOWN;
 }
}

/** @brief Convert LogTagId back to string (for display). */
inline const char* tagIdToString(uint8_t id) {
 static const char* const TAG_NAMES[] = {
 "APP", "NET", "TEL", "STO", "WEB", "CFG", "CLI",
 "SENSOR", "HIST", "SYS", "DSP", "SEC",
 "OTA", "?", "?", "?"
 };
 return (id < 16) ? TAG_NAMES[id] : "?";
}

/**
 * @brief Binary log record — 12 bytes, packed.
 *
 * Replaces CSV lines (~100-200 bytes each) with a fixed format
 * that stores only structured code + numeric context.
 * The human-readable message is reconstructed on demand via a translation table.
 *
 * Layout:
 * [0..3] epoch uint32_t Unix timestamp
 * [4..5] uptimeHr uint16_t Uptime in hours (covers ~7.5 years — wrap-safe)
 * [6..7] code uint16_t LogCode enum
 * [8..9] context int16_t Contextual value (GPIO, delta, count...)
 * [10] flags uint8_t [level:3 | core:1 | tagId:4]
 * [11] reserved uint8_t Reserved (padding)
 *
 * Savings: ~90-95% vs previous CSV format.
 */
struct __attribute__((packed)) CompactLogRecord {
 uint32_t epoch;
 uint16_t uptimeHr; /**< Uptime in hours (millis()/3600000); 65535h ≈ 7.5 years. */
 uint16_t code;
 int16_t context;
 uint8_t flags;
 uint8_t reserved;

 /** @brief Packs level, core and tagId into the flags field. */
 static inline uint8_t packFlags(uint8_t level, uint8_t core, uint8_t tagId) {
 return ((level & 0x07) << 5) | ((core & 0x01) << 4) | (tagId & 0x0F);
 }

 inline uint8_t getLevel( ) const { return (flags >> 5) & 0x07; }
 inline uint8_t getCore( ) const { return (flags >> 4) & 0x01; }
 inline uint8_t getTagId( ) const { return flags & 0x0F; }
};
static_assert(sizeof(CompactLogRecord) == 12, "CompactLogRecord must be 12 bytes!");

/** @brief Size of each log record in flash (bytes). */
#define LOG_RECORD_SIZE 12
