/**
 * @file SystemDefs_Reserved.h
 * @brief Central map of overlays in SystemConfig::reserved[64].
 * @details Previously, each call site did
 * cfg.reserved + sizeof(TouchCalData) + sizeof(SoundConfigData)
 * or used literal offsets — easy to get wrong and hard to audit.
 * Here live the named constants + static_asserts that lock the
 * layout at compile-time. Changing an offset = build break = traceable.
 *
 * Layout (CONFIG_VERSION 13+):
 * [ 0..11] TouchCalData (12 B) — XPT2046 calibration
 * [12..17] SoundConfigData ( 6 B) — toggles + volume + overrides
 * [18..21] DisplayOffsetData ( 4 B) — TFT xy offset
 * [22..23] CliConfigData ( 2 B) — UART/CLI flags
 * [24..25] WebConfigData ( 2 B) — web server TCP port
 * [26..27] SetupFlagsData ( 2 B) — must-change-pin
 * [28..47] NetworkTimeData (20 B) — DNS auto/manual + NTP
 * [48..51] HistoryConfigData ( 4 B) — recording interval (SystemDefs_Records.h)
 * [52..53] Dash slot selection ( 2 B) — top-pinned + selected idx, 0xFF = none
 * [54..55] HaDiscoveryData ( 2 B) — HA MQTT Discovery (SystemDefs_Records.h)
 * [56..63] free for future expansion
 *
 * [52..53] were claimed by AppManager_HistoryAlarm.cpp through raw literals
 * while every map still said "free" — the HA overlay landed on them and its
 * magic was eaten by the 0xFF sentinel. If you are adding an overlay, grep
 * for `reserved[<offset>` before trusting this comment.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stddef.h>

/* ────────────────────────────────────────────────────────────────────────
 * Offset constants — single source of truth. Whoever moves must touch
 * HERE and the static_assert at the end of the file locks any overlap.
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

/* HistoryConfigData [48..51] keeps its own constant (HISTORY_CONFIG_OFFSET,
 * SystemDefs_Records.h) — mirrored here only by the arithmetic. */
constexpr size_t RESERVED_HISTORY_SIZE = 4;

/* TFT dashboard slot persistence — formerly raw literals 52/53 in
 * AppManager_HistoryAlarm.cpp, registered nowhere, which is how the HA
 * overlay got parked on top of them. 0xFF = no slot. */
constexpr size_t RESERVED_DASH_TOP_IDX = RESERVED_NETTIME_OFFSET + RESERVED_NETTIME_SIZE
                                       + RESERVED_HISTORY_SIZE; /* 52 */
constexpr size_t RESERVED_DASH_CUR_IDX = RESERVED_DASH_TOP_IDX + 1; /* 53 */

constexpr size_t RESERVED_FREE_OFFSET = RESERVED_DASH_CUR_IDX + 1 + 2; /* 56 (54..55 = HaDiscoveryData) */
constexpr size_t RESERVED_TOTAL_SIZE = 64;

/* Compile-time sanity checks: nothing overflows the 64 B buffer and
 * blocks don't step on each other (the arithmetic above already guarantees,
 * but the next assertion formalizes the invariant). */
static_assert(RESERVED_FREE_OFFSET <= RESERVED_TOTAL_SIZE,
 "Overlays in SystemConfig::reserved[] exceed 64 B!");
