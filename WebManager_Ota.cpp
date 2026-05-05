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
    if (upload.status == UPLOAD_FILE_START) {
        ota::RestoreMode mode = (_server.arg("op") == "apply")
            ? ota::RestoreMode::APPLY : ota::RestoreMode::VALIDATE;
        ota::restore_session_begin(_restoreSession, mode);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        ota::restore_session_feed(_restoreSession, upload.buf, upload.currentSize);
        feedWatchdog();
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        ota::restore_session_abort(_restoreSession);
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
    bool is_apply = (_server.arg("op") == "apply");
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
