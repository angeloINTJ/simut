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

/* The first boot of an update, on trial (trial.h; docs/analysis/OTA_AB_RP2350.md,
 * step 5). The RP2040 has none: its applier writes over the program it runs. */
#if OTA_RP2040_MAP
constexpr bool trial_pending() { return false; }
#else
/* Whether the boot ROM started this image on trial: it waits for the buy, and
 * any reset before that boots the image the update replaced. Asks the ROM. */
bool trial_boot_pending();
/* At the top of setup( ): on a boot on trial, the watchdog guards setup( ) as
 * well, fed from a timer until loop( ) arms its own or the trial's deadline
 * passes. Nothing on any other boot. */
void trial_guard_begin();
/* From loop( ), before it arms its own watchdog: the timer stops feeding. */
void trial_guard_end();
/* In setup( ), once the log is up, with what the boot found: LittleFS mounted.
 * On trial, logs it and makes SIMUT's own reboots come back to this image.
 * Otherwise, an image flagged for trial in the other slot is one the ROM went
 * back from: logs its version, keeps it for /api/status and erases its first
 * sector, so that is said once. */
void trial_boot(StorageManager* storage, bool fsOk);
/* From loop( ), every pass: buys after a healthy minute, goes back at the
 * deadline (trial_step). @p loopOk is what loop( ) sees: the network as the
 * configuration asks for it, and Core 1 not restarted since the last pass. */
void trial_poll(StorageManager* storage, bool loopOk);
/* Whether this image still waits for its buy. No stage until then: it would
 * erase the image the ROM goes back to. */
bool trial_pending();
/* The version of the update the ROM went back from at this boot, or "". */
const char* trial_reverted_version();
#endif

} /* namespace ota */
