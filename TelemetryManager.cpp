/**
 * @file    TelemetryManager.cpp
 * @brief   Implementation of TelemetryManager — batch collection, HTTP/MQTT transport, and payload builders.
 * @details Implements flash-efficient batch collection using ReadLock (no Core 1
 *          pause), HTTP upload with configurable auth headers, MQTT transport
 *          with LWT (Last Will & Testament), individual and batch publish
 *          strategies, exponential backoff with jitter, and three payload
 *          format builders (JSON, CSV, custom template).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "TelemetryManager.h"
#include <LittleFS.h>
#include <algorithm>
#include <string.h>
#include <hardware/watchdog.h>

TelemetryManager::TelemetryManager()
    : _mqttClient(_mqttWifiClient)
{
    _lastCheckTime = 0;
    _isSending = false;
    _hasCert = false;
    _currentBackoff = BACKOFF_MIN_MS;
    _backoffUntil = 0;
    _consecutiveFails = 0;
    _mqttInitialized = false;
    _lastMqttReconnect = 0;
}

/**
 * @brief Initialize telemetry transport (HTTP or MQTT) with SSL certificate loading.
 * SSL certificates are cached in RAM at boot for reuse across uploads.
 */
void TelemetryManager::begin(StorageManager* storage, NetworkManager* network) {
    _storageRef = storage;
    _netRef = network;


    _hasCert = false;
    _cachedCert = "";

    SystemConfig &cfg = _storageRef->getConfig();
    if (cfg.telEncryption) {
        if (LittleFS.exists("/cert.pem")) {
            File certFile = LittleFS.open("/cert.pem", "r");
            if (certFile) {
                _cachedCert = certFile.readString();
                certFile.close();
                if (_cachedCert.length() > 0) {
                    _hasCert = true;
                    LOG_CODE(LOG_INFO, "TEL", SYS_TEL_SSL, _cachedCert.length(), "SSL cert.pem loaded (" + String(_cachedCert.length()) + " bytes)");
                } else {
                    LOG_WRN("TEL", "cert.pem is empty, TLS will use insecure mode");
                }
            } else {
                LOG_WRN("TEL", "cert.pem read error, TLS will use insecure mode");
            }
        } else {
            LOG_INF("TEL", "No cert.pem found, TLS will use insecure mode");
        }
    }


    if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
        if (cfg.telEncryption) {
            if (_hasCert) {
                _mqttWifiClientSecure.setCACert(_cachedCert.c_str());
            } else {
                _mqttWifiClientSecure.setInsecure();
            }
            _mqttClient.setClient(_mqttWifiClientSecure);
        } else {
            _mqttClient.setClient(_mqttWifiClient);
        }

        _mqttClient.setServer(cfg.telServer, cfg.telPort);
        _mqttClient.setKeepAlive(cfg.mqttKeepAlive > 0 ? cfg.mqttKeepAlive : 60);


        _mqttClient.setBufferSize(2048);

        _mqttInitialized = true;
        LOG_INF("TEL", "MQTT transport initialized -> " + String(cfg.telServer) + ":" + String(cfg.telPort));
    } else {
        LOG_INF("TEL", "HTTP transport initialized -> " + String(cfg.telServer) + ":" + String(cfg.telPort) + String(cfg.telPath));
    }

    resetBackoff();
}

/**
 * @brief Periodic telemetry check — collects batch and dispatches via configured transport.
 * Respects backoff intervals, network availability, and heavy task locks.
 */
