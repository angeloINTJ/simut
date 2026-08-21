/**
 * @file WebManager_Api.cpp
 * @brief REST API handlers: status, config, network, users, themes, alarms, set_time.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "MetricsManager.h"
#include "Themes.h"
#include "TouchPriority.h"
#include "DisplayManager.h" /* For getActiveWebDictSource */
#include "FlashIrqProbe.h"  /* For g_flashIrqExposed (metr.fx) */
#include <hardware/watchdog.h>
/* Position trace (hp=) — see WebManager_History.cpp. */
#define HPOS(v) do { watchdog_hw->scratch[7] = (uint32_t)(v); } while (0)
#include <LittleFS.h>
#include <time.h>

using ReadGuard = StorageManager::ReadGuard;

void WebManager::handleApiPerms( ) {
	uint16_t perms = getAuthPerms( );
	if (perms == 0) { _server->send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }

	bool ntpOk = _netRef->isTimeSynced( );
	time_t now = time(nullptr);
	/* Exposes metadata of the active .lng file (code+name) for the Web UI
	 * to populate the language selector dynamically. If no .lng is loaded,
	 * returns empty strings. Web JS falls back to "PT" when code is empty. */
	const char* lc = DisplayManager::getActiveLangCode( );
	const char* ln = DisplayManager::getActiveLangName( );
	char json[336];
	snprintf(json, sizeof(json),
	         "{\"user\":\"%s\",\"perms\":%u,\"ntp\":%d,\"time\":%lu,\"version\":\"%s\","
	         "\"langCode\":\"%s\",\"langName\":\"%s\"}",
	         _currentUserName.c_str( ), perms, ntpOk ? 1 : 0, (unsigned long)now, SIMUT_VERSION,
	         lc ? lc : "", ln ? ln : "");
	_server->send(200, "application/json", json);
}

void WebManager::handleApiNetwork( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_NET_CONFIG)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

	SystemConfig& cfg = _storageRef->getConfig( );

	WebConfigData* w = reinterpret_cast<WebConfigData*>(
		cfg.reserved + WEB_CONFIG_OFFSET);
	uint16_t currentPort = (w->port > 0) ? w->port : WEB_DEFAULT_PORT;

	/* Exposes NetworkTimeData overlay flags for the UI. */
	char json[640];
	char ipBuf[16], macBuf[18];
	_netRef->getIpAddress(ipBuf, sizeof(ipBuf));
	_netRef->getMacAddress(macBuf, sizeof(macBuf));
	snprintf(json, sizeof(json),
	         "{\"connected\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\","
	         "\"dns\":\"%s\",\"mac\":\"%s\",\"ssid\":\"%s\",\"use_dhcp\":%s,"
	         "\"static_ip\":\"%s\",\"static_mask\":\"%s\",\"static_gw\":\"%s\","
	         "\"static_dns\":\"%s\",\"dns_auto\":%s,\"dns2\":\"%s\","
	         "\"ntp_server\":\"%s\",\"ntp_enabled\":%s,\"web_port\":%u,"
	         "\"web_ka\":%s,\"web_tls\":%s}",
	         _netRef->isConnected( ) ? "true" : "false",
	         ipBuf,
	         _netRef->getSubnetMask( ).c_str( ),
	         _netRef->getGateway( ).c_str( ),
	         _netRef->getDns( ).c_str( ),
	         macBuf,
	         cfg.wifiSsid,
	         cfg.useDhcp ? "true" : "false",
	         cfg.staticIp, cfg.staticMask, cfg.staticGateway, cfg.staticDns,
	         _storageRef->isDnsAuto( ) ? "true" : "false",
	         _storageRef->getSecondaryDns( ),
	         cfg.ntpServer,
	         _storageRef->isNtpEnabled( ) ? "true" : "false",
	         (unsigned)currentPort,
	         _storageRef->isWebKeepAliveEnabled( ) ? "true" : "false",
	         tlsCertFilesPresent( ) ? "true" : "false");

	_server->send(200, "application/json", json);
}


