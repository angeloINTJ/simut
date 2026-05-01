/**
 * @file    SystemDefs_Limits.h
 * @brief   Hardware/system limits, version, RBAC permissions (EXT-003 split).
 * @details Inclui MAX_SENSORS, MAX_USERS, MOVING_AVG_WINDOW, GRAPH_WIDTH,
 *          SIMUT_VERSION, MinMaxSlot e os bitmasks PERM_*.
 *          Sub-header de SystemDefs.h (facade). EXT-003 / F17 etapa 4.
 *
 * @project SIMUT
 * @license MIT License
 */

#pragma once
#include <Arduino.h>

/* Hardware and system limits */
#define MAX_SENSORS 10                  /* Maximum number of configurable sensor slots */
#define MAX_USERS 5                     /* Maximum user accounts (Flash/RAM budget) */
#define MOVING_AVG_WINDOW 10            /* Samples in the trimmed-mean sliding window */
#define SIMUT_VERSION "v3.27.6" /* F-CSV.4: UI export historico em /history apos #chartContainer. Card com 2 pickers date+time + dropdown sensor (Todos | sensor unico) + botao "Exportar CSV". JS: mini lib CRC32-IEEE com tabela 256 (compativel com firmware crc32_*); divisao do range em meses calendar via _iterMonths(); fetch sequencial /api/export/history.bin?from=&to=; valida magic SIMX, version, kind=H, recordSize=28, CRC32 trailer; decodifica .simx + sensor_table + payload; gera CSV (BOM UTF-8, ISO-8601 com tz local, colunas timestamp_iso/sensor_id/sensor_name/value/unit); trigger download Blob. Nome: simut_history_<YYYY-MM>.csv (com sufixo _sN se sensor unico). i18n keys: exp_hist_title, exp_from, exp_to, exp_sensor, exp_all, exp_btn, exp_idle, exp_fetching, exp_validating, exp_done, exp_err_*. */

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
