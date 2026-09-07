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
#include "simut_config.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "SystemDefs.h"
#include "StorageManager.h"
#include "NetworkManager.h"
#include "LogManager.h"
#include "AlarmQueue.h" /* 2ª linha de telemetria (v21) */

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

 /** True when a CA certificate was loaded at boot (/cert.pem). When
  *  encryption is on and this is false, TLS runs via setInsecure( ) — encrypted
  *  but not authenticated. The web UI derives a "no cert validation" seal from
  *  it (M-8). */
 bool isTlsCertLoaded( ) const { return _hasCert; }

 uint16_t getPendingEstimate( ) const;
 /** Milliseconds until the current backoff (punishment) expires; 0 when the
  * uploader may send immediately. Used by the Air M1 cycle to sleep for the
  * backoff when it exceeds the wake interval. */
 uint32_t getBackoffRemainingMs( ) const;
 /** Air FLUSH: send back-to-back while pending, ignoring telInterval (the
  * backoff after a failure still applies). Off again when the phase ends. */
 void setDrainMode(bool on) { _drainMode = on; }
 void refreshPendingCount( );
 void notifyNewRecord( );


 /**
 * @brief Consumes the last send result (if any).
 * @param outSuccess Receives true on success, false on failure.
 * @return true if there was a pending result (outSuccess valid), false if nothing happened.
 */
 bool consumeLastSendResult(bool& outSuccess);

 /* ────────────────────────────────────────────────────────────────────────
  * SEGUNDA LINHA DE TELEMETRIA — ALARMES (v21)
  *
  * Fila em RAM com confirmação de recebimento (R3 da proposta em
  * docs/analysis/ANALISE_TELEMETRIA_ALARMES.md). O produtor é
  * AppManager::checkAlarmConditions (detecção de borda); o consumidor é
  * updateAlarms( ), chamado de update( ) a cada loop. Transporte, servidor
  * e criptografia são herdados da telemetria convencional; só o formato do
  * payload é próprio (cfg.alarmTel.*). */
 /* ──────────────────────────────────────────────────────────────────────── */

 /** Enfileira um alarme (borda detectada pelo AppManager). Escala o valor
  * pelo canal e atribui o seq. err=true grava HIST_NAN_SENTINEL no valor.
  * Arma o envio imediato (updateAlarms não espera o intervalo).
  * @return seq atribuído, ou 0 quando recusado (estouro) / linha desligada. */
 uint16_t pushAlarm(uint8_t slot, uint8_t channel, float value, uint8_t errCode);

 uint8_t alarmQueueSize( ) const { return _alarmQueue.size( ); }
 uint16_t alarmDropped( ) const { return _alarmQueue.dropped( ); }
 bool isAlarmLineEnabled( ) const { return _alarmEnabled; }

 /** Esvazia a fila sem confirmar (comando manual do CLI). */
 void flushAlarmQueue( ) { _alarmQueue.clear( ); }

 /** 'alarm dump' (CLI): arma o dump + dispara o envio imediato. */
 void forceAlarmSync( ) { _alarmDumpNext = true; _alarmSendPending = true; }

 /** Aplica enabled/queueMax em RUNTIME (CLI 'alarm set' sem reboot; o commit
  * web reboots e passa pelo begin( ) mesmo). Demais campos são lidos vivos de
  * cfg em cada ciclo. */
 void applyAlarmRuntimeConfig(const SystemConfig& cfg) {
 _alarmEnabled = cfg.alarmTel.enabled;
 _alarmQueue.setCapacity(cfg.alarmTel.queueMax);
 }

 /** Dump one-shot do próximo payload de alarmes (CLI/BT, espelho do tel dump). */
 void armAlarmPayloadDump( ) { _alarmDumpNext = true; }
 bool isAlarmPayloadDumpArmed( ) const { return _alarmDumpNext; }

