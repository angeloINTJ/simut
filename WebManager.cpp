/**
 * @file    WebManager.cpp
 * @brief   Implementation of WebManager — HTTP handlers, session management, and API endpoints.
 * @details Implements all web routes: login/logout, dashboard, history viewer,
 * file manager (upload/download/delete/mkdir), system/network/user
 * configuration, alarm/sound settings, log viewer, screenshot capture,
 * and JSON API endpoints. Uses SafeStream for client disconnect
 * detection and automatic gzip content negotiation.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "WebManager.h"

#include "WebUI_GZ.h"
#include "LogManager.h"
#include "MetricsManager.h"
#include "Themes.h"
#include "TouchPriority.h"
#include <LittleFS.h>
#include <time.h>
#include <hardware/watchdog.h>
#include <algorithm>
#include <functional>

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

WebManager::WebManager() : _server(80) {
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
    _sensorRef  = sensors;
    _netRef     = net;
    _displayRef = display;
    _telemetryRef = telemetry;
    _soundRef   = sound;

    /*
     * Porta configurável via WebConfigData (reserved[24..25]). Reconstrói
     * o servidor via placement new se !=80 — _server foi default-initializado
     * com porta 80 no construtor mas ainda não chamou .begin() nem .on(),
     * então descarte/recriação é seguro.
     */
    WebConfigData* w = reinterpret_cast<WebConfigData*>(
        storage->getConfig().reserved + WEB_CONFIG_OFFSET);
    uint16_t webPort = (w->port > 0) ? w->port : WEB_DEFAULT_PORT;
    if (webPort != WEB_DEFAULT_PORT) {
        _server.~WebServer();
        new (&_server) WebServer(webPort);
    }

    const char * headerkeys[] = {"Cookie", "Accept-Encoding"};
    size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
    _server.collectHeaders(headerkeys, headerkeyssize);


    initSendGuardTimer();


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


    _server.on("/api/login_init", HTTP_GET, std::bind(&WebManager::handleApiLoginInit, this));
    _server.on("/api/login", HTTP_POST, std::bind(&WebManager::handleApiLogin, this));
    _server.on("/api/force_chpass", HTTP_POST, std::bind(&WebManager::handleApiForceChpass, this));
    _server.on("/api/status", HTTP_GET, std::bind(&WebManager::handleApiStatus, this));
    _server.on("/api/perms", HTTP_GET, std::bind(&WebManager::handleApiPerms, this));
    _server.on("/api/network", HTTP_GET, std::bind(&WebManager::handleApiNetwork, this));
    _server.on("/api/config", HTTP_GET, std::bind(&WebManager::handleApiConfig, this));
    _server.on("/api/users", HTTP_GET, std::bind(&WebManager::handleApiUsers, this));
    _server.on("/api/themes", HTTP_GET, std::bind(&WebManager::handleApiThemes, this));
    _server.on("/api/alarms", HTTP_GET, std::bind(&WebManager::handleApiAlarms, this));


    _server.on("/api/save_sys", HTTP_POST, std::bind(&WebManager::handleSaveSystem, this));
    _server.on("/api/commit_all", HTTP_POST, std::bind(&WebManager::handleApiCommitAll, this));
    /* U24 Phase C: /api/save_net substituido por /api/commit_all */
    _server.on("/api/reset_touch_cal", HTTP_POST, std::bind(&WebManager::handleResetTouchCal, this));
    /* U24 Phase B: user_add/del/rst substituidos por /api/commit_all */
    _server.on("/api/history", HTTP_GET, std::bind(&WebManager::handleApiHistoryData, this));
    _server.on("/api/history_days", HTTP_GET, std::bind(&WebManager::handleApiHistoryDays, this));
    _server.on("/api/logs", HTTP_GET, std::bind(&WebManager::handleApiLogs, this));
    _server.on("/api/clear_logs", HTTP_POST, std::bind(&WebManager::handleApiClearLogs, this));
    _server.on("/api/screenshot", HTTP_GET, std::bind(&WebManager::handleApiScreenshot, this));
    _server.on("/api/sec_status", HTTP_GET, std::bind(&WebManager::handleApiSecStatus, this));
    _server.on("/api/set_time", HTTP_POST, std::bind(&WebManager::handleApiSetTime, this));


    _server.on("/download", HTTP_GET, std::bind(&WebManager::handleDownload, this));
    _server.on("/api/delete", HTTP_POST, std::bind(&WebManager::handleDelete, this));
    _server.on("/api/ls", HTTP_GET, std::bind(&WebManager::handleApiLs, this));
    _server.on("/api/mkdir", HTTP_POST, std::bind(&WebManager::handleApiMkdir, this));
    _server.on("/api/upload", HTTP_POST,
               std::bind(&WebManager::handleUploadComplete, this),
               std::bind(&WebManager::handleUploadData, this));

    _server.onNotFound(std::bind(&WebManager::handleNotFound, this));


    _server.on("/favicon.ico", HTTP_GET, [this]() { _server.send(204, "image/x-icon", ""); });
    _server.on("/apple-touch-icon.png", HTTP_GET, [this]() { _server.send(204, "image/png", ""); });

    _server.begin();
    LOG_CODE(LOG_INFO, "WEB", WEB_SERVER_STARTED, webPort, "");
}

bool WebManager::isRateLimited(uint32_t minIntervalMs) {
    uint32_t clientIP = (uint32_t)_server.client().remoteIP();
    uint32_t now = millis();
    int slot = -1;
    int oldest = 0;
    for (int i = 0; i < RATE_LIMIT_SLOTS; i++) {
        /* Expirar entradas antigas (TTL) — tratá-las como livres */
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

void WebManager::feedWatchdog() {
    watchdog_update();
    if (_lightYieldCb) _lightYieldCb();


}

bool WebManager::rejectIfTouchPriority() {
    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "5");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
        return true;
    }
    return false;
}

bool WebManager::isHandlerOvertime() {
    /* Wrap-safe: veja comentário em timeReached() (SystemDefs.h). */
    return (_handlerDeadline > 0 && timeReached(_handlerDeadline));
}


#include <pico/time.h>

/*
 * SendGuard — alimenta o watchdog durante chamadas bloqueantes de envio.
 *
 * Enquanto _sendGuardActive=true, o timer alimenta o watchdog a cada 2 s
 * (até WDT_FEED_MAX_WINDOW_MS). Se esse teto é atingido, sinaliza aborto
 * limpo via _sendGuardExpired — consultado por isClientGone(), fazendo
 * com que safeSend() retorne false e o handler encerre graciosamente
 * em vez de ser morto pelo watchdog.
 *
 * _sendGuardExpired tem ligação externa para que isClientGone() (inline
 * no header) possa consultá-lo sem indirection adicional.
 */
static volatile bool _sendGuardActive = false;
volatile bool _sendGuardExpired = false;    /* extern — consumido por WebManager.h */
static volatile uint32_t _sendGuardStartMs = 0;
static struct repeating_timer _sendGuardTimer;

static bool _sendGuardTimerCallback(struct repeating_timer *t) {
    (void)t;
    if (_sendGuardActive) {
        uint32_t elapsed = millis() - _sendGuardStartMs;
        if (elapsed < WDT_FEED_MAX_WINDOW_MS) {
            watchdog_update();
        } else {
            /* Cap atingido: para de alimentar (safety net contra deadlock)
             * e sinaliza aborto limpo para o handler próximo safeSend(). */
            _sendGuardExpired = true;
        }
    }
    return true;
}

void WebManager::initSendGuardTimer() {

    add_repeating_timer_ms(-2000, _sendGuardTimerCallback, nullptr, &_sendGuardTimer);
}


struct SendGuard {
    SendGuard()  {
        _sendGuardStartMs = millis();
        _sendGuardExpired = false;  /* reset por transferência */
        _sendGuardActive = true;
    }
    ~SendGuard() { _sendGuardActive = false; }
};


bool WebManager::safeSend(const char* content) {
    if (isClientGone()) return false;


    _server.client().setTimeout(500);

    feedWatchdog();
    {
        SendGuard sg;
        _server.sendContent(content);
    }
    return !isClientGone();
}

bool WebManager::safeSend(const char* data, size_t len) {
    if (isClientGone()) return false;

    _server.client().setTimeout(500);

    feedWatchdog();
    {
        SendGuard sg;
        if (len == 0) {
            _server.sendContent("");
        } else {
            _server.sendContent(data, len);
        }
    }
    return !isClientGone();
}

bool WebManager::safeSend(const String& content) {
    if (isClientGone()) return false;

    _server.client().setTimeout(500);

    feedWatchdog();
    {
        SendGuard sg;
        _server.sendContent(content);
    }
    return !isClientGone();
}

bool WebManager::safeSend_P(const char* content) {
    if (isClientGone()) return false;

    _server.client().setTimeout(500);

    feedWatchdog();
    {
        SendGuard sg;
        _server.sendContent_P(content);
    }
    return !isClientGone();
}


void WebManager::detectGzipSupport() {
    _clientAcceptsGzip = false;
    if (_server.hasHeader("Accept-Encoding")) {
        String ae = _server.header("Accept-Encoding");
        _clientAcceptsGzip = (ae.indexOf("gzip") >= 0);
    }
}

bool WebManager::safeSend_GZ(const uint8_t* gz_data, size_t gz_len) {
    if (isClientGone()) return false;

    _server.client().setTimeout(500);


    const size_t CHUNK = 512;
    size_t sent = 0;
    while (sent < gz_len) {
        if (isClientGone()) return false;
        feedWatchdog();
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
    _server.setContentLength(f.size());
    _server.send(200, contentType, "");

    _server.client().setTimeout(500);

    bool hasMore = true;
    while (hasMore) {
        if (isClientGone() || isHandlerOvertime()) {
            LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_FILE, 0, "");
            return;
        }

        size_t n = 0;
        {
            ReadGuard rg(_storageRef);
            if (f.available()) n = f.read(buf, CHUNK);
            hasMore = f.available();
        }
        if (n > 0) safeSend((const char*)buf, n);
        feedWatchdog();
    }
    safeSend("");
}

void WebManager::update() {
    _clientAcceptsGzip = false;

    uint32_t handlerStart = millis();
    _handlerDeadline = handlerStart + 6000;

    _server.handleClient();

    _handlerDeadline = 0;
}

void WebManager::handleNotFound() {
    String host = _server.hostHeader();
    String myIP = _netRef->getIpAddress();

    if (host != myIP && !host.endsWith(".local")) {
        _server.sendHeader("Location", "http://" + myIP + "/network", true);
        _server.send(302, "text/plain", "Redirecting...");
        return;
    }
    _server.send(404, "text/plain", "404: Not Found");
}

