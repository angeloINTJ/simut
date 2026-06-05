/**
 * @file src/ota/ota_layout.h
 * @brief Flash memory layout for the SIMUT OTA system.
 *
 * @details ACTUAL Pico W (2 MB QSPI flash) layout confirmed by inspecting
 * the PlatformIO builder (`platforms/raspberrypi/builder/main.py`):
 *
 * | Address (XIP)             | Size     | Function                      |
 * |-----------------------------|---------|-----------------------------|
 * | 0x10000000 - 0x100FEFFF     | 1020 KB | Sketch slot (app + boot2)   |
 * | 0x100FF000 - 0x101FEFFF     | 1024 KB | LittleFS (normal mode) /    |
 * |                             |         | staging .bin.gz (update)    |
 * | 0x101FF000 - 0x101FFFFF     | 4 KB    | OTA metadata                |
 *
 * CORRECTION of the original plan:
 * - Plan:  sketch=1024KB, staging=1020KB, metadata=4KB
 * - Real:  sketch=1020KB, staging=1024KB, metadata=4KB (was EEPROM)
 *
 * The 4 KiB at the end were RECLAIMED from the emulated EEPROM of the
 * arduino-pico core. Verified: SIMUT does not include <EEPROM.h>
 * in any .cpp/.h/.ino — zero collision risk.
 *
 * WARNING: never call EEPROM API in SIMUT (`EEPROM.begin/read/
 * write/commit`). If any future library needs it, OTA metadata
 * will have to migrate elsewhere.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>

/* Size constants — in bytes. */
#define OTA_FLASH_TOTAL (2u * 1024u * 1024u) /* 2 MB Pico W */
#define OTA_EEPROM_RESERVED (4u * 1024u) /* 4 KB emulated EEPROM — reclaimed */
#define OTA_FILESYSTEM_SIZE (1u * 1024u * 1024u) /* 1 MB LittleFS — matches platformio.ini */

/* Offsets relative to flash start (0x10000000 when via XIP). */
#define OTA_APP_OFFSET (0u)
#define OTA_APP_MAX_SIZE (OTA_FLASH_TOTAL - OTA_EEPROM_RESERVED - OTA_FILESYSTEM_SIZE)
 /* 0x000000 - 0x0FEFFF (1020 KB) */

#define OTA_STAGING_OFFSET (OTA_APP_MAX_SIZE) /* 0x0FF000 */
#define OTA_STAGING_MAX_SIZE (OTA_FILESYSTEM_SIZE) /* 0x100000 (1024 KB) */

#define OTA_METADATA_OFFSET (OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE) /* 0x1FF000 */
#define OTA_METADATA_SIZE (OTA_EEPROM_RESERVED) /* 4 KB */

/* Critical config snapshot in the LAST sector of the staging area.
 * Typical firmware occupies ~1.004 KiB → last 4 KiB sector untouched
 * by apply. Persists through destructive apply; only erased by
 * staging_erase_all of the NEXT OTA cycle, at which point it no longer
 * matters (current OTA snapshot already consumed at boot).
 *
 * Maximum safe sketch keeping snapshot intact: 1.020 KiB - 4 KiB =
 * 1.016 KiB. Currently at ~1.004 KiB → 12 KiB margin. */
#define OTA_SNAPSHOT_OFFSET (OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE - OTA_FLASH_SECTOR_SIZE)
 /* 0x1FE000 */
#define OTA_SNAPSHOT_SIZE (OTA_FLASH_SECTOR_SIZE) /* 4 KiB */

/* Compile-time sanity check. */
#ifdef __cplusplus
static_assert(OTA_APP_OFFSET == 0u, "App offset must be 0");
static_assert(OTA_APP_OFFSET + OTA_APP_MAX_SIZE == OTA_STAGING_OFFSET, "App and staging are not adjacent");
static_assert(OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE == OTA_METADATA_OFFSET, "Staging and metadata are not adjacent");
static_assert(OTA_METADATA_OFFSET + OTA_METADATA_SIZE == OTA_FLASH_TOTAL, "Layout does not cover full flash");
static_assert((OTA_STAGING_OFFSET % 4096u) == 0u, "Staging must be 4KB-aligned for flash erase");
static_assert((OTA_METADATA_OFFSET % 4096u) == 0u, "Metadata must be 4KB-aligned for flash erase");
#endif

/* Pico SDK hardware constants (replicated to avoid including hardware/flash.h
 * in non-Pico files). Keep in sync. */
#define OTA_FLASH_PAGE_SIZE 256u /* Minimum write granularity (write in multiples). */
#define OTA_FLASH_SECTOR_SIZE 4096u /* Minimum erase granularity. */
