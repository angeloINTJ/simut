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
#include <LittleFS.h>
#include <hardware/watchdog.h>

/* Server TLS material (M-6). Under /config so A-4's isSecretFsPath refuses to
 * serve the private key over /download, and so `system format` clears it with
 * the rest of the config. PEM, provisioned by the operator via the Files page:
 *   openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
 *     -keyout web_key.pem -out web_cert.pem -days 3650 -nodes -subj "/CN=simut"
 * EC (P-256) over RSA on purpose — a P-256 handshake fits this heap far more
 * comfortably than RSA-2048. */
#define FILE_WEB_CERT "/config/web_cert.pem"
#define FILE_WEB_KEY  "/config/web_key.pem"

using ReadGuard = StorageManager::ReadGuard;

#ifdef SIMUT_WEB_HTTPS
/* Framework override 2h (bearssl_server_static_pool.patch, v2): reserves the
 * TLS-accept pool (~21,5 KB: server ctx + iobufs) from the boot heap, once,
 * instead of the pool living in BSS. Called below ONLY when the HTTPS server
 * is actually starting, so HTTP-only configs keep the RAM — measured on the
 * rig: the BSS pool alone dropped idle free heap 38 KB → 16 KB and starved
 * the telemetry heap gates. C linkage, defined inside the patched
 * WiFiClientSecureBearSSL.cpp. */
extern "C" bool simut_reserve_tls_server_pool(void);
#endif