String WebManager::jsonEscape(const char* src) {
	String out;
	out.reserve(strlen(src) + 16);
	while (*src) {
		switch (*src) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += *src; break;
		}
		src++;
	}
	return out;
}
void WebManager::handleApiConfig( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_SYS_CONFIG)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

	SystemConfig& cfg = _storageRef->getConfig( );

	_server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
	_server->send(200, "application/json", "");

	char buf[256];

	/* t_cert: was a CA cert loaded at boot. With t_sec on and t_cert false, TLS
	 * runs unauthenticated (setInsecure) — the UI shows a "no cert validation"
	 * seal from this pair (M-8). */
	bool tlsCertLoaded = _telemetryRef ? _telemetryRef->isTlsCertLoaded( ) : false;
	snprintf(buf, sizeof(buf),
	         "{\"name\":\"%s\",\"tz\":%d,\"log\":%s,\"res\":%d,\"s_int\":%lu,"
	         "\"t_transport\":%d,\"t_sec\":%s,\"t_cert\":%s,"
	         "\"ntp_enabled\":%s,\"now_epoch\":%lu,",
	         jsonEscape(cfg.deviceName).c_str( ), cfg.timezoneOffset,
	         cfg.loggingEnabled ? "true" : "false", cfg.ds18Resolution,
	         (unsigned long)cfg.sampleIntervalMs, cfg.telTransport,
	         cfg.telEncryption ? "true" : "false",
	         tlsCertLoaded ? "true" : "false",
	         _storageRef->isNtpEnabled( ) ? "true" : "false",
	         (unsigned long)time(nullptr));
	if (!safeSend(buf)) return;

	/* Masks telApiKey. If length > 4, shows only the first 4 chars
	 * + "***" — prevents accidental screenshot/sharing of the full key.
	 * commit_all parser detects "***" and keeps the current value
	 * unless the user provides a new value without the marker. */
	char keyMask[16];
	size_t klen = strnlen(cfg.telApiKey, sizeof(cfg.telApiKey));
	if (klen == 0) {
		keyMask[0] = '\0';
	} else if (klen <= 4) {
		snprintf(keyMask, sizeof(keyMask), "***");
	} else {
		snprintf(keyMask, sizeof(keyMask), "%.4s***", cfg.telApiKey);
	}
	snprintf(buf, sizeof(buf),
	         "\"t_srv\":\"%s\",\"t_port\":%u,\"t_path\":\"%s\",\"t_key\":\"%s\",",
	         jsonEscape(cfg.telServer).c_str( ), cfg.telPort,
	         jsonEscape(cfg.telPath).c_str( ), jsonEscape(keyMask).c_str( ));
	if (!safeSend(buf)) return;

	snprintf(buf, sizeof(buf),
	         "\"m_topic\":\"%s\",\"m_cid\":\"%s\",\"m_user\":\"%s\","
	         "\"m_qos\":%d,\"m_retain\":%s,\"m_ka\":%u,\"m_had\":%s,",
	         jsonEscape(cfg.mqttTopic).c_str( ), jsonEscape(cfg.mqttClientId).c_str( ),
	         jsonEscape(cfg.mqttUser).c_str( ), cfg.mqttQos,
	         cfg.mqttRetain ? "true" : "false", cfg.mqttKeepAlive,
	         _storageRef->isHaDiscoveryEnabled( ) ? "true" : "false");
	if (!safeSend(buf)) return;

	snprintf(buf, sizeof(buf),
	         "\"t_int\":%lu,\"t_bat\":%d,\"t_mode\":%d,\"h_int\":%u,",
	         (unsigned long)cfg.telInterval, cfg.telBatchSize, cfg.telMode,
	         _storageRef->getHistoryIntervalMin( ));
	if (!safeSend(buf)) return;

	/* Syslog forwarder (overlay). Server rendered back to a dotted quad ("" if
	 * unset); the enable is the RAW flag, matching what commit_all stores, so
	 * the UI checkbox reflects the toggle even before a server is entered. */
	{
		uint32_t slIp = _storageRef->getSyslogServerIp( );
		String slIpStr = slIp ? IPAddress(slIp).toString( ) : String("");
		snprintf(buf, sizeof(buf),
		         "\"slog_en\":%s,\"slog_srv\":\"%s\",\"slog_port\":%u,\"slog_lvl\":%u,",
		         _storageRef->getSyslogEnabledFlag( ) ? "true" : "false",
		         slIpStr.c_str( ), _storageRef->getSyslogPort( ),
		         _storageRef->getSyslogMinLevel( ));
		if (!safeSend(buf)) return;
	}

	safeSend("\"t_glob\":\"");
	if (!safeSend(jsonEscape(cfg.telGlobalTemplate).c_str( ))) return;
	safeSend("\",\"t_line\":\"");
	if (!safeSend(jsonEscape(cfg.telLineTemplate).c_str( ))) return;
	safeSend("\",\"t_sep\":\"");
	if (!safeSend(jsonEscape(cfg.telLineSeparator).c_str( ))) return;
	safeSend("\",\"serial\":\"");
	if (!safeSend(jsonEscape(_storageRef->getBoardSerialNumber( ).c_str( )).c_str( ))) return;
	/* "ambHwId" (= cfg.sensors[10].hwId) is gone: the telemetry preview used
	 * it to guess the key the {tAMB}/{uAMB}/{pAMB} tokens would publish
	 * under, back when slot 10 was the ambient sensor by definition. Those
	 * tokens no longer exist — per-slot keys come from the sensors[] array
	 * below, same as the firmware resolves them. */
	safeSend("\",\"sensors\":[");
	for (int i = 0; i < MAX_SENSORS; i++) {
		/* hum/press say which channels this slot actually reports, so the
		 * telemetry Live Preview can resolve {uN}/{pN} the same way the
		 * firmware does instead of guessing from the hwId. */
		snprintf(buf, sizeof(buf), "%s{\"hwid\":\"%s\",\"active\":%s,\"hum\":%s,\"press\":%s}",
		         i == 0 ? "" : ",",
		         jsonEscape(cfg.sensors[i].hwId).c_str( ),
		         cfg.sensors[i].active ? "true" : "false",
		         sensorHasChannel((SensorType)cfg.sensors[i].sensorType, CH_HUM) ? "true" : "false",
		         sensorHasChannel((SensorType)cfg.sensors[i].sensorType, CH_PRESS) ? "true" : "false");
		if (!safeSend(buf)) return;
	}
	safeSend("]}");
}

