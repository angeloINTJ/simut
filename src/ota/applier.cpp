/**
 * @file src/ota/applier.cpp
 * @brief SRAM-resident applier — real (RAW mode). VALIDATED.
 *
 * @details Apply full cycle tested: stage upload → commit metadata →
 * /api/ota/apply → applier SRAM → reboot → boot OK in ~60s.
 *
 * RESOLVED BUGS:
 *
 * Bug 1: Apply was aborting mid-way. Diagnosis via picotool save showed
 * that the erase loop died after ~86 or ~196 iterations.
 * Cause: pico-sdk watchdog_update()/watchdog_reboot() live in
 * flash app slot (not __not_in_flash_func). When erase clears
 * the region where they reside, next call faults → BOOTSEL.
 * Fix: replaced with pure MMIO inlines (applier_wdt_feed
 * + applier_reboot). Fixed TRIGGER bit from
 * (1u<<30 ENABLE — wrong) to (1u<<31 TRIGGER — correct), which
 * made HW WDT fire at 8s default instead of feeding.
 *
 * Bug "boot2 not programmed": Sector 0 remained 0xFF even after
 * flash_range_program(0, ...). Empirically: programming sector 0 AFTER
 * bulk erase of 1..N-1 has an internal race with boot2 cache/QSPI ROM.
 * Fix: program sector 0 ISOLATED (erase + program 4 KiB)
 * BEFORE bulk erase. Validated: byte-perfect in sector 0.
 *
 * Bug "intermittent boot": Was a false positive — post-apply boot actually
 * takes ~60s because it combines: (a) LFS auto-format ~13s, (b) Core 1
 * lockout stuck recovery ~10s, (c) factory init: password regen +
 * touch cal default + WiFi disconnected.
 *
 * KNOWN LIMITATIONS:
 * - LFS is reformatted (user data lost) because staging area
 * SHARES partition with LittleFS.
 * - Sector 233 (offset 0xE9000) preserved with BTstack runtime
 * TLV. It is the __bluetooth_tlv reserved region (8 KiB), not code.
 * BTstack init re-initializes transparently.
 * - WiFi config + admin password + sensor mapping lost.
 *
 * Pre-conditions of the applier (caller orchestrator guarantees):
 * - WiFi/CYW43 off.
 * - LittleFS unmounted.
 * - Core 1 paused via multicore_lockout.
 * - Global IRQs disabled.
 * - Metadata.state == APPLYING and persisted in flash.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "applier.h"
#include "ota_layout.h"

#include <Arduino.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <string.h>

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

/* Hardcoded HW peripheral addresses — do not depend on includes or
 * functions that may be in erased flash app slot. */
#define WATCHDOG_BASE_ADDR 0x40058000u
#define WATCHDOG_CTRL_OFFSET 0x00u
#define WATCHDOG_LOAD_OFFSET 0x04u
#define WATCHDOG_SCRATCH4_OFFSET 0x18u
#define WATCHDOG_SET_ALIAS 0x00002000u /* RP2040 SET alias offset */
#define WATCHDOG_CLR_ALIAS 0x00003000u /* RP2040 CLR alias offset */
#define WATCHDOG_CTRL_TRIG (1u << 31) /* TRIGGER bit (1u<<30 is ENABLE — bug) */
#define WATCHDOG_CTRL_ENABLE (1u << 30) /* ENABLE bit */

/* PSM (Power Supply Monitor) — controls which peripherals the watchdog reset
 * drops. SDK pico-sdk hardware_watchdog/watchdog.c::_watchdog_enable
 * uses `PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS)`,
 * EXCLUDING the physical oscillators (ROSC and XOSC).
 *
 * Reason: resetting ROSC/XOSC forces a re-startup that can briefly leave
 * derived PLLs (PLL_SYS, PLL_USB) in an unstable state. If any peripheral
 * starts operating before `runtime_init_clocks` re-initializes the clocks,
 * it may end up with invalid state — observed symptom: USB CDC enumerates but
 * host does not receive data post-watchdog reboot.
 *
 * Fix: align with SDK instead of 0xFFFFFFFF. */
#define PSM_BASE_ADDR 0x40010000u
#define PSM_WDSEL_OFFSET 0x08u
#define PSM_WDSEL_ROSC_BIT (1u << 0)
#define PSM_WDSEL_XOSC_BIT (1u << 1)
#define PSM_WDSEL_BITS_ALL 0x0001FFFFu /* 17 valid bits */
#define PSM_WDSEL_RESET_MASK (PSM_WDSEL_BITS_ALL & ~(PSM_WDSEL_ROSC_BIT | PSM_WDSEL_XOSC_BIT))
 /* = 0x0001FFFC (all except ROSC/XOSC) */

/* SCB SYSRESETREQ (no longer used — incomplete, leaves SIO stale).
 * Kept for historical reference of the bug. */
#define SCB_AIRCR_ADDR 0xE000ED0Cu
#define SCB_AIRCR_KEY (0x05FAu << 16)
#define SCB_AIRCR_SYSRESET (1u << 2)

