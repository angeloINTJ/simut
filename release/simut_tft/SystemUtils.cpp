/**
 * @file SystemUtils.cpp
 * @brief Shared utility functions used across multiple modules.
 * @details Implements CRC8 Dallas/Maxim for 1-Wire ROM validation,
 * CRC32-IEEE-802.3 incremental for streaming export bundles, and
 * history filename format validation (YYYYMMDD.h5).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "SystemDefs.h"
#include <ctype.h>


/* =========================================================================== */
/* CRC8 DALLAS/MAXIM (1-WIRE) */
/* =========================================================================== */
/**
 * Compute CRC8 per Maxim Application Note 27.
 * Polynomial: x^8 + x^5 + x^4 + 1 (0x8C reflected).
 */
uint8_t dallasCrc8(const uint8_t *addr, uint8_t len) {
 uint8_t crc = 0;
 for (uint8_t i = 0; i < len; i++) {
 uint8_t inbyte = addr[i];
 for (uint8_t j = 0; j < 8; j++) {
 uint8_t mix = (crc ^ inbyte) & 0x01;
 crc >>= 1;
 if (mix) crc ^= 0x8C;
 inbyte >>= 1;
 }
 }
 return crc;
}


/* =========================================================================== */
/* CRC32-IEEE-802.3 INCREMENTAL */
/* =========================================================================== */
/* Bitwise (no table) — saves 1 KB of flash. RP2040 at 133 MHz processes
 * ~250 KB in ~120 ms; acceptable for the export path. Reverse polynomial
 * 0xEDB88320, same math as StorageManager::calculateCRC32. Reference
 * vector: crc32("123456789") == 0xCBF43926.
 */
uint32_t crc32_init( ) {
 return 0xFFFFFFFFu;
}

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
 for (size_t i = 0; i < len; i++) {
 crc ^= data[i];
 for (int j = 0; j < 8; j++) {
 if (crc & 1u) crc = (crc >> 1) ^ 0xEDB88320u;
 else crc >>= 1;
 }
 }
 return crc;
}

uint32_t crc32_final(uint32_t crc) {
 return ~crc;
}


/* =========================================================================== */
/* HISTORY FILENAME VALIDATION */
/* =========================================================================== */
/**
 * @brief Validates a history filename.
 * Expected format: "YYYYMMDD.h5" (11 characters). Both older forms went with
 * the codecs that wrote them — the GC uses this to decide what it may delete,
 * so leaving one here would have kept stale files exempt forever.
 */
bool isValidHistoryFileName(const char* name) {
 if (!name) return false;

 size_t nLen = strlen(name);
 if (nLen != 11) return false;
 if (name[8] != '.' || name[9] != 'h' || name[10] != '5') return false;

 /* First 8 characters must be digits (YYYYMMDD) */
 for (int i = 0; i < 8; i++) {
 if (!isdigit((unsigned char)name[i])) return false;
 }

 return true;
}
