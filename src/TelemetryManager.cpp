/**
 * @file TelemetryManager.cpp
 * @brief Implementation of TelemetryManager — batch collection, HTTP/MQTT transport, and payload builders.
 * @details Implements flash-efficient batch collection using ReadLock (no Core 1
 * pause), HTTP upload with configurable auth headers, MQTT transport
 * with LWT (Last Will & Testament), individual and batch publish
 * strategies, exponential backoff with jitter, and three payload
 * format builders (JSON, CSV, custom template).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "TelemetryManager.h"
#include "MetricsManager.h"
#include "HaDiscovery.h"
#include "AlarmPayload.h" /* formatadores da 2ª linha (header-only, testáveis) */
#include "TelemetryCursor.h" /* what the cursor may advance to (header-only, testable) */
#include "TelContentType.h" /* the Content-Type each line sends (header-only, testable) */

/* TelContentType.h mirrors TelMode instead of including it (its native suite
 * cannot include the records header); this is the pin that keeps the mirror
 * honest. */
static_assert(TEL_CT_MODE_JSON == TEL_MODE_JSON && TEL_CT_MODE_CSV == TEL_MODE_CSV &&
              TEL_CT_MODE_CUSTOM == TEL_MODE_CUSTOM,
              "TelContentType.h's mode values drifted from TelMode in SystemDefs_Records.h");
#include "TouchPriority.h"
#include "BuildIdentity.h"
#include "sensors/SensorChannelTable.h"
#include <LittleFS.h>
#include <algorithm>
#include <string.h>
#include <hardware/watchdog.h>
#include <pico/time.h>
#include <memory>

static void addIdentityHeaders(HTTPClient& http, StorageManager* storage);

/* The ~5.9 KB of V4 decode scratch that lived here is gone: collectBatch and
 * refreshPendingCount now read through StorageManager's V5 reader, which owns
 * the one block buffer the whole firmware shares. */

/**
 * @brief true se o arquivo de histórico @p fileName é de um dia ANTERIOR a
 *        @p minDay ("YYYYMMDD").
 *
 * @details L2: o corte de arquivos comparava o nome inteiro contra um limite
 * montado com o sufixo ".bin" fixo. Como os 8 dígitos da data decidem a
 * ordem antes de o sufixo pesar, funcionava — por acidente, e só enquanto
 * todas as extensões coexistissem sem mudar. Comparar exatamente a parte
 * que significa alguma coisa remove o acidente.
 *
 * Nomes com menos de 8 caracteres ou com não-dígitos no prefixo não são
 * arquivos de dia válidos; são mantidos (não cortados) para que a leitura
 * decida, em vez de sumirem silenciosamente aqui.
 *
 * @param fileName Nome do arquivo (sem diretório), e.g. "20260724.sim4".
 * @param minDay   Data limite no formato "YYYYMMDD".
 * @return true se deve ser pulado.
 */
/* Largest MQTT buffer the client will be asked for, and the fixed-header plus
 * topic-length overhead a PUBLISH carries on top of its payload. Named because
 * the ceiling is the difference between "this batch is published record by
 * record" and "telemetry stops forever" — see attemptMqttPublish. */
static constexpr size_t MQTT_BUFFER_CEILING = 8192;
static constexpr size_t MQTT_PACKET_OVERHEAD = 16;

/* SYS_TEL_FAIL's context when the configuration asks for encryption and the
 * image carries no TLS client (SIMUT_TEL_TLS=0). Far from everything else that
 * code's ctx holds — HTTP status codes, HTTPClient's -1..-11, PubSubClient's
 * -4..5, a failure count — so a log reader cannot take it for a network
 * failure: nothing was attempted, nothing left the device. */
static constexpr int TEL_CTX_NO_TLS = -200;
/* The same, for a configuration that names MQTT in an image without the MQTT
 * transport (SIMUT_TEL_MQTT=0). */
static constexpr int TEL_CTX_NO_MQTT = -201;

/*
 * TelemetryGuard is gone, and deliberately not replaced.
 *
 * It claimed to feed the watchdog during blocking network calls, via a 2 s
 * repeating timer. Measured on hardware 2026-07-25: the timer registers fine
 * and ticks correctly right up to http.POST(), then stops feeding the instant
 * the POST blocks. It never did its job in any build — what kept telemetry
 * alive was POSTs being fast, not the guard.
 *
 * Making it work would have been worse. The blocking was a TLS handshake with
 * no overall deadline (fixed in the framework — see
 * tools/arduino_pico_overrides/patches/wifi_tls_handshake_deadline.patch), and
 * a guard that fed through it would have turned a recoverable watchdog reboot
 * into a permanent freeze. That was verified the hard way: disarming the
 * watchdog around the POST left the device wedged with USB still enumerated
 * and both the CLI and the web dead, until a hardware reset.
 *
 * The rule this leaves: bound the blocking call, and let the watchdog be the
 * backstop. Never widen the window (the RP2040 ceiling is WATCHDOG_TIMEOUT_MS
 * = 8388 ms regardless of what you ask for) and never feed from an interrupt
 * to survive a call that should have been bounded in the first place.
 */

/* A ordem desta lista segue a ordem de DECLARAÇÃO em TelemetryManager.h
 * (_alarmQueue na linha 137, _mqttClient na 236), que é a ordem em que o
 * compilador realmente inicializa, independentemente do que se escreva aqui.
 * Escrevê-la ao contrário era só um aviso hoje, mas é a forma exata de um bug
 * futuro: basta um membro passar a depender de outro na construção. */
TelemetryManager::TelemetryManager( )
 : _alarmQueue(ALARM_QUEUE_DEFAULT)
#if SIMUT_TEL_MQTT
 , _mqttClient(_mqttWifiClient)
