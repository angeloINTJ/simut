/**
 * @file WebManager_Files.cpp
 * @brief File operations: download, delete, ls, mkdir, upload (batch-buffered).
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include "Themes.h"
#include <LittleFS.h>

using ReadGuard = StorageManager::ReadGuard;

/* Safe filename/dirname escaping for JSON emission.
 * Covers \n/\r/\t (short escape) and filters other control bytes
 * (0x00-0x1F, 0x7F) to '?' — files with bad bytes appear
 * in /files with '?' in the name, deletable by the user, without
 * breaking the client's JSON parse. */
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
 else if (c == '\r') { dst[di++] = '\\'; dst[di++] = 'r'; }
 else if (c == '\t') { dst[di++] = '\\'; dst[di++] = 't'; }
 else if (c < 0x20 || c == 0x7F) { dst[di++] = '?'; }
 else { dst[di++] = (char)c; }
 }
 dst[di] = '\0';
}



void WebManager::handleDownload( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_FILE_READ)) { _server->send(403, "text/plain", "Forbidden"); return; }
 if (!_server->hasArg("file")) { _server->send(400, "text/plain", "Bad Request"); return; }

 String path = _server->arg("file");

 /* PERM_FILE_READ downloads history, calib, themes and language packs — never
  * the credential store. Refuse everything under /config (system.bin holds the secrets and
  * the password hashes) and reject traversal / percent-encoding, the same
  * guard handleApiLs and the upload path already apply. Without the '..'
  * check, "/history/../config/system.bin" would walk straight past the
  * prefix test. Checked before the heavy-task lock so a probe is cheap to
  * turn away. See isSecretFsPath (StorageManager.h) and finding A-4. */
 if (path.indexOf("..") >= 0 || path.indexOf('%') >= 0 || isSecretFsPath(path)) {
  LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId, String("download refused: ") + path);
  _server->send(403, "text/plain", "Forbidden");
  return;
 }

 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) { _server->send(503, "text/plain", "System Busy"); return; }


 File f;
 {
 ReadGuard rg(_storageRef);
 if (!LittleFS.exists(path)) { _server->send(404, "text/plain", "File Not Found."); return; }
 f = LittleFS.open(path, "r");
 }

 if (!f) { _server->send(500, "text/plain", "Error."); return; }

 String fileName = path.substring(path.lastIndexOf('/') + 1);
 _server->sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
 safeStreamFile(f, "application/octet-stream");
 f.close( );
 LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, String(TRL("User downloaded: ")) + fileName);
}