namespace ota {

/* 4 KiB SRAM buffer for sector-by-sector copy. In BSS (zero-init at
 * boot) — does not need __not_in_flash because BSS already lives in RAM
 * (0x20xxxxxx) and is accessible during flash_range_erase/program (QSPI off,
 * RAM intact).
 *
 * External linkage (no `static`) — also used by
 * `metadata.cpp::ota_metadata_write` and `ota_snapshot_write` to preserve
 * the whole sector during read-erase-program-all. Race-free: applier only
 * runs after `state=APPLYING` persisted and ends via reboot, never
 * concurrent with the other callers. Declared in `metadata.h`. */
uint8_t s_applier_buf[OTA_FLASH_SECTOR_SIZE];

/* CRC32 EDB88320 inline in SRAM (without calling flash-resident tables). */
static inline uint32_t __not_in_flash_func(crc32_byte_sram)(uint32_t crc, uint8_t b) {
 crc ^= b;
 for (int i = 0; i < 8; i++) {
 crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
 }
 return crc;
}

/* Watchdog feed inline — pico-sdk watchdog_update() lives in flash app slot
 * (NOT __not_in_flash_func), so the first call after app slot erase
 * faults. Here we write directly to the SET alias of the hardware register. */
static inline void __not_in_flash_func(applier_wdt_feed)( ) {
 *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_SET_ALIAS) =
 WATCHDOG_CTRL_TRIG;
}

/* Reboot inline via watchdog reset (PSM full reset). Replaces SCB
 * SYSRESETREQ which was incomplete — only reset M0+ cores,
 * left SIO/multicore mailbox/RESETS in stale state, making
 * arduino-pico Core 1 launch hang intermittently on next boot
 * (reproduced in ~30% of applies). Watchdog reset via PSM
 * drops ALL peripherals (SIO, RESETS, BUSCTRL etc), producing
 * a clean state equivalent to physical hard reset. Pico-sdk watchdog_reboot
 * lives in flash slot — we replicate the sequence inline with pure MMIO:
 *
 * 1. Set PSM->wdsel = valid bitmask (selector for full reset).
 * 2. Clear watchdog ENABLE to clear previous state.
 * 3. Clear scratch[4] (boot mode = normal boot, not stage2).
 * 4. Set LOAD to max (24-bit max = 0xFFFFFF).
 * 5. Set TRIGGER to fire immediate.
 * 6. Spin waiting for reset. */
static inline void __not_in_flash_func(applier_reboot)( ) {
 /* SDK pico-sdk hardware_watchdog/
 * watchdog.c::_watchdog_enable uses only TRIGGER when delay_ms=0
 * (immediate reset). Previously we used ENABLE|TRIGGER simultaneously,
 * which after reset left the watchdog ARMED with small LOAD
 * (10ms). Watchdog HW is NOT reset by PSM (not in
 * PSM_WDSEL_BITS) — only reset by physical power cycle (3V3
 * cycle). Result: post-reset, watchdog continued ENABLE with
 * LOAD=10ms → fired new reset every 10ms → post-OTA boot
 * entered infinite reset loop until power cycle.
 *
 * Autopsy symptom: sc3=0x80088000 (bit 31 = HW WDT reason flag).
 *
 * Fix: TRIGGER only (immediate reset). Before TRIGGER, do
 * explicit watchdog_disable (clear ENABLE) + LOAD = max to
 * ensure that even if something goes wrong, the timer won't fire
 * before the firmware re-initializes normally. */

 /* (1) Configure PSM for reset on watchdog — all peripherals
 * EXCEPT ROSC/XOSC, aligned with pico-sdk _watchdog_enable. */
 *(volatile uint32_t*)(PSM_BASE_ADDR + PSM_WDSEL_OFFSET) = PSM_WDSEL_RESET_MASK;

 /* (2) Clear ENABLE in ctrl (via CLR alias) — disables the timer */
 *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_CLR_ALIAS + WATCHDOG_CTRL_OFFSET) =
 WATCHDOG_CTRL_ENABLE;

 /* (3) Clear scratch[4] — boot ROM checks this magic; 0 = normal boot */
 *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_SCRATCH4_OFFSET) = 0;

 /* (4) LOAD = max (24-bit max = 0xFFFFFF). Watchdog HW persists post-reset
 * (not in PSM_WDSEL); large LOAD gives firmware enough time
 * to reach the first watchdog_update in loop(). */
 *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_LOAD_OFFSET) = 0xFFFFFFu;

 /* (5) Set TRIGGER only (NOT ENABLE). TRIGGER forces immediate reset.
 * ENABLE is not needed — would arm the timer post-reset, creating loop. */
 *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_SET_ALIAS + WATCHDOG_CTRL_OFFSET) =
 WATCHDOG_CTRL_TRIG;

 __asm volatile("dsb");
 while (1) { __asm volatile("nop"); }
}