#endif
{
 /* Both were left as indeterminate members until begin( ) ran, which is fine
  * only while nothing touches them first — and the Air boot now does: it asks
  * whether this wake is due before the managers are wired up. An uninitialised
  * pointer read does not necessarily crash; it quietly answers something. */
 _storageRef = nullptr;
 _netRef = nullptr;
 _hasCert = false;
 _currentBackoff = BACKOFF_MIN_MS;
 _backoffUntil = 0;
 _consecutiveFails = 0;
#if SIMUT_TEL_MQTT
 _mqttInitialized = false;
 _lastMqttReconnect = 0;
#endif
 s_alarmInstance = this;
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

 SystemConfig &cfg = _storageRef->getConfig( );
#if SIMUT_TEL_TLS
 if (cfg.telEncryption) {
 if (LittleFS.exists("/cert.pem")) {
 File certFile = LittleFS.open("/cert.pem", "r");
 if (certFile) {
 /* N9: reject cert > 16 KB to avoid OOM at boot */
 if (certFile.size( ) > 16384) {
 LOG_CODE(LOG_WARN, "TEL", TEL_CERT_READ_ERR, (int)certFile.size( ), "cert.pem too large");
 certFile.close( );
 } else {
 _cachedCert = certFile.readString( );
 certFile.close( );
 if (_cachedCert.length( ) > 0) {
 _hasCert = true;
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_SSL, _cachedCert.length( ), "SSL cert.pem loaded (" + String(_cachedCert.length( )) + " bytes)");
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

 /* M-8: one clear, once-per-boot record when encryption is ON but no valid
  * cert was loaded — from here every transport calls setInsecure( ), so the
  * TLS session is encrypted but NOT authenticated and a man in the middle can
  * present any certificate, read the API key and payloads, and answer 200.
  * The per-cause lines above (too large / empty / missing) name WHY; this one
  * names the CONSEQUENCE, which the empty-message WARNs did not — an operator
  * read "cert read error" as a file glitch, not "telemetry is unauthenticated".
  * ctx=1 distinguishes it from the ctx=0 file-missing line. */
 if (!_hasCert) {
 LOG_CODE(LOG_WARN, "TEL", TEL_CERT_READ_ERR, 1,
          "TLS on without cert validation: connection not authenticated (MITM possible) — upload /cert.pem");
 }
 }
#else
 /* No TLS client in this image. A config that asks for encryption — restored
  * from an image that had one, or written before the switch existed — is not
  * sent at all: falling back to plain TCP would put the API key and the MQTT
  * password on the wire. Said once here, with the reason; the transports then
  * refuse under the same code, which the log's family latch keeps quiet. */
 if (cfg.telEncryption) {
 LOG_CODE(LOG_WARN, "TEL", SYS_TEL_FAIL, TEL_CTX_NO_TLS,
          "encryption is on and this image has no TLS client: telemetry is not sent — turn t_sec off");
 }
#endif


 /* v21 — segunda linha (alarmes). Só o formato é próprio; transporte,
  * servidor e criptografia vêm da config convencional. Desligada por
  * padrão (migração/fábrica) — ligar via web ou CLI. */
 _alarmEnabled = cfg.alarmTel.enabled;
 _alarmQueue.setCapacity(cfg.alarmTel.queueMax);
 if (_alarmEnabled) {
 LOG_CODE(LOG_INFO, "TEL", TEL_ALARM_LINE_ON, (int)_alarmQueue.capacity( ),
          "alarm telemetry line enabled (queue " + String(_alarmQueue.capacity( )) + ")");
 }

 if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
#if !SIMUT_TEL_MQTT
 /* No MQTT client in this image. Every other branch of the transport choice
  * is HTTP, so a saved MQTT transport would POST the batch to the broker's
  * port: it is refused instead — said once here, and again at every send
  * under the same code, which the log's family latch keeps quiet. */
 LOG_CODE(LOG_WARN, "TEL", SYS_TEL_FAIL, TEL_CTX_NO_MQTT,
          "transport is MQTT and this image has no MQTT client: telemetry is not sent — set t_transport to HTTP");
#else
 if (cfg.telEncryption) {
#if !SIMUT_TEL_TLS
 /* The client below was built on the plain socket (_mqttWifiClient), and
  * a connect on it would carry the password in the clear to a port that
  * expects TLS. Leaving _mqttInitialized false keeps every MQTT path shut. */
 resetBackoff( );
 return;
#else
 _mqttSecurePtr = new WiFiClientSecure( );
 if (_mqttSecurePtr) {
 _mqttSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
 /* Same 16 KB contiguous block that attemptHttpUpload documents at
  * length — BearSSL's default _clear() asks setBufferSizes(16384, 512)
  * and allocates the iobuf inside _connectSSL. The HTTP path got the
  * 4096 cap in v1.5.3-beta; this one never did, and MQTTS has been
  * dying of it ever since.
  *
  * Measured on the bench 2026-08-15, same backlog (39.2 k pending),
  * same t_bat and t_int, TLS the only variable:
  *   MQTTS  largest block 9542 B, heap 17680 — pending FROZEN at
  *          39234, telSent stuck at 1, telRetries climbing
  *   MQTT   largest block 29390 B, heap 39392 — pending draining,
  *          telSent 3 -> 72, telRetries 0
  * The first TLS connect succeeds and takes its ~16.7 KB; from then on
  * the largest free block is 9.5 KB and no reconnect can ever get its
  * own, so the cursor never advances again. The broker was innocent:
  * all three handshakes it saw completed without a failure.
  *
  * Setting it here, once, is enough where HTTP needs it per attempt:
  * _httpSecurePtr is recreated on socket error, this object is built
  * once in begin( ) and reused for the life of the boot. */
 _mqttSecurePtr->setBufferSizes(4096, 512);
 if (_hasCert) {
 _mqttSecurePtr->setCACert(_cachedCert.c_str( ));
 } else {
 _mqttSecurePtr->setInsecure( );
 }
 _mqttClient.setClient(*_mqttSecurePtr);
 }
#endif
 } else {
 _mqttWifiClient.setTimeout(NET_SOCKET_TIMEOUT_MS);
 _mqttClient.setClient(_mqttWifiClient);
 }

 _mqttClient.setServer(cfg.telServer, cfg.telPort);
 _mqttClient.setKeepAlive(cfg.mqttKeepAlive > 0 ? cfg.mqttKeepAlive : 60);

 /* PubSubClient socket timeout: limits read/write blocking */
 _mqttClient.setSocketTimeout(NET_SOCKET_TIMEOUT_MS / 1000);

 _mqttClient.setBufferSize(2048);

 /* Callback único do cliente: o ACK por aplicação da linha de alarmes
  * ({"seq":[...]} no tópico {base}/alarm/ack) chega por aqui. A assinatura
  * é (re)feita em cada conexão — ver mqttEnsureConnected. */
 _mqttClient.setCallback(TelemetryManager::mqttAlarmAckCallback);

 _mqttInitialized = true;
 LOG_CODE(LOG_INFO, "TEL", TEL_MQTT_INIT, cfg.telPort, String(cfg.telServer));
#endif
 } else {
 /*
 * HTTP: pre-allocate WiFiClientSecure at boot to avoid fragmentation.
 * If allocated later, the heap may be too fragmented for
 * the ~16KB contiguous block that TLS needs.
 */
#if SIMUT_TEL_TLS
 if (cfg.telEncryption && cfg.telInterval > 0) {
 _httpSecurePtr = new WiFiClientSecure( );
 if (_httpSecurePtr) {
 _httpSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
 if (_hasCert) _httpSecurePtr->setCACert(_cachedCert.c_str( ));
 else _httpSecurePtr->setInsecure( );
 }
 }
#endif
 LOG_CODE(LOG_INFO, "TEL", TEL_HTTP_INIT, cfg.telPort, String(cfg.telServer) + String(cfg.telPath));
 }

 resetBackoff( );

}

/* The rule, and the three ways it has been wrong, live in TelemetryCursor.h
 * where the host tests reach them. This is the vector-shaped door to it. */
static uint32_t deliveredCursor(const std::vector<BinaryHistoryRecord>& batch,
                                uint32_t fromCursor, uint32_t nowEpoch,
                                size_t n = SIZE_MAX) {
 return telDeliveredCursor(batch.data( ), n < batch.size( ) ? n : batch.size( ), fromCursor,
                           nowEpoch, (uint32_t)HIST_EPOCH_MIN);
}

/* What reached the server, as positions (A-04): the runs collectBatch kept,
 * cut to the records the payload carries or the broker took. */
void TelemetryManager::trimRuns(const std::vector<BinaryHistoryRecord>& batch, size_t n) {
 if (n > batch.size( )) n = batch.size( );
 telRunsTrim(_runs, _nRuns, (uint8_t)n, n ? batch[n - 1].epoch : 0);
}

void TelemetryManager::markDelivered(uint32_t lastEpoch) {
 _storageRef->telCursorDelivered(_runs, _nRuns, lastEpoch);
 _nRuns = 0;
}

void TelemetryManager::markDeliveredPrefix(const std::vector<BinaryHistoryRecord>& batch, size_t n) {
 trimRuns(batch, n);
 markDelivered(deliveredCursor(batch, 0, (uint32_t)time(nullptr), n));
}

/**
 * @brief Periodic telemetry check — collects batch and dispatches via configured transport.
 * Respects backoff intervals, network availability, and heavy task locks.
 */
void TelemetryManager::update( ) {
 /* Segunda linha (alarmes): ciclo próprio, independente do intervalo da
  * telemetria convencional — um alarme não espera a cadência de massa. */
 updateAlarms( );

 SystemConfig &cfg = _storageRef->getConfig( );

 /*
 * MQTT keepalive: calls loop() only if connected to the broker.
 * Prevents loop() from attempting implicit reconnect with long socket
 * timeout that would freeze the main loop on degraded networks.
 *
 * BEFORE the telemetry-off gate below, not after it: loop( ) is also the only
 * place PubSubClient hands over a message, and the alarm line's ACK arrives as
 * one (mqttAlarmAckCallback). With the conventional line off (t_int = 0) and
 * the alarm line on MQTT, loop( ) never ran: no ACK was ever read, the queue
 * never drained, and the line republished the same batch every 15 s until the
 * queue was full. Found by reading while the v2.7.1 manual was written
 * (finding 28).
 */
#if SIMUT_TEL_MQTT
 if (cfg.telTransport == TEL_TRANSPORT_MQTT && _mqttInitialized
 && _mqttClient.connected( )) {
 _mqttClient.loop( );
 watchdog_update( );

 /* HA discovery safety net for config paths that do NOT reboot (commit_all
  * does, and its post-reboot connect reconciles there): while connected,
  * a mismatch between the toggle and the persisted published bit is
  * settled here. Two RAM reads per pass when in sync; the CAS keeps the
  * publish burst from interleaving with a send. Only with the conventional
  * line on, as before loop( ) moved above the gate: with telemetry off
  * there is nothing to reconcile, and a want computed then would unpublish
  * the entities. */
 if (cfg.telInterval != 0 && _mqttClient.connected( )) {
 bool haWant = _storageRef->isHaDiscoveryEnabled( ) && cfg.telMode == TEL_MODE_JSON;
 if (haWant != _storageRef->wasHaDiscoveryPublished( )) {
 bool expected = false;
 if (__atomic_compare_exchange_n(&_isSending, &expected, true,
 false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
 haDiscoveryReconcile(false);
 __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
 }
 }
 }
 }
#endif

 if (cfg.telInterval == 0) return;

 uint32_t now = millis( );

#if TEL_TLS_KEEPALIVE_EXPERIMENT
 /* Idle guard for the kept session: a socket the server may have closed on
  * its side is worth nothing, and holding it costs the pool. Three seconds is
  * far longer than any back-to-back cadence and far shorter than any interval
  * a server would keep an idle connection for. */
 if (_httpSecurePtr && _httpSecurePtr->connected( ) && (now - _httpSecureLastUse) > 3000UL) {
 _httpSecurePtr->stop( );
 }
#endif

 if (_consecutiveFails > 0 && now < _backoffUntil) return;

 /* Two gates, and the configuration is only in the first one.
  *
  * telMinBatch (the field the web still calls t_int) is a COUNT of pending
  * records, not a time: telemetry happens when that many are waiting, and
  * then the drain empties the queue in batches of at most telBatchSize. A
  * clock never enters into it. Zero means telemetry is off, which is the
  * gate at the top of this function.
  *
  * It used to be milliseconds, and a floor between batches at that, which
  * made the configured value the throughput ceiling: at 300 s, one batch
  * every five minutes, so 35,000 pending records needed 31 hours; and inside
  * an Air wake, which is a boot, the first send was due after the whole
  * interval and the wake slept having sent nothing (measured: 57 s awake with
  * the radio on, 0 records).
  *
  * The second gate is the gap between batches WITHIN a drain, and that comes
  * from the server — see the cadence block after the send.
  *
  * The pending count is a RAM counter that the history writer bumps on every
  * new record (notifyNewRecord), so this gate costs one load. The drain does
  * not consult it again: it runs until collectBatch finds nothing, which is
  * the authority, and that is what clears the counter below. */
 if (!_drainActive) {
 if (!_drainMode && !telemetryDue( )) return;
 /* One breath after boot before the first send of a drain. The old code
  * waited a whole interval here, for a real reason: a TLS handshake plus a
  * POST while the rest of setup( ) is still settling used to reach the
  * watchdog. The Air wake bypasses it (drain mode) because there the whole
  * point is to send and go back to sleep. */
 if (now < TEL_FIRST_SEND_DELAY_MS) return;
 _drainActive = true;
 _gapMs = 0;
 _nextSendAt = now;
 } else if ((int32_t)(now - _nextSendAt) < 0) {
 return;
 }

 /* An operator's finger outranks a backlog. Back-to-back batches hold Core 0
  * for 70 to 280 ms at a time, and the old floor hid that by sending once a
  * minute; a drain must not make the screen feel dead. The drain is not
  * cancelled, only paced. Always false on a headless build — no provider is
  * registered — so the Air wake is untouched. */
 if (TouchPriority::isActive( )) { _nextSendAt = now + 1000UL; return; }


 /* Atomic CAS: prevents race between periodic update() and forceSync() CLI */
 bool expected = false;
 if (!__atomic_compare_exchange_n(&_isSending, &expected, true,
 false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return;
 if (!_netRef->isNetworkHealthy( )) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return; }
 if (!_storageRef->lockHeavyTask( )) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return; }

 /*
 * This asks for 120 s and gets 8.388 s, like every other WdtWindow in the
 * codebase: the RP2040 load register cannot express more (see the class
 * comment in LogManager.h). It is kept only so nested saves/logs cannot
 * shrink the window below the default mid-cycle, and auto-restores on any
 * exit path. It buys NO extra time — every blocking call in this cycle has
 * to be bounded on its own.
 */
 LogManager::WdtWindow _wdt(120000);

 /* Abort if heap is critically low.
 * Previously only checked getFreeHeap() < 20K — ignored fragmentation.
 * BearSSL needs ~16 KB contiguous block for TLS context; if the largest
 * block falls below that, malloc() fails mid-handshake → undefined
 * behavior. */
 uint32_t freeH = rp2040.getFreeHeap( );
 extern char* __brkval; (void)__brkval;
 /* Largest block via arduino-pico API (no direct — use heuristic
 * approxBlock = freeH / 2 if fragmented (ESP-style heap), or freeH if
 * contiguous. Conservative: requires freeH >= 24K (covers TLS 16K + margin).
 *
 * v2.3.1: the floor is now split by transport. The 24K figure exists for the
 * BearSSL client scratch, which PLAIN HTTP/MQTT never allocates — yet the
 * unconditional gate kept plain telemetry silent on any config idling below
 * 24 KB (measured: HTTPS web UI resident leaves ~16 KB idle free; every cycle
 * aborted right here and only escalated backoff). Plain floor = collectBatch's
 * own reserve (12288) + 2 KB working margin; safeBatchLimit and buildPayload
 * then size the batch to what actually fits. */
 const uint32_t PREFLIGHT_FLOOR = cfg.telEncryption ? 24576 : 14336;
 if (freeH < PREFLIGHT_FLOOR) {
 _storageRef->unlockHeavyTask( );
 __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
 escalateBackoff( );
 return;
 }

 /* The cycle the cadence is built on: everything this device spends to deliver
  * one batch — directory scan, decode, payload, connect, POST, end( ). The
  * POST alone (what _smoothedLatencyMs and metr.tl report) is 13 ms of a 73 ms
  * plain cycle, so pacing on it would have measured the wrong thing. */
 const uint32_t cycleStart = millis( );

 std::vector<BinaryHistoryRecord> batch;
 uint32_t newCursor = 0;

 if (!collectBatch(batch, newCursor)) {
 __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
 _storageRef->unlockHeavyTask( );
 /* Nothing left: the drain is over. collectBatch is the authority on what is
  * really sendable — it applies the 30-day floor and the cursor — so this is
  * also the moment the pending counter is known to be zero. Saying so keeps
  * a stale estimate from re-arming the trigger in a loop; the periodic
  * refreshPendingCount confirms it from flash a few seconds later. */
 _drainActive = false;
 _gapMs = 0;
 __atomic_store_n(&_pendingEstimate, 0, __ATOMIC_RELAXED);
 _pendingDirty = true;
 resetBackoff( );
#if TEL_TLS_KEEPALIVE_EXPERIMENT
 /* Drain over: nothing more to send, so the session has nothing to amortise. */
 if (_httpSecurePtr) _httpSecurePtr->stop( );
#endif
 return;
 }


 bool success = false;

 if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
 /*
 * MQTT: needs batch for individual publish (≤5 items).
 * For larger batches, buildPayload + free batch.
 */
 String payload = buildPayload(batch);
 if (_dumpPayloadNext) {
 _dumpPayload(payload.c_str( ), payload.length( ), "MQTT");
 _dumpPayloadNext = false;
 }
 /* buildPayload can drop records off the end under heap pressure, so the
  * cursor has to follow what the payload actually carries — see
  * deliveredCursor for why the last element is not that. */
 newCursor = deliveredCursor(batch, newCursor, (uint32_t)time(nullptr));
 trimRuns(batch, batch.size( ));
 /* Empty = nothing well formed could be built (see buildPayload): no
  * publish, and the failure path below owns the retry. */
 if (payload.length( ) > 0) success = attemptMqttPublish(payload, batch, newCursor);
 /* batch and payload go out of scope here and free memory */
 } else {
 /*
 * HTTP: builds payload, frees batch BEFORE POST.
 * This avoids batch (~7KB) + payload (~13KB) + TLS (~16KB)
 * coexisting in RAM simultaneously.
 */
 String payload = buildPayload(batch);
 if (_dumpPayloadNext) {
 _dumpPayload(payload.c_str( ), payload.length( ), "HTTP");
 _dumpPayloadNext = false;
 }

 /* Same reason as the MQTT branch above: read the frontier off the batch
  * buildPayload left behind, before it is thrown away. */
 newCursor = deliveredCursor(batch, newCursor, (uint32_t)time(nullptr));
 trimRuns(batch, batch.size( ));

 /* Free batch to reduce RAM peak before TLS handshake */
 batch.clear( );
 batch.shrink_to_fit( );

 /* Same as the MQTT branch: an empty payload is a failure to build, not a
  * batch to POST. */
 if (payload.length( ) > 0) success = attemptHttpUpload(payload, newCursor);
 }

 __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
 _storageRef->unlockHeavyTask( );
 _pendingDirty = true; /* Recalibrate after send */

 _lastCycleMs = millis( ) - cycleStart;

 if (success) {
 resetBackoff( );

 /* Cadence. "Fast" is measured against what this device costs on this
  * transport (TEL_FAST_MS_*): a server that answers quicker than the work
  * around it is not the bottleneck, so the next batch goes at once.
  *
  * Past that mark the gap is the cycle itself — the server gets as long to
  * breathe as it took to answer, which is the whole of "do not flood a slow
  * collector". Doubling is for a server that is getting WORSE, not merely
  * slow: measured 2026-09-07, doubling on every slow batch drove a perfectly
  * healthy 0.5 s collector to the 10 s ceiling and left it there, 6.6
  * records/s against the 37 the old fixed floor managed. A 0.5 s answer is
  * what a cloud ingest endpoint looks like on a good day; punishing it is
  * not backpressure, it is a bug. So the escalation needs evidence that the
  * pressure is real: this cycle noticeably worse than the recent average. */
 const uint32_t fastMs = cfg.telEncryption ? (uint32_t)TEL_FAST_MS_TLS
                                           : (uint32_t)TEL_FAST_MS_PLAIN;
 /* Increase on ANY success, not only a fast one. A slow server that keeps
  * answering 200 is telling us it can take the payload; what it cannot take
  * is the RATE, and the gap below is what answers that. Tying the batch to
  * speed instead measured badly: against a 3 s collector the batch stayed at
  * 50 for the whole wake, so every one of those expensive cycles carried
  * half of what it could have (bench, 2026-09-07). The one signal that means
  * "too big" is a failure, and that halves it. The heap ceiling still
  * decides the real limit, in collectBatch, where the heap is read. */
 const uint16_t ceiling = (cfg.telBatchSize > 0) ? (uint16_t)cfg.telBatchSize
                                                 : (uint16_t)TEL_BATCH_MAX;
 const uint16_t grown = (uint16_t)_batchAuto + (uint16_t)(_batchAuto / 2);
 _batchAuto = (uint8_t)((grown > ceiling) ? ceiling : grown);

 if (_lastCycleMs <= fastMs) {
 _gapMs = 0;
 } else {
 const bool worsening = (_cycleEmaMs > 0) &&
                        (_lastCycleMs > _cycleEmaMs + (_cycleEmaMs / 4));
 uint32_t g = worsening ? ((_gapMs > 0) ? (_gapMs * 2) : (_lastCycleMs * 2))
                        : _lastCycleMs;
 if (g < _lastCycleMs) g = _lastCycleMs;
 if (g > (uint32_t)TEL_GAP_MAX_MS) g = (uint32_t)TEL_GAP_MAX_MS;
 _gapMs = g;
 }
 /* The reference the next cycle is judged against. Updated after the test,
  * so "worse than the recent average" means the average before this one. */
 _cycleEmaMs = _cycleEmaMs ? ((_cycleEmaMs * 7 + _lastCycleMs * 3) / 10)
                           : _lastCycleMs;

 /* The RSSI penalty survives, applied to the gap instead of to a floor:
  * a link this weak drops packets, and hammering it is how a retry storm
  * starts. It never pushes past the ceiling. */
 uint32_t gap = _gapMs;
 const int32_t rssi = _netRef ? _netRef->getRssi( ) : 0;
 if (rssi < -85 && rssi > -100) gap *= 2;
 else if (rssi < -75) gap = (gap * 3) / 2;
 if (gap > (uint32_t)TEL_GAP_MAX_MS) gap = (uint32_t)TEL_GAP_MAX_MS;
 _effectiveIntervalMs = gap;
 _nextSendAt = millis( ) + gap;
 } else {
 escalateBackoff( );
 /* Multiplicative decrease: a payload the far side could not take is the
  * one thing the heap ceiling cannot predict. The drain stays open — the
  * backoff owns the schedule until it expires, and then it resumes. */
 const uint8_t halved = (uint8_t)(_batchAuto / 2);
 _batchAuto = (halved > (uint8_t)TEL_BATCH_MIN) ? halved : (uint8_t)TEL_BATCH_MIN;
 _gapMs = 0;
 _nextSendAt = millis( );
 }

 /* Signal result to the display */
 _lastSendSuccess = success;
 _hasSendResult = true;

 /* Release idle TLS resources to recover heap */
 releaseIdleResources( );
 /* WdtWindow destructor auto-restores WDT here */
}


/* =========================================================================== */
/* BATCH COLLECTION (SHARED HTTP/MQTT) */
/* =========================================================================== */
/**
 * @brief Collect pending history records into a batch for upload.
 * Uses lightweight ReadLock (no Core 1 pause) for flash I/O.
 * @return false if no pending data (success — nothing to send).
 */
/**
 * @brief Computes safe batch limit based on available heap.
 *
 * Heap remains stable at ~50 KB without
 * graph caches occupying space. Limits loosened to allow
 * significantly larger batches when configured by user.
 *
 * Preserved safety layers:
 * - update() preflight: aborts if heap < 24 KB (TLS) / < 14 KB (plain)
 * - buildPayload: dynamic resize if estimate exceeds available
 * - the TLS handshake is bounded by setTLSConnectTimeout, which only holds
 *   because of the framework patch in tools/arduino_pico_overrides
 *
 * A third layer used to be listed here — "TelemetryGuard feeds WDT during POST
 * up to 60s" — and it was never true, which made these limits look safer than
 * they were. Nothing in this cycle survives a blocking call that is not bounded
 * on its own; the watchdog window cannot be widened past 8.388 s.
 *
 * @param configured Maximum limit configured by user.
 * @return Effective limit (≥1, ≤configured).
 */

uint8_t TelemetryManager::safeBatchLimit(uint8_t configured) {
 SystemConfig& cfg = _storageRef->getConfig( );
 uint32_t freeHeap = rp2040.getFreeHeap( );
 /*
 * HEAP_RESERVE differentiated by encryption:
 * - HTTPS/MQTTS (cfg.telEncryption=true): 24 KB covers TLS reconnect
 * scratch (~10K BearSSL) + HTTPClient (~3K) + transients (~3K) +
 * operational margin (~8K).
 * - HTTP/MQTT plain (cfg.telEncryption=false): 12 KB — no TLS,
 * only HTTPClient + lwIP + margin. Allows ~35% larger batches.
 *
 * BYTES_PER_ENTRY: empirically measured — ~28 batch struct + payload line.
 * JSON ~310 B/line (conservative vs real ~221 for long hwId sensors), so
 * 350 total; CSV lines are fixed-layout and cost ~120 B, so 160 total.
 * The old flat 350 halved CSV batches for no protective reason.
 *
 * HARD_CAP = 250 (v2.3.1, was 50): the cap is no longer the sizing mechanism
 * — the heap formula below is. 50 dated from when the TLS client still took
 * its scratch as one ~16 KB block; with the 4096-B iobuf cap (v1.5.3) and
 * the 32K TLS reserve the formula already lands well under any dangerous
 * payload before the cap is ever reached: reaching N records requires
 * RESERVE + N×BYTES_PER_ENTRY free, so 250 only ever happens when ~100 KB
 * (JSON, plain) is actually free. The cap now only backstops the config
 * field (mirrors the 1..250 range the web/CLI validators accept, in case a
 * corrupted config byte slips through). The stress test that burned batch=200
 * (4 reboots/10 min, lbm=17564) ran with the old flat gates and no
 * buildPayload shrink — both layers below now cover exactly that scenario.
 */
 /* HEAP_RESERVE HTTPS increased 24K → 32K. Stress test
 * showed lbm=17564 mid-POST (largest block fragmented below TLS
 * scratch ~16K) → 4 reboots in 10 min with batch=200. 32K reserve guarantees
 * margin even after accumulated fragmentation from consecutive batches. */
 const uint32_t HEAP_RESERVE = cfg.telEncryption ? 32768 : 12288;
 const uint32_t BYTES_PER_ENTRY = (cfg.telMode == TEL_MODE_CSV) ? 160 : 350;
 const uint8_t HARD_CAP = 250;

 if (freeHeap <= HEAP_RESERVE) return 1;

 uint32_t heapLimit32 = (freeHeap - HEAP_RESERVE) / BYTES_PER_ENTRY;
 uint8_t heapLimit = (heapLimit32 > 255) ? 255 : (uint8_t)heapLimit32;
 return max((uint8_t)1, min(min(configured, HARD_CAP), heapLimit));
}

/* Where each channel of a schema lands in a BinaryHistoryRecord. Rebuilt
 * whenever the reader's schema changes: a day file can carry a second SCHEMA
 * (§3.7-2), and the blocks after it mean something else. The mapping used to
 * be taken once per file, from the first. */
struct TelChanMap {
	const H5ChannelDesc* of = nullptr;
	uint8_t n = 0;
	uint8_t slot[H5_MAX_CHANNELS];
	uint8_t ch[H5_MAX_CHANNELS];
	float   scale[H5_MAX_CHANNELS];

	void use(const H5ChannelDesc* schema, uint8_t nCh) {
		if (schema == of && nCh == n) return;
		of = schema;
		n = schema ? nCh : 0;
		for (uint8_t c = 0; c < n; c++) {
			slot[c]  = (uint8_t)(schema[c].id / MAX_SENSOR_CHANNELS);
			ch[c]    = (uint8_t)(schema[c].id % MAX_SENSOR_CHANNELS);
			scale[c] = powf(10.0f, (float)schema[c].scaleExp);
		}
	}
};

/* The day files, in day order: the collection walks them that way, and the
 * count does not mind. */
void TelemetryManager::listDayFiles(std::vector<String>& files) {
	{
		StorageManager::ReadGuard rg(_storageRef);
		Dir dir = LittleFS.openDir(DIR_HISTORY);
		while (dir.next( )) {
			if (dir.fileName( ).endsWith(HISTORY_FILE_EXT)) files.push_back(dir.fileName( ));
		}
	}
	std::sort(files.begin( ), files.end( ));
}

/* One collection pass: what collectDay( ) shares with collectBatch( ). */
struct TelCollect {
	std::vector<BinaryHistoryRecord>& batch;
	TelCursorState& c;
	uint8_t  limit;
	uint32_t nowEpoch;
	uint32_t firstUnsent;   /**< first day anything was taken from; 0 = none */
	bool     runsFull;      /**< the batch ran out of runs (TEL_RUNS_MAX) */
};

/* A record the writer would refuse today, or the clock cannot place. */
static bool telPlausible(uint32_t epoch, uint32_t nowEpoch) {
	return epoch >= HIST_EPOCH_MIN && (nowEpoch < HIST_EPOCH_MIN || epoch <= nowEpoch + 86400UL);
}

bool TelemetryManager::takeRecord(TelCollect& x, uint32_t day, uint32_t off, uint8_t idx,
                                  uint32_t epoch, const int16_t* vals, const TelChanMap& m) {
	if (!telRunPush(_runs, _nRuns, day, off, idx, (uint8_t)x.batch.size( ), epoch)) {
		x.runsFull = true;
		return false;
	}
	BinaryHistoryRecord rec; rec.clear( ); rec.epoch = epoch;
	for (uint8_t c = 0; c < m.n; c++) {
		if (vals[c] == H5_NAN_SENTINEL) continue;
		const uint8_t slot = m.slot[c];
		if (slot >= MAX_SENSORS) continue;
		const float v = (float)vals[c] * m.scale[c];
		if (m.ch[c] == CH_TEMP)       rec.sensors[slot]  = BinaryHistoryRecord::floatToI16(v);
		else if (m.ch[c] == CH_HUM)   rec.humidity[slot] = BinaryHistoryRecord::floatToI16(v);
		else if (m.ch[c] == CH_PRESS) rec.pressure       = BinaryHistoryRecord::floatToI16x10(v);
	}
	x.batch.push_back(rec);
	if (!x.firstUnsent) x.firstUnsent = day;
	return true;
}

bool TelemetryManager::collectDay(TelCollect& x, uint32_t day, const String& path,
                                  bool withRam, uint32_t ramOff) {
	TelCursorState& c = x.c;
	TelChanMap map;
	int16_t vals[H5_MAX_CHANNELS];
	uint32_t epoch = 0;

	bool opened = false;
	if (path.length( )) {
		StorageManager::ReadGuard rg(_storageRef);
		opened = _storageRef->h5OpenDay(path);
	}
	if (opened) {
		/* The file ends where the open block will land, when it lands here. */
		const uint32_t end = withRam ? ramOff : _storageRef->h5OpenDaySize( );
		telForgetIfBeyond(c, day, end);
		const TelPos* sp = telFind(c, day);
		const TelPos slot = sp ? *sp : TelPos{ };
		/* The blocks before the slot's are hopped by header. The first one
		 * read must be the slot's own and still hold the record it counted
		 * last, or the slot no longer means what it meant. */
		uint32_t minOff = slot.day ? slot.off : 0;
		bool check = slot.day && slot.rec;
		bool holds = true;
		while (holds && x.batch.size( ) < x.limit && !x.runsFull) {
			uint32_t off = 0;
			uint8_t count = 0;
			bool got = false;
			{
				StorageManager::ReadGuard rg(_storageRef);
				got = _storageRef->h5LoadBlockFrom(minOff, off, count);
			}
			minOff = 0;
			if (!got) break;
			map.use(_storageRef->h5ReaderSchema( ), _storageRef->h5ReaderChannels( ));
			/* A slot points at a DATA chunk it took records from. No block
			 * starting there, with one past it, means the file under the
			 * slot is not the one it counted in. */
			const bool anchor = check;
			check = false;
			if (anchor && off != slot.off) { holds = false; break; }
			bool anchored = !anchor;
			for (uint8_t idx = 0; ; idx++) {
				/* RAM only: the block was read whole by h5LoadBlockFrom( ). */
				if (!_storageRef->h5DecodeNext(epoch, vals)) break;
				if (anchor && idx + 1u == slot.rec) {
					anchored = telSlotHolds(slot, true, count, epoch);
					if (!anchored) break;
				}
				if (telUnsent(c, day, off, idx, epoch) && telPlausible(epoch, x.nowEpoch)) {
					if (!takeRecord(x, day, off, idx, epoch, vals, map)) break;
					if (x.batch.size( ) >= x.limit) break;
				}
				if ((idx % 10) == 9) { feedWdt( ); yield( ); }
			}
			if (!anchored) holds = false;
		}
		{ StorageManager::ReadGuard rg(_storageRef); _storageRef->h5CloseDay( ); }
		if (!holds) return false;
	}

	/* The hour still open in RAM, as the block it will be once sealed: the
	 * last of this file, at ramOff.
	 *
	 * A V5 block reaches the day file only when it seals — 60 records, so once
	 * an hour at the default sampling rate. Reading only .h5 meant telemetry
	 * could never send anything newer than the last sealed block: a fresh
	 * device stayed silent for its first 60 minutes, and in steady state every
	 * reading was delivered up to an hour late. Sent from here, a record keeps
	 * its position when the block lands, so nothing goes twice.
	 *
	 * No yield inside this walk — the history writer runs on this same core,
	 * and letting it in here could seal the block while it is being read. It
	 * is at most 60 records. */
	if (withRam && x.batch.size( ) < x.limit && !x.runsFull) {
		const uint8_t ramCount = _storageRef->h5RamCount( );
		telForgetIfBeyond(c, day, ramOff);
		const TelPos* sp = telFind(c, day);
		if (sp && sp->rec && sp->off == ramOff) {
			uint32_t e = 0;
			const bool have = ramCount >= sp->rec
			                  && _storageRef->h5RamRecord((uint8_t)(sp->rec - 1u), e, vals);
			if (!telSlotHolds(*sp, true, ramCount, have ? e : 0)) return false;
		}
		map.use(_storageRef->getH5Schema( ), _storageRef->getH5ChannelCount( ));
		for (uint8_t i = 0; i < ramCount && x.batch.size( ) < x.limit; i++) {
			if (!_storageRef->h5RamRecord(i, epoch, vals)) break;
			if (!telUnsent(c, day, ramOff, i, epoch) || !telPlausible(epoch, x.nowEpoch)) continue;
			if (!takeRecord(x, day, ramOff, i, epoch, vals, map)) break;
		}
		feedWdt( );
	}
	return true;
}

bool TelemetryManager::collectBatch(std::vector<BinaryHistoryRecord>& batch, uint32_t& fromCursor) {
 LogManager::TraceScope _tC(0, MOD_TEL_COLLECT);
 SystemConfig &cfg = _storageRef->getConfig( );
 TelCursorState& c = _storageRef->telCursor( );
 const TelCursorState before = c;
 _nRuns = 0;

 const uint32_t nowEpoch = (uint32_t)time(nullptr);
 const uint32_t lastRecorded = _storageRef->getLastRecordedTimestamp( );

 /* A record is sent by WHERE it was written, not by its stamp (A-04,
  * TelemetryPosition.h), so a clock that runs ahead and comes back no longer
  * leaves a cursor that rejects every new record. What is left of that
  * failure is the epoch migrated from the old 4-byte cursor, which still
  * governs the files that existed when it was read: one ahead of the data — a
  * manual future time set, a clock that drifted ahead and was corrected by
  * NTP — is dropped as before, and the files go again from the 30-day floor. */
 if (c.legacyDay) {
 const bool aheadNow  = (nowEpoch > 1600000000UL) && (c.legacyEpoch > nowEpoch + 3600UL);
 const bool aheadData = (lastRecorded > 1600000000UL) && (c.legacyEpoch > lastRecorded + 3600UL);
 if (aheadNow || aheadData) {
 LOG_CODE(LOG_WARN, "TEL", SYS_OK, 0,
 TRL("Telemetry cursor ahead of data — reset to 0"));
 telCursorReset(c, 0);
 }
 }

 /* Never sent, or reset: start 30 days behind the newest record, the floor
  * the epoch cursor fell back to at zero. */
 if (!c.floorDay && lastRecorded > 86400UL * 30) {
 c.floorDay = StorageManager::historyDayOf(lastRecorded - 86400UL * 30);
 }

 /* The newest epoch delivered before this batch — what deliveredCursor( )
  * builds on, after buildPayload has had its say. */
 fromCursor = c.lastEpoch;

 std::vector<String> files;
 listDayFiles(files);

 /* Two ceilings, and the lower one wins. safeBatchLimit is physics — what the
  * heap can hold right now, given the transport. _batchAuto is the controller
  * (§3.2 of the cadence plan): it only ever asks for less, and it exists for
  * the one thing the heap cannot predict — a server that chokes on a payload
  * this device could perfectly well have built. Measured on the bench, both
  * transports took the heap ceiling with zero failures, so on a healthy link
  * the controller sits at the top and this line is a no-op. */
 const uint8_t configured = (cfg.telBatchSize > 0) ? cfg.telBatchSize : 10;
 /* The controller starts AT the configured maximum. Anything lower would make
  * the operator's setting a target to be re-earned after every boot; the AIMD
  * exists to back away from a server that chokes, not to ration by default. */
 if (_batchAuto == 0) _batchAuto = configured;
 uint8_t limit = safeBatchLimit(configured);
 if (_batchAuto < limit) limit = _batchAuto;
 if (limit < 1) limit = 1;

 /* The block still open in RAM belongs to the file it will be sealed into,
  * and goes after that file's sealed blocks. */
 uint32_t ramDay = 0, ramOff = 0;
 if (_storageRef->h5RamCount( ) > 0
     && !_storageRef->h5SealPosition(_storageRef->h5RamT0( ), true, ramDay, ramOff)) {
 ramDay = 0;
 }

 /* Day files in order, the open block in its day's place. A file before the
  * floor was fully sent and is not opened again. */
 TelCollect x{ batch, c, limit, nowEpoch, 0, false };
 size_t fi = 0;
 bool ramLeft = ramDay != 0;
 while (batch.size( ) < limit && !x.runsFull) {
 uint32_t fileDay = 0;
 while (fi < files.size( ) && !(fileDay = StorageManager::historyDayOfName(files[fi]))) fi++;
 uint32_t day;
 String path;
 bool withRam = false;
 if (fileDay && (!ramLeft || fileDay <= ramDay)) {
 day = fileDay;
 path = String(DIR_HISTORY) + "/" + files[fi++];
 withRam = ramLeft && fileDay == ramDay;
 } else if (ramLeft) {
 day = ramDay;
 withRam = true;
 } else {
 break;
 }
 if (withRam) ramLeft = false;
 if (telFileDone(c, day)) continue;

 const size_t mark = batch.size( );
 const uint8_t runMark = _nRuns;
 if (!collectDay(x, day, path, withRam, ramOff)) {
 /* Something else sits where the records the slot counted were — the
  * open block lost to a power cut after some of it was sent, a seal that
  * failed, a day file deleted or put back by a restore. The position
  * cannot say which records went, so the day goes again from its start:
  * duplicates, which the server keys away by stamp, never a gap. */
 LOG_CODE(LOG_WARN, "TEL", TEL_CURSOR_RESENT, (int)(day % 10000u), "");
 telForgetDay(c, day);
 batch.erase(batch.begin( ) + mark, batch.end( ));
 _nRuns = runMark;
 x.runsFull = false;
 collectDay(x, day, path, withRam, ramOff);
 }
 feedWdt( );
 }

 /* The floor follows today, and waits for the first file that still had
  * something to send. The days are the day files' own: local dates, off the
  * same clock. Counted from today's noon, so the hour a DST change adds or
  * takes never lands the window on the wrong date. */
 TelToday today = { 0, 0, 0, _storageRef->clockTrusted( ) };
 if (nowEpoch >= HIST_EPOCH_MIN) {
 const time_t tt = (time_t)nowEpoch;
 struct tm lt;
 localtime_r(&tt, &lt);
 const uint32_t noon = nowEpoch - (uint32_t)(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) + 43200UL;
 today.day = StorageManager::historyDayOf(nowEpoch);
 today.window = StorageManager::historyDayOf(noon - 86400UL * TEL_POS_KEEP_DAYS);
 today.tomorrow = StorageManager::historyDayOf(noon + 86400UL);
 }
 telAdvanceFloor(c, today, x.firstUnsent);

 if (memcmp(&before, &c, sizeof(c)) != 0) _storageRef->telCursorTouched( );
 return !batch.empty( );
}


/* =========================================================================== */
/* HTTP TRANSPORT */
/* =========================================================================== */
/** @brief Upload a batch via HTTP POST with configurable auth headers.
 * Bound TLS handshake (setTLSConnectTimeout) to avoid BearSSL hang
 * when server drops mid-handshake. Do NOT touch setReuse() — it broke
 * shared HTTPClient/WiFiClientSecure internal state and caused bootloop
 * on large telemetries post-boot (hardware validated).
 * Recreate _httpSecurePtr only on explicit socket/TLS error, to avoid
 * losing TCP keep-alive on consecutive successes. */
bool TelemetryManager::attemptHttpUpload(String& payload, uint32_t newCursor) {
 LogManager::TraceScope _tS(0, MOD_TEL_SEND);
 SystemConfig &cfg = _storageRef->getConfig( );

 feedWdt( );

#if TEL_TLS_KEEPALIVE_EXPERIMENT
 /* A local HTTPClient could never reuse the kept session — see _httpKeepPtr.
  * The plain path keeps its per-call object: there is no handshake to save. */
 HTTPClient httpLocal;
 if (cfg.telEncryption && !_httpKeepPtr) _httpKeepPtr = new (std::nothrow) HTTPClient( );
 HTTPClient& http = (cfg.telEncryption && _httpKeepPtr) ? *_httpKeepPtr : httpLocal;
#else
 HTTPClient http;
#endif
 WiFiClient client;

 String protocol = cfg.telEncryption ? "https://" : "http://";
 String url = protocol + String(cfg.telServer) + ":" + String(cfg.telPort) + String(cfg.telPath);
 bool connected = false;

 if (cfg.telEncryption) {
#if !SIMUT_TEL_TLS
 /* Refused before any socket opens; begin( ) said why. */
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, TEL_CTX_NO_TLS, "no TLS client in this image");
 MetricsManager::instance( ).data( ).telFailed++;
 return false;
#else
 if (!_httpSecurePtr) {
 _httpSecurePtr = new WiFiClientSecure( );
 if (!_httpSecurePtr) {
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, 0, TRL("OOM: WiFiClientSecure"));
 return false;
 }
 _httpSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
 }
 _httpSecureLastUse = millis( );

 /* Bound the TLS handshake. Static method, affects all subsequent
  * WiFiClientSecure creation. Upstream this call is nearly decorative — it
  * bounds one _run_until iteration, never the handshake — so it only really
  * holds because of the framework patch. See NET_TLS_HANDSHAKE_MS. */
 WiFiClientSecure::setTLSConnectTimeout(NET_TLS_HANDSHAKE_MS);

 /* BearSSL defaults to a 16 KB receive buffer ("minimum safe", set from
  * _clear()), and it must get that as ONE contiguous block. Measured at the
  * moment of the attempt on this device: 31,900 B free but only 11,370 B
  * contiguous — the default cannot fit, and freeing more memory does not
  * help while the heap stays this fragmented.
  *
  * 4096 is the largest RFC 6066 max_fragment_length below the default, so
  * the request drops to ~4.4 KB and fits with room to spare. The server has
  * to honour the extension; if it does not and sends a larger record, the
  * connection fails instead of succeeding — a clean failure, not a hang.
  *
  * Do NOT drop this in favour of the boot-time pre-allocation in begin(),
  * whose comment has warned about this exact 16 KB contiguous block since
  * v1.0.0. That mitigation does not reach the problem and was measured
  * failing: pre-allocating the WiFiClientSecure OBJECT reserves nothing,
  * because BearSSL allocates the iobuf inside _connectSSL and frees it in
  * _freeSSL — once per connection, whatever the heap looks like by then.
  * Removing this line and booting with encryption already enabled still
  * watchdog-reboots at the first send. */
 _httpSecurePtr->setBufferSizes(4096, 512);

 if (_hasCert) _httpSecurePtr->setCACert(_cachedCert.c_str( ));
 else _httpSecurePtr->setInsecure( );

 connected = http.begin(*_httpSecurePtr, url);
#endif
 } else {
 connected = http.begin(client, url);
 }

 bool success = false;
 int code = 0;

 if (connected) {
 /* v26: the custom mode sends the operator's Content-Type (it used to fall
  * into "text/plain", which a JSON endpoint refuses). Checked again here, at
  * the sink — see TelContentType.h for why the commit check is not enough. */
 char ctBuf[TEL_CT_MAX + 1];
 http.addHeader("Content-Type",
                telContentTypeFor(cfg.telMode, cfg.telCustom.telCustomContentType,
                                  sizeof(cfg.telCustom.telCustomContentType),
                                  ctBuf, sizeof(ctBuf)));

 String tokenStr = String(cfg.telApiKey);
 tokenStr.trim( );
 if (tokenStr.length( ) > 0) {
 int colonIdx = tokenStr.indexOf(':');
 if (colonIdx > 0) {
 String hName = tokenStr.substring(0, colonIdx);
 String hVal = tokenStr.substring(colonIdx + 1);
 hName.trim( ); hVal.trim( );
 http.addHeader(hName, hVal);
 } else {
 http.addHeader("Authorization", "Bearer " + tokenStr);
 }
 }
 addIdentityHeaders(http, _storageRef);

 http.setTimeout(NET_SOCKET_TIMEOUT_MS);
 feedWdt( );

 uint32_t postStart = millis( );
 {
 code = http.POST(payload);
 }
 uint32_t postLatency = millis( ) - postStart;
 watchdog_update( );

 if (code > 0) {
 if (code >= 200 && code < 300) {
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_SENT, code,
 "HTTP OK: " + String(payload.length( )) + " bytes, code " + String(code));
 markDelivered(newCursor);
 success = true;
 auto& m = MetricsManager::instance( ).data( );
 m.telSent++;
 m.telTotalBytes += (uint32_t)payload.length( );
 m.telLastLatencyMs = postLatency;
 /* 0.7 × prev + 0.3 × observed (alpha=0.3) */
 _smoothedLatencyMs = (_smoothedLatencyMs == 0)
 ? postLatency
 : (_smoothedLatencyMs * 7 + (uint32_t)postLatency * 3) / 10;
 } else {
 /* A reply that arrived is not a delivery. This branch used to fall
  * through the same INFO line as success — a server answering 500 to
  * every batch logged "HTTP OK ... code 500" and left both telSent and
  * telFailed untouched, so the dashboard read "Falhas: 0" while nothing
  * was getting through. The cursor was already held back correctly; what
  * was missing was saying so. */
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, code,
 "HTTP rejected: " + String(payload.length( )) + " bytes, code " + String(code));
 MetricsManager::instance( ).data( ).telFailed++;
 }
 } else {
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, code, String(TRL("HTTP error: ")) + http.errorToString(code));
 MetricsManager::instance( ).data( ).telFailed++;
 }

 /* Close the socket before end( ).
  *
  * Everything this cycle needs is the status code, already read. Leaving the
  * connection open hands it to HTTPClient::disconnect( ), which drains
  * whatever the peer is still sending so the socket stays reusable — and
  * against a peer that never stops sending, that path still reaches the
  * 8.388 s watchdog even with the framework deadline in place.
  *
  * Measured, A/B, same servers and same windows:
  *
  *   with this stop( )     huge1mb 0 reboots, drip 0 reboots
  *   without this stop( )  huge1mb 0 reboots, drip 3 reboots + [FTL]
  *
  * It was removed once, on the theory that closing without reading was what
  * exhausted the lwIP pbuf pool. That theory was wrong: the pool is exhausted
  * in BOTH builds (D14 — a separate defect the watchdog reboots used to hide),
  * and removing the stop( ) only brought the drip kill back. Put it back.
  */