WebManager::WebManager( ) {
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
 * Configurable port via WebConfigData (reserved[24..25]). The concrete server
 * (HTTP or HTTPS) is created here, once, by beginServer( ): HTTPS when a
 * certificate is provisioned in /config, HTTP otherwise (M-6). Routes are then
 * registered on _server through its HTTPServer base below, and the concrete
 * begin( ) is called after the routes are in place.
 */
 WebConfigData* w = reinterpret_cast<WebConfigData*>(
 storage->getConfig( ).reserved + WEB_CONFIG_OFFSET);
 uint16_t webPort = (w->port > 0) ? w->port : WEB_DEFAULT_PORT;
 beginServer(webPort);

 /* Authorization: /metrics Basic auth — a Prometheus scraper cannot run
  * the login flow, so its credentials arrive as a header. */
 const char * headerkeys[] = {"Cookie", "Accept-Encoding", "Authorization"};
 size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
 _server->collectHeaders(headerkeys, headerkeyssize);


 initSendGuardTimer( );


 _server->on("/login", HTTP_GET, std::bind(&WebManager::handleLogin, this));
 _server->on("/logout", HTTP_GET, std::bind(&WebManager::handleLogout, this));
 _server->on("/force_chpass", HTTP_GET, std::bind(&WebManager::handleForceChpass, this));
 _server->on("/", HTTP_GET, std::bind(&WebManager::handleRoot, this));
 _server->on("/config", HTTP_GET, std::bind(&WebManager::handleConfig, this));
 _server->on("/telemetry", HTTP_GET, std::bind(&WebManager::handleTelemetry, this));
 _server->on("/network", HTTP_GET, std::bind(&WebManager::handleNetwork, this));
 _server->on("/users", HTTP_GET, std::bind(&WebManager::handleUsers, this));
 _server->on("/files", HTTP_GET, std::bind(&WebManager::handleFiles, this));
 _server->on("/alarms", HTTP_GET, std::bind(&WebManager::handleAlarms, this));
 _server->on("/license", HTTP_GET, std::bind(&WebManager::handleLicense, this));
 _server->on("/history", HTTP_GET, std::bind(&WebManager::handleHistory, this));
 _server->on("/lang.js", HTTP_GET, std::bind(&WebManager::handleLangJs, this));
 _server->on("/style.css", HTTP_GET, std::bind(&WebManager::handleStyleCss, this));
 _server->on("/favicon.ico", HTTP_GET, std::bind(&WebManager::handleFavicon, this));


 _server->on("/api/login_init", HTTP_GET, std::bind(&WebManager::handleApiLoginInit, this));
 _server->on("/api/login", HTTP_POST, std::bind(&WebManager::handleApiLogin, this));
 _server->on("/api/force_chpass", HTTP_POST, std::bind(&WebManager::handleApiForceChpass, this));
 _server->on("/api/login_chpass", HTTP_POST, std::bind(&WebManager::handleApiLoginChpass, this));
 _server->on("/api/status", HTTP_GET, std::bind(&WebManager::handleApiStatus, this));
 _server->on("/metrics", HTTP_GET, std::bind(&WebManager::handleMetrics, this));
 _server->on("/api/perms", HTTP_GET, std::bind(&WebManager::handleApiPerms, this));
 _server->on("/api/network", HTTP_GET, std::bind(&WebManager::handleApiNetwork, this));
 _server->on("/api/config", HTTP_GET, std::bind(&WebManager::handleApiConfig, this));
 _server->on("/api/users", HTTP_GET, std::bind(&WebManager::handleApiUsers, this));
 _server->on("/api/themes", HTTP_GET, std::bind(&WebManager::handleApiThemes, this));
 _server->on("/api/alarms", HTTP_GET, std::bind(&WebManager::handleApiAlarms, this));
 _server->on("/api/lang", HTTP_GET, std::bind(&WebManager::handleApiLang, this));
 _server->on("/api/sensors", HTTP_GET, std::bind(&WebManager::handleApiSensorsGet, this));
 _server->on("/api/calib", HTTP_GET, std::bind(&WebManager::handleApiCalibGet, this));
 _server->on("/api/calib", HTTP_POST, std::bind(&WebManager::handleApiCalibPost, this));


 _server->on("/api/save_sys", HTTP_POST, std::bind(&WebManager::handleSaveSystem, this));
 _server->on("/api/commit_all", HTTP_POST, std::bind(&WebManager::handleApiCommitAll, this));
 /* /api/save_net replaced by /api/commit_all */
 _server->on("/api/reset_touch_cal", HTTP_POST, std::bind(&WebManager::handleResetTouchCal, this));
 _server->on("/api/history_rebind", HTTP_POST, std::bind(&WebManager::handleApiHistoryRebind, this));
 /* user_add/del/rst replaced by /api/commit_all */
 _server->on("/api/history_multi", HTTP_GET, std::bind(&WebManager::handleApiHistoryMulti, this)); /* Multi-sensor replacement for /api/history single-sensor */
 _server->on("/api/history_days", HTTP_GET, std::bind(&WebManager::handleApiHistoryDays, this));
 _server->on("/api/export/history.bin", HTTP_GET, std::bind(&WebManager::handleApiExportHistory, this));
 _server->on("/api/history/open", HTTP_GET, std::bind(&WebManager::handleApiHistoryOpen, this));
 _server->on("/api/export/logs.bin", HTTP_GET, std::bind(&WebManager::handleApiExportLogs, this));
 _server->on("/api/logs", HTTP_GET, std::bind(&WebManager::handleApiLogs, this));
 _server->on("/api/clear_logs", HTTP_POST, std::bind(&WebManager::handleApiClearLogs, this));
 _server->on("/api/screenshot", HTTP_GET, std::bind(&WebManager::handleApiScreenshot, this));
 _server->on("/api/screenshot_chunk", HTTP_GET, std::bind(&WebManager::handleApiScreenshotChunk, this));
 _server->on("/api/sec_status", HTTP_GET, std::bind(&WebManager::handleApiSecStatus, this));
 _server->on("/api/set_time", HTTP_POST, std::bind(&WebManager::handleApiSetTime, this));

 /* Maintenance actions the serial CLI used to own (sensor scan/accept/wipe,
  * telemetry sync/reset). Six operations behind one route and an ?op=
  * selector, for the same reason /api/restore multiplexes validate/apply. */
 _server->on("/api/action", HTTP_POST, std::bind(&WebManager::handleApiAction, this));


 _server->on("/download", HTTP_GET, std::bind(&WebManager::handleDownload, this));
 _server->on("/api/delete", HTTP_POST, std::bind(&WebManager::handleDelete, this));
 _server->on("/api/ls", HTTP_GET, std::bind(&WebManager::handleApiLs, this));
 _server->on("/api/mkdir", HTTP_POST, std::bind(&WebManager::handleApiMkdir, this));
 _server->on("/api/upload", HTTP_POST,
 std::bind(&WebManager::handleUploadComplete, this),
 std::bind(&WebManager::handleUploadData, this));

 /* OTA: full LittleFS backup download.
 * Response also includes X-Backup-PSize/X-Backup-PCrc
 * for the browser to verify integrity before accepting OTA. */
 _server->on("/api/backup", HTTP_GET, std::bind(&WebManager::handleApiBackup, this));

 /* OTA: single route for validate/apply (mode in ?op= query param).
 * Adding 2 POST routes with upload callback would cost ~16 KB of flash
 * (likely internal buffer of arduino-pico WebServer per route). */
 _server->on("/api/restore", HTTP_POST,
 std::bind(&WebManager::handleApiRestoreFinish, this),
 std::bind(&WebManager::handleApiRestoreUploadData, this));

 /* OTA: triggers apply of pending update (separate route from
 * /api/restore to distinguish restore of .bkp vs firmware apply). */
 _server->on("/api/ota/apply", HTTP_POST,
 std::bind(&WebManager::handleApiOtaApply, this));

 _server->onNotFound(std::bind(&WebManager::handleNotFound, this));


 /* /favicon.ico is already served by handleFavicon above (the real 835 B
  * icon). The WebServer matches the FIRST registered handler for a path
  * (append-to-tail list, walked from the head), so a second /favicon.ico
  * here would be dead — it was, returning 204 that never reached a client.
  * Removed. /apple-touch-icon.png has no other handler, so the 204 stub
  * that stops iOS from 404-spamming for it stays. */
 _server->on("/apple-touch-icon.png", HTTP_GET, [this]( ) { _server->send(204, "image/png", ""); });

 /* HTTP keep-alive (framework override 2f, opt-in API): ON by default since
  * v2.3.0 — one TLS handshake per browser session instead of one per request
  * (~60% faster HTTPS pages, measured). The opt-out lives in the config
  * (SetupFlagsData) and is exposed on the network page when a TLS cert pair
  * is present; commit_all reboots, so reading it here at boot is enough. */
 if (_storageRef && _storageRef->isWebKeepAliveEnabled( )) {
 _server->enableKeepAlive(true);
 }

 /* Concrete begin( ) — not on the HTTPServer base (it binds the socket). */
#ifdef SIMUT_WEB_HTTPS
 if (_serverIsHttps) _serverHttps->begin( ); else
#endif
 _serverHttp->begin( );
 LOG_CODE(LOG_INFO, "WEB", WEB_SERVER_STARTED, _serverIsHttps ? (webPort | 0x8000) : webPort, "");
}

