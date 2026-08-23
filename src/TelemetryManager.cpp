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
#include "sensors/SensorChannelTable.h"
#include <LittleFS.h>
#include <algorithm>
#include <string.h>
#include <hardware/watchdog.h>
#include <pico/time.h>

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

static bool historyDayIsBefore(const String &fileName, const char *minDay) {
	if (fileName.length( ) < 8) return false;
	for (int i = 0; i < 8; i++) {
		const char c = fileName[i];
		if (c < '0' || c > '9') return false;
	}
	return strncmp(fileName.c_str( ), minDay, 8) < 0;
}

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

TelemetryManager::TelemetryManager( )
 : _mqttClient(_mqttWifiClient),
   _alarmQueue(ALARM_QUEUE_DEFAULT)
{
 _lastCheckTime = 0;
 _hasCert = false;
 _currentBackoff = BACKOFF_MIN_MS;
 _backoffUntil = 0;
 _consecutiveFails = 0;
 _mqttInitialized = false;
 _lastMqttReconnect = 0;
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
 if (cfg.telEncryption) {
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
 } else {
 /*
 * HTTP: pre-allocate WiFiClientSecure at boot to avoid fragmentation.
 * If allocated later, the heap may be too fragmented for
 * the ~16KB contiguous block that TLS needs.
 */
 if (cfg.telEncryption && cfg.telInterval > 0) {
 _httpSecurePtr = new WiFiClientSecure( );
 if (_httpSecurePtr) {
 _httpSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
 if (_hasCert) _httpSecurePtr->setCACert(_cachedCert.c_str( ));
 else _httpSecurePtr->setInsecure( );
 }
 }
 LOG_CODE(LOG_INFO, "TEL", TEL_HTTP_INIT, cfg.telPort, String(cfg.telServer) + String(cfg.telPath));
 }

 resetBackoff( );

 /*
 * Starts the timer with current millis() so that the first telemetry
 * attempt waits a full interval after boot.
 * Without this, _lastCheckTime=0 causes immediate firing on the first
 * loop iteration — the TLS handshake + POST can exceed the watchdog.
 */
 _lastCheckTime = millis( );
}

/**
 * @brief Periodic telemetry check — collects batch and dispatches via configured transport.
 * Respects backoff intervals, network availability, and heavy task locks.
 */
/**
 * @brief Highest epoch safe to record as delivered.
 *
 * @p batch is what buildPayload left behind, so it is exactly what the
 * transport carries. Two things this must not do:
 *
 *  · Read the last element as the newest one. That was the rule, and it holds
 *    only while records arrive in time order — which is an assumption about
 *    the writer's clock, not a property of the data. A boot with a mis-seeded
 *    provisional clock (2026-08-14) writes blocks stamped ahead of the ones
 *    that follow them, and then the tail of the vector is not the high-water
 *    mark at all.
 *
 *  · Let a record stamped in the future set the frontier. The cursor is a
 *    scalar in time and `epoch > lastCursor` skips everything at or below it,
 *    forever — so one block stamped hours ahead buried every correctly stamped
 *    record behind it, permanently, and without a log line. That is worse than
 *    the graph bug of the same night, because a chart redraws and a telemetry
 *    record that was never sent is gone.
 *
 * Clamping to @p nowEpoch stops a future stamp from moving the frontier past
 * real time. The record still goes out; it just does not get to define what
 * counts as sent. Anything ahead of the clamp is offered again on a later
 * round, which is the right way round: ingest is keyed by timestamp, so a
 * duplicate costs a write and a gap costs the measurement.
 *
 * What this does NOT fix, because a scalar cursor in time cannot: a record
 * stamped ahead of its neighbours but still behind `now` — a mis-stamped block
 * read back hours later — advances the frontier over records that are older
 * and not yet sent, and those stay unsent. Closing that needs the cursor to
 * become a scan position rather than an instant, which is a format change, or
 * needs the stamps to be right in the first place, which is what the seed
 * ceiling in h5SeedCeiling is for. The clamp covers the live case, where the
 * bad stamp is in the future at the moment of sending, and that is the shape
 * the 2026-08-14 device was in while it was writing.
 *
 * @param fallback Cursor to keep when nothing was delivered, or when the clamp
 *                 would move it backwards.
 */
static uint32_t deliveredCursor(const std::vector<BinaryHistoryRecord>& batch,
                                uint32_t fallback, uint32_t nowEpoch) {
 uint32_t hi = 0;
 for (size_t i = 0; i < batch.size( ); i++) {
 if (batch[i].epoch > hi) hi = batch[i].epoch;
 }
 if (hi == 0) return fallback;
 if (nowEpoch >= HIST_EPOCH_MIN && hi > nowEpoch) hi = nowEpoch;
 return (hi < fallback) ? fallback : hi;
}

