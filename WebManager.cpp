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
 * @version 3.4.8
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "WebManager.h"

#include "WebUI_GZ.h"
#include "LogManager.h"
#include "Themes.h"
#include <LittleFS.h>
#include <time.h>
#include <hardware/watchdog.h>
#include <algorithm>
#include <functional>

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
    _server.on("/api/save_alarms", HTTP_POST, std::bind(&WebManager::handleApiSaveAlarms, this));


    _server.on("/api/save_sys", HTTP_POST, std::bind(&WebManager::handleSaveSystem, this));
    _server.on("/api/save_net", HTTP_POST, std::bind(&WebManager::handleSaveNetwork, this));
    _server.on("/api/reset_touch_cal", HTTP_POST, std::bind(&WebManager::handleResetTouchCal, this));
    _server.on("/api/user_add", HTTP_POST, std::bind(&WebManager::handleApiUserAdd, this));
    _server.on("/api/user_del", HTTP_POST, std::bind(&WebManager::handleApiUserDel, this));
    _server.on("/api/user_rst", HTTP_POST, std::bind(&WebManager::handleApiUserReset, this));
    _server.on("/api/history", HTTP_GET, std::bind(&WebManager::handleApiHistoryData, this));
    _server.on("/api/history_days", HTTP_GET, std::bind(&WebManager::handleApiHistoryDays, this));
    _server.on("/api/logs", HTTP_GET, std::bind(&WebManager::handleApiLogs, this));
    _server.on("/api/clear_logs", HTTP_POST, std::bind(&WebManager::handleApiClearLogs, this));
    _server.on("/api/screenshot", HTTP_GET, std::bind(&WebManager::handleApiScreenshot, this));


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
    LOG_CODE(LOG_INFO, "WEB", WEB_SERVER_STARTED, 80, "");
}

bool WebManager::isRateLimited(uint32_t minIntervalMs) {
    uint32_t clientIP = (uint32_t)_server.client().remoteIP();
    uint32_t now = millis();
    int slot = -1;
    int oldest = 0;
    for (int i = 0; i < 3; i++) {
        if (_rateLimits[i].ip == clientIP) { slot = i; break; }
        if (_rateLimits[i].lastReq < _rateLimits[oldest].lastReq) oldest = i;
    }
    if (slot == -1) {
        for (int i = 0; i < 3; i++) { if (_rateLimits[i].ip == 0) { slot = i; break; } }
        if (slot == -1) slot = oldest;
        _rateLimits[slot].ip = clientIP;
        _rateLimits[slot].lastReq = 0;
    }
    if (now - _rateLimits[slot].lastReq < minIntervalMs) return true;
    _rateLimits[slot].lastReq = now;
    return false;
}

void WebManager::feedWatchdog() {
    watchdog_update();
    if (_lightYieldCb) _lightYieldCb();


}

bool WebManager::isHandlerOvertime() {
    /* Wrap-safe: veja comentário em timeReached() (SystemDefs.h). */
    return (_handlerDeadline > 0 && timeReached(_handlerDeadline));
}


#include <pico/time.h>

static volatile bool _sendGuardActive = false;
static volatile uint32_t _sendGuardStartMs = 0;
static struct repeating_timer _sendGuardTimer;

static bool _sendGuardTimerCallback(struct repeating_timer *t) {
    (void)t;
    if (_sendGuardActive) {


        uint32_t elapsed = millis() - _sendGuardStartMs;
        if (elapsed < 35000) {
            watchdog_update();
        }

    }
    return true;
}

void WebManager::initSendGuardTimer() {

    add_repeating_timer_ms(-2000, _sendGuardTimerCallback, nullptr, &_sendGuardTimer);
}


struct SendGuard {
    SendGuard()  { _sendGuardStartMs = millis(); _sendGuardActive = true;  }
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
                LOG_CODE(LOG_INFO, "SEC", SEC_SESSION_EXPIRE, i, "Session expired: " + _activeSessions[i].username);

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
    char json[192];
    snprintf(json, sizeof(json), "{\"user\":\"%s\",\"perms\":%u,\"ntp\":%d,\"time\":%lu}",
             _currentUserName.c_str(), perms, ntpOk ? 1 : 0, (unsigned long)now);
    _server.send(200, "application/json", json);
}