void TelemetryManager::update() {
    SystemConfig &cfg = _storageRef->getConfig();
    if (cfg.telInterval == 0) return;


    if (cfg.telTransport == TEL_TRANSPORT_MQTT && _mqttInitialized) {
        _mqttClient.loop();
        watchdog_update();
    }

    uint32_t now = millis();

    if (_consecutiveFails > 0 && now < _backoffUntil) return;
    if (_consecutiveFails == 0 && (now - _lastCheckTime < cfg.telInterval)) return;

    _lastCheckTime = now;


    if (_isSending) return;
    if (!_netRef->isConnected()) return;
    if (!_storageRef->lockHeavyTask()) return;

    _isSending = true;

    std::vector<String> batch;
    uint32_t newCursor = 0;

    if (!collectBatch(batch, newCursor)) {

        _isSending = false;
        _storageRef->unlockHeavyTask();
        resetBackoff();
        return;
    }


    bool success = false;

    if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
        success = attemptMqttPublish(batch, newCursor);
    } else {
        success = attemptHttpUpload(batch, newCursor);
    }

    _isSending = false;
    _storageRef->unlockHeavyTask();

    if (success) {
        resetBackoff();
    } else {
        escalateBackoff();
    }

    /* Sinalizar resultado para o display */
    _lastSendSuccess = success;
    _hasSendResult   = true;
}


/* =========================================================================== */
/*                    BATCH COLLECTION (SHARED HTTP/MQTT)                    */
/* =========================================================================== */
/**
 * @brief Collect pending history records into a batch for upload.
 * Uses lightweight ReadLock (no Core 1 pause) for flash I/O.
 * @return false if no pending data (success — nothing to send).
 */
bool TelemetryManager::collectBatch(std::vector<String>& batch, uint32_t& newCursor) {
    SystemConfig &cfg = _storageRef->getConfig();
    uint32_t lastCursor = _storageRef->getLastSentTimestamp();
    newCursor = lastCursor;


    std::vector<String> files;
    {
        _storageRef->enterFlashReadLock();
        Dir dir = LittleFS.openDir(DIR_HISTORY);
        while (dir.next()) {
            if (dir.fileName().endsWith(".csv")) {
                files.push_back(dir.fileName());
            }
        }
        _storageRef->exitFlashReadLock();
    }
    std::sort(files.begin(), files.end());

    String minFileName = "";
    if (lastCursor > 1000000000) {
        time_t tc = lastCursor + (cfg.timezoneOffset * 3600);
        struct tm timeinfo;
        gmtime_r(&tc, &timeinfo);
        char buff[24];
        snprintf(buff, sizeof(buff), "%04d%02d%02d.csv", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        minFileName = String(buff);
    }

    uint8_t limit = (cfg.telBatchSize > 0) ? cfg.telBatchSize : 10;


    for (const String& fn : files) {
        if (batch.size() >= limit) break;
        if (minFileName.length() > 0 && fn < minFileName) continue;

        String fullPath = String(DIR_HISTORY) + "/" + fn;

        _storageRef->enterFlashReadLock();
        File f = LittleFS.open(fullPath, "r");
        if (!f) { _storageRef->exitFlashReadLock(); continue; }

        bool hasMore = true;
        while (hasMore && batch.size() < limit) {

            char lineBuf[256];
            int linesRead = 0;


            static char tempLineBuf[20][256];
            static uint32_t tempCursors[20];
            int tempCount = 0;

            while (f.available() && linesRead < 20 && tempCount < 20) {
                size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
                if (len == 0) continue;
                lineBuf[len] = '\0';
                if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
                linesRead++;


                char* trimmed = lineBuf;
                while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
                if (*trimmed == '\0') continue;

                char* semi = strchr(trimmed, ';');
                if (semi) {
                    char* endPtr;
                    uint32_t ts = strtoul(trimmed, &endPtr, 10);
                    if (ts > lastCursor) {
                        strncpy(tempLineBuf[tempCount], trimmed, 255);
                        tempLineBuf[tempCount][255] = '\0';
                        tempCursors[tempCount] = ts;
                        tempCount++;
                    }
                }
            }
            hasMore = f.available();


            for (int i = 0; i < tempCount && batch.size() < limit; i++) {
                batch.push_back(String(tempLineBuf[i]));
                if (tempCursors[i] > newCursor) newCursor = tempCursors[i];
            }

            if (hasMore && batch.size() < limit) {

                _storageRef->exitFlashReadLock();
                watchdog_update();
                TRACE_BEAT(0);
                delay(1);
                _storageRef->enterFlashReadLock();
            }
        }
        f.close();
        _storageRef->exitFlashReadLock();


        watchdog_update();
        TRACE_BEAT(0);
    }

    return !batch.empty();
}


/* =========================================================================== */
/*                              HTTP TRANSPORT                               */
/* =========================================================================== */
/** @brief Upload a batch via HTTP POST with configurable auth headers. */
bool TelemetryManager::attemptHttpUpload(std::vector<String>& batch, uint32_t newCursor) {
    SystemConfig &cfg = _storageRef->getConfig();

    String payload = buildPayload(batch);
    watchdog_update();
    TRACE_BEAT(0);

    HTTPClient http;
    WiFiClient client;
    WiFiClientSecure clientSecure;

    String protocol = cfg.telEncryption ? "https://" : "http://";
    String url = protocol + String(cfg.telServer) + ":" + String(cfg.telPort) + String(cfg.telPath);
    bool connected = false;

    if (cfg.telEncryption) {
        if (_hasCert) {
            clientSecure.setCACert(_cachedCert.c_str());
        } else {
            clientSecure.setInsecure();
        }
        connected = http.begin(clientSecure, url);
    } else {
        connected = http.begin(client, url);
    }

    bool success = false;

    if (connected) {
        if (cfg.telMode == TEL_MODE_JSON) http.addHeader("Content-Type", "application/json");
        else if (cfg.telMode == TEL_MODE_CSV) http.addHeader("Content-Type", "text/csv");
        else http.addHeader("Content-Type", "text/plain");

        String tokenStr = String(cfg.telApiKey);
        tokenStr.trim();
        if (tokenStr.length() > 0) {
            int colonIdx = tokenStr.indexOf(':');
            if (colonIdx > 0) {
                String hName = tokenStr.substring(0, colonIdx);
                String hVal = tokenStr.substring(colonIdx + 1);
                hName.trim(); hVal.trim();
                http.addHeader(hName, hVal);
            } else {
                http.addHeader("Authorization", "Bearer " + tokenStr);
            }
        }

        http.setTimeout(7000);
        watchdog_update();
        TRACE_BEAT(0);
        int code = http.POST(payload);
        watchdog_update();

        if (code > 0) {
            LOG_CODE(LOG_INFO, "TEL", SYS_TEL_SENT, code, "HTTP OK: " + String(batch.size()) + " items, code " + String(code));
            if (code >= 200 && code < 300) {
                _storageRef->setLastSentTimestamp(newCursor);
                success = true;
            }
        } else {
            LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, code, "HTTP error: " + http.errorToString(code));
        }
        http.end();
    }

    return success;
}


