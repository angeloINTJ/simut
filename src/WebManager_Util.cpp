/**
 * @file WebManager_Util.cpp
 * @brief Utility functions: crypto tokens, file names, hex conversion, secure compare.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "WebManager.h"
#include "WebUI_GZ.h"
#include "Favicon.h"
#include <bearssl/bearssl_hash.h>

using ReadGuard = StorageManager::ReadGuard;

/* getDynamicExpectedHash was removed with the "*PENDING*" login branch: it
 * derived the first-login password as sha256(Capitalized(username)@DDMMYYYY),
 * which anyone knowing the username and the date could reproduce. New accounts
 * get a random one-time password instead (assignTempPassword,
 * WebManager_Commit.cpp). See the note in verifyPasswordFor. */

String WebManager::generateSecureToken( ) {


 uint32_t entropy[4];
 entropy[0] = rp2040.hwrand32( );
 entropy[1] = rp2040.hwrand32( );
 entropy[2] = rp2040.hwrand32( );
 entropy[3] = rp2040.hwrand32( );


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
/* extractCsvToken removed — not needed with binary format */

String WebManager::rgb565ToHex(uint16_t color) {
 uint8_t r = (color >> 11) * 255 / 31;
 uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
 uint8_t b = (color & 0x1F) * 255 / 31;
 char hex[8]; snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
 return String(hex);
}

void WebManager::handleLangJs( ) {
 _server.sendHeader("Cache-Control", "public, max-age=604800");
 _server.sendHeader("Content-Encoding", "gzip");
 _server.setContentLength(WebUI_GZ::LANG_JS_GZ_LEN);
 _server.send(200, "application/javascript", "");
 safeSend_GZ(WebUI_GZ::LANG_JS_GZ, WebUI_GZ::LANG_JS_GZ_LEN);
}

/* Common CSS (drawer/topbar/breadcrumb/toast) extracted from
 * the 8 authenticated pages into a single cacheable asset. */
void WebManager::handleStyleCss( ) {
 _server.sendHeader("Cache-Control", "public, max-age=604800");
 _server.sendHeader("Content-Encoding", "gzip");
 _server.setContentLength(WebUI_GZ::STYLE_CSS_GZ_LEN);
 _server.send(200, "text/css", "");
 safeSend_GZ(WebUI_GZ::STYLE_CSS_GZ, WebUI_GZ::STYLE_CSS_GZ_LEN);
}

/* Served from the firmware image (src/Favicon.cpp, generated from
 * data/favicon.ico by a pre-build hook).
 *
 * It spent a while on LittleFS to free 11 KB back when real flash headroom
 * was 660 bytes. That is no longer the binding constraint, and the filesystem
 * copy had two costs the flash one does not: it vanished on `system format`
 * until someone re-uploaded it, and publishing it meant `uploadfs`, which
 * reformats the partition and takes /history and calib.csv with it.
 *
 * safeSend_GZ is not gzip-specific — it is the length-aware PROGMEM blob
 * sender, which is exactly what an .ico needs (NUL bytes rule out the
 * string-oriented safeSend_P).
 *
 * LEN 0 means the tree had no asset at build time: answer 204, which browsers
 * treat as "no icon" without drawing an error. */
void WebManager::handleFavicon( ) {
 if (Favicon::LEN == 0) { _server.send(204, "image/x-icon", ""); return; }
 _server.sendHeader("Cache-Control", "public, max-age=604800");
 _server.setContentLength(Favicon::LEN);
 _server.send(200, "image/x-icon", "");
 safeSend_GZ(Favicon::DATA, Favicon::LEN);
}

bool WebManager::secureCompare(const String& a, const String& b) {

 size_t lenA = a.length( );
 size_t lenB = b.length( );
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
