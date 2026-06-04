/**
 * @file src/ota/orchestrator.cpp
 * @brief Implementation of the apply orchestrator.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "orchestrator.h"
#include "metadata.h"
#include "applier.h"
#include "StorageManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <hardware/sync.h>

namespace ota {

OrchestratorResult ota_apply_pending_update(StorageManager* storage) {
 /* (1) Read metadata. */
 UpdateMetadata meta;
 if (!ota_metadata_read(meta)) {
 return OrchestratorResult::NOT_PENDING;
 }
 if (meta.state != STATE_COMMITTED) {
 return OrchestratorResult::NOT_PENDING;
 }

 /* (2) Anti-loop. */
 if (meta.attempts >= OTA_MAX_APPLY_ATTEMPTS) {
 return OrchestratorResult::MAX_ATTEMPTS;
 }

 /* (3) Pause Core 1 BEFORE any flash_range_*. Project pattern
 * — without this, Core 1 (DisplayManager) reading flash via XIP during
 * erase causes hard fault classified by autopsy as "external"
 * reset (no watchdog flag), and metadata is never persisted. */
 if (storage) storage->enterFlashSafeMode( );

 /* (4) Mark APPLYING + attempts++ (before any tear down — if
 * it falls between here and watchdog_reboot, next boot counts the spent
 * attempt and eventually stops via MAX_ATTEMPTS). */
 meta.state = STATE_APPLYING;
 meta.attempts += 1;
 if (!ota_metadata_write(meta)) {
 if (storage) storage->exitFlashSafeMode( );
 return OrchestratorResult::METADATA_FAIL;
 }

 /* (5) Tear down — order matters. */
 WiFi.end( );

 /* LittleFS unmounted — staging is accessible via raw XIP. */
 LittleFS.end( );

 /* Mark scratch[5] with POST_OTA_APPLY_MAGIC to signal
 * that next boot should power-cycle CYW43. AppManager::setup detects early
 * and triggers WL_REG_ON LOW 500ms → high-Z. Resolves residual issue
 * where CYW43 chip is in intermediate state post-watchdog_reboot. */
 *(volatile uint32_t*)(0x40058000u + 0x20u) = 0xC72BAB07u;

 /* (5) Global IRQs OFF + jump to applier SRAM. */
 uint32_t saved_irq = save_and_disable_interrupts( );

 /* applier_run DOES NOT RETURN on success (reboots). On error,
 * returns false only on pre-destructive error. */
 bool ok = ota_applier_run(&meta);
 (void)ok;

 /* (6) Only reached if applier returns (error). Restore IRQs and
 * Core 1, return control to caller. App slot still intact. */
 restore_interrupts(saved_irq);
 if (storage) storage->exitFlashSafeMode( );

 return OrchestratorResult::APPLIER_RETURNED;
}

} /* namespace ota */