String TelemetryManager::buildMqttClientId() {
    SystemConfig &cfg = _storageRef->getConfig();
    String cid = String(cfg.mqttClientId);
    cid.trim();
    if (cid.length() > 0) return cid;


    String mac = _netRef->getMacAddress();
    mac.replace(":", "");
    if (mac.length() >= 6) {
        return "simut_" + mac.substring(mac.length() - 6);
    }
    return "simut_device";
}

/**
 * @brief Ensure MQTT broker connection with LWT (Last Will & Testament).
 * Rate-limited to one reconnection attempt every 5 seconds.
 */
bool TelemetryManager::mqttEnsureConnected() {
    if (_mqttClient.connected()) return true;


    uint32_t now = millis();
    if (now - _lastMqttReconnect < 5000) return false;
    _lastMqttReconnect = now;

    SystemConfig &cfg = _storageRef->getConfig();
    String clientId = buildMqttClientId();
    String devName = String(cfg.deviceName);


    String willTopic = String(cfg.mqttTopic);

    int lastSlash = willTopic.lastIndexOf('/');
    String willTopicFull;
    if (lastSlash > 0) {
        willTopicFull = willTopic.substring(0, lastSlash) + "/status";
    } else {
        willTopicFull = willTopic + "/status";
    }

    String willPayload = "{\"device\":\"" + devName + "\",\"status\":\"offline\"}";

    LOG_INF("TEL", "MQTT connecting as '" + clientId + "'...");
    watchdog_update();
    TRACE_BEAT(0);

    bool connected = false;
    String user = String(cfg.mqttUser);
    String pass = String(cfg.mqttPass);
    user.trim();
    pass.trim();

    if (user.length() > 0) {
        connected = _mqttClient.connect(
            clientId.c_str(),
            user.c_str(),
            pass.c_str(),
            willTopicFull.c_str(),
            0,
            true,
            willPayload.c_str()
        );
    } else {
        connected = _mqttClient.connect(
            clientId.c_str(),
            nullptr,
            nullptr,
            willTopicFull.c_str(),
            0,
            true,
            willPayload.c_str()
        );
    }

    watchdog_update();

    if (connected) {
        LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_CONN, 0, "MQTT connected to " + String(cfg.telServer));


        String onlinePayload = "{\"device\":\"" + devName + "\",\"status\":\"online\",\"ip\":\"" + _netRef->getIpAddress() + "\"}";
        _mqttClient.publish(willTopicFull.c_str(), onlinePayload.c_str(), true);

        return true;
    } else {
        int state = _mqttClient.state();
        String reason;
        switch (state) {
            case -4: reason = "Connection timeout"; break;
            case -3: reason = "Connection lost"; break;
            case -2: reason = "Connect failed"; break;
            case -1: reason = "Disconnected"; break;
            case  1: reason = "Bad protocol"; break;
            case  2: reason = "Client ID rejected"; break;
            case  3: reason = "Server unavailable"; break;
            case  4: reason = "Bad credentials"; break;
            case  5: reason = "Not authorized"; break;
            default: reason = "Unknown (" + String(state) + ")"; break;
        }
        LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_MQTT_DISC, state, "MQTT failed: " + reason);
        return false;
    }
}

