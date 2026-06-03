/**
 * @file src/ota/restore.cpp
 * @brief Implementação do state machine de restore (OTA).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "restore.h"
#include "backup.h"
#include "backup_format.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <hardware/watchdog.h>
#include <string.h>

namespace ota {

static constexpr int RESTORE_WALK_MAX_DEPTH = 8;

/* ---------------------------------------------------------------------------
 * Helpers de path / FS
 * ------------------------------------------------------------------------- */

static bool path_is_safe(const char* p, uint16_t len) {
 if (len == 0 || p[0] != '/') return false;
 for (uint16_t i = 0; i < len; i++) {
 if (p[i] == 0) return false;
 }
 for (uint16_t i = 0; i + 1 < len; i++) {
 if (p[i] == '.' && p[i + 1] == '.') return false;
 }
 return true;
}

/* Garante que diretórios pai do path existam (para LittleFS.open(..., "w")). */
static bool ensure_parent_dirs(const char* path) {
 /* LittleFS implicit cria parents na maior parte dos casos, mas garantimos. */
 char buf[RESTORE_MAX_PATH + 1];
 size_t n = strnlen(path, sizeof(buf) - 1);
 memcpy(buf, path, n);
 buf[n] = '\0';
 for (size_t i = 1; i < n; i++) {
 if (buf[i] == '/') {
 buf[i] = '\0';
 if (!LittleFS.exists(buf)) {
 LittleFS.mkdir(buf);
 }
 buf[i] = '/';
 }
 }
 return true;
}

/* ---------------------------------------------------------------------------
 * Walk + commit/cleanup unificado.
 *
 * Mantemos uma lista in-memory dos paths .restore_tmp criados durante a
 * sessão. Evita walk recursivo da LittleFS (que duplicaria código de
 * backup.cpp/walk_dir e puxaria mais símbolos da lib).
 * ------------------------------------------------------------------------- */

/* Pool global; tamanho dimensionado para ~32 arquivos de path médio.
 * Se estourar, restauração para com IO_ERROR — aceitável pra v1. */
static constexpr size_t TMP_POOL_BYTES = 2048;
static char s_tmp_pool[TMP_POOL_BYTES];
static uint16_t s_tmp_offsets[64];
static uint16_t s_tmp_count;
static uint16_t s_tmp_used;

static void tmp_list_reset( ) {
 s_tmp_count = 0;
 s_tmp_used = 0;
}

static bool tmp_list_add(const char* path) {
 size_t plen = strlen(path);
 if (s_tmp_count >= sizeof(s_tmp_offsets) / sizeof(s_tmp_offsets[0])) return false;
 if (s_tmp_used + plen + 1 > TMP_POOL_BYTES) return false;
 s_tmp_offsets[s_tmp_count++] = s_tmp_used;
 memcpy(s_tmp_pool + s_tmp_used, path, plen + 1);
 s_tmp_used += plen + 1;
 return true;
}

static const char* tmp_list_get(uint16_t i) {
 return s_tmp_pool + s_tmp_offsets[i];
}

uint32_t restore_cleanup_orphan_tmps( ) {
 uint32_t removed = 0;
 for (uint16_t i = 0; i < s_tmp_count; i++) {
 watchdog_update( );
 if (LittleFS.remove(tmp_list_get(i))) removed++;
 }
 tmp_list_reset( );
 return removed;
}

/* Strategy v2: sem rename. Escrevemos direto no path final em APPLY mode.
 * commit é no-op. rollback (em failure) deleta arquivos parcialmente escritos.
 *
 * Trade-off vs plano §6 ("estratégia atômica via .tmp + rename"): se CRC
 * falhar no final do upload, arquivos finais já foram tocados. Mitigação:
 * cliente DEVE chamar /api/restore/validate antes de /api/restore/apply para
 * garantir integridade prévia. Sem essa chamada, falha de integridade aplica
 * dano parcial. Económia: ~16 KB de flash que LittleFS.rename puxava. */
static uint32_t commit_all_tmps( ) {
 uint32_t n = s_tmp_count;
 tmp_list_reset( );
 return n; /* arquivos já estão no path final desde a escrita */
}

/* ---------------------------------------------------------------------------
 * Session lifecycle
 * ------------------------------------------------------------------------- */

static void session_reset(RestoreSession& s) {
 s.phase = RestorePhase::HEADER;
 s.status = BackupStatus::INTERNAL_ERROR; /* substituído quando finish( ) */
 s.header_filled = 0;
 memset(&s.header, 0, sizeof(s.header));
 s.payload_remaining = 0;
 s.payload_crc = OTA_CRC32_INIT;
 s.file_count = 0;
 s.entry_filled = 0;
 s.cur_path_len = 0;
 s.cur_content_len = 0;
 s.cur_path[0] = '\0';
 s.cur_path_filled = 0;
 s.cur_content_remaining = 0;
 if (s.cur_file) s.cur_file.close( );
}

