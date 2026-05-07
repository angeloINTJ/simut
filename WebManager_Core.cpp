/**
 * @file    WebManager_Core.cpp
 * @brief   Core infrastructure: constructor, begin, update, send guard, rate limiting.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include <hardware/watchdog.h>

using ReadGuard = StorageManager::ReadGuard;

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
    /* U24 Phase C: /api/save_net substituido por /api/commit_all */
    _server.on("/api/reset_touch_cal", HTTP_POST, std::bind(&WebManager::handleResetTouchCal, this));
    /* U24 Phase B: user_add/del/rst substituidos por /api/commit_all */
    _server.on("/api/history_multi", HTTP_GET, std::bind(&WebManager::handleApiHistoryMulti, this));  /* F-GRAPH-REVAMP — substitui /api/history single-sensor */
    _server.on("/api/history_days", HTTP_GET, std::bind(&WebManager::handleApiHistoryDays, this));
    _server.on("/api/export/history.bin", HTTP_GET, std::bind(&WebManager::handleApiExportHistory, this));  /* F-CSV.2 */
    _server.on("/api/export/logs.bin", HTTP_GET, std::bind(&WebManager::handleApiExportLogs, this));  /* F-CSV.3 */
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

    /* Fase 1 OTA: download de backup completo da LittleFS.
     * Alpha v3.44.0: response também inclui X-Backup-PSize/X-Backup-PCrc
     * pro browser verificar integridade antes de aceitar OTA. */
    _server.on("/api/backup", HTTP_GET, std::bind(&WebManager::handleApiBackup, this));

    /* Fase 2 OTA: rota única para validate/apply (modo no query param ?op=).
     * Adicionar 2 rotas POST com upload callback custaria ~16 KB de flash
     * (provável buffer interno do WebServer arduino-pico por rota). */
    _server.on("/api/restore", HTTP_POST,
               std::bind(&WebManager::handleApiRestoreFinish, this),
               std::bind(&WebManager::handleApiRestoreUploadData, this));

    /* Fase 4 OTA: endpoint smoke-test do staging POSTERGADO pra Fase 5 —
     * adicionar uma rota nova com flash_range_* puxa ~3 KB do Pico SDK no
     * primeiro consumidor. Vamos pagar esse custo quando a rota tiver
     * função real (upload de firmware). Validação da Fase 4 é via reuso
     * dentro do endpoint /api/firmware?op=begin da Fase 5. */

    /* Fase 7 OTA: dispara apply do update pendente (rota separada de
     * /api/restore para distinguir restore de .bkp vs apply de firmware). */
    _server.on("/api/ota/apply", HTTP_POST,
               std::bind(&WebManager::handleApiOtaApply, this));

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
volatile bool _sendGuardActive = false;
volatile bool _sendGuardExpired = false;    /* extern — consumido por WebManager.h */
volatile uint32_t _sendGuardStartMs = 0;
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


void WebManager::update() {
    _clientAcceptsGzip = false;

    uint32_t handlerStart = millis();
    _handlerDeadline = handlerStart + 6000;

    /* PERF: dreno multi-request por tick (até 4) com cap de tempo (50ms).
     * Reduz latência sistemica de ~600ms (1 request por iteração de loop)
     * para ~100-150ms quando o loop principal está ocupado com telemetria/
     * sensores. Cap de 50ms preserva responsividade do display. */
    const uint32_t budget = handlerStart + 50;
    for (int i = 0; i < 4; i++) {
        _server.handleClient();
        if (millis() >= budget) break;
    }

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
