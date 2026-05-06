/**
 * @file    src/ota/config_snapshot.h
 * @brief   Snapshot da configuração crítica preservado durante OTA (Fase 9).
 *
 * @details O apply destrutivo da Fase 7b reformata a partição LittleFS
 *          (compartilhada com a área de staging do firmware). Para que o
 *          device suba pós-update preservando WiFi/users/sensores, este
 *          módulo serializa `/config/system.bin` em pages 1..15 do setor
 *          de metadata OTA (`OTA_METADATA_OFFSET`, 4 KiB), que sobrevive
 *          ao apply.
 *
 *          Layout do setor pós-snapshot:
 *            page 0       (256 B)  → UpdateMetadata (já existente)
 *            pages 1..15  (3840 B) → ConfigSnapshotHeader + payload + CRC32
 *
 *          Demais arquivos (`/calib.csv`, `/history/*`, `/web/*`, `/lang/*`)
 *          NÃO entram no snapshot: o user mantém o `.bkp` baixado pelo
 *          navegador antes do apply (fallback A) e restaura manualmente
 *          via /files após o reboot.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

class StorageManager;

namespace ota {

constexpr uint32_t CONFIG_SNAPSHOT_MAGIC   = 0x434D4953u; /* 'SIMC' little-endian */
constexpr uint16_t CONFIG_SNAPSHOT_VERSION = 1u;

/* Tamanho útil em pages 1..15 da metadata partition (4096 - 256). */
constexpr uint16_t CONFIG_SNAPSHOT_REGION_SIZE = 3840u;

/* Header (16 B) + CRC32 trailer (4 B) = 20 B overhead. */
constexpr uint16_t CONFIG_SNAPSHOT_PAYLOAD_MAX = CONFIG_SNAPSHOT_REGION_SIZE - 20u;

struct __attribute__((packed)) ConfigSnapshotHeader {
    uint32_t magic;            /**< CONFIG_SNAPSHOT_MAGIC */
    uint16_t schema_version;   /**< CONFIG_SNAPSHOT_VERSION */
    uint16_t reserved0;
    uint32_t payload_size;     /**< Bytes do payload (system.bin raw). */
    uint32_t reserved1;
};
static_assert(sizeof(ConfigSnapshotHeader) == 16, "ConfigSnapshotHeader != 16 B");

/**
 * @brief Serializa `/config/system.bin` no setor de metadata.
 *
 * Pré-condição: chamada com Core 1 já em flash safe mode (caller responsável
 * — `staging_session_begin` faz isso antes de chamar).
 *
 * Lê o arquivo via LittleFS (que precisa estar montada), monta header +
 * payload + CRC32 em scratch (`s_applier_buf`), preserva a page 0 atual
 * e regrava o setor inteiro.
 *
 * @return true se snapshot foi gravado; false em caso de erro (arquivo
 *         ausente, tamanho > MAX, ou falha de flash).
 */
bool ota_snapshot_capture();

/**
 * @brief Verifica se há snapshot válido (magic + CRC) na metadata partition.
 *
 * Read-only — usa XIP, sem desabilitar IRQs nem mexer em flash.
 */
bool ota_snapshot_present();

/**
 * @brief Restaura `system.bin` do snapshot para o LittleFS recém-formatado.
 *
 * Pré-condição: LittleFS montada, diretório `/config` existe (ambos
 * garantidos por `StorageManager::begin()` antes da chamada).
 *
 * Em sucesso, NÃO limpa o snapshot — quem chama (AppManager_Boot via
 * `ota_metadata_clear()`) é responsável por isso. Isto permite que o
 * boot detecte estado consistente: se `ota_metadata_clear` falhar antes
 * do reboot final, próximo boot retenta o restore (idempotente).
 *
 * @return true se sistema.bin foi escrito (file path / CRC OK); false caso
 *         contrário. Caller decide se prossegue com factory defaults.
 */
bool ota_snapshot_restore_to_lfs();

} /* namespace ota */
