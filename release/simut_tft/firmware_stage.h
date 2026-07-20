/**
 * @file    src/ota/firmware_stage.h
 * @brief   State machine de upload do .bin.gz para staging custom (Fase 5 OTA).
 *
 * @details Single multipart POST recebe o .bin.gz inteiro. Em UPLOAD_FILE_START
 *          desmonta LittleFS + apaga staging (1 MB raw); em WRITE escreve
 *          chunks alinhados a 256 B via staging_write; em END finaliza CRC32.
 *
 *          NÃO usa Updater/PicoOTA do core arduino-pico (esses requerem
 *          LittleFS montada + firmware.bin como arquivo, e nosso firmware
 *          ~1 MB não cabe junto com configs em LFS de 1 MB).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

class StorageManager;

namespace ota {

enum class StageStatus : uint8_t {
    IDLE = 0,
    BEGIN_FAILED = 1,
    STAGING = 2,
    OVERFLOW_ERR = 3,
    WRITE_FAILED = 4,
    STAGED = 5,
    ABORTED = 6,
};

struct StageSession {
    StageStatus    status;
    uint32_t       bytes_written;       /**< Bytes flushed em staging. */
    uint32_t       crc32_running;       /**< CRC32 dos bytes recebidos. */
    uint32_t       page_buf_filled;
    uint8_t        page_buf[256];
    StorageManager* storage_ref;
};

bool stage_session_begin(StageSession& s, StorageManager* storage);
bool stage_session_feed(StageSession& s, const uint8_t* data, size_t len);
bool stage_session_end(StageSession& s);
void stage_session_abort(StageSession& s);

} /* namespace ota */
