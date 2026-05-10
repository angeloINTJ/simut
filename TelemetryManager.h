/**
 * @file    TelemetryManager.h
 * @brief   Telemetry uploader supporting HTTP POST and MQTT publish with exponential backoff.
 * @details Manages periodic and manual data uploads via configurable transport
 * (HTTP or MQTT). Supports JSON, CSV, and custom payload templates
 * with TLS/SSL, batch collection from history CSVs, and a pending
 * count estimator for dashboard display.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
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

    /** Arma dump one-shot do próximo payload construído (via USB+BT).
     *  Flag é consumida na primeira chamada de buildPayload após este. */
    void armPayloadDump() { _dumpPayloadNext = true; }

    /** True se o dump está armado mas ainda não foi consumido (ex: forceSync
     *  rodou sem dados pendentes, o dump vai esperar o próximo buildPayload). */
    bool isPayloadDumpArmed() const { return _dumpPayloadNext; }


    bool isMqttConnected();

    uint16_t getPendingEstimate() const;
    void     refreshPendingCount();
    void     notifyNewRecord();


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
    volatile bool _isSending = false;


    String _cachedCert;
    bool   _hasCert;

    volatile uint16_t _pendingEstimate = 0;
    volatile bool     _pendingDirty    = true;

    volatile bool _hasSendResult    = false;
    volatile bool _lastSendSuccess  = false;

    volatile bool _dumpPayloadNext  = false;  /**< One-shot flag set por `tel dump` (CLI/BT). */

    /** Dump do payload: header + corpo (quebrado a cada ',') + footer.
     *  Quebra por vírgula melhora legibilidade e evita drop no BT (cada linha
     *  cabe na janela TX). Segmento >= sizeof(buf) é truncado defensivamente. */
    void _dumpPayload(const char* payload, size_t len, const char* label);


    static const uint32_t BACKOFF_MIN_MS     = 5000;
    static const uint32_t BACKOFF_MAX_MS     = 300000;
    static const uint8_t  BACKOFF_MAX_STREAK = 10;

    uint32_t _currentBackoff;
    uint32_t _backoffUntil;
    uint8_t  _consecutiveFails;
    uint32_t _lastSuppressedLog = 0;    /**< millis() do último heartbeat suprimido */

    /* v4.3.0 F-TEL-ADAPTIVE: intervalo dinâmico runtime (não persistente).
     * Lógica inlined em update() pra economizar flash. */
    uint32_t _smoothedLatencyMs = 0;     /**< EMA da latência observada (alpha 0.3) */
    uint32_t _effectiveIntervalMs = 0;   /**< Intervalo efetivo computado (vs cfg.telInterval) */

    void     resetBackoff();
    void     escalateBackoff();
    uint32_t jitter(uint32_t base);


    bool collectBatch(std::vector<BinaryHistoryRecord>& batch, uint32_t& newCursor);


    bool attemptHttpUpload(String& payload, uint32_t newCursor);

    /* HTTP TLS — cliente reutilizável (evita realocar ~16KB a cada upload) */
    WiFiClientSecure* _httpSecurePtr  = nullptr;
    uint32_t          _httpSecureLastUse = 0;

    WiFiClient       _mqttWifiClient;
    WiFiClientSecure* _mqttSecurePtr = nullptr; /**< Alocado sob demanda (só MQTT+TLS) ~16KB */
    PubSubClient     _mqttClient;
    bool             _mqttInitialized;
    uint32_t         _lastMqttReconnect;

    bool mqttEnsureConnected();
    bool attemptMqttPublish(String& payload, std::vector<BinaryHistoryRecord>& batch, uint32_t newCursor);
    String buildMqttClientId();
    uint8_t safeBatchLimit(uint8_t configured);

    /** @brief No-op intencional — liberar TLS clients causaria fragmentação de heap. */
    void releaseIdleResources();


    String buildPayload(std::vector<BinaryHistoryRecord>& batch);
    int    formatLineJsonBuf(const BinaryHistoryRecord& rec, const SystemConfig& cfg, char* dest, size_t maxLen);
    String formatLineJson(const BinaryHistoryRecord& rec, const SystemConfig& cfg);
    int    formatLineCustomBuf(const BinaryHistoryRecord& rec, const SystemConfig& cfg, char* dest, size_t cap);
    String formatLineCustom(const BinaryHistoryRecord& rec, const SystemConfig& cfg);
};
