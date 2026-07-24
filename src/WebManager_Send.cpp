/**
 * @file WebManager_Send.cpp
 * @brief Send infrastructure: safeSend overloads, gzip delivery, safeStreamFile, client disconnect detection.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include <hardware/watchdog.h>

using ReadGuard = StorageManager::ReadGuard;

/* Single point for broken pipe observability.
 * Throttle 5s avoids flooding the log when a handler sends N chunks after the
 * client closes. `origin` identifies the overload (s/sP/sN/gz) — useful for
 * tracking which streaming died. No stack trace; just binary signal. */
void WebManager::maybeLogClientDisconnect(const char* origin) {
 uint32_t now = millis( );
 if (now - _lastDisconnectLogMs < 5000) return;
 _lastDisconnectLogMs = now;
 LogManager::instance( ).log(LOG_WARN, "WEB", WEB_CLIENT_DISCONNECT, String(origin));
}

/* A zero-length chunk is the terminator in HTTP chunked transfer-encoding, so
 * handing sendContent( ) an empty string ENDS the response body. Mid-stream
 * that silently truncates the reply and every later safeSend( ) then sees a
 * finished connection and reports the client as gone.
 *
 * This is not hypothetical: /api/config emits `"ambHwId":"` and then sends
 * jsonEscape(sensors[10].hwId), which is empty on any device without a custom
 * ambient id — the default. The body ended on that opening quote, the browser
 * got JSON it could not parse, and the settings page came up blank. Measured:
 * 483 bytes, ending exactly at `"ambHwId":"`.
 *
 * Nothing to send is success, not a chunk. */
static inline bool sendIsNoOp(const char* c) { return c == nullptr || c[0] == '\0'; }

bool WebManager::safeSend(const char* content) {
 if (sendIsNoOp(content)) return !isClientGone( );
 if (isClientGone( )) { maybeLogClientDisconnect("s/early"); return false; }

 _server.client( ).setTimeout(500);
 feedWatchdog( );
 {
 SendGuard sg;
 _server.sendContent(content);
 }
 bool gone = isClientGone( );
 if (gone) maybeLogClientDisconnect("s/post");
 return !gone;
}

bool WebManager::safeSend(const char* data, size_t len) {
 if (len == 0 || data == nullptr) return !isClientGone( );
 if (isClientGone( )) { maybeLogClientDisconnect("sN/early"); return false; }

 _server.client( ).setTimeout(500);
 feedWatchdog( );
 {
 SendGuard sg;
 _server.sendContent(data, len);
 }
 bool gone = isClientGone( );
 if (gone) maybeLogClientDisconnect("sN/post");
 return !gone;
}

bool WebManager::safeSend(const String& content) {
 if (content.length( ) == 0) return !isClientGone( );
 if (isClientGone( )) { maybeLogClientDisconnect("sStr/early"); return false; }

 _server.client( ).setTimeout(500);
 feedWatchdog( );
 {
 SendGuard sg;
 _server.sendContent(content);
 }
 bool gone = isClientGone( );
 if (gone) maybeLogClientDisconnect("sStr/post");
 return !gone;
}

bool WebManager::safeSend_P(const char* content) {
 if (sendIsNoOp(content)) return !isClientGone( );
 if (isClientGone( )) { maybeLogClientDisconnect("sP/early"); return false; }

 _server.client( ).setTimeout(500);
 feedWatchdog( );
 {
 SendGuard sg;
 _server.sendContent_P(content);
 }
 bool gone = isClientGone( );
 if (gone) maybeLogClientDisconnect("sP/post");
 return !gone;
}


void WebManager::detectGzipSupport( ) {
 _clientAcceptsGzip = false;
 if (_server.hasHeader("Accept-Encoding")) {
 String ae = _server.header("Accept-Encoding");
 _clientAcceptsGzip = (ae.indexOf("gzip") >= 0);
 }
}

bool WebManager::safeSend_GZ(const uint8_t* gz_data, size_t gz_len) {
 if (isClientGone( )) return false;

 _server.client( ).setTimeout(500);


 const size_t CHUNK = 512;
 size_t sent = 0;
 while (sent < gz_len) {
 if (isClientGone( )) return false;
 feedWatchdog( );
 size_t n = (gz_len - sent > CHUNK) ? CHUNK : (gz_len - sent);
 char buf[512];
 memcpy_P(buf, gz_data + sent, n);
 if (!safeSend(buf, n)) return false;
 sent += n;
 }
 return true;
}

void WebManager::safeStreamFile(File& f, const String& contentType) {
 const size_t CHUNK = 1024;
 uint8_t buf[CHUNK];
 _server.setContentLength(f.size( ));
 _server.send(200, contentType, "");

 _server.client( ).setTimeout(500);

 bool hasMore = true;
 while (hasMore) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_FILE, 0, "");
 return;
 }

 size_t n = 0;
 {
 ReadGuard rg(_storageRef);
 if (f.available( )) n = f.read(buf, CHUNK);
 hasMore = f.available( );
 }
 if (n > 0) safeSend((const char*)buf, n);
 feedWatchdog( );
 }
 safeSend("");
}