void TelemetryManager::update( ) {
 /* Segunda linha (alarmes): ciclo próprio, independente do intervalo da
  * telemetria convencional — um alarme não espera a cadência de massa. */
 updateAlarms( );

 SystemConfig &cfg = _storageRef->getConfig( );
 if (cfg.telInterval == 0) return;

 /*
 * MQTT keepalive: calls loop() only if connected to the broker.
 * Prevents loop() from attempting implicit reconnect with long socket
 * timeout that would freeze the main loop on degraded networks.
 */
 if (cfg.telTransport == TEL_TRANSPORT_MQTT && _mqttInitialized
 && _mqttClient.connected( )) {
 _mqttClient.loop( );
 watchdog_update( );

 /* HA discovery safety net for config paths that do NOT reboot (commit_all
  * does, and its post-reboot connect reconciles there): while connected,
  * a mismatch between the toggle and the persisted published bit is
  * settled here. Two RAM reads per pass when in sync; the CAS keeps the
  * publish burst from interleaving with a send. */
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

 uint32_t now = millis( );

 if (_consecutiveFails > 0 && now < _backoffUntil) return;
 /* Dynamic effective interval inline.
 * Floor: cfg.telInterval. Ceiling: smoothed_latency × 1.5 (avoids queue
 * buildup when server is slow). RSSI penalty: <-85 ×2, <-75 ×1.5.
 * Final cap 60s. */
 uint32_t effectiveInt = cfg.telInterval;
 if (_smoothedLatencyMs > 0) {
 uint32_t lf = (_smoothedLatencyMs * 3) / 2;
 if (lf > effectiveInt) effectiveInt = lf;
 }
 int32_t rssi = _netRef ? _netRef->getRssi( ) : 0;
 if (rssi < -85 && rssi > -100) effectiveInt *= 2;
 else if (rssi < -75) effectiveInt = (effectiveInt * 3) / 2;
 if (effectiveInt > 60000) effectiveInt = 60000;
 if (effectiveInt < cfg.telInterval) effectiveInt = cfg.telInterval;
 _effectiveIntervalMs = effectiveInt;
 if (_consecutiveFails == 0 && (now - _lastCheckTime < effectiveInt)) return;


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

 std::vector<BinaryHistoryRecord> batch;
 uint32_t newCursor = 0;

 if (!collectBatch(batch, newCursor)) {
 __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
 _storageRef->unlockHeavyTask( );
 resetBackoff( );
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
 success = attemptMqttPublish(payload, batch, newCursor);
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

 /* Free batch to reduce RAM peak before TLS handshake */
 batch.clear( );
 batch.shrink_to_fit( );

 success = attemptHttpUpload(payload, newCursor);
 }

 __atomic_store_n(&_isSending, false, __ATOMIC_RELEASE);
 _storageRef->unlockHeavyTask( );
 _pendingDirty = true; /* Recalibrate after send */

 if (success) {
 resetBackoff( );
 } else {
 escalateBackoff( );
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

bool TelemetryManager::collectBatch(std::vector<BinaryHistoryRecord>& batch, uint32_t& newCursor) {
 LogManager::TraceScope _tC(0, MOD_TEL_COLLECT);
 SystemConfig &cfg = _storageRef->getConfig( );
 uint32_t lastCursor = _storageRef->getLastSentTimestamp( );

 /* Cursor-in-the-future detection. If lastCursor > now + 1 day,
 * it is an artifact of manual future time set + return to NTP. Without reset,
 * collectBatch rejects all new records (rec.epoch > lastCursor
 * always false) and telemetry goes silent without log clues. Resets the
 * cursor → falls to the 30d fallback below. 1-day threshold tolerates
 * small drift (timezone). */
 uint32_t nowEpoch = (uint32_t)time(nullptr);
 if (nowEpoch > 1600000000UL && lastCursor > nowEpoch + 86400UL) {
 LOG_CODE(LOG_WARN, "TEL", SYS_OK, 0,
 TRL("Telemetry cursor in future — reset to 0"));
 _storageRef->setLastSentTimestamp(0);
 lastCursor = 0;
 }

 /* Fallback when cursor is 0 (no NTP / never sent) —
 * use last recorded timestamp - 30 days to limit scan. */
 if (lastCursor == 0) {
 uint32_t lastRecorded = _storageRef->getLastRecordedTimestamp( );
 if (lastRecorded > 86400UL * 30) lastCursor = lastRecorded - 86400UL * 30;
 }

 newCursor = lastCursor;


 std::vector<String> files;
 {
 _storageRef->enterFlashReadLock( );
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 if (dir.fileName().endsWith(HISTORY_FILE_EXT)) {
 files.push_back(dir.fileName( ));
 }
 }
 _storageRef->exitFlashReadLock( );
 }
 std::sort(files.begin( ), files.end( ));

 /* L2: o corte compara apenas os 8 digitos YYYYMMDD do nome. Antes o
  * limite era montado com o sufixo ".bin" fixo e comparado contra nomes
  * que podem terminar em ".sim4" — funcionava por acaso (os digitos
  * decidem antes de o sufixo importar) e quebraria em silencio ao mudar
  * qualquer extensao. Comparar so a data torna a regra explicita. */
 char minDay[9] = "";
 if (lastCursor > 1000000000) {
 /* The floor is one block span behind the cursor, not the cursor's own day.
  * A block open across midnight is filed under the day it STARTED, so
  * yesterday's file goes on holding records after 00:00 — and cutting at the
  * cursor's day closed that file the moment the cursor crossed midnight,
  * stranding those records for good. They are not missing and not older than
  * the cursor; they are in a file nobody opens again. Measured on the bench:
  * 48 records of one straddling block, never sent. */
 const time_t cursorEpoch = (time_t)h5ScanFloor(
     lastCursor, h5NominalSeconds(_storageRef->getHistoryIntervalMin( )));
 struct tm timeinfo;
 localtime_r(&cursorEpoch, &timeinfo);
 snprintf(minDay, sizeof(minDay), "%04d%02d%02d",
          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
 }

 uint8_t limit = safeBatchLimit(
 (cfg.telBatchSize > 0) ? cfg.telBatchSize : 10);


 /* Codec V2 (delta + anchor). Replaces the raw 28-byte read
 * (V1 format) that was silently broken after migration. Reader follows
 * the pattern used in StorageManager::getLastRecorded
 * and WebManager::handleApiHistoryData. */
 /* L1: piso unificado em SystemDefs_Limits.h. Era 1,7e9 aqui e no
  * escritor V2, contra 1,6e9 no escritor V4 — registros gravados na
  * janela entre os dois nunca eram enviados. */
 const uint32_t EPOCH_MIN = HIST_EPOCH_MIN;

 for (const String& fn : files) {
 if (batch.size( ) >= limit) break;
 if (minDay[0] && historyDayIsBefore(fn, minDay)) continue;

 String fullPath = String(DIR_HISTORY) + "/" + fn;

 _storageRef->enterFlashReadLock( );
 File f = LittleFS.open(fullPath, "r");
 if (!f) { _storageRef->exitFlashReadLock( ); continue; }

 	 {
	 f.close( );
	 _storageRef->exitFlashReadLock( );

	 /* V5 read. The reader lives in StorageManager, so what used to be
	  * ~5.9 KB of codec scratch plus a copy of the refill loop here is a
	  * pair of calls. Mapping a value back to a slot is arithmetic on the
	  * descriptor id rather than a string-pool lookup per measurement. */
	 bool opened = false;
	 { StorageManager::ReadGuard rg(_storageRef); opened = _storageRef->h5OpenDay(fullPath); }
	 if (!opened) continue;

	 uint8_t slotOf[H5_MAX_CHANNELS], chOf[H5_MAX_CHANNELS];
	 float   scaleOf[H5_MAX_CHANNELS];
	 uint8_t nCh = 0;
	 {
	 const H5ChannelDesc* schema = _storageRef->h5ReaderSchema( );
	 nCh = _storageRef->h5ReaderChannels( );
	 for (uint8_t c = 0; c < nCh && schema; c++) {
	 slotOf[c] = (uint8_t)(schema[c].id / MAX_SENSOR_CHANNELS);
	 chOf[c]   = (uint8_t)(schema[c].id % MAX_SENSOR_CHANNELS);
	 scaleOf[c] = powf(10.0f, (float)schema[c].scaleExp);
	 }
	 }

	 /* The cursor is an epoch, so start at the block that contains it
	  * instead of decoding the whole day up to it. */
	 { StorageManager::ReadGuard rg(_storageRef); _storageRef->h5SeekTo(lastCursor); }

	 int16_t vals[H5_MAX_CHANNELS];
	 uint32_t epoch = 0;
	 uint32_t inFileCount = 0;
	 bool fileHasMore = true;
	 while (fileHasMore && batch.size( ) < limit) {
	 {
	 StorageManager::ReadGuard rg(_storageRef);
	 if (!_storageRef->h5NextRecord(epoch, vals)) { fileHasMore = false; break; }
	 }
	 inFileCount++;
	 if (epoch >= EPOCH_MIN && (nowEpoch < EPOCH_MIN || epoch <= nowEpoch + 86400UL)
	     && epoch > lastCursor) {
	 BinaryHistoryRecord rec; rec.clear( ); rec.epoch = epoch;
	 for (uint8_t c = 0; c < nCh; c++) {
	 if (vals[c] == H5_NAN_SENTINEL) continue;
	 const uint8_t slot = slotOf[c];
	 if (slot >= MAX_SENSORS) continue;
	 const float v = (float)vals[c] * scaleOf[c];
	 if (chOf[c] == CH_TEMP)       rec.sensors[slot]  = BinaryHistoryRecord::floatToI16(v);
	 else if (chOf[c] == CH_HUM)   rec.humidity[slot] = BinaryHistoryRecord::floatToI16(v);
	 else if (chOf[c] == CH_PRESS) rec.pressure       = BinaryHistoryRecord::floatToI16x10(v);
	 }
	 batch.push_back(rec);
	 if (epoch > newCursor) newCursor = epoch;
	 }
	 if ((inFileCount % 10) == 0 && fileHasMore && batch.size( ) < limit) {
	 feedWdt( ); yield( );
	 }
	 }
	 { StorageManager::ReadGuard rg(_storageRef); _storageRef->h5CloseDay( ); }
	 }

 feedWdt( );
 }

 /* Carry on into the hour still open in RAM.
  *
  * A V5 block reaches the day file only when it seals — 60 records, so once an
  * hour at the default sampling rate. Reading only .h5 meant telemetry could
  * never send anything newer than the last sealed block: a fresh device stayed
  * silent for its first 60 minutes, and in steady state every reading was
  * delivered up to an hour late. The samples are held plain in the encoder, so
  * reaching them costs a copy and no decode.
  *
  * The cursor is an epoch, so nothing is sent twice: when this block later
  * lands in the day file, the loop above skips it on `epoch > lastCursor`.
  *
  * No yield inside this walk — the history writer runs on this same core, and
  * letting it in here could seal the block while it is being read. It is at
  * most 60 records. */
 if (batch.size( ) < limit) {
 const uint8_t ramCount = _storageRef->h5RamCount( );
 const H5ChannelDesc* ramSchema = _storageRef->getH5Schema( );
 const uint8_t ramNCh = _storageRef->getH5ChannelCount( );
 if (ramCount > 0 && ramSchema && ramNCh > 0) {
 int16_t vals[H5_MAX_CHANNELS];
 uint32_t epoch = 0;
 for (uint8_t i = 0; i < ramCount && batch.size( ) < limit; i++) {
 if (!_storageRef->h5RamRecord(i, epoch, vals)) break;
 if (epoch < EPOCH_MIN) continue;
 if (nowEpoch >= EPOCH_MIN && epoch > nowEpoch + 86400UL) continue;
 if (epoch <= lastCursor) continue;

 BinaryHistoryRecord rec; rec.clear( ); rec.epoch = epoch;
 for (uint8_t c = 0; c < ramNCh; c++) {
 if (vals[c] == H5_NAN_SENTINEL) continue;
 const uint8_t slot = (uint8_t)(ramSchema[c].id / MAX_SENSOR_CHANNELS);
 const uint8_t ch   = (uint8_t)(ramSchema[c].id % MAX_SENSOR_CHANNELS);
 if (slot >= MAX_SENSORS) continue;
 const float v = (float)vals[c] * powf(10.0f, (float)ramSchema[c].scaleExp);
 if (ch == CH_TEMP)       rec.sensors[slot]  = BinaryHistoryRecord::floatToI16(v);
 else if (ch == CH_HUM)   rec.humidity[slot] = BinaryHistoryRecord::floatToI16(v);
 else if (ch == CH_PRESS) rec.pressure       = BinaryHistoryRecord::floatToI16x10(v);
 }
 batch.push_back(rec);
 if (epoch > newCursor) newCursor = epoch;
 }
 feedWdt( );
 }
 }

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

 HTTPClient http;
 WiFiClient client;

 String protocol = cfg.telEncryption ? "https://" : "http://";
 String url = protocol + String(cfg.telServer) + ":" + String(cfg.telPort) + String(cfg.telPath);
 bool connected = false;

 if (cfg.telEncryption) {
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
 } else {
 connected = http.begin(client, url);
 }

 bool success = false;
 int code = 0;

 if (connected) {
 if (cfg.telMode == TEL_MODE_JSON) http.addHeader("Content-Type", "application/json");
 else if (cfg.telMode == TEL_MODE_CSV) http.addHeader("Content-Type", "text/csv");
 else http.addHeader("Content-Type", "text/plain");

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
 _storageRef->setLastSentTimestamp(newCursor);
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
 if (cfg.telEncryption) { if (_httpSecurePtr) _httpSecurePtr->stop( ); }
 else client.stop( );

 http.end( );
 }

 return success;
}


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
 _storageRef->setLastSentTimestamp(newCursor);
 success = true;
 } else {
 LOG_CODE(LOG_WARN, "TEL", SYS_TEL_MQTT_DISC, _mqttClient.state( ),
 "MQTT connection died during publish — cursor not advanced");
 success = false;
 }
 } else if (published > 0) {
 uint32_t partialCursor = batch[published - 1].epoch;
 _storageRef->setLastSentTimestamp(partialCursor);
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
 _storageRef->setLastSentTimestamp(batch[published - 1].epoch);
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
 _storageRef->setLastSentTimestamp(newCursor);
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


void TelemetryManager::resetBackoff( ) {
 _currentBackoff = BACKOFF_MIN_MS;
 _consecutiveFails = 0;
 _backoffUntil = 0;
 _lastCheckTime = millis( ); /* interval measured from end of cycle, not start */
}

void TelemetryManager::escalateBackoff( ) {
 _consecutiveFails++;
 MetricsManager::instance( ).data( ).telRetries++;
 _backoffUntil = millis( ) + jitter(_currentBackoff);
 _lastCheckTime = millis( ); /* avoids immediate re-fire when backoff expires */

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

 bool ok;
 if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
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

 if (cfg.telMode == TEL_MODE_JSON) {
 /*
 * JSON: builds directly with stack char buffer.
 * formatLineJson writes to lineBuf (512 bytes, stack).
 * s.concat(lineBuf, len) appends without creating temporary String.
 */
 s = "[";
 char lineBuf[512];
 for (size_t i = 0; i < batch.size( ); i++) {
 if (i > 0) s.concat(',');
 int len = formatLineJsonBuf(batch[i], cfg, lineBuf, sizeof(lineBuf));
 s.concat(lineBuf, len);
 if (i % 10 == 9) { watchdog_update( ); yield( ); }
 }
 s.concat(']');
 } else if (cfg.telMode == TEL_MODE_CSV) {
 /* The header has to name every column toCsvLine emits, and toCsvLine emits
  * the fixed layout `epoch;s0..s15;h0..h15;press` — all 16 slots, active or
  * not, then all 16 humidities, then pressure. Naming only the active slots
  * produced a 7-column header over 34-column rows, so anything reading by
  * header index read the wrong values. The rows are the persisted, upload-
  * compatible format and do not change; the header was what lied. */
 s = "timestamp";
 char hdrBuf[32];
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfg.sensors[i].active && cfg.sensors[i].hwId[0])
 snprintf(hdrBuf, sizeof(hdrBuf), ";s%d_%s", i, cfg.sensors[i].hwId);
 else
 snprintf(hdrBuf, sizeof(hdrBuf), ";s%d", i);
 s.concat(hdrBuf);
 }
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfg.sensors[i].active && cfg.sensors[i].hwId[0])
 snprintf(hdrBuf, sizeof(hdrBuf), ";h%d_%s", i, cfg.sensors[i].hwId);
 else
 snprintf(hdrBuf, sizeof(hdrBuf), ";h%d", i);
 s.concat(hdrBuf);
 }
 s.concat(";press");
 s.concat('\n');
 char csvBuf[256];
 for (size_t i = 0; i < batch.size( ); i++) {
 batch[i].toCsvLine(csvBuf, sizeof(csvBuf));
 s.concat(csvBuf);
 s.concat('\n');
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
 if (gi > spanStart) s.concat(gt + spanStart, gi - spanStart);

 if (tokKind == 1) {
 s.concat(cfg.deviceName);
 } else if (tokKind == 2) {
 s.concat(macStr);
 } else { /* DATA */
 for (size_t i = 0; i < batch.size( ); i++) {
 if (i > 0 && sepLen > 0) s.concat(sep, sepLen);
 int len = formatLineCustomBuf(batch[i], cfg, lineBuf, sizeof(lineBuf));
 if (len > 0) s.concat(lineBuf, len);
 if (i % 10 == 9) { watchdog_update( ); yield( ); }
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
 if (!_pendingDirty) return;

 uint32_t lastCursor = _storageRef->getLastSentTimestamp( );

 /* The same 30-day floor collectBatch applies when the cursor is zero.
  * Without it, the count right after `tel reset` includes every record on
  * flash — including the ones the sender will never reach — so the dashboard
  * shows a backlog that can only ever shrink to a non-zero number. */
 if (lastCursor == 0) {
 uint32_t lastRecorded = _storageRef->getLastRecordedTimestamp( );
 if (lastRecorded > 86400UL * 30) lastCursor = lastRecorded - 86400UL * 30;
 }

 std::vector<String> files;
 {
 _storageRef->enterFlashReadLock( );
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 if (dir.fileName().endsWith(HISTORY_FILE_EXT)) {
 files.push_back(dir.fileName( ));
 }
 }
 _storageRef->exitFlashReadLock( );
 }


 /* L2: o corte compara apenas os 8 digitos YYYYMMDD do nome. Antes o
  * limite era montado com o sufixo ".bin" fixo e comparado contra nomes
  * que podem terminar em ".sim4" — funcionava por acaso (os digitos
  * decidem antes de o sufixo importar) e quebraria em silencio ao mudar
  * qualquer extensao. Comparar so a data torna a regra explicita. */
 char minDay[9] = "";
 if (lastCursor > 1000000000) {
 /* Same floor as collectBatch, for the same reason: a block open across
  * midnight lives in the previous day's file. Counting from the cursor's own
  * day undercounts exactly the records collectBatch used to strand, so the
  * dashboard would have agreed with the bug instead of exposing it. */
 const time_t cursorEpoch = (time_t)h5ScanFloor(
     lastCursor, h5NominalSeconds(_storageRef->getHistoryIntervalMin( )));
 struct tm timeinfo;
 localtime_r(&cursorEpoch, &timeinfo);
 snprintf(minDay, sizeof(minDay), "%04d%02d%02d",
          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
 }

 /* 32-bit accumulator, saturated on the way out. It used to be uint16_t with
  * an explicit cast on every add, so an archive holding more than 65535
  * pending records wrapped to a plausible-looking wrong number on the
  * dashboard — this bench holds ~119k. */
 uint32_t total = 0;


 /* Same reason as collectBatch — was reading 28 B raw
 * in V2 format, count was wrong (generally inflated). Reader identical
 * to collectBatch but only counts records with epoch > lastCursor. */
 for (const String& fn : files) {
 if (minDay[0] && historyDayIsBefore(fn, minDay)) continue;

 String fullPath = String(DIR_HISTORY) + "/" + fn;

 bool opened = false;
 { StorageManager::ReadGuard rg(_storageRef); opened = _storageRef->h5OpenDay(fullPath, false); }
 if (!opened) continue;

	 /* Counting is a header walk, not a decode.
	  *
	  * A V5 block header states how many records it holds and when the
	  * first one is (§3.3), and blocks are in time order. So every block
	  * whose t0 is past the cursor contributes all of its records with
	  * nothing read but its header, and every block before those
	  * contributes none. At most ONE block straddles the cursor, and it is
	  * the last one with t0 <= cursor — that is the only one decoded.
	  *
	  * A dashboard tick that used to decode every record of every day now
	  * touches ~24 headers per day plus one block. verifyPayload is off
	  * for the same reason: CRCing payloads this path never reads would
	  * put the whole file back through flash every ten seconds. */
	 uint32_t straddleT0 = 0;
	 uint8_t  straddleCount = 0;
	 bool     haveStraddle = false;
	 uint32_t walked = 0;
	 for (;;) {
	  H5DataHeader hdr;
	  const int16_t *mn = nullptr, *mx = nullptr;
	  bool got = false;
	  { StorageManager::ReadGuard rg(_storageRef); got = _storageRef->h5NextBlock(hdr, mn, mx); }
	  if (!got) break;

	  if (hdr.t0 > lastCursor) {
	   total += hdr.pre.a;
	  } else {
	   straddleT0 = hdr.t0;
	   straddleCount = hdr.pre.a;
	   haveStraddle = true;
	  }
	  if ((++walked % 20) == 0) { feedWdt( ); yield( ); }
	 }

	 if (haveStraddle && straddleCount > 1) {
	  bool ok = false;
	  { StorageManager::ReadGuard rg(_storageRef); ok = _storageRef->h5SeekTo(straddleT0); }
	  if (ok) {
	   int16_t vals[H5_MAX_CHANNELS];
	   uint32_t epoch = 0;
	   for (uint8_t r = 0; r < straddleCount; r++) {
	    bool more = false;
	    { StorageManager::ReadGuard rg(_storageRef); more = _storageRef->h5NextRecord(epoch, vals); }
	    if (!more) break;
	    if (epoch > lastCursor) total++;
	    if ((r % 20) == 19) { feedWdt( ); yield( ); }
	   }
	  }
	 }
 { StorageManager::ReadGuard rg(_storageRef); _storageRef->h5CloseDay( ); }

 feedWdt( );
 }

 /* The hour still open in RAM counts too — collectBatch sends it now, so
  * leaving it out would report zero pending while data is waiting. */
 {
 const uint8_t ramCount = _storageRef->h5RamCount( );
 int16_t vals[H5_MAX_CHANNELS];
 uint32_t epoch = 0;
 for (uint8_t i = 0; i < ramCount; i++) {
 if (!_storageRef->h5RamRecord(i, epoch, vals)) break;
 if (epoch > lastCursor) total++;
 }
 }

 _pendingEstimate = (total > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)total;
 _pendingDirty = false;
}


uint16_t TelemetryManager::getPendingEstimate( ) const {
 return _pendingEstimate;
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

/** id com prefixo da grandeza: t{hwid} / u{hwid} / p{hwid} / l{hwid},
 * fallback {letra}{slot} — a mesma convenção das chaves JSON da linha
 * convencional (formatLineJsonBuf) e do HA discovery. */
static void alarmBuildId(const AlarmRecord& rec, const SystemConfig& cfg, char* dst, size_t cap) {
	const ChannelInfo& ci = channelInfo(rec.channel);
	if (rec.slot < MAX_SENSORS && cfg.sensors[rec.slot].hwId[0]) {
		snprintf(dst, cap, "%c%s", ci.letter, cfg.sensors[rec.slot].hwId);
	} else {
		snprintf(dst, cap, "%c%u", ci.letter, (unsigned)rec.slot);
	}
}

/** Linha CSV fixa da 2ª linha: seq;ts;id;v ("err" no valor quando falha). */
static int formatAlarmCsvLine(const AlarmRecord& rec, const SystemConfig& cfg, char* dest, size_t cap) {
	const ChannelInfo& ci = channelInfo(rec.channel);
	char idBuf[24];
	alarmBuildId(rec, cfg, idBuf, sizeof(idBuf));
	if (rec.flags & ALARM_FLAG_ERR) {
		return snprintf(dest, cap, "%u;%lu;%s;err", (unsigned)rec.seq,
		                (unsigned long)rec.epoch, idBuf);
	}
	return snprintf(dest, cap, "%u;%lu;%s;%.*f", (unsigned)rec.seq,
	                (unsigned long)rec.epoch, idBuf,
	                (int)ci.display.decimals, (double)((float)rec.value / ci.scale));
}

uint16_t TelemetryManager::pushAlarm(uint8_t slot, uint8_t channel, float value, bool err) {
	if (!_alarmEnabled || slot >= MAX_SENSORS) return 0;

	int16_t scaled;
	if (err) {
		scaled = HIST_NAN_SENTINEL;
	} else {
		const ChannelInfo& ci = channelInfo(channel);
		float s = value * ci.scale;
		if (s > 32767.0f) s = 32767.0f;
		if (s < -32767.0f) s = -32767.0f;
		scaled = (int16_t)lroundf(s);
	}

	uint16_t seq = _alarmQueue.push((uint32_t)time(nullptr), slot, channel, scaled, err);
	auto& m = MetricsManager::instance( ).data( );
	if (seq != 0) {
		m.alarmQueued++;
		if (err) m.alarmErrRecords++;
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

	std::vector<AlarmRecord> batch;
	{
		AlarmRecord tmp[ALARM_QUEUE_MAX];
		uint8_t n = _alarmQueue.snapshot(tmp, ALARM_BATCH_MAX);
		batch.reserve(n);
		for (uint8_t i = 0; i < n; i++) batch.push_back(tmp[i]);
	}
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

	bool success;
	if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
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

	if (mode == TEL_MODE_JSON) {
		/* Mesma regra da linha convencional: JSON ignora o template global
		 * e emite um array de linhas. */
		s = "[";
		char lineBuf[512];
		for (size_t i = 0; i < batch.size( ); i++) {
			if (i > 0) s.concat(',');
			int len = formatLineAlarmBuf(batch[i], cfg, lineBuf, sizeof(lineBuf));
			s.concat(lineBuf, len);
			if (i % 10 == 9) { watchdog_update( ); yield( ); }
		}
		s.concat(']');
	} else if (mode == TEL_MODE_CSV) {
		s = "seq;ts;id;v";
		s.concat('\n');
		char csvBuf[64];
		for (size_t i = 0; i < batch.size( ); i++) {
			int len = formatAlarmCsvLine(batch[i], cfg, csvBuf, sizeof(csvBuf));
			if (len > 0) s.concat(csvBuf, len);
			s.concat('\n');
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
		while (gi < gtLen) {
			if (gt[gi] != '{') { gi++; continue; }
			const size_t remaining = gtLen - gi;
			size_t tokLen = 0;
			int tokKind = 0; /* 1=DEV, 2=MAC, 3=DATA */
			if (remaining >= 5 && memcmp(gt + gi, "{DEV}", 5) == 0) { tokKind = 1; tokLen = 5; }
			else if (remaining >= 5 && memcmp(gt + gi, "{MAC}", 5) == 0) { tokKind = 2; tokLen = 5; }
			else if (remaining >= 6 && memcmp(gt + gi, "{DATA}", 6) == 0) { tokKind = 3; tokLen = 6; }
			if (tokKind == 0) { gi++; continue; }
			if (gi > spanStart) s.concat(gt + spanStart, gi - spanStart);
			if (tokKind == 1) {
				s.concat(cfg.deviceName);
			} else if (tokKind == 2) {
				s.concat(macStr);
			} else {
				for (size_t i = 0; i < batch.size( ); i++) {
					if (i > 0 && sepLen > 0) s.concat(sep, sepLen);
					int len = formatLineAlarmBuf(batch[i], cfg, lineBuf, sizeof(lineBuf));
					if (len > 0) s.concat(lineBuf, len);
					if (i % 10 == 9) { watchdog_update( ); yield( ); }
				}
			}
			gi += tokLen;
			spanStart = gi;
		}
		if (gtLen > spanStart) s.concat(gt + spanStart, gtLen - spanStart);
	}
	return s;
}

/** Formata uma linha de alarme pelo template cfg.alarmTel.lineTemplate.
 * Tokens: {TS} {ID} {HWID} {SLOT} {CH} {VAL} {ERR} {SEQ} — com aliases
 * minúsculos {val} {err} {seq}. {VAL} é o número (decimais do canal) ou VAZIO
 * em falha; {ERR} é o literal "err" ou VAZIO. As formas compostas
 * "<chave>":{VAL}/{ERR}/{SEQ} (chave == grafia do token) removem a chave
 * inteira quando o token está ausente — a mesma máquina da linha convencional
 * (formatLineCustomBuf) — que é o que deixa o template default produzir JSON
 * válido nos dois casos:
 *   ok:  {"ts":...,"id":"tX","val":25.30,"seq":1}
 *   err: {"ts":...,"id":"tX","err":"err","seq":2}
 */
int TelemetryManager::formatLineAlarmBuf(const AlarmRecord& rec, const SystemConfig& cfg,
                                         char* dest, size_t cap) {
	if (cap == 0) return 0;
	dest[0] = '\0';

	char tsBuf[16];
	snprintf(tsBuf, sizeof(tsBuf), "%lu", (unsigned long)rec.epoch);
	char seqBuf[8];
	snprintf(seqBuf, sizeof(seqBuf), "%u", (unsigned)rec.seq);
	char slotBuf[4];
	snprintf(slotBuf, sizeof(slotBuf), "%u", (unsigned)rec.slot);
	const ChannelInfo& ci = channelInfo(rec.channel);

	char idBuf[24];
	alarmBuildId(rec, cfg, idBuf, sizeof(idBuf));
	char hwidBuf[17] = {0};
	if (rec.slot < MAX_SENSORS) strlcpy(hwidBuf, cfg.sensors[rec.slot].hwId, sizeof(hwidBuf));
	char chBuf[2];
	chBuf[0] = ci.letter; chBuf[1] = '\0';

	const bool isErr = (rec.flags & ALARM_FLAG_ERR) != 0;
	char valBuf[16] = "";
	char errBuf[8] = "";
	if (isErr) {
		strlcpy(errBuf, "err", sizeof(errBuf));
	} else if (rec.value != HIST_NAN_SENTINEL) {
		snprintf(valBuf, sizeof(valBuf), "%.*f",
		         (int)ci.display.decimals, (double)((float)rec.value / ci.scale));
	}

	const char* tpl = cfg.alarmTel.lineTemplate;
	const size_t tplLen = strnlen(tpl, sizeof(cfg.alarmTel.lineTemplate));
	size_t di = 0;
	size_t ti = 0;

	while (ti < tplLen && di + 1 < cap) {
		char c = tpl[ti];
		if (c != '{') { dest[di++] = c; ti++; continue; }

		const size_t remaining = tplLen - ti;
		const char* val = nullptr;
		char compKey[8] = {0};
		size_t compKeyLen = 0;
		size_t tokenChars = 0;

		if (remaining >= 4 && memcmp(tpl + ti, "{TS}", 4) == 0) {
			val = tsBuf; tokenChars = 4;
		} else if (remaining >= 4 && memcmp(tpl + ti, "{ID}", 4) == 0) {
			val = idBuf; tokenChars = 4;
		} else if (remaining >= 6 && memcmp(tpl + ti, "{HWID}", 6) == 0) {
			val = hwidBuf; tokenChars = 6;
		} else if (remaining >= 6 && memcmp(tpl + ti, "{SLOT}", 6) == 0) {
			val = slotBuf; tokenChars = 6;
		} else if (remaining >= 4 && memcmp(tpl + ti, "{CH}", 4) == 0) {
			val = chBuf; tokenChars = 4;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{VAL}", 5) == 0) {
			val = valBuf;
			compKeyLen = 3; memcpy(compKey, "VAL", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{val}", 5) == 0) {
			/* alias minúsculo — o compKey segue a grafia do token, então a
			 * forma composta "val":{val} remove a chave "val" */
			val = valBuf;
			compKeyLen = 3; memcpy(compKey, "val", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{ERR}", 5) == 0) {
			val = errBuf;
			compKeyLen = 3; memcpy(compKey, "ERR", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{err}", 5) == 0) {
			val = errBuf;
			compKeyLen = 3; memcpy(compKey, "err", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{SEQ}", 5) == 0) {
			val = seqBuf;
			compKeyLen = 3; memcpy(compKey, "SEQ", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{seq}", 5) == 0) {
			val = seqBuf;
			compKeyLen = 3; memcpy(compKey, "seq", 4);
			tokenChars = 5;
		}

		if (tokenChars == 0) {
			/* '{' sem token conhecido: emite literal, avança 1 */
			dest[di++] = c;
			ti++;
			continue;
		}

		/* Forma composta "<compKey>":{<tok>} — só quando o token a suporta. */
		bool matchedBare = false;
		if (compKeyLen > 0) {
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

		if (matchedBare) {
			if (val && val[0] != '\0') {
				size_t vl = strlen(val);
				if (di + vl >= cap) vl = cap - 1 - di;
				memcpy(dest + di, val, vl);
				di += vl;
			} else {
				/* valor ausente: desfaz a chave já emitida */
				const size_t undo = compKeyLen + 3;
				if (di >= undo) di -= undo;
			}
		} else {
			const char* emit = (val && val[0] != '\0') ? val : "";
			size_t el = strlen(emit);
			if (di + el >= cap) el = cap - 1 - di;
			memcpy(dest + di, emit, el);
			di += el;
		}

		ti += tokenChars;
	}

	/* Limpeza in-place idêntica à linha convencional: ",," "{," ",}" etc. */
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

bool TelemetryManager::attemptAlarmHttpUpload(String& payload, std::vector<AlarmRecord>& batch) {
	SystemConfig &cfg = _storageRef->getConfig( );
	feedWdt( );

	HTTPClient http;
	WiFiClient client;
	String protocol = cfg.telEncryption ? "https://" : "http://";
	String url = protocol + String(cfg.telServer) + ":" + String(cfg.telPort) + alarmHttpPath(cfg);
	bool connected = false;

	if (cfg.telEncryption) {
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
	} else {
		connected = http.begin(client, url);
	}

	bool success = false;
	int code = 0;

	if (connected) {
		if (cfg.alarmTel.mode == TEL_MODE_JSON) http.addHeader("Content-Type", "application/json");
		else if (cfg.alarmTel.mode == TEL_MODE_CSV) http.addHeader("Content-Type", "text/csv");
		else http.addHeader("Content-Type", "text/plain");
		addTelemetryAuthHeader(http, cfg);

		http.setTimeout(NET_SOCKET_TIMEOUT_MS);
		feedWdt( );

		uint32_t postStart = millis( );
		{ code = http.POST(payload); }
		uint32_t postLatency = millis( ) - postStart;
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

		if (cfg.telEncryption) { if (_httpSecurePtr) _httpSecurePtr->stop( ); }
		else client.stop( );
		http.end( );
	}

	return success;
}

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
