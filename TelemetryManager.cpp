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
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "TelemetryManager.h"
#include "MetricsManager.h"
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


    /* CAS atômico: impede race entre update() periódico e forceSync() CLI */
    bool expected = false;
    if (!__atomic_compare_exchange_n(&_isSending, &expected, true,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return;
    if (!_netRef->isNetworkHealthy()) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return; }
    if (!_storageRef->lockHeavyTask()) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return; }

    /*
     * RAII: estende WDT ctx para 120s durante o ciclo. TelemetryGuard só
     * cobre http.POST (handshake/cleanup ficavam expostos). Context-aware:
     * saves/logs aninhados não reduzem a janela pra 8.3s durante telemetria.
     * Auto-restore em qualquer exit path (normal ou early return).
     */
    LogManager::WdtWindow _wdt(120000);

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
        if (_dumpPayloadNext) {
            _dumpPayload(payload.c_str(), payload.length(), "MQTT");
            _dumpPayloadNext = false;
        }
        success = attemptMqttPublish(payload, batch, newCursor);
        /* batch e payload saem de escopo aqui e liberam memória */
    } else {
        /*
         * HTTP: constrói payload, libera batch ANTES do POST.
         * Isso evita que batch (~7KB) + payload (~13KB) + TLS (~16KB)
         * coexistam em RAM simultaneamente.
         */
        String payload = buildPayload(batch);
        if (_dumpPayloadNext) {
            _dumpPayload(payload.c_str(), payload.length(), "HTTP");
            _dumpPayloadNext = false;
        }

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
    /* WdtWindow destrutor auto-restaura WDT aqui */
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
 * F-MEM-NOCACHE (alpha14): heap permanece estável em ~50 KB sem
 * caches de gráfico ocupando espaço. Limites afrouxados para permitir
 * batches significativamente maiores quando configurado pelo user.
 *
 * Camadas de safety preservadas:
 *  • update() preflight: aborta se heap < 20 KB
 *  • buildPayload: faz resize dinâmico se estimativa exceder o disponível
 *  • TelemetryGuard: alimenta WDT durante POST até 60s (cobre POSTs grandes)
 *
 * @param configured  Limite máximo configurado pelo usuário.
 * @return            Limite efetivo (≥1, ≤configured).
 */
uint8_t TelemetryManager::safeBatchLimit(uint8_t configured) {
    SystemConfig& cfg = _storageRef->getConfig();
    uint32_t freeHeap = rp2040.getFreeHeap();
    /*
     * HEAP_RESERVE diferenciado por encryption (alpha17):
     *  • HTTPS/MQTTS (cfg.telEncryption=true): 24 KB cobre TLS reconnect
     *    scratch (~10K BearSSL) + HTTPClient (~3K) + transients (~3K) +
     *    margem operacional (~8K).
     *  • HTTP/MQTT plain (cfg.telEncryption=false): 12 KB — sem TLS,
     *    só HTTPClient + lwIP + margem. Permite batches ~35% maiores.
     *
     * BYTES_PER_ENTRY = 350: medido empírico — ~28 batch struct +
     * ~310 JSON payload (conservador vs real ~221 para sensores hwId longo).
     *
     * HARD_CAP = 100: payload 100 entries ~= 35 KB, cabe + TLS scratch.
     */
    const uint32_t HEAP_RESERVE    = cfg.telEncryption ? 24576 : 12288;
    const uint32_t BYTES_PER_ENTRY = 350;
    const uint8_t  HARD_CAP        = 100;

    if (freeHeap <= HEAP_RESERVE) return 1;

    uint32_t heapLimit32 = (freeHeap - HEAP_RESERVE) / BYTES_PER_ENTRY;
    uint8_t heapLimit = (heapLimit32 > 255) ? 255 : (uint8_t)heapLimit32;
    return max((uint8_t)1, min(min(configured, HARD_CAP), heapLimit));
}

bool TelemetryManager::collectBatch(std::vector<BinaryHistoryRecord>& batch, uint32_t& newCursor) {
    SystemConfig &cfg = _storageRef->getConfig();
    uint32_t lastCursor = _storageRef->getLastSentTimestamp();

    /* F-NET-TIME.5: detecção cursor-no-futuro. Se lastCursor > now + 1 dia,
     * é artefato de set manual de hora futura + volta ao NTP. Sem reset,
     * collectBatch rejeita todos os records novos (rec.epoch > lastCursor
     * sempre falso) e telemetria fica muda sem pista nos logs. Reseta o
     * cursor → cai no fallback de 30d abaixo. Threshold de 1 dia tolera
     * drift pequeno (timezone). */
    uint32_t nowEpoch = (uint32_t)time(nullptr);
    if (nowEpoch > 1600000000UL && lastCursor > nowEpoch + 86400UL) {
        LOG_CODE(LOG_WARN, "TEL", SYS_OK, 0,
                 TRL("Telemetry cursor in future — reset to 0"));
        _storageRef->setLastSentTimestamp(0);
        lastCursor = 0;
    }

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

                /* Defensive: ignora records com epoch absurdo (lixo de boot
                 * pre-NTP ou corrupção do codec v2). Evita propagar para o
                 * cursor — bug do `19691231.bin` (v3.27.x). */
                const uint32_t EPOCH_MIN = 1700000000UL;  /* 2023-11-14 */
                bool epochValid = (rec.epoch >= EPOCH_MIN) &&
                                  (nowEpoch < EPOCH_MIN || rec.epoch <= nowEpoch + 86400UL);
                if (epochValid && rec.epoch > lastCursor) {
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
                feedWdt();
                yield();
                _storageRef->enterFlashReadLock();
            }
        }
        f.close();
        _storageRef->exitFlashReadLock();


        feedWdt();
    }

    return !batch.empty();
}