void WebManager::clearStaleSessions() {
    uint32_t now = millis();
    for (int i = 0; i < 3; i++) {
        if (_activeSessions[i].token != "") {
            if (now - _activeSessions[i].lastActivity > 900000) {
                LOG_CODE(LOG_INFO, "SEC", SEC_SESSION_EXPIRE, i, String(TRL("Session expired: ", "Sessao expirada: ")) + _activeSessions[i].username);

                memset((void*)_activeSessions[i].token.begin(), 0, _activeSessions[i].token.length());
                _activeSessions[i].token = "";
            }
        }
    }
}

uint16_t WebManager::getAuthPerms() {
    clearStaleSessions();

    if (!_server.hasHeader("Cookie")) return 0;
    String cookie = _server.header("Cookie");

    for (int i = 0; i < 3; i++) {
        if (_activeSessions[i].token != "" && cookie.indexOf("SIMUTSESS=" + _activeSessions[i].token) != -1) {
            _activeSessions[i].lastActivity = millis();
            _currentUserId = _activeSessions[i].userId;
            _currentUserName = _activeSessions[i].username;
            _currentUserPerms = _activeSessions[i].perms;
            return _currentUserPerms;
        }
    }
    return 0;
}

bool WebManager::isPasswordChangeRequired() {
    if (_currentUserId >= 0 && _currentUserId < MAX_USERS) {
        return _storageRef->getConfig().users[_currentUserId].mustChangePassword;
    }
    return false;
}


bool WebManager::serveProtectedPage(uint16_t requiredPerm, const uint8_t* gz_data, size_t gz_len) {
    uint16_t perms = getAuthPerms();
    if (perms == 0) {
        _server.sendHeader("Location", "/login", true);
        _server.send(302, "text/plain", "");
        return false;
    }
    if (isPasswordChangeRequired()) {
        _server.sendHeader("Location", "/force_chpass", true);
        _server.send(302, "text/plain", "");
        return false;
    }
    if (!(perms & requiredPerm)) {
        LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId, _currentUserName);
        _server.send(403, "text/html", "<h2>Access Denied</h2>");
        return false;
    }
    _server.sendHeader("Cache-Control", "public, max-age=3600");
    _server.sendHeader("Content-Encoding", "gzip");
    _server.setContentLength(gz_len);
    _server.send(200, "text/html", "");
    safeSend_GZ(gz_data, gz_len);
    return true;
}

void WebManager::handleLogin() {
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "0");

    _server.sendHeader("Content-Encoding", "gzip");
    _server.setContentLength(WebUI_GZ::LOGIN_PAGE_GZ_LEN);
    _server.send(200, "text/html", "");
    safeSend_GZ(WebUI_GZ::LOGIN_PAGE_GZ, WebUI_GZ::LOGIN_PAGE_GZ_LEN);
}

void WebManager::handleRoot()    { serveProtectedPage(PERM_DASHBOARD, WebUI_GZ::DASH_PAGE_GZ, WebUI_GZ::DASH_PAGE_GZ_LEN); }
void WebManager::handleHistory() { serveProtectedPage(PERM_HISTORY | PERM_LOGS, WebUI_GZ::HIST_PAGE_GZ, WebUI_GZ::HIST_PAGE_GZ_LEN); }
void WebManager::handleConfig()  { serveProtectedPage(PERM_SYS_CONFIG, WebUI_GZ::CFG_PAGE_GZ, WebUI_GZ::CFG_PAGE_GZ_LEN); }
void WebManager::handleNetwork() { serveProtectedPage(PERM_NET_CONFIG, WebUI_GZ::NET_PAGE_GZ, WebUI_GZ::NET_PAGE_GZ_LEN); }
void WebManager::handleUsers()   { serveProtectedPage(PERM_USER_MGR, WebUI_GZ::USR_PAGE_GZ, WebUI_GZ::USR_PAGE_GZ_LEN); }
void WebManager::handleFiles()   { serveProtectedPage(PERM_FILE_READ, WebUI_GZ::FILE_PAGE_GZ, WebUI_GZ::FILE_PAGE_GZ_LEN); }
void WebManager::handleAlarms()  { serveProtectedPage(PERM_SYS_CONFIG, WebUI_GZ::ALARMS_PAGE_GZ, WebUI_GZ::ALARMS_PAGE_GZ_LEN); }
void WebManager::handleLicense() { serveProtectedPage(PERM_DASHBOARD, WebUI_GZ::LICENSE_PAGE_GZ, WebUI_GZ::LICENSE_PAGE_GZ_LEN); }

void WebManager::handleForceChpass() {
    if (getAuthPerms() == 0) { _server.sendHeader("Location", "/login", true); _server.send(302, "text/plain", ""); return; }
    if (!isPasswordChangeRequired()) { _server.sendHeader("Location", "/", true); _server.send(302, "text/plain", ""); return; }

    _server.sendHeader("Content-Encoding", "gzip");
    _server.setContentLength(WebUI_GZ::FORCE_CHPASS_PAGE_GZ_LEN);
    _server.send(200, "text/html", "");
    safeSend_GZ(WebUI_GZ::FORCE_CHPASS_PAGE_GZ, WebUI_GZ::FORCE_CHPASS_PAGE_GZ_LEN);
}


void WebManager::handleApiPerms() {
    uint16_t perms = getAuthPerms();
    if (perms == 0) { _server.send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }

    bool ntpOk = _netRef->isTimeSynced();
    time_t now = time(nullptr);
    char json[224];
    snprintf(json, sizeof(json),
             "{\"user\":\"%s\",\"perms\":%u,\"ntp\":%d,\"time\":%lu,\"version\":\"%s\"}",
             _currentUserName.c_str(), perms, ntpOk ? 1 : 0, (unsigned long)now, SIMUT_VERSION);
    _server.send(200, "application/json", json);
}

void WebManager::handleApiNetwork() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_NET_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    SystemConfig& cfg = _storageRef->getConfig();

    WebConfigData* w = reinterpret_cast<WebConfigData*>(
        cfg.reserved + WEB_CONFIG_OFFSET);
    uint16_t currentPort = (w->port > 0) ? w->port : WEB_DEFAULT_PORT;

    /* F-NET-TIME.3a: expõe flags do overlay NetworkTimeData para a UI. */
    char json[640];
    snprintf(json, sizeof(json),
        "{\"connected\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\","
        "\"dns\":\"%s\",\"mac\":\"%s\",\"ssid\":\"%s\",\"use_dhcp\":%s,"
        "\"static_ip\":\"%s\",\"static_mask\":\"%s\",\"static_gw\":\"%s\","
        "\"static_dns\":\"%s\",\"dns_auto\":%s,\"dns2\":\"%s\","
        "\"ntp_server\":\"%s\",\"ntp_enabled\":%s,\"web_port\":%u}",
        _netRef->isConnected() ? "true" : "false",
        _netRef->getIpAddress().c_str(),
        _netRef->getSubnetMask().c_str(),
        _netRef->getGateway().c_str(),
        _netRef->getDns().c_str(),
        _netRef->getMacAddress().c_str(),
        cfg.wifiSsid,
        cfg.useDhcp ? "true" : "false",
        cfg.staticIp, cfg.staticMask, cfg.staticGateway, cfg.staticDns,
        _storageRef->isDnsAuto() ? "true" : "false",
        _storageRef->getSecondaryDns(),
        cfg.ntpServer,
        _storageRef->isNtpEnabled() ? "true" : "false",
        (unsigned)currentPort);

    _server.send(200, "application/json", json);
}


String WebManager::jsonEscape(const char* src) {
    String out;
    out.reserve(strlen(src) + 16);
    while (*src) {
        switch (*src) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += *src;   break;
        }
        src++;
    }
    return out;
}

void WebManager::handleApiConfig() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    SystemConfig& cfg = _storageRef->getConfig();

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    char buf[256];

    snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"tz\":%d,\"log\":%s,\"res\":%d,\"s_int\":%lu,"
        "\"t_transport\":%d,\"t_sec\":%s,"
        "\"ntp_enabled\":%s,\"now_epoch\":%lu,",
        jsonEscape(cfg.deviceName).c_str(), cfg.timezoneOffset,
        cfg.loggingEnabled ? "true" : "false", cfg.ds18Resolution,
        (unsigned long)cfg.sampleIntervalMs, cfg.telTransport,
        cfg.telEncryption ? "true" : "false",
        _storageRef->isNtpEnabled() ? "true" : "false",
        (unsigned long)time(nullptr));
    if (!safeSend(buf)) return;

    snprintf(buf, sizeof(buf),
        "\"t_srv\":\"%s\",\"t_port\":%u,\"t_path\":\"%s\",\"t_key\":\"%s\",",
        jsonEscape(cfg.telServer).c_str(), cfg.telPort,
        jsonEscape(cfg.telPath).c_str(), jsonEscape(cfg.telApiKey).c_str());
    if (!safeSend(buf)) return;

    snprintf(buf, sizeof(buf),
        "\"m_topic\":\"%s\",\"m_cid\":\"%s\",\"m_user\":\"%s\","
        "\"m_qos\":%d,\"m_retain\":%s,\"m_ka\":%u,",
        jsonEscape(cfg.mqttTopic).c_str(), jsonEscape(cfg.mqttClientId).c_str(),
        jsonEscape(cfg.mqttUser).c_str(), cfg.mqttQos,
        cfg.mqttRetain ? "true" : "false", cfg.mqttKeepAlive);
    if (!safeSend(buf)) return;

    snprintf(buf, sizeof(buf),
        "\"t_int\":%lu,\"t_bat\":%d,\"t_mode\":%d,",
        (unsigned long)cfg.telInterval, cfg.telBatchSize, cfg.telMode);
    if (!safeSend(buf)) return;

    safeSend("\"t_glob\":\"");
    if (!safeSend(jsonEscape(cfg.telGlobalTemplate).c_str())) return;
    safeSend("\",\"t_line\":\"");
    if (!safeSend(jsonEscape(cfg.telLineTemplate).c_str())) return;
    safeSend("\",\"t_sep\":\"");
    if (!safeSend(jsonEscape(cfg.telLineSeparator).c_str())) return;
    safeSend("\",\"serial\":\"");
    if (!safeSend(jsonEscape(_storageRef->getBoardSerialNumber().c_str()).c_str())) return;
    safeSend("\",\"sensors\":[");
    for (int i = 0; i < MAX_SENSORS; i++) {
        snprintf(buf, sizeof(buf), "%s{\"hwid\":\"%s\",\"active\":%s}",
                 i == 0 ? "" : ",",
                 jsonEscape(cfg.sensors[i].hwId).c_str(),
                 cfg.sensors[i].active ? "true" : "false");
        if (!safeSend(buf)) return;
    }
    safeSend("]}");
}