/* Creates the concrete server — HTTPS when /config/web_cert.pem (+key) load,
 * HTTP otherwise — and points _server at it for the request handlers. Does not
 * start it: routes are registered by the caller first, then the concrete
 * begin( ) runs. A cert that fails to load is not fatal: the device falls back
 * to HTTP so a bad or missing cert can never lock the operator out of the UI. */
void WebManager::beginServer(uint16_t port) {
 /* HTTPS pulls ~20 KB of BearSSL server + cert-parsing code into the image, so
  * it is a release-only feature (SIMUT_WEB_HTTPS): the flash-tight pico_w_test
  * env, which the on-device suite flashes, cannot spare it and does not need
  * it (the suite exercises the HTTP handlers, transport-agnostic). */
#ifdef SIMUT_WEB_HTTPS
 if (loadServerCert( )) {
  /* The TLS-accept pool (accept-stall fix, override 2h) is reserved HERE —
   * once, from a heap the boot has not fragmented yet, and only on the branch
   * where TLS accepts will actually exist. On failure the accept path falls
   * back to per-accept heap allocations (the pre-pool behaviour, with its
   * known stall risk under heap churn), so the server still comes up. */
  if (!simut_reserve_tls_server_pool( )) {
   LOG_CODE(LOG_WARN, "WEB", SYS_HEAP_LOW, (int)(rp2040.getFreeHeap( ) / 1024),
            "TLS accept pool not reserved — accepts fall back to heap");
  }
  /* Default the HTTPS listener to 443 so plain https://<ip> reaches it, but
   * honour an explicitly configured port (anything other than the 80 default).
   * Serving TLS on 80 works but surprises a browser, which speaks cleartext
   * there. */
  if (port == WEB_DEFAULT_PORT) port = 443;
  _serverHttps = new WebServerSecure(port);
  /* Buffers for the SERVER role, both numbers learned on hardware:
   * — Transmit 1024: a server SENDS its cert flight; the client-style 512 cap
   *   (below BearSSL's own 837 default) could not even hold the ServerHello +
   *   Certificate and the handshake returned zero bytes on the bench.
   * — Receive 16709 (BR_SSL_BUFSIZE_INPUT, one full TLS record): a record must
   *   fit the receive buffer WHOLE, the max-fragment-length extension is
   *   offered by CLIENTS only, and stock OpenSSL/browsers never offer it —
   *   they ship 16 KB records for any large body. The earlier 4096 cap
   *   therefore killed every upload past ~4 KB (2026-08-19, clean bisection:
   *   3.5 KB passes, 5 KB dies mid-body) — language packs and OTA were
   *   impossible over TLS while downloads worked, because our own transmit
   *   records are small. The buffer is allocated per accepted connection, not
   *   at boot (post-boot largest free block: 33.6 KB, one TLS client fits);
   *   under long-uptime fragmentation the accept can fail and that connection
   *   drops, while the listener itself keeps running. */
  /* EC keys go through setECCert (KEYX|SIGN usage), RSA through setRSACert —
   * calling the wrong one leaves the handshake reading zero bytes back, which
   * is exactly how the first EC cert failed on the bench. */
  if (_serverKey->isEC( ))
   _serverHttps->getServer( ).setECCert(_serverCert, BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN, _serverKey);
  else
   _serverHttps->getServer( ).setRSACert(_serverCert, _serverKey);
  _serverHttps->getServer( ).setBufferSizes(16709, 1024);
  _server = _serverHttps;
  _serverIsHttps = true;
  LOG_CODE(LOG_INFO, "WEB", SYS_TEL_SSL, port, "HTTPS server (provisioned cert)");
  return;
 }
#endif
 _serverHttp = new WebServer(port);
 _server = _serverHttp;
 _serverIsHttps = false;
}

