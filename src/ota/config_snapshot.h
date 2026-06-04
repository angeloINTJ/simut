/**
 * @file src/ota/config_snapshot.h
 * @brief Critical config snapshot preserved during OTA.
 *
 * @details The destructive apply reformats the LittleFS partition
 * (shared with the firmware staging area). So that the
 * device boots post-update preserving WiFi/users/sensors, this
 * module serializes `/config/system.bin` in pages 1..15 of the OTA
 * metadata sector (`OTA_METADATA_OFFSET`, 4 KiB), which survives
 * the apply.
 *
 * Other files (`/calib.csv`, `/history/`, `/web/`, `/lang/`)
 * are NOT included in the snapshot: the user keeps the `.bkp` downloaded
 * via browser before apply (fallback A) and restores manually
 * via /files after reboot.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

class StorageManager;

namespace ota {

constexpr uint32_t CONFIG_SNAPSHOT_MAGIC = 0x434D4953u; /* 'SIMC' little-endian */
constexpr uint16_t CONFIG_SNAPSHOT_VERSION = 1u;

/* Entire 4 KiB sector at the END of the staging area (`OTA_SNAPSHOT_OFFSET`). */
constexpr uint16_t CONFIG_SNAPSHOT_REGION_SIZE = 4096u;

/* Header (16 B) + CRC32 trailer (4 B) = 20 B overhead. */
constexpr uint16_t CONFIG_SNAPSHOT_PAYLOAD_MAX = CONFIG_SNAPSHOT_REGION_SIZE - 20u;

struct __attribute__((packed)) ConfigSnapshotHeader {
 uint32_t magic; /**< CONFIG_SNAPSHOT_MAGIC */
 uint16_t schema_version; /**< CONFIG_SNAPSHOT_VERSION */
 uint16_t reserved0;
 uint32_t payload_size; /**< Payload bytes (system.bin raw). */
 uint32_t reserved1;
};
static_assert(sizeof(ConfigSnapshotHeader) == 16, "ConfigSnapshotHeader != 16 B");

/**
 * @brief Serializes `/config/system.bin` into `ota::s_applier_buf` (read-only).
 *
 * **CRITICAL PRE-CONDITION**: called with LittleFS mounted and Core 1 ACTIVE
 * (NOT in flash safe mode). LittleFS.open/read interacts with mutexes that
 * conflict with `multicore_lockout` — causes hang.
 *
 * Assembles `[ConfigSnapshotHeader | system.bin raw | CRC32]` in `s_applier_buf`
 * but does NOT write to flash. The caller must call `ota_snapshot_commit()`
 * after entering flash safe mode to persist.
 *
 * @return Total size (header + payload + CRC) in bytes, or 0 on
 * error (file absent, > PAYLOAD_MAX, or read failure).
 */
uint16_t ota_snapshot_serialize( );

/**
 * @brief Persists the already-serialized snapshot in `s_applier_buf` to flash.
 *
 * **PRE-CONDITION**: caller must be in flash safe mode (Core 1 locked).
 * `ota_snapshot_serialize()` must have been called first — `s_applier_buf`
 * must contain the ready snapshot in its first @p total_len bytes.
 *
 * @param total_len Return value of `ota_snapshot_serialize()`.
 * @return true on success.
 */
bool ota_snapshot_commit(uint16_t total_len);

/**
 * @brief Checks if there is a valid snapshot (magic + CRC) in the metadata partition.
 *
 * Read-only — uses XIP, without disabling IRQs or touching flash.
 */
bool ota_snapshot_present( );

/**
 * @brief Restores `system.bin` from the snapshot to the freshly formatted LittleFS.
 *
 * Pre-condition: LittleFS mounted, `/config` directory exists (both
 * guaranteed by `StorageManager::begin()` before the call).
 *
 * On success, does NOT clear the snapshot — whoever calls (AppManager_Boot via
 * `ota_metadata_clear()`) is responsible for that. This allows the
 * boot to detect consistent state: if `ota_metadata_clear` fails before
 * the final reboot, next boot retries the restore (idempotent).
 *
 * @return true if system.bin was written (file path / CRC OK); false
 * otherwise. Caller decides whether to proceed with factory defaults.
 */
bool ota_snapshot_restore_to_lfs( );

} /* namespace ota */
