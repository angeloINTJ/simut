/**
 * @file src/ota/validation.h
 * @brief Dry-run pre-validation of staging (OTA).
 *
 * @details Before any destructive action (= apply), checks:
 * - binary size in reasonable range (RP2040 sketch),
 * - RP2040 boot2 heuristic: CRC-32/MPEG-2 of first 252 B
 * matches the following 4 bytes (auto-CRC that the RP2040
 * BootROM checks before jumping).
 *
 * PRECONDITION: StageSession.status == STAGED (upload already closed).
 * Does NOT use LittleFS (reads staging via XIP); safe to call with
 * LFS unmounted (normal state post-stage_session_end).
 *
 * RAW-only: gzip dry-run REMOVED. SIMUT only
 * receives RAW (.bin) firmware — eliminated the compression
 * library. NOT_GZIP and DECOMPRESS_FAIL codes are never
 * set (kept for ABI).
 * decompressed_size/decompressed_crc equal compressed_*.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include "firmware_stage.h"

namespace ota {

enum class ValidationStatus : uint8_t {
 OK = 0,
 STAGE_NOT_READY = 1, /**< s.status != STAGED */
 NOT_GZIP = 2, /**< [reserved, not set in raw-only] */
 DECOMPRESS_FAIL = 3, /**< [reserved, not set in raw-only] */
 SIZE_TOO_SMALL = 4, /**< decompressed < 100 KiB (sketch too small) */
 SIZE_TOO_LARGE = 5, /**< decompressed > app slot (1020 KiB) */
 BOOT2_BAD = 6, /**< CRC-32/MPEG-2 of first 256 B invalid */
};

struct ValidationReport {
 ValidationStatus status;
 uint32_t compressed_size;
 uint32_t compressed_crc; /**< From StageSession (CRC32 EDB88320 of received bytes). */
 uint32_t decompressed_size; /**< == compressed_size in raw-only. */
 uint32_t decompressed_crc; /**< == compressed_crc in raw-only. */
};

/**
 * @brief Validates staging content without destroying it.
 *
 * @param s Closed session (status==STAGED) with bytes_written/crc32_running.
 * @param report Out: state + sizes + CRCs.
 * @return true if report.status == OK.
 */
bool ota_validate_staging(const StageSession& s, ValidationReport& report);

} /* namespace ota */