#if SIMUT_TEL_TLS
#if TEL_TLS_KEEPALIVE_EXPERIMENT
 /* Experiment: a clean 2xx keeps the session for the next batch. HTTPClient's
  * end( ) has already drained any unread body under its own deadline and
  * cleared _canReuse if the server said Connection: close, so the socket is
  * only left open when both sides agreed to it. Anything but success closes,
  * exactly as before. */
 if (cfg.telEncryption && _httpSecurePtr && !success) _httpSecurePtr->stop( );
#else
 if (cfg.telEncryption) { if (_httpSecurePtr) _httpSecurePtr->stop( ); }
#endif
 else
#endif
 client.stop( );

 http.end( );
 }

 return success;
}


#if SIMUT_TEL_MQTT
String TelemetryManager::buildMqttClientId( ) {
 SystemConfig &cfg = _storageRef->getConfig( );
 String cid = String(cfg.mqttClientId);
 cid.trim( );
 if (cid.length( ) > 0) return cid;


 String mac = _netRef->getMacAddress( );
 mac.replace(":", "");
 if (mac.length( ) >= 6) {
 return "simut_" + mac.substring(mac.length( ) - 6);
 }
 return "simut_device";
}

String TelemetryManager::mqttDataTopic( ) {
 SystemConfig &cfg = _storageRef->getConfig( );
 String t = String(cfg.mqttTopic);
 t.trim( );
 if (t.length( ) == 0) t = "simut/data";
 return t;
}