void WebManager::pumpServer( ) {
#ifdef SIMUT_WEB_HTTPS
 if (_serverIsHttps) { _serverHttps->handleClient( ); return; }
#endif
 _serverHttp->handleClient( );
}

bool WebManager::tlsCertFilesPresent( ) {
 _storageRef->enterFlashReadLock( );
 bool present = LittleFS.exists(FILE_WEB_CERT) && LittleFS.exists(FILE_WEB_KEY);
 _storageRef->exitFlashReadLock( );
 return present;
}

/* Reads /config/web_cert.pem + web_key.pem and parses them into the X509List /
 * PrivateKey that beginServer( ) hands to WebServerSecure. Returns false —
 * quietly, so beginServer falls back to HTTP — when the pair is absent; returns
 * false and logs WEB_CERT_INVALID when a file is present but does not parse, so
 * a botched cert reads as "why is it still HTTP?" in the log rather than as
 * silence. Both files are size-capped (a legitimate EC cert+key is well under
 * 4 KB); the parsed objects live for the whole boot (owned by the manager). */
bool WebManager::loadServerCert( ) {
#ifndef SIMUT_WEB_HTTPS
 return false;   /* HTTPS compiled out (flash-tight envs) — always HTTP */
#else
 /* AP mode is the setup/recovery surface: a user who reached it did so because
  * the normal path failed, often BECAUSE of the TLS material (a cert that no
  * longer matches its key locks HTTPS with no way in). The Access Point serves
  * HTTP unconditionally so that surface can always delete or replace the cert,
  * no serial console required. Checked before the files are even read. */
 if (_netRef && _netRef->isApConfig( )) {
  LOG_CODE(LOG_INFO, "WEB", SYS_TEL_SSL, 0, "AP mode: HTTP only (recovery surface)");
  return false;
 }
 String certPem, keyPem;
 {
  ReadGuard rg(_storageRef);
  if (!LittleFS.exists(FILE_WEB_CERT) || !LittleFS.exists(FILE_WEB_KEY)) return false;
  File cf = LittleFS.open(FILE_WEB_CERT, "r");
  if (cf) { if (cf.size( ) > 0 && cf.size( ) <= 8192) certPem = cf.readString( ); cf.close( ); }
  File kf = LittleFS.open(FILE_WEB_KEY, "r");
  if (kf) { if (kf.size( ) > 0 && kf.size( ) <= 8192) keyPem = kf.readString( ); kf.close( ); }
 }
 if (certPem.length( ) == 0 || keyPem.length( ) == 0) {
  LOG_CODE(LOG_WARN, "WEB", WEB_CERT_INVALID, 1, "web cert/key empty or oversized");
  return false;
 }

 _serverCert = new (std::nothrow) BearSSL::X509List(certPem.c_str( ));
 _serverKey  = new (std::nothrow) BearSSL::PrivateKey(keyPem.c_str( ));
 const bool ok = _serverCert && _serverCert->getCount( ) > 0
              && _serverKey && (_serverKey->isRSA( ) || _serverKey->isEC( ));
 if (!ok) {
  delete _serverCert; _serverCert = nullptr;
  delete _serverKey;  _serverKey  = nullptr;
  LOG_CODE(LOG_WARN, "WEB", WEB_CERT_INVALID, 2, "web cert/key failed to parse");
  return false;
 }
 return true;
#endif /* SIMUT_WEB_HTTPS */
}
bool WebManager::isRateLimited(uint32_t minIntervalMs) {
 uint32_t clientIP = (uint32_t)_server->client( ).remoteIP( );
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
 /* The heartbeat must ride along too: this is the ONLY feeder for every
  * send-wait and stream-breath loop, and it fed the dog without stamping
  * the beat — so the autopsy's "at up=" froze at the last feedWdt( ) site
  * while whole seconds of fed waiting ran on. Every stall investigated
  * under load was mislocated by however long the beatless stretch was. */
 TRACE_BEAT(0);
 /* The send loop feeds through here rather than feedWdt( ), so without this the
  * Core-1 stall sampler would be blind for the whole streaming phase of a
  * download — half of the load under investigation. */
 core1StallSample( );
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
 _server->sendHeader("Retry-After", "5");
 _server->send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
 return true;
 }
 return false;
}

