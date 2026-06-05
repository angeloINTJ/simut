/**
 * @file SystemDefs_Records.h
 * @brief Persistent + runtime structs: SystemConfig, sensors, UI events, history.
 * @details Domain enums (SensorType, TelemetryMode, UiMode, GraphRange),
 * structs persisted in flash (SensorRecord, UserAccount, SystemConfig
 * and overlays in reserved[]), runtime structs (UiEvent, GraphDataPackage,
 * SystemStatusData, ScanResult, SensorReading) and BinaryHistoryRecord.
 * Forward decls of shared utilities (dallasCrc8, isValidHistoryFileName).
 * Sub-header of SystemDefs.h (facade).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <math.h>
#include <time.h>
#include "SystemDefs_Limits.h"

/** Physical sensor type detected during hardware scan. */
/** Physical sensor type detected during hardware scan.
 * All types are always present in the enum so consumer code compiles
 * regardless of SIMUT_SENSOR_* flags. The flags only gate driver code
 * in SensorManager and control sensorTypeEnabled() at runtime. */
enum SensorType {
 TYPE_NONE,
 TYPE_DS18B20,
 TYPE_DHT22,
 TYPE_BME280,
 TYPE_UNKNOWN_ACTIVITY
};

/** Payload format for telemetry uploads. */
enum TelemetryMode {
 TEL_MODE_JSON = 0,
 TEL_MODE_CSV = 1,
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
 * LANG_COUNT is a sentinel that ties the enum to the actual array size
 * in DisplayManager.cpp via static_assert, forcing joint update if
 * future languages are added.
 */
enum LanguageCode {
 LANG_EN = 0,
 LANG_PT = 1,
 LANG_COUNT = 2 /**< Sentinel — total supported languages. */
};

/** Active UI screen on the TFT display (Core 1 state). */
enum UiMode {
 MODE_DASHBOARD,
 MODE_STATS_VIEW,
 MODE_GRAPH_LOADING,
 MODE_GRAPH_VIEW,
 MODE_GRAPH_DETAIL, /**< Numeric graph detail screen */
 MODE_AUTH,
 MODE_SETTINGS_MAIN,
 MODE_SETTINGS_THEMES,
 MODE_SETTINGS_ALARMS,
 MODE_SETTINGS_ALARM_EDIT,
 MODE_SETTINGS_LANG,
 MODE_SETTINGS_PASSWORD,
 MODE_SETTINGS_TOUCH_CAL,
 MODE_SETTINGS_TOUCH_SENS, /**< Touch sensitivity calibration */
 MODE_SETTINGS_SOUNDS,
 MODE_SETTINGS_LICENSE,
 MODE_SETTINGS_STATUS, /**< Real-time system status screen */
 MODE_SETTINGS_DISPLAY_OFFSET, /**< LCD position adjustment (±4H/±4V) */
 MODE_ALARM_ACTION,
 MODE_CALENDAR, /**< History calendar */
 MODE_CONFIRM_MUTE_ALL, /**< Confirms Global Mute activation */
 MODE_PRESETS_DEMO /**< Sensor preset browser (CLI: display presets) */
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
 uint8_t sensorType; /**< SensorType enum — explicit, not inferred from ROM. */
 uint8_t pins[MAX_SENSOR_PINS]; /**< Up to 4 GPIOs (255=PIN_UNUSED). pins[0] is primary. */
 uint8_t rom[8]; /**< DS18B20 64-bit ROM (zero for non-1-Wire sensors). */
 char hwId[16];
 char friendlyName[32];
 uint32_t provisionEpoch;


