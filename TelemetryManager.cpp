/**
 * @file    TelemetryManager.cpp
 * @brief   Implementation of TelemetryManager — batch collection, HTTP/MQTT transport, and payload builders.
 * @details Implements flash-efficient batch collection using ReadLock (no Core 1
 * pause), HTTP upload with configurable auth headers, MQTT transport
 * with LWT (Last Will & Testament), individual and batch publish
 * strategies, exponential backoff with jitter, and three payload
 * format builders (JSON, CSV, custom template).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.8.0
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "TelemetryManager.h"
#include <LittleFS.h>
#include <algorithm>
#include <string.h>
#include <hardware/watchdog.h>
#include <pico/time.h>

/**
 * @brief Alimenta o watchdog durante operações bloqueantes de rede (TLS/HTTP/MQTT).
 *
 * O http.POST() com TLS pode bloquear por 4-8s em rede saudável. Em rede
 * degradada (RSSI baixo, 2G, roaming), handshake + transferência pode
 * legitimamente estender até dezenas de segundos. Sem alimentação, o
 * watchdog (8.3s) dispara durante uma operação normal.
 *
 * O timer roda a cada 2s e alimenta enquanto o guard está ativo.
 * Safety: para de alimentar após WDT_FEED_MAX_WINDOW_MS (60s) para
 * evitar mascarar deadlocks reais — nesse ponto, o watchdog toma ação
 * como *safety net* final. HTTP/MQTT internos já têm
 * NET_SOCKET_TIMEOUT_MS=4s, então operações saudáveis não chegam a 60s.
 */
static volatile bool _telGuardActive = false;
static volatile uint32_t _telGuardStartMs = 0;
static struct repeating_timer _telGuardTimer;
static bool _telGuardTimerStarted = false;

static bool _telGuardCallback(struct repeating_timer *t) {
    (void)t;
    if (_telGuardActive) {
        uint32_t elapsed = millis() - _telGuardStartMs;
        if (elapsed < WDT_FEED_MAX_WINDOW_MS) {
            watchdog_update();
        }
    }
    return true;
}

struct TelemetryGuard {
    TelemetryGuard() {
        if (!_telGuardTimerStarted) {
            add_repeating_timer_ms(-2000, _telGuardCallback, nullptr, &_telGuardTimer);
            _telGuardTimerStarted = true;
        }
        _telGuardStartMs = millis();
        _telGuardActive = true;
    }
    ~TelemetryGuard() { _telGuardActive = false; }
};