void WebManager::handleApiUsers() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_USER_MGR)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    SystemConfig& cfg = _storageRef->getConfig();

    char json[512];
    int pos = 0;
    json[pos++] = '[';

    bool first = true;
    for (int i = 0; i < MAX_USERS; i++) {
        if (!cfg.users[i].active) continue;
        if (!first) json[pos++] = ',';
        first = false;
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"id\":%d,\"name\":\"%s\",\"perms\":%u}",
            i, cfg.users[i].username, cfg.users[i].permissions);
    }
    json[pos++] = ']';
    json[pos] = '\0';

    _server.send(200, "application/json", json);
}

void WebManager::handleApiThemes() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_DASHBOARD)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }

    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    safeSend("[");
    char entry[64];

    for (int i = 0; i < getThemeCount(); i++) {
        char cleanName[32];
        int cn = 0;
        const char* p = availableThemes[i].displayName;
        if (p) {
            while (*p && cn < (int)sizeof(cleanName) - 1) {
                unsigned char c = (unsigned char)(*p);
                if (c >= 32 && c != '\"' && c != '\\') cleanName[cn++] = (char)c;
                p++;
            }
        }
        if (cn == 0) cn = snprintf(cleanName, sizeof(cleanName), "Tema %d", i);
        cleanName[cn] = '\0';

        snprintf(entry, sizeof(entry), "%s{\"id\":%d,\"name\":\"%s\"}", i > 0 ? "," : "", i, cleanName);
        if (!safeSend(entry)) return;
    }

    safeSend("]");
}


void WebManager::handleApiAlarms() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();


    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");


    if (!safeSend("{\"sensors\":[")) return;

    bool first = true;


    if (cfg.ambientSensor.active) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"idx\":-1,\"name\":\"Ambient\",\"type\":\"DHT22\",\"gpio\":%d,"
            "\"has_hum\":true,"
            "\"tmin\":%.1f,\"tmax\":%.1f,\"hmin\":%.1f,\"hmax\":%.1f,"
            "\"active\":%s}",
            cfg.ambientSensor.gpio,
            cfg.ambientSensor.tempMin, cfg.ambientSensor.tempMax,
            cfg.ambientSensor.humMin,  cfg.ambientSensor.humMax,
            cfg.ambientSensor.alarmsActive ? "true" : "false");
        if (!safeSend(buf)) return;
        first = false;
    }


    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!cfg.sensors[i].active) continue;
        if (!first) { if (!safeSend(",")) return; }
        first = false;


        bool hasHum = (cfg.sensors[i].rom[0] != 0x28);
        const char* typeName = hasHum ? "DHT22" : "DS18B20";


        String sName = cfg.sensors[i].friendlyName;
        sName.replace("\"", "\\\"");

        char buf[320];
        snprintf(buf, sizeof(buf),
            "{\"idx\":%d,\"name\":\"%s\",\"type\":\"%s\",\"gpio\":%d,"
            "\"has_hum\":%s,"
            "\"tmin\":%.1f,\"tmax\":%.1f,\"hmin\":%.1f,\"hmax\":%.1f,"
            "\"active\":%s}",
            i, sName.c_str(), typeName, cfg.sensors[i].gpio,
            hasHum ? "true" : "false",
            cfg.sensors[i].tempMin, cfg.sensors[i].tempMax,
            cfg.sensors[i].humMin,  cfg.sensors[i].humMax,
            cfg.sensors[i].alarmsActive ? "true" : "false");
        if (!safeSend(buf)) return;
    }

    if (!safeSend("],")) return;


    SoundSettingsState snd = _soundRef->getSettingsState();
    char sndBuf[280];
    snprintf(sndBuf, sizeof(sndBuf),
        "\"sounds\":{\"touch\":%s,\"confirm\":%s,\"error\":%s,"
        "\"alarm\":%s,\"web\":%s,\"mute\":%s,\"volume\":%d,\"alarmVolume\":%d,"
        "\"melTouch\":%d,\"melConfirm\":%d,\"melError\":%d,\"melAlarm\":%d}}",
        snd.touchEnabled   ? "true" : "false",
        snd.confirmEnabled ? "true" : "false",
        snd.errorEnabled   ? "true" : "false",
        snd.alarmEnabled   ? "true" : "false",
        snd.webEnabled     ? "true" : "false",
        snd.muted          ? "true" : "false",
        snd.volume,
        snd.alarmVolume,
        snd.touchMelody, snd.confirmMelody,
        snd.errorMelody, snd.alarmMelody);
    safeSend(sndBuf);
    safeSend("");
}

/* handleApiSaveAlarms removido em U24 Phase A.2 — substituido por handleApiCommitAll. */


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

void WebManager::handleApiLoginInit() {
    uint32_t clientIP = (uint32_t)_server.client().remoteIP();

    int slot = -1;
    int oldest = 0;
    for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
        if (_loginStates[i].ip == clientIP) { slot = i; break; }
        if (_loginStates[i].lastActivity < _loginStates[oldest].lastActivity) oldest = i;
    }
    if (slot == -1) {
        for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
            if (_loginStates[i].ip == 0) { slot = i; break; }
        }
        if (slot == -1) slot = oldest;
        _loginStates[slot].ip = clientIP;
        _loginStates[slot].failCount = 0;
        _loginStates[slot].lockoutUntil = 0;
    }


    _loginStates[slot].nonce = generateSecureToken();
    _loginStates[slot].nonceCreatedAt = millis();
    _loginStates[slot].lastActivity = millis();

    uint32_t lockSec = 0;
    bool locked = false;
    /* Wrap-safe: millis() sofre wrap a cada ~49,7d; comparações diretas invertem. */
    if (_loginStates[slot].lockoutUntil > 0 && !timeReached(_loginStates[slot].lockoutUntil)) {
        lockSec = timeRemaining(_loginStates[slot].lockoutUntil) / 1000;
        locked = true;
    }

    char json[128];
    snprintf(json, sizeof(json), "{\"nonce\":\"%s\",\"locked\":%s,\"lockSec\":%lu}",
             _loginStates[slot].nonce.c_str(), locked ? "true" : "false", (unsigned long)lockSec);

    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/json", json);
}

void WebManager::handleApiLogin() {
    uint32_t clientIP = (uint32_t)_server.client().remoteIP();

    int ls = -1;
    for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
        if (_loginStates[i].ip == clientIP) { ls = i; break; }
    }

    if (ls >= 0 && _loginStates[ls].lockoutUntil > 0 && !timeReached(_loginStates[ls].lockoutUntil)) {
        uint32_t rem = timeRemaining(_loginStates[ls].lockoutUntil) / 1000;
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)rem);
        _server.send(403, "application/json", buf);
        return;
    }

    String expectedNonce = (ls >= 0) ? _loginStates[ls].nonce : "";


    bool nonceExpired = (ls >= 0) && (_loginStates[ls].nonceCreatedAt > 0) &&
                        (millis() - _loginStates[ls].nonceCreatedAt > NONCE_LIFETIME_MS);

    if (!_server.hasArg("nonce") || _server.arg("nonce") != expectedNonce || expectedNonce == "" || nonceExpired) {
        if (ls >= 0) {
            _loginStates[ls].nonce = "";
            if (nonceExpired) {
                _loginStates[ls].failCount++;
                uint32_t penaltyMs = (1 << _loginStates[ls].failCount) * 1000;
                if (penaltyMs > 300000) penaltyMs = 300000;
                _loginStates[ls].lockoutUntil = millis() + penaltyMs;
            }
        }
        LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0,
                 nonceExpired ? "Login Rejected: Nonce Expired" : "Login Rejected: Invalid Nonce");
        if (ls >= 0 && _loginStates[ls].lockoutUntil > 0 && !timeReached(_loginStates[ls].lockoutUntil)) {
            uint32_t rem = timeRemaining(_loginStates[ls].lockoutUntil) / 1000;
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)rem);
            _server.send(401, "application/json", buf);
        } else {
            _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
        }
        return;
    }
    if (ls >= 0) _loginStates[ls].nonce = "";

    if (!_server.hasArg("user") || !_server.hasArg("pass")) {
        _server.send(400, "application/json", "{\"ok\":false,\"err\":1}");
        return;
    }

    String u = _server.arg("user");
    String p = _server.arg("pass");

    /* D13: Validar tamanho antes de passar para hashPassword (2500 rounds).
     * Username > 31 ou password > 128 → rejeitar imediatamente. */
    if (!isValidName(u.c_str(), 31) || p.length() > 128) {
        if (ls >= 0) {
            _loginStates[ls].failCount++;
            uint32_t penaltyMs = (1 << _loginStates[ls].failCount) * 1000;
            if (penaltyMs > 300000) penaltyMs = 300000;
            _loginStates[ls].lockoutUntil = millis() + penaltyMs;
        }
        LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Login Rejected: Invalid Input Size", "Login rejeitado: tamanho invalido"));
        _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();

    int foundId = -1;

    String inputHash = _storageRef->hashPassword(u, p);

    for (int i = 0; i < MAX_USERS; i++) {
        if (cfg.users[i].active && String(cfg.users[i].username) == u) {
            bool passValid = false;

            if (String(cfg.users[i].username) == "admin") {

                if (secureCompare(String(cfg.users[i].password), inputHash)) passValid = true;
            } else {
                if (cfg.users[i].mustChangePassword && String(cfg.users[i].password) == "*PENDING*") {
                    String expectedFrontendHash = getDynamicExpectedHash(u);

                    String expectedFinalHash = _storageRef->hashPassword(u, expectedFrontendHash);

                    if (secureCompare(inputHash, expectedFinalHash)) passValid = true;
                } else {

                    if (secureCompare(String(cfg.users[i].password), inputHash)) passValid = true;
                }
            }

            if (passValid) {
                foundId = i;
                break;
            }
        }
    }

    if (foundId >= 0) {
        clearStaleSessions();
        int slot = -1;

        for (int i = 0; i < 3; i++) {
            if (_activeSessions[i].token != "" && _activeSessions[i].userId == foundId) {
                slot = i; break;
            }
        }

        if (slot == -1) {
            for (int i = 0; i < 3; i++) {
                if (_activeSessions[i].token == "") {
                    slot = i; break;
                }
            }
        }

        if (slot == -1) {
            LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Login Rejected: Max Sessions Reached", "Login rejeitado: limite de sessoes"));
            _server.send(403, "application/json", "{\"ok\":false,\"err\":3}");
            return;
        }

        if (ls >= 0) {
            _loginStates[ls].failCount = 0;
            _loginStates[ls].lockoutUntil = 0;
        }


        String newToken = generateSecureToken();

        _activeSessions[slot].token = newToken;
        _activeSessions[slot].userId = foundId;
        _activeSessions[slot].username = u;
        _activeSessions[slot].perms = cfg.users[foundId].permissions;
        _activeSessions[slot].lastActivity = millis();

        _currentUserId = foundId;
        _currentUserName = u;
        _currentUserPerms = _activeSessions[slot].perms;

        LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, foundId, String(TRL("Login OK: ", "Login OK: ")) + u);
        if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);


        if (_displayRef) _displayRef->setWebNotification(u.c_str());

        String cookieFlags = "SIMUTSESS=" + newToken + "; Path=/; HttpOnly; SameSite=Strict";
        if (_storageRef->getConfig().useHttps) cookieFlags += "; Secure";
        _server.sendHeader("Set-Cookie", cookieFlags);

        const char* redirect = cfg.users[foundId].mustChangePassword ? "/force_chpass" : "/";
        char resp[64];
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"redirect\":\"%s\"}", redirect);
        _server.send(200, "application/json", resp);
    } else {
        if (ls >= 0) {
            _loginStates[ls].failCount++;
            uint32_t penaltyMs = (1 << _loginStates[ls].failCount) * 1000;
            if (penaltyMs > 300000) penaltyMs = 300000;
            _loginStates[ls].lockoutUntil = millis() + penaltyMs;
            LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, String(TRL("Login Failed: ", "Login falhou: ")) + u);
            if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_ERROR);

            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)(penaltyMs/1000));
            _server.send(401, "application/json", buf);
        } else {
            _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
        }
    }
}

