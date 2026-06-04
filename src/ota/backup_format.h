/**
 * @file src/ota/backup_format.h
 * @brief Binary layout of the SIMUT backup file (.bkp).
 * @details Defines the on-disk format used by the OTA system.
 * Schema-versioned with CRC32 protection at both header and payload
 * levels. Tied to the RP2040 chip_id (prevents cross-device restore).
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

/* Magic = "BKP1" in ASCII little-endian. */
#define OTA_BACKUP_MAGIC 0x31504B42u
#define OTA_BACKUP_SCHEMA 1u

/* CRC32 polynomial (identical to gzip/zlib). */
#define OTA_CRC32_POLY 0xEDB88320u
#define OTA_CRC32_INIT 0xFFFFFFFFu

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fixed backup file header. Always 40 bytes.
 *
 * All multi-byte fields are little-endian (RP2040 native).
 */
struct __attribute__((packed)) BackupHeader {
 uint32_t magic; /**< OTA_BACKUP_MAGIC. */
 uint16_t schema_version; /**< Current OTA_BACKUP_SCHEMA. */
 uint16_t reserved0; /**< Padding/alignment. Always 0. */
 uint8_t chip_id[8]; /**< RP2040 unique ID (flash_get_unique_id). */
 uint32_t firmware_version; /**< Encoded (major<<16)|(minor<<8)|patch. */
 uint32_t timestamp; /**< Unix epoch UTC; 0 if NTP not ready. */
 uint32_t payload_size; /**< Payload bytes after this header. */
 uint32_t payload_crc32; /**< CRC32 (poly EDB88320, init 0xFFFFFFFF, xor-out 0xFFFFFFFF) of payload. */
 uint32_t reserved1; /**< Reserved for future extension. Always 0 — included in header_crc32. */
 uint32_t header_crc32; /**< CRC32 of the preceding 36 bytes of this struct. */
};

/* Compile-time check: guarantees stable layout regardless of compiler/arch. */
#ifdef __cplusplus
static_assert(sizeof(BackupHeader) == 40, "BackupHeader must be exactly 40 bytes");
#else
_Static_assert(sizeof(struct BackupHeader) == 40, "BackupHeader must be exactly 40 bytes");
#endif

/**
 * @brief Header of a TLV entry in the payload.
 *
 * Followed by: char path[path_length] (no null terminator) + uint8_t content[content_length].
 * Sequence repeated until totaling payload_size.
 */
struct __attribute__((packed)) BackupEntry {
 uint16_t path_length; /**< Path bytes (no terminator). */
 uint32_t content_length; /**< File content bytes. */
};

#ifdef __cplusplus
static_assert(sizeof(BackupEntry) == 6, "BackupEntry header must be exactly 6 bytes");
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
