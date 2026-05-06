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
#include "ota_layout.h"
#include "backup.h"
#include "../../StorageManager.h"

#include <Arduino.h>
#include <hardware/watchdog.h>
#include <string.h>

namespace ota {

bool stage_session_begin(StageSession& s, StorageManager* storage) {
    s.status = StageStatus::IDLE;
    s.bytes_written = 0;
    s.crc32_running = OTA_CRC32_INIT;
    s.page_buf_filled = 0;
    s.storage_ref = storage;

    if (!staging_session_begin(storage)) {
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

    s.crc32_running = crc32_update(s.crc32_running, data, len);

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
