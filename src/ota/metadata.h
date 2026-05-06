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

namespace ota {

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
 * @brief Apaga setor metadata (todos 0xFF). Equivale a "no pending update".
 */
bool ota_metadata_clear();

} /* namespace ota */