/**
 * @brief Publish batch via MQTT — individual messages for small batches,
 * single payload for large batches (threshold: 5 items).
 */
bool TelemetryManager::attemptMqttPublish(std::vector<String>& batch, uint32_t newCursor) {
    if (!_mqttInitialized) return false;

    if (!mqttEnsureConnected()) return false;

    SystemConfig &cfg = _storageRef->getConfig();
    String topic = String(cfg.mqttTopic);
    topic.trim();
    if (topic.length() == 0) topic = "simut/data";


    bool success = false;

    if (batch.size() <= 5) {

        int published = 0;
        for (size_t i = 0; i < batch.size(); i++) {
            watchdog_update();
            TRACE_BEAT(0);

            String linePayload;
            if (cfg.telMode == TEL_MODE_JSON) {
                linePayload = formatLineJson(batch[i], cfg);
            } else if (cfg.telMode == TEL_MODE_CSV) {
                linePayload = batch[i];
            } else {
                linePayload = formatLineCustom(batch[i], cfg);
            }

            bool ok = _mqttClient.publish(
                topic.c_str(),
                linePayload.c_str(),
                cfg.mqttRetain
            );

            if (ok) published++;
            else break;


            _mqttClient.loop();
        }

        if (published > 0) {
            LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_PUB, published,
                "MQTT published " + String(published) + "/" + String(batch.size()) + " items to " + topic);
        }

        if (published == (int)batch.size()) {
            _storageRef->setLastSentTimestamp(newCursor);
            success = true;
        } else if (published > 0) {


            String& lastLine = batch[published - 1];
            int p = lastLine.indexOf(';');
            if (p > 0) {
                uint32_t partialCursor = lastLine.substring(0, p).toInt();
                _storageRef->setLastSentTimestamp(partialCursor);
            }
            success = false;
        }
    } else {

        String payload = buildPayload(batch);
        watchdog_update();
        TRACE_BEAT(0);


        if (payload.length() > _mqttClient.getBufferSize()) {

            uint16_t needed = min((size_t)12288, payload.length() + 64);
            _mqttClient.setBufferSize(needed);
        }

        bool ok = _mqttClient.publish(
            topic.c_str(),
            payload.c_str(),
            cfg.mqttRetain
        );

        if (ok) {
            LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_PUB, batch.size(),
                "MQTT batch OK: " + String(batch.size()) + " items (" + String(payload.length()) + " bytes)");
            _storageRef->setLastSentTimestamp(newCursor);
            success = true;
        } else {
            LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, _mqttClient.state(),
                "MQTT publish failed (payload " + String(payload.length()) + " bytes)");
        }
    }

    return success;
}

