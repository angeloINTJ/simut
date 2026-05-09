/**
 * @file    src/ota/validation.h
 * @brief   Pré-validação dry-run do staging (Fase 6 OTA).
 *
 * @details Antes de qualquer ação destrutiva (Fase 7 = apply), verifica:
 *           - tamanho do binário em range razoável (sketch RP2040),
 *           - heurística boot2 RP2040: CRC-32/MPEG-2 dos primeiros 252 B
 *             bate com os 4 bytes seguintes (auto-CRC que a BootROM do
 *             RP2040 verifica antes de saltar).
 *
 *          PRECONDIÇÃO: StageSession.status == STAGED (upload já fechou).
 *          NÃO usa LittleFS (lê staging via XIP); seguro de chamar com
 *          LFS desmontada (estado normal pós-stage_session_end final).
 *
 *          F-OTA-RAM (alpha2/alpha3): gzip dry-run REMOVIDO. SIMUT só
 *          recebe firmware RAW (.bin) desde v3.43.3 — eliminado o uzlib
 *          + 33 KiB de BSS (g_validate_ctx). Códigos NOT_GZIP e
 *          DECOMPRESS_FAIL nunca são setados (mantidos pra ABI).
 *          decompressed_size/decompressed_crc são iguais a compressed_*.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include "firmware_stage.h"

namespace ota {

enum class ValidationStatus : uint8_t {
    OK              = 0,
    STAGE_NOT_READY = 1,    /**< s.status != STAGED */
    NOT_GZIP        = 2,    /**< [reservado, não setado em raw-only] */
    DECOMPRESS_FAIL = 3,    /**< [reservado, não setado em raw-only] */
    SIZE_TOO_SMALL  = 4,    /**< descomprimido < 100 KiB (sketch demais pequeno) */
    SIZE_TOO_LARGE  = 5,    /**< descomprimido > app slot (1020 KiB) */
    BOOT2_BAD       = 6,    /**< CRC-32/MPEG-2 dos primeiros 256 B inválido */
};

struct ValidationReport {
    ValidationStatus status;
    uint32_t        compressed_size;
    uint32_t        compressed_crc;     /**< Vem do StageSession (CRC32 EDB88320 dos bytes recebidos). */
    uint32_t        decompressed_size;  /**< == compressed_size em raw-only. */
    uint32_t        decompressed_crc;   /**< == compressed_crc em raw-only. */
};

/**
 * @brief Valida o conteúdo do staging sem destruir.
 *
 * @param s        Sessão fechada (status==STAGED) com bytes_written/crc32_running.
 * @param report   Out: estado + tamanhos + CRCs.
 * @return true se report.status == OK.
 */
bool ota_validate_staging(const StageSession& s, ValidationReport& report);

} /* namespace ota */