void WebManager::handleApiNetwork() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_NET_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    SystemConfig& cfg = _storageRef->getConfig();

    char json[512];
    snprintf(json, sizeof(json),
        "{\"connected\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\","
        "\"dns\":\"%s\",\"mac\":\"%s\",\"ssid\":\"%s\",\"use_dhcp\":%s,"
        "\"static_ip\":\"%s\",\"static_mask\":\"%s\",\"static_gw\":\"%s\","
        "\"static_dns\":\"%s\",\"ntp_server\":\"%s\"}",
        _netRef->isConnected() ? "true" : "false",
        _netRef->getIpAddress().c_str(),
        _netRef->getSubnetMask().c_str(),
        _netRef->getGateway().c_str(),
        _netRef->getDns().c_str(),
        _netRef->getMacAddress().c_str(),
        cfg.wifiSsid,
        cfg.useDhcp ? "true" : "false",
        cfg.staticIp, cfg.staticMask, cfg.staticGateway, cfg.staticDns,
        cfg.ntpServer);

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

    String json;
    json.reserve(2048);
    json += "{";
    json += "\"name\":\"" + jsonEscape(cfg.deviceName) + "\",";
    json += "\"tz\":" + String(cfg.timezoneOffset) + ",";
    json += "\"log\":" + String(cfg.loggingEnabled ? "true" : "false") + ",";
    json += "\"res\":" + String(cfg.ds18Resolution) + ",";
    json += "\"s_int\":" + String(cfg.sampleIntervalMs) + ",";
    json += "\"t_transport\":" + String(cfg.telTransport) + ",";
    json += "\"t_sec\":" + String(cfg.telEncryption ? "true" : "false") + ",";
    json += "\"t_srv\":\"" + jsonEscape(cfg.telServer) + "\",";
    json += "\"t_port\":" + String(cfg.telPort) + ",";
    json += "\"t_path\":\"" + jsonEscape(cfg.telPath) + "\",";
    json += "\"t_key\":\"" + jsonEscape(cfg.telApiKey) + "\",";
    json += "\"m_topic\":\"" + jsonEscape(cfg.mqttTopic) + "\",";
    json += "\"m_cid\":\"" + jsonEscape(cfg.mqttClientId) + "\",";
    json += "\"m_user\":\"" + jsonEscape(cfg.mqttUser) + "\",";
    json += "\"m_qos\":" + String(cfg.mqttQos) + ",";
    json += "\"m_retain\":" + String(cfg.mqttRetain ? "true" : "false") + ",";
    json += "\"m_ka\":" + String(cfg.mqttKeepAlive) + ",";
    json += "\"t_int\":" + String(cfg.telInterval) + ",";
    json += "\"t_bat\":" + String(cfg.telBatchSize) + ",";
    json += "\"t_mode\":" + String(cfg.telMode) + ",";
    json += "\"t_glob\":\"" + jsonEscape(cfg.telGlobalTemplate) + "\",";
    json += "\"t_line\":\"" + jsonEscape(cfg.telLineTemplate) + "\",";
    json += "\"t_sep\":\"" + jsonEscape(cfg.telLineSeparator) + "\"";
    json += "}";

    _server.send(200, "application/json", json);
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

    String json;
    json.reserve(256);
    json += "[";

    for(int i = 0; i < getThemeCount(); i++) {
        if(i > 0) json += ",";


        String cleanName = "";
        const char* p = availableThemes[i].displayName;
        if (p != nullptr) {
            while (*p) {
                unsigned char c = (unsigned char)(*p);


                if (c >= 32 && c != '\"' && c != '\\') {
                    cleanName += (char)c;
                }
                p++;
            }
        } else {
            cleanName = "Tema " + String(i);
        }

        json += "{\"id\":";
        json += String(i);
        json += ",\"name\":\"";
        json += cleanName;
        json += "\"}";
    }
    json += "]";

    _server.send(200, "application/json", json);
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

