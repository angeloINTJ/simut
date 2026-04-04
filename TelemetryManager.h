/**
 * @file    TelemetryManager.h
 * @brief   Telemetry uploader supporting HTTP POST and MQTT publish with exponential backoff.
 * @details Manages periodic and manual data uploads via configurable transport
 *          (HTTP or MQTT). Supports JSON, CSV, and custom payload templates
 *          with TLS/SSL, batch collection from history CSVs, and a pending
 *          count estimator for dashboard display.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "SystemDefs.h"
#include "StorageManager.h"
#include "NetworkManager.h"
#include "LogManager.h"

class TelemetryManager {
public:
    TelemetryManager();
    void begin(StorageManager* storage, NetworkManager* network);
    void update();
    bool forceSync();


    bool isMqttConnected();

    uint16_t getPendingEstimate() const;
    void     refreshPendingCount();

    /**
     * @brief Consome o resultado do último envio (se houver).
     * @param outSuccess  Recebe true se sucesso, false se falha.
     * @return true se havia resultado pendente (outSuccess válido), false se nada aconteceu.
     */
    bool consumeLastSendResult(bool& outSuccess);

private:
    StorageManager* _storageRef;
    NetworkManager* _netRef;

    uint32_t _lastCheckTime;
    bool _isSending;


    String _cachedCert;
    bool   _hasCert;

    volatile uint16_t _pendingEstimate = 0;

    volatile bool _hasSendResult    = false;
    volatile bool _lastSendSuccess  = false;


    static const uint32_t BACKOFF_MIN_MS     = 5000;
    static const uint32_t BACKOFF_MAX_MS     = 300000;
    static const uint8_t  BACKOFF_MAX_STREAK = 10;

    uint32_t _currentBackoff;
    uint32_t _backoffUntil;
    uint8_t  _consecutiveFails;

    void     resetBackoff();
    void     escalateBackoff();
    uint32_t jitter(uint32_t base);


    bool collectBatch(std::vector<String>& batch, uint32_t& newCursor);


    bool attemptHttpUpload(std::vector<String>& batch, uint32_t newCursor);


    WiFiClient      _mqttWifiClient;
    WiFiClientSecure _mqttWifiClientSecure;
    PubSubClient    _mqttClient;
    bool            _mqttInitialized;
    uint32_t        _lastMqttReconnect;

    bool mqttEnsureConnected();
    bool attemptMqttPublish(std::vector<String>& batch, uint32_t newCursor);
    String buildMqttClientId();


    String buildPayload(std::vector<String>& csvLines);
    String formatLineJson(String& csvLine, const SystemConfig& cfg);
    String formatLineCustom(String& csvLine, const SystemConfig& cfg);
};
