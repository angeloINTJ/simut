/**
 * @file WebManager_Core.cpp
 * @brief Core infrastructure: constructor, begin, update, send guard, rate limiting.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include <hardware/watchdog.h>

using ReadGuard = StorageManager::ReadGuard;

WebManager::WebManager( ) : _server(80) {
 _currentUserPerms = 0;
 _currentUserId = -1;
 _currentUserName = "";


 for(int i = 0; i < 3; i++) {
 _activeSessions[i].token = "";
 _activeSessions[i].userId = -1;
 _activeSessions[i].lastActivity = 0;
 }
}

void WebManager::begin(StorageManager* storage, SensorManager* sensors,
 NetworkManager* net, DisplayManager* display,
 TelemetryManager* telemetry,
 SoundManager* sound) {
 _storageRef = storage;
 _sensorRef = sensors;
 _netRef = net;
 _displayRef = display;
 _telemetryRef = telemetry;
 _soundRef = sound;

 /*
 * Configurable port via WebConfigData (reserved[24..25]). Reconstructs
 * the server via placement new if !=80 — _server was default-initialized
 * with port 80 in the constructor but hasn't called .begin( ) or .on( ) yet,
 * so discard/recreation is safe.
 */
 WebConfigData* w = reinterpret_cast<WebConfigData*>(
 storage->getConfig( ).reserved + WEB_CONFIG_OFFSET);
 uint16_t webPort = (w->port > 0) ? w->port : WEB_DEFAULT_PORT;
 if (webPort != WEB_DEFAULT_PORT) {
 _server.~WebServer( );
 new (&_server) WebServer(webPort);
 }

 const char * headerkeys[] = {"Cookie", "Accept-Encoding"};
 size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
 _server.collectHeaders(headerkeys, headerkeyssize);


 initSendGuardTimer( );


 _server.on("/login", HTTP_GET, std::bind(&WebManager::handleLogin, this));
 _server.on("/logout", HTTP_GET, std::bind(&WebManager::handleLogout, this));
 _server.on("/force_chpass", HTTP_GET, std::bind(&WebManager::handleForceChpass, this));
 _server.on("/", HTTP_GET, std::bind(&WebManager::handleRoot, this));
 _server.on("/config", HTTP_GET, std::bind(&WebManager::handleConfig, this));
 _server.on("/network", HTTP_GET, std::bind(&WebManager::handleNetwork, this));
 _server.on("/users", HTTP_GET, std::bind(&WebManager::handleUsers, this));
 _server.on("/files", HTTP_GET, std::bind(&WebManager::handleFiles, this));
 _server.on("/alarms", HTTP_GET, std::bind(&WebManager::handleAlarms, this));
 _server.on("/license", HTTP_GET, std::bind(&WebManager::handleLicense, this));
 _server.on("/history", HTTP_GET, std::bind(&WebManager::handleHistory, this));
 _server.on("/lang.js", HTTP_GET, std::bind(&WebManager::handleLangJs, this));
 _server.on("/style.css", HTTP_GET, std::bind(&WebManager::handleStyleCss, this));
 _server.on("/favicon.ico", HTTP_GET, std::bind(&WebManager::handleFavicon, this));


 _server.on("/api/login_init", HTTP_GET, std::bind(&WebManager::handleApiLoginInit, this));
 _server.on("/api/login", HTTP_POST, std::bind(&WebManager::handleApiLogin, this));
 _server.on("/api/force_chpass", HTTP_POST, std::bind(&WebManager::handleApiForceChpass, this));
 _server.on("/api/login_chpass", HTTP_POST, std::bind(&WebManager::handleApiLoginChpass, this));
 _server.on("/api/status", HTTP_GET, std::bind(&WebManager::handleApiStatus, this));
 _server.on("/api/perms", HTTP_GET, std::bind(&WebManager::handleApiPerms, this));
 _server.on("/api/network", HTTP_GET, std::bind(&WebManager::handleApiNetwork, this));
 _server.on("/api/config", HTTP_GET, std::bind(&WebManager::handleApiConfig, this));
 _server.on("/api/users", HTTP_GET, std::bind(&WebManager::handleApiUsers, this));
 _server.on("/api/themes", HTTP_GET, std::bind(&WebManager::handleApiThemes, this));
 _server.on("/api/alarms", HTTP_GET, std::bind(&WebManager::handleApiAlarms, this));
 _server.on("/api/lang", HTTP_GET, std::bind(&WebManager::handleApiLang, this));
 _server.on("/api/calib", HTTP_GET, std::bind(&WebManager::handleApiCalibGet, this));
 _server.on("/api/calib", HTTP_POST, std::bind(&WebManager::handleApiCalibPost, this));


 _server.on("/api/save_sys", HTTP_POST, std::bind(&WebManager::handleSaveSystem, this));
 _server.on("/api/commit_all", HTTP_POST, std::bind(&WebManager::handleApiCommitAll, this));
 /* /api/save_net replaced by /api/commit_all */
 _server.on("/api/reset_touch_cal", HTTP_POST, std::bind(&WebManager::handleResetTouchCal, this));
 /* user_add/del/rst replaced by /api/commit_all */
 _server.on("/api/history_multi", HTTP_GET, std::bind(&WebManager::handleApiHistoryMulti, this)); /* Multi-sensor replacement for /api/history single-sensor */
 _server.on("/api/history_days", HTTP_GET, std::bind(&WebManager::handleApiHistoryDays, this));
 _server.on("/api/export/history.bin", HTTP_GET, std::bind(&WebManager::handleApiExportHistory, this));
 _server.on("/api/export/logs.bin", HTTP_GET, std::bind(&WebManager::handleApiExportLogs, this));
 _server.on("/api/logs", HTTP_GET, std::bind(&WebManager::handleApiLogs, this));
 _server.on("/api/clear_logs", HTTP_POST, std::bind(&WebManager::handleApiClearLogs, this));
 _server.on("/api/screenshot", HTTP_GET, std::bind(&WebManager::handleApiScreenshot, this));
 _server.on("/api/screenshot_chunk", HTTP_GET, std::bind(&WebManager::handleApiScreenshotChunk, this));
 _server.on("/api/sec_status", HTTP_GET, std::bind(&WebManager::handleApiSecStatus, this));
 _server.on("/api/set_time", HTTP_POST, std::bind(&WebManager::handleApiSetTime, this));


 _server.on("/download", HTTP_GET, std::bind(&WebManager::handleDownload, this));
 _server.on("/api/delete", HTTP_POST, std::bind(&WebManager::handleDelete, this));
 _server.on("/api/ls", HTTP_GET, std::bind(&WebManager::handleApiLs, this));
 _server.on("/api/mkdir", HTTP_POST, std::bind(&WebManager::handleApiMkdir, this));
 _server.on("/api/upload", HTTP_POST,
 std::bind(&WebManager::handleUploadComplete, this),
 std::bind(&WebManager::handleUploadData, this));

 /* OTA: full LittleFS backup download.
 * Response also includes X-Backup-PSize/X-Backup-PCrc
 * for the browser to verify integrity before accepting OTA. */
 _server.on("/api/backup", HTTP_GET, std::bind(&WebManager::handleApiBackup, this));

 /* OTA: single route for validate/apply (mode in ?op= query param).
 * Adding 2 POST routes with upload callback would cost ~16 KB of flash
 * (likely internal buffer of arduino-pico WebServer per route). */
 _server.on("/api/restore", HTTP_POST,
 std::bind(&WebManager::handleApiRestoreFinish, this),
 std::bind(&WebManager::handleApiRestoreUploadData, this));

 /* OTA: triggers apply of pending update (separate route from
 * /api/restore to distinguish restore of .bkp vs firmware apply). */
 _server.on("/api/ota/apply", HTTP_POST,
 std::bind(&WebManager::handleApiOtaApply, this));

 _server.onNotFound(std::bind(&WebManager::handleNotFound, this));


 _server.on("/favicon.ico", HTTP_GET, [this]( ) { _server.send(204, "image/x-icon", ""); });
 _server.on("/apple-touch-icon.png", HTTP_GET, [this]( ) { _server.send(204, "image/png", ""); });

 _server.begin( );
 LOG_CODE(LOG_INFO, "WEB", WEB_SERVER_STARTED, webPort, "");
}
bool WebManager::isRateLimited(uint32_t minIntervalMs) {
 uint32_t clientIP = (uint32_t)_server.client( ).remoteIP( );
 uint32_t now = millis( );
 int slot = -1;
 int oldest = 0;
 for (int i = 0; i < RATE_LIMIT_SLOTS; i++) {
 /* Expire old entries (TTL) — treat them as free */
 if (_rateLimits[i].ip != 0 && (now - _rateLimits[i].lastReq > RATE_LIMIT_TTL_MS)) {
 _rateLimits[i].ip = 0;
 _rateLimits[i].lastReq = 0;
 _rateLimits[i].hits = 0;
 }
 if (_rateLimits[i].ip == clientIP) { slot = i; break; }
 if (_rateLimits[i].lastReq < _rateLimits[oldest].lastReq) oldest = i;
 }
 if (slot == -1) {
 for (int i = 0; i < RATE_LIMIT_SLOTS; i++) {
 if (_rateLimits[i].ip == 0) { slot = i; break; }
 }
 if (slot == -1) slot = oldest;
 _rateLimits[slot].ip = clientIP;
 _rateLimits[slot].lastReq = 0;
 _rateLimits[slot].hits = 0;
 }
 if (now - _rateLimits[slot].lastReq < minIntervalMs) return true;
 _rateLimits[slot].lastReq = now;
 return false;
}