void WebManager::handleLogout() {
    if (_server.hasHeader("Cookie")) {
        String cookie = _server.header("Cookie");
        for (int i = 0; i < 3; i++) {
            if (_activeSessions[i].token != "" && cookie.indexOf("SIMUTSESS=" + _activeSessions[i].token) != -1) {
                LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, 0, String(TRL("Logout: ", "Logout: ")) + _activeSessions[i].username);

                memset((void*)_activeSessions[i].token.begin(), 0, _activeSessions[i].token.length());
                _activeSessions[i].token = "";
                break;
            }
        }
    }

    _currentUserId = -1;
    _currentUserName = "";
    _currentUserPerms = 0;

    _server.sendHeader("Set-Cookie", "SIMUTSESS=0; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; SameSite=Strict");
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Location", "/login", true);
    _server.send(302, "text/plain", "");
}

void WebManager::handleApiSecStatus() {
    if (!(getAuthPerms() & PERM_USER_MGR)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }

    uint32_t now = millis();
    char buf[512];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"slots\":[");

    bool first = true;
    for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
        if (_loginStates[i].ip == 0) continue;
        if (!first) buf[pos++] = ',';
        first = false;

        uint32_t ip = _loginStates[i].ip;
        uint32_t lockSec = 0;
        bool locked = (_loginStates[i].lockoutUntil > 0 && !timeReached(_loginStates[i].lockoutUntil));
        if (locked) lockSec = timeRemaining(_loginStates[i].lockoutUntil) / 1000;
        uint32_t ageSec = (now - _loginStates[i].lastActivity) / 1000;

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"ip\":\"%lu.%lu.%lu.%lu\",\"fails\":%u,\"lockSec\":%lu,\"ageSec\":%lu}",
            (unsigned long)(ip & 0xFF), (unsigned long)((ip >> 8) & 0xFF),
            (unsigned long)((ip >> 16) & 0xFF), (unsigned long)((ip >> 24) & 0xFF),
            _loginStates[i].failCount, (unsigned long)lockSec, (unsigned long)ageSec);

        if (pos >= (int)sizeof(buf) - 2) break;
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/json", buf);
}

void WebManager::handleApiForceChpass() {
    if (getAuthPerms() == 0 || !isPasswordChangeRequired()) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (rejectIfTouchPriority()) return;

    String p1 = _server.arg("p1");
    String p2 = _server.arg("p2");

    if (p1.length() < 8 || p1 != p2) {
        if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_ERROR);
        _server.send(400, "application/json", "{\"error\":\"Invalid payload\"}");
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();

    String hashedNewPass = _storageRef->hashPassword(_currentUserName, p1);

    safeCopy(cfg.users[_currentUserId].password, hashedNewPass.c_str(), sizeof(cfg.users[_currentUserId].password));
    cfg.users[_currentUserId].mustChangePassword = false;
    _storageRef->saveConfiguration();

    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, String(TRL("Password Reset Success: ", "Reset de senha bem-sucedido: ")) + _currentUserName);

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}


/*
 * handleSaveSystem — versao minimal pos-U24.
 * Versao pre-U24 foi substituida por handleApiCommitAll. Mantida apenas
 * pra dashboard trocar tema (aplicacao imediata, sem reboot).
 */
void WebManager::handleSaveSystem() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }
    SystemConfig& cfg = _storageRef->getConfig();
    if (_server.hasArg("theme")) {
        int t = _server.arg("theme").toInt();
        if (t >= 0 && t < getThemeCount() && cfg.themeIndex != t) {
            cfg.themeIndex = t;
            loadTheme(t);
            _storageRef->saveConfiguration();
            if (_displayRef) _displayRef->refreshTheme();
        }
    }
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}


/*
 * U24 — Commit-all + reboot (save-and-restart pattern).
 *
 * Recebe POST com `_payload` = JSON contendo seções `sys` e/ou `alarms`.
 * Aplica todas as mudanças em memória, faz um único saveConfiguration(),
 * responde ao cliente, e agenda reboot. A rajada de saves concorrentes que
 * triggerava deadlocks de lockout em multicore deixa de existir — a escrita
 * acontece 1x e o sistema reinicia limpo.
 *
 * Cliente envia `_payload` urlencoded. Exemplo:
 *   _payload={"sys":{"name":"SIMUT","tz":"-3","log":"1",...}}
 *
 * Phase A.1: suporta apenas a seção `sys`. Extensão para `alarms` vem a
 * seguir (mesma abordagem de parsing usada em handleApiSaveAlarms).
 */
