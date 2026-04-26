/**
 * @file    WebManager_Util.cpp
 * @brief   Utility functions: crypto tokens, file names, hex conversion, secure compare.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "WebManager.h"
#include "WebUI_GZ.h"
#include "Favicon.h"
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

void WebManager::handleLangJs() {
    _server.sendHeader("Cache-Control", "public, max-age=604800");
    _server.sendHeader("Content-Encoding", "gzip");
    _server.setContentLength(WebUI_GZ::LANG_JS_GZ_LEN);
    _server.send(200, "application/javascript", "");
    safeSend_GZ(WebUI_GZ::LANG_JS_GZ, WebUI_GZ::LANG_JS_GZ_LEN);
}

void WebManager::handleFavicon() {
    /* Servido do PROGMEM (Favicon.cpp gerado por tools/build_favicon_header.py).
     * Cache de 7 dias no browser. Em flash em vez de FS pra: (1) sobreviver a
     * uploadfs, (2) tirar dependência do FS, (3) carregar mais rápido (sem
     * lockout). Ver custo flash em SystemDefs_Limits.h. */
    _server.sendHeader("Cache-Control", "public, max-age=604800");
    _server.setContentLength(Favicon::LEN);
    _server.send(200, "image/x-icon", "");
    safeSend_GZ(Favicon::DATA, Favicon::LEN);
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
