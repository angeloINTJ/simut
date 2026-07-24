/**
 * @file SystemDefs_Cli.h
 * @brief CLI command architecture: DemandType + CliDemand + CLIMode.
 * @details Enum of commands parsed by CommandManager (USB/BT) and struct
 * CliDemand with typed payload (intVal/strVal/rom/etc). Setters
 * encapsulate safeCopy() for char[] fields. Sub-header of
 * SystemDefs.h (facade).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include "SystemDefs_Time.h" /* safeCopy */

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
 CMD_SHOW_METRICS,
 CMD_SHOW_SENSOR_TYPES, /**< List compiled-in sensor drivers with channel/pin info */
 CMD_SHOW_GPIO, /**< GPIO resource map — 16 pins, free/used by slot */
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
 CMD_SET_HISTORY_INTERVAL, /**< intVal1 = minutes (1..1440). */
 CMD_RESET_ADMIN,
 CMD_RESET_TOUCH_CAL,
 CMD_FACTORY_RESET,
 CMD_FORMAT_FS, /**< system format confirm — LittleFS.format() + reboot (recovers a corrupt-but-mountable FS) */
 CMD_SET_NTP_ENABLED, /**< intVal1 = 0 off, 1 on */
 CMD_SET_DNS_CFG, /**< intVal1 = 0 auto, 1 manual; strVal1=ip1, strVal2=ip2 */
 CMD_SET_TIME, /**< strVal1="YYYY-MM-DD", strVal2="HH:MM:SS" */
 CMD_DEFINE_SENSOR,
 CMD_WIPE_SENSOR,
 CMD_REMOVE_SENSOR, /**< sensor remove <gpio> confirm — deactivate and clear a slot */
 CMD_ACCEPT_SENSOR,
 CMD_SCAN_SENSORS,
 CMD_WRITE_MEMORY,
 CMD_CLEAR_LOGS,
 CMD_RELOAD,
 CMD_TEL_SYNC,
 CMD_TEL_DUMP,
 CMD_TEL_RESET, /**< Reset telemetry cursor: RAM cache + flash file */
 CMD_DEBUG,
 CMD_LANGUAGE,
 /* CLI↔Web parity */
 CMD_IP_CFG, /**< Static IP: intVal1 = 0 dhcp, 1 static, 2 addr, 3 mask, 4 gw, 5 dns; strVal1 = value */
 CMD_SENSOR_FIELD, /**< Limits/calib: intVal1 = gpio; strVal1 = field (tmin/tmax/hmin/hmax/alarm/calib); strVal2 = value */
 CMD_USER_ADD, /**< strVal1 = username; strVal2 = password */
 CMD_USER_DEL, /**< strVal1 = username (protects admin) */
 CMD_USER_PASS, /**< strVal1 = username; strVal2 = new password */
 CMD_USER_PERM, /**< strVal1 = username; strVal2 = role name or 0xHEX mask */
 CMD_SET_WEB_PORT, /**< intVal1 = port (1..65535) */

 /* 'touch sim X Y' — injects touch (x,y) screen-space.
 * Useful for automating screenshots on all screens
 * via /api/screenshot. intVal1=x, intVal2 reused for y. */
 CMD_TOUCH_SIM,

 /* 'screen <NAME>' — switches TFT screen directly via
 * show*Screen methods (bypasses handleTouch which has pressure gates).
 * NAMEs: dashboard, settings, themes, lang, password, license, status,
 * touchcal, sounds, alarms, alarmedit, graph, stats, calendar,
 * alarmaction, displayoffset, auth. strVal1=name. */
 CMD_GOTO_SCREEN,

 /* ── Cisco IOS-style mode navigation ── */
 CMD_ENABLE,        /**< enable — enter privileged EXEC mode */
 CMD_DISABLE,       /**< disable — return to user EXEC mode */
 CMD_CONFIGURE,     /**< configure terminal — enter global config mode */
 CMD_EXIT,          /**< exit — go up one mode level */
 CMD_END,           /**< end — return to privileged EXEC from any config mode */
 CMD_DO,            /**< do <cmd> — execute privileged EXEC command from config mode */
 CMD_SENSOR_ENTER,  /**< sensor <N> — enter sensor config mode (from global config) */
};

/** Cisco IOS-style hierarchical CLI modes. */
enum CLIMode : uint8_t {
 CLI_MODE_USER_EXEC      = 0,  /**< SIMUT> — monitoring, read-only */
 CLI_MODE_PRIV_EXEC      = 1,  /**< SIMUT# — maintenance, config entry */
 CLI_MODE_GLOBAL_CONFIG  = 2,  /**< SIMUT(config)# — global configuration */
 CLI_MODE_SENSOR_CONFIG  = 3   /**< SIMUT(config-sensor-N)# — single sensor */
};

/** Bitmask constants for command validity by CLI mode. */
#define CLI_VALID_USER      (1 << CLI_MODE_USER_EXEC)
#define CLI_VALID_PRIV      (1 << CLI_MODE_PRIV_EXEC)
#define CLI_VALID_CONFIG    (1 << CLI_MODE_GLOBAL_CONFIG)
#define CLI_VALID_SENSOR    (1 << CLI_MODE_SENSOR_CONFIG)
#define CLI_VALID_ALL       (CLI_VALID_USER | CLI_VALID_PRIV | CLI_VALID_CONFIG | CLI_VALID_SENSOR)
/** Commands that don't change config: valid in any mode including read-only user EXEC. */
#define CLI_VALID_READONLY  (CLI_VALID_USER | CLI_VALID_PRIV | CLI_VALID_CONFIG)

/** Returns a 4-bit mode mask indicating which CLI modes a DemandType is valid in. */
uint8_t getCommandModeMask(DemandType t);

/** Returns a human-readable prompt suffix for a CLI mode (e.g. ">", "#", "(config)#"). */
const char* getModePromptSuffix(CLIMode mode);

/** Returns a one-line help description for a CLI mode (used by '?' at each level). */
const char* getModeHelpLine(CLIMode mode, bool pt);

/** Parsed CLI command with typed payload fields.
 *
 * strVal1/strVal2 are fixed char[] (previously: String) to remove
 * dynamic allocations in the CLI parser (path exercised on every command).
 * 64 bytes covers the largest destinations: cfg.telServer[64], telApiKey[64],
 * wifiSsid[32], etc. Strings longer than 63 chars are silently truncated
 * in safeCopy — same previous behavior when they landed in cfg.* also truncated.
 */
struct CliDemand {
 DemandType type;
 char strVal1[64] = {0};
 char strVal2[64] = {0};
 char strVal3[32] = {0};
 int intVal1 = 0; /**< Zero-initialized: handlers must still check intVal1Valid. */
 bool boolVal;
 uint8_t rom[8];
 bool confirmed = false; /**< true if suffix 'confirm' present — gate for destructive commands */
 bool intVal1Valid = true; /**< false if the numeric token was not a well-formed int */

 /* Utility setters for char[] migration. Use at sites that previously did
 * `cmd.strVal1 = <String|const char*>`. */
 void setStrVal1(const char* s) { safeCopy(strVal1, s ? s : "", sizeof(strVal1)); }
 void setStrVal2(const char* s) { safeCopy(strVal2, s ? s : "", sizeof(strVal2)); }
 void setStrVal3(const char* s) { safeCopy(strVal3, s ? s : "", sizeof(strVal3)); }
};
