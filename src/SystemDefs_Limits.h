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

/* Hardware and system limits */
#define MAX_SENSORS 10 /* Maximum number of configurable sensor slots */
#define MAX_USERS 5 /* Maximum user accounts (Flash/RAM budget) */
#define MOVING_AVG_WINDOW 10 /* Samples in the trimmed-mean sliding window */
#define SIMUT_VERSION "1.0.0"

#define GRAPH_WIDTH 200 /* Maximum data points on the TFT graph */

/**
 * @brief Named indices for the min/max cache array (sensors + board temp).
 *
 * Slots 0–9 correspond to configurable sensors (MAX_SENSORS).
 * Slot 10 is reserved for the internal board temperature.
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