/* =========================================================================== */
/*                              HTTP TRANSPORT                               */
/* =========================================================================== */
/** @brief Upload a batch via HTTP POST with configurable auth headers. */
bool TelemetryManager::attemptHttpUpload(String& payload, uint32_t newCursor) {
    SystemConfig &cfg = _storageRef->getConfig();

    feedWdt();

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
                LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, 0, TRL("OOM: WiFiClientSecure"));
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
        feedWdt();

        uint32_t postStart = millis();
        int code;
        {
            TelemetryGuard tg;  /* Alimenta watchdog durante POST bloqueante */
            code = http.POST(payload);
        }
        uint32_t postLatency = millis() - postStart;
        watchdog_update();

        if (code > 0) {
            LOG_CODE(LOG_INFO, "TEL", SYS_TEL_SENT, code, "HTTP OK: " + String(payload.length()) + " bytes, code " + String(code));
            if (code >= 200 && code < 300) {
                _storageRef->setLastSentTimestamp(newCursor);
                success = true;
                auto& m = MetricsManager::instance().data();
                m.telSent++;
                m.telTotalBytes += (uint32_t)payload.length();
                m.telLastLatencyMs = postLatency;
            }
        } else {
            LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, code, String(TRL("HTTP error: ")) + http.errorToString(code));
            MetricsManager::instance().data().telFailed++;
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
    feedWdt();

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
        LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_CONN, 0, String(TRL("MQTT connected to ")) + cfg.telServer);
        MetricsManager::instance().data().mqttReconnects++;


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
        LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_MQTT_DISC, state, String(TRL("MQTT failed: ")) + reason);
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
            feedWdt();

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
        feedWdt();

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
    _lastCheckTime = millis();  /* intervalo medido do fim do ciclo, não do início */
}