TelemetryManager::TelemetryManager()
    : _mqttClient(_mqttWifiClient)
{
    _lastCheckTime = 0;
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
                /* N9: rejeitar cert > 16 KB para evitar OOM no boot */
                if (certFile.size() > 16384) {
                    LOG_CODE(LOG_WARN, "TEL", TEL_CERT_READ_ERR, (int)certFile.size(), "cert.pem too large");
                    certFile.close();
                } else {
                    _cachedCert = certFile.readString();
                    certFile.close();
                    if (_cachedCert.length() > 0) {
                        _hasCert = true;
                        LOG_CODE(LOG_INFO, "TEL", SYS_TEL_SSL, _cachedCert.length(), "SSL cert.pem loaded (" + String(_cachedCert.length()) + " bytes)");
                    } else {
                        LOG_CODE(LOG_WARN, "TEL", TEL_CERT_EMPTY, 0, "");
                    }
                }
            } else {
                LOG_CODE(LOG_WARN, "TEL", TEL_CERT_READ_ERR, 0, "");
            }
        } else {
            LOG_CODE(LOG_INFO, "TEL", TEL_CERT_MISSING, 0, "");
        }
    }


    if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
        if (cfg.telEncryption) {
            _mqttSecurePtr = new WiFiClientSecure();
            if (_mqttSecurePtr) {
                _mqttSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
                if (_hasCert) {
                    _mqttSecurePtr->setCACert(_cachedCert.c_str());
                } else {
                    _mqttSecurePtr->setInsecure();
                }
                _mqttClient.setClient(*_mqttSecurePtr);
            }
        } else {
            _mqttWifiClient.setTimeout(NET_SOCKET_TIMEOUT_MS);
            _mqttClient.setClient(_mqttWifiClient);
        }

        _mqttClient.setServer(cfg.telServer, cfg.telPort);
        _mqttClient.setKeepAlive(cfg.mqttKeepAlive > 0 ? cfg.mqttKeepAlive : 60);

        /* Socket timeout do PubSubClient: limita bloqueio de read/write */
        _mqttClient.setSocketTimeout(NET_SOCKET_TIMEOUT_MS / 1000);

        _mqttClient.setBufferSize(2048);

        _mqttInitialized = true;
        LOG_CODE(LOG_INFO, "TEL", TEL_MQTT_INIT, cfg.telPort, String(cfg.telServer));
    } else {
        /*
         * HTTP: pré-aloca WiFiClientSecure no boot para evitar fragmentação.
         * Se alocado tardiamente, a heap pode estar fragmentada demais para
         * o bloco contíguo de ~16KB que o TLS precisa.
         * U13: só aloca se telemetria está ativa (telInterval > 0).
         */
        if (cfg.telEncryption && cfg.telInterval > 0) {
            _httpSecurePtr = new WiFiClientSecure();
            if (_httpSecurePtr) {
                _httpSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
                if (_hasCert) _httpSecurePtr->setCACert(_cachedCert.c_str());
                else          _httpSecurePtr->setInsecure();
            }
        }
        LOG_CODE(LOG_INFO, "TEL", TEL_HTTP_INIT, cfg.telPort, String(cfg.telServer) + String(cfg.telPath));
    }

    resetBackoff();

    /*
     * Inicia o timer com millis() atual para que a primeira tentativa
     * de telemetria aguarde um intervalo completo após o boot.
     * Sem isso, _lastCheckTime=0 causa disparo imediato na primeira
     * iteração do loop — o TLS handshake + POST pode exceder o watchdog.
     */
    _lastCheckTime = millis();
}

/**
 * @brief Periodic telemetry check — collects batch and dispatches via configured transport.
 * Respects backoff intervals, network availability, and heavy task locks.
 */
void TelemetryManager::update() {
    SystemConfig &cfg = _storageRef->getConfig();
    if (cfg.telInterval == 0) return;

    /*
     * MQTT keepalive: chama loop() apenas se conectado ao broker.
     * Evita que loop() tente reconnect implícito com socket timeout
     * longo que congelaria o main loop em rede degradada.
     */
    if (cfg.telTransport == TEL_TRANSPORT_MQTT && _mqttInitialized
        && _mqttClient.connected()) {
        _mqttClient.loop();
        watchdog_update();
    }

    uint32_t now = millis();

    if (_consecutiveFails > 0 && now < _backoffUntil) return;
    if (_consecutiveFails == 0 && (now - _lastCheckTime < cfg.telInterval)) return;

    _lastCheckTime = now;


    /* CAS atômico: impede race entre update() periódico e forceSync() CLI */
    bool expected = false;
    if (!__atomic_compare_exchange_n(&_isSending, &expected, true,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return;
    if (!_netRef->isNetworkHealthy()) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return; }
    if (!_storageRef->lockHeavyTask()) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return; }

    /* Aborta se heap está criticamente baixa para evitar hard fault */
    if (rp2040.getFreeHeap() < 20480) {
        _storageRef->unlockHeavyTask();
        __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
        escalateBackoff();
        return;
    }

    std::vector<BinaryHistoryRecord> batch;
    uint32_t newCursor = 0;

    if (!collectBatch(batch, newCursor)) {
        __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
        _storageRef->unlockHeavyTask();
        resetBackoff();
        return;
    }


    bool success = false;

    if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
        /*
         * MQTT: precisa do batch para publish individual (≤5 itens).
         * Para batches maiores, buildPayload + free batch.
         */
        String payload = buildPayload(batch);
        success = attemptMqttPublish(payload, batch, newCursor);
        /* batch e payload saem de escopo aqui e liberam memória */
    } else {
        /*
         * HTTP: constrói payload, libera batch ANTES do POST.
         * Isso evita que batch (~7KB) + payload (~13KB) + TLS (~16KB)
         * coexistam em RAM simultaneamente.
         */
        String payload = buildPayload(batch);

        /* Libera batch para reduzir pico de RAM antes do TLS handshake */
        batch.clear();
        batch.shrink_to_fit();

        success = attemptHttpUpload(payload, newCursor);
    }

    __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
    _storageRef->unlockHeavyTask();
    _pendingDirty = true;  /* Recalibrar após envio */

    if (success) {
        resetBackoff();
    } else {
        escalateBackoff();
    }

    /* Sinalizar resultado para o display */
    _lastSendSuccess = success;
    _hasSendResult   = true;

    /* Libera recursos TLS ociosos para recuperar heap */
    releaseIdleResources();
}