void WebManager::handleApiCommitAll() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }
    if (isPasswordChangeRequired()) return;
    if (rejectIfTouchPriority()) return;

    if (!_server.hasArg("_payload")) {
        _server.send(400, "application/json", "{\"error\":\"Missing _payload\"}");
        return;
    }

    String body = _server.arg("_payload");
    if (body.length() == 0 || body.length() > 6144) {
        _server.send(400, "application/json", "{\"error\":\"Bad payload\"}");
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();
    bool themeChanged = false;

    /* ── Seção sys: extrai o sub-objeto e aplica cada campo ───────────────
     * Parser manual por simplicidade. Formato esperado:
     *   "sys":{"name":"...","tz":"-3","log":"1",...}
     * Cada campo pode vir como string entre aspas ou número bruto. */
    int sysStart = body.indexOf("\"sys\"");
    if (sysStart >= 0) {
        int objStart = body.indexOf('{', sysStart);
        int objEnd = -1;
        if (objStart >= 0) {
            /* Busca o '}' pareado levando em conta nesting. */
            int depth = 0;
            for (int i = objStart; i < (int)body.length(); i++) {
                char c = body.charAt(i);
                if (c == '{') depth++;
                else if (c == '}') { depth--; if (depth == 0) { objEnd = i; break; } }
            }
        }
        if (objStart >= 0 && objEnd > objStart) {
            String sys = body.substring(objStart, objEnd + 1);

            /* Helper: extrai valor string entre aspas de "key":"value". */
            auto getStr = [&](const char* key) -> String {
                String pat = String("\"") + key + "\":\"";
                int p = sys.indexOf(pat);
                if (p < 0) return String();
                int vStart = p + pat.length();
                int vEnd = sys.indexOf('"', vStart);
                if (vEnd < 0) return String();
                return sys.substring(vStart, vEnd);
            };
            /* Helper: extrai número bruto (não quoted) de "key":NNN.
             * Usado quando cliente envia int/float sem aspas. */
            auto getNum = [&](const char* key) -> String {
                String pat = String("\"") + key + "\":";
                int p = sys.indexOf(pat);
                if (p < 0) return String();
                int vStart = p + pat.length();
                /* Pula aspas se houver */
                if (sys.charAt(vStart) == '"') {
                    int vEnd = sys.indexOf('"', vStart + 1);
                    if (vEnd < 0) return String();
                    return sys.substring(vStart + 1, vEnd);
                }
                /* Lê até vírgula ou } */
                int vEnd = vStart;
                while (vEnd < (int)sys.length() && sys.charAt(vEnd) != ',' && sys.charAt(vEnd) != '}') vEnd++;
                return sys.substring(vStart, vEnd);
            };
            auto has = [&](const char* key) -> bool {
                String pat = String("\"") + key + "\":";
                return sys.indexOf(pat) >= 0;
            };

            /* Aplica cada campo (mirror of handleSaveSystem) */
            if (has("name")) {
                String n = getStr("name"); n.trim();
                if (n.length() > 0 && isValidName(n.c_str())) {
                    safeCopy(cfg.deviceName, n.c_str(), sizeof(cfg.deviceName));
                }
            }
            if (has("tz")) {
                cfg.timezoneOffset = (int8_t)getNum("tz").toInt();
                NetworkManager::applyTimezone(cfg.timezoneOffset);
            }
            if (has("log"))       cfg.loggingEnabled = (getNum("log") != "0");
            if (has("t_sec"))     cfg.telEncryption = (getNum("t_sec") != "0");
            if (has("t_key"))     safeCopy(cfg.telApiKey, getStr("t_key").c_str(), sizeof(cfg.telApiKey));
            if (has("res"))       { int r = getNum("res").toInt(); if (r >= 9 && r <= 12) cfg.ds18Resolution = (uint8_t)r; }
            if (has("s_int"))     cfg.sampleIntervalMs = getNum("s_int").toInt();
            if (has("t_srv"))     safeCopy(cfg.telServer, getStr("t_srv").c_str(), sizeof(cfg.telServer));
            if (has("t_port"))    { int p = getNum("t_port").toInt(); if (isInRange(p, 1, 65535)) cfg.telPort = (uint16_t)p; }
            if (has("t_path"))    safeCopy(cfg.telPath, getStr("t_path").c_str(), sizeof(cfg.telPath));
            if (has("t_int"))     cfg.telInterval = getNum("t_int").toInt();
            if (has("t_bat"))     cfg.telBatchSize = (uint8_t)getNum("t_bat").toInt();
            if (has("t_mode"))    cfg.telMode = (uint8_t)getNum("t_mode").toInt();
            if (has("t_transport")) cfg.telTransport = (uint8_t)getNum("t_transport").toInt();
            if (has("m_topic"))   safeCopy(cfg.mqttTopic, getStr("m_topic").c_str(), sizeof(cfg.mqttTopic));
            if (has("m_cid"))     safeCopy(cfg.mqttClientId, getStr("m_cid").c_str(), sizeof(cfg.mqttClientId));
            if (has("m_user"))    safeCopy(cfg.mqttUser, getStr("m_user").c_str(), sizeof(cfg.mqttUser));
            if (has("m_qos"))     cfg.mqttQos = (uint8_t)getNum("m_qos").toInt();
            if (has("m_retain"))  cfg.mqttRetain = (getNum("m_retain") != "0");
            if (has("m_ka"))      { int ka = getNum("m_ka").toInt(); if (isInRange(ka, 5, 600)) cfg.mqttKeepAlive = (uint16_t)ka; }
            if (has("t_glob"))    safeCopy(cfg.telGlobalTemplate, getStr("t_glob").c_str(), sizeof(cfg.telGlobalTemplate));
            if (has("t_line"))    safeCopy(cfg.telLineTemplate, getStr("t_line").c_str(), sizeof(cfg.telLineTemplate));
            if (has("t_sep"))     safeCopy(cfg.telLineSeparator, getStr("t_sep").c_str(), sizeof(cfg.telLineSeparator));
            /* F-NET-TIME.3a: flag NTP enable/disable (overlay NetworkTimeData). */
            if (has("ntp_enabled")) _storageRef->setNtpEnabled(getNum("ntp_enabled") != "0");
        }
    }

    /* ── Seção alarms: formato {sensors:[{idx,active,tmin,tmax,hmin,hmax}],
     * sounds:{...}}. Mesmo parsing manual usado em handleApiSaveAlarms. */
    int almStart = body.indexOf("\"alarms\"");
    if (almStart >= 0) {
        /* Sensors array */
        int sensorsStart = body.indexOf("\"sensors\"", almStart);
        int arrStart = (sensorsStart >= 0) ? body.indexOf('[', sensorsStart) : -1;
        int arrEnd = (arrStart >= 0) ? body.indexOf(']', arrStart) : -1;
        if (arrStart >= 0 && arrEnd > arrStart) {
            String arrStr = body.substring(arrStart, arrEnd + 1);
            int objStart = 0;
            while ((objStart = arrStr.indexOf('{', objStart)) >= 0) {
                int objEnd = arrStr.indexOf('}', objStart);
                if (objEnd < 0) break;
                String obj = arrStr.substring(objStart, objEnd + 1);

                int idxPos = obj.indexOf("\"idx\"");
                if (idxPos < 0) { objStart = objEnd + 1; continue; }
                int idxColon = obj.indexOf(':', idxPos);
                int idx = obj.substring(idxColon + 1).toInt();

                SensorRecord* rec = nullptr;
                if (idx == -1) rec = &cfg.ambientSensor;
                else if (idx >= 0 && idx < MAX_SENSORS && cfg.sensors[idx].active) rec = &cfg.sensors[idx];

                if (rec) {
                    auto extractFloat = [&](const char* key) -> float {
                        int kp = obj.indexOf(key);
                        if (kp < 0) return NAN;
                        int cp = obj.indexOf(':', kp + strlen(key));
                        if (cp < 0) return NAN;
                        return obj.substring(cp + 1).toFloat();
                    };
                    float tmin = extractFloat("\"tmin\"");
                    float tmax = extractFloat("\"tmax\"");
                    float hmin = extractFloat("\"hmin\"");
                    float hmax = extractFloat("\"hmax\"");
                    if (!isnan(tmin)) rec->tempMin = tmin;
                    if (!isnan(tmax)) rec->tempMax = tmax;
                    if (!isnan(hmin)) rec->humMin  = hmin;
                    if (!isnan(hmax)) rec->humMax  = hmax;
                    if (rec->tempMin >= rec->tempMax) {
                        rec->tempMax = roundf((rec->tempMin + 0.1f) * 10.0f) / 10.0f;
                    }
                    if (rec->humMin >= rec->humMax) {
                        rec->humMax = roundf((rec->humMin + 0.1f) * 10.0f) / 10.0f;
                        if (rec->humMax > 100.0f) { rec->humMax = 100.0f; rec->humMin = 99.9f; }
                    }
                    rec->alarmsActive = (obj.indexOf("\"active\":true") >= 0);
                }
                objStart = objEnd + 1;
            }
        }

        /* Sounds: mesmo parsing de handleApiSaveAlarms, aplica via
         * SoundSettingsState + fillConfig pra escrever no packed layout. */
        int soundsStart = body.indexOf("\"sounds\"", almStart);
        if (soundsStart >= 0) {
            int sObjStart = body.indexOf('{', soundsStart);
            int sObjEnd   = body.indexOf('}', sObjStart);
            if (sObjStart >= 0 && sObjEnd > sObjStart) {
                String sObj = body.substring(sObjStart, sObjEnd + 1);
                SoundSettingsState snd;
                snd.touchEnabled   = (sObj.indexOf("\"touch\":true")   >= 0);
                snd.confirmEnabled = (sObj.indexOf("\"confirm\":true") >= 0);
                snd.errorEnabled   = (sObj.indexOf("\"error\":true")   >= 0);
                snd.alarmEnabled   = (sObj.indexOf("\"alarm\":true")   >= 0);
                snd.webEnabled     = (sObj.indexOf("\"web\":true")     >= 0);
                snd.muted          = (sObj.indexOf("\"mute\":true")    >= 0);

                int volPos = sObj.indexOf("\"volume\"");
                if (volPos >= 0) { int vc = sObj.indexOf(':', volPos); snd.volume = (uint8_t)constrain(sObj.substring(vc + 1).toInt(), 0, 100); }
                else snd.volume = 70;

                int aVolPos = sObj.indexOf("\"alarmVolume\"");
                if (aVolPos >= 0) { int avc = sObj.indexOf(':', aVolPos); snd.alarmVolume = (uint8_t)constrain(sObj.substring(avc + 1).toInt(), 0, 100); }
                else snd.alarmVolume = 70;

                auto extractMelIdx = [&](const char* key) -> uint8_t {
                    int kp = sObj.indexOf(key);
                    if (kp < 0) return 0;
                    int cp = sObj.indexOf(':', kp);
                    if (cp < 0) return 0;
                    return (uint8_t)constrain(sObj.substring(cp + 1).toInt(), 0, 5);
                };
                snd.touchMelody   = extractMelIdx("\"melTouch\"");
                snd.confirmMelody = extractMelIdx("\"melConfirm\"");
                snd.errorMelody   = extractMelIdx("\"melError\"");
                snd.alarmMelody   = extractMelIdx("\"melAlarm\"");

                if (_soundRef) {
                    _soundRef->applySettingsState(snd);
                    SoundConfigData* sndCfg = reinterpret_cast<SoundConfigData*>(
                        cfg.reserved + sizeof(TouchCalData));
                    _soundRef->fillConfig(sndCfg);
                }
            }
        }
    }

    /* ── Seção users.actions: processa add/del/reset em ordem ───────────
     * Formato: {"users":{"actions":[{"type":"add","name":"x","perms":511},
     *                                 {"type":"del","id":3},
     *                                 {"type":"reset","id":5}]}} */
    int usrStart = body.indexOf("\"users\"");
    if (usrStart >= 0) {
        int actionsPos = body.indexOf("\"actions\"", usrStart);
        int arrStart = (actionsPos >= 0) ? body.indexOf('[', actionsPos) : -1;
        int arrEnd = (arrStart >= 0) ? body.indexOf(']', arrStart) : -1;
        if (arrStart >= 0 && arrEnd > arrStart) {
            String arr = body.substring(arrStart, arrEnd + 1);
            int objStart = 0;
            while ((objStart = arr.indexOf('{', objStart)) >= 0) {
                int objEnd = arr.indexOf('}', objStart);
                if (objEnd < 0) break;
                String obj = arr.substring(objStart, objEnd + 1);
                String type;
                int tp = obj.indexOf("\"type\":\"");
                if (tp >= 0) {
                    int vs = tp + 8;
                    int ve = obj.indexOf('"', vs);
                    if (ve > vs) type = obj.substring(vs, ve);
                }

                if (type == "add") {
                    /* find first inactive slot (skip slot 0 — admin) */
                    int slot = -1;
                    for (int i = 1; i < MAX_USERS; i++) {
                        if (!cfg.users[i].active) { slot = i; break; }
                    }
                    if (slot < 0) { objStart = objEnd + 1; continue; }

                    /* name */
                    String name;
                    int np = obj.indexOf("\"name\":\"");
                    if (np >= 0) {
                        int vs = np + 8;
                        int ve = obj.indexOf('"', vs);
                        if (ve > vs) name = obj.substring(vs, ve);
                    }
                    name.trim();
                    if (name.length() == 0 || !isValidName(name.c_str(), 15) || name.equalsIgnoreCase("admin")) {
                        objStart = objEnd + 1; continue;
                    }
                    /* dup check */
                    bool dup = false;
                    for (int i = 0; i < MAX_USERS; i++) {
                        if (cfg.users[i].active && name.equalsIgnoreCase(String(cfg.users[i].username))) { dup = true; break; }
                    }
                    if (dup) { objStart = objEnd + 1; continue; }

                    int perms = 0;
                    int pp = obj.indexOf("\"perms\":");
                    if (pp >= 0) perms = obj.substring(pp + 8).toInt();

                    safeCopy(cfg.users[slot].username, name.c_str(), sizeof(cfg.users[slot].username));
                    safeCopy(cfg.users[slot].password, "*PENDING*", sizeof(cfg.users[slot].password));
                    cfg.users[slot].permissions = (uint16_t)perms;
                    cfg.users[slot].mustChangePassword = true;
                    cfg.users[slot].active = true;
                }
                else if (type == "del" || type == "reset") {
                    int ip = obj.indexOf("\"id\":");
                    int id = (ip >= 0) ? obj.substring(ip + 5).toInt() : -1;
                    if (id > 0 && id < MAX_USERS && cfg.users[id].active) {
                        if (type == "del") {
                            cfg.users[id].active = false;
                            memset(cfg.users[id].username, 0, sizeof(cfg.users[id].username));
                            memset(cfg.users[id].password, 0, sizeof(cfg.users[id].password));
                            cfg.users[id].permissions = 0;
                        } else { /* reset */
                            safeCopy(cfg.users[id].password, "*PENDING*", sizeof(cfg.users[id].password));
                            cfg.users[id].mustChangePassword = true;
                        }
                    }
                }
                objStart = objEnd + 1;
            }
        }
    }

    /* ── Seção net: {ssid,pass,use_dhcp,ip,mask,gw,dns,ntp_server,web_port} ─ */
    uint16_t commitNewPort = 0;  /* 0 = sem mudança; != 0 = informa cliente */
    int netStart = body.indexOf("\"net\"");
    if (netStart >= 0) {
        int objStart = body.indexOf('{', netStart);
        int objEnd = -1;
        if (objStart >= 0) {
            int depth = 0;
            for (int i = objStart; i < (int)body.length(); i++) {
                char c = body.charAt(i);
                if (c == '{') depth++;
                else if (c == '}') { depth--; if (depth == 0) { objEnd = i; break; } }
            }
        }
        if (objStart >= 0 && objEnd > objStart) {
            String net = body.substring(objStart, objEnd + 1);
            auto getS = [&](const char* key) -> String {
                String pat = String("\"") + key + "\":\"";
                int p = net.indexOf(pat);
                if (p < 0) return String();
                int vs = p + pat.length();
                int ve = net.indexOf('"', vs);
                if (ve < 0) return String();
                return net.substring(vs, ve);
            };
            auto getN = [&](const char* key) -> String {
                String pat = String("\"") + key + "\":";
                int p = net.indexOf(pat);
                if (p < 0) return String();
                int vs = p + pat.length();
                if (net.charAt(vs) == '"') {
                    int ve = net.indexOf('"', vs + 1);
                    if (ve < 0) return String();
                    return net.substring(vs + 1, ve);
                }
                int ve = vs;
                while (ve < (int)net.length() && net.charAt(ve) != ',' && net.charAt(ve) != '}') ve++;
                return net.substring(vs, ve);
            };
            auto has = [&](const char* key) -> bool {
                return net.indexOf(String("\"") + key + "\":") >= 0;
            };

            if (has("ssid")) { String s = getS("ssid"); s.trim(); if (s.length() > 0) safeCopy(cfg.wifiSsid, s.c_str(), sizeof(cfg.wifiSsid)); }
            if (has("pass")) { String p = getS("pass"); p.trim(); if (p.length() > 0) safeCopy(cfg.wifiPass, p.c_str(), sizeof(cfg.wifiPass)); }
            if (has("use_dhcp")) cfg.useDhcp = (getN("use_dhcp") != "0");
            if (!cfg.useDhcp) {
                if (has("ip"))   { String s = getS("ip");   if (isValidIpv4(s.c_str())) safeCopy(cfg.staticIp, s.c_str(), sizeof(cfg.staticIp)); }
                if (has("mask")) { String s = getS("mask"); if (isValidIpv4(s.c_str())) safeCopy(cfg.staticMask, s.c_str(), sizeof(cfg.staticMask)); }
                if (has("gw"))   { String s = getS("gw");   if (isValidIpv4(s.c_str())) safeCopy(cfg.staticGateway, s.c_str(), sizeof(cfg.staticGateway)); }
                if (has("dns"))  { String s = getS("dns");  if (isValidIpv4(s.c_str())) safeCopy(cfg.staticDns, s.c_str(), sizeof(cfg.staticDns)); }
            }
            /* F-NET-TIME.3a: dns_auto + dns2 (primário manual reusa cfg.staticDns).
             * Aceita também `dns1` como atalho para `staticDns` quando user está
             * no modo manual com DHCP=true (sem os outros campos staticIp/mask/gw). */
            if (has("dns_auto")) _storageRef->setDnsAuto(getN("dns_auto") != "0");
            if (has("dns1")) { String s = getS("dns1"); if (isValidIpv4(s.c_str())) safeCopy(cfg.staticDns, s.c_str(), sizeof(cfg.staticDns)); }
            if (has("dns2")) {
                String s = getS("dns2"); s.trim();
                /* Vazio é válido (limpa secundário). Qualquer outro valor exige ser IPv4. */
                if (s.length() == 0 || isValidIpv4(s.c_str())) _storageRef->setSecondaryDns(s.c_str());
            }
            if (has("ntp_server")) {
                String ntp = getS("ntp_server"); ntp.trim();
                safeCopy(cfg.ntpServer, ntp.c_str(), sizeof(cfg.ntpServer));
                cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
            }
            if (has("web_port")) {
                WebConfigData* w = reinterpret_cast<WebConfigData*>(cfg.reserved + WEB_CONFIG_OFFSET);
                int p = getN("web_port").toInt();
                if (p >= 1 && p <= 65535) {
                    if (w->port != (uint16_t)p) commitNewPort = (uint16_t)p;
                    w->port = (uint16_t)p;
                }
            }
        }
    }

    if (themeChanged && _displayRef) _displayRef->refreshTheme();

    /* U24: mostra mensagem no display ANTES de qualquer flash I/O.
     * Core 1 vai pra tela de boot e renderiza, dando feedback visual
     * ao usuário de que o reinício está iminente.
     *
     * Tempos: ~1.5s em cada mensagem pra user ler com calma antes do
     * reboot. Alimentamos WDT em chunks pra não disparar durante o hold. */
    if (_displayRef) {
        _displayRef->setBootStatus("Aplicando configuracoes...");
        for (int i = 0; i < 15; i++) { delay(100); watchdog_update(); }
        _displayRef->setBootStatus("Reiniciando o sistema...");
        for (int i = 0; i < 15; i++) { delay(100); watchdog_update(); }
    }

    /* Bufferiza o audit log — heavy task gate faz writeCompactToFlash
     * empilhar em _pendingLogs em vez de gravar direto. Evita um flash
     * write extra antes do save; saveConfiguration drena o buffer. */
    _storageRef->lockHeavyTask();
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId,
             TRL("Admin committed changes — rebooting", "Admin aplicou alteracoes — reiniciando"));
    _storageRef->unlockHeavyTask();

    /* Single atomic save — drena pending logs + escreve config */
    bool saved = _storageRef->saveConfiguration();

    if (!saved) {
        /* Save falhou: desfaz o boot screen e devolve erro ao cliente.
         * Sistema continua vivo, user pode tentar novamente. */
        if (_displayRef) _displayRef->endBoot();
        _server.send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }

    /* Resposta ao cliente antes do reboot. Inclui newPort se porta web
     * mudou — cliente usa pra redirecionar ao novo host:porta. */
    char resp[64];
    if (commitNewPort != 0) {
        snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"newPort\":%u}", (unsigned)commitNewPort);
    } else {
        snprintf(resp, sizeof(resp), "{\"status\":\"ok\"}");
    }
    _server.send(200, "application/json", resp);
    _server.client().stop();

    /*
     * Hard reboot garantido via watchdog curto (500ms).
     * Depois de `watchdog_enable(500, 1)` e `while(1)`:
     *   - Não importa o que travar (lockout, multicore, flash GC),
     *     o WDT dispara em 500ms e reinicia o chip.
     *   - Elimina o espaço onde U21 (display congelado) ocorria entre
     *     save completo e rp2040.reboot().
     *   - Marca scratch pra autópsia NÃO reportar como HW_WATCHDOG —
     *     isso foi um reboot intencional, não um travamento.
     */
    LogManager::instance().markCleanReboot();
    delay(50);  /* último flush do Serial */
    watchdog_enable(500, 1);
    while (1) { tight_loop_contents(); }
}

