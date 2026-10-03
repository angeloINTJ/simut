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
#include <hardware/regs/addressmap.h>
#include "slot_stage.h"
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
