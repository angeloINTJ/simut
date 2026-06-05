/**
 * @file src/ota/applier.h
 * @brief OTA applier — runs entirely from SRAM.
 *
 * @details `ota_applier_run` is the destructive routine that erases the app slot
 * and rewrites it with the uncompressed staging content. IT IS MARKED
 * WITH `__not_in_flash_func` to reside in SRAM, and therefore can
 * execute even while flash is being erased/programmed.
 *
 * PRECONDITIONS (caller orchestrator guarantees):
 * - WiFi/CYW43 off (`WiFi.end()`).
 * - LittleFS unmounted (`LittleFS.end()`).
 * - Core 1 paused via `multicore_lockout_start_blocking()`.
 * - Global IRQs disabled (`save_and_disable_interrupts`).
 * - Metadata.state == APPLYING and persisted in flash.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include "metadata.h"

namespace ota {

/**
 * @brief Applies the update in destructive mode. DOES NOT RETURN on success.
 *
 * @param meta Metadata in RAM (caller read before IRQ disable).
 *
 * @return false only if error detected BEFORE any destructive action.
 * On success, NEVER returns (always reboots).
 */
bool ota_applier_run(const UpdateMetadata* meta);

} /* namespace ota */
