/**
 * @file    src/ota/backup.h
 * @brief   API pública do gerador de backup (.bkp) — Fase 1 do OTA.
 *
 * @details A geração faz dois passes sobre a LittleFS para evitar buffer em RAM:
 *          1) scan: calcula payload_size + payload_crc32 + file_count.
 *          2) emit: escreve header (com CRCs) + payload em streaming via Print.
 *
 *          O caller é responsável por garantir que o LittleFS está montado e
 *          que a operação é serializada contra escritas concorrentes (ex.:
 *          via HeavyTaskGuard no WebManager).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <Print.h>

namespace ota {

/**
 * @brief CRC32 incremental (poly 0xEDB88320, init 0xFFFFFFFF, xor-out 0xFFFFFFFF).
 *
 * Compatível com CRC32 do gzip/zlib. Reutilizado em outras fases (validação
 * de staging, metadata).
 *
 * @param crc   Estado anterior (passar OTA_CRC32_INIT no primeiro chunk).
 * @param data  Buffer de bytes.
 * @param len   Tamanho.
 * @return CRC parcial; aplicar `^ 0xFFFFFFFFu` ao terminar para obter o CRC final.
 */
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);

/**
 * @brief Lê o ID único do RP2040 (flash_get_unique_id).
 *
 * Cacheado após a primeira chamada (operação custa ~100us e desabilita IRQs).
 *
 * @param out  Buffer de 8 bytes; preenchido com o ID.
 */
void read_chip_id(uint8_t out[8]);

/**
 * @brief Encoda string de versão "vMAJOR.MINOR.PATCH" em uint32 packed.
 *
 * Formato: (major<<16) | (minor<<8) | patch. Cada campo é truncado para 8 bits.
 * Ex.: "v3.37.8" → 0x00032508.
 *
 * @param version_str  String tipo "v3.37.8" ou "3.37.8" (prefixo 'v' opcional).
 * @return uint32 encoded; 0 se string for inválida.
 */
uint32_t encode_version_u32(const char* version_str);

/**
 * @brief Resultado do scan da LittleFS (pré-requisito para gerar header).
 */
struct BackupScanResult {
    uint32_t payload_size;      /**< Tamanho total do payload em bytes. */
    uint32_t payload_crc32;     /**< CRC32 final do payload (já com xor-out aplicado). */
    uint16_t file_count;        /**< Quantidade de arquivos enumerados. */
};

/**
 * @brief Pass 1: varre a LittleFS, computa tamanho total e CRC32 do payload.
 *
 * NÃO escreve nada; apenas mede. Retorna métricas para construir o header
 * antes do pass 2.
 *
 * @param out  Resultado.
 * @return true se o scan foi bem-sucedido; false em I/O error.
 */
bool backup_scan(BackupScanResult& out);

/**
 * @brief Pass 2: escreve o backup completo (header + payload) em streaming.
 *
 * Requer um `BackupScanResult` válido obtido por `backup_scan` (idealmente
 * imediatamente antes, sob HeavyTaskGuard, para garantir consistência).
 *
 * @param out               Stream de saída (Print&; ex.: WebServer client).
 * @param scan              Resultado do pass 1.
 * @param firmware_version  Versão encoded via encode_version_u32.
 * @param timestamp         Unix epoch UTC; 0 se NTP indisponível.
 * @return true se a escrita completou sem erros de I/O.
 */
bool backup_emit(Print& out,
                 const BackupScanResult& scan,
                 uint32_t firmware_version,
                 uint32_t timestamp);

} /* namespace ota */
