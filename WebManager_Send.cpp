/**
 * @file    WebManager_Send.cpp
 * @brief   Send infrastructure: safeSend overloads, gzip delivery, safeStreamFile, client disconnect detection.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include <hardware/watchdog.h>

using ReadGuard = StorageManager::ReadGuard;

/* v3.36.2 (A7): single point pra observabilidade de broken pipe.
 * Throttle 5s evita inundar log quando um handler envia N chunks após o
 * cliente fechar. `origin` identifica overload (s/sP/sN/gz) — útil pra
 * rastrear qual streaming morreu. Sem stack trace; só sinal binário. */
void WebManager::maybeLogClientDisconnect(const char* origin) {
    uint32_t now = millis();
    if (now - _lastDisconnectLogMs < 5000) return;
    _lastDisconnectLogMs = now;
    LogManager::instance().log(LOG_WARN, "WEB", WEB_CLIENT_DISCONNECT, String(origin));
}

bool WebManager::safeSend(const char* content) {
    if (isClientGone()) { maybeLogClientDisconnect("s/early"); return false; }

    _server.client().setTimeout(500);
    feedWatchdog();
    {
        SendGuard sg;
        _server.sendContent(content);
    }
    bool gone = isClientGone();
    if (gone) maybeLogClientDisconnect("s/post");
    return !gone;
}

bool WebManager::safeSend(const char* data, size_t len) {
    if (isClientGone()) { maybeLogClientDisconnect("sN/early"); return false; }

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
    bool gone = isClientGone();
    if (gone) maybeLogClientDisconnect("sN/post");
    return !gone;
}

bool WebManager::safeSend(const String& content) {
    if (isClientGone()) { maybeLogClientDisconnect("sStr/early"); return false; }

    _server.client().setTimeout(500);
    feedWatchdog();
    {
        SendGuard sg;
        _server.sendContent(content);
    }
    bool gone = isClientGone();
    if (gone) maybeLogClientDisconnect("sStr/post");
    return !gone;
}

bool WebManager::safeSend_P(const char* content) {
    if (isClientGone()) { maybeLogClientDisconnect("sP/early"); return false; }

    _server.client().setTimeout(500);
    feedWatchdog();
    {
        SendGuard sg;
        _server.sendContent_P(content);
    }
    bool gone = isClientGone();
    if (gone) maybeLogClientDisconnect("sP/post");
    return !gone;
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
