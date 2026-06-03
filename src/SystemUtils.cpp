/**
 * @file SystemUtils.cpp
 * @brief Shared utility functions used across multiple modules.
 * @details Implements CRC8 Dallas/Maxim for 1-Wire ROM validation,
 * CRC32-IEEE-802.3 incremental for streaming export bundles, and
 * history filename format validation (YYYYMMDD.bin).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
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
/* CRC32-IEEE-802.3 INCREMENTAL (F-CSV-EXPORT) */
/* =========================================================================== */
/* Bitwise (sem tabela) — economiza 1 KB de flash. RP2040 a 133 MHz processa
 * ~250 KB em ~120 ms; aceitável para o caminho de export. Polinômio reverso
 * 0xEDB88320, mesma matemática do StorageManager::calculateCRC32. Vetor de
 * referência: crc32("123456789") == 0xCBF43926.
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
 * @brief Valida nome de arquivo de histórico binário.
 * Formato esperado: "YYYYMMDD.bin" (12 caracteres).
 */
bool isValidHistoryFileName(const char* name) {
 if (!name) return false;

 /* Fast-path: length exactly 12 and extension .bin */
 if (strlen(name) != 12) return false;
 if (name[8] != '.' || name[9] != 'b' || name[10] != 'i' || name[11] != 'n') {
 return false;
 }

 /* Primeiros 8 caracteres devem ser dígitos (YYYYMMDD) */
 for (int i = 0; i < 8; i++) {
 if (!isdigit((unsigned char)name[i])) return false;
 }

 return true;
}
