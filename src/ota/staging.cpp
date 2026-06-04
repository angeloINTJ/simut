/**
 * @file src/ota/staging.cpp
 * @brief Implementation of erase/write/read for the staging area (OTA).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "staging.h"
#include "config_snapshot.h"
#include "StorageManager.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <pico/multicore.h>
#include <string.h>

/* XIP_BASE = 0x10000000 — address where QSPI flash is mapped for reading. */
#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

namespace ota {

/* ---------------------------------------------------------------------------
 * Erase
 * ------------------------------------------------------------------------- */

bool __not_in_flash_func(staging_erase_sector)(uint32_t offset_in_staging) {
 if (offset_in_staging % OTA_FLASH_SECTOR_SIZE != 0) return false;
 if (offset_in_staging >= OTA_STAGING_MAX_SIZE) return false;

 uint32_t flash_offs = OTA_STAGING_OFFSET + offset_in_staging;
 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_erase(flash_offs, OTA_FLASH_SECTOR_SIZE);
 restore_interrupts(saved_irq);
 return true;
}

bool __not_in_flash_func(staging_erase_all)( ) {
 /* Erases sector by sector (4 KB each) with WDT feed between each.
 * Erasing the entire 1 MB at once would take ~5-10s and blow WDT
 * if it were too tight. Isolated sector: ~50ms. */
 constexpr uint32_t N_SECTORS = OTA_STAGING_MAX_SIZE / OTA_FLASH_SECTOR_SIZE;
 for (uint32_t i = 0; i < N_SECTORS; i++) {
 watchdog_update( );
 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_erase(OTA_STAGING_OFFSET + i * OTA_FLASH_SECTOR_SIZE,
 OTA_FLASH_SECTOR_SIZE);
 restore_interrupts(saved_irq);
 }
 watchdog_update( );
 return true;
}

/* ---------------------------------------------------------------------------
 * Write
 * ------------------------------------------------------------------------- */

bool __not_in_flash_func(staging_write)(uint32_t offset_in_staging,
 const uint8_t* data, size_t len) {
 if (!data || len == 0) return false;
 if (offset_in_staging % OTA_FLASH_PAGE_SIZE != 0) return false;
 if (len % OTA_FLASH_PAGE_SIZE != 0) return false;
 if (offset_in_staging + len > OTA_STAGING_MAX_SIZE) return false;

 uint32_t flash_offs = OTA_STAGING_OFFSET + offset_in_staging;
 /* flash_range_program processes in FLASH_PAGE_SIZE blocks; loop in
 * 4 KB chunks to feed WDT between them. */
 constexpr size_t CHUNK = 4096;
 size_t off = 0;
 while (off < len) {
 size_t take = (len - off > CHUNK) ? CHUNK : (len - off);
 watchdog_update( );
 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_program(flash_offs + off, data + off, take);
 restore_interrupts(saved_irq);
 off += take;
 }
 watchdog_update( );
 return true;
}

/* ---------------------------------------------------------------------------
 * Read (XIP)
 * ------------------------------------------------------------------------- */

void staging_read(uint32_t offset_in_staging, uint8_t* dst, size_t len) {
 if (!dst || len == 0) return;
 if (offset_in_staging + len > OTA_STAGING_MAX_SIZE) return;
 const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_STAGING_OFFSET + offset_in_staging);
 memcpy(dst, src, len);
}

/* ---------------------------------------------------------------------------
 * Session (mount/unmount LFS + Core 1 lockout)
 * ------------------------------------------------------------------------- */

bool staging_session_begin(StorageManager* storage) {
 if (!storage) return false;

 /* Capture config snapshot BEFORE any flash safe mode.
 *
 * IMPORTANT: serialize must run with Core 1 ACTIVE. LittleFS.open/read
 * uses internal mutexes that conflict with `multicore_lockout` (Core 1
 * frozen by enterFlashSafeMode), causing display hang.
 *
 * Sequence:
 * 1) serialize: reads system.bin via LFS, assembles payload in s_applier_buf
 * (no flash write). No lockout.
 * 2) enterFlashSafeMode: Core 1 locked.
 * 3) LittleFS.end + erase staging (1 MiB, ~7-10s).
 * 4) commit: program to last staging sector (already erased).
 *
 * Failure in (1) is non-fatal: proceeds with stage; user restores `.bkp` manually. */
 const uint16_t snap_len = ota_snapshot_serialize( );
 if (snap_len == 0) {
 Serial.println("[OTA] WARN: config snapshot serialize failed; relying on .bkp");
 }

 /* Pause Core 1 + signal heavy ops to other subsystems. */
 storage->enterFlashSafeMode( );

 /* Unmount LittleFS — from here no one can read files. */
 LittleFS.end( );

 /* Erase staging (1 MB). */
 bool ok = staging_erase_all( );

 if (ok && snap_len > 0) {
 /* Snapshot goes to last staging sector (already erased). Failure here
 * is non-fatal — stage proceeds and device boots in factory post-apply. */
 if (!ota_snapshot_commit(snap_len)) {
 Serial.println("[OTA] WARN: config snapshot commit failed; relying on .bkp");
 }
 }

 if (!ok) {
 /* Try to remount to leave the system usable. */
 LittleFS.begin( );
 storage->exitFlashSafeMode( );
 return false;
 }
 /* Does NOT exit safe mode here — the caller (upload/apply) controls
 * the lifecycle. Call staging_session_end to release. */
 return true;
}

/* Variant without upfront erase — caller does on-demand erase. */
bool staging_session_begin_lite(StorageManager* storage) {
 if (!storage) return false;
 storage->enterFlashSafeMode( );
 LittleFS.end( );
 return true;
}

bool staging_session_end(StorageManager* storage) {
 if (!storage) return false;
 /* Try to remount; LittleFS.begin() will see "invalid FS" (erased)
 * and format from scratch. */
 bool mounted = LittleFS.begin( );
 storage->exitFlashSafeMode( );
 return mounted;
}

/* ---------------------------------------------------------------------------
 * Self-test
 * ------------------------------------------------------------------------- */

bool staging_selftest(int* out_first_diff) {
 if (out_first_diff) *out_first_diff = -1;

 /* 1) Erase first sector and confirm it is all 0xFF. */
 if (!staging_erase_sector(0)) return false;
 {
 uint8_t buf[256];
 for (int p = 0; p < 16; p++) { /* 16 pages × 256 = 4 KB */
 staging_read((uint32_t)p * 256u, buf, sizeof(buf));
 for (size_t i = 0; i < sizeof(buf); i++) {
 if (buf[i] != 0xFF) {
 if (out_first_diff) *out_first_diff = (int)(p * 256 + i);
 return false;
 }
 }
 }
 }

 /* 2) Write checkerboard pattern 0xAA/0x55 in 4 pages (1 KB). */
 uint8_t pattern[1024];
 for (size_t i = 0; i < sizeof(pattern); i++) {
 pattern[i] = (i & 1) ? 0x55 : 0xAA;
 }
 if (!staging_write(0, pattern, sizeof(pattern))) return false;

 /* 3) Read back and compare. */
 {
 uint8_t buf[1024];
 staging_read(0, buf, sizeof(buf));
 for (size_t i = 0; i < sizeof(buf); i++) {
 if (buf[i] != pattern[i]) {
 if (out_first_diff) *out_first_diff = (int)i;
 return false;
 }
 }
 }

 /* 4) Erase again (leaves staging in known state for future upload). */
 if (!staging_erase_sector(0)) return false;

 return true;
}

} /* namespace ota */
