/**
 * @file    src/ota/staging.cpp
 * @brief   Implementação de erase/write/read da área de staging (Fase 4 OTA).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "staging.h"
#include "config_snapshot.h"
#include "../StorageManager.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <pico/multicore.h>
#include <string.h>
#if defined(PICO_RP2350) && PICO_RP2350
#include <pico/bootrom.h>
#include <pico/time.h>
#include <hardware/regs/addressmap.h>
#include <hardware/structs/watchdog.h>
#include "slot_stage.h"
#include "picobin.h"
#include "trial.h"
#include "../LogManager.h"
#endif

/* XIP_BASE = 0x10000000 — endereço onde a flash QSPI é mapeada para leitura. */
#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

namespace ota {

#if !OTA_RP2040_MAP
/* The RP2350's staging area is the slot the board did not boot from
 * (slot_stage.h). Erases and programs run with interrupts off, by physical
 * offset; the session keeps Core 1 parked, as on the RP2040. Reads go through
 * the untranslated window, because the slot being written lies outside the
 * window the boot ROM maps for the running one. */
static bool __not_in_flash_func(slot_erase)(uint32_t phys) {
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(phys, OTA_FLASH_SECTOR_SIZE);
    restore_interrupts(saved_irq);
    watchdog_update();
    return true;
}

/* One 64 KB block: the ROM's range erase takes the block command for an
 * aligned 64 KB (flash_range_erase), several times faster than 16 sectors. */
static bool __not_in_flash_func(slot_erase_block)(uint32_t phys) {
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(phys, SLOT_BLOCK);
    restore_interrupts(saved_irq);
    watchdog_update();
    return true;
}

static bool __not_in_flash_func(slot_program)(uint32_t phys, const uint8_t* data, uint32_t len) {
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_program(phys, data, len);
    restore_interrupts(saved_irq);
    return true;
}

static void slot_read(uint32_t phys, uint8_t* dst, uint32_t len) {
    memcpy(dst, (const void*)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + phys), len);
}

static const SlotFlashOps kSlotOps = { slot_erase, slot_erase_block, slot_program, slot_read };
static SlotStage s_slot;

static uint32_t inactive_slot() {
    boot_info_t bi;
    const int part = rom_get_boot_info(&bi) ? bi.partition : -1;
    return slot_inactive_offset(part, OTA_RP2350_SLOT_A_OFFSET, OTA_RP2350_SLOT_B_OFFSET);
}

bool staging_install_available() { return inactive_slot() != SLOT_NONE; }
bool staging_prepare(uint32_t len) { return slot_stage_prepare(s_slot, len); }
bool staging_mark_ready() { return slot_stage_mark_ready(s_slot); }
bool staging_ready() { return s_slot.active && s_slot.ready && !s_slot.committed; }
bool staging_commit() { return slot_stage_commit(s_slot); }
uint32_t staging_slot_offset() { return s_slot.slot_off; }
#endif

#if !OTA_RP2040_MAP
/* ---------------------------------------------------------------------------
 * The first boot of an update, on trial (trial.h; docs/analysis/OTA_AB_RP2350.md,
 * step 5)
 * ------------------------------------------------------------------------- */

static TrialClock s_trial;
static bool s_trialBootOk;           /* LittleFS and the configuration, as setup( ) found them */
static char s_reverted[16];          /* the version the ROM went back from, at this boot */
static repeating_timer_t s_guardTimer;
static volatile bool s_guardOn;
/* explicit_buy clears the flag by reading the sector that holds it into this
 * buffer, erasing the sector and writing it back: a whole sector, word-aligned. */
static uint32_t s_buyBuf[SLOT_SECTOR / 4];
/* The setup( ) guard's watchdog: under the RP2350's ceiling of 16.7 s (a 24-bit
 * count of 1 µs ticks), fed every second. */
static constexpr uint32_t TRIAL_GUARD_WDT_MS = 16000u;

bool trial_boot_pending() {
    boot_info_t bi;
    return rom_get_boot_info(&bi) &&
           (bi.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING) != 0;
}

/* The ROM arms a 16.7 s watchdog for the trial, and setup( ) switches it off on
 * every boot (main.cpp): setup( ) runs for tens of seconds and only loop( )
 * feeds. Without this, an update that hangs in setup( ) would stay hung until
 * someone reset the board. With it, the timer feeds until the deadline and
 * then stops, so a setup( ) that never ends goes back like a loop( ) that
 * never buys. */