void restore_session_begin(RestoreSession& s, RestoreMode mode) {
 s.mode = mode;
 if (mode == RestoreMode::APPLY) {
 /* Limpa qualquer .restore_tmp órfão de runs anteriores antes de começar. */
 restore_cleanup_orphan_tmps( );
 }
 session_reset(s);
}

void restore_session_abort(RestoreSession& s) {
 if (s.cur_file) s.cur_file.close( );
 if (s.mode == RestoreMode::APPLY) {
 restore_cleanup_orphan_tmps( );
 }
 s.phase = RestorePhase::FAILED;
 s.status = BackupStatus::IO_ERROR;
}

/* ---------------------------------------------------------------------------
 * State machine
 * ------------------------------------------------------------------------- */

static void fail(RestoreSession& s, BackupStatus st) {
 s.status = st;
 s.phase = RestorePhase::FAILED;
 if (s.cur_file) s.cur_file.close( );
}

/* Parse e valida header (chamado quando header_filled == sizeof(BackupHeader)). */
static void on_header_complete(RestoreSession& s) {
 memcpy(&s.header, s.header_buf, sizeof(BackupHeader));

 if (s.header.magic != OTA_BACKUP_MAGIC) { fail(s, BackupStatus::BAD_MAGIC); return; }
 if (s.header.schema_version != OTA_BACKUP_SCHEMA) {
 fail(s, BackupStatus::UNSUPPORTED_SCHEMA); return;
 }

 /* Header CRC: primeiros 36 bytes. */
 uint32_t hcrc = crc32_update(OTA_CRC32_INIT, s.header_buf,
 sizeof(BackupHeader) - sizeof(uint32_t));
 hcrc ^= 0xFFFFFFFFu;
 if (hcrc != s.header.header_crc32) { fail(s, BackupStatus::HEADER_CRC_MISMATCH); return; }

 uint8_t my_chip[8];
 read_chip_id(my_chip);
 if (memcmp(my_chip, s.header.chip_id, 8) != 0) {
 fail(s, BackupStatus::CHIP_ID_MISMATCH); return;
 }

 s.payload_remaining = s.header.payload_size;
 if (s.payload_remaining == 0) {
 /* Backup vazio. CRC esperado é 0xFFFFFFFF ^ xor-out = 0. */
 uint32_t pcrc = OTA_CRC32_INIT ^ 0xFFFFFFFFu;
 if (pcrc != s.header.payload_crc32) { fail(s, BackupStatus::PAYLOAD_CRC_MISMATCH); return; }
 s.phase = RestorePhase::DONE;
 s.status = BackupStatus::OK;
 return;
 }
 s.phase = RestorePhase::ENTRY_HEADER;
 s.entry_filled = 0;
}

static void on_entry_header_complete(RestoreSession& s) {
 s.cur_path_len = (uint16_t)s.entry_buf[0] | ((uint16_t)s.entry_buf[1] << 8);
 s.cur_content_len = (uint32_t)s.entry_buf[2]
 | ((uint32_t)s.entry_buf[3] << 8)
 | ((uint32_t)s.entry_buf[4] << 16)
 | ((uint32_t)s.entry_buf[5] << 24);

 if (s.cur_path_len == 0 || s.cur_path_len > RESTORE_MAX_PATH) {
 fail(s, BackupStatus::PATH_TOO_LONG); return;
 }
 if (s.cur_path_len > s.payload_remaining) { fail(s, BackupStatus::PAYLOAD_TRUNCATED); return; }
 if (s.cur_content_len > s.payload_remaining - s.cur_path_len) {
 fail(s, BackupStatus::PAYLOAD_TRUNCATED); return;
 }

 s.cur_path_filled = 0;
 s.phase = RestorePhase::PATH;
}

static void on_path_complete(RestoreSession& s) {
 s.cur_path[s.cur_path_len] = '\0';
 if (!path_is_safe(s.cur_path, s.cur_path_len)) {
 fail(s, BackupStatus::PATH_INVALID); return;
 }
 if (s.mode == RestoreMode::APPLY) {
 /* Escreve direto no path final (sem rename). Ver commit_all_tmps.
 *
 * F-RESTORE fix: feed WDT antes de cada operação que pode triggerar
 * GC do LittleFS (mkdir, open com truncate). Sob LFS fragmentado
 * (>70%), GC interno bloqueia por segundos. Em backup com 32 arquivos
 * o tempo total acumulado excedia WDT 8s e o restore perdia o tail
 * do payload (lang/themes/últimos history). */
 watchdog_update( );
 ensure_parent_dirs(s.cur_path);
 watchdog_update( );
 if (s.cur_file) s.cur_file.close( );
 s.cur_file = LittleFS.open(s.cur_path, "w");
 watchdog_update( );
 if (!s.cur_file) { fail(s, BackupStatus::IO_ERROR); return; }
 /* Tracking: para rollback em CRC mismatch deletamos os escritos. */
 if (!tmp_list_add(s.cur_path)) { fail(s, BackupStatus::IO_ERROR); return; }
 }
 s.cur_content_remaining = s.cur_content_len;
 s.phase = RestorePhase::CONTENT;
}