bool WebManager::isHandlerOvertime( ) {
 /* Wrap-safe: see comment in timeReached( ) (SystemDefs.h). */
 return (_handlerDeadline > 0 && timeReached(_handlerDeadline));
}


#include <pico/time.h>
#include <hardware/watchdog.h>

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
   /* Fresh request, fresh position: a death wearing a previous request's
    * hp= sent one investigation down a stale trail already. */
   watchdog_hw->scratch[7] = 0;
   _chunkedResponse = false;
   pumpServer( );
   /* 740: handleClient RETURNED. Together with 722 (end of safeSendN) this
    * splits the storm residual three ways instead of lumping it all under
    * 721 — ours, the framework's, or the drain's. */
   watchdog_hw->scratch[7] = 740;
   /* Feed here, at the one choke point every request passes through on its
    * way out. The window between handleClient returning and drainOrDrop's own
    * fed loop was the last unfed stretch on Core 0: its entry queries lwIP
    * (client()/connected()/availableForWrite()) before reaching HPOS(600),
    * and a reboot wearing exactly hp=740 on a graph read (2026-08-10, user's
    * browser, ~1 in 33 drains) placed the stall right there. drainOrDrop
    * feeds inside its loop and is capped at WEB_SEND_STALL_MS, so it was never
    * the culprit; this line closes the gap ahead of it. Cheap: watchdog_update
    * is a register write, run a few times per tick. */
   watchdog_update( );
   /* Before the framework can retire this client with its polite close
    * (whose ACK-wait a trickling reader extends forever, unfed), drain
    * the un-ACKed tail with the watchdog fed or drop the connection.
    * Only when a send completed — see _drainPending.
    *
    * Careful: for a response the framework closes itself (Connection:
    * close reaches CLIENT_MUST_STOP), retirement happens INSIDE
    * handleClient and this call is already too late — measured 2026-08-10. */
   if (_drainPending) { drainOrDrop( ); _drainPending = false; }
   if (millis( ) >= budget) break;
  }
 }

 _handlerDeadline = 0;
}

void WebManager::handleNotFound( ) {
 String host = _server->hostHeader( );
 String myIP = _netRef->getIpAddress( );

 if (host != myIP && !host.endsWith(".local")) {
 /* Match the redirect scheme to the transport: an https:// page that redirects
  * to http:// trips mixed-content and downgrades the session. */
 const char* scheme = _serverIsHttps ? "https://" : "http://";
 _server->sendHeader("Location", String(scheme) + myIP + "/network", true);
 _server->send(302, "text/plain", "Redirecting...");
 return;
 }
 _server->send(404, "text/plain", "404: Not Found");
}
