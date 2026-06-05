/**
 * @file src/ota/backup.h
 * @brief Public API of the backup (.bkp) generator — OTA.
 *
 * @details Generation does two passes over LittleFS to avoid buffering in RAM:
 * 1) scan: calculates payload_size + payload_crc32 + file_count.
 * 2) emit: writes header (with CRCs) + payload streaming via Print.
 *
 * The caller is responsible for ensuring LittleFS is mounted and
 * that the operation is serialized against concurrent writes (e.g.
 * via HeavyTaskGuard in WebManager).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <Print.h>
#include <Stream.h>
#include "backup_format.h"

namespace ota {

/**
 * @brief Incremental CRC32 (poly 0xEDB88320, init 0xFFFFFFFF, xor-out 0xFFFFFFFF).
 *
 * Compatible with gzip/zlib CRC32. Reused in other steps (staging
 * validation, metadata).
 *
 * @param crc Previous state (pass OTA_CRC32_INIT on first chunk).
 * @param data Byte buffer.
 * @param len Length.
 * @return Partial CRC; apply `^ 0xFFFFFFFFu` when done to get the final CRC.
 */
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);

/**
 * @brief Reads the unique RP2040 ID (flash_get_unique_id).
 *
 * Cached after first call (operation costs ~100us and disables IRQs).
 *
 * @param out 8-byte buffer; filled with the ID.
 */
void read_chip_id(uint8_t out[8]);

/**
 * @brief Encodes version string "vMAJOR.MINOR.PATCH" into packed uint32.
 *
 * Format: (major<<16) | (minor<<8) | patch. Each field truncated to 8 bits.
 * E.g.: "v3.37.8" → 0x00032508.
 *
 * @param version_str String like "v3.37.8" or "3.37.8" (prefix 'v' optional).
 * @return uint32 encoded; 0 if string is invalid.
 */
uint32_t encode_version_u32(const char* version_str);

/**
 * @brief Result of the LittleFS scan (prerequisite to generate header).
 */
struct BackupScanResult {
 uint32_t payload_size; /**< Total payload size in bytes. */
 uint32_t payload_crc32; /**< Final payload CRC32 (already with xor-out applied). */
 uint16_t file_count; /**< Number of files enumerated. */
};

/**
 * @brief Backup validation status codes.
 *
 * Ordered so lower values correspond to failures detected
 * earlier in parsing — useful for hierarchical error messages.
 */
enum class BackupStatus : uint8_t {
 OK = 0,
 BAD_MAGIC = 1,
 UNSUPPORTED_SCHEMA = 2,
 HEADER_CRC_MISMATCH = 3,
 PAYLOAD_TRUNCATED = 4,
 PAYLOAD_CRC_MISMATCH = 5,
 CHIP_ID_MISMATCH = 6,
 PATH_INVALID = 7,
 PATH_TOO_LONG = 8,
 IO_ERROR = 9,
 INTERNAL_ERROR = 10,
};

/* Validation of .bkp in production is done by the state machine in
 * ota::RestoreSession (VALIDATE mode) — see src/ota/restore.h. The
 * Stream-based backup_validate was removed to save flash; the web upload
 * pipeline already consumes chunks and maintains state, so the state
 * machine covers both scenarios. */

/**
 * @brief Pass 1: scans LittleFS, computes total size and payload CRC32.
 *
 * Does NOT write anything; only measures. Returns metrics to build the header
 * before pass 2.
 *
 * @param out Result.
 * @return true if scan was successful; false on I/O error.
 */
bool backup_scan(BackupScanResult& out);

/**
 * @brief Pass 2: writes the full backup (header + payload) in streaming.
 *
 * Requires a valid BackupScanResult obtained by backup_scan (ideally
 * immediately before, under HeavyTaskGuard, to ensure consistency).
 *
 * @param out Output stream (Print&; e.g. WebServer client).
 * @param scan Result of pass 1.
 * @param firmware_version Encoded version via encode_version_u32.
 * @param timestamp Unix epoch UTC; 0 if NTP unavailable.
 * @return true if write completed without I/O errors.
 */
bool backup_emit(Print& out,
 const BackupScanResult& scan,
 uint32_t firmware_version,
 uint32_t timestamp);

} /* namespace ota */
