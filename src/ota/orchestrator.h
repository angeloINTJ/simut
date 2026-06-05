/**
 * @file src/ota/orchestrator.h
 * @brief OTA apply orchestrator.
 *
 * @details Sequence:
 * 1. Read metadata; if magic absent or state != COMMITTED, exit.
 * 2. Mark state=APPLYING + attempts++; persist in flash.
 * 3. Tear down: WiFi.end(), LittleFS.end(), enterFlashSafeMode
 * (Core 1 lockout).
 * 4. save_and_disable_interrupts.
 * 5. Jump to applier (SRAM). Does not return on success.
 * 6. On return (pre-destructive error), restore IRQs + Core 1 +
 * report.
 *
 * Anti-loop: if attempts >= OTA_MAX_APPLY_ATTEMPTS, refuses
 * apply and leaves user to go to BOOTSEL recovery.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>

class StorageManager;

namespace ota {

enum class OrchestratorResult : uint8_t {
 NOT_PENDING = 0, /**< No valid update in metadata. */
 MAX_ATTEMPTS = 1, /**< Exceeded OTA_MAX_APPLY_ATTEMPTS. */
 METADATA_FAIL = 2, /**< Could not persist state=APPLYING. */
 APPLIER_RETURNED = 3, /**< Pre-destructive error inside applier. */
 /* Success = does not return (watchdog_reboot inside applier). */
};

/**
 * @brief Triggers apply of the pending update.
 *
 * Pre-condition: web request just responded and flushed. This function
 * DOES NOT RESPOND TO ANYTHING — caller is responsible for send before.
 *
 * @param storage Manages Core 1 lockout via enterFlashSafeMode.
 * @return Result in case of return (rare). On success, watchdog reset.
 */
OrchestratorResult ota_apply_pending_update(StorageManager* storage);

} /* namespace ota */