void TelemetryManager::escalateBackoff() {
    _consecutiveFails++;
    MetricsManager::instance().data().telRetries++;
    _backoffUntil = millis() + jitter(_currentBackoff);
    _lastCheckTime = millis();  /* evita re-disparo imediato quando backoff expira */

    if (_consecutiveFails <= BACKOFF_MAX_STREAK) {
        LOG_CODE(LOG_WARN, "TEL", SYS_TEL_RETRY, _consecutiveFails,
            String(TRL("Upload failed (#")) + _consecutiveFails +
            TRL("). Retry in ") + (_currentBackoff / 1000) + "s");
    } else if (_consecutiveFails == BACKOFF_MAX_STREAK + 1) {
        LOG_CODE(LOG_WARN, "TEL", TEL_BACKOFF_SUPPRESSED, 0, "");
        _lastSuppressedLog = millis();
    } else if (timeSince(_lastSuppressedLog, 3600000)) {
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
    resetBackoff();

    bool expected = false;
    if (!__atomic_compare_exchange_n(&_isSending, &expected, true,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return false;
    if (!_netRef->isNetworkHealthy()) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return false; }
    if (!_storageRef->lockHeavyTask()) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return false; }

    /* RAII: estende ctx WDT 120s, context-aware igual update(). */
    LogManager::WdtWindow _wdt(120000);

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
    if (_dumpPayloadNext) {
        _dumpPayload(payload.c_str(), payload.length(), "SYNC");
        _dumpPayloadNext = false;
    }

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
    /* WdtWindow destrutor auto-restaura WDT */
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

    /* Verifica heap e reduz batch se necessário.
     * alpha17: reserve diferenciado por TLS — 12K com encryption,
     * 6K sem (sem scratch BearSSL). shrink_to_fit() força liberação
     * real da capacity do vector (resize só muda size, não capacity). */
    uint32_t freeHeap = rp2040.getFreeHeap();
    const uint32_t SEC_RESERVE = cfg.telEncryption ? 12288 : 6144;
    if (freeHeap < estimatedSize + SEC_RESERVE) {
        size_t safeCount = (freeHeap > SEC_RESERVE) ? (freeHeap - SEC_RESERVE) / perLine : 1;
        if (safeCount < batch.size()) {
            batch.resize(safeCount);
            batch.shrink_to_fit();   /* libera capacity efetiva */
        }
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
        /*
         * Custom: walk global template char-by-char, emit per-line content
         * into `s` directly on {DATA}. Zero intermediate String.
         */
        char sep[16];
        strlcpy(sep, cfg.telLineSeparator, sizeof(sep));
        if (sep[0] == '\\' && sep[1] == 'n' && sep[2] == '\0') { sep[0] = '\n'; sep[1] = '\0'; }
        size_t sepLen = strlen(sep);

        String macStr = _netRef->getMacAddress();
        const char* gt = cfg.telGlobalTemplate;
        const size_t gtLen = strnlen(gt, sizeof(cfg.telGlobalTemplate));

        char lineBuf[1024];
        size_t gi = 0;
        size_t spanStart = 0;

        while (gi < gtLen) {
            if (gt[gi] != '{') { gi++; continue; }

            /* Match known token prefixes at '{'. Advance 1 char on miss
             * so nested braces in JSON templates are handled correctly. */
            const size_t remaining = gtLen - gi;
            size_t tokLen = 0;
            int tokKind = 0;  /* 1=DEV, 2=MAC, 3=DATA */
            if (remaining >= 5 && memcmp(gt + gi, "{DEV}", 5) == 0) { tokKind = 1; tokLen = 5; }
            else if (remaining >= 5 && memcmp(gt + gi, "{MAC}", 5) == 0) { tokKind = 2; tokLen = 5; }
            else if (remaining >= 6 && memcmp(gt + gi, "{DATA}", 6) == 0) { tokKind = 3; tokLen = 6; }

            if (tokKind == 0) { gi++; continue; }

            /* Flush literal span before token */
            if (gi > spanStart) s.concat(gt + spanStart, gi - spanStart);

            if (tokKind == 1) {
                s.concat(cfg.deviceName);
            } else if (tokKind == 2) {
                s.concat(macStr);
            } else {  /* DATA */
                for (size_t i = 0; i < batch.size(); i++) {
                    if (i > 0 && sepLen > 0) s.concat(sep, sepLen);
                    int len = formatLineCustomBuf(batch[i], cfg, lineBuf, sizeof(lineBuf));
                    if (len > 0) s.concat(lineBuf, len);
                    if (i % 10 == 9) { watchdog_update(); yield(); }
                }
            }

            gi += tokLen;
            spanStart = gi;
        }
        if (gtLen > spanStart) s.concat(gt + spanStart, gtLen - spanStart);
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

/**
 * Walk template once, emit into dest. Zero String allocations.
 * Tokens: {TS} {DHT_ID} {tAMB} {uAMB} {t0}..{t9}
 * Compound forms "<key>_ID":{<tok>} and "<key>":{<tok>} trigger key rewrite/removal.
 */
int TelemetryManager::formatLineCustomBuf(const BinaryHistoryRecord& rec,
                                          const SystemConfig& cfg,
                                          char* dest, size_t cap) {
    if (cap == 0) return 0;
    dest[0] = '\0';

    char tsBuf[16];
    snprintf(tsBuf, sizeof(tsBuf), "%lu", (unsigned long)rec.epoch);

    char boardSerial[20] = {0};
    {
        String bs = _storageRef->getBoardSerialNumber();
        strlcpy(boardSerial, bs.c_str(), sizeof(boardSerial));
    }

    char ambTBuf[16] = {0};
    char ambHBuf[16] = {0};
    const bool hasAmbT = (rec.ambientTemp != HIST_NAN_SENTINEL);
    const bool hasAmbH = (rec.ambientHum != HIST_NAN_SENTINEL);
    if (hasAmbT) snprintf(ambTBuf, sizeof(ambTBuf), "%.2f", BinaryHistoryRecord::i16ToFloat(rec.ambientTemp));
    if (hasAmbH) snprintf(ambHBuf, sizeof(ambHBuf), "%.1f", BinaryHistoryRecord::i16ToFloat(rec.ambientHum));

    char slotVal[MAX_SENSORS][16];
    bool slotHas[MAX_SENSORS];
    for (int i = 0; i < MAX_SENSORS; i++) {
        slotHas[i] = (cfg.sensors[i].active && rec.sensors[i] != HIST_NAN_SENTINEL);
        if (slotHas[i]) snprintf(slotVal[i], sizeof(slotVal[i]), "%.2f", BinaryHistoryRecord::i16ToFloat(rec.sensors[i]));
        else slotVal[i][0] = '\0';
    }

    const char* tpl = cfg.telLineTemplate;
    const size_t tplLen = strnlen(tpl, sizeof(cfg.telLineTemplate));
    size_t di = 0;   /* dest cursor */
    size_t ti = 0;   /* template cursor */

    while (ti < tplLen && di + 1 < cap) {
        char c = tpl[ti];
        if (c != '{') { dest[di++] = c; ti++; continue; }

        /*
         * At '{': try to match a known token prefix.
         * Do NOT scan for a generic '}' — JSON templates like {"ts":{TS}}
         * have nested braces, so the outer '{' must be emitted literally
         * and the scan must continue one char forward.
         */
        const size_t remaining = tplLen - ti;
        const char* val = nullptr;   /* resolved value (NULL = absent) */
        const char* hwid = nullptr;  /* hwid for compound key rewrite */
        char compKey[8] = {0};
        size_t compKeyLen = 0;
        size_t tokenChars = 0;       /* total chars to advance in template */
        bool tokenValid = false;

        if (remaining >= 4 && memcmp(tpl + ti, "{TS}", 4) == 0) {
            val = tsBuf; tokenChars = 4; tokenValid = true;
        } else if (remaining >= 8 && memcmp(tpl + ti, "{DHT_ID}", 8) == 0) {
            val = boardSerial; tokenChars = 8; tokenValid = true;
        } else if (remaining >= 6 && memcmp(tpl + ti, "{tAMB}", 6) == 0) {
            val = hasAmbT ? ambTBuf : nullptr;
            hwid = boardSerial;
            memcpy(compKey, "tAMB", 4); compKeyLen = 4;
            tokenChars = 6; tokenValid = true;
        } else if (remaining >= 6 && memcmp(tpl + ti, "{uAMB}", 6) == 0) {
            val = hasAmbH ? ambHBuf : nullptr;
            hwid = boardSerial;
            memcpy(compKey, "uAMB", 4); compKeyLen = 4;
            tokenChars = 6; tokenValid = true;
        } else if (remaining >= 4 && tpl[ti+1] == 't' &&
                   tpl[ti+2] >= '0' && tpl[ti+2] <= '9' && tpl[ti+3] == '}') {
            /* {t0}..{t9} — MAX_SENSORS=10 means single digit */
            int idx = tpl[ti+2] - '0';
            if (idx < MAX_SENSORS) {
                val = slotHas[idx] ? slotVal[idx] : nullptr;
                hwid = cfg.sensors[idx].hwId;
                compKeyLen = snprintf(compKey, sizeof(compKey), "t%d", idx);
                tokenChars = 4; tokenValid = true;
            }
        }

        if (!tokenValid) {
            /* '{' not followed by a known token — emit literally, advance 1 */
            dest[di++] = c;
            ti++;
            continue;
        }

        /* Check compound context by looking back in template:
         *   "<compKey>_ID":{<tok>}  → pattern1
         *   "<compKey>":{<tok>}     → pattern2
         */
        bool matchedFull = false, matchedBare = false;
        if (compKeyLen > 0) {
            const size_t p1 = compKeyLen + 6;  /* "<k>_ID": */
            if (ti >= p1) {
                const char* p = tpl + ti - p1;
                if (p[0] == '"' &&
                    memcmp(p + 1, compKey, compKeyLen) == 0 &&
                    memcmp(p + 1 + compKeyLen, "_ID\":", 5) == 0) {
                    matchedFull = true;
                }
            }
            if (!matchedFull) {
                const size_t p2 = compKeyLen + 3;  /* "<k>": */
                if (ti >= p2) {
                    const char* p = tpl + ti - p2;
                    if (p[0] == '"' &&
                        memcmp(p + 1, compKey, compKeyLen) == 0 &&
                        memcmp(p + 1 + compKeyLen, "\":", 2) == 0) {
                        matchedBare = true;
                    }
                }
            }
        }

        if (matchedFull) {
            /* Undo "<compKey>_ID": already emitted */
            const size_t undo = compKeyLen + 6;
            if (di >= undo) di -= undo;
            if (val) {
                /* Emit "t<hwid>":<val>, trimming hwid whitespace */
                char hwidTrim[20] = {0};
                const char* h = hwid ? hwid : "";
                while (*h == ' ' || *h == '\t') h++;
                size_t hlen = strnlen(h, sizeof(hwidTrim) - 1);
                while (hlen > 0 && (h[hlen-1] == ' ' || h[hlen-1] == '\t')) hlen--;
                memcpy(hwidTrim, h, hlen); hwidTrim[hlen] = '\0';
                const char* prefix = (compKey[0] == 'u') ? "\"u" : "\"t";
                int w = snprintf(dest + di, cap - di, "%s%s\":%s", prefix, hwidTrim, val);
                if (w > 0) { di += ((size_t)w < cap - di) ? (size_t)w : (cap - di - 1); }
            }
            /* else: nothing emitted (span removed) */
        } else if (matchedBare) {
            if (val) {
                /* Key already in dest; append value */
                size_t vl = strlen(val);
                if (di + vl >= cap) vl = cap - 1 - di;
                memcpy(dest + di, val, vl);
                di += vl;
            } else {
                /* Undo key emission */
                const size_t undo = compKeyLen + 3;
                if (di >= undo) di -= undo;
            }
        } else {
            /* Bare {tok}: emit value or "null" */
            const char* emit = val ? val : "null";
            size_t el = strlen(emit);
            if (di + el >= cap) el = cap - 1 - di;
            memcpy(dest + di, emit, el);
            di += el;
        }

        ti += tokenChars;
    }

    /* In-place cleanup: collapse ",," runs; drop "{," "[," ","}" ","]" */
    size_t r = 0, w = 0;
    while (r < di) {
        char c = dest[r++];
        if (c == ',') {
            if (w == 0) continue;
            char prev = dest[w-1];
            if (prev == ',' || prev == '{' || prev == '[') continue;
        } else if ((c == '}' || c == ']') && w > 0 && dest[w-1] == ',') {
            w--;
        }
        dest[w++] = c;
    }
    dest[w] = '\0';
    return (int)w;
}

/** Thin wrapper: preserves String-returning API for MQTT per-item publish. */
String TelemetryManager::formatLineCustom(const BinaryHistoryRecord& rec, const SystemConfig& cfg) {
    char buf[1024];
    formatLineCustomBuf(rec, cfg, buf, sizeof(buf));
    return String(buf);
}

void TelemetryManager::_dumpPayload(const char* payload, size_t len, const char* label) {
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "=== PAYLOAD %s (%u B) ===", label, (unsigned)len);
    LogManager::instance().writeConsole(hdr);

    char buf[256];
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (payload[i] == ',') {
            size_t n = i - start + 1;  /* inclui a vírgula */
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, payload + start, n);
            buf[n] = '\0';
            LogManager::instance().writeConsole(buf);
            start = i + 1;
        }
    }
    if (start < len) {
        size_t n = len - start;
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, payload + start, n);
        buf[n] = '\0';
        LogManager::instance().writeConsole(buf);
    }

    LogManager::instance().writeConsole("=== END ===");
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

        feedWdt();
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