 float tempMin;
 float tempMax;
 float humMin;
 float humMax;
 bool alarmsActive;
};
/* Locks layout at compile-time. Deliberate schema bump
 * must touch HERE AND migration code; build breaks if someone adds/removes
 * a field without considering existing flash consequences. */
static_assert(sizeof(SensorRecord) == 83, "SensorRecord v16 must be 83 bytes - update migration if changing");

/**
 * User account for web interface authentication (packed for Flash storage).
 *
 * Added salt[8] and hashVersion to support the new hashing scheme
 * with transparent migration. Older configs are migrated via
 * StorageManager::loadAndMigrateV14: users stay in legacy mode
 * (salt = {0}, hashVersion = 0), keeping the stored hash valid.
 * password grew from [32] to [33] to fit 32 hex chars + null (128 bits)
 * of the v1 scheme.
 */
struct __attribute__((packed)) UserAccount {
 bool active;
 char username[16];
 char password[33]; /**< Hex string of hash; ≤32 chars + null. */
 uint16_t permissions;
 bool mustChangePassword;
 uint8_t salt[8]; /**< Per-user random salt. {0} = legacy mode. */
 uint8_t hashVersion; /**< 0=legacy (2500r/120b/username-salt), 1=v1 (PASSWORD_HMAC_ROUNDS/128b/random-salt). */
};
static_assert(sizeof(UserAccount) == 62, "UserAccount v15 must be 62 bytes (packed)");


/**
 * Touchscreen calibration data stored in SystemConfig::reserved[0..9].
 * magic == 0xCA indicates valid calibration.
 */
struct __attribute__((packed)) TouchCalData {
 uint8_t magic;
 uint8_t flags;
 int16_t xMin;
 int16_t xMax;
 int16_t yMin;
 int16_t yMax;
 uint16_t zThreshold; /**< Pressure threshold (default 400, calibratable) */
};
static_assert(sizeof(TouchCalData) <= 24, "TouchCalData exceeds reserved[]!");

/**
 * Display alignment offset persisted in SystemConfig::reserved[sizeof(TouchCalData)+sizeof(SoundConfigData) .. +3].
 * magic == 0xD0 indicates saved offset; otherwise uses default (0,0).
 * Each axis limited to [-4, +4] pixels — compensates small misalignments
 * of the TFT ILI9341 viewing window without requiring significant content clipping.
 */
struct __attribute__((packed)) DisplayOffsetData {
 uint8_t magic; /**< 0xD0 = valid; other value = default */
 int8_t offsetX; /**< -4..+4 pixels (horizontal) */
 int8_t offsetY; /**< -4..+4 pixels (vertical) */
 uint8_t reserved; /**< padding; reserved for future extension */
};
static_assert(sizeof(DisplayOffsetData) == 4, "DisplayOffsetData must be 4 bytes!");

/**
 * CLI session config persisted in SystemConfig::reserved[22..23].
 * magic == 0xDB indicates valid data; other value = default (debug OFF).
 * Located after TouchCalData(12) + SoundConfigData(6) + DisplayOffsetData(4) = offset 22.
 */
struct __attribute__((packed)) CliConfigData {
 uint8_t magic; /**< 0xDB = valid; other value = default (debug OFF) */
 uint8_t debugMode; /**< 0 = CONFIG (silent), 1 = DEBUG (log stream) */
};
static_assert(sizeof(CliConfigData) == 2, "CliConfigData must be 2 bytes!");
#define CLI_CONFIG_MAGIC 0xDB
#define CLI_CONFIG_OFFSET 22 /* sizeof(TouchCalData)+sizeof(SoundConfigData)+sizeof(DisplayOffsetData) */

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

 char ntpServer[32]; /**< Configurable NTP server (default: pool.ntp.org) */
 /* reserved[] layout:
 * [ 0..11] TouchCalData (12 B)
 * [12..17] SoundConfigData (6 B)
 * [18..21] DisplayOffsetData (4 B)
 * [22..23] CliConfigData (2 B)
 * [24..25] WebConfigData (2 B — web server port)
 * [26..27] SetupFlagsData (2 B — mustChangePin)
 * [28..47] NetworkTimeData (20 B — DNS auto/manual + NTP toggle + secondary DNS)
 * [48..63] free for future expansion
 */
 uint8_t reserved[64];
};
/* Locks SystemConfig layout. Adding a field without
 * CONFIG_VERSION bump + migration = corrupts existing flash; the assert forces
 * the author to intentionally touch attemptLoad. */
static_assert(sizeof(SystemConfig) > 0,
 "Empty SystemConfig? Revert — persistent flash schema needs stability");