void WebManager::handleApiSaveAlarms() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }
    if (isPasswordChangeRequired()) return;


    String body = _server.arg("plain");
    if (body.length() == 0 || body.length() > 4096) {
        _server.send(400, "application/json", "{\"error\":\"Bad request\"}");
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();


    int searchPos = 0;
    int sensorsStart = body.indexOf("\"sensors\"", searchPos);
    if (sensorsStart >= 0) {
        int arrStart = body.indexOf('[', sensorsStart);
        int arrEnd   = body.indexOf(']', arrStart);
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
                if (idx == -1) {
                    rec = &cfg.ambientSensor;
                } else if (idx >= 0 && idx < MAX_SENSORS && cfg.sensors[idx].active) {
                    rec = &cfg.sensors[idx];
                }

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
    }


    int soundsStart = body.indexOf("\"sounds\"");
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
            if (volPos >= 0) {
                int vc = sObj.indexOf(':', volPos);
                snd.volume = (uint8_t)constrain(sObj.substring(vc + 1).toInt(), 0, 100);
            } else {
                snd.volume = 70;
            }


            int aVolPos = sObj.indexOf("\"alarmVolume\"");
            if (aVolPos >= 0) {
                int avc = sObj.indexOf(':', aVolPos);
                snd.alarmVolume = (uint8_t)constrain(sObj.substring(avc + 1).toInt(), 0, 100);
            } else {
                snd.alarmVolume = 70;
            }


            auto extractMelIdx = [&](const char* key) -> uint8_t {
                int kp = sObj.indexOf(key);
                if (kp < 0) return 0;
                int cp = sObj.indexOf(':', kp);
                if (cp < 0) return 0;
                int val = sObj.substring(cp + 1).toInt();
                return (uint8_t)constrain(val, 0, 5);
            };
            snd.touchMelody   = extractMelIdx("\"melTouch\"");
            snd.confirmMelody = extractMelIdx("\"melConfirm\"");
            snd.errorMelody   = extractMelIdx("\"melError\"");
            snd.alarmMelody   = extractMelIdx("\"melAlarm\"");


            _soundRef->applySettingsState(snd);


            SoundConfigData* sndCfg = reinterpret_cast<SoundConfigData*>(
                cfg.reserved + sizeof(TouchCalData));
            _soundRef->fillConfig(sndCfg);
        }
    }


    bool saved = _storageRef->saveConfiguration();


    _sensorRef->syncAlarmLimits(cfg);

    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(saved ? SND_CONFIRM : SND_ERROR);

    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId,
             "Admin updated Alarms & Sounds via web");

    _server.send(200, "application/json",
                 saved ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
}


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
    for (int i = 0; i < 3; i++) {
        if (_loginStates[i].ip == clientIP) { slot = i; break; }
        if (_loginStates[i].lastActivity < _loginStates[oldest].lastActivity) oldest = i;
    }
    if (slot == -1) {
        for (int i = 0; i < 3; i++) {
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
    for (int i = 0; i < 3; i++) {
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
        if (ls >= 0) _loginStates[ls].nonce = "";
        LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0,
                 nonceExpired ? "Login Rejected: Nonce Expired" : "Login Rejected: Invalid Nonce");
        _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
        return;
    }
    if (ls >= 0) _loginStates[ls].nonce = "";

    if (!_server.hasArg("user") || !_server.hasArg("pass")) {
        _server.send(400, "application/json", "{\"ok\":false,\"err\":1}");
        return;
    }

    String u = _server.arg("user");
    String p = _server.arg("pass");
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
            LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, "Login Rejected: Max Sessions Reached");
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

        LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, foundId, "Login OK: " + u);
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
            LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, "Login Failed: " + u);
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
                LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, 0, "Logout: " + _activeSessions[i].username);

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

void WebManager::handleApiForceChpass() {
    if (getAuthPerms() == 0 || !isPasswordChangeRequired()) { _server.send(403, "text/plain", "Forbidden"); return; }

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
    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Password Reset Success: " + _currentUserName);

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}