/* handleSaveNetwork removido em U24 Phase C — substituido por handleApiCommitAll. */

/**
 * @brief Reseta calibração do touch via API web.
 *
 * Limpa TouchCalData no config (invalida magic), reseta parâmetros
 * no DisplayManager e salva no flash.
 */
/**
 * @brief F-NET-TIME.3a: POST /api/set_time — set manual RTC imediato.
 *
 * Body JSON: {"epoch": <uint32_t UTC seconds>}
 * Resposta: {"ok":true,"now":<uint32_t epoch now>} ou {"error":"..."}.
 *
 * NÃO passa pelo commit-all nem exige reboot — aplica agora via
 * NetworkManager::setManualTime(). Útil quando ntp_enabled=false;
 * com ntp_enabled=true, o próximo sync NTP sobrescreve.
 * Permissão: PERM_SYS_CONFIG.
 */
void WebManager::handleApiSetTime() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    String body = _server.arg("plain");
    int p = body.indexOf("\"epoch\"");
    if (p < 0) { _server.send(400, "application/json", "{\"error\":\"missing epoch\"}"); return; }
    int vs = body.indexOf(':', p);
    if (vs < 0) { _server.send(400, "application/json", "{\"error\":\"bad format\"}"); return; }
    vs++;
    while (vs < (int)body.length() && (body.charAt(vs) == ' ' || body.charAt(vs) == '"')) vs++;
    int ve = vs;
    while (ve < (int)body.length() && isDigit(body.charAt(ve))) ve++;
    if (ve == vs) { _server.send(400, "application/json", "{\"error\":\"bad epoch\"}"); return; }
    uint32_t epoch = (uint32_t)body.substring(vs, ve).toInt();
    if (epoch <= 1600000000UL) { _server.send(400, "application/json", "{\"error\":\"epoch too low\"}"); return; }

    _netRef->setManualTime((time_t)epoch);

    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":true,\"now\":%lu}", (unsigned long)time(nullptr));
    _server.send(200, "application/json", json);
}

void WebManager::handleResetTouchCal() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }
    if (rejectIfTouchPriority()) return;

    SystemConfig& cfg = _storageRef->getConfig();
    TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
    memset(cal, 0, sizeof(TouchCalData));
    _displayRef->resetTouchCalibration();
    _storageRef->saveConfiguration();

    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Touch calibration reset via web", "Calibracao do touch resetada via web"));

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

