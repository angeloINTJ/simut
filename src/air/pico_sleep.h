/**
 * @file air/pico_sleep.h
 * @brief Minimal vendored subset of the pico-sdk hardware_sleep library.
 *
 * The bundled pico-sdk (arduino-pico 5.6.1 / pico-sdk 2.x) does not ship
 * hardware_sleep, so the dormant helpers are vendored here. Only the RP2040
 * dormant path is needed for SIMUT Air.
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

/** Enter dormant until the given RTC alarm fires. Wake is a full reset. */
void sleep_goto_dormant_until(datetime_t *t, dormant_wake_source_callback_t callback);

#ifdef __cplusplus
}
#endif