/** Overlay in reserved[24..25]: web server configuration. */
struct __attribute__((packed)) WebConfigData {
 uint16_t port; /**< Web server TCP port. 0 = use default (80). */
};
constexpr size_t WEB_CONFIG_OFFSET = 24;
constexpr uint16_t WEB_DEFAULT_PORT = 80;

/**
 * @brief Setup flags overlay in reserved[26..27].
 *
 * Currently used only for FLAG_MUST_CHANGE_PIN, which forces the user to change
 * the default PIN 1234 on first access to the settings menu.
 * magic == SETUP_FLAGS_MAGIC validates that the overlay was initialized
 * (distinct from legacy zeroed bytes).
 *
 * Free bits in flags for future setup flags (e.g. mustChangeBtPin).
 */
struct __attribute__((packed)) SetupFlagsData {
 uint8_t magic; /**< 0xBE = initialized; 0x00/other = legacy. */
 uint8_t flags; /**< bitmask of FLAG_MUST_* */
};
constexpr size_t SETUP_FLAGS_OFFSET = 26;
constexpr uint8_t SETUP_FLAGS_MAGIC = 0xBE;
constexpr uint8_t FLAG_MUST_CHANGE_PIN = 0x01;

static_assert(sizeof(SetupFlagsData) == 2, "SetupFlagsData must be 2 bytes");

/**
 * @brief Network/time flags overlay in reserved[28..47].
 *
 * Decouples DNS from DHCP and enables NTP toggle without requiring
 * a CONFIG_VERSION bump. Legacy configs (magic absent) return backward-
 * compatible defaults (DNS auto + NTP ON) — identical to previous behavior.
 *
 * flags uses "bit=1 means AUTO/default" pattern: this way configs
 * with zeroed bytes (no magic) fall into the magic-check path before reading
 * flags, guaranteeing correct defaults.
 *
 * dns2[16] stores the secondary manual DNS (when DNS_AUTO=false).
 * dns1 reuses the existing field SystemConfig::staticDns.
 */
struct __attribute__((packed)) NetworkTimeData {
 uint8_t magic; /**< 0xCE = valid; other = legacy (returns defaults). */
 uint8_t flags; /**< bitmask of FLAG_DNS_AUTO, FLAG_NTP_ENABLED. */
 char dns2[16]; /**< Secondary manual DNS; "" if not configured. */
 uint8_t pad[2]; /**< Reserved for future extension. */
};
constexpr size_t NETTIME_OFFSET = 28;
constexpr uint8_t NETTIME_MAGIC = 0xCE;
constexpr uint8_t FLAG_DNS_AUTO = 0x01; /**< 1 = DNS via DHCP (default). */
constexpr uint8_t FLAG_NTP_ENABLED = 0x02; /**< 1 = NTP sync active (default). */

static_assert(sizeof(NetworkTimeData) == 20, "NetworkTimeData must be 20 bytes");

/**
 * @brief History recording interval overlay in reserved[48..51].
 *
 * Granularity in minutes (1..1440 = 1 min..24 h). Legacy configs without
 * magic return default 1 min — identical to the previous hardcoded
 * behavior. uint16 fits up to 65535 min (~45 days) with margin.
 */
struct __attribute__((packed)) HistoryConfigData {
 uint8_t magic; /**< 0xDC = initialized; other = legacy (default 1 min). */
 uint8_t pad;
 uint16_t intervalMin; /**< 1..1440. */
};
constexpr size_t HISTORY_CONFIG_OFFSET = 48;
constexpr uint8_t HISTORY_CONFIG_MAGIC = 0xDC;
constexpr uint16_t HISTORY_INTERVAL_DEFAULT_MIN = 1;
constexpr uint16_t HISTORY_INTERVAL_MIN_MIN = 1;
constexpr uint16_t HISTORY_INTERVAL_MAX_MIN = 1440;
static_assert(sizeof(HistoryConfigData) == 4, "HistoryConfigData must be 4 bytes");

