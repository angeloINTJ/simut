/**
 * @file SystemDefs_Reserved.h
 * @brief Map central dos overlays em SystemConfig::reserved[64].
 * @details Antes (pré-.3): cada call site fazia
 * cfg.reserved + sizeof(TouchCalData) + sizeof(SoundConfigData)
 * ou usava offsets literais — fácil errar e difícil auditar.
 * Aqui ficam as constantes nomeadas + static_asserts que travam o
 * layout em compile-time. Mudar offset = quebra build = rastreável.
 *
 * Layout (CONFIG_VERSION 13+):
 * [ 0..11] TouchCalData (12 B) — calibração XPT2046
 * [12..17] SoundConfigData ( 6 B) — toggles + volume + overrides
 * [18..21] DisplayOffsetData ( 4 B) — offset xy do TFT
 * [22..23] CliConfigData ( 2 B) — UART/CLI flags 
 * [24..25] WebConfigData ( 2 B) — porta TCP do web (U3)
 * [26..27] SetupFlagsData ( 2 B) — must-change-pin (F12.4)
 * [28..47] NetworkTimeData (20 B) — DNS auto/manual + NTP (F-NET-TIME.1)
 * [48..63] livre para expansão futura
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stddef.h>

/* ────────────────────────────────────────────────────────────────────────
 * Constantes de offset — uma fonte da verdade. Quem move tem que tocar
 * AQUI e o static_assert no fim do arquivo trava qualquer overlap.
 * ──────────────────────────────────────────────────────────────────────── */

constexpr size_t RESERVED_TOUCH_OFFSET = 0;
constexpr size_t RESERVED_TOUCH_SIZE = 12;

constexpr size_t RESERVED_SOUND_OFFSET = RESERVED_TOUCH_OFFSET + RESERVED_TOUCH_SIZE; /* 12 */
constexpr size_t RESERVED_SOUND_SIZE = 6;

constexpr size_t RESERVED_DISPLAY_OFFSET = RESERVED_SOUND_OFFSET + RESERVED_SOUND_SIZE; /* 18 */
constexpr size_t RESERVED_DISPLAY_SIZE = 4;

constexpr size_t RESERVED_CLI_OFFSET = RESERVED_DISPLAY_OFFSET + RESERVED_DISPLAY_SIZE; /* 22 */
constexpr size_t RESERVED_CLI_SIZE = 2;

constexpr size_t RESERVED_WEB_OFFSET = RESERVED_CLI_OFFSET + RESERVED_CLI_SIZE; /* 24 */
constexpr size_t RESERVED_WEB_SIZE = 2;

constexpr size_t RESERVED_SETUP_OFFSET = RESERVED_WEB_OFFSET + RESERVED_WEB_SIZE; /* 26 */
constexpr size_t RESERVED_SETUP_SIZE = 2;

constexpr size_t RESERVED_NETTIME_OFFSET = RESERVED_SETUP_OFFSET + RESERVED_SETUP_SIZE; /* 28 */
constexpr size_t RESERVED_NETTIME_SIZE = 20;

constexpr size_t RESERVED_FREE_OFFSET = RESERVED_NETTIME_OFFSET + RESERVED_NETTIME_SIZE; /* 48 */
constexpr size_t RESERVED_TOTAL_SIZE = 64;

/* Sanity checks em compile-time: nada estoura o buffer de 64 B e
 * os blocos não pisam um no outro (a aritmética acima já garante,
 * mas a próxima asserção formaliza a invariante). */
static_assert(RESERVED_FREE_OFFSET <= RESERVED_TOTAL_SIZE,
 "Overlays em SystemConfig::reserved[] excedem 64 B!");

/* WEB_CONFIG_OFFSET (constexpr em SystemDefs_Records.h) e CLI_CONFIG_OFFSET
 * (#define em SystemDefs_Records.h) já existem com os mesmos valores. Os
 * RESERVED_*_OFFSET são alternativas mais consistentes; ambos coexistem. */
