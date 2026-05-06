/**
 * @file    WebManager_Util.cpp
 * @brief   Utility functions: crypto tokens, file names, hex conversion, secure compare.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "WebManager.h"
#include "WebUI_FS.h"
#include "LogManager.h"   /* feedWdt() */
#include <LittleFS.h>     /* v3.34.0: favicon servido do FS */
#include <bearssl/bearssl_hash.h>

using ReadGuard = StorageManager::ReadGuard;

String WebManager::getDynamicExpectedHash(String username) {
    String capUser = username;
    if (capUser.length() > 0) {
        capUser.toLowerCase();
        capUser[0] = toupper(capUser[0]);
    }

    time_t now = _netRef->getEpoch();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char dateBuf[16];
    snprintf(dateBuf, sizeof(dateBuf), "%02d%02d%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);


    String rawPass = capUser + "@" + String(dateBuf);

    br_sha256_context ctx;
    br_sha256_init(&ctx);
    br_sha256_update(&ctx, rawPass.c_str(), rawPass.length());
    unsigned char hash[32];
    br_sha256_out(&ctx, hash);

    char hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
    }

    return String(hex);
}

String WebManager::generateSecureToken() {


    uint32_t entropy[4];
    entropy[0] = rp2040.hwrand32();
    entropy[1] = rp2040.hwrand32();
    entropy[2] = rp2040.hwrand32();
    entropy[3] = rp2040.hwrand32();


    br_sha256_context ctx;
    br_sha256_init(&ctx);


    br_sha256_update(&ctx, &entropy, sizeof(entropy));

    unsigned char hash[32];
    br_sha256_out(&ctx, hash);


    char hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(hex + (i * 2), 3, "%02x", hash[i]);
    }
    hex[32] = '\0';

    return String(hex);
}
String WebManager::getHistoryFileName(time_t date) {
    struct tm timeinfo; localtime_r(&date, &timeinfo);
    char buff[40]; snprintf(buff, sizeof(buff), "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return String(buff);
}

const char* WebManager::getHistoryFileNameC(time_t date) {
    struct tm timeinfo; localtime_r(&date, &timeinfo);
    snprintf(_historyFnBuf, sizeof(_historyFnBuf), "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return _historyFnBuf;
}

/* extractCsvToken removida — não é necessária com formato binário */

String WebManager::rgb565ToHex(uint16_t color) {
    uint8_t r = (color >> 11) * 255 / 31;
    uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (color & 0x1F) * 255 / 31;
    char hex[8]; snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
    return String(hex);
}

/* Helper member: abre + stream de gz de LittleFS com headers já enviados.
 * Reusa safeSend interno (private OK porque é membro). */
void WebManager::servePageFromFS(const char* path, const char* ctype,
                                 const char* cache_ctrl) {
    File f;
    { StorageManager::ReadGuard rg(_storageRef); f = LittleFS.open(path, "r"); }
    if (!f) {
        _server.send(503, "text/plain", "WebUI asset missing — run uploadfs");
        return;
    }
    if (cache_ctrl) _server.sendHeader("Cache-Control", cache_ctrl);
    _server.sendHeader("Content-Encoding", "gzip");
    _server.setContentLength(f.size());
    _server.send(200, ctype, "");
    if (isClientGone()) { f.close(); return; }
    _server.client().setTimeout(500);
    const size_t CHUNK = 512;
    uint8_t buf[CHUNK];
    while (f.available()) {
        if (isClientGone()) break;
        feedWatchdog();
        size_t n;
        { StorageManager::ReadGuard rg(_storageRef); n = f.read(buf, CHUNK); }
        if (n == 0) break;
        if (!safeSend((const char*)buf, n)) break;
    }
    f.close();
}

void WebManager::handleLangJs() {
    servePageFromFS(WebUI_FS::LANG_JS_PATH,
                    "application/javascript", "public, max-age=604800");
}

/* v3.34.0: F-WEB-DEDUP — CSS comum (drawer/topbar/breadcrumb/toast) extraído
 * das 8 páginas autenticadas para um asset único cacheável. */
void WebManager::handleStyleCss() {
    servePageFromFS(WebUI_FS::STYLE_CSS_PATH,
                    "text/css", "public, max-age=604800");
}

/* v3.34.0: favicon movido pra LittleFS (`/favicon.ico`) para liberar 11KB
 * de flash. data/favicon.ico vai pro FS via uploadfs. Sem o arquivo, retorna
 * 204 (browsers tratam como ausência sem erro visual). */
void WebManager::handleFavicon() {
    if (!LittleFS.exists("/favicon.ico")) { _server.send(204, "image/x-icon", ""); return; }
    File f = LittleFS.open("/favicon.ico", "r");
    if (!f) { _server.send(204, "image/x-icon", ""); return; }
    _server.sendHeader("Cache-Control", "public, max-age=604800");
    _server.setContentLength(f.size());
    _server.send(200, "image/x-icon", "");
    uint8_t buf[512];
    while (f.available()) {
        size_t r = f.read(buf, sizeof(buf));
        if (r == 0) break;
        _server.client().write(buf, r);
    }
    f.close();
}

bool WebManager::secureCompare(const String& a, const String& b) {

    size_t lenA = a.length();
    size_t lenB = b.length();
    size_t maxLen = (lenA > lenB) ? lenA : lenB;
    if (maxLen == 0) return (lenA == 0 && lenB == 0);

    uint8_t result = (lenA != lenB) ? 1 : 0;
    for (size_t i = 0; i < maxLen; i++) {
        char ca = (i < lenA) ? a[i] : 0;
        char cb = (i < lenB) ? b[i] : 0;
        result |= (ca ^ cb);
    }
    return (result == 0);
}