void WebManager::handleSaveSystem() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (isPasswordChangeRequired()) return;

    SystemConfig& cfg = _storageRef->getConfig();
    bool themeChanged = false;

    if (_server.hasArg("theme")) {
        int t = _server.arg("theme").toInt();
        if (t >= 0 && t < getThemeCount() && cfg.themeIndex != t) {
            cfg.themeIndex = t; loadTheme(t); themeChanged = true;
        }
    }

    if (_server.hasArg("name")) {
        String n = _server.arg("name"); n.trim();

        if (n.length() > 0 && isValidName(n.c_str())) {
            safeCopy(cfg.deviceName, n.c_str(), sizeof(cfg.deviceName));
        }
    }
    if (_server.hasArg("tz")) {
        cfg.timezoneOffset = (int8_t)_server.arg("tz").toInt();
        NetworkManager::applyTimezone(cfg.timezoneOffset);
    }


    if (_server.hasArg("log")) cfg.loggingEnabled = (_server.arg("log") != "0");
    if (_server.hasArg("t_sec")) cfg.telEncryption = (_server.arg("t_sec") != "0");
    if (_server.hasArg("t_key")) { safeCopy(cfg.telApiKey, _server.arg("t_key").c_str(), sizeof(cfg.telApiKey)); }

    if (_server.hasArg("res")) { int r = _server.arg("res").toInt(); if (r >= 9 && r <= 12) cfg.ds18Resolution = (uint8_t)r; }
    if (_server.hasArg("s_int")) cfg.sampleIntervalMs = _server.arg("s_int").toInt();

    if (_server.hasArg("t_srv")) { safeCopy(cfg.telServer, _server.arg("t_srv").c_str(), sizeof(cfg.telServer)); }
    if (_server.hasArg("t_port")) {
        int p = _server.arg("t_port").toInt();
        if (isInRange(p, 1, 65535)) cfg.telPort = (uint16_t)p;
    }
    if (_server.hasArg("t_path")) { safeCopy(cfg.telPath, _server.arg("t_path").c_str(), sizeof(cfg.telPath)); }
    if (_server.hasArg("t_int")) cfg.telInterval = _server.arg("t_int").toInt();
    if (_server.hasArg("t_bat")) cfg.telBatchSize = (uint8_t)_server.arg("t_bat").toInt();
    if (_server.hasArg("t_mode")) cfg.telMode = (uint8_t)_server.arg("t_mode").toInt();

    if (_server.hasArg("t_transport")) cfg.telTransport = (uint8_t)_server.arg("t_transport").toInt();
    if (_server.hasArg("m_topic")) { safeCopy(cfg.mqttTopic, _server.arg("m_topic").c_str(), sizeof(cfg.mqttTopic)); }
    if (_server.hasArg("m_cid")) { safeCopy(cfg.mqttClientId, _server.arg("m_cid").c_str(), sizeof(cfg.mqttClientId)); }
    if (_server.hasArg("m_user")) { safeCopy(cfg.mqttUser, _server.arg("m_user").c_str(), sizeof(cfg.mqttUser)); }
    if (_server.hasArg("m_pass")) {
        String mp = _server.arg("m_pass"); mp.trim();
        if (mp.length() > 0) { safeCopy(cfg.mqttPass, mp.c_str(), sizeof(cfg.mqttPass)); }
    }
    if (_server.hasArg("m_qos")) cfg.mqttQos = (uint8_t)_server.arg("m_qos").toInt();

    if (_server.hasArg("m_retain")) cfg.mqttRetain = (_server.arg("m_retain") != "0");
    if (_server.hasArg("m_ka")) {
        int ka = _server.arg("m_ka").toInt();
        if (isInRange(ka, 5, 600)) cfg.mqttKeepAlive = (uint16_t)ka;
    }

    if (_server.hasArg("t_glob")) { safeCopy(cfg.telGlobalTemplate, _server.arg("t_glob").c_str(), sizeof(cfg.telGlobalTemplate)); }
    if (_server.hasArg("t_line")) { safeCopy(cfg.telLineTemplate, _server.arg("t_line").c_str(), sizeof(cfg.telLineTemplate)); }
    if (_server.hasArg("t_sep")) { safeCopy(cfg.telLineSeparator, _server.arg("t_sep").c_str(), sizeof(cfg.telLineSeparator)); }

    bool saved = _storageRef->saveConfiguration();
    if (themeChanged && _displayRef) _displayRef->refreshTheme();
    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(saved ? SND_CONFIRM : SND_ERROR);

    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Admin updated System Settings");

    _server.send(200, "application/json", saved ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
}

