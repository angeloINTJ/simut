/**
 * @file SystemDefs_Cli.h
 * @brief CLI command architecture: DemandType + CliDemand.
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
 CMD_SET_NTP_ENABLED, /**< intVal1 = 0 off, 1 on */
 CMD_SET_DNS_CFG, /**< intVal1 = 0 auto, 1 manual; strVal1=ip1, strVal2=ip2 */
 CMD_SET_TIME, /**< strVal1="YYYY-MM-DD", strVal2="HH:MM:SS" */
 CMD_DEFINE_SENSOR,
 CMD_WIPE_SENSOR,
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
};

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
 int intVal1;
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