static bool trial_guard_feed(repeating_timer_t*) {
    if (!s_guardOn || millis() >= TRIAL_DEADLINE_MS) {
        s_guardOn = false;
        return false;
    }
    watchdog_update();
    watchdog_hw->scratch[6] = millis();   /* the uptime at the last feed, as loop( ) stamps it */
    return true;
}

void trial_guard_begin() {
    if (!trial_boot_pending()) return;
    /* A boot on trial follows the ROM's FLASH_UPDATE reboot, which leaves its
     * parameters in the watchdog's scratch[6] (the reboot type) and scratch[7]
     * (0xB007C0D3), varm_apis.c. Those are the uptime at the last feed and the
     * web position the autopsy reads, and only loop( ) writes them: a setup( )
     * hung on trial read up 0 min and hp out of range (bands 4000 and 2999, on
     * the bench 2026-10-04) where it had run 300 s and served no request. The
     * session that ended in that reboot asked for it, so nothing is lost. */
    watchdog_hw->scratch[6] = 0;
    watchdog_hw->scratch[7] = 0;
    watchdog_enable(TRIAL_GUARD_WDT_MS, 1);
    s_guardOn = true;
    if (!add_repeating_timer_ms(-1000, trial_guard_feed, nullptr, &s_guardTimer)) {
        /* No timer, no guard: this boot goes on as any other, watchdog off. */
        s_guardOn = false;
        hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    }
}

void trial_guard_end() {
    if (!s_guardOn) return;
    s_guardOn = false;
    cancel_repeating_timer(&s_guardTimer);
}

void trial_boot(StorageManager* storage, bool fsOk) {
    memset(&s_trial, 0, sizeof(s_trial));
    s_reverted[0] = '\0';
    boot_info_t bi;
    if (!storage || !rom_get_boot_info(&bi)) return;
    if (bi.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING) {
        s_trial.pending = true;
        s_trialBootOk = fsOk && !storage->configDiscarded();
        const uint32_t self = slot_active_offset(bi.partition, OTA_RP2350_SLOT_A_OFFSET,
                                                 OTA_RP2350_SLOT_B_OFFSET);
        if (self != SLOT_NONE) LogManager::instance().setRebootSlot(XIP_BASE + self);
        LOG_CODE(LOG_INFO, "OTA", OTA_TRIAL_STARTED, trial_version_code(SIMUT_VERSION),
                 s_trialBootOk ? "kept after a healthy minute, or the previous image comes back"
                               : "LittleFS or the configuration unread: the previous image comes back");
        return;
    }
    /* Not on trial. An image flagged for trial in the other slot is one the ROM
     * went back from: a reset, a power cut, the watchdog or the deadline came
     * before its buy. The ROM never boots it again (only right after its own
     * FLASH_UPDATE reboot), so its first sector goes: this is said once, and
     * the slot holds no image the ROM could weigh. */
    const uint32_t other = slot_inactive_offset(bi.partition, OTA_RP2350_SLOT_A_OFFSET,
                                                OTA_RP2350_SLOT_B_OFFSET);
    if (other == SLOT_NONE) return;
    const uint8_t* slot = (const uint8_t*)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + other);
    const PicobinBlock b = picobin_first_block(slot, SLOT_SECTOR);
    if (!picobin_is_rp2350_arm_exe(b) || !picobin_is_trial(b)) return;
    const uint32_t t0 = millis();
    if (!trial_tag_version(slot, OTA_RP2350_SLOT_SIZE, s_reverted, sizeof(s_reverted)))
        strcpy(s_reverted, "?");
    const uint32_t scanMs = millis() - t0;
    LOG_CODE(LOG_WARN, "OTA", OTA_TRIAL_REVERTED, trial_version_code(s_reverted),
             String("v") + s_reverted + " was not kept, v" SIMUT_VERSION " runs (tag read in " +
             scanMs + " ms)");
    storage->enterFlashSafeMode();
    slot_erase(other);
    storage->exitFlashSafeMode();
}

static int __not_in_flash_func(trial_buy_rom)(rom_explicit_buy_fn fn) {
    const uint32_t irq = save_and_disable_interrupts();
    const int rc = fn((uint8_t*)s_buyBuf, sizeof(s_buyBuf));
    restore_interrupts(irq);
    return rc;
}