void WebManager::handleDelete( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_FILE_DELETE)) { _server->send(403, "text/plain", "Forbidden"); return; }
 if (!_server->hasArg("file")) { _server->send(400, "text/plain", "Bad Request"); return; }
 if (rejectIfTouchPriority( )) return;

 String path = _server->arg("file");

 /* Refused here, not only hidden in the page: /files omits the checkbox on
  * a protected row, but this handler is one POST away from anybody. */
 if (isProtectedFsPath(path)) {
 LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId, path);
 _server->send(403, "application/json",
              "{\"error\":\"Protected file — the firmware rewrites it on boot\"}");
 return;
 }

 bool existed = false;
 {
 RenderGuard rg(_displayRef);
 if (LittleFS.exists(path)) {
 existed = true;
 LittleFS.remove(path);
 LOG_CODE(LOG_WARN, "SEC", SEC_FILE_DELETE, _currentUserId, path);
 }
 }
 if (!existed) {
 /* Used to answer ok for nonexistent paths — silent no-ops cost a
  * whole forensic session ("ghost purge"). Be honest. */
 _server->send(404, "application/json", "{\"error\":\"Not found\"}");
 return;
 }
 /* External mutation of the live history file: without invalidating,
  * the writer appends to a recreated HEADERLESS file until reboot. */
 if (path.startsWith("/history/") &&
     path.endsWith(HISTORY_FILE_EXT)) {
 _storageRef->invalidateHistoryCodec( );
 }
 /* Hot-reload custom theme on delete (same pattern as upload) —
 * avoids residue in in-memory list after user removes a .thm. */
 if (path.endsWith(".thm")) {
 scanCustomThemes( );
 }
 _server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleApiLs( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_FILE_READ)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }
 if (isRateLimited(200)) { _server->send(429, "application/json", "{\"error\":\"Too Fast\"}"); return; }

 String dirPath = "/";
 if (_server->hasArg("dir")) {
 dirPath = _server->arg("dir");
 dirPath.trim( );
 if (dirPath.length( ) == 0) dirPath = "/";

 /* Reject early instead of O(n^2) loop with replace.
 * Adversarial input like "...." or "//../..//a" would cause multiple
 * replace iterations. Single-pass: if it contains ".." → immediate 400.
 * "//" collapsed in single-pass. */
 if (dirPath.indexOf("..") >= 0 || dirPath.indexOf('%') >= 0) {
 _server->send(400, "application/json", "{\"error\":\"invalid path\"}");
 return;
 }
 /* Collapse // → / in single-pass O(n). */
 {
 String collapsed; collapsed.reserve(dirPath.length( ));
 char prev = 0;
 for (size_t i = 0; i < dirPath.length( ); i++) {
 char c = dirPath[i];
 if (c == '/' && prev == '/') continue;
 collapsed += c;
 prev = c;
 }
 dirPath = collapsed;
 }
 if (!dirPath.startsWith("/")) dirPath = "/" + dirPath;
 while (dirPath.length( ) > 1 && dirPath.endsWith("/")) {
 dirPath = dirPath.substring(0, dirPath.length( ) - 1);
 }

 /* Removed strict allowlist (/history,/config,/lang,/themes).
 * `..` + `%` already rejected above — any other path is legitimate
 * navigation. Allows UI to list custom dirs created via mkdir
 * (case where upload to /test was invisible for delete). */
 }


 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) { _server->send(503, "application/json", "{\"error\":\"System Busy\"}"); return; }

 /* Listing a big directory belongs with the other long handlers: /history
  * holds one file per day, and every entry costs a flash read plus a chunked
  * write, which contends with telemetry for the same flash lock. At the
  * default 6 s budget an archive of ~90 days was being cut off mid-scan. */
 const uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;

 _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
 _server->send(200, "application/json", "");

 char buf[256];
 snprintf(buf, sizeof(buf), "{\"path\":\"%s\",\"entries\":[", dirPath.c_str( ));
 if (!safeSend(buf)) { _handlerDeadline = savedDeadline; return; }

 bool first = true;

 /* Dirs at root are enumerated by the main loop below
 * (no longer via hardcoded sysDirs). README.md placeholder in each
 * folder ensures persistence even when empty (LittleFS loses dirs
 * without entries). Allows showing custom folders in the UI. */

 bool dirDone = false;

 Dir dir;
 {
 ReadGuard rg(_storageRef);
 dir = LittleFS.openDir(dirPath);
 }

 /* Set when the enumeration is cut short, and reported in the body. A caller
  * that cannot tell a partial listing from a complete one will treat missing
  * files as deleted: two listings of the same /history minutes apart came
  * back with 84 entries each and DIFFERENT contents, and both looked like
  * well-formed, finished JSON. */
 bool truncated = false;

 while (!dirDone) {
 if (isHandlerOvertime( )) { truncated = true; break; }

 struct DirEntry { String name; size_t size; bool isDir; };
 DirEntry batch[20];
 int batchCount = 0;

 {
 ReadGuard rg(_storageRef);
 /* Count first, THEN advance. Written the other way round —
  * `dir.next( ) && batchCount < 20` — the iterator moves before the count is
  * tested, so the entry that would have filled the batch is consumed and
  * never recorded: every full batch of 20 silently lost one file. Measured:
  * 88 files on flash, 84 in the listing, and because LittleFS iteration
  * order varies, two listings minutes apart dropped DIFFERENT files
  * (20260523 vs 20260524 — both present, both intact, neither ever shown
  * together). Nothing in the response said anything was missing. */
 while (batchCount < 20 && dir.next( )) {
 /* feedWdt( ) under the read lock — feedWatchdog( ) would run the light yield,
  * which reaches enterFlashReadLock( ) on this same core and self-deadlocks on
  * a non-recursive mutex. See the note in handleApiHistoryDays. */
 feedWdt( );
 batch[batchCount].isDir = dir.isDirectory( );
 batch[batchCount].name = dir.fileName( );
 batch[batchCount].size = dir.isDirectory( ) ? 0 : dir.fileSize( );
 batchCount++;
 }
 dirDone = (batchCount < 20);
 }

 for (int i = 0; i < batchCount; i++) {
 if (batch[i].isDir) {
 /* Dirs emitted even in "/" — previously skipped at root
 * because sysDirs came from a hardcoded list. Now any
 * folder created via mkdir appears in UI (and is deletable). */
 const char* dName = batch[i].name.c_str( );
 if (dName[0] == '\0') continue;
 if (strcmp(dName, ".") == 0 || strcmp(dName, "..") == 0) continue;
 /* Escape dirname. */
 char dEscaped[96];
 jsonEscapeFilename(dName, dEscaped, sizeof(dEscaped));
 snprintf(buf, sizeof(buf), "%s{\"n\":\"%s\",\"t\":\"d\",\"s\":0}",
 first ? "" : ",", dEscaped);
 if (!safeSend(buf)) { _handlerDeadline = savedDeadline; return; }
 first = false;
 continue;
 }

 const String& fnStr = batch[i].name;
 if (fnStr.length( ) == 0) continue;

 /* Escape filename. Control bytes become '?' — file remains
 * visible in /files and deletable, without breaking
 * the client's JSON. */
 char escaped[128];
 jsonEscapeFilename(fnStr.c_str( ), escaped, sizeof(escaped));

 /* "p":1 marks a file the page must render without a selection checkbox.
  * The flag is emitted rather than the page hardcoding a name, so the two
  * sides cannot disagree about what is protected. */
 String full = (dirPath == "/") ? ("/" + fnStr) : (dirPath + "/" + fnStr);
 bool prot = isProtectedFsPath(full);

 snprintf(buf, sizeof(buf), "%s{\"n\":\"%s\",\"t\":\"f\",\"s\":%u%s}",
 first ? "" : ",", escaped, (unsigned)batch[i].size,
 prot ? ",\"p\":1" : "");
 if (!safeSend(buf)) { _handlerDeadline = savedDeadline; return; }
 first = false;
 }

 feedWatchdog( );
 }

 safeSend(truncated ? "],\"truncated\":true}" : "]}");
 _handlerDeadline = savedDeadline;
}