void WebManager::handleApiUsers( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_USER_MGR)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

	SystemConfig& cfg = _storageRef->getConfig( );

	char json[512];
	int pos = 0;
	json[pos++] = '[';

	bool first = true;
	for (int i = 0; i < MAX_USERS; i++) {
		if (!cfg.users[i].active) continue;
		if (!first) json[pos++] = ',';
		first = false;
		pos += snprintf(json + pos, sizeof(json) - pos,
		                "{\"id\":%d,\"name\":\"%s\",\"perms\":%u}",
		                i, cfg.users[i].username, cfg.users[i].permissions);
	}
	json[pos++] = ']';
	json[pos] = '\0';

	_server->send(200, "application/json", json);
}

void WebManager::handleApiThemes( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_DASHBOARD)) {
		_server->send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}

	_server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	_server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
	_server->send(200, "application/json", "");

	safeSend("[");
	char entry[64];

	for (int i = 0; i < getThemeCount( ); i++) {
		char cleanName[32];
		int cn = 0;
		const char* p = getThemePalette(i)->displayName;
		if (p) {
			while (*p && cn < (int)sizeof(cleanName) - 1) {
				unsigned char c = (unsigned char)(*p);
				if (c >= 32 && c != '\"' && c != '\\') cleanName[cn++] = (char)c;
				p++;
			}
		}
		if (cn == 0) cn = snprintf(cleanName, sizeof(cleanName), "Tema %d", i);
		cleanName[cn] = '\0';

		snprintf(entry, sizeof(entry), "%s{\"id\":%d,\"name\":\"%s\"}", i > 0 ? "," : "", i, cleanName);
		if (!safeSend(entry)) return;
	}

	safeSend("]");
}