/* handleApiUserAdd/Del/Reset removidos em U24 Phase B — substituidos por handleApiCommitAll. */


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
            { RenderGuard rg(_displayRef); _uploadFile.write(upload.buf, upload.currentSize); }
            feedWatchdog();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (_uploadRejected) return;
        if (_uploadFile) {
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


void WebManager::handleApiStatus() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_DASHBOARD)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    _server.sendHeader("Cache-Control", "no-cache");

    SystemConfig& cfg = _storageRef->getConfig();
    String ipStr = _netRef->getIpAddress();

    uint32_t heapTot = rp2040.getTotalHeap();
    uint32_t heapFree = rp2040.getFreeHeap();

    if (millis() - _lastFsInfoRefresh > 10000 || _cachedFsTotalBytes == 0) {

        ReadGuard rg(_storageRef);
        FSInfo fs_info;
        fs_info.totalBytes = 0;
        fs_info.usedBytes = 0;
        LittleFS.info(fs_info);
        _cachedFsTotalBytes = fs_info.totalBytes;
        _cachedFsUsedBytes = fs_info.usedBytes;
        _lastFsInfoRefresh = millis();
    }

    time_t now = time(nullptr);
    bool ntp = _netRef->isTimeSynced();


    int pending = _telemetryRef
                ? static_cast<int>(_telemetryRef->getPendingEstimate())
                : -1;

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    char buffer[1024];

    String devName = cfg.deviceName;
    devName.replace("\"", "\\\"");

    uint32_t heapLargest = MetricsManager::instance().data().heapLargestBlock;
    snprintf(buffer, sizeof(buffer), "{\"sys\":{\"name\":\"%s\",\"uptime\":%lu,\"rssi\":%d,\"ip\":\"%s\",\"theme\":%d,\"heap_f\":%lu,\"heap_t\":%lu,\"heap_lb\":%lu,\"fs_u\":%lu,\"fs_t\":%lu,\"time\":%lu,\"ntp\":%d,\"pending\":%d},",
        devName.c_str(), millis(), _netRef->getRssi(), ipStr.c_str(), cfg.themeIndex,
        (unsigned long)heapFree, (unsigned long)heapTot, (unsigned long)heapLargest,
        (unsigned long)_cachedFsUsedBytes, (unsigned long)_cachedFsTotalBytes,
        (unsigned long)now, ntp ? 1 : 0, pending);

    if (!safeSend(buffer)) return;
    if (!safeSend("\"sensors\":[")) return;

    const auto& sensors = _sensorRef->getRuntimeSensors();
    bool first = true;
    for (const auto &s : sensors) {
        if (!s.config.active) continue;
        if (!first) { if (!safeSend(",")) return; }
        first = false;

        String sName = s.config.friendlyName;
        sName.replace("\"", "\\\"");

        String sId = s.config.hwId;
        sId.replace("\"", "\\\"");

        char valBuffer[16]; char humBuffer[32] = "";
        if (s.inErrorState) snprintf(valBuffer, sizeof(valBuffer), "\"Error\"");
        else if (isnan(s.avgValue1)) snprintf(valBuffer, sizeof(valBuffer), "\"--\"");
        else snprintf(valBuffer, sizeof(valBuffer), "%.2f", s.avgValue1);

        if (s.type == TYPE_DHT22 && !s.inErrorState && !isnan(s.avgValue2)) {
            snprintf(humBuffer, sizeof(humBuffer), ",\"hum\":%.1f", s.avgValue2);
        }

        snprintf(buffer, sizeof(buffer), "{\"gpio\":%d,\"id\":\"%s\",\"name\":\"%s\",\"val\":%s%s}", s.config.gpio, sId.c_str(), sName.c_str(), valBuffer, humBuffer);
        if (!safeSend(buffer)) return;
    }

    safeSend("]}");
    safeSend("");
}

void WebManager::handleApiHistoryData() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_HISTORY)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }
    if (!_server.hasArg("sensor")) { _server.send(400, "application/json", "{\"error\":\"Missing sensor param\"}"); return; }


    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
        return;
    }


    if (__atomic_exchange_n(&_inHistoryHandler, true, __ATOMIC_ACQ_REL)) {
        _server.send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
    }

    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) {
        __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
        _server.send(503, "application/json", "{\"error\":\"System Busy.\"}");
        return;
    }


    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;


    if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str());

    int sensorIdx = _server.arg("sensor").toInt();
    String reqDate = _server.arg("date");
    String reqRange = _server.arg("range");
    String reqEnd   = _server.arg("end");

    uint32_t epochLimit = 0;
    if (sensorIdx >= 0 && sensorIdx < MAX_SENSORS) {
        epochLimit = _storageRef->getConfig().sensors[sensorIdx].provisionEpoch;
    }

    /*
     * Calcula janela temporal — mesma lógica do display.
     * Parâmetros: range=0..4, end=epoch (âncora), date=YYYYMMDD
     */
    time_t now = _netRef->getEpoch();
    static const time_t rangeDuration[] = { 3600, 21600, 43200, 86400, 604800 };
    static const int rangeDecimation[]  = { 1, 1, 2, 3, 15 };
    static const int rangeDays[]        = { 1, 1, 1, 2, 7 };

    time_t effectiveEnd = now;
    time_t cutoff = 0;
    int decimation = 1;
    int daysToLoad = 1;

    if (reqRange.length() > 0) {
        int r = reqRange.toInt();
        if (r < 0) r = 0; if (r > 4) r = 4;

        /* Âncora: se 'end' especificado, usa como fim da janela */
        if (reqEnd.length() > 0) {
            effectiveEnd = (time_t)reqEnd.toInt();
            if (effectiveEnd > now) effectiveEnd = now;
        }

        cutoff = effectiveEnd - rangeDuration[r];
        decimation = rangeDecimation[r];
        daysToLoad = rangeDays[r];

        /* Para ranges ≤24H, verifica se cruza meia-noite */
        if (r <= 3) {
            struct tm etm;
            localtime_r(&effectiveEnd, &etm);
            etm.tm_hour = 0; etm.tm_min = 0; etm.tm_sec = 0;
            time_t eMidnight = mktime(&etm);
            if (cutoff < eMidnight) daysToLoad = 2;
        }
    } else if (reqDate.length() == 8) {
        /* Modo data: dia inteiro (00:00–23:59) */
        int y = reqDate.substring(0,4).toInt();
        int m = reqDate.substring(4,6).toInt();
        int d = reqDate.substring(6,8).toInt();
        struct tm dtm = {};
        dtm.tm_year = y - 1900; dtm.tm_mon = m - 1; dtm.tm_mday = d;
        cutoff = mktime(&dtm);
        effectiveEnd = cutoff + 86400;
        decimation = 3;
        daysToLoad = 1;
    }

    /* Monta lista de arquivos a ler */
    std::vector<String> filesToRead;
    for (int d = daysToLoad - 1; d >= 0; d--) {
        time_t targetDay = effectiveEnd - (d * 86400);
        struct tm timeinfo; localtime_r(&targetDay, &timeinfo);
        char defPath[40];
        snprintf(defPath, sizeof(defPath), "/history/%04d%02d%02d" HISTORY_FILE_EXT,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        filesToRead.push_back(String(defPath));
    }

    /* Metadados para o frontend: min/max reais, janela temporal */
    float realMinT = 1000.0f, realMaxT = -1000.0f;
    time_t tsRealMinT = 0, tsRealMaxT = 0;
    float realMinH = 1000.0f, realMaxH = -1000.0f;

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    /* Envia header com metadados da janela temporal */
    {
        char metaBuf[128];
        snprintf(metaBuf, sizeof(metaBuf),
            "{\"cutoff\":%lu,\"end\":%lu,\"now\":%lu,\"data\":[",
            (unsigned long)cutoff, (unsigned long)effectiveEnd, (unsigned long)now);
        safeSend(metaBuf);
    }

    bool first = true;
    static char chunkBuf[2048];
    chunkBuf[0] = '\0';
    int chunkLen = 0;
    bool aborted = false;
    int lineIdx = 0;

    for (size_t fi = 0; fi < filesToRead.size(); fi++) {
        if (aborted) break;
        String path = filesToRead[fi];

        File f;
        bool fileOk = false;
        {
            ReadGuard rg(_storageRef);
            if (LittleFS.exists(path)) {
                f = LittleFS.open(path, "r");
                fileOk = (bool)f;
            }
        }

        if (fileOk) {
            size_t fileSize = f.size();
            size_t totalRecords = fileSize / HISTORY_RECORD_SIZE;

            /* Seek otimizado — mesma lógica do display */
            if (totalRecords > 50 && cutoff > 0) {
                struct tm fileTm;
                {
                    time_t targetDay = effectiveEnd - (int)(filesToRead.size() - 1 - fi) * 86400;
                    localtime_r(&targetDay, &fileTm);
                }
                fileTm.tm_hour = 0; fileTm.tm_min = 0; fileTm.tm_sec = 0;
                time_t fileMidnight = mktime(&fileTm);

                if (cutoff > fileMidnight) {
                    int seekFromMidnight = max(0, (int)((cutoff - fileMidnight) / 60) - 10);
                    static const int maxRec[] = { 80, 380, 740, 1460, 1460 };
                    int rIdx = (reqRange.length() > 0) ? constrain(reqRange.toInt(), 0, 4) : 3;
                    int seekFromEnd = max(0, (int)totalRecords - maxRec[rIdx]);
                    int seekRecord = (seekFromMidnight < (int)totalRecords)
                                     ? min(seekFromMidnight, seekFromEnd) : seekFromEnd;
                    if (seekRecord > 0 && seekRecord < (int)totalRecords) {
                        ReadGuard rg(_storageRef);
                        f.seek((size_t)seekRecord * HISTORY_RECORD_SIZE);
                    }
                }
            }

            bool fileHasMore = true;
            while (fileHasMore) {
                if (isClientGone() || isHandlerOvertime()) {
                    LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
                    { ReadGuard rg(_storageRef); f.close(); }
                    aborted = true;
                    break;
                }

                BinaryHistoryRecord readBatch[20];
                int batchCount = 0;

                {
                    ReadGuard rg(_storageRef);
                    while (f.available() >= HISTORY_RECORD_SIZE && batchCount < 20) {
                        if (f.read((uint8_t*)&readBatch[batchCount], HISTORY_RECORD_SIZE)
                            == HISTORY_RECORD_SIZE)
                        {
                            batchCount++;
                        }
                    }
                    fileHasMore = (f.available() >= HISTORY_RECORD_SIZE);
                }

                bool pastWindow = false;
                for (int bi = 0; bi < batchCount && !aborted; bi++) {
                    const BinaryHistoryRecord& rec = readBatch[bi];
                    time_t ts = (time_t)rec.epoch;

                    if (ts < cutoff && cutoff > 0) continue;
                    if (ts > effectiveEnd) { pastWindow = true; break; }

                    /* Rastreia min/max reais de TODOS os registros (pré-decimação) */
                    float preValT = NAN;
                    if (sensorIdx == -1) preValT = BinaryHistoryRecord::i16ToFloat(rec.ambientTemp);
                    else if (sensorIdx >= 0 && sensorIdx < MAX_SENSORS) preValT = BinaryHistoryRecord::i16ToFloat(rec.sensors[sensorIdx]);
                    if (ts < epochLimit) preValT = NAN;

                    if (!isnan(preValT)) {
                        if (preValT < realMinT) { realMinT = preValT; tsRealMinT = ts; }
                        if (preValT > realMaxT) { realMaxT = preValT; tsRealMaxT = ts; }
                    }

                    lineIdx++;
                    if (lineIdx % decimation != 0) continue;

                    float valT = preValT;
                    float valH = NAN;
                    if (sensorIdx == -1) valH = BinaryHistoryRecord::i16ToFloat(rec.ambientHum);

                    if (!isnan(valH)) {
                        if (valH < realMinH) realMinH = valH;
                        if (valH > realMaxH) realMaxH = valH;
                    }

                    /* Emite ponto: NAN como null para o Chart.js criar buracos */
                    char pointBuf[96];
                    if (!isnan(valT) && valT > -50.0f && valT < 150.0f) {
                        const char* signT = (valT < 0.0f) ? "-" : "";
                        int tInt = abs((int)valT);
                        int tDec = abs((int)(valT * 100.0f) % 100);

                        if (sensorIdx == -1 && !isnan(valH) && valH >= 0.0f && valH <= 100.0f) {
                            int hInt = abs((int)valH);
                            int hDec = abs((int)(valH * 10.0f) % 10);
                            snprintf(pointBuf, sizeof(pointBuf), "%s{\"t\":%lu,\"v1\":%s%d.%02d,\"v2\":%d.%01d}",
                                     first ? "" : ",", (unsigned long)ts, signT, tInt, tDec, hInt, hDec);
                        } else {
                            snprintf(pointBuf, sizeof(pointBuf), "%s{\"t\":%lu,\"v1\":%s%d.%02d}",
                                     first ? "" : ",", (unsigned long)ts, signT, tInt, tDec);
                        }
                    } else {
                        /* Ponto NAN: emite com v1:null para buraco visível no Chart.js */
                        snprintf(pointBuf, sizeof(pointBuf), "%s{\"t\":%lu,\"v1\":null}",
                                 first ? "" : ",", (unsigned long)ts);
                    }

                    int pLen = strlen(pointBuf);
                    if (chunkLen + pLen >= (int)sizeof(chunkBuf) - 1) {
                        if (!safeSend(chunkBuf)) {
                            { ReadGuard rg(_storageRef); f.close(); }
                            aborted = true;
                            break;
                        }
                        chunkBuf[0] = '\0';
                        chunkLen = 0;
                        delay(5);
                        watchdog_update();
                    }
                    memcpy(chunkBuf + chunkLen, pointBuf, pLen + 1);
                    chunkLen += pLen;
                    first = false;

                    if (chunkLen > 1500) {
                        if (!safeSend(chunkBuf)) {
                            { ReadGuard rg(_storageRef); f.close(); }
                            aborted = true;
                            break;
                        }
                        chunkBuf[0] = '\0';
                        chunkLen = 0;
                        delay(5);
                        watchdog_update();
                    }
                }

                if (pastWindow) break;
                if (aborted) break;
                if (_lightYieldCb) _lightYieldCb();
                delay(5);
                watchdog_update();
            }
            if (!aborted) { ReadGuard rg(_storageRef); f.close(); }
        }
    }

    if (!aborted) {
        if (chunkLen > 0) safeSend(chunkBuf);

        /* Fecha array e emite metadados de min/max reais */
        char metaEnd[192];
        if (realMaxT > -999.0f) {
            const char* sMin = (realMinT < 0) ? "-" : "";
            const char* sMax = (realMaxT < 0) ? "-" : "";
            snprintf(metaEnd, sizeof(metaEnd),
                "],\"minT\":%s%d.%02d,\"maxT\":%s%d.%02d,\"tsMinT\":%lu,\"tsMaxT\":%lu}",
                sMin, abs((int)realMinT), abs((int)(realMinT*100)%100),
                sMax, abs((int)realMaxT), abs((int)(realMaxT*100)%100),
                (unsigned long)tsRealMinT, (unsigned long)tsRealMaxT);
        } else {
            snprintf(metaEnd, sizeof(metaEnd), "]}");
        }
        safeSend(metaEnd);
        safeSend("");
    }
    _handlerDeadline = savedDeadline;
    if (_displayRef) _displayRef->setWebBusy(false);
    __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
}

