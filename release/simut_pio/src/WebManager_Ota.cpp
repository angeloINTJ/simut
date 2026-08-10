/**
 * @file WebManager_Ota.cpp
 * @brief OTA endpoints — GET /api/backup (download .bkp).
 *
 * @details Pipeline: auth → HeavyTaskGuard → scan (CRC32 + size) →
 * headers (Content-Length, Content-Disposition with chip_id+timestamp)
 * → emit (header + payload via Print adapter over safeSend).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include "ota/backup.h"
#include "ota/backup_format.h"
#include "ota/restore.h"
#include "ota/staging.h"
#include "ota/firmware_stage.h"
#include "ota/validation.h"
#include "ota/metadata.h"
#include "ota/orchestrator.h"
#include <Print.h>
#include <time.h>
#include <hardware/watchdog.h>

/* Adapter Print → WebManager::safeSend (declared friend in WebManager.h). */
struct OtaBackupPrintAdapter : public Print {
 WebManager* w;
 bool ok;
 explicit OtaBackupPrintAdapter(WebManager* m) : w(m), ok(true) {}

 size_t write(uint8_t b) override {
 if (!ok) return 0;
 ok = w->safeSend(reinterpret_cast<const char*>(&b), 1);
 return ok ? 1u : 0u;
 }
 size_t write(const uint8_t* buf, size_t size) override {
 if (!ok || size == 0) return ok ? size : 0u;
 ok = w->safeSend(reinterpret_cast<const char*>(buf), size);
 return ok ? size : 0u;
 }
};

void WebManager::handleApiBackup( ) {
 /* Permission: backup has the same risk level as reading all
 * LittleFS files, so it matches PERM_FILE_READ. */
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_FILE_READ)) {
 _server.send(403, "text/plain", "Forbidden");
 return;
 }

 /* Touch priority: respects physical display usage (long operation). */
 if (rejectIfTouchPriority( )) return;

 /* Serialize against other heavy tasks (uploads, exports, etc.). */
 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 _server.send(503, "text/plain", "System Busy");
 return;
 }

 /* Pass 1: scan. */
 ota::BackupScanResult scan;
 {
 StorageManager::ReadGuard rg(_storageRef);
 if (!ota::backup_scan(scan)) {
 _server.send(500, "text/plain", "Backup scan failed");
 LOG_CODE(LOG_ERROR, "OTA", SEC_CONFIG_CHANGED, _currentUserId, "backup_scan");
 return;
 }
 }

 /* Build filename: backup_<chip_id_hex>_<unix_ts>.bkp */
 uint8_t chip[8];
 ota::read_chip_id(chip);
 char chipHex[17];
 for (int i = 0; i < 8; i++) {
 static const char H[] = "0123456789abcdef";
 chipHex[i * 2] = H[(chip[i] >> 4) & 0xF];
 chipHex[i * 2 + 1] = H[chip[i] & 0xF];
 }
 chipHex[16] = '\0';

 time_t now = time(nullptr);
 uint32_t ts = (now > 0) ? (uint32_t)now : 0u;

 char fname[64];
 snprintf(fname, sizeof(fname), "backup_%s_%lu.bkp", chipHex, (unsigned long)ts);

 /* Total = header (40 B) + payload. */
 uint32_t total = (uint32_t)sizeof(BackupHeader) + scan.payload_size;

 char dispo[128];
 snprintf(dispo, sizeof(dispo), "attachment; filename=\"%s\"", fname);
 _server.sendHeader("Content-Disposition", dispo);
 _server.sendHeader("X-Backup-Files", String(scan.file_count));
 _server.sendHeader("X-Backup-Schema", String((unsigned)OTA_BACKUP_SCHEMA));
 /* /psz/pcrc in header so browser can verify download integrity
 * before OTA (without needing a separate /api/fs/manifest endpoint). */
 _server.sendHeader("X-Backup-PSize", String(scan.payload_size));
 _server.sendHeader("X-Backup-PCrc", String(scan.payload_crc32));
 _server.setContentLength(total);
 _server.send(200, "application/octet-stream", "");

 /* Pass 2: emit. */
 OtaBackupPrintAdapter adapter(this);
 uint32_t fwv = ota::encode_version_u32(SIMUT_VERSION);
 bool ok;
 {
 StorageManager::ReadGuard rg(_storageRef);
 ok = ota::backup_emit(adapter, scan, fwv, ts);
 }

 if (!ok || !adapter.ok) {
 /* Headers already sent — can only log. Client will detect
 * truncation via Content-Length mismatch or invalid CRC. */
 LOG_CODE(LOG_WARN, "OTA", WEB_DISCONNECT_FILE, _currentUserId, "backup_emit");
 drainOrDrop( );
 _drainPending = false;
 return;
 }

 LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId,
 String("backup ") + scan.file_count + "f " + scan.payload_size + "B");

 /* Same reason as safeStreamFile( ) — see the note there. This response is
  * setContentLength( ), so nothing in the abort discipline covers its tail,
  * and the framework retires the client inside handleClient( ) with a bare
  * stop( ) whose ACK-wait renews on progress and never feeds the watchdog.
  * update( )'s drainOrDrop( ) runs after that, too late.
  *
  * Measured 2026-08-10 with safeStreamFile already fixed: three sequential
  * GET /api/backup (794 KB each) still produced a watchdog reboot on the
  * second, autopsy `C0=[WEB_POLL] hp=721 (219)` — the same one, reached by
  * this path instead. Drain before returning and it stops. */
 drainOrDrop( );
 _drainPending = false;
}

