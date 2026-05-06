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
 * Fase 9 (corrigido v3.43.16): metadata partition continua sendo só
 * UpdateMetadata (page 0). O snapshot da config foi movido para o
 * último setor da staging area (`OTA_SNAPSHOT_OFFSET`) — write isolado,
 * sem afetar o caminho do orchestrator. Mantém 256 B program (validado
 * em HW desde v3.43.10/11). */
bool __not_in_flash_func(ota_metadata_write)(const UpdateMetadata& in) {
    /* Setor inteiro vai a 0xFF; a página 0 recebe os 256 B do struct. */
    uint8_t page[OTA_FLASH_PAGE_SIZE];
    memcpy(page, &in, sizeof(in));

    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(OTA_METADATA_OFFSET, OTA_METADATA_SIZE);
    flash_range_program(OTA_METADATA_OFFSET, page, OTA_FLASH_PAGE_SIZE);
    restore_interrupts(saved_irq);
    return true;
}

/* Snapshot da config: setor único (4 KiB) no FIM da staging area.
 * Offset fixo: STAGING_OFFSET + (STAGING_SIZE - SECTOR_SIZE) = 0x1FE000.
 *
 * Por que aqui (e não na metadata partition):
 *  - Metadata write é chamado pelo orchestrator durante apply, num path
 *    crítico já validado em HW. Tocar lá em ota_metadata_write traz
 *    risco de regressão no apply.
 *  - Staging tem 1 MiB. Firmware atual ~1.004 KiB. Último setor (4 KiB)
 *    raramente é tocado pelo apply (que copia só `staging[0..raw_size]`).
 *  - O staging_erase_all apaga 1 MiB inteiro DEPOIS deste setor já ter
 *    sido escrito? Não — o erase ocorre ANTES do write (via snapshot
 *    state machine). Veja staging.cpp:staging_session_begin sequência.
 *
 * Pré-condição: caller em flash safe mode + setor já apagado (faz parte
 * do staging_erase_all). Aqui só programamos. */
bool __not_in_flash_func(ota_snapshot_write)(const uint8_t* data, uint16_t len) {
    if (!data || len > OTA_FLASH_SECTOR_SIZE) return false;

    /* Copia para s_applier_buf + padding 0xFF até 4 KiB (granularidade
     * do erase, mesmo que o program seja por páginas de 256 B). */
    memcpy(s_applier_buf, data, len);
    memset(s_applier_buf + len, 0xFF, OTA_FLASH_SECTOR_SIZE - len);

    uint32_t saved_irq = save_and_disable_interrupts();
    /* Setor já foi apagado pelo staging_erase_all. Programamos direto. */
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

bool __not_in_flash_func(ota_metadata_clear)() {
    uint32_t saved_irq = save_and_disable_interrupts();
    flash_range_erase(OTA_METADATA_OFFSET, OTA_METADATA_SIZE);
    restore_interrupts(saved_irq);
    return true;
}

} /* namespace ota */