String TelemetryManager::mqttStatusTopic( ) {
 String base = mqttDataTopic( );
 int lastSlash = base.lastIndexOf('/');
 if (lastSlash > 0) return base.substring(0, lastSlash) + "/status";
 return base + "/status";
}

/**
 * @brief Settle HA discovery against the persisted state.
 *
 * want = toggle && JSON mode; have = FLAG_HA_PUBLISHED. Publishes the
 * retained configs when wanted (always on @p forceRepublish — the connect
 * path uses it so broker restarts and sensor-table edits are covered),
 * publishes the empty payloads that remove the entities when no longer
 * wanted. The bit only moves when every message was accepted, so a failed
 * burst is retried on the next reconcile instead of being lost.
 */
void TelemetryManager::haDiscoveryReconcile(bool forceRepublish) {
 SystemConfig &cfg = _storageRef->getConfig( );
 bool want = _storageRef->isHaDiscoveryEnabled( ) && cfg.telMode == TEL_MODE_JSON;
 bool have = _storageRef->wasHaDiscoveryPublished( );

 if (want) {
 if (forceRepublish || !have) {
 if (publishHaDiscovery(true)) _storageRef->markHaDiscoveryPublished(true);
 }
 } else if (have) {
 if (publishHaDiscovery(false)) _storageRef->markHaDiscoveryPublished(false);
 }
}

/**
 * @brief Publish (or clear) the Home Assistant discovery config messages.
 *
 * One retained message per measurement the JSON formatter emits — the entity
 * list must mirror formatLineJsonBuf, because each config message is a
 * promise that `value_json['<key>']` exists in the state topic: per active
 * slot a temperature and (channel mask allowing) a humidity entity, plus one
 * pressure entity attributed exactly like the formatter attributes the `p`
 * key. Lux stays out for the same reason: BinaryHistoryRecord does not carry
 * it, so telemetry never publishes it.
 *
 * @param enable false publishes empty retained payloads to the same topics,
 * which is how HA is told to remove the entities.
 * @return true if every message was accepted by the client.
 */
bool TelemetryManager::publishHaDiscovery(bool enable) {
 if (!_mqttClient.connected( )) return false;
 SystemConfig &cfg = _storageRef->getConfig( );

 char nodeId[32];
 HaDiscovery::sanitizeId(buildMqttClientId( ).c_str( ), nodeId, sizeof(nodeId));

 String stateT = mqttDataTopic( );
 String availT = mqttStatusTopic( );

 /* configuration_url: the device's own web UI. Port comes from the same
  * overlay WebManager_Core reads at boot. */
 char cu[48] = "";
 if (enable) {
 const WebConfigData* w = reinterpret_cast<const WebConfigData*>(
 cfg.reserved + WEB_CONFIG_OFFSET);
 uint16_t port = (w->port == 0) ? WEB_DEFAULT_PORT : w->port;
 String ip = _netRef->getIpAddress( );
 if (port == 80) snprintf(cu, sizeof(cu), "http://%s", ip.c_str( ));
 else snprintf(cu, sizeof(cu), "http://%s:%u", ip.c_str( ), port);
 }

 HaDiscovery::EntityCtx ctx;
 ctx.nodeId = nodeId;
 ctx.stateTopic = stateT.c_str( );
 ctx.availTopic = availT.c_str( );
 ctx.deviceName = cfg.deviceName;
 ctx.swVersion = SIMUT_VERSION;
 ctx.configUrl = cu;

 int published = 0, skipped = 0;
 bool allOk = true;

 auto pubEntity = [&](const char* key, const char* slotLabel, uint8_t ch) {
 if (!HaDiscovery::keyTemplatable(key)) { skipped++; return; }
 char objectId[24];
 HaDiscovery::sanitizeId(key, objectId, sizeof(objectId));

 char topic[96];
 int tl = HaDiscovery::configTopic(topic, sizeof(topic), nodeId, objectId);
 if (tl <= 0 || tl >= (int)sizeof(topic)) { skipped++; return; }

 feedWdt( );
 bool ok;
 if (!enable) {
 /* Empty retained payload = delete the retained config → HA removes
  * the entity. */
 ok = _mqttClient.publish(topic, (const uint8_t*)"", 0, true);
 } else {
 const ChannelInfo& ci = channelInfo(ch);
 char name[64];
 snprintf(name, sizeof(name), "%s %s", slotLabel, ci.name);
 char payload[896];
 int n = HaDiscovery::entityConfigJson(payload, sizeof(payload), ctx,
 objectId, key, name, HaDiscovery::deviceClass(ch), ci.display.unit,
 (int8_t)ci.display.decimals);
 if (n <= 0 || n >= (int)sizeof(payload)) { skipped++; return; }
 ok = _mqttClient.publish(topic, payload, true);
 }
 if (ok) published++; else allOk = false;
 _mqttClient.loop( );
 };

 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!cfg.sensors[i].active) continue;
 SensorType type = (SensorType)cfg.sensors[i].sensorType;
 const char* hwid = cfg.sensors[i].hwId;

 char key[20];
 const char* label = cfg.sensors[i].friendlyName[0] ? cfg.sensors[i].friendlyName
                   : (hwid[0] ? hwid : "Slot");

 if (sensorHasChannel(type, CH_TEMP)) {
 if (hwid[0]) snprintf(key, sizeof(key), "t%s", hwid);
 else snprintf(key, sizeof(key), "t%d", i);
 pubEntity(key, label, CH_TEMP);
 }
 if (sensorHasChannel(type, CH_HUM)) {
 if (hwid[0]) snprintf(key, sizeof(key), "u%s", hwid);
 else snprintf(key, sizeof(key), "u%d", i);
 pubEntity(key, label, CH_HUM);
 }
 }

 /* Pressure: single entity, attributed like formatLineJsonBuf attributes
  * the `p` key (first active pressure-capable slot with a hwId; "p"
  * otherwise, yielding the same "pp" fallback key the formatter emits). */
 bool anyPress = false;
 const char* pHwid = "p";
 const char* pLabel = "Slot";
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!cfg.sensors[i].active) continue;
 if (!sensorHasChannel((SensorType)cfg.sensors[i].sensorType, CH_PRESS)) continue;
 anyPress = true;
 if (cfg.sensors[i].hwId[0]) {
 pHwid = cfg.sensors[i].hwId;
 pLabel = cfg.sensors[i].friendlyName[0] ? cfg.sensors[i].friendlyName
        : cfg.sensors[i].hwId;
 break;
 }
 }
 if (anyPress) {
 char key[20];
 snprintf(key, sizeof(key), "p%s", pHwid);
 pubEntity(key, pLabel, CH_PRESS);
 }

 LOG_CODE(LOG_INFO, "TEL", TEL_HA_DISCOVERY, published,
 String(enable ? "HA discovery published " : "HA discovery cleared ")
 + String(published) + (skipped ? " (skipped " + String(skipped) + ")" : ""));
 return allOk;
}

/**
 * @brief Ensure MQTT broker connection with LWT (Last Will & Testament).
 * Rate-limited to one reconnection attempt every 5 seconds.
 */
