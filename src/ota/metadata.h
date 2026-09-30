/**
 * @file    src/ota/metadata.h
 * @brief   Metadata persistente do update OTA (Fase 7+).
 *
 * @details Setor de 4 KiB em OTA_METADATA_OFFSET — apenas a primeira página
 *          (256 B) recebe gravação. Survives reboots por watchdog/power.
 *
 *          Estados (UpdateState):
 *           NONE       — sem update pendente (magic ausente).
 *           COMMITTED  — upload ok, validate ok; pronto pra apply.
 *           APPLYING   — orchestrator entrou no path destrutivo. Se boot
 *                        ler isto, é primeiro boot pós-update (Fase 8).
 *           POST_BOOT  — primeiro boot pós-update detectado, LFS reformat
 *                        em progresso ou aguardando restore.
 *           COMPLETED  — restore completo, ciclo fechado. Próximo boot
 *                        normal.
 *
 *          Apply é destrutivo. Se cair energia entre APPLYING e o reboot
 *          do firmware novo, attempts++ no próximo boot e o user vai pra
 *          BOOTSEL recovery (ver docs/RECOVERY.md).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include "ota_layout.h"

namespace ota {

/**
 * Scratch buffer SRAM de 8 KiB compartilhado pelas operações de flash
 * (apply sector copy + snapshot de config de 2 setores). Definido em
 * `applier.cpp` (BSS estática, 0-init no boot). v21: cresceu de 4 KiB
 * porque SystemConfig + AlarmTelConfig não cabe mais num setor.
 *
 * Usuários:
 *  - `applier.cpp::ota_applier_run` (durante apply, IRQ off — uso exclusivo).
 *  - `metadata.cpp::ota_metadata_write` (preserva snapshot region).
 *  - `metadata.cpp::ota_snapshot_write` (preserva metadata page 0).
 *  - `config_snapshot.cpp::ota_snapshot_capture` (monta payload pré-write).
 *
 * Race-free porque caller 1 (apply) só roda APÓS `state=APPLYING` persistido,
 * e callers 2/3/4 nunca rodam concorrentemente com apply (apply só termina
 * via reboot).
 */
extern uint8_t s_applier_buf[2u * OTA_FLASH_SECTOR_SIZE];

constexpr uint32_t OTA_MAGIC_PENDING = 0xA5C3F00Du;

enum UpdateState : uint32_t {
    STATE_NONE      = 0,
    STATE_COMMITTED = 1,
    STATE_APPLYING  = 2,
    STATE_POST_BOOT = 3,
    STATE_COMPLETED = 4,
};

/* Anti-loop: se attempts >= MAX_ATTEMPTS num boot, recusa apply (deixa
 * BOOTSEL ser o caminho de recovery). */
constexpr uint32_t OTA_MAX_APPLY_ATTEMPTS = 3;

struct __attribute__((packed)) UpdateMetadata {
    uint32_t magic;                /**< OTA_MAGIC_PENDING quando válido. */
    uint32_t state;                /**< UpdateState. */
    uint32_t compressed_size;      /**< Bytes em staging (.bin.gz). */
    uint32_t uncompressed_size;    /**< Bytes esperados após gunzip (.bin). */
    uint32_t compressed_crc32;     /**< CRC32 do staging — bate com client. */
    uint32_t uncompressed_crc32;   /**< CRC32 do output do gunzip dry-run. */
    uint32_t attempts;             /**< Tentativas de apply (anti-loop). */
    uint32_t reserved[57];         /**< Pad até 256 B (preencher 0xFFFFFFFF). */
};
static_assert(sizeof(UpdateMetadata) == 256, "UpdateMetadata != 256 B");

/**
 * @brief Lê metadata via XIP (sem desabilitar IRQs / unmount).
 *
 * @return true se leu; false se magic mismatch (out preenchido com zeros).
 */
bool ota_metadata_read(UpdateMetadata& out);

/**
 * @brief Apaga setor metadata + grava 256 B na primeira página.
 *
 * **PRE-CONDIÇÃO OBRIGATÓRIA**: caller deve garantir Core 1 pausado
 * (`StorageManager::enterFlashSafeMode()`) ANTES de chamar — função
 * faz `flash_range_erase` interno e Core 1 lendo flash via XIP durante
 * o erase causa hard fault (autópsia classifica como reset "external"
 * sem flag de watchdog, e a metadata fica sem persistir).
 *
 * Exceção: durante apply pós-IRQ-disable + Core 1 lockout do orchestrator,
 * já está safe; pode chamar direto.
 *
 * @return true em sucesso.
 */
bool ota_metadata_write(const UpdateMetadata& in);

/**
 * @brief Marca metadata.state e persiste. Atalho.
 */
bool ota_metadata_set_state(UpdateState st);

/**
 * @brief Apaga o setor de metadata (todos 0xFF). Equivale a "no pending update".
 *
 * Só o setor de 0x1FF000. O snapshot da configuração tem setores próprios
 * (`OTA_SNAPSHOT_OFFSET`) desde a v21 e NÃO é apagado aqui — este comentário
 * dizia que era, e o boot confiou nisso até 2026-09-30: veja
 * `ota_snapshot_clear( )`.
 */
bool ota_metadata_clear();

/**
 * @brief Programa o snapshot nos seus dois setores (`OTA_SNAPSHOT_OFFSET`).
 *
 * Copia @p data para `s_applier_buf`, completa com 0xFF até 8 KiB e programa
 * os dois setores, que o caller já apagou (`ensure_sector_erased` no stage).
 *
 * **PRE-CONDIÇÃO**: caller deve garantir Core 1 pausado
 * (`StorageManager::enterFlashSafeMode()`). Não é a função quem decide.
 *
 * @param data  Buffer com snapshot serializado (header + payload + CRC).
 * @param len   Tamanho em bytes; <= 8192.
 * @return true se gravado.
 */
bool ota_snapshot_write(const uint8_t* data, uint16_t len);

/**
 * @brief Apaga os dois setores do snapshot da configuração.
 *
 * O snapshot é de uso único: o boot seguinte ao stage o restaura (se faltar
 * `system.bin`) e o apaga logo depois, e `system format` o apaga antes de
 * formatar. Os setores são os blocos 254..255 do LittleFS, então só chame com
 * `ota_snapshot_present( )` verdadeiro — um snapshot válido ali prova que o
 * LittleFS não guarda nada nesses blocos.
 *
 * **PRE-CONDIÇÃO**: Core 1 pausado (`StorageManager::enterFlashSafeMode()`).
 */
bool ota_snapshot_clear();

} /* namespace ota */