private:
 /* ── estado da 2ª linha ───────────────────────────────────────────────── */
 AlarmQueue _alarmQueue;
 bool _alarmEnabled = false;            /**< cfg.alarmTel.enabled no boot */
 volatile bool _alarmSendPending = false; /**< gatilho imediato após push */
 uint32_t _lastAlarmAttempt = 0;        /**< millis() do último ciclo de envio */
 volatile bool _alarmSending = false;   /**< CAS do ciclo de alarmes */
 volatile bool _alarmDumpNext = false;  /**< one-shot 'alarm dump' */
 static TelemetryManager* s_alarmInstance; /**< para o callback MQTT de ACK */

 static const uint32_t ALARM_RETRY_INTERVAL_MS = 15000;
 static const uint8_t ALARM_BATCH_MAX = 64; /**< == ALARM_QUEUE_MAX */

 void updateAlarms( );
 /** Monta o payload da fila no formato cfg.alarmTel (JSON/CSV/custom).
  * A formatação de linha vive em AlarmPayload.h (alarmFormatLine). */
 String buildAlarmPayload(std::vector<AlarmRecord>& batch);
 bool attemptAlarmHttpUpload(String& payload, std::vector<AlarmRecord>& batch);
 bool attemptAlarmMqttPublish(String& payload, std::vector<AlarmRecord>& batch);
 /** base + "/alarm" e base + "/alarm/ack" (mesma regra de fallback do tópico de dados). */
 String mqttAlarmTopic( );
 String mqttAlarmAckTopic( );
 /** Assina o tópico de ACK após cada (re)conexão MQTT. */
 void mqttSubscribeAlarmAck( );
 /** Callback estático do PubSubClient — roteia para o instance único. */
 static void mqttAlarmAckCallback(char* topic, uint8_t* payload, unsigned int length);
 /** Processa {"seq":[...]}: remove os confirmados da fila + métricas. */
 void handleAlarmAckPayload(const uint8_t* payload, unsigned int length);
 /** Confirmou os seqs contidos no batch (HTTP 2xx / MQTT publish aceito). */
 void ackAlarmBatch(const std::vector<AlarmRecord>& batch);
 void _dumpAlarmPayload(const char* payload, size_t len, const char* label);
 StorageManager* _storageRef;
 NetworkManager* _netRef;

 uint32_t _lastCheckTime;
 bool _drainMode = false; /**< update( ) ignores the interval; see the comment there. */
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
#if TEL_TLS_KEEPALIVE_EXPERIMENT
 /* Session reuse is state of the HTTPClient INSTANCE (_canReuse starts false
  * on every new one), so the kept TLS socket is only picked up again by the
  * same instance: one lives here for the secure path, for as long as the
  * experiment is on. */
 HTTPClient* _httpKeepPtr = nullptr;
#endif

 WiFiClient _mqttWifiClient;
 WiFiClientSecure* _mqttSecurePtr = nullptr; /**< Allocated on demand (MQTT+TLS only) ~16KB */
 PubSubClient _mqttClient;
 bool _mqttInitialized;
 uint32_t _lastMqttReconnect;

 bool mqttEnsureConnected( );
 bool attemptMqttPublish(String& payload, std::vector<BinaryHistoryRecord>& batch, uint32_t newCursor);
 String buildMqttClientId( );
 uint8_t safeBatchLimit(uint8_t configured);

 /* Resolved MQTT topics. Both live behind the same fallback ("simut/data"
 * when cfg.mqttTopic is blank) so the LWT, the data publishes and the
 * discovery messages can never drift apart. */
 String mqttDataTopic( );
 String mqttStatusTopic( );

 /* Home Assistant discovery. Driven by two persisted bits (want = the user
 * toggle && JSON mode; have = FLAG_HA_PUBLISHED) rather than by commit
 * hooks, because commit_all reboots — the connect after that reboot is
 * the only reliable place to publish the refresh or the removal. */
 void haDiscoveryReconcile(bool forceRepublish);
 bool publishHaDiscovery(bool enable);

 /** @brief Intentional no-op — freeing TLS clients would cause heap fragmentation. */
 void releaseIdleResources( );


 String buildPayload(std::vector<BinaryHistoryRecord>& batch);
 int formatLineJsonBuf(const BinaryHistoryRecord& rec, const SystemConfig& cfg, char* dest, size_t maxLen);
 String formatLineJson(const BinaryHistoryRecord& rec, const SystemConfig& cfg);
 int formatLineCustomBuf(const BinaryHistoryRecord& rec, const SystemConfig& cfg, char* dest, size_t cap);
 String formatLineCustom(const BinaryHistoryRecord& rec, const SystemConfig& cfg);
};