/* ===========================================================================
 * Validation and Restore
 *
 * Pipeline: HTTP upload (multipart) → handleApiRestoreUploadData (callback) →
 * handleApiRestoreValidate or handleApiRestoreApply (final). The callback is
 * shared: the route URI decides the mode (VALIDATE vs APPLY).
 * ========================================================================= */

void WebManager::handleApiRestoreUploadData( ) {
 HTTPUpload& upload = _server.upload( );
 bool is_stage = (_server.arg("op") == "stage");
 if (upload.status == UPLOAD_FILE_START) {
 _restoreRejected = false;
 if (is_stage) {
 /* Pre-check ADMIN-ONLY permission: OTA stage erases 1 MB of
 * flash — only admin can trigger. Without perm, doesn't unmount LFS;
 * status stays IDLE; finish responds 403. */
 if (getAuthPerms( ) == PERM_FULL_ADMIN) {
 ota::stage_session_begin(_stageSession, _storageRef);
 } else {
 _stageSession.status = ota::StageStatus::IDLE;
 _restoreRejected = true;
 }
 } else {
 ota::RestoreMode mode = (_server.arg("op") == "apply")
 ? ota::RestoreMode::APPLY : ota::RestoreMode::VALIDATE;
 /* The restore branch had no gate here at all — it was checked
  * only in the finish handler, which the framework calls AFTER
  * the entire body has passed through this callback. An apply
  * feed writes each entry straight to its final path (see
  * on_path_complete: "sem rename"), so by the time the 403 was
  * emitted /config, /calib.csv and /history had already been
  * overwritten, with no cookie required to do it. The check is
  * the same one the finish handler makes, moved to the first
  * byte, and it mirrors what the stage branch above and
  * handleUploadData have always done. */
 uint16_t need = (mode == ota::RestoreMode::APPLY) ? PERM_FILE_UPLOAD
                                                   : PERM_FILE_READ;
 if (!(getAuthPerms( ) & need)) {
 _restoreRejected = true;
 /* Logged because the old hole left no trace at all: the write
  * happened in the feed and the only LOG_CODE on this route sits
  * in the finish handler, behind the very check that failed. An
  * exposed device therefore had nothing to show for the request,
  * which is the worst possible answer to "was I hit?". */
 /* The refusal is recorded in the finish handler, not here. Logging
  * from inside the multipart callback is not itself the fault — the
  * reboot that showed up while chasing this was the non-chunked park
  * on the 403, and it survived removing the log line. But the finish
  * handler is where every other route on this server logs, and a
  * flash write inside the parser is not a habit worth starting. */
 /* A refused session never reaches the unpause in finish, so a
  * lockout left over from a previous upload whose client vanished
  * would stay held and the display frozen. Cheapest place to
  * notice it is here. */
 if (_restoreCorePaused && _displayRef) {
 _displayRef->pauseRendering(false);
 _restoreCorePaused = false;
 }
 return;
 }
 ota::restore_session_begin(_restoreSession, mode);
 /* Pause Core 1 ONCE at start of apply session
 * (instead of RenderGuard per chunk). Spans entire upload →
 * single lockout transition → much lower chance of deadlock. */
 if (mode == ota::RestoreMode::APPLY && _displayRef && !_restoreCorePaused) {
 _displayRef->pauseRendering(true);
 _restoreCorePaused = true;
 }
 }
 } else if (upload.status == UPLOAD_FILE_WRITE) {
 /* Fed before the latch check on purpose: a refused upload still
  * streams its whole body through here, and starving the watchdog
  * would turn a 403 into a reboot. */
 feedWatchdog( );
 if (_restoreRejected) return;
 if (is_stage) {
 ota::stage_session_feed(_stageSession, upload.buf, upload.currentSize);
 } else {
 /* Core 1 already paused since START.
 * No RenderGuard recreated per chunk — saving hundreds of
 * lockout cycles that occasionally deadlock. */
 ota::restore_session_feed(_restoreSession, upload.buf, upload.currentSize);
 }
 } else if (upload.status == UPLOAD_FILE_END) {
 if (_restoreRejected) return;
 /* Stage needs explicit finalize (pad last page + xor-out CRC).
 * Restore has no separate finalize — the finish handler manages everything. */
 if (is_stage) {
 ota::stage_session_end(_stageSession);
 }
 /* Resume Core 1 BEFORE the finish handler (which will emit JSON response
 * and — if OK — trigger safeReboot). Display briefly resumes rendering
 * (shows "Applying restore..." if setBootStatusKey is called
 * before safeReboot). */
 if (_restoreCorePaused && _displayRef) {
 _displayRef->pauseRendering(false);
 _restoreCorePaused = false;
 }
 } else if (upload.status == UPLOAD_FILE_ABORTED) {
 if (_restoreRejected) return;
 if (is_stage) {
 ota::stage_session_abort(_stageSession);
 } else {
 ota::restore_session_abort(_restoreSession);
 }
 /* Cleanup lockout on error path too. */
 if (_restoreCorePaused && _displayRef) {
 _displayRef->pauseRendering(false);
 _restoreCorePaused = false;
 }
 }
}

