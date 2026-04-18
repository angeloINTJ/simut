/**
 * @file    SystemUtils.cpp
 * @brief   Shared utility functions used across multiple modules.
 * @details Implements CRC8 Dallas/Maxim for 1-Wire ROM validation and
 * history filename format validation (YYYYMMDD.csv).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "SystemDefs.h"
#include <ctype.h>


/* =========================================================================== */
/*                        CRC8 DALLAS/MAXIM (1-WIRE)                         */
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
/*                        HISTORY FILENAME VALIDATION                        */
/* =========================================================================== */
/**
 * Validate history filename format: YYYYMMDD.csv (exactly 12 chars).
 * Prevents spurious files from being deleted by enforceStorageLimit().
 */
/**
 * @brief  Valida nome de arquivo de histórico binário.
 * Formato esperado: "YYYYMMDD.bin" (12 caracteres).
 */
bool isValidHistoryFileName(const char* name) {
    if (!name) return false;

    /* Verifica tamanho exato: YYYYMMDD.bin = 12 chars */
    int len = 0;
    const char* p = name;
    while (*p) { len++; p++; }
    if (len != 12) return false;

    /* Primeiros 8 caracteres devem ser dígitos (YYYYMMDD) */
    for (int i = 0; i < 8; i++) {
        if (!isdigit((unsigned char)name[i])) return false;
    }

    /* Extensão deve ser .bin */
    if (name[8] != '.' || name[9] != 'b' || name[10] != 'i' || name[11] != 'n') {
        return false;
    }

    return true;
}