void WebManager::handleApiMkdir( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_FILE_UPLOAD)) { _server->send(403, "text/plain", "Forbidden"); return; }
 if (!_server->hasArg("dir")) { _server->send(400, "text/plain", "Missing dir"); return; }
 if (rejectIfTouchPriority( )) return;

 String dirPath = _server->arg("dir");
 dirPath.trim( );
 /* Allowlist validation at the source (M-7): a folder name is echoed into the
  * /files listing, so a name like "<img src=x onerror=...>" would be stored
  * XSS. isSafeDirPath refuses the HTML/JS-hostile bytes and ".." outright —
  * the old `replace("..","")` here was the non-recursive kind the upload path
  * documents as broken ("...." → ".."). */
 if (!isSafeDirPath(dirPath.c_str( ))) {
 LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId, String("mkdir rejected: ") + dirPath);
 _server->send(400, "text/plain", "Invalid path");
 return;
 }
 if (!dirPath.startsWith("/")) dirPath = "/" + dirPath;

 int slashCount = 0;
 for (size_t i = 0; i < dirPath.length( ); i++) {
 if (dirPath[i] == '/') slashCount++;
 }
 if (slashCount > 2) { _server->send(400, "text/plain", "Max depth exceeded"); return; }

 bool ok;
 bool alreadyExisted = LittleFS.exists(dirPath);
 {
 RenderGuard rg(_displayRef);
 ok = alreadyExisted ? true : LittleFS.mkdir(dirPath);
 }

 if (ok) {
 if (!alreadyExisted) {
 /* README.md placeholder ensures empty folder persists
 * in LittleFS (without entries, dir becomes invisible orphan metadata). */
 String readmePath = dirPath + "/" FS_DIR_NOTE_NAME;
 if (!LittleFS.exists(readmePath)) {
 RenderGuard rg(_displayRef);
 File rf = LittleFS.open(readmePath, "w");
 if (rf) {
 rf.print("Pasta SIMUT. Mantém esta entrada para preservar a pasta.\n");
 rf.close( );
 }
 }
 LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, String(TRL("Created folder: ")) + dirPath);
 }
 _server->send(200, "application/json", "{\"status\":\"ok\"}");
 } else {
 _server->send(500, "application/json", "{\"error\":\"Failed\"}");
 }
}

