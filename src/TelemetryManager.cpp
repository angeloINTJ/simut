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
#include "HistoryCodec.h"
#include <LittleFS.h>
#include <algorithm>
#include <string.h>
#include <hardware/watchdog.h>
#include <pico/time.h>

/**
 * @brief Feeds the watchdog during blocking network operations (TLS/HTTP/MQTT).
 *
 * http.POST() with TLS can block for 4-8s on a healthy network. On degraded
 * networks (low RSSI, 2G, roaming), handshake + transfer can
 * legitimately extend to tens of seconds. Without feeding, the
 * watchdog (8.3s) fires during a normal operation.
 *
 * The timer runs every 2s and feeds while the guard is active.
 * Safety: stops feeding after WDT_FEED_MAX_WINDOW_MS (60s) to
 * avoid masking real deadlocks — at that point, the watchdog takes action
 * as the final safety net. HTTP/MQTT internals already have
 * NET_SOCKET_TIMEOUT_MS=4s, so healthy operations don't reach 60s.
 */
static volatile bool _telGuardActive = false;
static volatile uint32_t _telGuardStartMs = 0;
static struct repeating_timer _telGuardTimer;
static bool _telGuardTimerStarted = false;

static bool _telGuardCallback(struct repeating_timer *t) {
 (void)t;
 if (_telGuardActive) {
 uint32_t elapsed = millis( ) - _telGuardStartMs;
 if (elapsed < WDT_FEED_MAX_WINDOW_MS) {
 watchdog_update( );
 }
 }
 return true;
}

struct TelemetryGuard {
 TelemetryGuard( ) {
 if (!_telGuardTimerStarted) {
 add_repeating_timer_ms(-2000, _telGuardCallback, nullptr, &_telGuardTimer);
 _telGuardTimerStarted = true;
 }
 _telGuardStartMs = millis( );
 _telGuardActive = true;
 }
 ~TelemetryGuard( ) { _telGuardActive = false; }
};