void WebManager::handleApiAlarms( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_SYS_CONFIG)) {
		_server->send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}

	SystemConfig& cfg = _storageRef->getConfig( );


	_server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
	_server->send(200, "application/json", "");


	if (!safeSend("{\"sensors\":[")) return;

	bool first = true;

	for (int i = 0; i < MAX_SENSORS; i++) {
		if (!cfg.sensors[i].active) continue;
		if (!first) { if (!safeSend(",")) return; }


		bool hasHum = sensorHasHumidity((SensorType)cfg.sensors[i].sensorType);
		/* From the driver catalogue, not from "has humidity => it is a DHT22":
		 * that two-type guess labelled every BMP280 card on /alarms "DHT22". */
		const char* typeName = sensorTypeName((SensorType)cfg.sensors[i].sensorType);


		String sName = cfg.sensors[i].friendlyName;
		sName.replace("\"", "\\\"");

		char buf[320];
			snprintf(buf, sizeof(buf),
		         "{\"idx\":%d,\"name\":\"%s\",\"type\":\"%s\",\"gpio\":%d,"
		         "\"has_hum\":%s,"
		         "\"tmin\":%.1f,\"tmax\":%.1f,\"hmin\":%.1f,\"hmax\":%.1f,"
		         "\"active\":%s,\"lim\":{",
		         i, sName.c_str( ), typeName, cfg.sensors[i].pins[0],
		         hasHum ? "true" : "false",
		         cfg.sensors[i].chMin[CH_TEMP], cfg.sensors[i].chMax[CH_TEMP],
		         cfg.sensors[i].chMin[CH_HUM], cfg.sensors[i].chMax[CH_HUM],
		         cfg.sensors[i].alarmsActive ? "true" : "false");
			if (!safeSend(buf)) return;

			/* Limits for every channel the part reports. The tmin/tmax/hmin/hmax
			 * above are the same two channels under their old names, kept for a
			 * cached page; a BMP280 has no humidity and its pressure limits only
			 * exist here. */
			bool firstLim = true;
			for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
				if (!sensorHasChannel((SensorType)cfg.sensors[i].sensorType, c)) continue;
				char lb[96];
				snprintf(lb, sizeof(lb), "%s\"%s\":[%.1f,%.1f]",
				         firstLim ? "" : ",", channelInfo(c).key,
				         cfg.sensors[i].chMin[c], cfg.sensors[i].chMax[c]);
				if (!safeSend(lb)) return;
				firstLim = false;
			}
			if (!safeSend("}}")) return;
			first = false;
	}

	if (!safeSend("],")) return;


	SoundSettingsState snd = _soundRef->getSettingsState( );
	char sndBuf[336];
	snprintf(sndBuf, sizeof(sndBuf),
	         "\"sounds\":{\"touch\":%s,\"confirm\":%s,\"error\":%s,"
	         "\"alarm\":%s,\"web\":%s,\"attention\":%s,\"mute\":%s,"
	         "\"volume\":%d,\"alarmVolume\":%d,"
	         "\"melTouch\":%d,\"melConfirm\":%d,\"melError\":%d,"
	         "\"melAlarm\":%d,\"melAttention\":%d}}",
	         snd.touchEnabled ? "true" : "false",
	         snd.confirmEnabled ? "true" : "false",
	         snd.errorEnabled ? "true" : "false",
	         snd.alarmEnabled ? "true" : "false",
	         snd.webEnabled ? "true" : "false",
	         snd.attentionEnabled ? "true" : "false",
	         snd.muted ? "true" : "false",
	         snd.volume,
	         snd.alarmVolume,
	         snd.touchMelody, snd.confirmMelody,
	         snd.errorMelody, snd.alarmMelody, snd.attentionMelody);
	safeSend(sndBuf);
	safeSend("");
}
/* Serves the @WEBDICT blob from the active .lng file (UTF-8 JSON).
 * Open without auth — login and force_chpass need to fetch it before session.
 * Cacheable per session: client fetches once and uses sessionStorage.
 *
 * Streamed straight off the filesystem rather than from a resident copy: the
 * blob is ~14 KB and the firmware never reads it, so the parser deliberately
 * leaves it on flash (see DisplayManager_LangParser.cpp). Content-Length is
 * known up front, so this is a plain-bodied response, not chunked. */
