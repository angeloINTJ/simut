/**
 * @file WebManager_Metrics.cpp
 * @brief GET /metrics — Prometheus text exposition endpoint.
 * @details Everything served here already existed in RAM for /api/status or
 * `show metrics`; this route only spells it in the format a Prometheus
 * scraper pulls. Nothing is stored and nothing retries: the device answers
 * the instant snapshot and the scraper owns retention, graphing and alerting.
 *
 * Auth: a scraper cannot run the login flow, so besides the cookie session
 * the route accepts HTTP Basic against the existing user table, requiring
 * the same PERM_DASHBOARD bit /api/status requires. Failed credentials feed
 * the SAME per-IP exponential lockout as the login form (ensureLoginStateSlot
 * + applyExponentialPenalty) — this route must not become the cheap door for
 * a brute force the login already rate-limits. Successful scrapes are not
 * audit-logged: at one line per scrape interval they would bury the log.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "WebManager.h"
#include "MetricsManager.h"
#include "FlashIrqProbe.h"
#include "B64Decode.h"
#include "PromMetrics.h"
#include <LittleFS.h>

/**
 * @brief Effective permissions for /metrics.
 *
 * @return >0 permission mask; 0 unauthenticated/bad credentials (caller
 * answers 401); -1 a response was already sent here (active lockout or
 * every login slot locked — 429/403 with Retry-After semantics).
 */
int WebManager::metricsAuthPerms( ) {
	/* A logged-in browser session works as-is. */
	uint16_t perms = getAuthPerms( );
	if (perms) return (int)perms;

	String h = _server->header("Authorization");
	if (!h.startsWith("Basic ")) return 0;

	uint32_t clientIP = (uint32_t)_server->client( ).remoteIP( );
	int ls = findLoginStateForIp(clientIP);
	if (respondIfLockedOut(ls, 429)) return -1;

	/* user[16] + ':' + raw password (<=128) — 160 covers it. */
	char creds[160];
	if (b64Decode(h.c_str( ) + 6, creds, sizeof(creds)) < 0) return 0;
	char* colon = strchr(creds, ':');
	if (!colon) return 0;
	*colon = '\0';
	const char* user = creds;
	const char* pass = colon + 1;
	if (!isValidName(user, 31) || strlen(pass) > 128) return 0;

	/* The user store holds hashes of sha256(typed) — the browser hashes
	 * client-side before POSTing. Basic auth carries the raw password, so
	 * hash it here to meet verifyPasswordFor on the same ground. */
	int foundId = verifyPasswordFor(String(user), _storageRef->sha256Hex(String(pass)));
	if (foundId < 0) {
		if (ls < 0) ls = ensureLoginStateSlot(clientIP);
		if (ls >= 0) applyExponentialPenalty(ls);
		LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0,
		         String(TRL("Login Failed: ")) + user);
		return 0;
	}

	SystemConfig& cfg = _storageRef->getConfig( );
	return (int)cfg.users[foundId].permissions;
}

