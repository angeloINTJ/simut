/**
 * @file    src/ota/firmware_stage.cpp
 * @brief   State machine de upload do .bin.gz para staging custom (Fase 5 OTA).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "firmware_stage.h"
#include "staging.h"
#include "config_snapshot.h"
#include "ota_layout.h"
#include "backup.h"
#include "StorageManager.h"

#include <Arduino.h>
#include <hardware/watchdog.h>
#include <string.h>

namespace ota {

/* v4.4.0 F-OTA-STAGE-NOBLOCK: erase on-demand sector-by-sector evita o
 * ~13 s de erase sync em UPLOAD_FILE_START que derrubava o socket TCP
 * (cliente desconectava enquanto WebServer não retornava do callback).
 * Cada flush_page apaga só o setor que vai escrever (~50 ms).
 *
 * Statics em vez de struct members pra evitar recompile cascade via
 * firmware_stage.h (WebManager.h inclui). OTA stage é serial — só uma
 * sessão de cada vez. */
static uint8_t  s_sectors_erased[32];   /* 256 setores ÷ 8 bits */
static uint16_t s_snapshot_len;

static bool ensure_sector_erased(uint32_t off) {
    uint32_t sec = off / OTA_FLASH_SECTOR_SIZE;
    if (sec >= sizeof(s_sectors_erased) * 8) return false;
    if (s_sectors_erased[sec >> 3] & (1u << (sec & 7))) return true;
    if (!staging_erase_sector(sec * OTA_FLASH_SECTOR_SIZE)) return false;
    s_sectors_erased[sec >> 3] |= (1u << (sec & 7));
    return true;
}

bool stage_session_begin(StageSession& s, StorageManager* storage) {
    s.status = StageStatus::IDLE;
    s.bytes_written = 0;
    s.bytes_received = 0;
    s.crc32_running = OTA_CRC32_INIT;
    s.page_buf_filled = 0;
    s.storage_ref = storage;
    memset(s_sectors_erased, 0, sizeof(s_sectors_erased));
    s_snapshot_len = 0;

    if (!storage) { s.status = StageStatus::BEGIN_FAILED; return false; }

    /* Snapshot ANTES de unmount LFS (precisa LFS-readable). Commit em end. */
    s_snapshot_len = ota_snapshot_serialize();

    if (!staging_session_begin_lite(storage)) {
        s.status = StageStatus::BEGIN_FAILED;
        return false;
    }
    s.status = StageStatus::STAGING;
    return true;
}

static bool flush_page(StageSession& s) {
    if (s.bytes_written + OTA_FLASH_PAGE_SIZE > OTA_STAGING_MAX_SIZE) {
        s.status = StageStatus::OVERFLOW_ERR;
        return false;
    }
    if (!ensure_sector_erased(s.bytes_written)) {
        s.status = StageStatus::WRITE_FAILED;
        return false;
    }
    if (!staging_write(s.bytes_written, s.page_buf, OTA_FLASH_PAGE_SIZE)) {
        s.status = StageStatus::WRITE_FAILED;
        return false;
    }
    s.bytes_written += OTA_FLASH_PAGE_SIZE;
    s.page_buf_filled = 0;
    return true;
}

bool stage_session_feed(StageSession& s, const uint8_t* data, size_t len) {
    if (s.status != StageStatus::STAGING) return false;
    if (!data || len == 0) return true;

    s.crc32_running  = crc32_update(s.crc32_running, data, len);
    s.bytes_received += (uint32_t)len;

    while (len > 0) {
        watchdog_update();
        size_t free_in_page = OTA_FLASH_PAGE_SIZE - s.page_buf_filled;
        size_t take = (len < free_in_page) ? len : free_in_page;
        memcpy(s.page_buf + s.page_buf_filled, data, take);
        s.page_buf_filled += take;
        data += take;
        len -= take;

        if (s.page_buf_filled == OTA_FLASH_PAGE_SIZE) {
            if (!flush_page(s)) return false;
        }
    }
    return true;
}

bool stage_session_end(StageSession& s) {
    if (s.status != StageStatus::STAGING) return false;
    if (s.page_buf_filled > 0) {
        memset(s.page_buf + s.page_buf_filled, 0xFF,
               OTA_FLASH_PAGE_SIZE - s.page_buf_filled);
        if (!flush_page(s)) return false;
    }
    /* Snapshot commit no fim — apaga setor 255 (último, reservado pra
     * snapshot) e escreve. Não-fatal: sem snapshot, device sobe em
     * factory pós-apply e user restaura via .bkp. */
    if (s_snapshot_len > 0) {
        const uint32_t snap_off = OTA_STAGING_MAX_SIZE - OTA_FLASH_SECTOR_SIZE;
        if (ensure_sector_erased(snap_off)) {
            ota_snapshot_commit(s_snapshot_len);
        }
    }
    s.crc32_running ^= 0xFFFFFFFFu;
    s.status = StageStatus::STAGED;
    return true;
}

void stage_session_abort(StageSession& s) {
    if (s.storage_ref) {
        staging_session_end(s.storage_ref);
    }
    s.status = StageStatus::ABORTED;
    s.bytes_written = 0;
    s.crc32_running = 0;
    s.page_buf_filled = 0;
}

} /* namespace ota */
