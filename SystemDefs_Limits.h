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
#define SIMUT_VERSION "v3.34.0" /* F-CALIB-UI integrado no /dashboard (toggle "Modo Calibração" + inputs Nome/ID/Ref + botão Atualizar inline). Toggle só aparece se /web/calib.on existe no FS (opt-in). Backend: 2 endpoints — GET /api/calib (estado leve com leituras correntes), POST /api/calib (apply + reescrita atômica de calib.csv com VERSION=epoch, NTP-gated 503). Aceitação de sensor via CLI `sensor accept N`. F-WEB-DEDUP: CSS comum (drawer/topbar/breadcrumb/toast) extraído pra /style.css cacheável (8 páginas), drawer HTML extraído pra LANG_JS via installDrawer(). Favicon migrado de PROGMEM (11KB) pra LittleFS (/favicon.ico) — fallback 204 se ausente. Economia total absorve custo do patch + libera ~8.6KB. Flash final 97.7%. */

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
#define PERM_CALIB       0x0200   /* v3.34.0: calibração de sensores via /dashboard */

#define PERM_FULL_ADMIN  0xFFFF