void WebManager::handleApiLogs() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_LOGS)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (isRateLimited(200)) { _server.send(429, "text/plain", "Too Fast"); return; }


    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
        return;
    }

    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) {
        _server.send(503, "text/plain", "System Busy.");
        return;
    }


    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;
    if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str());

    /*
     * Envia logs binários brutos (12 bytes/registro) para máxima
     * eficiência de transferência. A tradução acontece no browser.
     * Formato: application/octet-stream, N × CompactLogRecord(12 bytes).
     * ~10x menor que o CSV traduzido anterior.
     */
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/octet-stream", "");

    auto streamRawLog = [&](const char* path) -> bool {
        File f;
        {
            ReadGuard rg(_storageRef);
            if (!LittleFS.exists(path)) return true;
            f = LittleFS.open(path, "r");
        }

        if (!f) return true;

        int count = 0;
        while (f.available() >= LOG_RECORD_SIZE) {
            if (count > 0 && count % 80 == 0) {
                if (isClientGone() || isHandlerOvertime()) {
                    f.close();
                    return false;
                }
            }

            /* Lê batch de até 40 registros (480 bytes) e envia de uma vez */
            uint8_t buf[480];
            int bytesRead = 0;
            {
                ReadGuard rg(_storageRef);
                while (f.available() >= LOG_RECORD_SIZE && bytesRead + LOG_RECORD_SIZE <= (int)sizeof(buf)) {
                    if (f.read(buf + bytesRead, LOG_RECORD_SIZE) == LOG_RECORD_SIZE) {
                        bytesRead += LOG_RECORD_SIZE;
                    }
                }
            }

            if (bytesRead > 0) {
                _server.sendContent((const char*)buf, bytesRead);
                count += bytesRead / LOG_RECORD_SIZE;
            }

            if (_lightYieldCb) _lightYieldCb();
            delay(2);
            watchdog_update();
        }
        f.close();
        return true;
    };

    if (streamRawLog(LOG_FILE_OLD)) {
        streamRawLog(LOG_FILE_CURRENT);
    }

    safeSend("");
    _handlerDeadline = savedDeadline;
    if (_displayRef) _displayRef->setWebBusy(false);
}

void WebManager::handleApiClearLogs() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_LOGS) || !(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (isPasswordChangeRequired()) return;
    if (rejectIfTouchPriority()) return;

    {
        RenderGuard rg(_displayRef);
        LittleFS.remove(LOG_FILE_CURRENT);
        LittleFS.remove(LOG_FILE_OLD);
        /* Remove também logs CSV legados (pré-v3.4.7) */
        LittleFS.remove("/system.log");
        LittleFS.remove("/system.old");
        LogManager::instance().begin(true, LOG_DEBUG);
    }

    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Admin erased System Logs", "Admin apagou logs do sistema"));
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleApiScreenshot() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }


    if (TouchPriority::isActive()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
        return;
    }


    if (__atomic_exchange_n(&_isProcessingScreenshot, true, __ATOMIC_ACQ_REL)) {
        /* Screenshot em andamento: sinalizar cancelamento e retornar 409 */
        _cancelScreenshot = true;
        _server.send(409, "application/json", "{\"error\":\"Screenshot in progress, cancelling.\"}");
        return;
    }
    _cancelScreenshot = false;

    if (!_displayRef) {
        __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
        _server.send(500, "text/plain", "Display offline");
        return;
    }


    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + WEB_LONG_HANDLER_DEADLINE_MS;

    uint32_t w = 320;
    uint32_t h = 240;
    uint32_t rowSize = 960;
    uint32_t imgSize = rowSize * h;
    uint32_t fileSize = 54 + imgSize;

    uint8_t bmpHeader[54] = {
        'B', 'M',
        (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
        (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24),
        (uint8_t)(h), (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24),
        1, 0, 24, 0, 0, 0, 0, 0,
        (uint8_t)(imgSize), (uint8_t)(imgSize >> 8), (uint8_t)(imgSize >> 16), (uint8_t)(imgSize >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    _server.setContentLength(fileSize);
    _server.send(200, "image/bmp", "");
    safeSend((const char*)bmpHeader, 54);

    uint8_t rowBuffer[960];
    uint16_t pixelRow[320];
    bool clientDisconnected = false;

    for (int y = h - 1; y >= 0; y--) {


        _displayRef->pauseRendering(true);
        _displayRef->readRow(y, pixelRow, w);
        _displayRef->pauseRendering(false);


        for (int x = 0; x < (int)w; x++) {
            uint16_t color = pixelRow[x];
            rowBuffer[x*3 + 0] = (color & 0x001F) << 3;
            rowBuffer[x*3 + 1] = ((color & 0x07E0) >> 5) << 2;
            rowBuffer[x*3 + 2] = ((color & 0xF800) >> 11) << 3;
        }

        if (!_server.client().connected() || isHandlerOvertime() || _cancelScreenshot) {
            clientDisconnected = true;
            break;
        }

        safeSend((const char*)rowBuffer, 960);


        if (y % 4 == 0) {
            watchdog_update();
            if (_lightYieldCb) _lightYieldCb();
            delay(1);
        }
    }

    __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
    _handlerDeadline = savedDeadline;
    if (clientDisconnected) LOG_CODE(LOG_WARN, "WEB", WEB_SCREENSHOT_ABORTED, 0, "");
}

void WebManager::handleApiHistoryDays() {
    if ((getAuthPerms() & PERM_HISTORY) == 0) { _server.send(403); return; }


    HeavyTaskGuard htg(_storageRef);
    if (!htg.isLocked()) { _server.send(503, "application/json", "{\"error\":\"System Busy\"}"); return; }

    std::vector<String> files;
    {

        ReadGuard rg(_storageRef);
        Dir dir = LittleFS.openDir(DIR_HISTORY);
        while (dir.next()) {
            feedWatchdog();
            if (dir.fileName().endsWith(HISTORY_FILE_EXT)) {
                files.push_back(dir.fileName());
            }
        }
    }

    std::sort(files.begin(), files.end(), std::greater<String>());

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");
    safeSend("[");
    for (size_t i = 0; i < files.size(); i++) {
        files[i].replace(HISTORY_FILE_EXT, "");
        String entry = (i > 0 ? ",\"" : "\"") + files[i] + "\"";
        safeSend(entry);
    }
    safeSend("]");
    safeSend("");
}

String WebManager::getHistoryFileName(time_t date) {
    struct tm timeinfo; localtime_r(&date, &timeinfo);
    char buff[32]; snprintf(buff, sizeof(buff), "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return String(buff);
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