bool TelemetryManager::isMqttConnected() {
    if (!_mqttInitialized) return false;
    return _mqttClient.connected();
}


void TelemetryManager::resetBackoff() {
    _currentBackoff = BACKOFF_MIN_MS;
    _consecutiveFails = 0;
    _backoffUntil = 0;
}

void TelemetryManager::escalateBackoff() {
    _consecutiveFails++;
    _backoffUntil = millis() + jitter(_currentBackoff);

    if (_consecutiveFails <= BACKOFF_MAX_STREAK) {
        LOG_CODE(LOG_WARN, "TEL", SYS_TEL_RETRY, _consecutiveFails, "Upload failed (#" + String(_consecutiveFails) + "). Retry in " + String(_currentBackoff / 1000) + "s");
    } else if (_consecutiveFails == BACKOFF_MAX_STREAK + 1) {
        LOG_WRN("TEL", "Suppressing further retry logs until success.");
    }

    _currentBackoff = min(_currentBackoff * 2, BACKOFF_MAX_MS);
}

uint32_t TelemetryManager::jitter(uint32_t base) {
    uint32_t quarter = base / 4;
    return base - quarter + (random(0, quarter * 2));
}

bool TelemetryManager::forceSync() {
    LOG_INF("TEL", "Forcing Sync...");
    resetBackoff();

    if (!_netRef->isConnected()) return false;
    if (!_storageRef->lockHeavyTask()) return false;

    _isSending = true;

    std::vector<String> batch;
    uint32_t newCursor = 0;

    if (!collectBatch(batch, newCursor)) {
        _isSending = false;
        _storageRef->unlockHeavyTask();
        return true;
    }

    SystemConfig &cfg = _storageRef->getConfig();
    bool ok;
    if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
        ok = attemptMqttPublish(batch, newCursor);
    } else {
        ok = attemptHttpUpload(batch, newCursor);
    }

    _isSending = false;
    _storageRef->unlockHeavyTask();

    if (!ok) escalateBackoff();
    return ok;
}


/* =========================================================================== */
/*                             PAYLOAD BUILDERS                              */
/* =========================================================================== */
/** @brief Build the upload payload in the configured format (JSON/CSV/Custom). */
String TelemetryManager::buildPayload(std::vector<String>& batch) {
    SystemConfig &cfg = _storageRef->getConfig();
    String s = "";
    s.reserve(batch.size() * 256 + 128);

    String devName = String(cfg.deviceName);
    String macAddr = _netRef->getMacAddress();

    if (cfg.telMode == TEL_MODE_JSON) {
        s = "[";
        for (size_t i = 0; i < batch.size(); i++) {
            s += formatLineJson(batch[i], cfg);
            if (i < batch.size() - 1) s += ",";
        }
        s += "]";
    } else if (cfg.telMode == TEL_MODE_CSV) {
        s = "timestamp;ambT;ambH";
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (cfg.sensors[i].active) s += ";s" + String(i) + "_" + String(cfg.sensors[i].hwId);
        }
        s += "\n";
        for (String& line : batch) s += line + "\n";
    } else if (cfg.telMode == 2) {
        String dataBlocks = "";
        String separator = String(cfg.telLineSeparator);
        if (separator == "\\n") separator = "\n";

        for (size_t i = 0; i < batch.size(); i++) {
            dataBlocks += formatLineCustom(batch[i], cfg);
            if (i < batch.size() - 1) dataBlocks += separator;
        }

        s = String(cfg.telGlobalTemplate);
        s.replace("{DEV}", devName);
        s.replace("{MAC}", macAddr);
        s.replace("{DATA}", dataBlocks);
    }
    return s;
}