/** Size of reserved[] field in v12 configs — used in migration. */
#define CONFIG_V12_RESERVED_SIZE 24

/* =========================================================================== */
/* SHARED UTILITIES */
/* =========================================================================== */

/** Compute CRC8 Dallas/Maxim checksum for 1-Wire ROM validation. */
uint8_t dallasCrc8(const uint8_t *addr, uint8_t len);


/** Validate history filename format (YYYYMMDD.bin, 12 chars). */
bool isValidHistoryFileName(const char* name);

/* CRC32-IEEE-802.3 incremental (reverse polynomial 0xEDB88320). Compatible with
 * StorageManager::calculateCRC32 (same math) but exposed in 3 phases
 * to allow streaming without materializing the entire blob in RAM. Usage:
 * uint32_t c = crc32_init();
 * c = crc32_update(c, chunk1, len1);
 * c = crc32_update(c, chunk2, len2);
 * uint32_t final = crc32_final(c);
 */
uint32_t crc32_init( );
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
 EVT_APPLY_DISPLAY_OFFSET, /**< Applies LCD position adjustment and restarts touch calibration */
 EVT_OPEN_CALENDAR, /**< Requests calendar open */
 EVT_GRAPH_NAV, /**< Navigate graph: param = -1 (◀) or +1 (▶) */
 EVT_CALENDAR_DAY, /**< Selected day: param = day (1-31) */
 EVT_CALENDAR_MONTH /**< Month change: param = -1 or +1 */
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
 uint32_t tsPoints[GRAPH_WIDTH]; /**< Epoch of each point (temporal position on X axis) */
 int count;
 float minVal; /**< Min of displayed points (decimated) */
 float maxVal; /**< Max of displayed points (decimated) */
 bool hasHumidity;

 /* REAL min/max — calculated from ALL records in the window,
 * not just the decimated points for display.
 * Used for Y-axis scale and extreme value badges. */
 float realMinVal; /**< Real minimum temperature in the window */
 float realMaxVal; /**< Real maximum temperature in the window */
 time_t tsRealMin; /**< Epoch of real minimum temperature */
 time_t tsRealMax; /**< Epoch of real maximum temperature */

 /* Indices of DISPLAYED extreme points for markers on the graph */
 int idxMinTemp; /**< Index of minimum temperature point */
 int idxMaxTemp; /**< Index of maximum temperature point */

 /* Requested time window — defines total X-axis width */
 time_t tsCutoff; /**< Epoch of window start (cutoff) */
 time_t tsEnd; /**< Epoch of window end (effectiveEnd) */

 /**
 * Timestamps for display.
 * tsFirst/tsLast are now derived from tsPoints[0] and tsPoints[count-1].
 */
 time_t tsFirst; /**< Epoch of first point */
 time_t tsMid; /**< Epoch of center point */
 time_t tsLast; /**< Epoch of last point */
 time_t tsMaxTemp; /**< Epoch of displayed maximum temperature */
 time_t tsMinTemp; /**< Epoch of displayed minimum temperature */
 time_t tsMaxHum; /**< Epoch of maximum humidity */
 time_t tsMinHum; /**< Epoch of minimum humidity */

 /* Statistics calculated during data parsing */
 float avgTemp; /**< Arithmetic mean of temperature */
 float stdTemp; /**< Standard deviation of temperature */
 float deltaTemp; /**< Variation (last - first point) */
 float avgHum; /**< Arithmetic mean of humidity */
 float stdHum; /**< Standard deviation of humidity */
 float deltaHum; /**< Humidity variation */
};


/** System status data for real-time display. */
struct SystemStatusData {
 /* System */
 uint32_t uptimeSec;
 uint32_t heapFree;
 uint32_t heapTotal;
 float boardTemp;
 uint32_t flashUsed;
 uint32_t flashTotal;
 char fwVersion[16];
 char deviceName[32];

 /* Network */
 bool wifiConnected;
 int32_t rssi;
 char ip[16];
 char mac[18];
 char ssid[33];
 bool ntpSynced;
 char ntpServer[48];
 int8_t timezone;