/* Byte range of the @WEBDICT block in the pack AS IT IS ON DISK RIGHT NOW.
 *
 * The parser records that range once, at boot. Replacing the pack through
 * /files leaves those numbers describing the PREVIOUS file: the handler then
 * seeks to a stale offset and streams a stale length, and the response ends in
 * the middle of a string. Invalid JSON makes the browser's JSON.parse throw,
 * which drops the WHOLE dictionary — every key falls back to English, not just
 * the ones that changed. Silent, and it survives until the next reboot.
 *
 * Rescanning here rather than reloading the pack is deliberate. A reload costs
 * a ~28 KB transient allocation and rewrites the strings Core 1 is reading off
 * the display; this endpoint never touches the resident dictionary, so it only
 * needs the range. One pass over ~28 KB of flash on an endpoint the client
 * caches for 5 minutes.
 *
 * Byte-wise on purpose: the block is one ~15 KB line, so no line-oriented read
 * with a bounded buffer can be trusted to keep its place. */
static bool scanWebDictRange(File &f, uint32_t &outOffset, uint32_t &outLen) {
	static const char kDir[] = "@WEBDICT";
	const size_t kDirLen = sizeof(kDir) - 1;

	f.seek(0);
	uint8_t buf[128];
	uint32_t pos = 0;
	bool atLineStart = true;   /* byte 0 counts as a line start */
	size_t match = 0;          /* how much of kDir matched so far */
	bool inBlock = false;
	uint32_t bodyStart = 0;
	bool skippingDirLine = false;

	while (true) {
		int n = f.read(buf, sizeof(buf));
		if (n <= 0) break;
		for (int i = 0; i < n; i++, pos++) {
			const char c = (char)buf[i];

			if (skippingDirLine) {
				/* Body begins on the line after the directive. */
				if (c == '\n') { skippingDirLine = false; inBlock = true; bodyStart = pos + 1; }
				continue;
			}
			if (inBlock) {
				/* Any later directive at column zero closes the block. */
				if (atLineStart && c == '@') {
					outOffset = bodyStart;
					outLen = pos - bodyStart;
					return outLen > 0;
				}
				atLineStart = (c == '\n');
				continue;
			}

			if (atLineStart && c == '@') { match = 1; }
			else if (match > 0 && match < kDirLen && c == kDir[match]) { match++; }
			else { match = 0; }

			if (match == kDirLen) { skippingDirLine = true; match = 0; continue; }
			atLineStart = (c == '\n');
		}
	}

	if (inBlock && pos > bodyStart) {   /* block runs to EOF */
		outOffset = bodyStart;
		outLen = pos - bodyStart;
		return true;
	}
	return false;
}

void WebManager::handleApiLang( ) {
	/* Short cache: translations only change when user swaps .lng, so 5 min is
	 * enough without being too stale. */
	_server->sendHeader("Cache-Control", "public, max-age=300");

	const char* path = nullptr;
	uint32_t offset = 0, len = 0;
	if (!DisplayManager::getActiveWebDictSource(&path, &offset, &len)) {
		_server->send(200, "application/json", "{}");
		return;
	}

	File f;
	{
		ReadGuard rg(_storageRef);
		f = LittleFS.open(path, "r");
		if (f) {
			/* Take the range from the file, not from what boot remembered.
			 * Falls back to the cached pair only if the scan finds no
			 * @WEBDICT — a pack shape this build does not understand. */
			uint32_t scanOff = 0, scanLen = 0;
			if (scanWebDictRange(f, scanOff, scanLen)) {
				offset = scanOff;
				len = scanLen;
			}
			if (!f.seek(offset)) { f.close( ); }
		}
	}
	if (!f) {
		/* Pack vanished or shrank under us (upload/delete between boot and
		 * now). An empty dict is honest here: the UI falls back to EN. */
		_server->send(200, "application/json", "{}");
		return;
	}

	_server->setContentLength(len);
	_server->send(200, "application/json", "");
	_server->client( ).setTimeout(500);

	char buf[WEB_STREAM_CHUNK_SOFT];
	uint32_t sent = 0;
	while (sent < len) {
		if (isClientGone( ) || isHandlerOvertime( )) {
			LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_FILE, (int)sent, "");
			break;
		}
		size_t want = len - sent;
		if (want > sizeof(buf)) want = sizeof(buf);

		size_t n = 0;
		{
			ReadGuard rg(_storageRef);
			n = f.read((uint8_t*)buf, want);
		}
		if (n == 0) break; /* short read: file truncated since boot */
		if (!safeSend(buf, n)) break;
		sent += n;
		streamBreath( );
	}
	f.close( );
}

