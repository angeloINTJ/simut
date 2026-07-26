/**
 * @file TelemetryManager.h
 * @brief Telemetry uploader supporting HTTP POST and MQTT publish with exponential backoff.
 * @details Manages periodic and manual data uploads via configurable transport
 * (HTTP or MQTT). Supports JSON, CSV, and custom payload templates
 * with TLS/SSL, batch collection from history, and a pending
 * count estimator for dashboard display.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
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
 TelemetryManager( );
 void begin(StorageManager* storage, NetworkManager* network);
 void update( );
 bool forceSync( );

 /** Arms one-shot dump of the next built payload (via USB+BT).
 * Flag is consumed on the first buildPayload call after this. */
 void armPayloadDump( ) { _dumpPayloadNext = true; }

 /** True if dump is armed but not yet consumed (e.g. forceSync
 * ran with no pending data, dump will wait for next buildPayload). */
 bool isPayloadDumpArmed( ) const { return _dumpPayloadNext; }


 bool isMqttConnected( );

 uint16_t getPendingEstimate( ) const;
 void refreshPendingCount( );
 void notifyNewRecord( );


 /**
 * @brief Consumes the last send result (if any).
 * @param outSuccess Receives true on success, false on failure.
 * @return true if there was a pending result (outSuccess valid), false if nothing happened.
 */
 bool consumeLastSendResult(bool& outSuccess);

private:
 StorageManager* _storageRef;
 NetworkManager* _netRef;

 uint32_t _lastCheckTime;
 volatile bool _isSending = false;


 String _cachedCert;
 bool _hasCert;

 volatile uint16_t _pendingEstimate = 0;
 volatile bool _pendingDirty = true;

 volatile bool _hasSendResult = false;
 volatile bool _lastSendSuccess = false;

 volatile bool _dumpPayloadNext = false; /**< One-shot flag set by 'tel dump' (CLI/BT). */

 /** Payload dump: header + body (split at each ',') + footer.
 * Splitting by comma improves readability and avoids BT drops (each line
 * fits in TX window). Segment >= sizeof(buf) is defensively truncated. */
 void _dumpPayload(const char* payload, size_t len, const char* label);


 static const uint32_t BACKOFF_MIN_MS = 5000;
 /* constexpr for the same reason as NetworkManager::MAX_RECONNECT_DELAY:
  * min( ) binds a reference, which ODR-uses the member and only shows up
  * as a link error in the unoptimised debug build. */
 static constexpr uint32_t BACKOFF_MAX_MS = 300000;
 static const uint8_t BACKOFF_MAX_STREAK = 10;

 uint32_t _currentBackoff;
 uint32_t _backoffUntil;
 uint8_t _consecutiveFails;
 uint32_t _lastSuppressedLog = 0; /**< millis() of last suppressed heartbeat */

 /* Dynamic runtime interval (not persisted).
 * Logic inlined in update() to save flash. */
 uint32_t _smoothedLatencyMs = 0; /**< EMA of observed latency (alpha 0.3) */
 uint32_t _effectiveIntervalMs = 0; /**< Computed effective interval (vs cfg.telInterval) */

 void resetBackoff( );
 void escalateBackoff( );
 uint32_t jitter(uint32_t base);


 bool collectBatch(std::vector<BinaryHistoryRecord>& batch, uint32_t& newCursor);


 bool attemptHttpUpload(String& payload, uint32_t newCursor);

 /* HTTP TLS — reusable client (avoids reallocating ~16KB each upload) */
 WiFiClientSecure* _httpSecurePtr = nullptr;
 uint32_t _httpSecureLastUse = 0;

 WiFiClient _mqttWifiClient;
 WiFiClientSecure* _mqttSecurePtr = nullptr; /**< Allocated on demand (MQTT+TLS only) ~16KB */
 PubSubClient _mqttClient;
 bool _mqttInitialized;
 uint32_t _lastMqttReconnect;

 bool mqttEnsureConnected( );
 bool attemptMqttPublish(String& payload, std::vector<BinaryHistoryRecord>& batch, uint32_t newCursor);
 String buildMqttClientId( );
 uint8_t safeBatchLimit(uint8_t configured);

 /** @brief Intentional no-op — freeing TLS clients would cause heap fragmentation. */
 void releaseIdleResources( );


 String buildPayload(std::vector<BinaryHistoryRecord>& batch);
 int formatLineJsonBuf(const BinaryHistoryRecord& rec, const SystemConfig& cfg, char* dest, size_t maxLen);
 String formatLineJson(const BinaryHistoryRecord& rec, const SystemConfig& cfg);
 int formatLineCustomBuf(const BinaryHistoryRecord& rec, const SystemConfig& cfg, char* dest, size_t cap);
 String formatLineCustom(const BinaryHistoryRecord& rec, const SystemConfig& cfg);
};
