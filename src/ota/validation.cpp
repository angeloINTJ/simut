/**
 * @file src/ota/validation.cpp
 * @brief Implementation of dry-run pre-validation (OTA).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "validation.h"
#include "staging.h"
#include "ota_layout.h"
#include "backup.h" /* crc32_update / OTA_CRC32_INIT */

#include <Arduino.h>
#include <hardware/watchdog.h>
#include <string.h>

/* gzip dry-run REMOVED. SIMUT only uploads RAW firmware (.bin).
 * Compression was removed.
 * Trade-off: if user uploads .bin.gz by mistake, validation fails at boot2_crc
 * (gzip header does not match RP2040 boot2 layout). Error message v=6. */

namespace ota {

/* CRC-32/MPEG-2 — polynomial 0x04C11DB7, init 0xFFFFFFFF, no reflect, no
 * xor-out. Distinct from zlib CRC32 (poly 0xEDB88320 reflected).
 *
 * Rationale: the RP2040 BootROM verifies boot2 by calculating this
 * exact CRC over the first 252 B of flash and comparing with the 4 B
 * following; if it does not match, BOOT FAILS. Validating this pre-apply
 * catches 99% of "image is not valid RP2040 firmware" cases (random zip,
 * tar, gzip of another file). */
static uint32_t boot2_crc32(const uint8_t* data, size_t len) {
 uint32_t crc = 0xFFFFFFFFu;
 for (size_t i = 0; i < len; i++) {
 crc ^= ((uint32_t)data[i]) << 24;
 for (int b = 0; b < 8; b++) {
 crc = (crc & 0x80000000u) ? ((crc << 1) ^ 0x04C11DB7u) : (crc << 1);
 }
 }
 return crc;
}

bool ota_validate_staging(const StageSession& s, ValidationReport& report) {
 memset(&report, 0, sizeof(report));
 report.compressed_size = s.bytes_written;
 report.compressed_crc = s.crc32_running;

 if (s.status != StageStatus::STAGED) {
 report.status = ValidationStatus::STAGE_NOT_READY;
 return false;
 }

 /* RAW-only path: SIMUT only uploads RAW (.bin) firmware.
 * Validation reduced to: size range + boot2 CRC-32/MPEG-2.
 * decompressed_* equals compressed_* (no real decompression). */
 report.decompressed_size = s.bytes_written;
 report.decompressed_crc = s.crc32_running;

 if (s.bytes_written < 100u * 1024u) {
 report.status = ValidationStatus::SIZE_TOO_SMALL;
 return false;
 }
 if (s.bytes_written > OTA_APP_MAX_SIZE) {
 report.status = ValidationStatus::SIZE_TOO_LARGE;
 return false;
 }

 uint8_t boot2[256];
 staging_read(0, boot2, 256);
 uint32_t expected = boot2_crc32(boot2, 252);
 uint32_t stored = (uint32_t)boot2[252]
 | ((uint32_t)boot2[253] << 8)
 | ((uint32_t)boot2[254] << 16)
 | ((uint32_t)boot2[255] << 24);
 if (expected != stored) {
 report.status = ValidationStatus::BOOT2_BAD;
 return false;
 }
 report.status = ValidationStatus::OK;
 return true;
}

} /* namespace ota */
