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
#define SIMUT_VERSION "v3.37.8" /* CLI: re-introduzido CMD_DBG_SENSOR_HISTORY_ALL (removido em v3.24.15 / EXT-002) — comando oculto pra recuperar visualização de histórico pré factory reset zerando provisionEpoch dos sensores. Sintaxe: 'conf sensor <N> history all' ou 'conf sensor all history all'. Restrito a sessão Bluetooth autenticada (BT só valida slot 0 = admin); USB serial responde com "comando desconhecido". CommandManager ganhou wasLastInputFromBt() flag setada por processInput. Após o comando, usar 'write memory' + 'reload' pra aplicar. Não listado em help. v3.37.7: dots licença removidos + auth keypad fix. */ /* Fase 18 fechada (Profissionalização — auditoria pós-v3.35.0). Cinco sub-fases entregues e validadas em HW: 18.1 hardening web (XSS escape + rate-limit calib + mask t_key, v3.36.0); 18.2 parsing robustness (parseIntStrict + path normalize O(n) + safety counters, v3.36.1); 18.3 refactor + qualidade (executeCommand 952L→624L + zero warnings + Reserved.h map + broken-pipe observability, v3.36.2); 18.4 observabilidade (audit log tags + parseFloatStrict tests + doc OTA + schema invariant, v3.36.3); 18.5 polish (M3 mutex timeout API + M5 drift detection doc + G3 CSS comment cleanup, v3.37.0). M4 (TODOs sem rastreio) auditado: zero TODO/HACK/FIXME no código próprio. G1/G2/G4 deliberadamente skip (cosmético puro, sem ganho funcional). Total: 76/76 testes HW passam acumulado (29 + 12 + 9 + 10 + 12 + 4 host). Flash 98.2%, heap 58KB. */

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