/* Emits restore state JSON. Numeric codes from BackupStatus enum
 * are interpreted by the client (maps status → message). */
static void emit_restore_json(WebServer& srv, const ota::RestoreSession& s,
 bool fs_modified) {
 char buf[224];
 int code = (s.status == ota::BackupStatus::OK) ? 200 :
 (s.status == ota::BackupStatus::IO_ERROR ? 500 : 422);
 if (s.header.magic == OTA_BACKUP_MAGIC) {
 char chip_hex[17];
 for (int i = 0; i < 8; i++) {
 uint8_t b = s.header.chip_id[i];
 uint8_t hi = (b >> 4) & 0xF, lo = b & 0xF;
 chip_hex[i*2] = (char)(hi < 10 ? '0' + hi : 'a' + hi - 10);
 chip_hex[i*2 + 1] = (char)(lo < 10 ? '0' + lo : 'a' + lo - 10);
 }
 chip_hex[16] = '\0';
 snprintf(buf, sizeof(buf),
 "{\"st\":%u,\"chip\":\"%s\",\"fwv\":%lu,\"psz\":%lu,\"fc\":%u,\"fsm\":%u}",
 (unsigned)s.status, chip_hex,
 (unsigned long)s.header.firmware_version,
 (unsigned long)s.header.payload_size,
 (unsigned)s.file_count, fs_modified ? 1u : 0u);
 } else {
 snprintf(buf, sizeof(buf), "{\"st\":%u,\"fsm\":%u}",
 (unsigned)s.status, fs_modified ? 1u : 0u);
 }
 srv.send(code, "application/json", buf);
}

