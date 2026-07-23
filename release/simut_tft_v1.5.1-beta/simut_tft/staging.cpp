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
#include "StorageManager.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <pico/multicore.h>
#include <string.h>

/* XIP_BASE = 0x10000000 — endereço onde a flash QSPI é mapeada para leitura. */
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
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(flash_offs, OTA_FLASH_SECTOR_SIZE);
    restore_interrupts(saved_irq);
    return true;
}

bool __not_in_flash_func(staging_erase_all)() {
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
 * Sessão (mount/unmount LFS + Core 1 lockout)
 * ------------------------------------------------------------------------- */

bool staging_session_begin(StorageManager* storage) {
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
}

/* v4.4.0: variante sem erase upfront — caller faz erase on-demand. */
bool staging_session_begin_lite(StorageManager* storage) {
    if (!storage) return false;
    storage->enterFlashSafeMode();
    LittleFS.end();
    return true;
}

bool staging_session_end(StorageManager* storage) {
    if (!storage) return false;
    /* Tenta remontar; LittleFS.begin() vai ver "FS inválida" (apagada)
     * e formatar do zero. */
    bool mounted = LittleFS.begin();
    storage->exitFlashSafeMode();
    return mounted;
}

/* ---------------------------------------------------------------------------
 * Self-test
 * ------------------------------------------------------------------------- */

bool staging_selftest(int* out_first_diff) {
    if (out_first_diff) *out_first_diff = -1;

    /* 1) Apaga primeiro setor e confirma que ficou todo 0xFF. */
    if (!staging_erase_sector(0)) return false;
    {
        uint8_t buf[256];
        for (int p = 0; p < 16; p++) {  /* 16 páginas × 256 = 4 KB */
            staging_read((uint32_t)p * 256u, buf, sizeof(buf));
            for (size_t i = 0; i < sizeof(buf); i++) {
                if (buf[i] != 0xFF) {
                    if (out_first_diff) *out_first_diff = (int)(p * 256 + i);
                    return false;
                }
            }
        }
    }

    /* 2) Escreve padrão xadrez 0xAA/0x55 em 4 páginas (1 KB). */
    uint8_t pattern[1024];
    for (size_t i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (i & 1) ? 0x55 : 0xAA;
    }
    if (!staging_write(0, pattern, sizeof(pattern))) return false;

    /* 3) Lê de volta e compara. */
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

    /* 4) Apaga de novo (deixa staging em estado conhecido pra upload futuro). */
    if (!staging_erase_sector(0)) return false;

    return true;
}

} /* namespace ota */