static void on_content_complete(RestoreSession& s) {
 if (s.mode == RestoreMode::APPLY && s.cur_file) {
 s.cur_file.close( );
 }
 s.file_count++;
 s.entry_filled = 0;
 if (s.payload_remaining == 0) {
 /* Validação final do CRC do payload. */
 uint32_t pcrc = s.payload_crc ^ 0xFFFFFFFFu;
 if (pcrc != s.header.payload_crc32) { fail(s, BackupStatus::PAYLOAD_CRC_MISMATCH); return; }
 s.phase = RestorePhase::DONE;
 s.status = BackupStatus::OK;
 } else {
 s.phase = RestorePhase::ENTRY_HEADER;
 }
}

bool restore_session_feed(RestoreSession& s, const uint8_t* data, size_t len) {
 while (len > 0 &&
 s.phase != RestorePhase::FAILED &&
 s.phase != RestorePhase::DONE) {
 watchdog_update( );
 switch (s.phase) {
 case RestorePhase::HEADER: {
 size_t need = sizeof(BackupHeader) - s.header_filled;
 size_t take = (len < need) ? len : need;
 memcpy(s.header_buf + s.header_filled, data, take);
 s.header_filled += take;
 data += take; len -= take;
 if (s.header_filled == sizeof(BackupHeader)) on_header_complete(s);
 break;
 }
 case RestorePhase::ENTRY_HEADER: {
 size_t need = sizeof(s.entry_buf) - s.entry_filled;
 size_t take = (len < need) ? len : need;
 /* Bytes do entry header CONTAM no payload_crc + payload_remaining. */
 if (take > s.payload_remaining) { fail(s, BackupStatus::PAYLOAD_TRUNCATED); break; }
 memcpy(s.entry_buf + s.entry_filled, data, take);
 s.payload_crc = crc32_update(s.payload_crc, data, take);
 s.payload_remaining -= take;
 s.entry_filled += take;
 data += take; len -= take;
 if (s.entry_filled == sizeof(s.entry_buf)) on_entry_header_complete(s);
 break;
 }
 case RestorePhase::PATH: {
 size_t need = s.cur_path_len - s.cur_path_filled;
 size_t take = (len < need) ? len : need;
 if (take > s.payload_remaining) { fail(s, BackupStatus::PAYLOAD_TRUNCATED); break; }
 memcpy(s.cur_path + s.cur_path_filled, data, take);
 s.payload_crc = crc32_update(s.payload_crc, data, take);
 s.payload_remaining -= take;
 s.cur_path_filled += take;
 data += take; len -= take;
 if (s.cur_path_filled == s.cur_path_len) on_path_complete(s);
 break;
 }
 case RestorePhase::CONTENT: {
 size_t need = s.cur_content_remaining;
 size_t take = (len < need) ? len : need;
 if (take > s.payload_remaining) { fail(s, BackupStatus::PAYLOAD_TRUNCATED); break; }
 s.payload_crc = crc32_update(s.payload_crc, data, take);
 if (s.mode == RestoreMode::APPLY && s.cur_file) {
 size_t w = s.cur_file.write(data, take);
 if (w != take) { fail(s, BackupStatus::IO_ERROR); break; }
 }
 s.payload_remaining -= take;
 s.cur_content_remaining -= take;
 data += take; len -= take;
 if (s.cur_content_remaining == 0) on_content_complete(s);
 break;
 }
 case RestorePhase::DONE:
 case RestorePhase::FAILED:
 break; /* unreachable, mas keep -Wswitch happy */
 }
 }
 return s.phase != RestorePhase::FAILED;
}

BackupStatus restore_session_finish(RestoreSession& s, bool* fs_modified) {
 if (fs_modified) *fs_modified = false;

 if (s.cur_file) s.cur_file.close( );

 /* Se ainda em meio ao stream, marca como truncated. */
 if (s.phase != RestorePhase::DONE && s.phase != RestorePhase::FAILED) {
 fail(s, BackupStatus::PAYLOAD_TRUNCATED);
 }

 if (s.mode == RestoreMode::APPLY) {
 if (s.phase == RestorePhase::DONE && s.status == BackupStatus::OK) {
 commit_all_tmps( );
 if (fs_modified) *fs_modified = true;
 } else {
 restore_cleanup_orphan_tmps( );
 }
 }
 return s.status;
}

} /* namespace ota */