void trial_poll(StorageManager* storage, bool networkOk) {
    const TrialAction a = trial_step(s_trial, s_trialBootOk && networkOk, millis());
    if (a == TrialAction::NONE || !storage) return;
    if (a == TrialAction::BUY) {
        /* The ROM's explicit_buy, without the SDK's rom_explicit_buy( ): that
         * one goes through flash_safe_execute( ), which needs Core 1 set up as
         * a multicore_lockout victim, and SIMUT parks Core 1 its own way. */
        rom_explicit_buy_fn fn = (rom_explicit_buy_fn)rom_func_lookup(ROM_FUNC_EXPLICIT_BUY);
        int rc = PICO_ERROR_GENERIC;
        if (fn) {
            storage->enterFlashSafeMode();
            rc = trial_buy_rom(fn);
            storage->exitFlashSafeMode();
        }
        /* explicit_buy starts by clearing the watchdog's enable bit
         * (s_varm_api_explicit_buy, varm_launch_image.c), and the bench's A2
         * does (2026-10-04: ENABLE set before the call, clear after). The next
         * WdtWindow would set it again, and every log written to flash opens
         * one; a log kept in RAM during a touch or a heavy task opens none,
         * and the board would run unwatched until something did. */
        watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);
        if (rc == 0) {
            s_trial.pending = false;
            LogManager::instance().setRebootSlot(0);
            LOG_CODE(LOG_INFO, "OTA", OTA_TRIAL_CONFIRMED, (int)(millis() / 1000u),
                     "the boot ROM keeps this image");
            storage->releaseTrialHold();
            return;
        }
        LOG_CODE(LOG_ERROR, "OTA", OTA_TRIAL_BUY_FAILED, rc, "the boot ROM did not keep this image");
    } else {
        LOG_CODE(LOG_WARN, "OTA", OTA_TRIAL_EXPIRED, (int)(millis() / 1000u),
                 "not a healthy minute in time: back to the previous image");
    }
    /* A plain reboot, and the ROM boots the image the update replaced. */
    s_trial.pending = false;
    LogManager::instance().setRebootSlot(0);
    LogManager::instance().flushPendingIfAny();
    LogManager::instance().safeReboot();
}

bool trial_pending() { return s_trial.pending; }
const char* trial_reverted_version() { return s_reverted; }
#endif

/* ---------------------------------------------------------------------------
 * Erase
 * ------------------------------------------------------------------------- */

bool __not_in_flash_func(staging_erase_sector)(uint32_t offset_in_staging) {
#if !OTA_RP2040_MAP
    return slot_stage_erase(s_slot, offset_in_staging);
#else
    if (offset_in_staging % OTA_FLASH_SECTOR_SIZE != 0) return false;
    if (offset_in_staging >= OTA_STAGING_MAX_SIZE) return false;

    uint32_t flash_offs = OTA_STAGING_OFFSET + offset_in_staging;
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(flash_offs, OTA_FLASH_SECTOR_SIZE);
    restore_interrupts(saved_irq);
    return true;
#endif
}

bool __not_in_flash_func(staging_erase_all)() {
#if !OTA_RP2040_MAP
    return false;
#else
    /* Apaga setor por setor (4 KB cada) com WDT feed entre cada um.
     * Apagar 1 MB inteiro de uma vez levaria ~5-10s e estouraria WDT
     * se ele estivesse muito apertado. Setor isolado: ~50ms. */
    constexpr uint32_t N_SECTORS = OTA_STAGING_MAX_SIZE / OTA_FLASH_SECTOR_SIZE;
    for (uint32_t i = 0; i < N_SECTORS; i++) {
        watchdog_update();
        uint32_t saved_irq = save_and_disable_interrupts();
        flash_range_erase(OTA_STAGING_OFFSET + i * OTA_FLASH_SECTOR_SIZE,
                          OTA_FLASH_SECTOR_SIZE);
        restore_interrupts(saved_irq);
    }
    watchdog_update();
    return true;
#endif
}

/* ---------------------------------------------------------------------------
 * Write
 * ------------------------------------------------------------------------- */

bool __not_in_flash_func(staging_write)(uint32_t offset_in_staging,
                                        const uint8_t* data, size_t len) {
#if !OTA_RP2040_MAP
    return slot_stage_write(s_slot, offset_in_staging, data, (uint32_t)len);
#else
    if (!data || len == 0) return false;
    if (offset_in_staging % OTA_FLASH_PAGE_SIZE != 0) return false;
    if (len % OTA_FLASH_PAGE_SIZE != 0) return false;
    if (offset_in_staging + len > OTA_STAGING_MAX_SIZE) return false;

    uint32_t flash_offs = OTA_STAGING_OFFSET + offset_in_staging;
    /* flash_range_program processa em blocos de FLASH_PAGE_SIZE; loop em
     * chunks de 4 KB para alimentar WDT entre eles. */
    constexpr size_t CHUNK = 4096;
    size_t off = 0;
    while (off < len) {
        size_t take = (len - off > CHUNK) ? CHUNK : (len - off);
        watchdog_update();
        uint32_t saved_irq = save_and_disable_interrupts();
        flash_range_program(flash_offs + off, data + off, take);
        restore_interrupts(saved_irq);
        off += take;
    }
    watchdog_update();
    return true;
#endif
}