/* =========================================================================== */
/*                    BATCH COLLECTION (SHARED HTTP/MQTT)                    */
/* =========================================================================== */
/**
 * @brief Collect pending history records into a batch for upload.
 * Uses lightweight ReadLock (no Core 1 pause) for flash I/O.
 * @return false if no pending data (success — nothing to send).
 */
/**
 * @brief Calcula limite seguro de batch baseado na heap disponível.
 *
 * Cada entrada consome ~450 bytes (200 batch + 250 payload).
 * Reserva 20KB de headroom para WiFi/HTTP/MQTT/TLS.
 * Nunca retorna mais que o valor configurado.
 *
 * @param configured  Limite máximo configurado pelo usuário.
 * @return            Limite efetivo (≥1, ≤configured).
 */
uint8_t TelemetryManager::safeBatchLimit(uint8_t configured) {
    uint32_t freeHeap = rp2040.getFreeHeap();
    const uint32_t HEAP_RESERVE   = 28672; /* 28KB para WiFi + TLS + stack */
    const uint32_t BYTES_PER_ENTRY = 600;  /* ~28 batch + ~250 payload + ~300 String temporários */
    const uint8_t  HARD_CAP       = 50;    /* Máximo absoluto por envio */

    if (freeHeap <= HEAP_RESERVE) return 1;

    uint8_t heapLimit = (uint8_t)min((uint32_t)255,
                                     (freeHeap - HEAP_RESERVE) / BYTES_PER_ENTRY);
    return max((uint8_t)1, min(min(configured, HARD_CAP), heapLimit));
}