bool TelemetryManager::mqttEnsureConnected( ) {
 if (_mqttClient.connected( )) return true;


 uint32_t now = millis( );
 if (now - _lastMqttReconnect < 5000) return false;
 _lastMqttReconnect = now;

 SystemConfig &cfg = _storageRef->getConfig( );
 String clientId = buildMqttClientId( );
 String devName = String(cfg.deviceName);

 /* Was derived from the raw cfg.mqttTopic here while the data publish used
  * a trimmed copy with a "simut/data" fallback — so a blank topic put the
  * will on the degenerate "/status". Both now come from the same resolver. */
 String willTopicFull = mqttStatusTopic( );

 String willPayload = "{\"device\":\"" + devName + "\",\"status\":\"offline\"}";

 LOG_CODE(LOG_INFO, "TEL", TEL_MQTT_CONNECTING, 0, clientId);
 feedWdt( );

 bool connected = false;
 String user = String(cfg.mqttUser);
 String pass = String(cfg.mqttPass);
 user.trim( );
 pass.trim( );

 {
 if (user.length( ) > 0) {
 connected = _mqttClient.connect(
 clientId.c_str( ),
 user.c_str( ),
 pass.c_str( ),
 willTopicFull.c_str( ),
 0,
 true,
 willPayload.c_str( )
 );
 } else {
 connected = _mqttClient.connect(
 clientId.c_str( ),
 nullptr,
 nullptr,
 willTopicFull.c_str( ),
 0,
 true,
 willPayload.c_str( )
 );
 }
 }

 watchdog_update( );

 if (connected) {
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_CONN, 0, String(TRL("MQTT connected to ")) + cfg.telServer);
 MetricsManager::instance( ).data( ).mqttReconnects++;


 String onlinePayload = "{\"device\":\"" + devName + "\",\"status\":\"online\",\"ip\":\"" + _netRef->getIpAddress( ) + "\"}";
 _mqttClient.publish(willTopicFull.c_str( ), onlinePayload.c_str( ), true);

 /* HA discovery rides every (re)connect: republish covers broker restarts
  * and sensor-table edits (commit_all reboots into exactly this path),
  * and a fresh OFF-with-published-bit state clears the retained configs. */
 haDiscoveryReconcile(true);

 /* Linha de alarmes: (re)assina o tópico de ACK. PubSubClient re-assina
  * sozinho em reconexões, mas a assinatura explícita aqui cobre o broker
  * que esquece sessões (clean session) — idempotente. */
 mqttSubscribeAlarmAck( );

 return true;
 } else {
 int state = _mqttClient.state( );
 String reason;
 switch (state) {
 case -4: reason = "Connection timeout"; break;
 case -3: reason = "Connection lost"; break;
 case -2: reason = "Connect failed"; break;
 case -1: reason = "Disconnected"; break;
 case 1: reason = "Bad protocol"; break;
 case 2: reason = "Client ID rejected"; break;
 case 3: reason = "Server unavailable"; break;
 case 4: reason = "Bad credentials"; break;
 case 5: reason = "Not authorized"; break;
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

 /* Metrics are recorded here for the same reason attemptHttpUpload records
  * them: without it this whole transport is invisible. Measured on the bench
  * — 386 publishes carrying 384 records, and telSent / telFailed / telBytes /
  * telLastLatencyMs all still read zero, so the dashboard and `show metrics`
  * said nothing had ever been sent. The functional half of that is worse than
  * the cosmetic one: update( ) derives its effective interval from
  * _smoothedLatencyMs, which only the HTTP path was feeding, so the adaptive
  * pacing never engaged on MQTT at all. */
 const uint32_t pubStart = millis( );
 auto& m = MetricsManager::instance( ).data( );

 if (!mqttEnsureConnected( )) { m.telFailed++; return false; }

 SystemConfig &cfg = _storageRef->getConfig( );
 String topic = mqttDataTopic( );


 bool success = false;
 uint32_t sentBytes = 0;

 if (batch.size( ) <= 5) {
 /* Small batch: publish each line individually */
 int published = 0;
 for (size_t i = 0; i < batch.size( ); i++) {
 feedWdt( );

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
 topic.c_str( ),
 linePayload.c_str( ),
 cfg.mqttRetain
 );

 if (ok) { published++; sentBytes += (uint32_t)linePayload.length( ); }
 else break;

 _mqttClient.loop( );
 }

 if (published > 0) {
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_PUB, published,
 "MQTT published " + String(published) + "/" + String(batch.size( )) + " items to " + topic);
 }

 if (published == (int)batch.size( )) {
 /* G4 (sem perda silenciosa): publish() só confirma a ESCRITA no socket.
  * Se a conexão morreu logo depois (broker drop-on-publish), os registros
  * podem nunca ter chegado e QoS 0 não tem ACK — não avança o cursor; o
  * próximo ciclo reenvia a partir daqui (duplicatas são o preço do QoS 0,
  * perda não). A FIN/RST chega em ~ms numa LAN: espera limitada a ~60 ms
  * antes de confirmar; além disso a decisão volta à semântica QoS-0. */
 bool connAlive = true;
 uint32_t finWait = millis( );
 while (millis( ) - finWait < 60) {
 _mqttClient.loop( );
 if (!_mqttClient.connected( )) { connAlive = false; break; }
 delay(5);
 }
 if (connAlive) {
 markDelivered(newCursor);
 success = true;
 } else {
 LOG_CODE(LOG_WARN, "TEL", SYS_TEL_MQTT_DISC, _mqttClient.state( ),
 "MQTT connection died during publish — cursor not advanced");
 success = false;
 }
 } else if (published > 0) {
 markDeliveredPrefix(batch, (size_t)published);
 success = false;
 }
 } else {
 /* Large batch: uses payload pre-built by caller */
 feedWdt( );

 /* PubSubClient refuses any packet that does not fit its buffer, and the
  * buffer cannot be grown past MQTT_BUFFER_CEILING. Above that the publish
  * fails DETERMINISTICALLY — so the retry fails identically, the backoff
  * walks up to its 300 s ceiling, and telemetry stops for good without a
  * reboot or a message that explains it.
  *
  * It is reachable straight from the config page. Measured on the bench with
  * a long custom line template (235 B/record): at batch 5 the broker got 296
  * messages in 70 s, at batch 50 it got 11 — and not one message larger than
  * 235 B ever arrived, because the ~11.75 KB combined payload was never sent.
  * The eleven that did were small residual batches falling through the
  * per-record path below.
  *
  * So when the combined payload will not fit, publish record by record
  * instead of failing. That path already exists, is already used for small
  * batches, and was measured working at exactly this record size. The cursor
  * follows what was actually published, so a partial run costs nothing. */
 const size_t needed = payload.length( ) + topic.length( ) + MQTT_PACKET_OVERHEAD;
 if (needed > MQTT_BUFFER_CEILING) {
 LOG_CODE(LOG_WARN, "TEL", SYS_TEL_QUEUE, (int)batch.size( ),
 "MQTT payload " + String(payload.length( )) +
 " B over buffer ceiling — publishing per record");
 int published = 0;
 for (size_t i = 0; i < batch.size( ); i++) {
 feedWdt( );
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
 if (!_mqttClient.publish(topic.c_str( ), linePayload.c_str( ), cfg.mqttRetain)) break;
 published++;
 sentBytes += (uint32_t)linePayload.length( );
 _mqttClient.loop( );
 }
 if (published > 0) {
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_PUB, published,
 "MQTT split publish " + String(published) + "/" + String(batch.size( )));
 markDeliveredPrefix(batch, (size_t)published);
 success = (published == (int)batch.size( ));
 }
 } else {

 if (payload.length( ) > _mqttClient.getBufferSize( )) {
 _mqttClient.setBufferSize((uint16_t)min((size_t)MQTT_BUFFER_CEILING,
                                         payload.length( ) + 64));
 }

 bool ok;
 {
 ok = _mqttClient.publish(
 topic.c_str( ),
 payload.c_str( ),
 cfg.mqttRetain
 );
 }

 if (ok) {
 /* G4 (sem perda silenciosa): mesmo pós-check do caminho de lotes pequenos
  * — janela curta para a FIN/RST do broker chegar; se a conexão morreu na
  * sequência, não avança o cursor e o próximo ciclo reenvia. */
 bool connAlive = true;
 uint32_t finWait = millis( );
 while (millis( ) - finWait < 60) {
 _mqttClient.loop( );
 if (!_mqttClient.connected( )) { connAlive = false; break; }
 delay(5);
 }
 if (connAlive) {
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_PUB, batch.size( ),
 "MQTT batch OK: " + String(batch.size( )) + " items (" + String(payload.length( )) + " bytes)");
 markDelivered(newCursor);
 sentBytes = (uint32_t)payload.length( );
 success = true;
 } else {
 LOG_CODE(LOG_WARN, "TEL", SYS_TEL_MQTT_DISC, _mqttClient.state( ),
 "MQTT connection died during publish — cursor not advanced");
 }
 } else {
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, _mqttClient.state( ),
 "MQTT publish failed (payload " + String(payload.length( )) + " bytes)");
 }
 }
 }

 /* Same bookkeeping attemptHttpUpload does, so the two transports report
  * through the same counters and the dashboard means the same thing whichever
  * one is configured. */
 const uint32_t pubLatency = millis( ) - pubStart;
 if (success) {
 m.telSent++;
 m.telTotalBytes += sentBytes;
 m.telLastLatencyMs = pubLatency;
 _smoothedLatencyMs = (_smoothedLatencyMs == 0)
 ? pubLatency
 : (_smoothedLatencyMs * 7 + pubLatency * 3) / 10;
 } else {
 m.telFailed++;
 }

 return success;
}

bool TelemetryManager::isMqttConnected( ) {
 if (!_mqttInitialized) return false;
 return _mqttClient.connected( );
}
#else
/* SIMUT_TEL_MQTT=0: the doors the rest of the manager still knocks on. The data
 * send refuses with the same context begin( ) logged, and counts the failure so
 * the dashboard does not read "0 failures" while nothing is sent. */
bool TelemetryManager::isMqttConnected( ) { return false; }

bool TelemetryManager::attemptMqttPublish(String& payload, std::vector<BinaryHistoryRecord>& batch,
                                          uint32_t newCursor) {
 (void)payload; (void)batch; (void)newCursor;
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, TEL_CTX_NO_MQTT, "no MQTT client in this image");
 MetricsManager::instance( ).data( ).telFailed++;
 return false;
}
#endif


void TelemetryManager::resetBackoff( ) {
 _currentBackoff = BACKOFF_MIN_MS;
 _consecutiveFails = 0;
 _backoffUntil = 0;
}

uint32_t TelemetryManager::getBackoffRemainingMs( ) const {
 const uint32_t now = millis( );
 return (_backoffUntil > now) ? (_backoffUntil - now) : 0u;
}

void TelemetryManager::escalateBackoff( ) {
 _consecutiveFails++;
 MetricsManager::instance( ).data( ).telRetries++;
 _backoffUntil = millis( ) + jitter(_currentBackoff);

 if (_consecutiveFails <= BACKOFF_MAX_STREAK) {
 LOG_CODE(LOG_WARN, "TEL", SYS_TEL_RETRY, _consecutiveFails,
 String(TRL("Upload failed (#")) + _consecutiveFails +
 TRL("). Retry in ") + (_currentBackoff / 1000) + "s");
 } else if (_consecutiveFails == BACKOFF_MAX_STREAK + 1) {
 LOG_CODE(LOG_WARN, "TEL", TEL_BACKOFF_SUPPRESSED, 0, "");
 _lastSuppressedLog = millis( );
 } else if (timeSince(_lastSuppressedLog, 3600000)) {
 /* Heartbeat once per hour after suppression */
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, _consecutiveFails,
 "Still failing (#" + String(_consecutiveFails) + ")");
 _lastSuppressedLog = millis( );
 }

 _currentBackoff = min(_currentBackoff * 2, BACKOFF_MAX_MS);
}

uint32_t TelemetryManager::jitter(uint32_t base) {
 uint32_t quarter = base / 4;
 return base - quarter + (random(0, quarter * 2));
}

/**
 * @brief Releases idle TLS resources to recover heap.
 *
 * TLS clients (WiFiClientSecure) are NOT released.
 * The ~16KB is a permanent cost of using encryption.
 * Releasing and reallocating causes heap fragmentation that leads to
 * hard faults when the 16KB contiguous block no longer exists.
 *
 * The certificate also stays in RAM while there is a TLS client.
 */
void TelemetryManager::releaseIdleResources( ) {
}

bool TelemetryManager::forceSync( ) {
 resetBackoff( );
 /* "Send now" means the drain starts now and update( ) carries it on at the
  * server's pace; without this the one batch below would go out and the next
  * would wait a whole period. */
 _drainActive = true;
 _gapMs = 0;
 _nextSendAt = millis( );

 bool expected = false;
 if (!__atomic_compare_exchange_n(&_isSending, &expected, true,
 false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return false;
 if (!_netRef->isNetworkHealthy( )) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return false; }
 if (!_storageRef->lockHeavyTask( )) { __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE); return false; }

 /* RAII: extends WDT context 120s, context-aware same as update(). */
 LogManager::WdtWindow _wdt(120000);

 std::vector<BinaryHistoryRecord> batch;
 uint32_t newCursor = 0;

 if (!collectBatch(batch, newCursor)) {
 __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
 _storageRef->unlockHeavyTask( );
 return true;
 }

 SystemConfig &cfg = _storageRef->getConfig( );

 /* Builds payload and frees batch to reduce RAM peak */
 String payload = buildPayload(batch);
 if (_dumpPayloadNext) {
 _dumpPayload(payload.c_str( ), payload.length( ), "SYNC");
 _dumpPayloadNext = false;
 }

 /* Same as update( ): the cursor follows the payload, not the collection. */
 newCursor = deliveredCursor(batch, newCursor, (uint32_t)time(nullptr));
 trimRuns(batch, batch.size( ));

 bool ok = false;
 if (payload.length( ) == 0) {
 /* nothing well formed to send — see buildPayload */
 } else if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
 ok = attemptMqttPublish(payload, batch, newCursor);
 } else {
 batch.clear( );
 batch.shrink_to_fit( );
 ok = attemptHttpUpload(payload, newCursor);
 }

 __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
 _storageRef->unlockHeavyTask( );
 _pendingDirty = true; /* Recalibrate after send */

 if (!ok) escalateBackoff( );
 return ok;
 /* WdtWindow destructor auto-restores WDT */
}


/* =========================================================================== */
/* PAYLOAD BUILDERS */
/* =========================================================================== */
/**
 * @brief Build the upload payload using fixed char buffers — zero heap fragmentation.
 *
 * All construction is done with snprintf/strlcat in stack buffers.
 * The only heap object is the String `s` which is reserved once.
 * No temporary String is created during the loop → safe for 50+ records.
 */
/* Append two pieces — a separator and a line, or a line and its newline — only
 * if the string can take both AND still has `tail` bytes left for whatever must
 * close the payload after them. Either both go in or neither does; see the
 * measurement in buildPayload( ) for what happened when nothing checked. */
static bool appendWhole(String& s, const char* a, size_t alen,
                        const char* b, size_t blen, size_t tail) {
 if (!s.reserve(s.length( ) + alen + blen + tail)) return false;
 if (alen) s.concat(a, alen);
 if (blen) s.concat(b, blen);
 return true;
}

