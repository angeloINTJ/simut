/**
 * @file    WebManager_Files.cpp
 * @brief   File operations: download, delete, ls, mkdir, upload (batch-buffered).
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include <LittleFS.h>

using ReadGuard = StorageManager::ReadGuard;

/* WEB-001: escape seguro de filename/dirname para emissão em JSON.
 * Cobre \n/\r/\t (escape curto) e filtra outros bytes de controle
 * (0x00-0x1F, 0x7F) para '?' — arquivos com bytes ruins ficam visíveis
 * no /files com '?' no nome, podendo ser deletados pelo user, sem
 * quebrar o parse JSON do cliente. */
static void jsonEscapeFilename(const char* src, char* dst, size_t dstSize) {
    if (!src || !dst || dstSize == 0) {
        if (dst && dstSize) dst[0] = '\0';
        return;
    }
    size_t di = 0;
    while (*src && di + 2 < dstSize) {
        unsigned char c = (unsigned char)*src++;
        if (c == '"' || c == '\\') {
            dst[di++] = '\\';
            dst[di++] = (char)c;
        } else if (c == '\n') { dst[di++] = '\\'; dst[di++] = 'n'; }
        else   if (c == '\r') { dst[di++] = '\\'; dst[di++] = 'r'; }
        else   if (c == '\t') { dst[di++] = '\\'; dst[di++] = 't'; }
        else   if (c < 0x20 || c == 0x7F) { dst[di++] = '?'; }
        else   { dst[di++] = (char)c; }
    }
    dst[di] = '\0';
}



void WebManager::handleDownload() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_READ)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (!_server.hasArg("file")) { _server.send(400, "text/plain", "Bad Request"); return; }

    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) { _server.send(503, "text/plain", "System Busy"); return; }

    String path = _server.arg("file");


    File f;
    {
        ReadGuard rg(_storageRef);
        if (!LittleFS.exists(path)) { _server.send(404, "text/plain", "File Not Found."); return; }
        f = LittleFS.open(path, "r");
    }

    if (!f) { _server.send(500, "text/plain", "Error."); return; }

    String fileName = path.substring(path.lastIndexOf('/') + 1);
    _server.sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
    safeStreamFile(f, "application/octet-stream");
    f.close();
    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, String(TRL("User downloaded: ", "Usuario baixou: ")) + fileName);
}