void WebManager::handleSaveNetwork() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_NET_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (isPasswordChangeRequired()) return;

    SystemConfig& cfg = _storageRef->getConfig();

    if (_server.hasArg("ssid")) { String s = _server.arg("ssid"); s.trim(); if (s.length() > 0) { safeCopy(cfg.wifiSsid, s.c_str(), sizeof(cfg.wifiSsid)); } }
    if (_server.hasArg("pass")) { String p = _server.arg("pass"); p.trim(); if (p.length() > 0) { safeCopy(cfg.wifiPass, p.c_str(), sizeof(cfg.wifiPass)); } }


    if (_server.hasArg("use_dhcp")) cfg.useDhcp = (_server.arg("use_dhcp") != "0");

    if (!cfg.useDhcp) {

        if (_server.hasArg("ip") && isValidIpv4(_server.arg("ip").c_str())) {
            safeCopy(cfg.staticIp, _server.arg("ip").c_str(), sizeof(cfg.staticIp));
        }
        if (_server.hasArg("mask") && isValidIpv4(_server.arg("mask").c_str())) {
            safeCopy(cfg.staticMask, _server.arg("mask").c_str(), sizeof(cfg.staticMask));
        }
        if (_server.hasArg("gw") && isValidIpv4(_server.arg("gw").c_str())) {
            safeCopy(cfg.staticGateway, _server.arg("gw").c_str(), sizeof(cfg.staticGateway));
        }
        if (_server.hasArg("dns") && isValidIpv4(_server.arg("dns").c_str())) {
            safeCopy(cfg.staticDns, _server.arg("dns").c_str(), sizeof(cfg.staticDns));
        }
    }

    /* Servidor NTP customizado (vazio = pool.ntp.org) */
    if (_server.hasArg("ntp_server")) {
        String ntp = _server.arg("ntp_server"); ntp.trim();
        safeCopy(cfg.ntpServer, ntp.c_str(), sizeof(cfg.ntpServer));
        cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
    }

    bool saved = _storageRef->saveConfiguration();
    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Admin updated Network Settings");

    _server.send(200, "application/json", "{\"status\":\"ok\",\"reboot\":true}");

    if (saved) {
        LOG_CODE(LOG_WARN, "SYS", SYS_REBOOT_USER, _currentUserId, "Reboot via web (network save)");
        delay(1000);
        rp2040.reboot();
    }
}

/**
 * @brief Reseta calibração do touch via API web.
 *
 * Limpa TouchCalData no config (invalida magic), reseta parâmetros
 * no DisplayManager e salva no flash.
 */