String TelemetryManager::buildPayload(std::vector<BinaryHistoryRecord>& batch) {
 LogManager::TraceScope _tB(0, MOD_TEL_BUILD);
 SystemConfig &cfg = _storageRef->getConfig( );

 /* Estimate size: JSON ~300 bytes/record with 12 sensors.
  * CSV needs a bigger fixed part: its header names all 34 columns of the
  * row layout (~440 B), which does not fit in the 256 B slack the other
  * modes use and would force the String to reallocate on every batch. */
 size_t perLine = (cfg.telMode == TEL_MODE_CSV) ? 120 : 300;
 size_t fixedPart = (cfg.telMode == TEL_MODE_CSV) ? 640 : 256;
 size_t estimatedSize = batch.size( ) * perLine + fixedPart;

 /* Check heap and reduce batch if needed.
 * Differentiated reserve by TLS — 12K with encryption,
 * 6K without (no BearSSL scratch). shrink_to_fit() forces actual
 * release of vector capacity (resize only changes size, not capacity). */
 uint32_t freeHeap = rp2040.getFreeHeap( );
 const uint32_t SEC_RESERVE = cfg.telEncryption ? 12288 : 6144;
 if (freeHeap < estimatedSize + SEC_RESERVE) {
 size_t safeCount = (freeHeap > SEC_RESERVE) ? (freeHeap - SEC_RESERVE) / perLine : 1;
 if (safeCount < batch.size( )) {
 batch.resize(safeCount);
 batch.shrink_to_fit( ); /* release effective capacity */
 }
 estimatedSize = batch.size( ) * perLine + fixedPart;
 }

 String s;
 s.reserve(estimatedSize);

 /* Every record goes in whole or not at all, and the payload always closes.
  *
  * String::concat( ) does not throw and does not partially append: when the
  * realloc behind it fails it returns false and the string is left as it was,
  * and nothing here used to look. The reserve above asks for 300 B a line —
  * twelve sensors' worth — so with five sensors a 199-record batch wanted
  * 60 KB while the largest free block in M0 was 46 KB; the reserve failed,
  * the string grew one realloc per record, and around 29 KB it could not move
  * any more. From there every record line was dropped while the one-byte
  * commas and the closing bracket still fitted. Measured on the bench
  * 2026-09-23 (Air v2.7.1, M0 drain into a collector that keeps every body):
  * 68 of 69 batches ended in `},,,,,,,,,]` — invalid JSON, about nine records
  * short, and the cursor moved past all of them.
  *
  * So room is reserved BEFORE each record: its separator, its line and the
  * bytes that must still close the payload after it. If that reserve fails,
  * the batch ends there — `batch` is cut to what went in, which is what
  * deliveredCursor( ) reads, and the rest is offered again next batch. */
 size_t kept = batch.size( );
 bool closed = false;

 if (cfg.telMode == TEL_MODE_JSON) {
 /*
 * JSON: builds directly with stack char buffer.
 * formatLineJson writes to lineBuf (512 bytes, stack), and appendWhole( )
 * adds it without creating a temporary String.
 */
 s = "[";
 char lineBuf[512];
 for (size_t i = 0; i < batch.size( ); i++) {
 int len = formatLineJsonBuf(batch[i], cfg, lineBuf, sizeof(lineBuf));
 /* this record, its comma and the closing bracket */
 if (s.length( ) == 0 || !appendWhole(s, ",", i > 0 ? 1 : 0, lineBuf, (size_t)len, 1)) { kept = i; break; }
 if (i % 10 == 9) { watchdog_update( ); yield( ); }
 }
 closed = (s.length( ) > 0) && s.concat(']');
 } else if (cfg.telMode == TEL_MODE_CSV) {
 /* The header has to name every column toCsvLine emits, and toCsvLine emits
  * the fixed layout `epoch;s0..s15;h0..h15;press` — all 16 slots, active or
  * not, then all 16 humidities, then pressure. Naming only the active slots
  * produced a 7-column header over 34-column rows, so anything reading by
  * header index read the wrong values. The rows are the persisted, upload-
  * compatible format and do not change; the header was what lied. */
 s = "timestamp";
 /* The header is not a record: if any piece of it fails to land, there is
  * nothing well formed to send. */
 bool hdrOk = (s.length( ) > 0);
 char hdrBuf[32];
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfg.sensors[i].active && cfg.sensors[i].hwId[0])
 snprintf(hdrBuf, sizeof(hdrBuf), ";s%d_%s", i, cfg.sensors[i].hwId);
 else
 snprintf(hdrBuf, sizeof(hdrBuf), ";s%d", i);
 hdrOk = hdrOk && s.concat(hdrBuf);
 }
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfg.sensors[i].active && cfg.sensors[i].hwId[0])
 snprintf(hdrBuf, sizeof(hdrBuf), ";h%d_%s", i, cfg.sensors[i].hwId);
 else
 snprintf(hdrBuf, sizeof(hdrBuf), ";h%d", i);
 hdrOk = hdrOk && s.concat(hdrBuf);
 }
 hdrOk = hdrOk && s.concat(";press");
 hdrOk = hdrOk && s.concat('\n');
 closed = hdrOk;
 char csvBuf[256];
 for (size_t i = 0; closed && i < batch.size( ); i++) {
 batch[i].toCsvLine(csvBuf, sizeof(csvBuf));
 /* a row closes itself: its newline goes in with it */
 if (!appendWhole(s, csvBuf, strlen(csvBuf), "\n", 1, 0)) { kept = i; break; }
 if (i % 10 == 9) { watchdog_update( ); yield( ); }
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

 String macStr = _netRef->getMacAddress( );
 const char* gt = cfg.telGlobalTemplate;
 const size_t gtLen = strnlen(gt, sizeof(cfg.telGlobalTemplate));

 char lineBuf[1024];
 size_t gi = 0;
 size_t spanStart = 0;
 bool ok = true;   /* every literal span and token landed */

 while (gi < gtLen) {
 if (gt[gi] != '{') { gi++; continue; }

 /* Match known token prefixes at '{'. Advance 1 char on miss
 * so nested braces in JSON templates are handled correctly. */
 const size_t remaining = gtLen - gi;
 size_t tokLen = 0;
 int tokKind = 0; /* 1=DEV, 2=MAC, 3=DATA */
 if (remaining >= 5 && memcmp(gt + gi, "{DEV}", 5) == 0) { tokKind = 1; tokLen = 5; }
 else if (remaining >= 5 && memcmp(gt + gi, "{MAC}", 5) == 0) { tokKind = 2; tokLen = 5; }
 else if (remaining >= 6 && memcmp(gt + gi, "{DATA}", 6) == 0) { tokKind = 3; tokLen = 6; }

 if (tokKind == 0) { gi++; continue; }

 /* Flush literal span before token */
 if (gi > spanStart) ok = ok && s.concat(gt + spanStart, gi - spanStart);

 if (tokKind == 1) {
 ok = ok && s.concat(cfg.deviceName);
 } else if (tokKind == 2) {
 ok = ok && s.concat(macStr);
 } else { /* DATA */
 /* What must still fit after the last record: the rest of the template,
  * plus room for one device name or MAC in it. Not exact — a template with
  * several of them after {DATA} can still run out while closing, and then
  * `ok` goes false and nothing is sent: the records stay queued for the
  * next attempt instead of going out half-formed. */
 const size_t tail = (gtLen - (gi + tokLen)) + sizeof(cfg.deviceName);
 /* `kept`, not the batch: a second {DATA} in the template must not put back
  * records the first one had to leave out. */
 for (size_t i = 0; ok && i < kept; i++) {
 int len = formatLineCustomBuf(batch[i], cfg, lineBuf, sizeof(lineBuf));
 if (len < 0) len = 0;
 if (!appendWhole(s, sep, i > 0 ? sepLen : 0, lineBuf, (size_t)len, tail)) { kept = i; break; }
 if (i % 10 == 9) { watchdog_update( ); yield( ); }
 }
 }

 gi += tokLen;
 spanStart = gi;
 }
 if (gtLen > spanStart) ok = ok && s.concat(gt + spanStart, gtLen - spanStart);
 closed = ok;
 }

 if (kept < batch.size( )) {
 batch.resize(kept);
 batch.shrink_to_fit( );
 }
 /* Nothing well formed to send: an empty string and an empty batch, which
  * the callers read as "could not build" — never as a delivery. */
 if (!closed || batch.empty( )) {
 batch.clear( );
 return String( );
 }
 return s;
}

/**
 * @brief Formats a record as JSON directly in a char buffer — zero heap allocation.
 * @return Number of bytes written to dest (excluding \0).
 */
int TelemetryManager::formatLineJsonBuf(const BinaryHistoryRecord& rec, const SystemConfig& cfg,
	 char* dest, size_t maxLen) {
	 dest[0] = '\0';
	 int pos = snprintf(dest, maxLen, "{\"ts\":%lu", (unsigned long)rec.epoch);

	 char tmp[48];
	 /* V4 universal keys: {prefix}{hwId}. Legacy ambient removed. */
	 for (int i = 0; i < MAX_SENSORS; i++) {
	 if (!cfg.sensors[i].active) continue;
	 const char* hwid = cfg.sensors[i].hwId;
	 if (rec.sensors[i] != HIST_NAN_SENTINEL) {
	 float tv = BinaryHistoryRecord::i16ToFloat(rec.sensors[i]);
	 char tKey[20];
	 if (hwid[0]) snprintf(tKey, sizeof(tKey), "t%s", hwid);
	 else snprintf(tKey, sizeof(tKey), "t%d", i);
	 snprintf(tmp, sizeof(tmp), ",\"%s\":%.2f", tKey, (double)tv);
	 pos += strlcat(dest + pos, tmp, maxLen - pos);
	 }
	 if (rec.humidity[i] != HIST_NAN_SENTINEL) {
	 float hv = BinaryHistoryRecord::i16ToFloat(rec.humidity[i]);
	 /* Build key inline */
	 char key[16];
	 if (hwid[0]) snprintf(key, sizeof(key), "u%s", hwid);
	 else snprintf(key, sizeof(key), "u%d", i);
	 snprintf(tmp, sizeof(tmp), ",\"%s\":%.1f", key, (double)hv);
	 pos += strlcat(dest + pos, tmp, maxLen - pos);
	 }
	 }
	 if (rec.pressure != HIST_NAN_SENTINEL) {
	 float pv = BinaryHistoryRecord::i16ToFloatx10(rec.pressure);
	 /* Attributed to the slot that actually reports pressure. It used to
	  * pick the first humidity-capable slot — "has humidity" stood in for
	  * "is the ambient sensor", so on a board with a DHT22 before the
	  * BMP280 the pressure was published under the DHT22's key. */
	 const char* pHwid = "p";
	 for (int i = 0; i < MAX_SENSORS; i++) {
	 if (cfg.sensors[i].active && cfg.sensors[i].hwId[0] &&
	     sensorHasChannel((SensorType)cfg.sensors[i].sensorType, CH_PRESS)) {
	 pHwid = cfg.sensors[i].hwId; break;
	 }
	 }
	 snprintf(tmp, sizeof(tmp), ",\"p%s\":%.1f", pHwid, (double)pv);
	 pos += strlcat(dest + pos, tmp, maxLen - pos);
	 }
	 if ((size_t)pos < maxLen - 1) { dest[pos] = '}'; dest[pos+1] = '\0'; pos++; }
	 return pos;
	}
/** @brief Wrapper returning String — used by MQTT individual publish. */
String TelemetryManager::formatLineJson(const BinaryHistoryRecord& rec, const SystemConfig& cfg) {
 char buf[512];
 formatLineJsonBuf(rec, cfg, buf, sizeof(buf));
 return String(buf);
}