/* ---------------------------------------------------------------------------
 * Read (XIP)
 * ------------------------------------------------------------------------- */

void staging_read(uint32_t offset_in_staging, uint8_t* dst, size_t len) {
#if !OTA_RP2040_MAP
    slot_stage_read(s_slot, offset_in_staging, dst, (uint32_t)len);
#else
    if (!dst || len == 0) return;
    if (offset_in_staging + len > OTA_STAGING_MAX_SIZE) return;
    const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_STAGING_OFFSET + offset_in_staging);
    memcpy(dst, src, len);
#endif
}

/* ---------------------------------------------------------------------------
 * Sessão (mount/unmount LFS + Core 1 lockout)
 * ------------------------------------------------------------------------- */

bool staging_session_begin(StorageManager* storage) {
#if !OTA_RP2040_MAP
    (void)storage;
    return false;  /* the RP2350 erases on demand: staging_session_begin_lite */
#else
    if (!storage) return false;

    /* Fase 9 — captura snapshot da config ANTES de qualquer flash safe mode.
     *
     * IMPORTANTE: serialize tem que rodar com Core 1 ATIVO. LittleFS.open/read
     * usa mutexes internos que conflitam com `multicore_lockout` (Core 1
     * congelado pelo enterFlashSafeMode), causando hang do display.
     *
     * Sequência:
     *   1) serialize: lê system.bin via LFS, monta payload em s_applier_buf
     *      (sem flash write). Sem lockout.
     *   2) enterFlashSafeMode: Core 1 lockado.
     *   3) LittleFS.end + erase staging (1 MiB, ~7-10 s).
     *   4) commit: program no último setor da staging (já apagada).
     *
     * Falha em (1) é não-fatal: segue stage; user restaura `.bkp` manual. */
    const uint16_t snap_len = ota_snapshot_serialize();
    if (snap_len == 0) {
        Serial.println("[OTA] WARN: config snapshot serialize failed; relying on .bkp");
    }

    /* Pausa Core 1 + sinaliza heavy ops para outros subsystemas. */
    storage->enterFlashSafeMode();

    /* Desmonta LittleFS — a partir daqui ninguém pode ler arquivos. */
    LittleFS.end();

    /* Apaga staging (1 MB). */
    bool ok = staging_erase_all();

    if (ok && snap_len > 0) {
        /* Snapshot vai no último setor da staging (já apagada). Falha aqui
         * é não-fatal — stage segue e device sobe em factory pós-apply. */
        if (!ota_snapshot_commit(snap_len)) {
            Serial.println("[OTA] WARN: config snapshot commit failed; relying on .bkp");
        }
    }

    if (!ok) {
        /* Tenta remontar pra deixar o sistema utilizável. */
        LittleFS.begin();
        storage->exitFlashSafeMode();
        return false;
    }
    /* NÃO sai do safe mode aqui — o caller (upload/apply) controla
     * o ciclo de vida. Chamar staging_session_end pra liberar. */
    return true;
#endif
}

/* v4.4.0: variante sem erase upfront — caller faz erase on-demand. */
bool staging_session_begin_lite(StorageManager* storage) {
#if !OTA_RP2040_MAP
    /* LittleFS stays mounted: the slot is not the filesystem. Whatever happens
     * below, an image an earlier stage kept for the apply is gone. */
    memset(&s_slot, 0, sizeof(s_slot));
    if (!storage) return false;
    const uint32_t off = inactive_slot();
    if (off == SLOT_NONE) return false;
    storage->enterFlashSafeMode();
    if (!slot_stage_begin(s_slot, &kSlotOps, off, OTA_RP2350_SLOT_SIZE)) {
        storage->exitFlashSafeMode();
        return false;
    }
    return true;
#else
    if (!storage) return false;
    storage->enterFlashSafeMode();
    LittleFS.end();
    return true;
#endif
}

bool staging_session_end(StorageManager* storage) {
    if (!storage) return false;
#if !OTA_RP2040_MAP
    /* The RP2350 never unmounted LittleFS: only Core 1 comes back. */
    storage->exitFlashSafeMode();
    return true;
#else
    /* Tenta remontar; LittleFS.begin() vai ver "FS inválida" (apagada)
     * e formatar do zero. */
    bool mounted = LittleFS.begin();
    storage->exitFlashSafeMode();
    return mounted;
#endif
}

} /* namespace ota */