bool TelemetryManager::collectBatch(std::vector<BinaryHistoryRecord>& batch, uint32_t& newCursor) {
    SystemConfig &cfg = _storageRef->getConfig();
    uint32_t lastCursor = _storageRef->getLastSentTimestamp();

    /* U8: fallback quando cursor é 0 (sem NTP / nunca enviou) —
     * usar último timestamp gravado - 30 dias para limitar varredura. */
    if (lastCursor == 0) {
        uint32_t lastRecorded = _storageRef->getLastRecordedTimestamp();
        if (lastRecorded > 86400UL * 30) lastCursor = lastRecorded - 86400UL * 30;
    }

    newCursor = lastCursor;


    std::vector<String> files;
    {
        _storageRef->enterFlashReadLock();
        Dir dir = LittleFS.openDir(DIR_HISTORY);
        while (dir.next()) {
            if (dir.fileName().endsWith(HISTORY_FILE_EXT)) {
                files.push_back(dir.fileName());
            }
        }
        _storageRef->exitFlashReadLock();
    }
    std::sort(files.begin(), files.end());

    String minFileName = "";
    if (lastCursor > 1000000000) {
        time_t cursorEpoch = (time_t)lastCursor;
        struct tm timeinfo;
        localtime_r(&cursorEpoch, &timeinfo);
        char buff[24];
        snprintf(buff, sizeof(buff), "%04d%02d%02d" HISTORY_FILE_EXT, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        minFileName = String(buff);
    }

    uint8_t limit = safeBatchLimit(
        (cfg.telBatchSize > 0) ? cfg.telBatchSize : 10);


    for (const String& fn : files) {
        if (batch.size() >= limit) break;
        if (minFileName.length() > 0 && fn < minFileName) continue;

        String fullPath = String(DIR_HISTORY) + "/" + fn;

        _storageRef->enterFlashReadLock();
        File f = LittleFS.open(fullPath, "r");
        if (!f) { _storageRef->exitFlashReadLock(); continue; }

        bool hasMore = true;
        while (hasMore && batch.size() < limit) {

            BinaryHistoryRecord tempBuf[10];
            uint32_t tempCursors[10];
            int tempCount = 0;

            while (f.available() >= HISTORY_RECORD_SIZE && tempCount < 10) {
                BinaryHistoryRecord rec;
                if (f.read((uint8_t*)&rec, HISTORY_RECORD_SIZE) != HISTORY_RECORD_SIZE) continue;

                if (rec.epoch > lastCursor) {
                    tempBuf[tempCount] = rec;
                    tempCursors[tempCount] = rec.epoch;
                    tempCount++;
                }
            }
            hasMore = (f.available() >= HISTORY_RECORD_SIZE);

            for (int i = 0; i < tempCount && batch.size() < limit; i++) {
                batch.push_back(tempBuf[i]);
                if (tempCursors[i] > newCursor) newCursor = tempCursors[i];
            }

            if (hasMore && batch.size() < limit) {
                _storageRef->exitFlashReadLock();
                watchdog_update();
                TRACE_BEAT(0);
                yield();
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
bool TelemetryManager::attemptHttpUpload(String& payload, uint32_t newCursor) {
    SystemConfig &cfg = _storageRef->getConfig();

    watchdog_update();
    TRACE_BEAT(0);

    HTTPClient http;
    WiFiClient client;

    String protocol = cfg.telEncryption ? "https://" : "http://";
    String url = protocol + String(cfg.telServer) + ":" + String(cfg.telPort) + String(cfg.telPath);
    bool connected = false;

    if (cfg.telEncryption) {
        /* Reutiliza cliente TLS pré-alocado no begin() */
        if (!_httpSecurePtr) {
            _httpSecurePtr = new WiFiClientSecure();
            if (!_httpSecurePtr) {
                LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, 0, "OOM: WiFiClientSecure");
                return false;
            }
            _httpSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
        }
        _httpSecureLastUse = millis();

        if (_hasCert) {
            _httpSecurePtr->setCACert(_cachedCert.c_str());
        } else {
            _httpSecurePtr->setInsecure();
        }
        connected = http.begin(*_httpSecurePtr, url);
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

        http.setTimeout(NET_SOCKET_TIMEOUT_MS);
        watchdog_update();
        TRACE_BEAT(0);

        int code;
        {
            TelemetryGuard tg;  /* Alimenta watchdog durante POST bloqueante */
            code = http.POST(payload);
        }
        watchdog_update();

        if (code > 0) {
            LOG_CODE(LOG_INFO, "TEL", SYS_TEL_SENT, code, "HTTP OK: " + String(payload.length()) + " bytes, code " + String(code));
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

    LOG_CODE(LOG_INFO, "TEL", TEL_MQTT_CONNECTING, 0, clientId);
    watchdog_update();
    TRACE_BEAT(0);

    bool connected = false;
    String user = String(cfg.mqttUser);
    String pass = String(cfg.mqttPass);
    user.trim();
    pass.trim();

    {
        TelemetryGuard tg;  /* Alimenta watchdog durante connect bloqueante */
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
bool TelemetryManager::attemptMqttPublish(String& payload, std::vector<BinaryHistoryRecord>& batch, uint32_t newCursor) {
    if (!_mqttInitialized) return false;

    if (!mqttEnsureConnected()) return false;

    SystemConfig &cfg = _storageRef->getConfig();
    String topic = String(cfg.mqttTopic);
    topic.trim();
    if (topic.length() == 0) topic = "simut/data";


    bool success = false;

    if (batch.size() <= 5) {
        /* Lote pequeno: publica cada linha individualmente */
        int published = 0;
        for (size_t i = 0; i < batch.size(); i++) {
            watchdog_update();
            TRACE_BEAT(0);

            String linePayload;
            if (cfg.telMode == TEL_MODE_JSON) {
                linePayload = formatLineJson(batch[i], cfg);
            } else if (cfg.telMode == TEL_MODE_CSV) {
                char csvBuf[256];
                batch[i].toCsvLine(csvBuf, sizeof(csvBuf));
                linePayload = String(csvBuf);
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
            uint32_t partialCursor = batch[published - 1].epoch;
            _storageRef->setLastSentTimestamp(partialCursor);
            success = false;
        }
    } else {
        /* Lote grande: usa payload pré-construído pelo caller */
        watchdog_update();
        TRACE_BEAT(0);

        if (payload.length() > _mqttClient.getBufferSize()) {
            uint16_t needed = min((size_t)8192, payload.length() + 64);
            _mqttClient.setBufferSize(needed);
        }

        bool ok;
        {
            TelemetryGuard tg;  /* Alimenta watchdog durante publish bloqueante */
            ok = _mqttClient.publish(
                topic.c_str(),
                payload.c_str(),
                cfg.mqttRetain
            );
        }

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
        LOG_CODE(LOG_WARN, "TEL", TEL_BACKOFF_SUPPRESSED, 0, "");
        _lastSuppressedLog = millis();
    } else if (millis() - _lastSuppressedLog >= 3600000) {
        /* U11: heartbeat 1x/hora após supressão */
        LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, _consecutiveFails,
            "Still failing (#" + String(_consecutiveFails) + ")");
        _lastSuppressedLog = millis();
    }

    _currentBackoff = min(_currentBackoff * 2, BACKOFF_MAX_MS);
}

uint32_t TelemetryManager::jitter(uint32_t base) {
    uint32_t quarter = base / 4;
    return base - quarter + (random(0, quarter * 2));
}

/**
 * @brief Libera recursos TLS ociosos para recuperar heap.
 *
 * - HTTP WiFiClientSecure: liberado após 60s sem uso (~16KB)
 * - MQTT WiFiClientSecure: liberado se MQTT desconectado há >60s (~16KB)
 * - Certificado SSL: liberado se nenhum cliente TLS está ativo (~2KB)
 */
void TelemetryManager::releaseIdleResources() {
    /*
     * TLS clients (WiFiClientSecure) NÃO são liberados.
     * Os ~16KB são um custo permanente de usar criptografia.
     * Liberar e realocar causa fragmentação de heap que leva a
     * hard faults quando o bloco contíguo de 16KB não existe mais.
     *
     * O certificado também permanece em RAM enquanto houver cliente TLS.
     */
}

bool TelemetryManager::forceSync() {
    LOG_CODE(LOG_INFO, "TEL", TEL_FORCE_SYNC, 0, "");
    resetBackoff();

    bool expected = false;
    if (!__atomic_compare_exchange_n(&_isSending, &expected, true,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return false;
    if (!_netRef->isNetworkHealthy()) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return false; }
    if (!_storageRef->lockHeavyTask()) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return false; }

    std::vector<BinaryHistoryRecord> batch;
    uint32_t newCursor = 0;

    if (!collectBatch(batch, newCursor)) {
        __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
        _storageRef->unlockHeavyTask();
        return true;
    }

    SystemConfig &cfg = _storageRef->getConfig();

    /* Constrói payload e libera batch para reduzir pico de RAM */
    String payload = buildPayload(batch);

    bool ok;
    if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
        ok = attemptMqttPublish(payload, batch, newCursor);
    } else {
        batch.clear();
        batch.shrink_to_fit();
        ok = attemptHttpUpload(payload, newCursor);
    }

    __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
    _storageRef->unlockHeavyTask();
    _pendingDirty = true;  /* Recalibrar após envio */

    if (!ok) escalateBackoff();
    return ok;
}


/* =========================================================================== */
/*                             PAYLOAD BUILDERS                              */
/* =========================================================================== */
/**
 * @brief Build the upload payload using fixed char buffers — zero heap fragmentation.
 *
 * Toda a construção é feita com snprintf/strlcat em buffers de stack.
 * O único objeto heap é a String `s` que é reservada uma única vez.
 * Nenhum String temporário é criado durante o loop → safe para 50+ registros.
 */
String TelemetryManager::buildPayload(std::vector<BinaryHistoryRecord>& batch) {
    SystemConfig &cfg = _storageRef->getConfig();

    /* Estima tamanho: JSON ~300 bytes/registro com 12 sensores */
    size_t perLine = (cfg.telMode == TEL_MODE_CSV) ? 120 : 300;
    size_t estimatedSize = batch.size() * perLine + 256;

    /* Verifica heap e reduz batch se necessário */
    uint32_t freeHeap = rp2040.getFreeHeap();
    if (freeHeap < estimatedSize + 8192) {
        size_t safeCount = (freeHeap > 8192) ? (freeHeap - 8192) / perLine : 1;
        if (safeCount < batch.size()) batch.resize(safeCount);
        estimatedSize = batch.size() * perLine + 256;
    }

    String s;
    s.reserve(estimatedSize);

    if (cfg.telMode == TEL_MODE_JSON) {
        /*
         * JSON: constrói diretamente com char buffer de stack.
         * formatLineJson escreve em lineBuf (512 bytes, stack).
         * s.concat(lineBuf, len) anexa sem criar String temporário.
         */
        s = "[";
        char lineBuf[512];
        for (size_t i = 0; i < batch.size(); i++) {
            if (i > 0) s.concat(',');
            int len = formatLineJsonBuf(batch[i], cfg, lineBuf, sizeof(lineBuf));
            s.concat(lineBuf, len);
            if (i % 10 == 9) { watchdog_update(); yield(); }
        }
        s.concat(']');
    } else if (cfg.telMode == TEL_MODE_CSV) {
        s = "timestamp;ambT;ambH";
        char hdrBuf[32];
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (cfg.sensors[i].active) {
                snprintf(hdrBuf, sizeof(hdrBuf), ";s%d_%s", i, cfg.sensors[i].hwId);
                s.concat(hdrBuf);
            }
        }
        s.concat('\n');
        char csvBuf[256];
        for (size_t i = 0; i < batch.size(); i++) {
            batch[i].toCsvLine(csvBuf, sizeof(csvBuf));
            s.concat(csvBuf);
            s.concat('\n');
            if (i % 10 == 9) { watchdog_update(); yield(); }
        }
    } else if (cfg.telMode == 2) {
        /* Custom: usa formatLineCustom existente (menos comum) */
        String separator = String(cfg.telLineSeparator);
        if (separator == "\\n") separator = "\n";
        String dataBlocks;
        dataBlocks.reserve(batch.size() * perLine);
        for (size_t i = 0; i < batch.size(); i++) {
            if (i > 0) dataBlocks += separator;
            dataBlocks += formatLineCustom(batch[i], cfg);
            if (i % 10 == 9) { watchdog_update(); yield(); }
        }
        String devName = String(cfg.deviceName);
        String macAddr = _netRef->getMacAddress();
        s = String(cfg.telGlobalTemplate);
        s.replace("{DEV}", devName);
        s.replace("{MAC}", macAddr);
        s.replace("{DATA}", dataBlocks);
    }
    return s;
}

/**
 * @brief Formata um registro como JSON diretamente em buffer de char — zero heap allocation.
 * @return Número de bytes escritos em dest (excluindo \0).
 */
int TelemetryManager::formatLineJsonBuf(const BinaryHistoryRecord& rec, const SystemConfig& cfg,
                                         char* dest, size_t maxLen) {
    dest[0] = '\0';
    int pos = snprintf(dest, maxLen, "{\"ts\":%lu", (unsigned long)rec.epoch);

    char tmp[32];
    if (rec.ambientTemp != HIST_NAN_SENTINEL) {
        snprintf(tmp, sizeof(tmp), ",\"tAMB\":%.2f", BinaryHistoryRecord::i16ToFloat(rec.ambientTemp));
        pos += strlcat(dest + pos, tmp, maxLen - pos);
    }
    if (rec.ambientHum != HIST_NAN_SENTINEL) {
        snprintf(tmp, sizeof(tmp), ",\"uAMB\":%.1f", BinaryHistoryRecord::i16ToFloat(rec.ambientHum));
        pos += strlcat(dest + pos, tmp, maxLen - pos);
    }
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].active && rec.sensors[i] != HIST_NAN_SENTINEL) {
            /* Usa hwId diretamente do char[] da config — sem String temporário */
            const char* hwid = cfg.sensors[i].hwId;
            if (hwid[0] == '\0') {
                snprintf(tmp, sizeof(tmp), ",\"t%d\":%.2f", i, BinaryHistoryRecord::i16ToFloat(rec.sensors[i]));
            } else {
                snprintf(tmp, sizeof(tmp), ",\"t%s\":%.2f", hwid, BinaryHistoryRecord::i16ToFloat(rec.sensors[i]));
            }
            pos += strlcat(dest + pos, tmp, maxLen - pos);
        }
    }
    if ((size_t)pos < maxLen - 1) { dest[pos] = '}'; dest[pos+1] = '\0'; pos++; }
    return pos;
}

/** @brief Wrapper que retorna String — usado pelo MQTT individual publish. */
String TelemetryManager::formatLineJson(const BinaryHistoryRecord& rec, const SystemConfig& cfg) {
    char buf[512];
    formatLineJsonBuf(rec, cfg, buf, sizeof(buf));
    return String(buf);
}

String TelemetryManager::formatLineCustom(const BinaryHistoryRecord& rec, const SystemConfig& cfg) {
    char tsBuf[16];
    snprintf(tsBuf, sizeof(tsBuf), "%lu", (unsigned long)rec.epoch);

    String ambT = (rec.ambientTemp != HIST_NAN_SENTINEL)
                  ? String(BinaryHistoryRecord::i16ToFloat(rec.ambientTemp), 2) : "";
    String ambH = (rec.ambientHum != HIST_NAN_SENTINEL)
                  ? String(BinaryHistoryRecord::i16ToFloat(rec.ambientHum), 1) : "";

    String slotVals[MAX_SENSORS];
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (rec.sensors[i] != HIST_NAN_SENTINEL) {
            slotVals[i] = String(BinaryHistoryRecord::i16ToFloat(rec.sensors[i]), 2);
        }
    }

    String boardSerial = _storageRef->getBoardSerialNumber();
    String out = String(cfg.telLineTemplate);
    out.replace("{TS}", String(tsBuf));
    out.replace("{DHT_ID}", boardSerial);

    if (ambT.length() > 0) {
        out.replace("\"tAMB_ID\":{tAMB}", "\"t" + boardSerial + "\":" + ambT);
        out.replace("\"tAMB\":{tAMB}", "\"tAMB\":" + ambT);
        out.replace("{tAMB}", ambT);
    } else {
        out.replace("\"tAMB_ID\":{tAMB}", "");
        out.replace("\"tAMB\":{tAMB}", "");
        out.replace("{tAMB}", "null");
    }

    if (ambH.length() > 0) {
        out.replace("\"uAMB_ID\":{uAMB}", "\"u" + boardSerial + "\":" + ambH);
        out.replace("\"uAMB\":{uAMB}", "\"uAMB\":" + ambH);
        out.replace("{uAMB}", ambH);
    } else {
        out.replace("\"uAMB_ID\":{uAMB}", "");
        out.replace("\"uAMB\":{uAMB}", "");
        out.replace("{uAMB}", "null");
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        String val = slotVals[i]; val.trim();
        String tagVal = "{t" + String(i) + "}";
        String tagKeyFull = "\"t" + String(i) + "\":" + tagVal;
        if (cfg.sensors[i].active && val.length() > 0) {
            String hwid = String(cfg.sensors[i].hwId); hwid.trim();
            out.replace(tagKeyFull, "\"t" + hwid + "\":" + val);
            out.replace(tagVal, val);
        } else {
            out.replace(tagKeyFull, "");
            out.replace(tagVal, "null");
        }
    }

    while (out.indexOf(",,") != -1) { out.replace(",,", ","); }
    out.replace("{,", "{"); out.replace(",}", "}");
    out.replace("[,", "["); out.replace(",]", "]");

    return out;
}


/**
 * @brief Count pending telemetry records/**
 * @brief Count pending telemetry records by scanning history CSVs.
 * Called periodically (~10s) by AppManager for dashboard display.
 */
void TelemetryManager::refreshPendingCount() {
    if (!_pendingDirty) return;

    SystemConfig& cfg = _storageRef->getConfig();
    uint32_t lastCursor = _storageRef->getLastSentTimestamp();


    std::vector<String> files;
    {
        _storageRef->enterFlashReadLock();
        Dir dir = LittleFS.openDir(DIR_HISTORY);
        while (dir.next()) {
            if (dir.fileName().endsWith(HISTORY_FILE_EXT)) {
                files.push_back(dir.fileName());
            }
        }
        _storageRef->exitFlashReadLock();
    }


    String minFileName = "";
    if (lastCursor > 1000000000) {
        time_t cursorEpoch = (time_t)lastCursor;
        struct tm timeinfo;
        localtime_r(&cursorEpoch, &timeinfo);
        char buff[24];
        snprintf(buff, sizeof(buff), "%04d%02d%02d" HISTORY_FILE_EXT,
                 timeinfo.tm_year + 1900,
                 timeinfo.tm_mon + 1,
                 timeinfo.tm_mday);
        minFileName = String(buff);
    }

    uint16_t total = 0;


    for (const String& fn : files) {
        if (minFileName.length() > 0 && fn < minFileName) continue;

        String fullPath = String(DIR_HISTORY) + "/" + fn;

        _storageRef->enterFlashReadLock();
        File f = LittleFS.open(fullPath, "r");
        if (!f) { _storageRef->exitFlashReadLock(); continue; }

        /* Lê apenas o epoch (4 bytes) de cada registro e salta os 24 restantes */
        while (f.available() >= HISTORY_RECORD_SIZE) {
            uint32_t epoch;
            if (f.read((uint8_t*)&epoch, sizeof(epoch)) == sizeof(epoch)) {
                if (epoch > lastCursor) total++;
                f.seek(f.position() + HISTORY_RECORD_SIZE - sizeof(epoch));
            }
        }
        f.close();
        _storageRef->exitFlashReadLock();

        watchdog_update();
        TRACE_BEAT(0);
    }

    _pendingEstimate = total;
    _pendingDirty = false;
}


uint16_t TelemetryManager::getPendingEstimate() const {
    return _pendingEstimate;
}

void TelemetryManager::notifyNewRecord() {
    __atomic_fetch_add(&_pendingEstimate, 1, __ATOMIC_RELAXED);
}


/**
 * @brief Consome o resultado do último envio de telemetria.
 *
 * Retorna true se houve um envio desde a última chamada, preenchendo
 * outSuccess com o resultado. O flag é limpo após o consumo, garantindo
 * que cada resultado seja processado uma única vez.
 */
bool TelemetryManager::consumeLastSendResult(bool& outSuccess) {
    if (_hasSendResult) {
        outSuccess     = _lastSendSuccess;
        _hasSendResult = false;
        return true;
    }
    return false;
}