void WebManager::handleApiRestoreFinish( ) {
 String op = _server.arg("op");
 bool is_stage = (op == "stage");
 bool is_apply = (op == "apply");

 if (is_stage) {
 /* OTA stage finish: ADMIN-ONLY. */
 if (getAuthPerms( ) != PERM_FULL_ADMIN) {
 _server.send(403, "text/plain", "Forbidden — admin only");
 return;
 }
 bool ok_staged = (_stageSession.status == ota::StageStatus::STAGED);
 bool commit = (_server.arg("commit") == "1");

 /* Dry-run validate BEFORE remounting LFS — after
 * remount, LittleFS reformats the area and staging
 * content becomes garbage. Validation accesses staging via XIP read. */
 ota::ValidationReport vr;
 memset(&vr, 0, sizeof(vr));
 bool valid = false;
 if (ok_staged) {
 valid = ota::ota_validate_staging(_stageSession, vr);
 }

 /* Remount decision:
 * commit=1 + valid → persists staging, writes metadata
 * COMMITTED, does NOT remount LFS (next step
 * is POST /api/ota/apply). Device stays in
 * "awaiting apply" mode — to abort,
 * reboot.
 * otherwise → remount LFS, no metadata. */
 bool committed = false;
 if (ok_staged && valid && commit) {
 ota::UpdateMetadata m;
 memset(&m, 0, sizeof(m));
 m.magic = ota::OTA_MAGIC_PENDING;
 m.state = ota::STATE_COMMITTED;
 m.compressed_size = vr.compressed_size;
 m.uncompressed_size = vr.decompressed_size;
 m.compressed_crc32 = vr.compressed_crc;
 m.uncompressed_crc32 = vr.decompressed_crc;
 m.attempts = 0;
 /* Core 1 already active — wrap in flash safe mode. */
 {
 RenderGuard rg(_displayRef);
 _storageRef->enterFlashSafeMode( );
 committed = ota::ota_metadata_write(m);
 _storageRef->exitFlashSafeMode( );
 }
 /* Do NOT remount LFS — staging preserved for apply. */
 } else if (ok_staged) {
 /* Testing: remount. */
 RenderGuard rg(_displayRef);
 ota::staging_session_end(_storageRef);
 } else if (_stageSession.status == ota::StageStatus::STAGING ||
 _stageSession.status == ota::StageStatus::OVERFLOW_ERR ||
 _stageSession.status == ota::StageStatus::WRITE_FAILED) {
 RenderGuard rg(_displayRef);
 ota::stage_session_abort(_stageSession);
 }

 char buf[256];
 if (ok_staged) {
 snprintf(buf, sizeof(buf),
 "{\"st\":%u,\"bytes\":%lu,\"crc32\":\"%08lX\","
 "\"v\":%u,\"dsize\":%lu,\"dcrc\":\"%08lX\",\"committed\":%u}",
 (unsigned)_stageSession.status,
 (unsigned long)_stageSession.bytes_written,
 (unsigned long)_stageSession.crc32_running,
 (unsigned)vr.status,
 (unsigned long)vr.decompressed_size,
 (unsigned long)vr.decompressed_crc,
 committed ? 1u : 0u);
 } else {
 snprintf(buf, sizeof(buf),
 "{\"st\":%u,\"bytes\":%lu,\"crc32\":\"%08lX\"}",
 (unsigned)_stageSession.status,
 (unsigned long)_stageSession.bytes_written,
 (unsigned long)_stageSession.crc32_running);
 }
 bool overall_ok = ok_staged && valid;
 LOG_CODE(overall_ok ? LOG_INFO : LOG_WARN, "OTA", WEB_UPLOAD,
 (int)_stageSession.status,
 ok_staged ? (valid ? "stage+v_ok" : "stage_v_fail") : "stage_fail");
 _server.send(overall_ok ? 200 : 422, "application/json", buf);
 return;
 }

 uint16_t need = is_apply ? PERM_FILE_UPLOAD : PERM_FILE_READ;
 if (!(getAuthPerms( ) & need)) {
 /* The refusal is recorded here rather than at the callback that
  * enforces it, because the old hole left no trace at all and the
  * obvious place to fix that turned out to reboot the device: a
  * log write from inside the multipart callback took Core 0 past
  * the watchdog window on the 12th refusal. This is the ordinary
  * request path, and it costs nothing. */
 LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId,
          String("restore rejected: op=") + op);
 _server.send(403, "text/plain", "Forbidden");
 /* Same reason as handleApiBackup above, reached by a third path.
  * A refusal answers non-chunked and returns, so nothing in the
  * abort discipline covers its tail; the framework retires the
  * client inside handleClient( ) with a bare stop( ) whose ACK-wait
  * renews on progress and never feeds the watchdog. Measured on the
  * rig: repeating a refused restore rebooted the device on the 12th
  * and the 31st across two runs, autopsy `HW WATCHDOG C0=[WEB_POLL]`
  * (ctx=219) — the same signature, and the same cure. */
 drainOrDrop( );
 _drainPending = false;
 return;
 }
 if (is_apply && rejectIfTouchPriority( )) return;
 /* Core 1 already paused since UPLOAD_FILE_START (apply mode).
 * Defensive: if for some reason UPLOAD_FILE_END didn't run (client
 * timeout/strange abort), ensure we're protected here too.
 * Self-balanced via _restoreCorePaused flag. */
 bool need_local_pause = is_apply && !_restoreCorePaused && _displayRef;
 if (need_local_pause) {
 _displayRef->pauseRendering(true);
 _restoreCorePaused = true;
 }
 bool fs_mod = false;
 ota::restore_session_finish(_restoreSession, &fs_mod);
 if (_restoreCorePaused && _displayRef) {
 _displayRef->pauseRendering(false);
 _restoreCorePaused = false;
 }
 LOG_CODE(is_apply ? LOG_WARN : LOG_INFO, "OTA", WEB_UPLOAD,
 (int)_restoreSession.status, is_apply ? "rsta" : "rstv");
 emit_restore_json(_server, _restoreSession, fs_mod);

 /* Auto-reboot after apply OK. Without this, restore writes
 * files to LFS but runtime keeps stale caches — user needed
 * manual RESET. Same sequence as commit-all. */
 if (is_apply && fs_mod && _restoreSession.status == ota::BackupStatus::OK) {
 _server.client( ).stop( );
 if (_displayRef) {
 _displayRef->setBootStatusKey(TR_BOOT_APPLYING_CFG);
 for (int i = 0; i < 10; i++) { delay(100); watchdog_update( ); }
 }
 LogManager::instance( ).safeReboot( );
 }
}

