/**
 * @file    src/ota/metadata.cpp
 * @brief   Read/write do setor de metadata OTA (Fase 7+).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
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

/* Erase + program — IRQ disable interno (operação em flash exige).
 * Não chamar com Core 1 ativo: caller tem que entrar em flash safe mode
 * antes (ou estar pós-IRQ-disable durante apply).
 *
 * Fase 9: pages 1..15 (3840 B após page 0) carregam o ConfigSnapshot.
 * O setor inteiro é apagado a cada write, então preservamos a região do
 * snapshot lendo via XIP antes do erase, recompondo o sector inteiro em
 * `s_applier_buf`, e regravando 4 KiB. Custo: ~30 ms a mais por write. */
bool __not_in_flash_func(ota_metadata_write)(const UpdateMetadata& in) {
    /* Snapshot region atual (3840 B) — copia do XIP enquanto ainda é válido. */
    const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_METADATA_OFFSET);
    memcpy(s_applier_buf + OTA_FLASH_PAGE_SIZE, src + OTA_FLASH_PAGE_SIZE,
           OTA_FLASH_SECTOR_SIZE - OTA_FLASH_PAGE_SIZE);

    /* Page 0: novo UpdateMetadata. */
    memcpy(s_applier_buf, &in, sizeof(in));

    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(OTA_METADATA_OFFSET, OTA_METADATA_SIZE);
    flash_range_program(OTA_METADATA_OFFSET, s_applier_buf, OTA_FLASH_SECTOR_SIZE);
    restore_interrupts(saved_irq);
    return true;
}

/* Análogo a ota_metadata_write mas operando sobre as pages 1..15:
 * preserva page 0 (UpdateMetadata) atual e regrava o sector com o
 * novo snapshot_data nas pages 1..15. */
bool __not_in_flash_func(ota_snapshot_write)(const uint8_t* data, uint16_t len) {
    if (!data || len > (OTA_FLASH_SECTOR_SIZE - OTA_FLASH_PAGE_SIZE)) return false;

    /* Page 0: preserva UpdateMetadata atual. */
    const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_METADATA_OFFSET);
    memcpy(s_applier_buf, src, OTA_FLASH_PAGE_SIZE);

    /* Pages 1..15: snapshot + 0xFF pad. */
    memcpy(s_applier_buf + OTA_FLASH_PAGE_SIZE, data, len);
    memset(s_applier_buf + OTA_FLASH_PAGE_SIZE + len, 0xFF,
           OTA_FLASH_SECTOR_SIZE - OTA_FLASH_PAGE_SIZE - len);

    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(OTA_METADATA_OFFSET, OTA_METADATA_SIZE);
    flash_range_program(OTA_METADATA_OFFSET, s_applier_buf, OTA_FLASH_SECTOR_SIZE);
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

bool __not_in_flash_func(ota_metadata_clear)() {
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(OTA_METADATA_OFFSET, OTA_METADATA_SIZE);
    restore_interrupts(saved_irq);
    return true;
}

} /* namespace ota */