void WebManager::handleDelete() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_DELETE)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (!_server.hasArg("file")) { _server.send(400, "text/plain", "Bad Request"); return; }
    if (rejectIfTouchPriority()) return;

    String path = _server.arg("file");

    {
        RenderGuard rg(_displayRef);
        if (LittleFS.exists(path)) {
            LittleFS.remove(path);
            LOG_CODE(LOG_WARN, "SEC", SEC_FILE_DELETE, _currentUserId, path);
        }
    }
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleApiLs() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_READ)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }
    if (isRateLimited(200)) { _server.send(429, "application/json", "{\"error\":\"Too Fast\"}"); return; }

    String dirPath = "/";
    if (_server.hasArg("dir")) {
        dirPath = _server.arg("dir");
        dirPath.trim();
        if (dirPath.length() == 0) dirPath = "/";

        while (dirPath.indexOf("..") >= 0) dirPath.replace("..", "");
        while (dirPath.indexOf("//") >= 0) dirPath.replace("//", "/");
        if (!dirPath.startsWith("/")) dirPath = "/" + dirPath;
        while (dirPath.length() > 1 && dirPath.endsWith("/")) {
            dirPath = dirPath.substring(0, dirPath.length() - 1);
        }

        if (dirPath != "/" && !dirPath.startsWith("/history") && !dirPath.startsWith("/config")) {
            _server.send(403, "application/json", "{\"error\":\"Forbidden path\"}");
            return;
        }
    }


    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) { _server.send(503, "application/json", "{\"error\":\"System Busy\"}"); return; }

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    char buf[256];
    snprintf(buf, sizeof(buf), "{\"path\":\"%s\",\"entries\":[", dirPath.c_str());
    if (!safeSend(buf)) return;

    bool first = true;

    if (dirPath == "/") {
        const char* sysDirs[] = {"/config", "/history"};
        for (auto sd : sysDirs) {
            feedWatchdog();

            bool hasContent;
            {
                ReadGuard rg(_storageRef);
                Dir testDir = LittleFS.openDir(sd);
                hasContent = testDir.next();
                if (!hasContent) hasContent = LittleFS.exists(String(sd) + "/");
            }
            if (hasContent) {
                snprintf(buf, sizeof(buf), "%s{\"n\":\"%s\",\"t\":\"d\",\"s\":0}",
                         first ? "" : ",", sd + 1);
                if (!safeSend(buf)) return;
                first = false;
            }
        }
    }

    bool dirDone = false;

    Dir dir;
    {
        ReadGuard rg(_storageRef);
        dir = LittleFS.openDir(dirPath);
    }

    while (!dirDone) {
        if (isHandlerOvertime()) break;

        struct DirEntry { String name; size_t size; bool isDir; };
        DirEntry batch[20];
        int batchCount = 0;

        {
            ReadGuard rg(_storageRef);
            while (dir.next() && batchCount < 20) {
                feedWatchdog();
                batch[batchCount].isDir = dir.isDirectory();
                batch[batchCount].name = dir.fileName();
                batch[batchCount].size = dir.isDirectory() ? 0 : dir.fileSize();
                batchCount++;
            }
            dirDone = (batchCount < 20);
        }

        for (int i = 0; i < batchCount; i++) {
            if (batch[i].isDir) {
                if (dirPath == "/") continue;
                const char* dName = batch[i].name.c_str();
                if (dName[0] == '\0') continue;
                if (strcmp(dName, ".") == 0 || strcmp(dName, "..") == 0) continue;
                /* WEB-001: escape dirname (antes era emitido cru). */
                char dEscaped[96];
                jsonEscapeFilename(dName, dEscaped, sizeof(dEscaped));
                snprintf(buf, sizeof(buf), "%s{\"n\":\"%s\",\"t\":\"d\",\"s\":0}",
                         first ? "" : ",", dEscaped);
                if (!safeSend(buf)) return;
                first = false;
                continue;
            }

            const String& fnStr = batch[i].name;
            if (fnStr.length() == 0) continue;

            /* WEB-001: escape filename (antes cobria só \ e "). Bytes de
             * controle viram '?' — arquivo fica visível no /files e
             * deletável, sem quebrar JSON do cliente. */
            char escaped[128];
            jsonEscapeFilename(fnStr.c_str(), escaped, sizeof(escaped));

            snprintf(buf, sizeof(buf), "%s{\"n\":\"%s\",\"t\":\"f\",\"s\":%u}",
                     first ? "" : ",", escaped, (unsigned)batch[i].size);
            if (!safeSend(buf)) return;
            first = false;
        }

        feedWatchdog();
    }

    safeSend("]}");
}

void WebManager::handleApiMkdir() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_UPLOAD)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (!_server.hasArg("dir")) { _server.send(400, "text/plain", "Missing dir"); return; }
    if (rejectIfTouchPriority()) return;

    String dirPath = _server.arg("dir");
    dirPath.trim();
    dirPath.replace("..", "");
    if (!dirPath.startsWith("/")) dirPath = "/" + dirPath;

    int slashCount = 0;
    for (size_t i = 0; i < dirPath.length(); i++) {
        if (dirPath[i] == '/') slashCount++;
    }
    if (slashCount > 2) { _server.send(400, "text/plain", "Max depth exceeded"); return; }

    bool ok;
    {
        RenderGuard rg(_displayRef);
        ok = LittleFS.mkdir(dirPath);
    }

    if (ok) {
        LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, String(TRL("Created folder: ", "Pasta criada: ")) + dirPath);
        _server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        _server.send(500, "application/json", "{\"error\":\"Failed\"}");
    }
}