String TelemetryManager::formatLineJson(String& line, const SystemConfig& cfg) {
    line.trim();
    if (line.length() == 0) return "";

    String parts[15];
    int partCnt = 0;
    int start = 0;
    int end = line.indexOf(';');

    while (end != -1 && partCnt < 14) {
        parts[partCnt++] = line.substring(start, end);
        start = end + 1;
        end = line.indexOf(';', start);
    }
    parts[partCnt++] = line.substring(start);

    if (partCnt == 0 || parts[0].length() == 0) return "";

    char jBuffer[512];
    jBuffer[0] = '\0';

    snprintf(jBuffer, sizeof(jBuffer), "{\"ts\":%s", parts[0].c_str());

    char tempBuf[64];

    auto isValidData = [](const String& val) {
        return (val.length() > 0 && val != "--" && val != "nan" && val != "null");
    };

    if (partCnt > 1 && isValidData(parts[1])) {
        snprintf(tempBuf, sizeof(tempBuf), ",\"tAMB\":%s", parts[1].c_str());
        strlcat(jBuffer, tempBuf, sizeof(jBuffer));
    }

    if (partCnt > 2 && isValidData(parts[2])) {
        snprintf(tempBuf, sizeof(tempBuf), ",\"uAMB\":%s", parts[2].c_str());
        strlcat(jBuffer, tempBuf, sizeof(jBuffer));
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].active) {
            int csvIndex = 3 + i;
            if (csvIndex < partCnt && isValidData(parts[csvIndex])) {
                String hwid = String(cfg.sensors[i].hwId);
                if (hwid.length() == 0) hwid = String(i);

                snprintf(tempBuf, sizeof(tempBuf), ",\"t%s\":%s", hwid.c_str(), parts[csvIndex].c_str());
                strlcat(jBuffer, tempBuf, sizeof(jBuffer));
            }
        }
    }
    strlcat(jBuffer, "}", sizeof(jBuffer));

    return String(jBuffer);
}

String TelemetryManager::formatLineCustom(String& line, const SystemConfig& cfg) {
    String parts[15];
    int pCount = 0;
    int start = 0, end = line.indexOf(';');

    while (end != -1 && pCount < 14) {
        parts[pCount++] = line.substring(start, end);
        start = end + 1;
        end = line.indexOf(';', start);
    }
    parts[pCount++] = line.substring(start);

    String ts = (pCount > 0) ? parts[0] : "";
    String ambT = (pCount > 1) ? parts[1] : "";
    String ambH = (pCount > 2) ? parts[2] : "";

    String slotVals[MAX_SENSORS];
    for(int i=0; i<MAX_SENSORS; i++) {
        slotVals[i] = (pCount > 3 + i) ? parts[3 + i] : "";
    }

    String out = String(cfg.telLineTemplate);
    out.replace("{TS}", ts);

    String boardSerial = _storageRef->getBoardSerialNumber();
    out.replace("{DHT_ID}", boardSerial);

    if (ambT.length() > 0 && ambT != "null" && ambT != "--" && ambT != "nan") {
        out.replace("\"tAMB_ID\":{tAMB}", "\"t" + boardSerial + "\":" + ambT);
        out.replace("\"tAMB\":{tAMB}", "\"tAMB\":" + ambT);
        out.replace("{tAMB}", ambT);
    } else {
        out.replace("\"tAMB_ID\":{tAMB}", "");
        out.replace("\"tAMB\":{tAMB}", "");
        out.replace("{tAMB}", "null");
    }

    if (ambH.length() > 0 && ambH != "null" && ambH != "--" && ambH != "nan") {
        out.replace("\"uAMB_ID\":{uAMB}", "\"u" + boardSerial + "\":" + ambH);
        out.replace("\"uAMB\":{uAMB}", "\"uAMB\":" + ambH);
        out.replace("{uAMB}", ambH);
    } else {
        out.replace("\"uAMB_ID\":{uAMB}", "");
        out.replace("\"uAMB\":{uAMB}", "");
        out.replace("{uAMB}", "null");
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        String val = slotVals[i];
        val.trim();
        String tagVal = "{t" + String(i) + "}";
        String tagKeyFull = "\"t" + String(i) + "\":" + tagVal;

        if (cfg.sensors[i].active && val.length() > 0 && val != "null" && val != "--" && val != "nan") {
            String hwid = String(cfg.sensors[i].hwId);
            hwid.trim();
            out.replace(tagKeyFull, "\"t" + hwid + "\":" + val);
            out.replace(tagVal, val);
        } else {
            out.replace(tagKeyFull, "");
            out.replace(tagVal, "null");
        }
    }

    while(out.indexOf(",,") != -1) {
        out.replace(",,", ",");
    }
    out.replace("{,", "{");
    out.replace(",}", "}");
    out.replace("[,", "[");
    out.replace(",]", "]");

    return out;
}