/* ===========================================================================
 * Staging selftest (DESTRUCTIVE)
 * ===========================================================================
 *
 * GET /api/ota/staging_test
 *
 * Sequence: HeavyTaskGuard → session_begin (erases 1 MB) → selftest
 * (write/read/erase 1 4KB sector) → session_end (LittleFS reformatted).
 *
 * Client MUST call /api/backup BEFORE and /api/restore?op=apply AFTER
 * to preserve data. Without auth we get 403; without PERM_FILE_UPLOAD too.
 * Admin only.
 * ========================================================================= */
void WebManager::handleApiOtaStagingTest( ) {
 /* OTA staging selftest: DESTRUCTIVE (erases 1 MB) — ADMIN-ONLY. */
 if (getAuthPerms( ) != PERM_FULL_ADMIN) {
 _server.send(403, "text/plain", "Forbidden — admin only");
 return;
 }
 if (rejectIfTouchPriority( )) return;

 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 _server.send(503, "text/plain", "System Busy");
 return;
 }

 LOG_CODE(LOG_WARN, "OTA", SEC_CONFIG_CHANGED, _currentUserId, "stg_test_begin");

 /* RenderGuard pauses display during operation (~5-10s). */
 bool ok_begin = false, ok_test = false, ok_end = false;
 int diff = -1;
 uint32_t t0 = millis( );
 {
 RenderGuard rg(_displayRef);
 ok_begin = ota::staging_session_begin(_storageRef);
 if (ok_begin) {
 ok_test = ota::staging_selftest(&diff);
 ok_end = ota::staging_session_end(_storageRef);
 }
 }
 uint32_t dt = millis( ) - t0;

 LOG_CODE(ok_test ? LOG_INFO : LOG_ERROR, "OTA", SEC_CONFIG_CHANGED,
 _currentUserId, ok_test ? "stg_test_ok" : "stg_test_fail");

 char buf[160];
 snprintf(buf, sizeof(buf),
 "{\"ok\":%s,\"begin\":%d,\"selftest\":%d,\"end\":%d,\"first_diff\":%d,\"time_ms\":%lu}",
 (ok_begin && ok_test && ok_end) ? "true" : "false",
 ok_begin ? 1 : 0, ok_test ? 1 : 0, ok_end ? 1 : 0,
 diff, (unsigned long)dt);
 _server.send(200, "application/json", buf);
}

