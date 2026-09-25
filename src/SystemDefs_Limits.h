/**
 * @file SystemDefs_Limits.h
 * @brief Hardware/system limits, firmware version, RBAC permissions.
 * @details Includes MAX_SENSORS, MAX_USERS, MOVING_AVG_WINDOW, GRAPH_WIDTH,
 * SIMUT_VERSION, MinMaxSlot, and the PERM_* bitmasks.
 * Sub-header of SystemDefs.h (facade).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include "sensors/SensorConfig.h"

/* Hardware and system limits — defaults in src/simut_config.h */
#ifndef MAX_SENSORS
#define MAX_SENSORS 16 /* Maximum number of configurable sensor slots (GPIO0–GPIO15) */
#endif
/* v24 (2026-09-19): 5 -> 32. An account is 70 B of config (62 + the 8-byte
 * panel-PIN digest), so the table costs 2,240 B in the config file, in the
 * heap copy of SystemConfig and in every transient copy a commit makes. The
 * old count survives as CFG_LEGACY_MAX_USERS in ConfigMigrate.h, because the
 * migration has to walk files that were written with it. */
#define MAX_USERS 32 /* Maximum user accounts (v24; 5 until v23) */

/* ── Does this image have a panel PIN at all? (v25) ──────────────────────
 * The PIN exists to prove who is standing at the TOUCH PANEL. An image with
 * no panel has nobody standing at it: the scrambled keypad, the policy, the
 * digest chain, the CLI verbs and the web fields are all dead weight there —
 * 2,750 B on the Air and 972 B on the alpha, measured 2026-09-20.
 *
 * What does NOT depend on this flag is the CONFIG LAYOUT. UserAccount::pinHash
 * and DisplayAuthConfig keep their bytes in every image, because
 * sizeof(SystemConfig) is the schema and a blob written by one image is read
 * by another — flash an Air over a panel unit and back, and the PINs have to
 * still be there. The fields travel; only the code that means anything by
 * them is conditional.
 *
 * Undefined (the native test builds) means YES: the rules in
 * SystemDefs_Validate.h are pure and the suite exists to hold them. */
#if !defined(SIMUT_DISPLAY_TFT) || SIMUT_DISPLAY_TFT
  #define SIMUT_PANEL_PIN 1
#else
  #define SIMUT_PANEL_PIN 0
#endif
/* Panel PIN digest (v24), bytes per account. 64 bits: the PIN space is
 * 10^4..10^8, so the digest is not where its strength lives. The length rule
 * (4..8 digits) is isValidPanelPin in SystemDefs_Validate.h. */
#define PIN_HASH_LEN 8
#define MOVING_AVG_WINDOW 10 /* Samples in the trimmed-mean sliding window */
#ifndef MAX_SENSOR_PINS
#define MAX_SENSOR_PINS 4 /* Maximum GPIO pins per sensor (fits SPI: MOSI,MISO,SCK,CS) */
#endif
#ifndef PIN_UNUSED
#define PIN_UNUSED 255 /* Sentinel for unused pin slots */
#endif

#ifndef MAX_SENSOR_CHANNELS
/* Channel slots per sensor. Four are defined today (TEMP, HUM, PRESS, LUX —
 * see sensors/SensorChannelTable.h); the other four are headroom, because
 * raising this cap moves SensorRecord and therefore the stored-config layout.
 * Doing that once with room to spare beats doing it per new quantity. */
#define MAX_SENSOR_CHANNELS 8
#endif
#define SIMUT_VERSION "2.7.3"

/* Fallback epoch for provisional time when NTP is unavailable and no
 * history records exist to seed the virtual RTC. Override via
 * platformio.ini build_flags: -DSIMUT_BUILD_EPOCH=<unix_timestamp> */
#ifndef SIMUT_BUILD_EPOCH
#define SIMUT_BUILD_EPOCH 1785380400UL  /* 2026-07-30 */
#endif

#define GRAPH_WIDTH 200 /* Maximum data points on the TFT graph */

/**
 * @brief Piso de epoch aceito em qualquer registro de histórico.
 *
 * L1: o valor estava duplicado e DIVERGENTE — escritores V4 e o gate de
 * processHistoryLogging usavam 1,6e9 (2020-09-13) enquanto telemetria e
 * leitores usavam 1,7e9 (2023-11-14). Registros gravados na janela entre
 * os dois eram descartados na leitura: dados no flash que nunca subiam.
 *
 * Unificado no valor MAIS PERMISSIVO para não invalidar o que já está
 * gravado. Serve só para barrar epoch de relógio não sincronizado
 * (~1970), que criaria arquivos /history/19691231.* e envenenaria o
 * cursor de telemetria.
 */
#define HIST_EPOCH_MIN 1600000000UL /* 2020-09-13 */

/**
 * @brief Named indices for the min/max cache array (sensors + board temp).
 *
 * Slots 0–15 correspond to configurable sensors (MAX_SENSORS).
 * Slot 16 is reserved for the internal board temperature.
 * MINMAX_SLOT_COUNT defines the total size of the cache arrays.
 */
enum MinMaxSlot {
 MINMAX_SLOT_BOARD_TEMP = MAX_SENSORS, /**< Index of board temp in cache */
 MINMAX_SLOT_COUNT = MAX_SENSORS + 1 /**< Total size of the cache array */
};

/* Permission bitmasks for role-based access control (RBAC) */
#define PERM_DASHBOARD 0x0001
#define PERM_HISTORY 0x0002
#define PERM_LOGS 0x0004
#define PERM_SYS_CONFIG 0x0008
#define PERM_NET_CONFIG 0x0010
#define PERM_FILE_READ 0x0020
#define PERM_FILE_UPLOAD 0x0040
#define PERM_FILE_DELETE 0x0080
#define PERM_USER_MGR 0x0100
#define PERM_CALIB 0x0200 /* Sensor calibration via /dashboard */
/* v24 — what a user may do at the panel itself, one bit per action so that an
 * account can be handed exactly one of them. The panel tests THE BIT of the
 * user its PIN identified; the alarms section of /api/commit_all accepts the
 * matching bit as an alternative to PERM_SYS_CONFIG for the same field, so the
 * two surfaces agree on what an "alarm operator" is. */
#define PERM_ALARM_LIMITS 0x0400 /* edit a sensor's alarm limits */
#define PERM_ALARM_BLOCK  0x0800 /* enable / disable a sensor's alarms */
#define PERM_MAINT        0x1000 /* open / close a maintenance window */
/* Any of the three opens the Alarms item on the panel. */
#define PERM_PANEL_ALARM_ANY (PERM_ALARM_LIMITS | PERM_ALARM_BLOCK | PERM_MAINT)

/* Every bit the users page can actually set — its checkbox map is exactly the
 * thirteen above. A grant carrying anything else is not a preference the device
 * can honour partially, so /api/commit_all refuses the action instead of
 * masking it down to something the operator never asked for. */
#define PERM_ALL_BITS 0x1FFF

#define PERM_FULL_ADMIN 0xFFFF
