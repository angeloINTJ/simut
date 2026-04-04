/**
 * @file    SystemDefs.h
 * @brief   Global type definitions, enumerations, structs, and shared utilities.
 * @details Central header shared by all modules. Defines hardware limits,
 *          permission bitmasks, sensor/config/UI structures, CSV parser,
 *          input validation helpers, and the CLI command architecture.
 *          All packed structs use __attribute__((packed)) for binary storage.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once
#include <Arduino.h>

/* =========================================================================== */
/*                        HARDWARE AND SYSTEM LIMITS                         */
/* =========================================================================== */

#define MAX_SENSORS         10      /* Maximum number of configurable sensor slots      */
#define MAX_USERS           5       /* Maximum user accounts (Flash/RAM budget)          */
#define MOVING_AVG_WINDOW   10      /* Samples in the trimmed-mean sliding window        */
#define SIMUT_VERSION       "v3.3.10" /* Firmware version string                         */
#define GRAPH_WIDTH         260     /* Maximum data points on the TFT graph              */

/* =========================================================================== */
/*                     ROLE-BASED ACCESS CONTROL (RBAC)                      */
/* =========================================================================== */

#define PERM_DASHBOARD      0x0001
#define PERM_HISTORY        0x0002
#define PERM_LOGS           0x0004
#define PERM_SYS_CONFIG     0x0008
#define PERM_NET_CONFIG     0x0010
#define PERM_FILE_READ      0x0020
#define PERM_FILE_UPLOAD    0x0040
#define PERM_FILE_DELETE    0x0080
#define PERM_USER_MGR       0x0100
#define PERM_FULL_ADMIN     0xFFFF

/* =========================================================================== */
/*                   BLACK BOX PROFILER — MODULE TRACKING                    */
/* =========================================================================== */

/** Identifiers for the per-core module profiler (crash forensics). */
enum TraceModule {
    MOD_BOOT            = 0,
    MOD_IDLE            = 1,
    MOD_WIFI            = 2,
    MOD_WEB_SERVER      = 3,
    MOD_STORAGE_READ    = 4,
    MOD_STORAGE_WRITE   = 5,
    MOD_SENSOR_READ     = 6,
    MOD_TELEMETRY       = 7,
    MOD_DISPLAY         = 8,
    MOD_CLI             = 9
};

/* =========================================================================== */
/*                            SYSTEM ENUMERATIONS                            */
/* =========================================================================== */

/** Physical sensor type detected during hardware scan. */
enum SensorType {
    TYPE_NONE,
    TYPE_DS18B20,
    TYPE_DHT22,
    TYPE_UNKNOWN_ACTIVITY
};

/** Payload format for telemetry uploads. */
enum TelemetryMode {
    TEL_MODE_JSON   = 0,
    TEL_MODE_CSV    = 1,
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

    ERR_UNKNOWN         = 999
};

/** Active UI screen on the TFT display (Core 1 state). */
enum UiMode {
    MODE_DASHBOARD,
    MODE_STATS_VIEW,
    MODE_GRAPH_LOADING,
    MODE_GRAPH_VIEW,
    MODE_AUTH,
    MODE_SETTINGS_MAIN,
    MODE_SETTINGS_THEMES,
    MODE_SETTINGS_ALARMS,
    MODE_SETTINGS_ALARM_EDIT,
    MODE_SETTINGS_LANG,
    MODE_SETTINGS_PASSWORD,
    MODE_SETTINGS_TOUCH_CAL,
    MODE_SETTINGS_SOUNDS,
    MODE_SETTINGS_LICENSE,
    MODE_ALARM_ACTION
};

/** Time range selection for graph rendering. */
enum GraphRange {
    RANGE_1H = 0,
    RANGE_6H,
    RANGE_12H,
    RANGE_24H,
    RANGE_1W
};

/* =========================================================================== */
/*                        PERSISTENT DATA STRUCTURES                         */
/* =========================================================================== */

/** Persistent sensor configuration stored in Flash (binary, packed). */
struct __attribute__((packed)) SensorRecord {
    bool     active;
    uint8_t  gpio;
    uint8_t  rom[8];
    char     hwId[16];
    char     friendlyName[32];
    uint32_t provisionEpoch;
    float    tempMin;
    float    tempMax;
    float    humMin;
    float    humMax;
    bool     alarmsActive;
};

/** User account for web interface authentication (packed for Flash storage). */
struct __attribute__((packed)) UserAccount {
    bool     active;
    char     username[16];
    char     password[32];
    uint16_t permissions;
    bool     mustChangePassword;
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
};
static_assert(sizeof(TouchCalData) <= 56, "TouchCalData exceeds reserved[]!");

/**
 * Master system configuration — persisted to Flash as a binary blob
 * with CRC32 integrity check and dual-bank backup.
 */
struct __attribute__((packed)) SystemConfig {
    uint32_t    magic;
    uint16_t    version;
    char        deviceName[32];

    /* WiFi settings */
    char        wifiSsid[32];
    char        wifiPass[32];
    bool        useDhcp;
    char        staticIp[16];
    char        staticMask[16];
    char        staticGateway[16];
    char        staticDns[16];

    /* Web server */
    bool        useHttps;
    UserAccount users[MAX_USERS];

    /* Telemetry — HTTP */
    char        telServer[64];
    uint16_t    telPort;
    char        telPath[32];
    char        telApiKey[64];
    uint32_t    telInterval;
    uint8_t     telBatchSize;
    bool        telEncryption;
    uint8_t     telMode;
    char        telGlobalTemplate[256];
    char        telLineTemplate[512];
    char        telLineSeparator[8];