void WebManager::handleApiStatus( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_DASHBOARD)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

	_server->sendHeader("Cache-Control", "no-cache");

	SystemConfig& cfg = _storageRef->getConfig( );
	String ipStr = _netRef->getIpAddress( );

	uint32_t heapTot = rp2040.getTotalHeap( );
	uint32_t heapFree = rp2040.getFreeHeap( );

	if (timeSince(_lastFsInfoRefresh, 10000) || _cachedFsTotalBytes == 0) {

		ReadGuard rg(_storageRef);
		FSInfo fs_info;
		fs_info.totalBytes = 0;
		fs_info.usedBytes = 0;
		LittleFS.info(fs_info);
		_cachedFsTotalBytes = fs_info.totalBytes;
		_cachedFsUsedBytes = fs_info.usedBytes;
		_lastFsInfoRefresh = millis( );
	}

	time_t now = time(nullptr);
	bool ntp = _netRef->isTimeSynced( );


	int pending = _telemetryRef
	              ? static_cast<int>(_telemetryRef->getPendingEstimate( ))
	              : -1;

	_server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
	_server->send(200, "application/json", "");

	char buffer[1024];

	String devName = cfg.deviceName;
	devName.replace("\"", "\\\"");

	/* Refreshes heap samples before serving metrics (cost: ~16 malloc/free).
	 * Frequency limited by the dashboard polling interval (3s). */
	MetricsManager& mm = MetricsManager::instance( );
	HPOS(810);
	mm.sampleHeap( );
	mm.sampleLargestBlock( );
	HPOS(811);
	const int liveRssi = _netRef->getRssi( ); /* one live ioctl, bracketed */
	HPOS(812);
	mm.observeRssi(liveRssi);
	const SystemMetrics& mt = mm.data( );

	uint32_t heapLargest = mt.heapLargestBlock;
	/* "tel": is telemetry switched on at all (interval 0 = off, the same test
	 * TelemetryManager::update makes before returning early).
	 *
	 * Without it the dashboard cannot tell "nothing left to send" from "nothing
	 * is ever sent", and pending==0 means both. */
	int telOn = (cfg.telInterval > 0) ? 1 : 0;
	snprintf(buffer, sizeof(buffer), "{\"sys\":{\"name\":\"%s\",\"uptime\":%lu,\"rssi\":%d,\"ip\":\"%s\",\"theme\":%d,\"heap_f\":%lu,\"heap_t\":%lu,\"heap_lb\":%lu,\"fs_u\":%lu,\"fs_t\":%lu,\"time\":%lu,\"ntp\":%d,\"pending\":%d,\"tel\":%d,\"hi\":%u},",
	         devName.c_str( ), millis( ), liveRssi, ipStr.c_str( ), cfg.themeIndex,
	         (unsigned long)heapFree, (unsigned long)heapTot, (unsigned long)heapLargest,
	         (unsigned long)_cachedFsUsedBytes, (unsigned long)_cachedFsTotalBytes,
	         (unsigned long)now, ntp ? 1 : 0, pending, telOn,
	         /* hi: sampling interval in minutes. A V5 file does not carry it —
	          * the time symbols are deltas against the nominal interval (§3.5),
	          * so a reader outside the device has to be told. Without it a
	          * browser decoding .h5 would place every record after the first
	          * at the wrong instant on any device not sampling once a minute. */
	         (unsigned)_storageRef->getHistoryIntervalMin( ));

	if (!safeSend(buffer)) return;


	/* "Never sampled" sentinel for min/max RSSI: serializes 0 when invalid. */
	int32_t rmn = (mt.rssiMin == 127) ? 0 : mt.rssiMin;
	int32_t rmx = (mt.rssiMax == -127) ? 0 : mt.rssiMax;
	uint32_t hmin = (mt.heapMinSeen == 0xFFFFFFFFU) ? mt.heapFreeNow : mt.heapMinSeen;
	uint32_t lbmin = (mt.heapLargestMin == 0xFFFFFFFFU) ? mt.heapLargestBlock : mt.heapLargestMin;

	/* fo/fom/fot/f50 are the flash-write counters. They were tracked since
	 * T0.1 but reachable only from a CLI the shipping image does not carry,
	 * which made "how often does this device stop Core 1 to write?" — the
	 * question the V5 format exists to answer — unanswerable from outside.
	 *
	 * fx is the invariant among them: flash ops that ran with Core 1
	 * possibly executing and not frozen. Any value but 0 names a write
	 * path missing its Core1FlashPause — the class behind e035791 and the
	 * calibration reboots of 4611987 — and the release image needs it
	 * readable for exactly the same reason as the four above.
	 *
	 * cgd/cgg/cgx are the three reasons a chunked response was cut short
	 * (WebManager.h:34-36): deadline, guard latch, real disconnect. `show
	 * metrics` prints them, but that command does not exist outside the
	 * full-CLI image, so from the network they were indistinguishable —
	 * a truncated download and a client that walked away read the same.
	 * They are what makes an abort diagnosable while a storm is running.
	 *
	 * c1a/c1n/c1kl/c1kh/c1kq/c1s are the Core-1 lifecycle, and they are here
	 * for the third time the same lesson has had to be learned: a STALLED
	 * display reads exactly like a healthy one from outside, and every kill
	 * and relaunch was visible only through `show metrics` — a command the
	 * shipping image does not carry. Acceptance A6 asks for a 72 h soak with
	 * "heartbeat/WDT without regression", so on the release image the very
	 * quantity being certified could not be read at all, and a soak that
	 * cannot see a Core-1 death would have reported PASS through one. c1a is
	 * the age of the stamp Core 1 writes once per loop: tens of ms is
	 * healthy, seconds means frozen, killed or parked. The kills split by
	 * cause so a rising c1kl names the stuck-handshake path rather than the
	 * health watchdog. */
	const uint32_t c1BeatAge = millis( ) - g_core1HeartbeatMs;
	snprintf(buffer, sizeof(buffer),
	         "\"metr\":{\"lb\":%lu,\"lbm\":%lu,\"hm\":%lu,\"wf\":%lu,\"mq\":%lu,\"rmn\":%ld,\"rmx\":%ld,\"ts\":%lu,\"tf\":%lu,\"tr\":%lu,\"tb\":%lu,\"tl\":%lu,\"so\":%lu,\"se\":%lu,\"cs\":%lu,"
	         "\"fo\":%lu,\"fom\":%lu,\"fot\":%lu,\"f50\":%lu,\"fx\":%lu,\"ad\":%lu,"
	         "\"c1a\":%lu,\"c1n\":%lu,\"c1kl\":%lu,\"c1kh\":%lu,\"c1kq\":%lu,\"c1s\":%lu,"
	         "\"cgd\":%lu,\"cgg\":%lu,\"cgx\":%lu},",
	         (unsigned long)mt.heapLargestBlock, (unsigned long)lbmin, (unsigned long)hmin,
	         (unsigned long)mt.wifiReconnects, (unsigned long)mt.mqttReconnects,
	         (long)rmn, (long)rmx,
	         (unsigned long)mt.telSent, (unsigned long)mt.telFailed, (unsigned long)mt.telRetries,
	         (unsigned long)mt.telTotalBytes, (unsigned long)mt.telLastLatencyMs,
	         (unsigned long)mt.sensorReadsOk, (unsigned long)mt.sensorReadsErr,
	         (unsigned long)mt.configSaves,
	         (unsigned long)mt.flashOps, (unsigned long)mt.flashOpMaxMs,
	         (unsigned long)mt.flashOpTotalMs, (unsigned long)mt.flashOpsOver50ms,
	         (unsigned long)g_flashIrqExposed, (unsigned long)_abortDrops,
	         (unsigned long)c1BeatAge, (unsigned long)g_core1Launches,
	         (unsigned long)g_core1KillsLockout, (unsigned long)g_core1KillsHealth,
	         (unsigned long)g_core1KillsQuiet, (unsigned long)g_core1LockoutStuck,
	         (unsigned long)_cgDeadlineHits, (unsigned long)_cgGuardHits,
	         (unsigned long)_cgDisconnHits);

	if (!safeSend(buffer)) return;
	if (!safeSend("\"sensors\":[")) return;

	const auto& sensors = _sensorRef->getRuntimeSensors( );
	bool first = true;
	for (const auto &s : sensors) {
		if (!s.config.active) continue;
		if (!first) { if (!safeSend(",")) return; }

		String sName = s.config.friendlyName;
		sName.replace("\"", "\\\"");

		String sId = s.config.hwId;
		sId.replace("\"", "\\\"");

		char valBuffer[16]; char humBuffer[32] = ""; char presBuffer[32] = "";
		if (s.inErrorState) snprintf(valBuffer, sizeof(valBuffer), "\"Error\"");
		else if (isnan(s.avgValue[0])) snprintf(valBuffer, sizeof(valBuffer), "\"--\"");
		else snprintf(valBuffer, sizeof(valBuffer), "%.2f", s.avgValue[0]);

		const char* typeName = sensorTypeName(s.type);
		uint8_t chCount = sensorValueCount(s.type);
		if (sensorHasChannel(s.type, CH_HUM) && !s.inErrorState && !isnan(s.avgValue[1]) && s.avgValue[1] < 1e9f) {
			snprintf(humBuffer, sizeof(humBuffer), ",\"hum\":%.1f", s.avgValue[1]);
		}
		if (sensorHasChannel(s.type, CH_PRESS) && !s.inErrorState && !isnan(s.avgValue[2]) && s.avgValue[2] < 1e9f) {
			snprintf(presBuffer, sizeof(presBuffer), ",\"press\":%.1f", s.avgValue[2]);
		}

		auto fmt = SensorFormat::forType(s.type);
		char rolesBuf[32] = "";
		for (int p = 0; p < fmt.pinCount && p < 4; p++) {
		 if (p > 0) { size_t l = strlen(rolesBuf); rolesBuf[l] = ','; rolesBuf[l+1] = '\0'; }
		 strncat(rolesBuf, fmt.pins[p].label, sizeof(rolesBuf) - strlen(rolesBuf) - 1);
		}
		/* Find slot index for this sensor */
		int slotIdx = -1;
		for (int si = 0; si < MAX_SENSORS; si++) {
		 if (cfg.sensors[si].active && cfg.sensors[si].pins[0] == s.config.pins[0]
		     && cfg.sensors[si].sensorType == (uint8_t)s.type) {
		  slotIdx = si; break;
		 }
		}
		snprintf(buffer, sizeof(buffer), "{\"slot\":%d,\"gpio\":%d,\"id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"ch\":%d,\"pc\":%d,\"pr\":\"%s\",\"val\":%s%s%s}",
		         slotIdx, s.config.pins[0], sId.c_str( ), sName.c_str( ), typeName, chCount, fmt.pinCount, rolesBuf, valBuffer, humBuffer, presBuffer);
		if (!safeSend(buffer)) return;
			first = false;
	}

	safeSend("]}");
	safeSend("");
}