/**
 * @brief Count pending telemetry records by scanning history CSVs.
 * Called periodically (~10s) by AppManager for dashboard display.
 */
void TelemetryManager::refreshPendingCount() {
    SystemConfig& cfg = _storageRef->getConfig();
    uint32_t lastCursor = _storageRef->getLastSentTimestamp();


    std::vector<String> files;
    {
        _storageRef->enterFlashReadLock();
        Dir dir = LittleFS.openDir(DIR_HISTORY);
        while (dir.next()) {
            if (dir.fileName().endsWith(".csv")) {
                files.push_back(dir.fileName());
            }
        }
        _storageRef->exitFlashReadLock();
    }


    String minFileName = "";
    if (lastCursor > 1000000000) {
        time_t tc = lastCursor + (cfg.timezoneOffset * 3600);
        struct tm timeinfo;
        gmtime_r(&tc, &timeinfo);
        char buff[24];
        snprintf(buff, sizeof(buff), "%04d%02d%02d.csv",
                 timeinfo.tm_year + 1900,
                 timeinfo.tm_mon + 1,
                 timeinfo.tm_mday);
        minFileName = String(buff);
    }

    uint16_t total = 0;
    char lineBuf[256];


    for (const String& fn : files) {
        if (minFileName.length() > 0 && fn < minFileName) continue;

        String fullPath = String(DIR_HISTORY) + "/" + fn;

        _storageRef->enterFlashReadLock();
        File f = LittleFS.open(fullPath, "r");
        if (!f) { _storageRef->exitFlashReadLock(); continue; }

        while (f.available()) {
            size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
            if (len == 0) continue;
            lineBuf[len] = '\0';

            char* trimmed = lineBuf;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
            if (*trimmed == '\0') continue;

            if (strchr(trimmed, ';')) {
                uint32_t ts = strtoul(trimmed, nullptr, 10);
                if (ts > lastCursor) total++;
            }
        }
        f.close();
        _storageRef->exitFlashReadLock();

        watchdog_update();
        TRACE_BEAT(0);
    }

    _pendingEstimate = total;
}


uint16_t TelemetryManager::getPendingEstimate() const {
    return _pendingEstimate;
}


/**
 * @brief Consume the result of the last telemetry send attempt.
 *
 * Returns true if a send occurred since the last call, filling
 * outSuccess with the result. The flag is cleared after consumption,
 * ensuring each result is processed exactly once.
 */
bool TelemetryManager::consumeLastSendResult(bool& outSuccess) {
    if (_hasSendResult) {
        outSuccess     = _lastSendSuccess;
        _hasSendResult = false;
        return true;
    }
    return false;
}
