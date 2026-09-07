/**
 * @file air/pico_sleep.h
 * @brief Minimal vendored subset of the pico-sdk hardware_sleep library.
 *
 * The bundled pico-sdk (arduino-pico 5.6.1 / pico-sdk 2.x) does not ship
 * hardware_sleep, so the sleep helpers are vendored here. Only the RP2040
 * deep-sleep (SLEEP) path is needed for SIMUT Air.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include "hardware/rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dormant_wake_source_callback_t)(void);

/** Sleep (deep sleep) until the given RTC alarm fires.
 *
 * clk_rtc must already be running from the XOSC (the caller configures it).
 * The RTC alarm is armed here; the processor then halts in SLEEP until the
 * alarm wakes it. Waking is a RESUME — this function returns with the system
 * still running from the XOSC and the PLLs powered down. The caller is
 * expected to re-initialise the clocks (or soft-reset). */
void sleep_goto_sleep_until(datetime_t *t, dormant_wake_source_callback_t callback);

#ifdef __cplusplus
}
#endif
