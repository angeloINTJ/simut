/**
 * @file    SystemDefs_Limits.h
 * @brief   Hardware/system limits, version, RBAC permissions (EXT-003 split).
 * @details Inclui MAX_SENSORS, MAX_USERS, MOVING_AVG_WINDOW, GRAPH_WIDTH,
 *          SIMUT_VERSION, MinMaxSlot e os bitmasks PERM_*.
 *          Sub-header de SystemDefs.h (facade). EXT-003 / F17 etapa 4.
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>

/* Hardware and system limits */
#define MAX_SENSORS 10                  /* Maximum number of configurable sensor slots */
#define MAX_USERS 5                     /* Maximum user accounts (Flash/RAM budget) */
#define MOVING_AVG_WINDOW 10            /* Samples in the trimmed-mean sliding window */
#define SIMUT_VERSION "v3.30.0" /* F-BUILD FECHADA — bump minor que marca fim de fase. EXT-001 ✅ (lib pins exatos em platformio.ini, herdado de v3.29.1) + EXT-009 ✅ (host-side unit tests via pio test -e native + Unity, 25/25 passed em 0.82s). + Bug fix: paulstoffregen/XPT2046_Touchscreen@1.3 nao existia no PIO registry (so tem alpha de 2019); re-pinned via GitHub URL + SHA d57f64c (v1.4 tag). Refator de platformio.ini: settings HW movidas de [env] para [pico_base] (estendido por release/debug); [env:native] standalone. test/native_stubs/Arduino.h provê String + millis() stub minimo. test/test_validators/test_main.cpp cobre isValidIpv4, isSafeUploadFilename, isValidName, isValidCfgString, isInRange, parseIntStrict, timeReached, timeSince, dallasCrc8, floatToI16/i16ToFloat. Tag git v3.30.0. Build OK (Flash 98.5%, RAM 40.9%). */

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
