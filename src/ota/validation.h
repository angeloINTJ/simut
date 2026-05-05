/**
 * @file    src/ota/validation.h
 * @brief   Pré-validação dry-run do staging (Fase 6 OTA).
 *
 * @details Antes de qualquer ação destrutiva (Fase 7 = apply), verifica:
 *           - magic gzip nos primeiros bytes do staging,
 *           - descompressão completa em dry-run via uzlib (sem escrever
 *             slot da app),
 *           - tamanho descomprimido em range razoável (sketch RP2040),
 *           - CRC32 do descomprimido (informativo — comparado client-side
 *             com o cabeçalho gzip embarcado),
 *           - heurística boot2 RP2040: CRC-32/MPEG-2 dos primeiros 252 B
 *             do binário descomprimido bate com os 4 bytes seguintes
 *             (auto-CRC que a BootROM do RP2040 verifica antes de saltar).
 *
 *          PRECONDIÇÃO: StageSession.status == STAGED (upload já fechou).
 *          NÃO usa LittleFS (lê staging via XIP); seguro de chamar com
 *          LFS desmontada (estado normal pós-stage_session_end final).
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
    NOT_GZIP        = 2,    /**< magic 0x1F 0x8B ausente */
    DECOMPRESS_FAIL = 3,    /**< uzlib retornou erro mid-stream */
    SIZE_TOO_SMALL  = 4,    /**< descomprimido < 100 KiB (sketch demais pequeno) */
    SIZE_TOO_LARGE  = 5,    /**< descomprimido > app slot (1020 KiB) */
    BOOT2_BAD       = 6,    /**< CRC-32/MPEG-2 dos primeiros 256 B inválido */
};

struct ValidationReport {
    ValidationStatus status;
    uint32_t        compressed_size;
    uint32_t        compressed_crc;     /**< Vem do StageSession (CRC32 EDB88320 dos bytes recebidos). */
    uint32_t        decompressed_size;
    uint32_t        decompressed_crc;   /**< CRC32 EDB88320 do output do gunzip. */
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