/* ===========================================================================
 * Apply orchestrator endpoint
 * ===========================================================================
 *
 * POST /api/ota/apply (admin / PERM_FILE_UPLOAD)
 *
 * Fires the destructive update path. In no-op mode, accepts `?test=1`
 * which injects a stub metadata (state=COMMITTED) and exercises the infra (tear down
 * → IRQ off → SRAM applier → watchdog reboot) without writing to the app slot.
 *
 * Response:
 * - 202 Accepted BEFORE tear down (client loses connection after).
 * - Client waits ~1-2s and re-requests to confirm boot.
 *
 * Without ?test=1: requires legitimate metadata.state==COMMITTED (set after
 * stage+validate OK).
 * Anti-loop via OTA_MAX_APPLY_ATTEMPTS (already in orchestrator).
 * Retry guard: rejects if another apply was triggered in last 10s.
 * ========================================================================= */
void WebManager::handleApiOtaApply( ) {
 /* OTA apply: DESTRUCTIVE IRREVERSIBLE — ADMIN-ONLY. */
 if (getAuthPerms( ) != PERM_FULL_ADMIN) {
 _server.send(403, "text/plain", "Forbidden — admin only");
 return;
 }
 if (rejectIfTouchPriority( )) return;

 bool test_mode = (_server.arg("test") == "1");

 if (test_mode) {
 /* Inject stub metadata. enterFlashSafeMode pauses Core 1
 * before flash_range_erase — without it, Core 1 (DisplayManager)
 * reads via XIP during erase and gets hard fault. */
 ota::UpdateMetadata m;
 memset(&m, 0, sizeof(m));
 m.magic = ota::OTA_MAGIC_PENDING;
 m.state = ota::STATE_COMMITTED;
 m.attempts = 0;
 bool ok;
 {
 RenderGuard rg(_displayRef);
 _storageRef->enterFlashSafeMode( );
 ok = ota::ota_metadata_write(m);
 _storageRef->exitFlashSafeMode( );
 }
 if (!ok) {
 _server.send(500, "application/json",
 "{\"error\":\"metadata write failed\"}");
 return;
 }
 } else {
 /* Real path: requires COMMITTED. */
 ota::UpdateMetadata m;
 if (!ota::ota_metadata_read(m) || m.state != ota::STATE_COMMITTED) {
 _server.send(409, "application/json",
 "{\"error\":\"no committed update pending\"}");
 return;
 }
 }

 LOG_CODE(LOG_WARN, "OTA", SEC_CONFIG_CHANGED, _currentUserId,
 test_mode ? "apply_fw test" : "apply_fw real");

 /* Respond BEFORE tearing down Wi-Fi. Client will lose connection
 * immediately after reboot. */
 _server.send(202, "application/json",
 test_mode ? "{\"accepted\":true,\"mode\":\"test\"}"
 : "{\"accepted\":true,\"mode\":\"apply\"}");
 _server.client( ).flush( );
 delay(500); /* TCP flush before WiFi.end. */

 /* Tear down + jump to SRAM applier — does not return on success. */
 auto result = ota::ota_apply_pending_update(_storageRef);

 /* Only reached on pre-destructive error (rare). No response possible
 * — Wi-Fi already torn down. Log and proceed (next loop may attempt
 * Wi-Fi recovery via reload, but state is likely bad). */
 LOG_CODE(LOG_ERROR, "OTA", SEC_CONFIG_CHANGED, _currentUserId,
 String("apply returned, result=") + (int)result);
}