    /* Telemetry — MQTT */
    uint8_t     telTransport;
    char        mqttTopic[64];
    char        mqttUser[32];
    char        mqttPass[32];
    uint8_t     mqttQos;
    bool        mqttRetain;
    char        mqttClientId[24];
    uint16_t    mqttKeepAlive;

    /* General settings */
    int8_t      timezoneOffset;
    uint32_t    sampleIntervalMs;
    bool        loggingEnabled;
    uint8_t     ds18Resolution;

    /* Sensor slots + ambient sensor */
    SensorRecord sensors[MAX_SENSORS];
    SensorRecord ambientSensor;

    /* Display settings */
    int8_t      themeIndex;
    char        displayPin[8];
    uint8_t     displayLang;

    /* Reserved bytes for TouchCalData + SoundConfigData */
    uint8_t     reserved[56];
};

/* =========================================================================== */
/*                             SHARED UTILITIES                              */
/* =========================================================================== */

/** Compute CRC8 Dallas/Maxim checksum for 1-Wire ROM validation. */
uint8_t dallasCrc8(const uint8_t *addr, uint8_t len);

/** Validate history filename format (YYYYMMDD.csv, 12 chars). */
bool isValidHistoryFileName(const char* name);

/** Result of a hardware sensor scan on a GPIO pin. */
struct ScanResult {
    uint8_t    pin;
    SensorType type;
    uint8_t    rom[8];
};

/** Single sensor reading result (temperature and optional humidity). */
struct SensorReading {
    float   value1;
    float   value2;
    bool    isValid;
    char    typeName[10];
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
        EVT_ALARM_OPEN_MINMAX
    };
    EventType type;
    int       id;
    int       param;
};

/** Data package for rendering a temperature/humidity graph on the TFT. */
struct GraphDataPackage {
    int     sensorIdx;
    int     timeRange;
    char    title[32];
    char    hwId[16];
    char    rom[24];
    float   pointsV1[GRAPH_WIDTH];
    float   pointsV2[GRAPH_WIDTH];
    int     count;
    float   minVal;
    float   maxVal;
    bool    hasHumidity;
};

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
    CMD_SET_TEL_SERVER,
    CMD_SET_TEL_PORT,
    CMD_SET_TEL_PATH,
    CMD_SET_TEL_BATCH,
    CMD_SET_TEL_INTERVAL,
    CMD_SET_TEL_CRYPTO,
    CMD_SET_TEL_MODE,
    CMD_RESET_ADMIN,
    CMD_DEFINE_SENSOR,
    CMD_WIPE_SENSOR,
    CMD_ACCEPT_SENSOR,
    CMD_SCAN_SENSORS,
    CMD_WRITE_MEMORY,
    CMD_CLEAR_LOGS,
    CMD_RELOAD,
    CMD_TEL_SYNC
};

/** Parsed CLI command with typed payload fields. */
struct CliDemand {
    DemandType type;
    String     strVal1;
    String     strVal2;
    int        intVal1;
    bool       boolVal;
    uint8_t    rom[8];
};

/* =========================================================================== */
/*                          CENTRALIZED CSV PARSER                           */
/* =========================================================================== */

/**
 * Centralized CSV history line parser.
 * Eliminates duplication between AppManager, WebManager, and TelemetryManager.
 * Format: epoch;ambT;ambH;s0;s1;...;s9
 */
struct CsvHistoryLine {
    time_t   timestamp;
    float    ambientTemp;
    float    ambientHum;
    float    sensorValues[MAX_SENSORS];
    bool     valid;

    /** Parse a single CSV history line into structured data. */
    static CsvHistoryLine parse(const char* line) {
        CsvHistoryLine out;
        out.valid       = false;
        out.ambientTemp = NAN;
        out.ambientHum  = NAN;
        for (int i = 0; i < MAX_SENSORS; i++) out.sensorValues[i] = NAN;

        if (!line || *line == '\0') return out;

        const char* ptr = line;
        char* endPtr;

        /* Token 0: timestamp (epoch) */
        out.timestamp = strtoul(ptr, &endPtr, 10);
        if (ptr == endPtr || out.timestamp == 0) return out;
        if (*endPtr == ';') ptr = endPtr + 1; else return out;

        /* Token 1: ambient temperature */
        if (*ptr && *ptr != ';') {
            out.ambientTemp = strtof(ptr, &endPtr);
            if (ptr == endPtr) out.ambientTemp = NAN;
            ptr = endPtr;
        }
        if (*ptr == ';') ptr++; else if (*ptr != '\0') return out;

        /* Token 2: ambient humidity */
        if (*ptr && *ptr != ';') {
            out.ambientHum = strtof(ptr, &endPtr);
            if (ptr == endPtr) out.ambientHum = NAN;
            ptr = endPtr;
        }
        if (*ptr == ';') ptr++; else if (*ptr == '\0') { out.valid = true; return out; }

        /* Tokens 3..12: sensor slot values */
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (*ptr == '\0') break;
            if (*ptr != ';') {
                out.sensorValues[i] = strtof(ptr, &endPtr);
                if (ptr == endPtr) out.sensorValues[i] = NAN;
                ptr = endPtr;
            }
            if (*ptr == ';') ptr++;
        }

        out.valid = true;
        return out;
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
