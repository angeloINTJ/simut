/**
 * @file    FlashIrqProbe.h
 * @brief   T0.1 (completed) — direct measurement of the Core-0 IRQ-off window.
 * @details The wave-1 metric (`flashOpMaxMs`) times the whole FLASH_OP block,
 *          which includes LittleFS bookkeeping and mutex handling. The plan's
 *          acceptance criterion is stated on the *interrupt-off* window, which
 *          is a strict subset of that: LittleFS wraps every program/erase in
 *          `noInterrupts()` / `interrupts()` (framework LittleFS.cpp:181-212).
 *
 *          These counters close that gap by intercepting the two SDK entry
 *          points via `-Wl,--wrap`, so the number reported is the real stall
 *          seen by cyw43/lwIP rather than a proxy for it.
 *
 *          Counters are plain globals, not `SystemMetrics` fields: the probe
 *          runs with interrupts disabled and XIP potentially down, where the
 *          singleton accessor is not reachable. Core 1 never writes flash
 *          (concurrency invariant 1), so no cross-core synchronisation is
 *          needed.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t g_flashIrqEraseCount;  /**< flash_range_erase calls */
extern volatile uint32_t g_flashIrqProgCount;   /**< flash_range_program calls */
extern volatile uint32_t g_flashIrqMaxUs;       /**< Longest single IRQ-off window */
extern volatile uint64_t g_flashIrqTotalUs;     /**< Sum, for the average (64-bit: a long soak wraps 32) */
extern volatile uint32_t g_flashIrqOver1msCount;   /**< Windows longer than 1 ms */

#ifdef __cplusplus
}
#endif
