/**
 * @file src/ota/backup_format.h
 * @brief Binary layout of the SIMUT backup file (.bkp).
 * @details Defines the on-disk format used by Phases 1-2 of the OTA system.
 * Schema-versioned with CRC32 protection at both header and payload
 * levels. Atrelado ao chip_id do RP2040 (impede restauração cruzada).
 *
 * Conforme IMPLEMENTATION_PLAN.md .
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* Magic = "BKP1" em ASCII little-endian. */
#define OTA_BACKUP_MAGIC 0x31504B42u
#define OTA_BACKUP_SCHEMA 1u

/* CRC32 polinômio (idêntico ao gzip/zlib). */
#define OTA_CRC32_POLY 0xEDB88320u
#define OTA_CRC32_INIT 0xFFFFFFFFu

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cabeçalho fixo do arquivo de backup. Sempre 40 bytes.
 *
 * Todos os campos multi-byte são little-endian (nativo do RP2040).
 */
struct __attribute__((packed)) BackupHeader {
 uint32_t magic; /**< OTA_BACKUP_MAGIC. */
 uint16_t schema_version; /**< OTA_BACKUP_SCHEMA atual. */
 uint16_t reserved0; /**< Padding/alinhamento. Sempre 0. */
 uint8_t chip_id[8]; /**< RP2040 unique ID (flash_get_unique_id). */
 uint32_t firmware_version; /**< Encoded (major<<16)|(minor<<8)|patch. */
 uint32_t timestamp; /**< Unix epoch UTC; 0 se NTP não estiver pronto. */
 uint32_t payload_size; /**< Bytes de payload após este header. */
 uint32_t payload_crc32; /**< CRC32 (poly EDB88320, init 0xFFFFFFFF, xor-out 0xFFFFFFFF) do payload. */
 uint32_t reserved1; /**< Reserva para extensão futura. Sempre 0 — incluído no header_crc32. */
 uint32_t header_crc32; /**< CRC32 dos 36 bytes anteriores deste struct. */
};

/* Compile-time check: garante layout estável independente do compilador/arch. */
#ifdef __cplusplus
static_assert(sizeof(BackupHeader) == 40, "BackupHeader must be exactly 40 bytes");
#else
_Static_assert(sizeof(struct BackupHeader) == 40, "BackupHeader must be exactly 40 bytes");
#endif

/**
 * @brief Cabeçalho de uma entrada TLV no payload.
 *
 * Seguido por: char path[path_length] (sem nul-terminator) + uint8_t content[content_length].
 * Sequência repetida até totalizar payload_size.
 */
struct __attribute__((packed)) BackupEntry {
 uint16_t path_length; /**< Bytes do path (sem terminador). */
 uint32_t content_length; /**< Bytes do conteúdo do arquivo. */
};

#ifdef __cplusplus
static_assert(sizeof(BackupEntry) == 6, "BackupEntry header must be exactly 6 bytes");
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