void WebManager::feedWatchdog( ) {
 watchdog_update( );
 if (_lightYieldCb) _lightYieldCb( );


}

/* A single breath between packets while streaming a long response. feedWatchdog
 * pets the watchdog and runs the light yield (keeps the live display fed); the
 * micro-delay then lets lwIP flush the just-sent packet and drain the PBUF pool
 * before we pile on more, and releases the heap/SPI arbiter so Core 1 renders a
 * frame. Cheap enough to call after every flushed chunk without slowing the
 * transfer meaningfully (a few ms per handful of KB). */
void WebManager::streamBreath( ) {
 feedWatchdog( );
 delay(WEB_STREAM_BREATH_DELAY_MS);
}

bool WebManager::rejectIfTouchPriority( ) {
 if (TouchPriority::isActive( )) {
 _server.sendHeader("Retry-After", "5");
 _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
 return true;
 }
 return false;
}

bool WebManager::isHandlerOvertime( ) {
 /* Wrap-safe: see comment in timeReached( ) (SystemDefs.h). */
 return (_handlerDeadline > 0 && timeReached(_handlerDeadline));
}


#include <pico/time.h>

/*
 * SendGuard — feeds the watchdog during blocking send calls.
 *
 * While _sendGuardActive=true, the timer feeds the watchdog every 2 s
 * (up to WDT_FEED_MAX_WINDOW_MS). If that ceiling is reached, signals clean
 * abort via _sendGuardExpired — queried by isClientGone( ), making
 * safeSend( ) return false and the handler exit gracefully
 * instead of being killed by the watchdog.
 *
 * _sendGuardExpired has external linkage so that isClientGone( ) (inline
 * in the header) can query it without additional indirection.
 */
