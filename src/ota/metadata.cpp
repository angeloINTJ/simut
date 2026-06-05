/**
 * @file src/ota/metadata.cpp
 * @brief Read/write of the OTA metadata sector.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "metadata.h"
#include "ota_layout.h"

#include <Arduino.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <string.h>

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

namespace ota {

bool ota_metadata_read(UpdateMetadata& out) {
 const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_METADATA_OFFSET);
 memcpy(&out, src, sizeof(out));
 if (out.magic != OTA_MAGIC_PENDING) {
 memset(&out, 0, sizeof(out));
 return false;
 }
 return true;
}

/* Erase + program — internal IRQ disable (flash operation requires it).
 * Do not call with Core 1 active: caller must enter flash safe mode
 * first (or be post-IRQ-disable during apply).
 *
 * The config snapshot was moved to the last sector of the staging area
 * (`OTA_SNAPSHOT_OFFSET`) — isolated write, without affecting the
 * orchestrator path. Keeps 256 B program (hardware validated). */
bool __not_in_flash_func(ota_metadata_write)(const UpdateMetadata& in) {
 /* Whole sector goes to 0xFF; page 0 receives the 256 B struct. */
 uint8_t page[OTA_FLASH_PAGE_SIZE];
 memcpy(page, &in, sizeof(in));

 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_erase(OTA_METADATA_OFFSET, OTA_METADATA_SIZE);
 flash_range_program(OTA_METADATA_OFFSET, page, OTA_FLASH_PAGE_SIZE);
 restore_interrupts(saved_irq);
 return true;
}

/* Config snapshot: single sector (4 KiB) at the END of the staging area.
 * Fixed offset: STAGING_OFFSET + (STAGING_SIZE - SECTOR_SIZE) = 0x1FE000.
 *
 * Why here (and not in the metadata partition):
 * - Metadata write is called by the orchestrator during apply, on a path
 * already hardware-validated. Touching it in ota_metadata_write brings
 * regression risk in the apply.
 * - Staging has 1 MiB. Current firmware ~1.004 KiB. Last sector (4 KiB)
 * is rarely touched by the apply (which copies only staging[0..raw_size]).
 * - staging_erase_all erases 1 MiB entirely AFTER this sector has already
 * been written. See staging.cpp:staging_session_begin sequence.
 *
 * Pre-condition: caller in flash safe mode + sector already erased (part
 * of staging_erase_all). Here we only program. */
bool __not_in_flash_func(ota_snapshot_write)(const uint8_t* data, uint16_t len) {
 if (!data || len > OTA_FLASH_SECTOR_SIZE) return false;

 /* Copy to s_applier_buf + 0xFF padding up to 4 KiB (erase
 * granularity, even though program is per 256 B pages). */
 memcpy(s_applier_buf, data, len);
 memset(s_applier_buf + len, 0xFF, OTA_FLASH_SECTOR_SIZE - len);

 uint32_t saved_irq = save_and_disable_interrupts( );
 /* Sector already erased by staging_erase_all. Program directly. */
 flash_range_program(OTA_SNAPSHOT_OFFSET, s_applier_buf, OTA_FLASH_SECTOR_SIZE);
 restore_interrupts(saved_irq);
 return true;
}

bool ota_metadata_set_state(UpdateState st) {
 UpdateMetadata m;
 if (!ota_metadata_read(m)) {
 memset(&m, 0, sizeof(m));
 m.magic = OTA_MAGIC_PENDING;
 }
 m.state = (uint32_t)st;
 return ota_metadata_write(m);
}

bool __not_in_flash_func(ota_metadata_clear)( ) {
 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_erase(OTA_METADATA_OFFSET, OTA_METADATA_SIZE);
 restore_interrupts(saved_irq);
 return true;
}

} /* namespace ota */
