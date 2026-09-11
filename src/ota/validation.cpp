/**
 * @file    src/ota/validation.cpp
 * @brief   Implementação da pré-validação dry-run (Fase 6 OTA).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "validation.h"
#include "staging.h"
#include "ota_layout.h"
#include "backup.h"      /* crc32_update / OTA_CRC32_INIT */
#include "../BuildIdentity.h"  /* SIMUT_ENV_TAG, simut_env_tag_scan */

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
     * Sem decompressão real, então decompressed_crc == compressed_crc.
     *
     * Os dois tamanhos NÃO são iguais, e a diferença importa. bytes_written
     * inclui o padding 0xFF que stage_session_end acrescenta pra fechar a
     * última página de 256 B — é quanto o applier precisa copiar. O CRC, por
     * outro lado, cobre só os bytes que chegaram. Reportar bytes_written nos
     * dois campos fazia qualquer verificação sobre o par (tamanho, CRC)
     * falhar mesmo numa cópia perfeita: o CRC de 957.460 bytes comparado
     * contra o CRC de 957.696. */
    report.decompressed_size = s.bytes_received;
    report.decompressed_crc  = s.crc32_running;

    if (s.bytes_written < 100u * 1024u) {
        report.status = ValidationStatus::SIZE_TOO_SMALL;
        return false;
    }
    if (s.bytes_written > OTA_APP_SAFE_MAX_SIZE) {
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

    /* Variant check. Size and boot2 CRC prove the file is *a* Pico image;
     * nothing proved it was an image for THIS hardware, and staging the
     * wrong variant formats the file system on the way in. The staged
     * image is scanned for the SIMUT-ENV tag (BuildIdentity.cpp) in 4 KiB
     * windows with an overlap of one tag length, so a tag straddling two
     * windows is still found. ~1 MiB of XIP-speed reads, once per stage.
     * No tag = an image older than the tag: accepted, reported as "". */
    {
        static uint8_t win[4096 + SIMUT_ENV_TAG_MAX];
        const uint32_t total = s.bytes_written;
        uint32_t off = 0;
        bool found = false;
        while (off < total && !found) {
            uint32_t n = total - off;
            if (n > sizeof(win)) n = sizeof(win);
            staging_read(off, win, n);
            found = simut_env_tag_scan(win, n, report.image_env, sizeof(report.image_env));
            if (n < sizeof(win)) break;
            off += 4096;
            watchdog_update();
        }
        /* A variante EM EXECUÇÃO sai da etiqueta desta própria imagem, não do
         * macro. Duas razões, e as duas importam: é a leitura que impede o
         * linker de descartar a string (sem ela o .bin saía sem etiqueta), e
         * comparar etiqueta contra etiqueta é o que garante que o formato que
         * gravamos é o mesmo que sabemos ler. Se a nossa própria etiqueta não
         * for legível, cai no macro — recusar toda atualização por causa de
         * uma conferência interna seria pior que o problema. */
        char mine[sizeof(report.image_env)];
        const bool mineOk = simut_env_tag_scan(
            reinterpret_cast<const unsigned char*>(SIMUT_ENV_TAG),
            (unsigned)strlen(SIMUT_ENV_TAG), mine, sizeof(mine));
        const char* running = mineOk ? mine : simut_env_name();
        if (found && strcmp(report.image_env, running) != 0) {
            report.status = ValidationStatus::ENV_MISMATCH;
            return false;
        }
    }
    report.status = ValidationStatus::OK;
    return true;
}

} /* namespace ota */
