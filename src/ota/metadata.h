/**
 * @file src/ota/metadata.h
 * @brief Persistent OTA update metadata.
 *
 * @details 4 KiB sector at OTA_METADATA_OFFSET — only the first page
 * (256 B) receives write. Survives watchdog/power reboots.
 *
 * States (UpdateState):
 * NONE — no pending update (magic absent).
 * COMMITTED — upload ok, validate ok; ready for apply.
 * APPLYING — orchestrator entered destructive path. If boot
 * reads this, it is first post-update boot.
 * POST_BOOT — first post-update boot detected, LFS reformat
 * in progress or awaiting restore.
 * COMPLETED — restore complete, cycle closed. Next boot
 * normal.
 *
 * Apply is destructive. If power fails between APPLYING and the new
 * firmware reboot, attempts++ on next boot and user goes to
 * BOOTSEL recovery.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include "ota_layout.h"

namespace ota {

/**
 * 4 KiB SRAM scratch buffer shared by flash operations
 * (apply sector copy + metadata read-erase-program-all preserving
 * snapshot). Defined in `applier.cpp` (static BSS, 0-init at boot).
 *
 * Users:
 * - `applier.cpp::ota_applier_run` (during apply, IRQ off — exclusive use).
 * - `metadata.cpp::ota_metadata_write` (preserves snapshot region).
 * - `metadata.cpp::ota_snapshot_write` (preserves metadata page 0).
 * - `config_snapshot.cpp::ota_snapshot_capture` (assembles payload pre-write).
 *
 * Race-free because caller 1 (apply) only runs AFTER `state=APPLYING` persisted,
 * and callers 2/3/4 never run concurrently with apply (apply only ends
 * via reboot).
 */
extern uint8_t s_applier_buf[OTA_FLASH_SECTOR_SIZE];

constexpr uint32_t OTA_MAGIC_PENDING = 0xA5C3F00Du;

enum UpdateState : uint32_t {
 STATE_NONE = 0,
 STATE_COMMITTED = 1,
 STATE_APPLYING = 2,
 STATE_POST_BOOT = 3,
 STATE_COMPLETED = 4,
};

/* Anti-loop: if attempts >= MAX_ATTEMPTS at boot, refuses apply (leaves
 * BOOTSEL as the recovery path). */
constexpr uint32_t OTA_MAX_APPLY_ATTEMPTS = 3;

struct __attribute__((packed)) UpdateMetadata {
 uint32_t magic; /**< OTA_MAGIC_PENDING when valid. */
 uint32_t state; /**< UpdateState. */
 uint32_t compressed_size; /**< Bytes in staging (.bin.gz). */
 uint32_t uncompressed_size; /**< Expected bytes after gunzip (.bin). */
 uint32_t compressed_crc32; /**< CRC32 of staging — matches client. */
 uint32_t uncompressed_crc32; /**< CRC32 of gunzip dry-run output. */
 uint32_t attempts; /**< Apply attempts (anti-loop). */
 uint32_t reserved[57]; /**< Pad to 256 B (fill with 0xFFFFFFFF). */
};
static_assert(sizeof(UpdateMetadata) == 256, "UpdateMetadata != 256 B");

/**
 * @brief Reads metadata via XIP (without disabling IRQs / unmounting).
 *
 * @return true if read; false on magic mismatch (out filled with zeros).
 */
bool ota_metadata_read(UpdateMetadata& out);

/**
 * @brief Erases metadata sector + writes 256 B to first page.
 *
 * **MANDATORY PRE-CONDITION**: caller must ensure Core 1 paused
 * (`StorageManager::enterFlashSafeMode()`) BEFORE calling — function
 * does internal `flash_range_erase` and Core 1 reading flash via XIP during
 * erase causes hard fault.
 *
 * Exception: during apply post-IRQ-disable + Core 1 lockout from orchestrator,
 * already safe; can call directly.
 *
 * @return true on success.
 */
bool ota_metadata_write(const UpdateMetadata& in);

/**
 * @brief Sets metadata.state and persists. Shortcut.
 */
bool ota_metadata_set_state(UpdateState st);

/**
 * @brief Erases metadata sector (all 0xFF). Equivalent to "no pending update".
 *
 * IMPORTANT: also erases the config snapshot in pages 1..15.
 * Only call after restore has succeeded OR in factory init.
 */
bool ota_metadata_clear( );

/**
 * @brief Writes raw bytes to pages 1..15 of the metadata sector.
 *
 * Preserves current page 0 (UpdateMetadata): reads via XIP, assembles scratch
 * [page0 | snapshot_data | 0xFF padding] in `s_applier_buf`, erase + program
 * full 4 KiB.
 *
 * **PRE-CONDITION**: caller must ensure Core 1 paused
 * (`StorageManager::enterFlashSafeMode()`). It is not the function that decides.
 *
 * @param data Buffer with serialized snapshot (header + payload + CRC).
 * @param len Size in bytes; <= 3840.
 * @return true if written.
 */
bool ota_snapshot_write(const uint8_t* data, uint16_t len);

} /* namespace ota */
