/**
 * @file    src/ota/staging.h
 * @brief   Acesso de baixo nível à área de staging (Fase 4 OTA).
 *
 * @details Lê/escreve/apaga a região da LittleFS em modo bruto (sem
 *          mount). PRECONDIÇÕES:
 *           - LittleFS desmontada antes de escrever/apagar (ler via XIP é OK).
 *           - Core 1 pausado via `multicore_lockout` durante erase/program.
 *           - StorageManager::enterFlashSafeMode() já cobre isso.
 *
 *          API alta-nível: usar `staging_session_begin/end` (manage tudo).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ota_layout.h"

class StorageManager;

namespace ota {

/**
 * @brief Apaga toda a região de staging (1024 KB).
 *
 * Demora ~5-10s. Pausa Core 1 internamente. Atualiza WDT.
 * PRECONDIÇÃO: LittleFS desmontada (caller responsável).
 *
 * @return true em sucesso.
 */
bool staging_erase_all();

/**
 * @brief Apaga UM setor (4 KB) da staging em offset relativo.
 *
 * @param offset_in_staging  Múltiplo de 4096; 0 = primeiro setor.
 */
bool staging_erase_sector(uint32_t offset_in_staging);

/**
 * @brief Programa @p data em @p offset_in_staging.
 *
 * @p len e @p offset_in_staging devem ser múltiplos de OTA_FLASH_PAGE_SIZE (256).
 * Setor deve ter sido apagado ANTES (flash NAND-style: só escreve 1→0).
 *
 * @return true em sucesso.
 */
bool staging_write(uint32_t offset_in_staging, const uint8_t* data, size_t len);

/**
 * @brief Lê bytes da staging via XIP (bypass LittleFS).
 *
 * Sem desabilitar IRQs / Core 1 (XIP read é seguro durante operação normal).
 * NÃO chamar enquanto LittleFS está montada se a região foi escrita por
 * fora (cache do XIP pode estar desatualizado).
 */
void staging_read(uint32_t offset_in_staging, uint8_t* dst, size_t len);

/**
 * @brief Sessão alta-nível: prepara a staging para uso de upload.
 *
 * Sequência:
 *   1. Salva flag "FS in staging mode" (futuro — Fase 5).
 *   2. LittleFS.end() — desmonta.
 *   3. staging_erase_all() — limpa.
 *
 * Ao retornar, a área da LittleFS está toda 0xFF e pronta para receber
 * o .bin.gz via staging_write().
 *
 * @param storage  Ponteiro pro StorageManager (necessário pra desmontar/remount).
 * @return true em sucesso.
 */
bool staging_session_begin(StorageManager* storage);

/* v4.4.0: variante sem erase upfront — caller faz erase on-demand. */
bool staging_session_begin_lite(StorageManager* storage);

/**
 * @brief Encerra a sessão e remonta a LittleFS.
 *
 * Útil pra ABORT de upload (descarta staging e volta ao normal). NÃO
 * formatar — depois de erase_all + LittleFS.begin(), o LFS detecta
 * "filesystem inválido" e formata sozinho.
 *
 * Em caminho de APPLY (Fase 7), staging_session_end NÃO é chamado —
 * o aplicador continua com LittleFS desmontada e reboota.
 */
bool staging_session_end(StorageManager* storage);

/* Whether this board can take an update over the air. The RP2040 always can;
 * the RP2350 only when it booted from a slot of a partition table, and then the
 * staging area above is the other slot (slot_stage.h). */
#if OTA_RP2040_MAP
/* constexpr, not inline: the callers' gates then fold before the compiler lays
 * out their branches, and the RP2040 images stay byte-identical to the ones
 * built before the RP2350 could install (an inline function moved one block in
 * the upload callback, +4 B, measured 2026-10-03). */
constexpr bool staging_install_available() { return true; }
#else
bool staging_install_available();
/* Erases what an upload of @p len bytes will cover, before its body is read
 * (slot_stage_prepare): the upload is then page programs alone. Core 1 must be
 * parked, as for every write here. */
bool staging_prepare(uint32_t len);
/* The RP2350's stage ends here when the image checked out and commit=1 asked
 * for it: the image is kept for the apply, its first sector still in RAM, so
 * a reset before the apply drops it the way a reboot drops a COMMITTED stage
 * on the RP2040 (slot_stage.h). Any new stage drops it too. */
bool staging_mark_ready();
bool staging_ready();
/* The apply's commit: the slot's first sector goes to flash, last, and only
 * now can the slot boot. True once it reads back. Core 1 must be parked. */
bool staging_commit();
/* The physical offset of the slot the stage wrote. */
uint32_t staging_slot_offset();
#endif

} /* namespace ota */
