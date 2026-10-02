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
 *          Por último, a assinatura (docs/analysis/OTA_ASSINADA.md): quem
 *          construiu a imagem, e se ela pode suceder a que está rodando. A
 *          mesma checagem roda de novo no apply (ota_check_staged_signature),
 *          sobre o que o staging tiver naquela hora.
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
#include "signature.h"

namespace ota {

enum class ValidationStatus : uint8_t {
    OK              = 0,
    STAGE_NOT_READY = 1,    /**< s.status != STAGED */
    NOT_GZIP        = 2,    /**< [reservado, não setado em raw-only] */
    DECOMPRESS_FAIL = 3,    /**< [reservado, não setado em raw-only] */
    SIZE_TOO_SMALL  = 4,    /**< descomprimido < 100 KiB (sketch demais pequeno) */
    SIZE_TOO_LARGE  = 5,    /**< imagem > OTA_APP_SAFE_MAX_SIZE (1016 KiB — o teto que não toca o snapshot) */
    BOOT2_BAD       = 6,    /**< CRC-32/MPEG-2 dos primeiros 256 B inválido */
    ENV_MISMATCH    = 7,    /**< a imagem traz uma etiqueta SIMUT-ENV de OUTRA variante (release/alpha/air) */
    /* 8..12: a assinatura. Os números são os de SigVerdict, de propósito
     * (validation.cpp confere com static_assert). */
    SIG_MISSING     = 8,    /**< sem trailer: imagem não assinada */
    SIG_INVALID     = 9,    /**< trailer malformado, ou uma assinatura que não confere */
    SIG_REVOKED     = 10,   /**< chave de assinatura abaixo da menor série aceita */
    SIG_ROLLBACK    = 11,   /**< security_version abaixo do mínimo desta imagem */
    SIG_SCOPE       = 12,   /**< escopo que esta imagem não aceita (chave de bancada numa de produção) */
};

struct ValidationReport {
    ValidationStatus status;
    uint32_t        compressed_size;
    uint32_t        compressed_crc;     /**< Vem do StageSession (CRC32 EDB88320 dos bytes recebidos). */
    uint32_t        decompressed_size;  /**< == compressed_size em raw-only. */
    uint32_t        decompressed_crc;   /**< == compressed_crc em raw-only. */
    /** Variante que a imagem declara (etiqueta SIMUT-ENV), ou "" quando a
     *  imagem não traz etiqueta — build anterior a esta, aceita como antes. */
    char            image_env[12];
};

/**
 * @brief Valida o conteúdo do staging sem destruir.
 *
 * @param s        Sessão fechada (status==STAGED) com bytes_written/crc32_running.
 * @param report   Out: estado + tamanhos + CRCs.
 * @return true se report.status == OK.
 */
bool ota_validate_staging(const StageSession& s, ValidationReport& report);

/**
 * @brief A assinatura da imagem no staging, contra o bloco de confiança desta
 *        imagem (src/ota/ota_trust.h) e a variante em execução.
 *
 * ota_validate_staging( ) a chama no fim do stage. O apply a chama de novo,
 * logo antes de o applier copiar o staging: nada amarra os metadados
 * COMMITTED aos bytes validados — um stage novo não os apaga, e o applier
 * copia o que a área tiver.
 *
 * @param staged_len  bytes recebidos (o trailer está no fim deles).
 * @param report      Out: o veredito e os campos lidos do trailer.
 * @return true se report.verdict == OK.
 */
bool ota_check_staged_signature(uint32_t staged_len, SigReport& report);

} /* namespace ota */
