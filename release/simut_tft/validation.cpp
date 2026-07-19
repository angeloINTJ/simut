/**
 * @file    src/ota/validation.cpp
 * @brief   Implementação da pré-validação dry-run (Fase 6 OTA).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "ota/validation.h"
#include "ota/staging.h"
#include "ota/ota_layout.h"
#include "ota/backup.h"      /* crc32_update / OTA_CRC32_INIT */

#include <Arduino.h>
#include <hardware/watchdog.h>
#include <string.h>

/* F-OTA-RAM (v3.44.0-alpha2/alpha3, source removed v3.45.1): gzip dry-run
 * REMOVIDO. SIMUT só sobe firmware RAW (.bin) desde v3.43.3. Em v3.45.1
 * o source de uzlib (lib/uzlib/) e o wrapper decompressor.{h,cpp} foram
 * deletados (eram dead-stripped mas dívida no source tree).
 * Trade-off: se user upar .bin.gz por engano, validation falha em boot2_crc
 * (gzip header não bate com layout RP2040 boot2). Mensagem de erro v=6. */

namespace ota {

/* CRC-32/MPEG-2 — polinômio 0x04C11DB7, init 0xFFFFFFFF, sem reflect, sem
 * xor-out. Distinto do CRC32 zlib (poly 0xEDB88320 reflected).
 *
 * Razão da escolha: a BootROM do RP2040 verifica boot2 calculando este
 * exato CRC sobre os primeiros 252 B do flash e comparando com os 4 B
 * seguintes; se não bater, BOOT FALHA. Validar isto pré-apply pega 99 %
 * dos casos de "imagem não é firmware RP2040 válido" (zip aleatório,
 * tar, gzip de outro arquivo). */
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
    report.compressed_crc  = s.crc32_running;

    if (s.status != StageStatus::STAGED) {
        report.status = ValidationStatus::STAGE_NOT_READY;
        return false;
    }

    /* RAW-only path (F-OTA-RAM): SIMUT só upa RAW (.bin) desde v3.43.3.
     * Validação reduzida a: tamanho range + boot2 CRC-32/MPEG-2.
     * decompressed_* fica == compressed_* (sem decompressão real). */
    report.decompressed_size = s.bytes_written;
    report.decompressed_crc  = s.crc32_running;

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
    uint32_t stored   = (uint32_t)boot2[252]
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