void WebManager::handleResetTouchCal() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    SystemConfig& cfg = _storageRef->getConfig();
    TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
    memset(cal, 0, sizeof(TouchCalData));
    _displayRef->resetTouchCalibration();
    _storageRef->saveConfiguration();

    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Touch calibration reset via web");

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleApiUserAdd() {
    if (!(getAuthPerms() & PERM_USER_MGR)) { _server.send(403, "text/plain", "Forbidden"); return; }
    SystemConfig& cfg = _storageRef->getConfig();

    int slot = -1;
    for(int i = 1; i < MAX_USERS; i++) {
        if(!cfg.users[i].active) { slot = i; break; }
    }

    if(slot == -1) {
        _server.send(400, "application/json", "{\"error\":\"Users list full\"}");
        return;
    }

    safeCopy(cfg.users[slot].username, _server.arg("u_name").c_str(), sizeof(cfg.users[slot].username));


    String uName = String(cfg.users[slot].username);
    uName.trim();
    if (!isValidName(uName.c_str(), 15) || uName.equalsIgnoreCase("admin")) {
        cfg.users[slot].active = false;
        _server.send(400, "application/json", "{\"error\":\"Invalid username\"}");
        return;
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (i != slot && cfg.users[i].active && uName.equalsIgnoreCase(String(cfg.users[i].username))) {
            cfg.users[slot].active = false;
            _server.send(400, "application/json", "{\"error\":\"Username already exists\"}");
            return;
        }
    }

    safeCopy(cfg.users[slot].password, "*PENDING*", sizeof(cfg.users[slot].password));
    cfg.users[slot].mustChangePassword = true;

    uint16_t newPerms = 0;
    if(_server.hasArg("p_dash")) newPerms |= PERM_DASHBOARD;
    if(_server.hasArg("p_hist")) newPerms |= PERM_HISTORY;
    if(_server.hasArg("p_logs")) newPerms |= PERM_LOGS;
    if(_server.hasArg("p_sys"))  newPerms |= PERM_SYS_CONFIG;
    if(_server.hasArg("p_net"))  newPerms |= PERM_NET_CONFIG;
    if(_server.hasArg("p_fread")) newPerms |= PERM_FILE_READ;
    if(_server.hasArg("p_fupl"))  newPerms |= PERM_FILE_UPLOAD;
    if(_server.hasArg("p_fdel"))  newPerms |= PERM_FILE_DELETE;
    if(_server.hasArg("p_usr"))   newPerms |= PERM_USER_MGR;

    cfg.users[slot].permissions = newPerms;
    cfg.users[slot].active = true;

    _storageRef->saveConfiguration();
    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Admin Created User: " + String(cfg.users[slot].username));

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleApiUserDel() {
    if (!(getAuthPerms() & PERM_USER_MGR)) { _server.send(403, "text/plain", "Forbidden"); return; }
    int id = _server.arg("id").toInt();
    if (id <= 0 || id >= MAX_USERS) { _server.send(400, "text/plain", "Invalid Slot"); return; }

    SystemConfig& cfg = _storageRef->getConfig();
    cfg.users[id].active = false;
    _storageRef->saveConfiguration();
    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Admin Deleted User Slot " + String(id));

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleApiUserReset() {
    if (!(getAuthPerms() & PERM_USER_MGR)) { _server.send(403, "text/plain", "Forbidden"); return; }
    int id = _server.arg("id").toInt();
    if (id <= 0 || id >= MAX_USERS) { _server.send(400, "text/plain", "Invalid Slot"); return; }

    SystemConfig& cfg = _storageRef->getConfig();

    if (id == 0 || String(cfg.users[id].username) == "admin") {

        String resetHash = _storageRef->hashPassword("admin", "8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918");
        safeCopy(cfg.users[id].password, resetHash.c_str(), sizeof(cfg.users[id].password));
    } else {
        safeCopy(cfg.users[id].password, "*PENDING*", sizeof(cfg.users[id].password));
    }
    cfg.users[id].password[31] = '\0';
    cfg.users[id].mustChangePassword = true;

    _storageRef->saveConfiguration();
    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Admin Reset Password Slot " + String(id));

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
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
    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "User downloaded: " + fileName);
}

void WebManager::handleDelete() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_DELETE)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (!_server.hasArg("file")) { _server.send(400, "text/plain", "Bad Request"); return; }

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

    String json;
    json.reserve(2048);
    json = "{\"path\":\"" + dirPath + "\",\"entries\":[";
    bool first = true;

    if (dirPath == "/") {
        String foundDirs[10];
        int numDirs = 0;

        const char* sysDirs[] = {"/config", "/history"};
        for (auto sd : sysDirs) {
            feedWatchdog();

            ReadGuard rg(_storageRef);
            Dir testDir = LittleFS.openDir(sd);
            bool hasContent = testDir.next();
            if (hasContent || LittleFS.exists(String(sd) + "/")) {
                if (numDirs < 10) foundDirs[numDirs++] = String(sd + 1);
            }
        }

        for (int i = 0; i < numDirs; i++) {
            if (!first) json += ",";
            first = false;
            json += "{\"n\":\"" + foundDirs[i] + "\",\"t\":\"d\",\"s\":0}";
        }
    }


    bool dirDone = false;
    int totalCount = 0;


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
                if (dirPath != "/") {
                    String dName = batch[i].name;
                    if (dName.length() > 0 && dName != "." && dName != "..") {
                        if (!first) json += ",";
                        first = false;
                        json += "{\"n\":\"" + dName + "\",\"t\":\"d\",\"s\":0}";
                    }
                }
                continue;
            }

            String fnStr = batch[i].name;
            size_t sz = batch[i].size;

            fnStr.replace("\\", "\\\\");
            fnStr.replace("\"", "\\\"");

            if (fnStr.length() == 0) continue;

            if (!first) json += ",";
            first = false;

            char entry[192];
            snprintf(entry, sizeof(entry), "{\"n\":\"%s\",\"t\":\"f\",\"s\":%u}", fnStr.c_str(), (unsigned)sz);
            json += entry;
            totalCount++;
        }

        feedWatchdog();
    }

    json += "]}";
    _server.send(200, "application/json", json);
}