void WebManager::handleMetrics( ) {
	int auth = metricsAuthPerms( );
	if (auth < 0) return; /* lockout answer already sent */
	if (auth == 0) {
		_server->sendHeader("WWW-Authenticate", "Basic realm=\"SIMUT metrics\"");
		_server->send(401, "text/plain", "unauthorized\n");
		return;
	}
	if (!((uint16_t)auth & PERM_DASHBOARD)) {
		_server->send(403, "text/plain", "forbidden\n");
		return;
	}

	SystemConfig& cfg = _storageRef->getConfig( );

	/* Same refresh discipline as /api/status: heap samples cost ~16
	 * malloc/free, FS info is cached for 10 s. */
	MetricsManager& mm = MetricsManager::instance( );
	mm.sampleHeap( );
	mm.sampleLargestBlock( );
	const int liveRssi = _netRef->getRssi( );
	mm.observeRssi(liveRssi);
	const SystemMetrics& mt = mm.data( );

	if (timeSince(_lastFsInfoRefresh, 10000) || _cachedFsTotalBytes == 0) {
		StorageManager::ReadGuard rg(_storageRef);
		FSInfo fs_info;
		fs_info.totalBytes = 0;
		fs_info.usedBytes = 0;
		LittleFS.info(fs_info);
		_cachedFsTotalBytes = fs_info.totalBytes;
		_cachedFsUsedBytes = fs_info.usedBytes;
		_lastFsInfoRefresh = millis( );
	}

	_server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
	_server->send(200, "text/plain; version=0.0.4; charset=utf-8", "");

	/* Accumulate lines and flush in ~1 KB chunks — one TCP chunk per line
	 * would triple the wire overhead of a ~2.5 KB body. */
	char acc[1024];
	size_t used = 0;
	bool dead = false;
	auto flush = [&]( ) {
		if (dead || used == 0) return;
		if (!safeSend(acc, used)) dead = true;
		used = 0;
	};
	auto add = [&](const char* line, int n) {
		if (dead || n <= 0) return;
		if ((size_t)n >= sizeof(acc)) return; /* single line larger than acc: drop */
		if (used + (size_t)n >= sizeof(acc)) flush( );
		if (dead) return;
		memcpy(acc + used, line, (size_t)n);
		used += (size_t)n;
	};
	char ln[192];
	auto emitType = [&](const char* name, const char* type) {
		add(ln, PromMetrics::typeLine(ln, sizeof(ln), name, type));
	};
	auto emitU32 = [&](const char* name, const char* type, uint32_t v) {
		emitType(name, type);
		add(ln, PromMetrics::lineU32(ln, sizeof(ln), name, "", v));
	};

	/* ── device ── */
	{
		char devEsc[64];
		PromMetrics::escapeLabel(cfg.deviceName, devEsc, sizeof(devEsc));
		char labels[96];
		snprintf(labels, sizeof(labels), "version=\"%s\",device=\"%s\"",
		         SIMUT_VERSION, devEsc);
		emitType("simut_build_info", "gauge");
		add(ln, PromMetrics::lineU32(ln, sizeof(ln), "simut_build_info", labels, 1));
	}
	emitU32("simut_uptime_seconds", "gauge", millis( ) / 1000U);
	emitU32("simut_ntp_synced", "gauge", _netRef->isTimeSynced( ) ? 1 : 0);

	/* ── memory / storage ── */
	emitU32("simut_heap_free_bytes", "gauge", rp2040.getFreeHeap( ));
	emitU32("simut_heap_total_bytes", "gauge", rp2040.getTotalHeap( ));
	uint32_t hmin = (mt.heapMinSeen == 0xFFFFFFFFU) ? mt.heapFreeNow : mt.heapMinSeen;
	emitU32("simut_heap_min_bytes", "gauge", hmin);
	emitU32("simut_heap_largest_block_bytes", "gauge", mt.heapLargestBlock);
	uint32_t lbmin = (mt.heapLargestMin == 0xFFFFFFFFU) ? mt.heapLargestBlock : mt.heapLargestMin;
	emitU32("simut_heap_largest_block_min_bytes", "gauge", lbmin);
	emitU32("simut_fs_used_bytes", "gauge", _cachedFsUsedBytes);
	emitU32("simut_fs_total_bytes", "gauge", _cachedFsTotalBytes);

	/* ── network ── */
	emitU32("simut_wifi_connected", "gauge", _netRef->isNetworkHealthy( ) ? 1 : 0);
	emitType("simut_wifi_rssi_dbm", "gauge");
	add(ln, PromMetrics::lineI32(ln, sizeof(ln), "simut_wifi_rssi_dbm", "", liveRssi));
	emitU32("simut_wifi_reconnects_total", "counter", mt.wifiReconnects);
	emitU32("simut_mqtt_reconnects_total", "counter", mt.mqttReconnects);
	emitU32("simut_mqtt_connected", "gauge",
	        (_telemetryRef && _telemetryRef->isMqttConnected( )) ? 1 : 0);

	/* ── telemetry ── */
	emitU32("simut_telemetry_enabled", "gauge", (cfg.telInterval > 0) ? 1 : 0);
	if (_telemetryRef)
		emitU32("simut_telemetry_pending_records", "gauge", _telemetryRef->getPendingEstimate( ));
	emitU32("simut_telemetry_sent_total", "counter", mt.telSent);
	emitU32("simut_telemetry_failed_total", "counter", mt.telFailed);
	emitU32("simut_telemetry_retries_total", "counter", mt.telRetries);
	emitU32("simut_telemetry_sent_bytes_total", "counter", mt.telTotalBytes);
	emitType("simut_telemetry_last_latency_seconds", "gauge");
	add(ln, PromMetrics::lineF(ln, sizeof(ln), "simut_telemetry_last_latency_seconds",
	                           "", mt.telLastLatencyMs / 1000.0, 3));

	/* ── 2ª linha (alarmes, v21) ── */
	emitU32("simut_alarm_line_enabled", "gauge", cfg.alarmTel.enabled ? 1 : 0);
	if (_telemetryRef)
		emitU32("simut_alarm_pending_records", "gauge", _telemetryRef->alarmQueueSize( ));
	emitU32("simut_alarm_queued_total", "counter", mt.alarmQueued);
	emitU32("simut_alarm_sent_total", "counter", mt.alarmSent);
	emitU32("simut_alarm_acked_total", "counter", mt.alarmAcked);
	emitU32("simut_alarm_failed_total", "counter", mt.alarmFailed);
	emitU32("simut_alarm_dropped_total", "counter", mt.alarmDropped);
	emitU32("simut_alarm_error_records_total", "counter", mt.alarmErrRecords);

	/* ── sensors aggregate / storage ── */
	emitU32("simut_sensor_reads_ok_total", "counter", mt.sensorReadsOk);
	emitU32("simut_sensor_reads_error_total", "counter", mt.sensorReadsErr);
	emitU32("simut_config_saves_total", "counter", mt.configSaves);

	/* ── flash / Core 1 — the release-image observability /api/status grew
	 *    for the A6 soak; a scraper is exactly who watches a 72 h soak. ── */
	emitU32("simut_flash_ops_total", "counter", mt.flashOps);
	emitType("simut_flash_op_max_seconds", "gauge");
	add(ln, PromMetrics::lineF(ln, sizeof(ln), "simut_flash_op_max_seconds",
	                           "", mt.flashOpMaxMs / 1000.0, 3));
	emitU32("simut_flash_ops_over50ms_total", "counter", mt.flashOpsOver50ms);
	emitU32("simut_flash_unguarded_ops_total", "counter", g_flashIrqExposed);
	emitType("simut_core1_heartbeat_age_seconds", "gauge");
	add(ln, PromMetrics::lineF(ln, sizeof(ln), "simut_core1_heartbeat_age_seconds",
	                           "", (millis( ) - g_core1HeartbeatMs) / 1000.0, 3));
	emitU32("simut_core1_launches_total", "counter", g_core1Launches);
	emitType("simut_core1_kills_total", "counter");
	add(ln, PromMetrics::lineU32(ln, sizeof(ln), "simut_core1_kills_total",
	                             "cause=\"lockout\"", g_core1KillsLockout));
	add(ln, PromMetrics::lineU32(ln, sizeof(ln), "simut_core1_kills_total",
	                             "cause=\"health\"", g_core1KillsHealth));
	add(ln, PromMetrics::lineU32(ln, sizeof(ln), "simut_core1_kills_total",
	                             "cause=\"quiet\"", g_core1KillsQuiet));
	emitType("simut_web_aborts_total", "counter");
	add(ln, PromMetrics::lineU32(ln, sizeof(ln), "simut_web_aborts_total",
	                             "cause=\"deadline\"", _cgDeadlineHits));
	add(ln, PromMetrics::lineU32(ln, sizeof(ln), "simut_web_aborts_total",
	                             "cause=\"guard\"", _cgGuardHits));
	add(ln, PromMetrics::lineU32(ln, sizeof(ln), "simut_web_aborts_total",
	                             "cause=\"disconnect\"", _cgDisconnHits));

	/* ── per-slot readings — same source and slot resolution as /api/status ── */
	const auto& sensors = _sensorRef->getRuntimeSensors( );
	struct Fam { const char* name; uint8_t ch; int idx; int prec; };
	static const Fam FAMS[] = {
		{ "simut_temperature_celsius", CH_TEMP,  0, 2 },
		{ "simut_humidity_percent",    CH_HUM,   1, 1 },
		{ "simut_pressure_hpa",        CH_PRESS, 2, 1 },
	};

	auto slotLabels = [&](const RuntimeSensor& s, char* out, size_t cap) {
		int slotIdx = -1;
		for (int si = 0; si < MAX_SENSORS; si++) {
			if (cfg.sensors[si].active && cfg.sensors[si].pins[0] == s.config.pins[0]
			    && cfg.sensors[si].sensorType == (uint8_t)s.type) {
				slotIdx = si; break;
			}
		}
		char idEsc[40], nameEsc[72];
		PromMetrics::escapeLabel(s.config.hwId, idEsc, sizeof(idEsc));
		PromMetrics::escapeLabel(s.config.friendlyName, nameEsc, sizeof(nameEsc));
		snprintf(out, cap, "slot=\"%d\",hwid=\"%s\",name=\"%s\"", slotIdx, idEsc, nameEsc);
	};

	emitType("simut_sensor_ok", "gauge");
	for (const auto& s : sensors) {
		if (!s.config.active) continue;
		char labels[144];
		slotLabels(s, labels, sizeof(labels));
		add(ln, PromMetrics::lineU32(ln, sizeof(ln), "simut_sensor_ok", labels,
		                             s.inErrorState ? 0 : 1));
	}
	for (const auto& f : FAMS) {
		emitType(f.name, "gauge");
		for (const auto& s : sensors) {
			if (!s.config.active || s.inErrorState) continue;
			if (!sensorHasChannel(s.type, f.ch)) continue;
			float v = s.avgValue[f.idx];
			if (isnan(v) || v >= 1e9f) continue;
			char labels[144];
			slotLabels(s, labels, sizeof(labels));
			add(ln, PromMetrics::lineF(ln, sizeof(ln), f.name, labels, v, f.prec));
		}
	}

	flush( );
	if (!dead) safeSend(""); /* empty chunk = end of stream, same as /api/status */
}
