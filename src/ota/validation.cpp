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
#include "decompressor.h"
#include "backup.h"      /* crc32_update / OTA_CRC32_INIT */

#include <Arduino.h>
#include <hardware/watchdog.h>
#include <string.h>

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

/* Source callback do gunzip: lê staging via XIP byte-a-byte. */
struct StgSrc { uint32_t offset; uint32_t end; };

static int stg_src_cb(void* user) {
    StgSrc* s = static_cast<StgSrc*>(user);
    if (s->offset >= s->end) return -1;
    uint8_t b;
    staging_read(s->offset, &b, 1);
    s->offset++;
    /* WDT feed a cada 4 KiB consumidos (operação pode levar ~3-5 s para
     * 1 MB descomprimido — sem feed, watchdog reseta). */
    if ((s->offset & 0xFFFu) == 0u) watchdog_update();
    return (int)b;
}

/* Output callback: agrega tamanho + CRC32 e captura os primeiros 256 B
 * pra heurística boot2. NÃO escreve em flash (dry-run puro). */
struct OutCtx {
    uint32_t size;
    uint32_t crc;            /* running CRC32 EDB88320 (xor-out aplicado depois). */
    uint8_t  boot2[256];
    bool     boot2_filled;
};

static bool out_cb(const uint8_t* data, size_t len, void* user) {
    OutCtx* c = static_cast<OutCtx*>(user);

    /* Captura primeiros 256 B (boot2 RP2040). */
    if (c->size < 256u) {
        size_t take_b2 = 256u - c->size;
        if (take_b2 > len) take_b2 = len;
        memcpy(c->boot2 + c->size, data, take_b2);
        if (c->size + take_b2 == 256u) c->boot2_filled = true;
    }

    c->crc = crc32_update(c->crc, data, len);
    c->size += len;
    return true;
}

/* Singleton GunzipContext em BSS (~33 KiB de janela LZ77 + state). Single-
 * thread access garantida por mutex implícito do WebServer Arduino-pico
 * (1 request por vez). Compartilhado com decompressor.cpp seria ideal mas
 * decompressor não expõe instância pública; alocamos a nossa. */
static GunzipContext g_validate_ctx;

bool ota_validate_staging(const StageSession& s, ValidationReport& report) {
    memset(&report, 0, sizeof(report));
    report.compressed_size = s.bytes_written;
    report.compressed_crc  = s.crc32_running;

    if (s.status != StageStatus::STAGED) {
        report.status = ValidationStatus::STAGE_NOT_READY;
        return false;
    }

    /* (1) Detecta formato pelo magic. Gzip (0x1F 0x8B) → caminho de decompress
     * dry-run. Senão → caminho RAW (sem decompress, valida só size + boot2).
     *
     * RAW é o formato usado pelo apply real (Fase 7b): uzlib seria destruída
     * junto com a app slot durante o erase, e marcar uzlib em SRAM via
     * __not_in_flash_func grew o binário em ~16 KiB (ld overflow). Raw é
     * mais simples e seguro: stage_size limitado a OTA_APP_MAX_SIZE
     * (1020 KiB) força sketches que caibam diretamente. */
    uint8_t magic[2] = {0, 0};
    staging_read(0, magic, 2);
    bool is_gzip = (magic[0] == 0x1Fu && magic[1] == 0x8Bu);

    if (!is_gzip) {
        /* Raw path: pular gunzip; size sanity + boot2 CRC. */
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

    /* (2) Decompress dry-run. */
    if (!gunzip_begin(g_validate_ctx)) {
        report.status = ValidationStatus::DECOMPRESS_FAIL;
        return false;
    }

    StgSrc src;
    src.offset = 0;
    src.end    = s.bytes_written;

    OutCtx out;
    memset(&out, 0, sizeof(out));
    out.crc = OTA_CRC32_INIT;

    bool gz_ok = gunzip_decompress(g_validate_ctx, stg_src_cb, &src,
                                   out_cb, &out);

    /* xor-out final do CRC32 zlib. */
    out.crc ^= 0xFFFFFFFFu;

    report.decompressed_size = out.size;
    report.decompressed_crc  = out.crc;

    if (!gz_ok) {
        report.status = ValidationStatus::DECOMPRESS_FAIL;
        return false;
    }

    /* (3) Sanity de tamanho. SIMUT atual cabe em ~1 MiB; sketches
     *     com pelo menos 100 KiB são razoáveis. */
    if (out.size < 100u * 1024u) {
        report.status = ValidationStatus::SIZE_TOO_SMALL;
        return false;
    }
    if (out.size > OTA_APP_MAX_SIZE) {
        report.status = ValidationStatus::SIZE_TOO_LARGE;
        return false;
    }

    /* (4) Heurística boot2. */
    if (!out.boot2_filled) {
        report.status = ValidationStatus::BOOT2_BAD;
        return false;
    }
    uint32_t expected = boot2_crc32(out.boot2, 252);
    uint32_t stored   = (uint32_t)out.boot2[252]
                      | ((uint32_t)out.boot2[253] << 8)
                      | ((uint32_t)out.boot2[254] << 16)
                      | ((uint32_t)out.boot2[255] << 24);
    if (expected != stored) {
        report.status = ValidationStatus::BOOT2_BAD;
        return false;
    }

    report.status = ValidationStatus::OK;
    return true;
}

} /* namespace ota */