TelemetryManager::TelemetryManager( )
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
 }


 if (cfg.telTransport == TEL_TRANSPORT_MQTT) {
 if (cfg.telEncryption) {
 _mqttSecurePtr = new WiFiClientSecure( );
 if (_mqttSecurePtr) {
 _mqttSecurePtr->setTimeout(NET_SOCKET_TIMEOUT_MS);
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
void TelemetryManager::update( ) {
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
 * RAII: extends WDT context to 120s during the cycle. TelemetryGuard only
 * covers http.POST (handshake/cleanup were exposed). Context-aware:
 * nested saves/logs don't reduce the window to 8.3s during telemetry.
 * Auto-restore on any exit path (normal or early return).
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
 * contiguous. Conservative: requires freeH >= 24K (covers TLS 16K + margin). */
 if (freeH < 24576) {
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
 * - update() preflight: aborts if heap < 24 KB
 * - buildPayload: dynamic resize if estimate exceeds available
 * - TelemetryGuard: feeds WDT during POST up to 60s (covers large POSTs)
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
 * BYTES_PER_ENTRY = 350: empirically measured — ~28 batch struct +
 * ~310 JSON payload (conservative vs real ~221 for long hwId sensors).
 *
 * HARD_CAP = 50: payload 50 entries ~= 17.5 KB, fits + TLS scratch.
 */
 /* HEAP_RESERVE HTTPS increased 24K → 32K. Stress test
 * showed lbm=17564 mid-POST (largest block fragmented below TLS
 * scratch ~16K) → 4 reboots in 10 min with batch=200. 32K reserve guarantees
 * margin even after accumulated fragmentation from consecutive batches.
 * HARD_CAP 100 → 50 for the same reason: smaller batch, smaller payload
 * (50 entries × 350 B = 17.5 KB vs 100 × 350 = 35 KB), reduces fragmentation. */
 const uint32_t HEAP_RESERVE = cfg.telEncryption ? 32768 : 12288;
 const uint32_t BYTES_PER_ENTRY = 350;
 const uint8_t HARD_CAP = 50;

 if (freeHeap <= HEAP_RESERVE) return 1;

 uint32_t heapLimit32 = (freeHeap - HEAP_RESERVE) / BYTES_PER_ENTRY;
 uint8_t heapLimit = (heapLimit32 > 255) ? 255 : (uint8_t)heapLimit32;
 return max((uint8_t)1, min(min(configured, HARD_CAP), heapLimit));
}

bool TelemetryManager::collectBatch(std::vector<BinaryHistoryRecord>& batch, uint32_t& newCursor) {
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
 if (dir.fileName( ).endsWith(HISTORY_FILE_EXT)) {
 files.push_back(dir.fileName( ));
 }
 }
 _storageRef->exitFlashReadLock( );
 }
 std::sort(files.begin( ), files.end( ));

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


 /* Codec V2 (delta + anchor). Replaces the raw 28-byte read
 * (V1 format) that was silently broken after migration. Reader follows
 * the pattern used in StorageManager::getLastRecorded
 * and WebManager::handleApiHistoryData. */
 const uint32_t EPOCH_MIN = 1700000000UL;

 for (const String& fn : files) {
 if (batch.size( ) >= limit) break;
 if (minFileName.length( ) > 0 && fn < minFileName) continue;

 String fullPath = String(DIR_HISTORY) + "/" + fn;

 _storageRef->enterFlashReadLock( );
 File f = LittleFS.open(fullPath, "r");
 if (!f) { _storageRef->exitFlashReadLock( ); continue; }

 /* V2 header */
 HistoryFileHeaderV2 hdr;
 bool headerOk = false;
 if (f.size( ) >= HIST_V2_HEADER_SIZE) {
 f.seek(0);
 if (f.read((uint8_t*)&hdr, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
 headerOk = (memcmp(hdr.magic, HIST_V2_MAGIC, 4) == 0 &&
 (hdr.version == HIST_V2_VERSION || hdr.version == HIST_V3_VERSION) &&
 hdr.anchorPeriod > 0);
 }
 }
 if (!headerOk) {
 /* Legacy or corrupted V1 file: silent skip. V1→V2 migration
 * is offline (tools/history_v1_to_v2.py). */
 f.close( );
 _storageRef->exitFlashReadLock( );
 continue;
 }

 HistoryCodecState rdState;
 historyCodecReset(rdState);
 rdState.fileVersion = hdr.version; /* MUST set before decode — auto-detect unreliable */
 uint8_t rdBuf[256];
 size_t rdFilled = 0;
 uint32_t inFileCount = 0;
 bool fileHasMore = true;

 while (fileHasMore && batch.size( ) < limit) {
 BinaryHistoryRecord rec;

 if (rdFilled < HIST_V2_MAX_DELTA_SIZE && f.available( ) > 0) {
 int rN = f.read(rdBuf + rdFilled, sizeof(rdBuf) - rdFilled);
 if (rN > 0) rdFilled += (size_t)rN;
 }
 if (rdFilled == 0) { fileHasMore = false; break; }

 bool isAnchor = (rdState.recordsSinceAnchor == 0) ||
 (rdState.recordsSinceAnchor == hdr.anchorPeriod);
 size_t consumed = historyDecodeRecord(rdBuf, rdFilled, rdState, rec, isAnchor);
 if (consumed == 0) {
 /* Decode failed: codec corrupted or truncated. Skip rest of file. */
 fileHasMore = false;
 break;
 }
 memmove(rdBuf, rdBuf + consumed, rdFilled - consumed);
 rdFilled -= consumed;
 inFileCount++;

 /* Defensive: ignores records with absurd epoch (before 2023-11
 * or in the future beyond 1 day). Same logic as the old V1 reader. */
 bool epochValid = (rec.epoch >= EPOCH_MIN) &&
 (nowEpoch < EPOCH_MIN || rec.epoch <= nowEpoch + 86400UL);
 if (epochValid && rec.epoch > lastCursor) {
 batch.push_back(rec);
 if (rec.epoch > newCursor) newCursor = rec.epoch;
 }

 /* Periodic WDT feed + yield every 10 decoded records, avoiding
 * holding the lock too long on large files. */
 if ((inFileCount % 10) == 0 && fileHasMore && batch.size( ) < limit) {
 _storageRef->exitFlashReadLock( );
 feedWdt( );
 yield( );
 _storageRef->enterFlashReadLock( );
 }
 }
 f.close( );
 _storageRef->exitFlashReadLock( );

 feedWdt( );
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

 /* Bound TLS handshake. Default lib >10s; server slow/dying
 * mid-handshake won't blow WDT_FEED_MAX_WINDOW (60s). Static method,
 * affects all subsequent WiFiClientSecure creation. */
 WiFiClientSecure::setTLSConnectTimeout(NET_SOCKET_TIMEOUT_MS);

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
 TelemetryGuard tg; /* Feeds watchdog during blocking POST */
 code = http.POST(payload);
 }
 uint32_t postLatency = millis( ) - postStart;
 watchdog_update( );

 if (code > 0) {
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_SENT, code, "HTTP OK: " + String(payload.length( )) + " bytes, code " + String(code));
 if (code >= 200 && code < 300) {
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
 }
 } else {
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, code, String(TRL("HTTP error: ")) + http.errorToString(code));
 MetricsManager::instance( ).data( ).telFailed++;
 }
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
 feedWdt( );

 bool connected = false;
 String user = String(cfg.mqttUser);
 String pass = String(cfg.mqttPass);
 user.trim( );
 pass.trim( );

 {
 TelemetryGuard tg; /* Feeds watchdog during blocking connect */
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

 if (!mqttEnsureConnected( )) return false;

 SystemConfig &cfg = _storageRef->getConfig( );
 String topic = String(cfg.mqttTopic);
 topic.trim( );
 if (topic.length( ) == 0) topic = "simut/data";


 bool success = false;

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

 if (ok) published++;
 else break;

 _mqttClient.loop( );
 }

 if (published > 0) {
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_PUB, published,
 "MQTT published " + String(published) + "/" + String(batch.size( )) + " items to " + topic);
 }

 if (published == (int)batch.size( )) {
 _storageRef->setLastSentTimestamp(newCursor);
 success = true;
 } else if (published > 0) {
 uint32_t partialCursor = batch[published - 1].epoch;
 _storageRef->setLastSentTimestamp(partialCursor);
 success = false;
 }
 } else {
 /* Large batch: uses payload pre-built by caller */
 feedWdt( );

 if (payload.length( ) > _mqttClient.getBufferSize( )) {
 uint16_t needed = min((size_t)8192, payload.length( ) + 64);
 _mqttClient.setBufferSize(needed);
 }

 bool ok;
 {
 TelemetryGuard tg; /* Feeds watchdog during blocking publish */
 ok = _mqttClient.publish(
 topic.c_str( ),
 payload.c_str( ),
 cfg.mqttRetain
 );
 }

 if (ok) {
 LOG_CODE(LOG_INFO, "TEL", SYS_TEL_MQTT_PUB, batch.size( ),
 "MQTT batch OK: " + String(batch.size( )) + " items (" + String(payload.length( )) + " bytes)");
 _storageRef->setLastSentTimestamp(newCursor);
 success = true;
 } else {
 LOG_CODE(LOG_ERROR, "TEL", SYS_TEL_FAIL, _mqttClient.state( ),
 "MQTT publish failed (payload " + String(payload.length( )) + " bytes)");
 }
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
 SystemConfig &cfg = _storageRef->getConfig( );

 /* Estimate size: JSON ~300 bytes/record with 12 sensors */
 size_t perLine = (cfg.telMode == TEL_MODE_CSV) ? 120 : 300;
 size_t estimatedSize = batch.size( ) * perLine + 256;

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
 estimatedSize = batch.size( ) * perLine + 256;
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
 const char* hwid = cfg.sensors[i].hwId;
 if (hwid[0] == '\0') {
 snprintf(tmp, sizeof(tmp), ",\"t%d\":%.2f", i, BinaryHistoryRecord::i16ToFloat(rec.sensors[i]));
 } else {
 snprintf(tmp, sizeof(tmp), ",\"t%s\":%.2f", hwid, BinaryHistoryRecord::i16ToFloat(rec.sensors[i]));
 }
 pos += strlcat(dest + pos, tmp, maxLen - pos);
 }
 if (cfg.sensors[i].active && rec.humidity[i] != HIST_NAN_SENTINEL) {
 const char* hwid = cfg.sensors[i].hwId;
 if (hwid[0] == '\0') {
 snprintf(tmp, sizeof(tmp), ",\"u%d\":%.1f", i, BinaryHistoryRecord::i16ToFloat(rec.humidity[i]));
 } else {
 snprintf(tmp, sizeof(tmp), ",\"u%s\":%.1f", hwid, BinaryHistoryRecord::i16ToFloat(rec.humidity[i]));
 }
 pos += strlcat(dest + pos, tmp, maxLen - pos);
 }
 }
 if (rec.pressure != HIST_NAN_SENTINEL) {
 snprintf(tmp, sizeof(tmp), ",\"pAMB\":%.1f", BinaryHistoryRecord::i16ToFloatx10(rec.pressure));
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
 String bs = _storageRef->getBoardSerialNumber( );
 strlcpy(boardSerial, bs.c_str( ), sizeof(boardSerial));
 }

 char ambTBuf[16] = {0};
 char ambHBuf[16] = {0};
 char pressBuf[16] = {0};
 const bool hasAmbT = (rec.ambientTemp != HIST_NAN_SENTINEL);
 const bool hasAmbH = (rec.ambientHum != HIST_NAN_SENTINEL);
 const bool hasPress = (rec.pressure != HIST_NAN_SENTINEL);
 if (hasAmbT) snprintf(ambTBuf, sizeof(ambTBuf), "%.2f", BinaryHistoryRecord::i16ToFloat(rec.ambientTemp));
 if (hasAmbH) snprintf(ambHBuf, sizeof(ambHBuf), "%.1f", BinaryHistoryRecord::i16ToFloat(rec.ambientHum));
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
 } else if (remaining >= 6 && memcmp(tpl + ti, "{tAMB}", 6) == 0) {
 val = hasAmbT ? ambTBuf : nullptr;
 /* Custom ID via calib.csv (line `t<id>`) overrides
 * the default picoUID. Detect "custom" as hwId != "AMB"
 * (default from loadDefaults). Without calib, fallback boardSerial preserves
 * compat with already-configured dashboards. */
 hwid = (cfg.sensors[10].active && cfg.sensors[10].hwId[0] != '\0' && strcmp(cfg.sensors[10].hwId, "AMB") != 0)
 ? cfg.sensors[10].hwId : boardSerial;
 memcpy(compKey, "tAMB", 4); compKeyLen = 4;
 tokenChars = 6; tokenValid = true;
 } else if (remaining >= 6 && memcmp(tpl + ti, "{uAMB}", 6) == 0) {
 val = hasAmbH ? ambHBuf : nullptr;
 hwid = (cfg.sensors[10].active && cfg.sensors[10].hwId[0] != '\0' && strcmp(cfg.sensors[10].hwId, "AMB") != 0)
 ? cfg.sensors[10].hwId : boardSerial;
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
 } else if (remaining >= 7 && memcmp(tpl + ti, "{pAMB}", 7) == 0) {
 val = hasPress ? pressBuf : nullptr;
 tokenChars = 7; tokenValid = true;
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


 std::vector<String> files;
 {
 _storageRef->enterFlashReadLock( );
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 if (dir.fileName( ).endsWith(HISTORY_FILE_EXT)) {
 files.push_back(dir.fileName( ));
 }
 }
 _storageRef->exitFlashReadLock( );
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


 /* Same reason as collectBatch — was reading 28 B raw
 * in V2 format, count was wrong (generally inflated). Reader identical
 * to collectBatch but only counts records with epoch > lastCursor. */
 for (const String& fn : files) {
 if (minFileName.length( ) > 0 && fn < minFileName) continue;

 String fullPath = String(DIR_HISTORY) + "/" + fn;

 _storageRef->enterFlashReadLock( );
 File f = LittleFS.open(fullPath, "r");
 if (!f) { _storageRef->exitFlashReadLock( ); continue; }

 HistoryFileHeaderV2 hdr;
 bool headerOk = false;
 if (f.size( ) >= HIST_V2_HEADER_SIZE) {
 f.seek(0);
 if (f.read((uint8_t*)&hdr, HIST_V2_HEADER_SIZE) == HIST_V2_HEADER_SIZE) {
 headerOk = (memcmp(hdr.magic, HIST_V2_MAGIC, 4) == 0 &&
 (hdr.version == HIST_V2_VERSION || hdr.version == HIST_V3_VERSION) &&
 hdr.anchorPeriod > 0);
 }
 }
 if (!headerOk) { f.close( ); _storageRef->exitFlashReadLock( ); continue; }

 HistoryCodecState rdState;
 historyCodecReset(rdState);
 rdState.fileVersion = hdr.version; /* MUST set before decode — auto-detect unreliable */
 uint8_t rdBuf[256];
 size_t rdFilled = 0;

 while (true) {
 BinaryHistoryRecord rec;

 if (rdFilled < HIST_V2_MAX_DELTA_SIZE && f.available( ) > 0) {
 int rN = f.read(rdBuf + rdFilled, sizeof(rdBuf) - rdFilled);
 if (rN > 0) rdFilled += (size_t)rN;
 }
 if (rdFilled == 0) break;

 bool isAnchor = (rdState.recordsSinceAnchor == 0) ||
 (rdState.recordsSinceAnchor == hdr.anchorPeriod);
 size_t consumed = historyDecodeRecord(rdBuf, rdFilled, rdState, rec, isAnchor);
 if (consumed == 0) break;
 memmove(rdBuf, rdBuf + consumed, rdFilled - consumed);
 rdFilled -= consumed;

 if (rec.epoch > lastCursor) total++;
 }
 f.close( );
 _storageRef->exitFlashReadLock( );

 feedWdt( );
 }

 _pendingEstimate = total;
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
