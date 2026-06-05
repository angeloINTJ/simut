/**
 * @file src/ota/staging.h
 * @brief Low-level access to the staging area (OTA).
 *
 * @details Reads/writes/erases the LittleFS region in raw mode (without
 * mount). PRECONDITIONS:
 * - LittleFS unmounted before writing/erasing (reading via XIP is OK).
 * - Core 1 paused via `multicore_lockout` during erase/program.
 * - StorageManager::enterFlashSafeMode() already covers this.
 *
 * High-level API: use staging_session_begin/end (manages everything).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ota_layout.h"

class StorageManager;

namespace ota {

/**
 * @brief Erases the entire staging region (1024 KB).
 *
 * Takes ~5-10s. Pauses Core 1 internally. Updates WDT.
 * PRECONDITION: LittleFS unmounted (caller responsibility).
 *
 * @return true on success.
 */
bool staging_erase_all( );

/**
 * @brief Erases ONE sector (4 KB) of staging at relative offset.
 *
 * @param offset_in_staging Multiple of 4096; 0 = first sector.
 */
bool staging_erase_sector(uint32_t offset_in_staging);

/**
 * @brief Programs @p data at @p offset_in_staging.
 *
 * @p len and @p offset_in_staging must be multiples of OTA_FLASH_PAGE_SIZE (256).
 * Sector must have been erased BEFORE (NAND-style flash: only writes 1→0).
 *
 * @return true on success.
 */
bool staging_write(uint32_t offset_in_staging, const uint8_t* data, size_t len);

/**
 * @brief Reads bytes from staging via XIP (bypasses LittleFS).
 *
 * Without disabling IRQs / Core 1 (XIP read is safe during normal operation).
 * Do NOT call while LittleFS is mounted if the region was written
 * externally (XIP cache may be outdated).
 */
void staging_read(uint32_t offset_in_staging, uint8_t* dst, size_t len);

/**
 * @brief High-level session: prepares staging for upload use.
 *
 * Sequence:
 * 1. LittleFS.end() — unmounts.
 * 2. staging_erase_all() — cleans.
 *
 * On return, the LittleFS area is all 0xFF and ready to receive
 * the .bin.gz via staging_write().
 *
 * @param storage Pointer to StorageManager (needed for unmount/remount).
 * @return true on success.
 */
bool staging_session_begin(StorageManager* storage);

/* Variant without upfront erase — caller does on-demand erase. */
bool staging_session_begin_lite(StorageManager* storage);

/**
 * @brief Ends the session and remounts LittleFS.
 *
 * Useful for upload ABORT (discards staging and returns to normal). Does NOT
 * format — after erase_all + LittleFS.begin(), LFS detects
 * "invalid filesystem" and formats by itself.
 *
 * In the APPLY path, staging_session_end is NOT called —
 * the applier continues with LittleFS unmounted and reboots.
 */
bool staging_session_end(StorageManager* storage);

/**
 * @brief Self-test of the raw flash path (acceptance criteria).
 *
 * Writes checkerboard pattern (0xAA/0x55) in ONE sector; reads back; compares;
 * erases sector. Callable only with active session (LittleFS unmounted).
 *
 * Result in @p out_first_diff: -1 = OK, >=0 = offset of first byte
 * that did not match.
 *
 * @return true if pattern written + read + erased all match 100%.
 */
bool staging_selftest(int* out_first_diff);

} /* namespace ota */