volatile bool _sendGuardActive = false;
volatile bool _sendGuardExpired = false; /* extern — consumed by WebManager.h */

/* Abort-cause counters — see WebManager.h. */
volatile uint32_t _cgDeadlineHits = 0;
volatile uint32_t _cgGuardHits    = 0;
volatile uint32_t _cgDisconnHits  = 0;
volatile uint32_t _sendGuardStartMs = 0;
static struct repeating_timer _sendGuardTimer;

static bool _sendGuardTimerCallback(struct repeating_timer *t) {
 (void)t;
 if (_sendGuardActive) {
 uint32_t elapsed = millis( ) - _sendGuardStartMs;
 if (elapsed < WDT_FEED_MAX_WINDOW_MS) {
 watchdog_update( );
 } else {
 /* Ceiling reached: stop feeding (safety net against deadlock)
 * and signal clean abort for the handler on next safeSend( ). */
 _sendGuardExpired = true;
 }
 }
 return true;
}

void WebManager::initSendGuardTimer( ) {

 add_repeating_timer_ms(-2000, _sendGuardTimerCallback, nullptr, &_sendGuardTimer);
}


void WebManager::update( ) {
 _clientAcceptsGzip = false;

 uint32_t handlerStart = millis( );
 _handlerDeadline = handlerStart + 6000;

 /* Clear the abort latch at the start of every tick. It is set by the
  * SendGuard timer to end ONE overlong send, but safeSend( ) tests it before
  * constructing the SendGuard that would clear it — so once set, every later
  * safeSend returns false at its entry check, never reaches the constructor,
  * and the latch stays set. That turns a single slow send into permanently
  * truncated chunked responses for the rest of the uptime. */
 _sendGuardExpired = false;

 /* Multi-request drain per tick (up to 4) with time cap (50ms).
 * Reduces systemic latency from ~600ms (1 request per loop iteration)
 * to ~100-150ms when the main loop is busy with telemetry/
 * sensors. 50ms cap preserves display responsiveness. */
 const uint32_t budget = handlerStart + 50;
 {
  /* Everything the web server does on Core 0 lives under this scope. A stall
   * that traces as WEB_POLL rather than as one of the handler modules is in
   * the server/lwIP/CYW43 plumbing, not in our code. */
  LogManager::TraceScope _tPoll(0, MOD_WEB_POLL);
  for (int i = 0; i < 4; i++) {
   _server.handleClient( );
   if (millis( ) >= budget) break;
  }
 }

 _handlerDeadline = 0;
}

void WebManager::handleNotFound( ) {
 String host = _server.hostHeader( );
 String myIP = _netRef->getIpAddress( );

 if (host != myIP && !host.endsWith(".local")) {
 _server.sendHeader("Location", "http://" + myIP + "/network", true);
 _server.send(302, "text/plain", "Redirecting...");
 return;
 }
 _server.send(404, "text/plain", "404: Not Found");
}
