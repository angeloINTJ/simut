/**
 * @file SystemDefs_Limits.h
 * @brief Hardware/system limits, firmware version, RBAC permissions.
 * @details Includes MAX_SENSORS, MAX_USERS, MOVING_AVG_WINDOW, GRAPH_WIDTH,
 * SIMUT_VERSION, MinMaxSlot, and the PERM_* bitmasks.
 * Sub-header of SystemDefs.h (facade).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Angelo Moises Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include "sensors/SensorConfig.h"

/* Hardware and system limits — defaults in src/simut_config.h */
#ifndef MAX_SENSORS
#define MAX_SENSORS 16 /* Maximum number of configurable sensor slots (GPIO0–GPIO15) */
#endif
#define MAX_USERS 5 /* Maximum user accounts (Flash/RAM budget) */
#define MOVING_AVG_WINDOW 10 /* Samples in the trimmed-mean sliding window */
#ifndef MAX_SENSOR_PINS
#define MAX_SENSOR_PINS 4 /* Maximum GPIO pins per sensor (fits SPI: MOSI,MISO,SCK,CS) */
#endif
#ifndef PIN_UNUSED
#define PIN_UNUSED 255 /* Sentinel for unused pin slots */
#endif

#ifndef MAX_SENSOR_CHANNELS
#define MAX_SENSOR_CHANNELS 4 /* Measurement channels per sensor (TEMP, HUM, PRESS, LUX) */
#endif
#define SIMUT_VERSION "1.5.2-rc24"

/* Fallback epoch for provisional time when NTP is unavailable and no
 * history records exist to seed the virtual RTC. Override via
 * platformio.ini build_flags: -DSIMUT_BUILD_EPOCH=<unix_timestamp> */
#ifndef SIMUT_BUILD_EPOCH
#define SIMUT_BUILD_EPOCH 1758380000UL  /* approx 2026-07-21 */
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

#define PERM_FULL_ADMIN 0xFFFF