void WebManager::handleApiMkdir() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_UPLOAD)) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (!_server.hasArg("dir")) { _server.send(400, "text/plain", "Missing dir"); return; }

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
        LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Created folder: " + dirPath);
        _server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        _server.send(500, "application/json", "{\"error\":\"Failed\"}");
    }
}

void WebManager::handleUploadComplete() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_UPLOAD)) { _server.send(403, "text/plain", "Forbidden"); return; }
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleUploadData() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_FILE_UPLOAD)) return;

    HTTPUpload& upload = _server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if (!filename.startsWith("/")) filename = "/" + filename;

        String targetDir = "/";
        if (_server.hasArg("uploadDir")) {
            targetDir = _server.arg("uploadDir");
            targetDir.trim();
            targetDir.replace("..", "");
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
        if (_uploadFile) {
            { RenderGuard rg(_displayRef); _uploadFile.write(upload.buf, upload.currentSize); }
            feedWatchdog();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (_uploadFile) {
            { RenderGuard rg(_displayRef); _uploadFile.close(); }

            if (upload.filename == "calib.csv" || upload.filename == "/calib.csv") {
                if (_storageRef->processCalibrationUpload()) {
                    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Universal Calibration Updated.");
                }
            } else {
                LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "File Uploaded.");
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

    snprintf(buffer, sizeof(buffer), "{\"sys\":{\"name\":\"%s\",\"uptime\":%lu,\"rssi\":%d,\"ip\":\"%s\",\"theme\":%d,\"heap_f\":%lu,\"heap_t\":%lu,\"fs_u\":%lu,\"fs_t\":%lu,\"time\":%lu,\"ntp\":%d,\"pending\":%d},",
        devName.c_str(), millis(), _netRef->getRssi(), ipStr.c_str(), cfg.themeIndex,
        (unsigned long)heapFree, (unsigned long)heapTot, (unsigned long)_cachedFsUsedBytes, (unsigned long)_cachedFsTotalBytes,
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


    if (_isTouchPriorityFn && _isTouchPriorityFn()) {
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
    _handlerDeadline = millis() + 30000;


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


    if (_isTouchPriorityFn && _isTouchPriorityFn()) {
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
    _handlerDeadline = millis() + 30000;
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

    {
        RenderGuard rg(_displayRef);
        LittleFS.remove(LOG_FILE_CURRENT);
        LittleFS.remove(LOG_FILE_OLD);
        /* Remove também logs CSV legados (pré-v3.4.7) */
        LittleFS.remove("/system.log");
        LittleFS.remove("/system.old");
        LogManager::instance().begin(true, LOG_DEBUG);
    }

    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, "Admin erased System Logs");
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void WebManager::handleApiScreenshot() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }


    if (_isTouchPriorityFn && _isTouchPriorityFn()) {
        _server.sendHeader("Retry-After", "3");
        _server.send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
        return;
    }


    if (__atomic_exchange_n(&_isProcessingScreenshot, true, __ATOMIC_ACQ_REL)) {
        _server.send(429, "text/plain", "Too Many Requests."); return;
    }

    if (!_displayRef) {
        __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
        _server.send(500, "text/plain", "Display offline");
        return;
    }


    uint32_t savedDeadline = _handlerDeadline;
    _handlerDeadline = millis() + 30000;

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

        if (!_server.client().connected() || isHandlerOvertime()) {
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