bool __not_in_flash_func(ota_applier_run)(const UpdateMetadata* meta) {
 if (!meta || meta->magic != OTA_MAGIC_PENDING || meta->state != STATE_APPLYING) {
 applier_reboot( );
 }

 /* RAW mode: compressed_size == raw firmware size (already aligned
 * to 256 B by stage_session_end). uncompressed_crc32 == compressed_crc32
 * in raw mode (same bytes). */
 const uint32_t raw_size = meta->compressed_size;
 const uint32_t want_crc = meta->uncompressed_crc32;

 if (raw_size == 0 || raw_size > OTA_APP_MAX_SIZE) {
 applier_reboot( );
 }

 /* (1a) Program sector 0 (boot2) BEFORE bulk erase. Diagnosis:
 * flash_range_program(0,...) silently DID NOT persist WHEN called
 * AFTER a bulk erase of 255 sectors — sector 0 remained 0xFF even
 * with retry and page-by-page. Hypothesis: internal QSPI chip state
 * or ROM function has a race when the first program after bulk erase
 * is at offset 0. Moving this operation BEFORE bulk erase (after
 * isolated erase of sector 0 only) avoids the race. */
 {
 const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_STAGING_OFFSET);
 memcpy(s_applier_buf, src, OTA_FLASH_SECTOR_SIZE);

 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_erase(0u, OTA_FLASH_SECTOR_SIZE);
 flash_range_program(0u, s_applier_buf, OTA_FLASH_SECTOR_SIZE);
 restore_interrupts(saved_irq);
 applier_wdt_feed( );
 }

 /* (1b) Erase sectors 1..N-1 of the app slot. ~13s for ~1019 KiB. */
 constexpr uint32_t N_APP_SECTORS = OTA_APP_MAX_SIZE / OTA_FLASH_SECTOR_SIZE;
 for (uint32_t i = 1; i < N_APP_SECTORS; i++) {
 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_erase(i * OTA_FLASH_SECTOR_SIZE, OTA_FLASH_SECTOR_SIZE);
 restore_interrupts(saved_irq);
 applier_wdt_feed( );
 }

 /* (2) Program valid sectors from staging via XIP read + flash program. */
 const uint32_t n_data_sectors = (raw_size + OTA_FLASH_SECTOR_SIZE - 1u)
 / OTA_FLASH_SECTOR_SIZE;
 for (uint32_t i = 1; i < n_data_sectors; i++) {
 const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_STAGING_OFFSET +
 i * OTA_FLASH_SECTOR_SIZE);
 memcpy(s_applier_buf, src, OTA_FLASH_SECTOR_SIZE);

 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_program(i * OTA_FLASH_SECTOR_SIZE,
 s_applier_buf, OTA_FLASH_SECTOR_SIZE);
 restore_interrupts(saved_irq);
 applier_wdt_feed( );
 }

 /* (sector 0 already programmed in (1a) before bulk erase) */

 /* (3) Validate CRC of app slot post-write. */
 uint32_t crc = 0xFFFFFFFFu;
 for (uint32_t off = 0; off < raw_size; off++) {
 const uint8_t* p = (const uint8_t*)(XIP_BASE + OTA_APP_OFFSET + off);
 crc = crc32_byte_sram(crc, *p);
 if ((off & 0xFFFu) == 0u) applier_wdt_feed( );
 }
 crc ^= 0xFFFFFFFFu;

 /* (3.5) ERASE staging/LFS region. After apply, the 0xFF000 region
 * to OTA_SNAPSHOT_OFFSET is polluted with firmware upload bytes
 * (staging shares partition with LittleFS). On new boot, mountFS reads
 * firmware-bytes in the LFS superblock → fails → tries auto-format → each
 * erase calls flash_safe_execute → multicore_lockout → hangs in
 * post-OTA conditions.
 *
 * Erase here (with IRQs OFF + Core 1 lockout from orchestrator + flash
 * direct MMIO via __not_in_flash_func) ensures next boot sees
 * LFS region all-0xFF → mountFS detects clean → format quick path.
 *
 * Snapshot region (last staging sector, OTA_SNAPSHOT_OFFSET) is
 * PRESERVED — post-OTA config needs it. */
 {
 constexpr uint32_t LFS_ERASE_END = OTA_SNAPSHOT_OFFSET;
 for (uint32_t off = OTA_STAGING_OFFSET; off < LFS_ERASE_END; off += OTA_FLASH_SECTOR_SIZE) {
 uint32_t saved_irq = save_and_disable_interrupts( );
 flash_range_erase(off, OTA_FLASH_SECTOR_SIZE);
 restore_interrupts(saved_irq);
 applier_wdt_feed( );
 }
 }

 /* (4) Reboot. CRC mismatch info lost (no persistence possible
 * in IRQ-off mode); BootROM detects bad boot2 and falls to BOOTSEL
 * in case of broken write. */
 (void)crc;
 (void)want_crc;
 applier_reboot( );
 return false; /* unreachable */
}

} /* namespace ota */