void WebManager::handleUploadComplete() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_UPLOAD)) { _server.send(403, "text/plain", "Forbidden"); return; }
    /* SEC-001/F12.1: se o START marcou rejeição (nome inválido, uploadDir ruim,
     * sem espaço), responde 400 aqui — a resposta não pode ser enviada de dentro
     * do upload handler do Arduino WebServer. */
    if (_uploadRejected) {
        _uploadRejected = false;
        _server.send(400, "application/json", "{\"error\":\"Invalid upload\"}");
        return;
    }
    _storageRef->invalidateOldestFileCache();  /* U7: arquivo restaurado pode ser mais antigo */
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleUploadData() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_UPLOAD)) return;

    HTTPUpload& upload = _server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        /* SEC-001/F12.1: reset de estado de rejeição (novo upload). */
        _uploadRejected = false;
        _uploadBatchLen = 0;

        /* SEC-001/F12.1: sanitização do filename ANTES de qualquer uso.
         * upload.filename vem direto do cliente multipart HTTP — trata como hostil. */
        if (!isSafeUploadFilename(upload.filename.c_str())) {
            LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId,
                     String("Upload rejeitado: filename invalido '") + upload.filename + "'");
            _uploadRejected = true;
            return;
        }

        String filename = upload.filename;
        if (!filename.startsWith("/")) filename = "/" + filename;

        /* D14: validar espaço livre antes de aceitar upload */
        {
            FSInfo fsi;
            _storageRef->enterFlashReadLock();
            LittleFS.info(fsi);
            _storageRef->exitFlashReadLock();
            uint32_t freeBytes = fsi.totalBytes - fsi.usedBytes;
            if (_server.hasHeader("Content-Length")) {
                uint32_t cl = _server.header("Content-Length").toInt();
                if (cl > freeBytes) {
                    LOG_CODE(LOG_WARN, "WEB", WEB_UPLOAD, (int)cl, "Upload rejected: no space");
                    /* Não podemos enviar 413 daqui (upload handler). Marca rejeição
                     * — handleUploadComplete retorna 400 (genérico). */
                    _uploadRejected = true;
                    return;
                }
            }
        }

        String targetDir = "/";
        if (_server.hasArg("uploadDir")) {
            targetDir = _server.arg("uploadDir");
            targetDir.trim();

            /* SEC-002/F12.2: rejeita em vez de tentar limpar.
             * `String::replace("..","")` é não-recursivo — `"...."` passa a `".."`
             * após uma passada, e variantes percent-encoded (`%2e%2e`) também
             * escapam. Rejeita literal `..` e `%` (que habilita encoding).
             * Paths legítimos nunca contêm nenhum dos dois. */
            if (targetDir.indexOf("..") >= 0 || targetDir.indexOf('%') >= 0) {
                LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId,
                         String("uploadDir rejeitado: ") + targetDir);
                _uploadRejected = true;
                return;
            }

            if (!targetDir.startsWith("/")) targetDir = "/" + targetDir;
            while (targetDir.length() > 1 && targetDir.endsWith("/")) {
                targetDir = targetDir.substring(0, targetDir.length() - 1);
            }
        }

        String finalPath;
        if (targetDir == "/") {
            finalPath = filename;
        } else {
            String baseName = filename.substring(filename.lastIndexOf('/') + 1);
            finalPath = targetDir + "/" + baseName;
        }

        if (finalPath == "/calib.csv") finalPath = "/calib.tmp";

        LOG_CODE(LOG_INFO, "WEB", WEB_UPLOAD, 0, finalPath);
        LOG_CODE(LOG_INFO, "SEC", SEC_FILE_UPLOAD, _currentUserId, finalPath);
        { RenderGuard rg(_displayRef); _uploadFile = LittleFS.open(finalPath, "w"); }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_uploadRejected) return;
        if (_uploadFile) {
            /* PER-002: acumula chunks em _uploadBatchBuf.
             * Flush (com RenderGuard) só a cada 8 KB — reduz pauses do Core 1
             * de ~1 por chunk HTTP para ~1 a cada 8 KB. */
            size_t remaining = upload.currentSize;
            const uint8_t* src = upload.buf;
            while (remaining > 0) {
                uint16_t space = sizeof(_uploadBatchBuf) - _uploadBatchLen;
                uint16_t take = (remaining <= space) ? (uint16_t)remaining : space;
                memcpy(_uploadBatchBuf + _uploadBatchLen, src, take);
                _uploadBatchLen += take;
                src += take;
                remaining -= take;
                if (_uploadBatchLen >= sizeof(_uploadBatchBuf)) {
                    _flushUploadBatch();
                    feedWatchdog();
                }
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (_uploadRejected) return;
        if (_uploadFile) {
            _flushUploadBatch();  /* PER-002: flush final dos bytes restantes. */
            { RenderGuard rg(_displayRef); _uploadFile.close(); }

            if (upload.filename == "calib.csv" || upload.filename == "/calib.csv") {
                if (_storageRef->processCalibrationUpload()) {
                    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Universal Calibration Updated.", "Calibracao universal atualizada."));
                }
            } else {
                LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("File Uploaded.", "Arquivo enviado."));
            }
        }
    }
}

void WebManager::_flushUploadBatch() {
    if (_uploadBatchLen > 0 && _uploadFile) {
        { RenderGuard rg(_displayRef); _uploadFile.write(_uploadBatchBuf, _uploadBatchLen); }
        _uploadBatchLen = 0;
    }
}