 /* Telemetry */
 uint16_t telPending;
 bool mqttConnected;
 uint8_t telTransport;
 uint8_t telFails;
 uint32_t telInterval;
 char telServer[64];

 /* Sensors */
 uint8_t activeSensors;
 float ambientTemp;
 float ambientHum;
 bool ambientValid;
};


/* =========================================================================== */
/* BINARY HISTORY RECORD — 28 bytes per record, packed */
/* =========================================================================== */

/** @brief Fixed size of each binary history record (28 bytes). */
#define HISTORY_RECORD_SIZE 28

/** @brief Sentinel for fields without valid reading (equivalent to NAN in float). */
#define HIST_NAN_SENTINEL INT16_MIN /* -32768 */

/** @brief Extension for binary history files. */
#define HISTORY_FILE_EXT ".bin"

/**
 * @brief Binary history record — 28 bytes, packed.
 *
 * Fixed layout per record:
 * [0..3] epoch uint32_t Unix timestamp
 * [4..5] ambientTemp int16_t ×100 (e.g. 2345 = 23.45°C)
 * [6..7] ambientHum int16_t ×100 (e.g. 6120 = 61.20%)
 * [8..27] sensors[10] int16_t×10 ×100 (e.g. -1850 = -18.50°C)
 *
 * Invalid values (no reading) use HIST_NAN_SENTINEL (-32768).
 * Supported range: -327.67 to +327.67 — covers DS18B20 and DHT22.
 */
struct __attribute__((packed)) BinaryHistoryRecord {
 uint32_t epoch; /* Unix timestamp (seconds) */
 int16_t ambientTemp; /* Ambient temperature × 100 */
 int16_t ambientHum; /* Ambient humidity × 100 */
 int16_t sensors[MAX_SENSORS]; /* Slot temperatures × 100 */

 /* ── Conversion helpers ── */

 /**
 * @brief Converts float → int16 with ×100 scale.
 * @param v Float value (NAN returns HIST_NAN_SENTINEL).
 * @return Scaled value, clamped to int16 range.
 */
 static inline int16_t floatToI16(float v) {
 if (isnan(v)) return HIST_NAN_SENTINEL;
 float scaled = v * 100.0f;
 if (scaled > 32767.0f) return 32767;
 if (scaled < -32767.0f) return -32767; /* -32768 reserved for NAN */
 return (int16_t)roundf(scaled);
 }

 /**
 * @brief Converts scaled int16 → float.
 * @param v int16 value (HIST_NAN_SENTINEL returns NAN).
 * @return Float value with 2 decimal places of precision.
 */
 static inline float i16ToFloat(int16_t v) {
 if (v == HIST_NAN_SENTINEL) return NAN;
 return (float)v / 100.0f;
 }

 /** @brief Initializes all fields with invalid values. */
 void clear( ) {
 epoch = 0;
 ambientTemp = HIST_NAN_SENTINEL;
 ambientHum = HIST_NAN_SENTINEL;
 for (int i = 0; i < MAX_SENSORS; i++) {
 sensors[i] = HIST_NAN_SENTINEL;
 }
 }

 /**
 * @brief Converts the record to a human-readable CSV line (for telemetry).
 *
 * Format: "epoch;ambT;ambH;s0;s1;...;s9"
 * Invalid fields remain empty (compatible with upload format).
 *
 * @param buf Destination buffer.
 * @param bufSize Buffer size.
 * @return Pointer to buf (convenience for chaining).
 */
 char* toCsvLine(char* buf, size_t bufSize) const {
 if (bufSize == 0) return buf;

 int pos = snprintf(buf, bufSize, "%lu", (unsigned long)epoch);
 if ((size_t)pos >= bufSize) return buf; /* Buffer exhausted */

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
 appendField(ambientHum != HIST_NAN_SENTINEL,
 ";%.1f", (float)ambientHum / 100.0f);

 for (int i = 0; i < MAX_SENSORS; i++) {
 appendField(sensors[i] != HIST_NAN_SENTINEL,
 ";%.2f", (float)sensors[i] / 100.0f);
 }

 return buf;
 }
};
