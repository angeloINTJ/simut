/**
 * @file src/ota/restore.h
 * @brief State machine for .bkp backup restore via streaming chunks.
 *
 * @details Unlike backup_validate (which needs a seekable Stream&),
 * restore consumes chunks coming from the HTTP upload (callback
 * handleUploadData) and maintains state between chunks.
 *
 * VALIDATE mode: reads + computes CRCs + validates; never touches LittleFS.
 * APPLY mode: during CONTENT, writes to the final path;
 * on final commit, files are already in place.
 * On rollback, deletes partially written files.
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
#include "backup.h"
#include "backup_format.h"
#include <LittleFS.h>

namespace ota {

constexpr const char* RESTORE_TMP_SUFFIX = ".restore_tmp";
constexpr size_t RESTORE_MAX_PATH = 200; /* < 256 (path_len uint16) and fits in struct */

enum class RestoreMode : uint8_t {
 VALIDATE = 0, /**< Does not write to LittleFS. */
 APPLY = 1, /**< Writes to final path; commit/rollback at end. */
};

enum class RestorePhase : uint8_t {
 HEADER = 0, /**< Accumulating 40 bytes of BackupHeader. */
 ENTRY_HEADER = 1, /**< Accumulating 6 bytes of BackupEntry. */
 PATH = 2, /**< Accumulating path_len bytes of path. */
 CONTENT = 3, /**< Accumulating/writing content_len bytes. */
 DONE = 4, /**< payload_size complete, CRC validated. */
 FAILED = 5, /**< Error detected; status tells why. */
};

struct RestoreSession {
 RestoreMode mode;
 RestorePhase phase;
 BackupStatus status;

 /* HEADER phase */
 uint8_t header_buf[sizeof(BackupHeader)];
 uint32_t header_filled;
 BackupHeader header;

 /* PAYLOAD phase */
 uint32_t payload_remaining;
 uint32_t payload_crc;
 uint16_t file_count;

 /* ENTRY phase */
 uint8_t entry_buf[6];
 uint32_t entry_filled;
 uint16_t cur_path_len;
 uint32_t cur_content_len;

 /* PATH phase */
 char cur_path[RESTORE_MAX_PATH + 1]; /* +1 nul */
 uint32_t cur_path_filled;

 /* CONTENT phase */
 File cur_file; /**< Open only in APPLY. */
 uint32_t cur_content_remaining;
};

/**
 * @brief Initializes session. In APPLY: cleans up orphan .restore_tmp.
 */
void restore_session_begin(RestoreSession& s, RestoreMode mode);

/**
 * @brief Feeds a chunk of bytes. Can be called multiple times.
 *
 * If the state enters FAILED, subsequent calls are no-op.
 *
 * @return true if not yet failed (FAILED or DONE not differentiated here).
 */
bool restore_session_feed(RestoreSession& s, const uint8_t* data, size_t len);

/**
 * @brief Finalizes the session.
 *
 * In VALIDATE: only reports status (no side effects).
 * In APPLY:
 * - If status == OK and phase == DONE: commit.
 * - Otherwise: rollback (deletes all written files).
 *
 * @param s Session.
 * @param fs_modified Out: true if LittleFS was modified (only APPLY+commit).
 * @return Final status.
 */
BackupStatus restore_session_finish(RestoreSession& s, bool* fs_modified);

/**
 * @brief Aborts the session (cleanup tmps + reset).
 *
 * For use in UPLOAD_FILE_ABORTED.
 */
void restore_session_abort(RestoreSession& s);

/**
 * @brief Standalone cleanup of orphan .restore_tmp (callable at any time).
 *
 * Useful at boot and before any new APPLY session.
 *
 * @return Number of files removed.
 */
uint32_t restore_cleanup_orphan_tmps( );

} /* namespace ota */