/**
 * Walk template once, emit into dest. Zero String allocations.
 * Tokens: {TS} {DHT_ID} {t0}..{t15} {u0}..{u15} {p0}..{p15}
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
 String bs = _storageRef->getBoardSerialNumber( );
 strlcpy(boardSerial, bs.c_str( ), sizeof(boardSerial));
 }

 char pressBuf[16] = {0};
 const bool hasPress = (rec.pressure != HIST_NAN_SENTINEL);
 if (hasPress) snprintf(pressBuf, sizeof(pressBuf), "%.1f", BinaryHistoryRecord::i16ToFloatx10(rec.pressure));

 char slotVal[MAX_SENSORS][16];
 bool slotHas[MAX_SENSORS];
 char slotHumVal[MAX_SENSORS][16];
 bool slotHumHas[MAX_SENSORS];
 for (int i = 0; i < MAX_SENSORS; i++) {
 slotHas[i] = (cfg.sensors[i].active && rec.sensors[i] != HIST_NAN_SENTINEL);
 if (slotHas[i]) snprintf(slotVal[i], sizeof(slotVal[i]), "%.2f", BinaryHistoryRecord::i16ToFloat(rec.sensors[i]));
 else slotVal[i][0] = '\0';
 slotHumHas[i] = (cfg.sensors[i].active && rec.humidity[i] != HIST_NAN_SENTINEL);
 if (slotHumHas[i]) snprintf(slotHumVal[i], sizeof(slotHumVal[i]), "%.1f", BinaryHistoryRecord::i16ToFloat(rec.humidity[i]));
 else slotHumVal[i][0] = '\0';
 }

 const char* tpl = cfg.telLineTemplate;
 const size_t tplLen = strnlen(tpl, sizeof(cfg.telLineTemplate));
 size_t di = 0; /* dest cursor */
 size_t ti = 0; /* template cursor */

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
 const char* val = nullptr; /* resolved value (NULL = absent) */
 const char* hwid = nullptr; /* hwid for compound key rewrite */
 char compKey[8] = {0};
 size_t compKeyLen = 0;
 size_t tokenChars = 0; /* total chars to advance in template */
 bool tokenValid = false;

 if (remaining >= 4 && memcmp(tpl + ti, "{TS}", 4) == 0) {
 val = tsBuf; tokenChars = 4; tokenValid = true;
 } else if (remaining >= 8 && memcmp(tpl + ti, "{DHT_ID}", 8) == 0) {
 val = boardSerial; tokenChars = 8; tokenValid = true;
 /* {tAMB} and {uAMB} are gone. They read the record's ambientTemp and
  * ambientHum, the two columns that belonged to "the ambient sensor" —
  * i.e. slot 10 — and nothing had written them since V4 landed, so they
  * had already been resolving as absent on every board. Per-sensor keys
  * are {t<slot>} and {u<slot>}; both accept the compound
  * "<key>_ID":{<key>} form that rewrites the key to the sensor hwId. */
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
 } else if (remaining >= 5 && tpl[ti+1] == 't' &&
 tpl[ti+2] >= '1' && tpl[ti+2] <= '1' &&
 tpl[ti+3] >= '0' && tpl[ti+3] <= '5' && tpl[ti+4] == '}') {
 /* {t10}..{t15} — two-digit slot index */
 int idx = (tpl[ti+2] - '0') * 10 + (tpl[ti+3] - '0');
 if (idx < MAX_SENSORS) {
 val = slotHas[idx] ? slotVal[idx] : nullptr;
 hwid = cfg.sensors[idx].hwId;
 compKeyLen = snprintf(compKey, sizeof(compKey), "t%d", idx);
 tokenChars = 5; tokenValid = true;
 }
 } else if (remaining >= 4 && tpl[ti+1] == 'u' &&
 tpl[ti+2] >= '0' && tpl[ti+2] <= '9' && tpl[ti+3] == '}') {
 /* {u0}..{u9} — per-slot humidity single digit */
 int idx = tpl[ti+2] - '0';
 if (idx < MAX_SENSORS) {
 val = slotHumHas[idx] ? slotHumVal[idx] : nullptr;
 hwid = cfg.sensors[idx].hwId;
 compKeyLen = snprintf(compKey, sizeof(compKey), "u%d", idx);
 tokenChars = 4; tokenValid = true;
 }
 } else if (remaining >= 5 && tpl[ti+1] == 'u' &&
 tpl[ti+2] >= '1' && tpl[ti+2] <= '1' &&
 tpl[ti+3] >= '0' && tpl[ti+3] <= '5' && tpl[ti+4] == '}') {
 /* {u10}..{u15} — per-slot humidity two-digit */
 int idx = (tpl[ti+2] - '0') * 10 + (tpl[ti+3] - '0');
 if (idx < MAX_SENSORS) {
 val = slotHumHas[idx] ? slotHumVal[idx] : nullptr;
 hwid = cfg.sensors[idx].hwId;
 compKeyLen = snprintf(compKey, sizeof(compKey), "u%d", idx);
 tokenChars = 5; tokenValid = true;
 }
 /* {pAMB} is gone with {tAMB}/{uAMB}: it could only attribute pressure
  * to the ambient slot or to the board, never to the sensor that
  * measured it. {p<slot>} does, and its rewritten key matches the V4
  * history key for the same channel. */
 } else if (remaining >= 4 && tpl[ti+1] == 'p' &&
            tpl[ti+2] >= '0' && tpl[ti+2] <= '9' &&
            (tpl[ti+3] == '}' ||
             (remaining >= 5 && tpl[ti+3] >= '0' && tpl[ti+3] <= '9' && tpl[ti+4] == '}'))) {
 /* {p0}..{p15} — pressure attributed to the slot that produces it.
  *
  * BinaryHistoryRecord carries ONE pressure field, not an array, because
  * only one sensor on a bus reports it: collectBatch writes rec.pressure
  * from whichever active slot has CH_PRESS. So {pN} resolves to that
  * single value, but only when slot N is really the pressure source —
  * asking for {p1} on a DHT22 yields nothing rather than borrowing the
  * BMP280's reading. That makes the rewritten key ("pTBD0001") match the
  * V4 history key for the same channel. */
 const bool twoDigit = !(tpl[ti+3] == '}');
 int idx = twoDigit ? (tpl[ti+2] - '0') * 10 + (tpl[ti+3] - '0') : (tpl[ti+2] - '0');
 if (idx < MAX_SENSORS) {
 const bool slotHasPress = cfg.sensors[idx].active &&
                           sensorHasChannel((SensorType)cfg.sensors[idx].sensorType, CH_PRESS);
 val = (slotHasPress && hasPress) ? pressBuf : nullptr;
 hwid = cfg.sensors[idx].hwId;
 compKeyLen = snprintf(compKey, sizeof(compKey), "p%d", idx);
 tokenChars = twoDigit ? 5 : 4; tokenValid = true;
 }
 }

 if (!tokenValid) {
 /* '{' not followed by a known token — emit literally, advance 1 */
 dest[di++] = c;
 ti++;
 continue;
 }

 /* Check compound context by looking back in template:
 * "<compKey>_ID":{<tok>} → pattern1
 * "<compKey>":{<tok>} → pattern2
 */
 bool matchedFull = false, matchedBare = false;
 if (compKeyLen > 0) {
 const size_t p1 = compKeyLen + 6; /* "<k>_ID": */
 if (ti >= p1) {
 const char* p = tpl + ti - p1;
 if (p[0] == '"' &&
 memcmp(p + 1, compKey, compKeyLen) == 0 &&
 memcmp(p + 1 + compKeyLen, "_ID\":", 5) == 0) {
 matchedFull = true;
 }
 }
 if (!matchedFull) {
 const size_t p2 = compKeyLen + 3; /* "<k>": */
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
 /* Channel letter comes from the token itself (t/u/p), so a new
  * channel does not need this line touched again. */
 const char prefix[3] = { '"', compKey[0], '\0' };
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
 LogManager::instance( ).writeConsole(hdr);

 char buf[256];
 size_t start = 0;
 for (size_t i = 0; i < len; i++) {
 if (payload[i] == ',') {
 size_t n = i - start + 1; /* includes the comma */
 if (n >= sizeof(buf)) n = sizeof(buf) - 1;
 memcpy(buf, payload + start, n);
 buf[n] = '\0';
 LogManager::instance( ).writeConsole(buf);
 start = i + 1;
 }
 }
 if (start < len) {
 size_t n = len - start;
 if (n >= sizeof(buf)) n = sizeof(buf) - 1;
 memcpy(buf, payload + start, n);
 buf[n] = '\0';
 LogManager::instance( ).writeConsole(buf);
 }

 LogManager::instance( ).writeConsole("=== END ===");
}


/**
 * @brief Count pending telemetry records by scanning history files.
 * Called periodically (~10s) by AppManager for dashboard display.
 */
void TelemetryManager::refreshPendingCount( ) {
 if (!_storageRef || !_pendingDirty) return;

 /* A copy: counting must not move the cursor. It takes the same corrections
  * collectBatch makes — the floor of a cursor that never sent, a slot past
  * the end of its file — so the dashboard agrees with the next batch. */
 TelCursorState v = _storageRef->telCursor( );

 /* The same 30-day floor collectBatch applies when nothing was ever sent.
  * Without it, the count right after `tel reset` includes every record on
  * flash — including the ones the sender will never reach — so the dashboard
  * shows a backlog that can only ever shrink to a non-zero number. */
 if (!v.floorDay) {
 const uint32_t lastRecorded = _storageRef->getLastRecordedTimestamp( );
 if (lastRecorded > 86400UL * 30) v.floorDay = StorageManager::historyDayOf(lastRecorded - 86400UL * 30);
 }

 std::vector<String> files;
 listDayFiles(files);

 /* 32-bit accumulator, saturated on the way out. It used to be uint16_t with
  * an explicit cast on every add, so an archive holding more than 65535
  * pending records wrapped to a plausible-looking wrong number on the
  * dashboard — this bench holds ~119k. */
 uint32_t total = 0;

 /* Where the open block will land: the file it goes to ends there. */
 const uint8_t ramCount = _storageRef->h5RamCount( );
 uint32_t ramDay = 0, ramOff = 0;
 if (ramCount > 0
     && !_storageRef->h5SealPosition(_storageRef->h5RamT0( ), true, ramDay, ramOff)) {
 ramDay = 0;
 }

 for (const String& fn : files) {
 const uint32_t day = StorageManager::historyDayOfName(fn);
 if (!day || telFileDone(v, day)) continue;

 String fullPath = String(DIR_HISTORY) + "/" + fn;

 bool opened = false;
 { StorageManager::ReadGuard rg(_storageRef); opened = _storageRef->h5OpenDay(fullPath, false); }
 if (!opened) continue;
 telForgetIfBeyond(v, day, day == ramDay ? ramOff : _storageRef->h5OpenDaySize( ));

	 /* Counting is a header walk, not a decode.
	  *
	  * A V5 block header states how many records it holds (§3.3), and that
	  * is all the position rule needs: a block past the file's slot counts
	  * whole, one before it counts nothing, the slot's own block counts what
	  * lies past the slot. A dashboard tick touches ~24 headers per day.
	  * verifyPayload is off for the same reason: CRCing payloads this path
	  * never reads would put the whole file back through flash every ten
	  * seconds.
	  *
	  * A file still under the migrated epoch's rule has only its blocks' first
	  * stamps here, and a block that starts at or before that epoch counts as
	  * sent (telBlockUnsent). At most one straddles it; its tail is counted
	  * from the first delivery after the update, which gives the file a slot.
	  * The collection itself decides record by record, so this is the
	  * estimate being short, never a record held back. */
	 uint32_t walked = 0;
	 for (;;) {
	  H5DataHeader hdr;
	  const int16_t *mn = nullptr, *mx = nullptr;
	  bool got = false;
	  uint32_t off = 0;
	  {
	   StorageManager::ReadGuard rg(_storageRef);
	   got = _storageRef->h5NextBlock(hdr, mn, mx);
	   off = _storageRef->h5BlockOffset( );
	  }
	  if (!got) break;
	  total += telBlockUnsent(v, day, off, hdr.pre.a, hdr.t0);
	  if ((++walked % 20) == 0) { feedWdt( ); yield( ); }
	 }
 { StorageManager::ReadGuard rg(_storageRef); _storageRef->h5CloseDay( ); }

 feedWdt( );
 }

 /* The hour still open counts too — collectBatch sends it now, so leaving it
  * out would report zero pending while data is waiting.
  *
  * WHERE that block is depends on when the question is asked. Normally it is
  * in the encoder. On a SIMUT Air wake it is not: the decision to raise the
  * radio is taken right after _storageMgr->begin( ), and recoverWipV5( ) —
  * which puts the snapshot back into the encoder — runs several hundred lines
  * later in setup( ). Between those two points the block exists only as
  * /history/.wip, so counting RAM alone answered zero on every wake and the
  * radio never came up: the whole telemetry schedule of the Air, silently off
  * since the boot stopped sealing the snapshot into the day file (F23).
  *
  * h5WipBlock( ) reads the snapshot, checks it, and hands over its first
  * stamp and record count; the position rule counts it against where
  * recoverWipV5( ) will file it. It counts only while the encoder is empty, so
  * the two terms can never count the block twice. */
 if (ramCount > 0) {
 if (ramDay) {
 telForgetIfBeyond(v, ramDay, ramOff);
 int16_t vals[H5_MAX_CHANNELS];
 uint32_t epoch = 0;
 for (uint8_t i = 0; i < ramCount; i++) {
 if (!_storageRef->h5RamRecord(i, epoch, vals)) break;
 if (telUnsent(v, ramDay, ramOff, i, epoch)) total++;
 }
 }
 } else {
 uint32_t t0 = 0, wipDay = 0, wipOff = 0;
 uint8_t count = 0;
 if (_storageRef->h5WipBlock(t0, count)
     && _storageRef->h5SealPosition(t0, false, wipDay, wipOff)) {
 telForgetIfBeyond(v, wipDay, wipOff);
 total += telBlockUnsent(v, wipDay, wipOff, count, t0);
 }
 }

 _pendingEstimate = (total > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)total;
 _pendingDirty = false;
}


uint16_t TelemetryManager::getPendingEstimate( ) const {
 return _pendingEstimate;
}

/* The trigger: enough records waiting to be worth the radio.
 *
 * telMinBatch is the field the web and CLI still call t_int, and it used to be
 * an interval in milliseconds. As a count it answers the question the operator
 * actually has — "how much data is worth a transmission?" — and it answers it
 * the same way on mains and on battery: an Air wake raises the CYW43 only when
 * this is true, so a quiet device with nothing to say never powers the radio.
 *
 * The counter is the RAM one, bumped per record by notifyNewRecord and rebuilt
 * from flash by refreshPendingCount; both are approximations of the same thing,
 * and collectBatch remains the authority on what is really sendable. */
bool TelemetryManager::telemetryDue( ) const {
 if (!_storageRef) return false;               /* asked before begin( ): nothing to send yet */
 const uint32_t minBatch = _storageRef->getConfig( ).telInterval;
 if (minBatch == 0) return false;          /* telemetry off */
 return (uint32_t)_pendingEstimate >= minBatch;
}

void TelemetryManager::notifyNewRecord( ) {
 __atomic_fetch_add(&_pendingEstimate, 1, __ATOMIC_RELAXED);
}


/**
 * @brief Consumes the last telemetry send result.
 *
 * Returns true if there was a send since the last call, filling
 * outSuccess with the result. The flag is cleared after consumption, ensuring
 * each result is processed only once.
 */
bool TelemetryManager::consumeLastSendResult(bool& outSuccess) {
 if (_hasSendResult) {
 outSuccess = _lastSendSuccess;
 _hasSendResult = false;
 return true;
 }
 return false;
}



/* =========================================================================== */
/* SECOND TELEMETRY LINE — ALARMS (v21)                                        */
/* =========================================================================== */
/* Fila em RAM + confirmação de recebimento, payload editável, criptografia
 * herdada da linha convencional. Design: docs/analysis/ANALISE_TELEMETRIA_ALARMES.md */

TelemetryManager* TelemetryManager::s_alarmInstance = nullptr;

/** Header de auth do HTTP — mesma semântica da linha convencional
 * (attemptHttpUpload), extraído para reuso sem tocar no caminho original. */
/* Who is sending, on every POST, in headers — the payload does not change.
 * The JSON batch is a bare array and the CSV a table; putting identity in
 * the body would break every receiver that parses them today. A receiver
 * that keeps the request headers (simut-rx does) correlates the source
 * address with the board id, the version, the variant and the config
 * fingerprint for free, and notices a config change or an update without
 * ever opening a session. Four short headers, ~90 B per request. */
static void addIdentityHeaders(HTTPClient& http, StorageManager* storage) {
	http.addHeader("X-SIMUT-Uid", StorageManager::getBoardSerialNumber( ));
	http.addHeader("X-SIMUT-Ver", SIMUT_VERSION);
	http.addHeader("X-SIMUT-Env", simut_env_name( ));
	if (storage) {
		char crc[9];
		snprintf(crc, sizeof(crc), "%08lX", (unsigned long)storage->getConfigCrc( ));
		http.addHeader("X-SIMUT-Cfg", crc);
	}
}

static void addTelemetryAuthHeader(HTTPClient& http, const SystemConfig& cfg) {
	String tokenStr = String(cfg.telApiKey);
	tokenStr.trim( );
	if (tokenStr.length( ) == 0) return;
	int colonIdx = tokenStr.indexOf(':');
	if (colonIdx > 0) {
		String hName = tokenStr.substring(0, colonIdx);
		String hVal = tokenStr.substring(colonIdx + 1);
		hName.trim( ); hVal.trim( );
		http.addHeader(hName, hVal);
	} else {
		http.addHeader("Authorization", "Bearer " + tokenStr);
	}
}

/** URL de alarmes no HTTP: cfg.alarmTel.path verbatim; vazio = telPath + "/alarm". */
static String alarmHttpPath(const SystemConfig& cfg) {
	String p = String(cfg.alarmTel.path);
	p.trim( );
	if (p.length( ) > 0) return p;
	return String(cfg.telPath) + "/alarm";
}

uint16_t TelemetryManager::pushAlarm(uint8_t slot, uint8_t channel, float value, uint8_t errCode,
                                     uint8_t actor, float value2, uint32_t untilEpoch) {
	if (!_alarmEnabled || slot >= MAX_SENSORS) return 0;

	/* Valor de leitura só na borda de limite ("alarm"); ações (sil/off/on) e
	 * falhas (err*) são marcadores sem valor — o {val} fica ausente. Um
	 * "alarm_lim" carrega o PAR de limites, pela mesma escala do canal. */
	const bool isErr = (errCode == ALARM_ERR_ERROR ||
	                    errCode == ALARM_ERR_ERR_SIL ||
	                    errCode == ALARM_ERR_ERR_OFF);
	const bool hasValue = (errCode == ALARM_ERR_ALARM || errCode == ALARM_ERR_ALARM_LIM);
	const ChannelInfo& ci = channelInfo(channel);
	auto scale = [&](float v) -> int16_t {
		if (!isfinite(v)) return HIST_NAN_SENTINEL;
		float s = v * ci.scale;
		if (s > 32767.0f) s = 32767.0f;
		if (s < -32767.0f) s = -32767.0f;
		return (int16_t)lroundf(s);
	};
	const uint32_t now = (uint32_t)time(nullptr);
	const int16_t scaled = hasValue ? scale(value) : HIST_NAN_SENTINEL;
	int16_t second = 0;
	if (errCode == ALARM_ERR_ALARM_LIM) {
		second = scale(value2);
	} else if (errCode == ALARM_ERR_MAINT_ON && untilEpoch > now) {
		/* Minutos até o fim previsto, arredondados; 30 dias = 43.200 cabe num
		 * uint16, que é como AlarmPayload lê o campo de volta. */
		uint32_t mins = (untilEpoch - now + 30u) / 60u;
		if (mins > 65535u) mins = 65535u;
		second = (int16_t)(uint16_t)mins;
	}

	/* v25 — o nome de quem agiu é resolvido AGORA, com a conta ainda no ar, e
	 * viaja dentro do registro. Antes o payload o resolvia na hora do envio,
	 * lendo cfg.users[actor-1]: uma conta apagada no intervalo deixava o
	 * registro assinado por quem tomasse o slot depois. */
	const char* actorName = "";
	if (actor != ALARM_ACTOR_NONE && actor <= MAX_USERS && _storageRef) {
		const UserAccount& ua = _storageRef->getConfig( ).users[actor - 1];
		if (ua.active) actorName = ua.username;
	}
	uint16_t seq = _alarmQueue.push(now, slot, channel, scaled, errCode, actor, second, actorName);
	auto& m = MetricsManager::instance( ).data( );
	if (seq != 0) {
		m.alarmQueued++;
		if (isErr) m.alarmErrRecords++;
		/* gatilho imediato: o próximo update( ) não espera o retry interval */
		__atomic_store_n(&_alarmSendPending, true, __ATOMIC_RELEASE);
	} else {
		m.alarmDropped++;
		/* string fixa sem TRL: os packs de idioma têm teto de RAM e esta
		 * linha é operacional/de operador — mesmo padrão dos demais logs TEL */
		LOG_CODE(LOG_WARN, "TEL", TEL_ALARM_DROP, (int)_alarmQueue.dropped( ),
		         "Alarm queue full — record dropped");
	}
	return seq;
}

void TelemetryManager::updateAlarms( ) {
	if (!_alarmEnabled) { _alarmSendPending = false; return; }

	SystemConfig &cfg = _storageRef->getConfig( );
	String server = String(cfg.telServer);
	server.trim( );
	if (server.length( ) == 0) { _alarmSendPending = false; return; }

	const bool due = _alarmSendPending ||
	                 (_alarmQueue.size( ) > 0 &&
	                  timeSince(_lastAlarmAttempt, ALARM_RETRY_INTERVAL_MS));
	if (!due) return;

	_alarmSendPending = false; /* consumido — falha rearma pelo retry interval */
	_lastAlarmAttempt = millis( );

	bool expected = false;
	if (!__atomic_compare_exchange_n(&_alarmSending, &expected, true,
	                                 false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) return;
	if (!_netRef->isNetworkHealthy( )) { __atomic_store_n(&_alarmSending, false, __ATOMIC_RELEASE); return; }
	if (!_storageRef->lockHeavyTask( )) { __atomic_store_n(&_alarmSending, false, __ATOMIC_RELEASE); return; }

	LogManager::WdtWindow _wdt(120000);

	/* Mesmo preflight da linha convencional: TLS pede a reserva maior. */
	uint32_t freeH = rp2040.getFreeHeap( );
	const uint32_t PREFLIGHT_FLOOR = cfg.telEncryption ? 24576 : 14336;
	if (freeH < PREFLIGHT_FLOOR) {
		_storageRef->unlockHeavyTask( );
		__atomic_store_n(&_alarmSending, false, __ATOMIC_RELEASE);
		return; /* fila fica; retry no próximo intervalo */
	}

	/* Direto no vetor, sem o array intermediário de ALARM_QUEUE_MAX que ficava
	 * na PILHA: eram 1.024 B de stack (64 x 16) copiados para um heap que ia
	 * receber os mesmos dados de qualquer jeito. Some a cópia e some o custo
	 * de pilha — o que também é o que permite o registro crescer para 32 B na
	 * v25 sem dobrar a pilha deste caminho (AlarmQueue.h explica o porquê do
	 * nome congelado). Medido 2026-09-20. */
	std::vector<AlarmRecord> batch(ALARM_BATCH_MAX);
	batch.resize(_alarmQueue.snapshot(batch.data( ), ALARM_BATCH_MAX));
	if (batch.empty( )) {
		__atomic_store_n(&_alarmSending, false, __ATOMIC_RELEASE);
		_storageRef->unlockHeavyTask( );
		return;
	}

	String payload = buildAlarmPayload(batch);
	if (_alarmDumpNext) {
		_dumpAlarmPayload(payload.c_str( ), payload.length( ),
		                  cfg.telTransport == TEL_TRANSPORT_MQTT ? "ALARM-MQTT" : "ALARM-HTTP");
		_alarmDumpNext = false;
	}

	bool success = false;
	if (payload.length( ) == 0) {
		/* nada bem formado para enviar (buildAlarmPayload) — a fila fica */
	} else if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
		success = attemptAlarmMqttPublish(payload, batch);
	} else {
		success = attemptAlarmHttpUpload(payload, batch);
	}

	__atomic_store_n(&_alarmSending, false, __ATOMIC_RELEASE);
	_storageRef->unlockHeavyTask( );
	if (!success) {
		/* contagem única por ciclo — as funções de transporte não contam */
		MetricsManager::instance( ).data( ).alarmFailed++;
	}
}

String TelemetryManager::buildAlarmPayload(std::vector<AlarmRecord>& batch) {
	LogManager::TraceScope _tA(0, MOD_TEL_BUILD);
	SystemConfig &cfg = _storageRef->getConfig( );
	const uint8_t mode = cfg.alarmTel.mode;

	size_t perLine = (mode == TEL_MODE_CSV) ? 48 : 128;
	size_t fixedPart = (mode == TEL_MODE_CSV) ? 16 : 128;
	String s;
	s.reserve(batch.size( ) * perLine + fixedPart);

	/* Mesma regra de buildPayload( ) (o porquê e a medição estão lá): cada
	 * registro entra inteiro ou não entra, e o payload sempre fecha. O lote é
	 * cortado no que entrou — a confirmação da fila é pelos `seq` deste vetor,
	 * então o que ficou de fora continua na fila para o próximo envio. */
	size_t kept = batch.size( );
	bool closed = false;

	if (mode == TEL_MODE_JSON) {
		/* Mesma regra da linha convencional: JSON ignora o template global
		 * e emite um array de linhas. */
		s = "[";
		char lineBuf[512];
		for (size_t i = 0; i < batch.size( ); i++) {
			int len = alarmFormatLine(batch[i], cfg, lineBuf, sizeof(lineBuf));
			if (len < 0) len = 0;
			if (s.length( ) == 0 || !appendWhole(s, ",", i > 0 ? 1 : 0, lineBuf, (size_t)len, 1)) { kept = i; break; }
			if (i % 10 == 9) { watchdog_update( ); yield( ); }
		}
		closed = (s.length( ) > 0) && s.concat(']');
	} else if (mode == TEL_MODE_CSV) {
		s = "seq;ts;id;v;user;lo;hi;until";
		closed = (s.length( ) > 0) && s.concat('\n');
		char csvBuf[96];
		for (size_t i = 0; closed && i < batch.size( ); i++) {
			int len = alarmFormatCsvLine(batch[i], cfg, csvBuf, sizeof(csvBuf));
			if (len < 0) len = 0;
			if (!appendWhole(s, csvBuf, (size_t)len, "\n", 1, 0)) { kept = i; break; }
			if (i % 10 == 9) { watchdog_update( ); yield( ); }
		}
	} else {
		/* Custom: mesmo contrato da linha convencional — template global com
		 * {DEV} {MAC} {DATA}; linhas unidas pelo separador configurado. */
		char sep[16];
		strlcpy(sep, cfg.alarmTel.lineSeparator, sizeof(sep));
		if (sep[0] == '\\' && sep[1] == 'n' && sep[2] == '\0') { sep[0] = '\n'; sep[1] = '\0'; }
		size_t sepLen = strlen(sep);

		String macStr = _netRef->getMacAddress( );
		const char* gt = cfg.alarmTel.globalTemplate;
		const size_t gtLen = strnlen(gt, sizeof(cfg.alarmTel.globalTemplate));

		char lineBuf[512];
		size_t gi = 0;
		size_t spanStart = 0;
		bool ok = true;
		while (gi < gtLen) {
			if (gt[gi] != '{') { gi++; continue; }
			const size_t remaining = gtLen - gi;
			size_t tokLen = 0;
			int tokKind = 0; /* 1=DEV, 2=MAC, 3=DATA */
			if (remaining >= 5 && memcmp(gt + gi, "{DEV}", 5) == 0) { tokKind = 1; tokLen = 5; }
			else if (remaining >= 5 && memcmp(gt + gi, "{MAC}", 5) == 0) { tokKind = 2; tokLen = 5; }
			else if (remaining >= 6 && memcmp(gt + gi, "{DATA}", 6) == 0) { tokKind = 3; tokLen = 6; }
			if (tokKind == 0) { gi++; continue; }
			if (gi > spanStart) ok = ok && s.concat(gt + spanStart, gi - spanStart);
			if (tokKind == 1) {
				ok = ok && s.concat(cfg.deviceName);
			} else if (tokKind == 2) {
				ok = ok && s.concat(macStr);
			} else {
				/* o resto do template, com folga para um nome ou um MAC */
				const size_t tail = (gtLen - (gi + tokLen)) + sizeof(cfg.deviceName);
				for (size_t i = 0; ok && i < kept; i++) {
					int len = alarmFormatLine(batch[i], cfg, lineBuf, sizeof(lineBuf));
					if (len < 0) len = 0;
					if (!appendWhole(s, sep, i > 0 ? sepLen : 0, lineBuf, (size_t)len, tail)) { kept = i; break; }
					if (i % 10 == 9) { watchdog_update( ); yield( ); }
				}
			}
			gi += tokLen;
			spanStart = gi;
		}
		if (gtLen > spanStart) ok = ok && s.concat(gt + spanStart, gtLen - spanStart);
		closed = ok;
	}

	if (kept < batch.size( )) batch.resize(kept);
	/* Nada bem formado para enviar: string vazia e lote vazio — o chamador
	 * não envia e a fila fica como estava. */
	if (!closed || batch.empty( )) {
		batch.clear( );
		return String( );
	}
	return s;
}

bool TelemetryManager::attemptAlarmHttpUpload(String& payload, std::vector<AlarmRecord>& batch) {
	SystemConfig &cfg = _storageRef->getConfig( );
	feedWdt( );

	HTTPClient http;
	WiFiClient client;
	String protocol = cfg.telEncryption ? "https://" : "http://";
	String url = protocol + String(cfg.telServer) + ":" + String(cfg.telPort) + alarmHttpPath(cfg);
	bool connected = false;

	if (cfg.telEncryption) {
#if !SIMUT_TEL_TLS
		/* Same refusal as the data line: no TLS client, nothing sent. */
		LOG_CODE(LOG_ERROR, "TEL", TEL_ALARM_FAIL, TEL_CTX_NO_TLS, "no TLS client in this image");
		return false;
#else
		if (!_httpSecurePtr) {
			_httpSecurePtr = new WiFiClientSecure( );
			if (!_httpSecurePtr) {
				LOG_CODE(LOG_ERROR, "TEL", TEL_ALARM_FAIL, 0, TRL("OOM: WiFiClientSecure"));
				return false;
			}
			_httpSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
		}
		_httpSecureLastUse = millis( );
		WiFiClientSecure::setTLSConnectTimeout(NET_TLS_HANDSHAKE_MS);
		_httpSecurePtr->setBufferSizes(4096, 512);
		if (_hasCert) _httpSecurePtr->setCACert(_cachedCert.c_str( ));
		else _httpSecurePtr->setInsecure( );
		connected = http.begin(*_httpSecurePtr, url);
#endif
	} else {
		connected = http.begin(client, url);
	}

	bool success = false;
	int code = 0;

	if (connected) {
		/* v26: same rule as the data line, with the alarm line's own field. */
		char ctBuf[TEL_CT_MAX + 1];
		http.addHeader("Content-Type",
		               telContentTypeFor(cfg.alarmTel.mode, cfg.telCustom.alarmCustomContentType,
		                                 sizeof(cfg.telCustom.alarmCustomContentType),
		                                 ctBuf, sizeof(ctBuf)));
		addTelemetryAuthHeader(http, cfg);
		addIdentityHeaders(http, _storageRef);

		http.setTimeout(NET_SOCKET_TIMEOUT_MS);
		feedWdt( );

		{ code = http.POST(payload); }
		watchdog_update( );

		if (code >= 200 && code < 300) {
			/* 2xx = confirmação de recebimento (R3): a fila só esvazia aqui. */
			ackAlarmBatch(batch);
			MetricsManager::instance( ).data( ).alarmSent += (uint32_t)batch.size( );
			LOG_CODE(LOG_INFO, "TEL", TEL_ALARM_SENT, (int)batch.size( ),
			         "Alarm HTTP OK: " + String(payload.length( )) + " bytes, " +
			         String(batch.size( )) + " records");
			success = true;
		} else if (code > 0) {
			LOG_CODE(LOG_ERROR, "TEL", TEL_ALARM_FAIL, code,
			         "Alarm HTTP rejected: code " + String(code));
		} else {
			LOG_CODE(LOG_ERROR, "TEL", TEL_ALARM_FAIL, code,
			         String(TRL("HTTP error: ")) + http.errorToString(code));
		}

#if SIMUT_TEL_TLS
		if (cfg.telEncryption) { if (_httpSecurePtr) _httpSecurePtr->stop( ); }
		else
#endif
		client.stop( );
		http.end( );
	}

	return success;
}

#if SIMUT_TEL_MQTT
String TelemetryManager::mqttAlarmTopic( ) {
	return mqttDataTopic( ) + "/alarm";
}

String TelemetryManager::mqttAlarmAckTopic( ) {
	return mqttDataTopic( ) + "/alarm/ack";
}

void TelemetryManager::mqttSubscribeAlarmAck( ) {
	if (!_alarmEnabled || !_mqttInitialized || !_mqttClient.connected( )) return;
	String ackTopic = mqttAlarmAckTopic( );
	/* QoS 1 na ASSINATURA: o ACK em si merece entrega garantida. O publish
	 * da linha continua QoS 0 (PubSubClient não publica QoS 1 — D-232-QOS);
	 * a confirmação é por aplicação, e o retry do device cobre a perda. */
	_mqttClient.subscribe(ackTopic.c_str( ), 1);
	LOG_CODE(LOG_INFO, "TEL", TEL_ALARM_ACK, 0, "subscribed " + ackTopic);
}

bool TelemetryManager::attemptAlarmMqttPublish(String& payload, std::vector<AlarmRecord>& batch) {
	if (!_mqttInitialized) return false;

	if (!mqttEnsureConnected( )) return false;

	String topic = mqttAlarmTopic( );

	/* The batch goes out in ONE publish, and PubSubClient refuses a packet
	 * longer than its buffer (5 B of header, 2 + the topic, the payload) by
	 * returning false and nothing else. begin( ) sets 2048 B and only the
	 * conventional line's large batches ever grew it, while a JSON alarm record
	 * is ~120 B: a full default queue of 32 never fit, every retry failed the
	 * same way and the line stayed stuck (finding 29, by reading). Grow the
	 * buffer as the conventional line does; past MQTT_BUFFER_CEILING, send the
	 * front of the batch — the ACK is per seq, and the rest goes next round. */
	const size_t overhead = topic.length( ) + MQTT_PACKET_OVERHEAD;
	while (payload.length( ) + overhead > MQTT_BUFFER_CEILING && batch.size( ) > 1) {
		batch.resize(batch.size( ) / 2);
		payload = buildAlarmPayload(batch);
	}
	if (payload.length( ) + overhead > _mqttClient.getBufferSize( )) {
		_mqttClient.setBufferSize((uint16_t)min(MQTT_BUFFER_CEILING,
		                                        payload.length( ) + overhead));
	}

	feedWdt( );
	bool ok = _mqttClient.publish(topic.c_str( ), payload.c_str( ), false);
	if (ok) {
		/* QoS 0: publish aceito NÃO é confirmação. Os registros ficam na fila
		 * e só saem quando o servidor publicar o ACK no tópico de ack —
		 * mqttAlarmAckCallback → handleAlarmAckPayload. Reenvios duplicam;
		 * o seq permite o servidor deduplicar. */
		MetricsManager::instance( ).data( ).alarmSent += (uint32_t)batch.size( );
		LOG_CODE(LOG_INFO, "TEL", TEL_ALARM_SENT, (int)batch.size( ),
		         "Alarm MQTT published to " + topic + " (" +
		         String(batch.size( )) + " records, awaiting ACK)");
		return true;
	}
	LOG_CODE(LOG_ERROR, "TEL", TEL_ALARM_FAIL, _mqttClient.state( ),
	         "Alarm MQTT publish failed");
	return false;
}

void TelemetryManager::mqttAlarmAckCallback(char* topic, uint8_t* payload, unsigned int length) {
	(void)topic;
	if (s_alarmInstance) s_alarmInstance->handleAlarmAckPayload(payload, length);
}

void TelemetryManager::handleAlarmAckPayload(const uint8_t* payload, unsigned int length) {
	uint16_t seqs[ALARM_BATCH_MAX];
	uint8_t n = alarmParseSeqList(payload, length, seqs, ALARM_BATCH_MAX);
	if (n == 0) return;
	uint8_t removed = _alarmQueue.ack(seqs, n);
	if (removed > 0) {
		MetricsManager::instance( ).data( ).alarmAcked += removed;
		LOG_CODE(LOG_INFO, "TEL", TEL_ALARM_ACK, removed,
		         "Alarm receipt confirmed: " + String(removed) + " dequeued, " +
		         String(_alarmQueue.size( )) + " pending");
	}
}
#else
bool TelemetryManager::attemptAlarmMqttPublish(String& payload, std::vector<AlarmRecord>& batch) {
 (void)payload; (void)batch;
 LOG_CODE(LOG_ERROR, "TEL", TEL_ALARM_FAIL, TEL_CTX_NO_MQTT, "no MQTT client in this image");
 return false;
}
#endif

void TelemetryManager::ackAlarmBatch(const std::vector<AlarmRecord>& batch) {
	if (batch.empty( )) return;
	uint16_t seqs[ALARM_BATCH_MAX];
	uint8_t n = batch.size( ) > ALARM_BATCH_MAX ? ALARM_BATCH_MAX : (uint8_t)batch.size( );
	for (uint8_t i = 0; i < n; i++) seqs[i] = batch[i].seq;
	uint8_t removed = _alarmQueue.ack(seqs, n);
	if (removed > 0) {
		MetricsManager::instance( ).data( ).alarmAcked += removed;
		LOG_CODE(LOG_INFO, "TEL", TEL_ALARM_ACK, removed,
		         "Alarm batch confirmed: " + String(removed) + " dequeued, " +
		         String(_alarmQueue.size( )) + " pending");
	}
}

void TelemetryManager::_dumpAlarmPayload(const char* payload, size_t len, const char* label) {
	char hdr[48];
	snprintf(hdr, sizeof(hdr), "=== ALARM PAYLOAD %s (%u B) ===", label, (unsigned)len);
	LogManager::instance( ).writeConsole(hdr);

	char buf[256];
	size_t start = 0;
	for (size_t i = 0; i < len; i++) {
		if (payload[i] == ',') {
			size_t n = i - start + 1;
			if (n >= sizeof(buf)) n = sizeof(buf) - 1;
			memcpy(buf, payload + start, n);
			buf[n] = '\0';
			LogManager::instance( ).writeConsole(buf);
			start = i + 1;
		}
	}
	if (start < len) {
		size_t n = len - start;
		if (n >= sizeof(buf)) n = sizeof(buf) - 1;
		memcpy(buf, payload + start, n);
		buf[n] = '\0';
		LogManager::instance( ).writeConsole(buf);
	}
	LogManager::instance( ).writeConsole("=== END ===");
}
