/**
 * @file    src/ota/decompressor.cpp
 * @brief   Wrapper one-shot pull-based sobre uzlib (Fase 3 OTA).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040)
 * @author  Ângelo Moisés Alves
 * @license MIT (wrapper) / zlib (uzlib upstream)
 */
#include "decompressor.h"
#include <string.h>

#if defined(ARDUINO_ARCH_RP2040)
#include <hardware/watchdog.h>
#define OTA_WDT_FEED() watchdog_update()
#else
#define OTA_WDT_FEED() ((void)0)
#endif

namespace ota {

static constexpr size_t OUT_CHUNK = 256;  /* Era 1024; reduzido pra economizar 768B de BSS. */
static unsigned char s_out_buf[OUT_CHUNK];

/* Trampoline C → C++. uc é primeiro membro de GunzipContext (cast safe). */
extern "C" {
static int trampoline_read_cb(struct uzlib_uncomp* d) {
    GunzipContext* ctx = reinterpret_cast<GunzipContext*>(d);
    if (!ctx->src_cb) return -1;
    return ctx->src_cb(ctx->src_user);
}
}

bool gunzip_begin(GunzipContext& ctx) {
    memset(&ctx.uc, 0, sizeof(ctx.uc));
    ctx.uc.source = nullptr;
    ctx.uc.source_limit = nullptr;
    ctx.uc.source_read_cb = trampoline_read_cb;
    ctx.src_cb = nullptr;
    ctx.src_user = nullptr;
    ctx.last_status = 0;
    ctx.finished = false;
    ctx.out_aborted = false;
    uzlib_uncompress_init(&ctx.uc, ctx.dict, GUNZIP_DICT_SIZE);
    return true;
}

bool gunzip_decompress(GunzipContext& ctx,
                       GunzipSourceCb src_cb, void* src_user,
                       GunzipOutCb out_cb, void* out_user) {
    if (!src_cb || !out_cb) return false;
    ctx.src_cb = src_cb;
    ctx.src_user = src_user;
    ctx.uc.source = nullptr;
    ctx.uc.source_limit = nullptr;
    ctx.uc.source_read_cb = trampoline_read_cb;

    int hr = uzlib_gzip_parse_header(&ctx.uc);
    if (hr != TINF_OK) {
        ctx.last_status = hr;
        return false;
    }

    while (true) {
        OTA_WDT_FEED();
        ctx.uc.dest = s_out_buf;
        ctx.uc.dest_limit = s_out_buf + OUT_CHUNK;

        int rc = uzlib_uncompress_chksum(&ctx.uc);
        size_t produced = (size_t)(ctx.uc.dest - s_out_buf);

        if (produced > 0) {
            if (!out_cb(s_out_buf, produced, out_user)) {
                ctx.out_aborted = true;
                ctx.last_status = TINF_DATA_ERROR;
                return false;
            }
        }

        if (rc == TINF_DONE) {
            ctx.finished = true;
            ctx.last_status = TINF_DONE;
            return true;
        }
        if (rc == TINF_OK) {
            /* Mais bytes disponíveis ou mais espaço de output necessário. */
            continue;
        }
        /* Erro de format/checksum/dict. */
        ctx.last_status = rc;
        return false;
    }
}

bool gunzip_finish(GunzipContext& ctx) {
    return ctx.finished && !ctx.out_aborted && ctx.last_status == TINF_DONE;
}

} /* namespace ota */
