/**
 * @file    WebManager_Ota.cpp
 * @brief   Endpoints OTA — Fase 1: GET /api/backup (download .bkp).
 *
 * @details Pipeline: auth → HeavyTaskGuard → scan (CRC32 + size) →
 *          headers (Content-Length, Content-Disposition com chip_id+timestamp)
 *          → emit (header + payload via Print adapter sobre safeSend).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
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
#include <Print.h>
#include <time.h>

/* Adapter Print → WebManager::safeSend (declarado friend em WebManager.h). */
struct OtaBackupPrintAdapter : public Print {
    WebManager* w;
    bool        ok;
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

void WebManager::handleApiBackup() {
    /* Permissão: backup tem o mesmo nível de risco de leitura de todos os
     * arquivos da LittleFS, então casa com PERM_FILE_READ. */
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_READ)) {
        _server.send(403, "text/plain", "Forbidden");
        return;
    }

    /* Touch priority: respeita uso físico do display (operação longa). */
    if (rejectIfTouchPriority()) return;

    /* Serializa contra outros heavy tasks (uploads, exports, etc.). */
    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) {
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
        chipHex[i * 2]     = H[(chip[i] >> 4) & 0xF];
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
        /* Headers já foram enviados — só dá pra logar. O cliente vai detectar
         * truncamento via Content-Length mismatch ou CRC inválido. */
        LOG_CODE(LOG_WARN, "OTA", WEB_DISCONNECT_FILE, _currentUserId, "backup_emit");
        return;
    }

    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId,
             String("backup ") + scan.file_count + "f " + scan.payload_size + "B");
}

/* ===========================================================================
 * Fase 2 — Validação e Restore
 *
 * Pipeline: HTTP upload (multipart) → handleApiRestoreUploadData (callback) →
 * handleApiRestoreValidate ou handleApiRestoreApply (final). O callback é
 * compartilhado: a URI da rota decide o modo (VALIDATE vs APPLY).
 * ========================================================================= */

void WebManager::handleApiRestoreUploadData() {
    HTTPUpload& upload = _server.upload();
    bool is_stage = (_server.arg("op") == "stage");
    if (upload.status == UPLOAD_FILE_START) {
        if (is_stage) {
            /* Pre-check perm: sem ela, não desmonta LFS. _stageSession.status
             * fica IDLE; feed/end ignoram silenciosamente; finish responde 403. */
            if (getAuthPerms() & PERM_FILE_UPLOAD) {
                ota::stage_session_begin(_stageSession, _storageRef);
            } else {
                _stageSession.status = ota::StageStatus::IDLE;
            }
        } else {
            ota::RestoreMode mode = (_server.arg("op") == "apply")
                ? ota::RestoreMode::APPLY : ota::RestoreMode::VALIDATE;
            ota::restore_session_begin(_restoreSession, mode);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (is_stage) {
            ota::stage_session_feed(_stageSession, upload.buf, upload.currentSize);
        } else {
            ota::restore_session_feed(_restoreSession, upload.buf, upload.currentSize);
        }
        feedWatchdog();
    } else if (upload.status == UPLOAD_FILE_END) {
        /* Stage precisa finalize explícito (pad da última página + xor-out CRC).
         * Restore não tem finalize separado — o finish handler já gerencia tudo. */
        if (is_stage) {
            ota::stage_session_end(_stageSession);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (is_stage) {
            ota::stage_session_abort(_stageSession);
        } else {
            ota::restore_session_abort(_restoreSession);
        }
    }
}

/* Emite JSON do estado de restore. Códigos numéricos do enum BackupStatus
 * são interpretados pelo cliente (mapeia status → mensagem). */
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
            chip_hex[i*2]     = (char)(hi < 10 ? '0' + hi : 'a' + hi - 10);
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

void WebManager::handleApiRestoreFinish() {
    String op = _server.arg("op");
    bool is_stage = (op == "stage");
    bool is_apply = (op == "apply");

    if (is_stage) {
        if (!(getAuthPerms() & PERM_FILE_UPLOAD)) {
            _server.send(403, "text/plain", "Forbidden");
            return;
        }
        bool ok_staged = (_stageSession.status == ota::StageStatus::STAGED);

        /* Fase 6: dry-run validate ANTES de remontar LFS — depois do
         * remount o LittleFS reformata a área e o conteúdo do staging
         * vira lixo. Validação acessa staging via XIP read. */
        ota::ValidationReport vr;
        memset(&vr, 0, sizeof(vr));
        bool valid = false;
        if (ok_staged) {
            valid = ota::ota_validate_staging(_stageSession, vr);
        }

        /* Sempre remonta/restaura LFS pra deixar sistema utilizável.
         * Fase 5/6 não persiste staging — Fase 7 (apply) vai pular este
         * remount e ir direto pro applier. */
        if (ok_staged) {
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
                "\"v\":%u,\"dsize\":%lu,\"dcrc\":\"%08lX\"}",
                (unsigned)_stageSession.status,
                (unsigned long)_stageSession.bytes_written,
                (unsigned long)_stageSession.crc32_running,
                (unsigned)vr.status,
                (unsigned long)vr.decompressed_size,
                (unsigned long)vr.decompressed_crc);
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
    if (!(getAuthPerms() & need)) {
        _server.send(403, "text/plain", "Forbidden");
        return;
    }
    if (is_apply && rejectIfTouchPriority()) return;
    bool fs_mod = false;
    {
        RenderGuard rg(is_apply ? _displayRef : nullptr);
        ota::restore_session_finish(_restoreSession, &fs_mod);
    }
    LOG_CODE(is_apply ? LOG_WARN : LOG_INFO, "OTA", WEB_UPLOAD,
             (int)_restoreSession.status, is_apply ? "rsta" : "rstv");
    emit_restore_json(_server, _restoreSession, fs_mod);
}

/* ===========================================================================
 * Fase 4 — Staging selftest (DESTRUTIVO)
 * ===========================================================================
 *
 * GET /api/ota/staging_test
 *
 * Sequência: HeavyTaskGuard → session_begin (apaga 1 MB) → selftest
 * (escreve/lê/apaga 1 setor de 4KB) → session_end (LittleFS reformatada).
 *
 * Cliente DEVE chamar /api/backup ANTES e /api/restore?op=apply DEPOIS
 * para preservar dados. Sem auth temos 403; sem PERM_FILE_UPLOAD também.
 * Só admin pode rodar.
 * ========================================================================= */
void WebManager::handleApiOtaStagingTest() {
    if (!(getAuthPerms() & PERM_FILE_UPLOAD)) {
        _server.send(403, "text/plain", "Forbidden");
        return;
    }
    if (rejectIfTouchPriority()) return;

    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) {
        _server.send(503, "text/plain", "System Busy");
        return;
    }

    LOG_CODE(LOG_WARN, "OTA", SEC_CONFIG_CHANGED, _currentUserId, "stg_test_begin");

    /* RenderGuard pausa display durante operação (~5-10s). */
    bool ok_begin = false, ok_test = false, ok_end = false;
    int  diff = -1;
    uint32_t t0 = millis();
    {
        RenderGuard rg(_displayRef);
        ok_begin = ota::staging_session_begin(_storageRef);
        if (ok_begin) {
            ok_test = ota::staging_selftest(&diff);
            ok_end = ota::staging_session_end(_storageRef);
        }
    }
    uint32_t dt = millis() - t0;

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
