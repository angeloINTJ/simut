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
 * antes (ou estar pós-IRQ-disable durante apply). */
bool __not_in_flash_func(ota_metadata_write)(const UpdateMetadata& in) {
    /* Setor inteiro vai a 0xFF; a página 0 recebe os 256 B do struct. */
    uint8_t page[OTA_FLASH_PAGE_SIZE];
    memcpy(page, &in, sizeof(in));
    /* Pad já está embutido: reserved[57] são 4*57 = 228 bytes que somados
     * aos 28 B antes (7 uint32 = 28) totalizam 256. memcpy copia tudo. */

    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(OTA_METADATA_OFFSET, OTA_METADATA_SIZE);
    flash_range_program(OTA_METADATA_OFFSET, page, OTA_FLASH_PAGE_SIZE);
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
