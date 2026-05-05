/**
 * @file    src/ota/backup.cpp
 * @brief   Implementação do gerador de backup .bkp (Fase 1 OTA).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "backup.h"
#include "backup_format.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Print.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <pico/unique_id.h>
#include <string.h>

namespace ota {

/* ---------------------------------------------------------------------------
 * CRC32 (poly 0xEDB88320, init 0xFFFFFFFF, xor-out 0xFFFFFFFF) — gzip/zlib
 * ---------------------------------------------------------------------------
 *
 * Tabela de 256 entradas em flash (1 KB const). Tradeoff: ~1 KB de flash em
 * troca de ~8x mais throughput que o cálculo bit-a-bit. Crítico aqui porque
 * o backup processa todos os arquivos da LittleFS sob o WDT da Web.
 */
static const uint32_t CRC32_TABLE[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
    0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u, 0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u,
    0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu, 0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
    0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
    0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u, 0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
    0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
    0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
    0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u, 0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
    0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
    0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
    0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu, 0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
    0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
    0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u, 0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
    0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u, 0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u,
    0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
    0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au, 0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
    0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u, 0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
    0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
    0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu, 0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
    0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu, 0x4669BE79u,
    0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u, 0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu,
    0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
    0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
    0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u, 0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u,
    0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
    0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu, 0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
    0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u, 0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu,
    0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu,
    0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u, 0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du,
};

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    while (len--) {
        crc = CRC32_TABLE[(crc ^ *data++) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

/* ---------------------------------------------------------------------------
 * Chip ID
 * ------------------------------------------------------------------------- */

static bool   s_chip_id_cached = false;
static uint8_t s_chip_id[8];

void read_chip_id(uint8_t out[8]) {
    if (!s_chip_id_cached) {
        pico_unique_board_id_t uid;
        pico_get_unique_board_id(&uid);
        memcpy(s_chip_id, uid.id, 8);
        s_chip_id_cached = true;
    }
    memcpy(out, s_chip_id, 8);
}

/* ---------------------------------------------------------------------------
 * Version parser
 * ------------------------------------------------------------------------- */

uint32_t encode_version_u32(const char* s) {
    if (!s) return 0;
    if (*s == 'v' || *s == 'V') s++;
    uint32_t v[3] = {0, 0, 0};
    int idx = 0;
    while (*s && idx < 3) {
        char c = *s++;
        if (c >= '0' && c <= '9') {
            v[idx] = v[idx] * 10u + (uint32_t)(c - '0');
            if (v[idx] > 255u) v[idx] = 255u;  /* satura em 8 bits */
        } else if (c == '.') {
            idx++;
        } else {
            break;  /* qualquer coisa não-numérica/não-ponto encerra (ex.: "-rc1") */
        }
    }
    return ((v[0] & 0xFFu) << 16) | ((v[1] & 0xFFu) << 8) | (v[2] & 0xFFu);
}

/* ---------------------------------------------------------------------------
 * LittleFS walk (DFS recursivo, profundidade limitada)
 * ------------------------------------------------------------------------- */

static constexpr int MAX_WALK_DEPTH = 8;
static constexpr size_t IO_CHUNK = 512;

/* Visitor callback. Retornar false aborta o walk (usado p/ propagar erro de I/O). */
typedef bool (*VisitFn)(const char* full_path, File& f, void* ctx);

static bool walk_dir(const String& dir_path, VisitFn visit, void* ctx, int depth) {
    if (depth > MAX_WALK_DEPTH) return false;
    Dir d = LittleFS.openDir(dir_path);
    while (d.next()) {
        watchdog_update();  /* operação longa — alimenta WDT */
        String name = d.fileName();
        if (name.length() == 0) continue;
        if (name == "." || name == "..") continue;

        String full = dir_path;
        if (!full.endsWith("/")) full += "/";
        full += name;

        if (d.isDirectory()) {
            if (!walk_dir(full, visit, ctx, depth + 1)) return false;
        } else {
            File f = LittleFS.open(full, "r");
            if (!f) return false;
            bool ok = visit(full.c_str(), f, ctx);
            f.close();
            if (!ok) return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Pass 1: scan
 * ------------------------------------------------------------------------- */

struct ScanCtx {
    uint32_t payload_size;
    uint32_t crc;
    uint16_t file_count;
    bool     overflow;  /* path > 65535 bytes */
};

static bool scan_visitor(const char* full_path, File& f, void* vctx) {
    ScanCtx* ctx = static_cast<ScanCtx*>(vctx);
    size_t path_len = strlen(full_path);
    if (path_len > 0xFFFFu) {
        ctx->overflow = true;
        return false;
    }
    size_t content_len = f.size();

    /* Atualiza CRC e tamanho do payload exatamente como o emit irá escrever:
     *   [BackupEntry packed (6 B)] [path bytes] [content bytes]
     *
     * BackupEntry é serializado em little-endian via 6 bytes literais. */
    uint8_t entry_bytes[6];
    uint16_t pl16 = (uint16_t)path_len;
    uint32_t cl32 = (uint32_t)content_len;
    entry_bytes[0] = (uint8_t)(pl16 & 0xFFu);
    entry_bytes[1] = (uint8_t)((pl16 >> 8) & 0xFFu);
    entry_bytes[2] = (uint8_t)(cl32 & 0xFFu);
    entry_bytes[3] = (uint8_t)((cl32 >> 8) & 0xFFu);
    entry_bytes[4] = (uint8_t)((cl32 >> 16) & 0xFFu);
    entry_bytes[5] = (uint8_t)((cl32 >> 24) & 0xFFu);

    ctx->crc = crc32_update(ctx->crc, entry_bytes, 6);
    ctx->crc = crc32_update(ctx->crc, reinterpret_cast<const uint8_t*>(full_path), path_len);

    uint8_t buf[IO_CHUNK];
    size_t remaining = content_len;
    while (remaining > 0) {
        watchdog_update();
        size_t to_read = remaining > IO_CHUNK ? IO_CHUNK : remaining;
        int got = f.read(buf, to_read);
        if (got <= 0) return false;  /* I/O error ou tamanho mudou entre stat e leitura */
        ctx->crc = crc32_update(ctx->crc, buf, (size_t)got);
        remaining -= (size_t)got;
    }

    ctx->payload_size += 6u + (uint32_t)path_len + (uint32_t)content_len;
    ctx->file_count++;
    return true;
}

bool backup_scan(BackupScanResult& out) {
    out.payload_size = 0;
    out.payload_crc32 = 0;
    out.file_count = 0;

    ScanCtx ctx;
    ctx.payload_size = 0;
    ctx.crc = OTA_CRC32_INIT;
    ctx.file_count = 0;
    ctx.overflow = false;

    if (!walk_dir(String("/"), scan_visitor, &ctx, 0)) return false;
    if (ctx.overflow) return false;

    out.payload_size = ctx.payload_size;
    out.payload_crc32 = ctx.crc ^ 0xFFFFFFFFu;  /* xor-out final */
    out.file_count = ctx.file_count;
    return true;
}

/* ---------------------------------------------------------------------------
 * Pass 2: emit
 * ------------------------------------------------------------------------- */

struct EmitCtx {
    Print* out;
    bool   io_ok;
};

static bool emit_visitor(const char* full_path, File& f, void* vctx) {
    EmitCtx* ctx = static_cast<EmitCtx*>(vctx);
    if (!ctx->io_ok) return false;

    size_t path_len = strlen(full_path);
    size_t content_len = f.size();

    uint8_t entry_bytes[6];
    uint16_t pl16 = (uint16_t)path_len;
    uint32_t cl32 = (uint32_t)content_len;
    entry_bytes[0] = (uint8_t)(pl16 & 0xFFu);
    entry_bytes[1] = (uint8_t)((pl16 >> 8) & 0xFFu);
    entry_bytes[2] = (uint8_t)(cl32 & 0xFFu);
    entry_bytes[3] = (uint8_t)((cl32 >> 8) & 0xFFu);
    entry_bytes[4] = (uint8_t)((cl32 >> 16) & 0xFFu);
    entry_bytes[5] = (uint8_t)((cl32 >> 24) & 0xFFu);

    if (ctx->out->write(entry_bytes, 6) != 6) { ctx->io_ok = false; return false; }
    if (path_len > 0 &&
        ctx->out->write(reinterpret_cast<const uint8_t*>(full_path), path_len) != path_len) {
        ctx->io_ok = false; return false;
    }

    uint8_t buf[IO_CHUNK];
    size_t remaining = content_len;
    while (remaining > 0) {
        watchdog_update();
        size_t to_read = remaining > IO_CHUNK ? IO_CHUNK : remaining;
        int got = f.read(buf, to_read);
        if (got <= 0) { ctx->io_ok = false; return false; }
        if (ctx->out->write(buf, (size_t)got) != (size_t)got) {
            ctx->io_ok = false;
            return false;
        }
        remaining -= (size_t)got;
    }
    return true;
}

bool backup_emit(Print& out,
                 const BackupScanResult& scan,
                 uint32_t firmware_version,
                 uint32_t timestamp) {
    BackupHeader h;
    memset(&h, 0, sizeof(h));
    h.magic            = OTA_BACKUP_MAGIC;
    h.schema_version   = OTA_BACKUP_SCHEMA;
    h.reserved0        = 0;
    read_chip_id(h.chip_id);
    h.firmware_version = firmware_version;
    h.timestamp        = timestamp;
    h.payload_size     = scan.payload_size;
    h.payload_crc32    = scan.payload_crc32;

    /* CRC do header = CRC dos primeiros 36 bytes (tudo exceto header_crc32). */
    uint32_t hcrc = crc32_update(OTA_CRC32_INIT,
                                 reinterpret_cast<const uint8_t*>(&h),
                                 sizeof(BackupHeader) - sizeof(uint32_t));
    h.header_crc32 = hcrc ^ 0xFFFFFFFFu;

    if (out.write(reinterpret_cast<const uint8_t*>(&h), sizeof(BackupHeader)) != sizeof(BackupHeader)) {
        return false;
    }

    EmitCtx ctx;
    ctx.out = &out;
    ctx.io_ok = true;
    if (!walk_dir(String("/"), emit_visitor, &ctx, 0)) return false;
    return ctx.io_ok;
}

} /* namespace ota */
