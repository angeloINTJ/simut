/**
 * @file src/ota/firmware_stage.h
 * @brief State machine for .bin.gz upload to custom staging (OTA).
 *
 * @details Single multipart POST receives the entire .bin.gz. In UPLOAD_FILE_START
 * unmounts LittleFS + erases staging (1 MB raw); in WRITE writes
 * 256 B-aligned chunks via staging_write; in END finalizes CRC32.
 *
 * Does NOT use Updater/PicoOTA from the arduino-pico core (those require
 * LittleFS mounted + firmware.bin as file, and our firmware
 * ~1 MB does not fit alongside configs in a 1 MB LFS).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
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
 StageStatus status;
 uint32_t bytes_written; /**< Bytes flushed to staging. */
 uint32_t crc32_running; /**< CRC32 of received bytes. */
 uint32_t page_buf_filled;
 uint8_t page_buf[256];
 StorageManager* storage_ref;
};

bool stage_session_begin(StageSession& s, StorageManager* storage);
bool stage_session_feed(StageSession& s, const uint8_t* data, size_t len);
bool stage_session_end(StageSession& s);
void stage_session_abort(StageSession& s);

} /* namespace ota */