void WebManager::handleUploadComplete( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_FILE_UPLOAD)) { _server->send(403, "text/plain", "Forbidden"); return; }
 /* If START marked rejection (invalid name, bad uploadDir,
 * no space), respond 400 here — the response cannot be sent from inside
 * the Arduino WebServer upload handler. */
 if (_uploadRejected) {
 _uploadRejected = false;
 _server->send(400, "application/json", "{\"error\":\"Invalid upload\"}");
 return;
 }
 _storageRef->invalidateOldestFileCache( ); /* Restored file may be older */
 _server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleUploadData( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_FILE_UPLOAD)) return;

 HTTPUpload& upload = _server->upload( );

 if (upload.status == UPLOAD_FILE_START) {
 /* Reset rejection state (new upload). */
 _uploadRejected = false;
 _uploadBatchLen = 0;

 /* Filename sanitization BEFORE any use.
 * upload.filename comes directly from the HTTP multipart client — treat as hostile. */
 if (!isSafeUploadFilename(upload.filename.c_str( ))) {
 LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId,
 String("Upload rejected: invalid filename '") + upload.filename + "'");
 _uploadRejected = true;
 return;
 }

 String filename = upload.filename;
 if (!filename.startsWith("/")) filename = "/" + filename;

 /* Validate free space before accepting upload */
 {
 FSInfo fsi;
 _storageRef->enterFlashReadLock( );
 LittleFS.info(fsi);
 _storageRef->exitFlashReadLock( );
 uint32_t freeBytes = fsi.totalBytes - fsi.usedBytes;
 if (_server->hasHeader("Content-Length")) {
 uint32_t cl = _server->header("Content-Length").toInt( );
 if (cl > freeBytes) {
 LOG_CODE(LOG_WARN, "WEB", WEB_UPLOAD, (int)cl, "Upload rejected: no space");
 /* Cannot send 413 from here (upload handler). Mark rejection
 * — handleUploadComplete returns 400 (generic). */
 _uploadRejected = true;
 return;
 }
 }
 }

 String targetDir = "/";
 if (_server->hasArg("uploadDir")) {
 targetDir = _server->arg("uploadDir");
 targetDir.trim( );

 /* Reject instead of trying to clean.
 * `String::replace("..","")` is non-recursive — `"...."` passes as `".."`
 * after one pass, and percent-encoded variants (`%2e%2e`) also
 * escape. Reject literal `..` and `%` (which enables encoding).
 * Legitimate paths never contain either. */
 if (targetDir.indexOf("..") >= 0 || targetDir.indexOf('%') >= 0) {
 LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId,
 String("uploadDir rejected: ") + targetDir);
 _uploadRejected = true;
 return;
 }

 if (!targetDir.startsWith("/")) targetDir = "/" + targetDir;
 while (targetDir.length( ) > 1 && targetDir.endsWith("/")) {
 targetDir = targetDir.substring(0, targetDir.length( ) - 1);
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
 _uploadPath = finalPath;
 { RenderGuard rg(_displayRef); _uploadFile = LittleFS.open(finalPath, "w"); }

 } else if (upload.status == UPLOAD_FILE_WRITE) {
 if (_uploadRejected) return;
 if (_uploadFile) {
 /* Accumulate chunks in _uploadBatchBuf.
 * Flush (with RenderGuard) only every 8 KB — reduces Core 1 pauses
 * from ~1 per HTTP chunk to ~1 every 8 KB. */
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
 _flushUploadBatch( );
 feedWatchdog( );
 }
 }
 }
 } else if (upload.status == UPLOAD_FILE_END) {
 if (_uploadRejected) return;
 if (_uploadFile) {
 _flushUploadBatch( ); /* Final flush of remaining bytes. */
 { RenderGuard rg(_displayRef); _uploadFile.close( ); }

 if (upload.filename == "calib.csv" || upload.filename == "/calib.csv") {
 if (_storageRef->processCalibrationUpload( )) {
 LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Universal Calibration Updated."));
 }
 } else {
 LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("File Uploaded."));
 /* Hot-reload custom theme — re-scan if .thm uploaded
 * to any path (Files UI allows uploading to subdir).
 * New theme appears immediately in getThemeCount( ) and in the
 * selection list without reboot. */
 if (upload.filename.endsWith(".thm")) {
 scanCustomThemes( );
 LOG_CODE(LOG_INFO, "CFG", CFG_THEME_APPLIED, getThemeCount( ), TRL("Custom themes rescanned"));
 }
 }
 }
 _uploadPath = "";
 } else if (upload.status == UPLOAD_FILE_ABORTED) {
 /* Connection dropped mid-upload. Without this branch the File handle stayed
  * open and the partial file survived on flash — for calib.csv that meant an
  * orphan /calib.tmp that no boot path ever collected. The buffered tail is
  * dropped unflushed: the file is incomplete either way. */
 if (_uploadRejected) { _uploadPath = ""; return; }
 _uploadBatchLen = 0;
 if (_uploadFile) { RenderGuard rg(_displayRef); _uploadFile.close( ); }
 if (_uploadPath.length( ) > 0) {
 { RenderGuard rg(_displayRef); LittleFS.remove(_uploadPath); }
 LOG_CODE(LOG_WARN, "WEB", WEB_UPLOAD, 0, String("upload aborted, discarded ") + _uploadPath);
 _uploadPath = "";
 }
 }
}

void WebManager::_flushUploadBatch( ) {
 if (_uploadBatchLen > 0 && _uploadFile) {
 { RenderGuard rg(_displayRef); _uploadFile.write(_uploadBatchBuf, _uploadBatchLen); }
 _uploadBatchLen = 0;
 }
}
