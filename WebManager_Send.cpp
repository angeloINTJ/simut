/**
 * @file    WebManager_Send.cpp
 * @brief   Send infrastructure: safeSend overloads, gzip delivery, safeStreamFile, client disconnect detection.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include <hardware/watchdog.h>

using ReadGuard = StorageManager::ReadGuard;

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
