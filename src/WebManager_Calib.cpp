/**
 * @file WebManager_Calib.cpp
 * @brief Integrated calibration UI on /dashboard.
 * @details 2 endpoints: GET /api/calib (lightweight state) + POST /api/calib (apply
 * + atomic rewrite of calib.csv with VERSION=epoch, NTP required).
 * No physical scan (use CLI `sensor accept`); no accept via UI.
 * Refactored without std::vector<String>: streaming 2-pass of calib.csv.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "WebManager.h"
#include "ParseFloat.h"
#include "StorageManager.h"
#include "SensorManager.h"
#include "NetworkManager.h"
#include "AppManager.h"
#include "LogManager.h"
#include "DisplayManager.h"
#include <LittleFS.h>
#include <math.h>

extern AppManager app;

namespace {

bool jsonExtractFloat(const String& obj, const char* key, float& out) {
	char needle[24]; snprintf(needle, sizeof(needle), "\"%s\":", key);
	int p = obj.indexOf(needle);
	if (p < 0) return false;
	int s = p + (int)strlen(needle);
	while (s < (int)obj.length( ) && (obj[s] == ' ' || obj[s] == '\t')) s++;
	int e = s;
	while (e < (int)obj.length( ) && obj[e] != ',' && obj[e] != '}' && obj[e] != ']') e++;
	if (e <= s) return false;
	String v = obj.substring(s, e); v.trim( );
	if (v.length( ) == 0 || v == "null") return false;
	out = parseFloat(v.c_str( ));
	return true;
}

bool jsonExtractCStr(const String& obj, const char* key, char* outBuf, size_t outSize) {
	char needle[24]; snprintf(needle, sizeof(needle), "\"%s\":\"", key);
	int p = obj.indexOf(needle);
	if (p < 0) { outBuf[0] = '\0'; return false; }
	int s = p + (int)strlen(needle);
	int e = s;
	while (e < (int)obj.length( ) && obj[e] != '"') {
		if (obj[e] == '\\' && e + 1 < (int)obj.length( )) e++;
		e++;
	}
	size_t n = (size_t)(e - s);
	if (n >= outSize) n = outSize - 1;
	memcpy(outBuf, obj.c_str( ) + s, n);
	outBuf[n] = '\0';
	return true;
}

void romToHex(const uint8_t* rom, char* out17) {
	snprintf(out17, 17, "%02X%02X%02X%02X%02X%02X%02X%02X",
	         rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
}

bool isAllZero(const uint8_t* rom) {
	for (int i = 0; i < 8; i++) if (rom[i] != 0) return false;
	return true;
}

void sanitizeName(char* s) {
	for (; *s; s++) if (*s == ',' || *s == '"') *s = ' ';
}

} /* anonymous namespace */


/* ===== GET /api/sensors =====
 * Whole slot map for the /config sensor editor. Two halves:
 *
 *  "types" — the driver catalogue, taken from SensorFormat::forType( ), which
 *            is the same source gpioInitForRole( ) uses at boot. The pin COUNT
 *            and the pin LABELS ("SDA"/"SCL"/"1-Wire") come from there, so the
 *            form draws exactly the pins the driver will claim. Hardcoding that
 *            table in JavaScript would let the two drift apart silently.
 *  "slots" — every persisted SensorRecord field the user can edit.
 *
 * Guarded by PERM_SYS_CONFIG: this is provisioning (which chip on which GPIO),
 * not calibration (PERM_CALIB, still served by /api/calib). */
void WebManager::handleApiSensorsGet( ) {
	if (!(getAuthPerms( ) & PERM_SYS_CONFIG)) {
		_server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}
	SystemConfig& cfg = _storageRef->getConfig( );

	_server.sendHeader("Cache-Control", "no-store");
	_server.setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
	_server.send(200, "application/json", "");

	char buf[320];

	/* gmax: sensors live on GP0..GP15 only — display/touch/buzzer own GP16+.
	 * See the pin budget in simut_config.h and docs/WIRING.md. */
	snprintf(buf, sizeof(buf), "{\"nslot\":%d,\"npin\":%d,\"gmax\":%d,\"types\":[",
	         MAX_SENSORS, MAX_SENSOR_PINS, MAX_SENSORS - 1);
	if (!safeSend(buf)) return;

	/* Explicit list, not a range. `t <= TYPE_BME280` happened to stop before
	 * TYPE_UNKNOWN_ACTIVITY (a scan marker, never a user choice) only because of
	 * the order the enum was written in, and it silently excluded every type
	 * added after it — TYPE_BMP280 would never have reached the picker. */
	static const SensorType kSelectable[] = {
		TYPE_DS18B20, TYPE_DHT22, TYPE_BME280, TYPE_BMP280
	};
	bool firstType = true;
	for (size_t ti = 0; ti < sizeof(kSelectable) / sizeof(kSelectable[0]); ti++) {
		const SensorType t = kSelectable[ti];
		if (!sensorTypeEnabled(t)) continue; /* driver not compiled in */
		SensorFormat f = SensorFormat::forType(t);

		/* Pin labels built separately so the snprintf below stays a single
		 * bounded call instead of running-offset arithmetic. */
		char labels[80];
		labels[0] = '\0';
		size_t o = 0;
		for (uint8_t p = 0; p < f.pinCount && p < MAX_SENSOR_PINS; p++) {
			int w = snprintf(labels + o, sizeof(labels) - o, "%s\"%s\"",
			                 p ? "," : "", f.pins[p].label);
			if (w < 0 || (size_t)w >= sizeof(labels) - o) break;
			o += (size_t)w;
		}
		/* "ch" is the channel MASK, so the page can tell a BMP280 (temperature
		 * and pressure) from a DHT22 (temperature and humidity) — both report
		 * two values, which is all "nv" ever said. */
		snprintf(buf, sizeof(buf), "%s{\"t\":%d,\"n\":\"%s\",\"nv\":%u,\"ch\":%u,\"pins\":[%s]}",
		         firstType ? "" : ",", t, sensorTypeName(t),
		         f.channelCount( ), f.channelMask, labels);
		if (!safeSend(buf)) return;
		firstType = false;
	}

	if (!safeSend("],\"slots\":[")) return;

	static_assert(MAX_SENSOR_PINS == 4, "pins[] emitted as a fixed 4-element array");
	for (int i = 0; i < MAX_SENSORS; i++) {
		const SensorRecord& s = cfg.sensors[i];
		char romHex[17];
		romToHex(s.rom, romHex);
		char nm[40], hw[20];
		safeCopy(nm, s.friendlyName, sizeof(nm));
		safeCopy(hw, s.hwId, sizeof(hw));
		sanitizeName(nm);
		sanitizeName(hw);
		snprintf(buf, sizeof(buf),
		         "%s{\"i\":%d,\"a\":%s,\"t\":%u,\"p\":[%u,%u,%u,%u],\"rom\":\"%s\","
		         "\"hwId\":\"%s\",\"name\":\"%s\",\"al\":%s,\"lim\":{",
		         i ? "," : "", i, s.active ? "true" : "false", s.sensorType,
		         s.pins[0], s.pins[1], s.pins[2], s.pins[3], romHex,
		         hw, nm, s.alarmsActive ? "true" : "false");
		if (!safeSend(buf)) return;

		/* Limits keyed by channel, replacing the fixed tmin/tmax/hmin/hmax.
		 * Emitted for every channel slot the record carries rather than only
		 * the ones this sensor reports: the /config editor lets the user retype
		 * a slot, and the limits it already stored for the new type should not
		 * disappear from the form on the way there. */
		bool firstLim = true;
		for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
			if (!channelValid(c)) continue;
			snprintf(buf, sizeof(buf), "%s\"%s\":[%.2f,%.2f]",
			         firstLim ? "" : ",", channelInfo(c).key, s.chMin[c], s.chMax[c]);
			if (!safeSend(buf)) return;
			firstLim = false;
		}
		if (!safeSend("}}")) return;
	}
	safeSend("]}");
}


/* ===== GET /api/calib =====
 * One entry per active slot. There is no privileged "ambient" sensor:
 * a humidity-capable slot is just a slot that reports a second channel.
 *
 * Until 1.5.6 this emitted an extra "ambient" object hardwired to
 * cfg.sensors[10], and the per-slot arrays it filled were INDEXED BY GPIO
 * while being READ BY SLOT INDEX. Those two agree only while every slot
 * sits on the GPIO of its own number — which is nothing but the factory
 * layout. Move one sensor off that diagonal and the calibration panel
 * showed it the reading of whatever sat on the GPIO matching its slot
 * number, or none at all. Matching runtime to config by GPIO removes both
 * the special case and the coincidence it relied on. */
void WebManager::handleApiCalibGet( ) {
	if (!(getAuthPerms( ) & PERM_CALIB)) {
		_server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}

	SystemConfig& cfg = _storageRef->getConfig( );
	bool ntpOk = _netRef->isTimeSynced( );
	long calibVer = _storageRef->getCalibrationVersion("/calib.csv");
	const auto& runtime = _sensorRef->getRuntimeSensors( );

	_server.sendHeader("Cache-Control", "no-store");
	_server.setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
	_server.send(200, "application/json", "");

	/* 480, not 400: the per-sensor object grew by hasPress/pressRead/
	 * pressOffset, and a truncated object is invalid JSON for the whole page,
	 * not a missing field. */
	char buf[480];
	snprintf(buf, sizeof(buf),
	         "{\"ntp\":%s,\"calibVersion\":%ld,\"picoUID\":\"%s\",\"sensors\":[",
	         ntpOk ? "true" : "false", calibVer,
	         StorageManager::getBoardSerialNumber( ).c_str( ));
	if (!safeSend(buf)) return;

	bool first = true;
	for (int i = 0; i < MAX_SENSORS; i++) {
		if (!cfg.sensors[i].active) continue;

		const RuntimeSensor* rs = nullptr;
		for (const auto& s : runtime) {
			if (s.config.pins[0] == cfg.sensors[i].pins[0]) { rs = &s; break; }
		}

		bool hasH = sensorHasHumidity((SensorType)cfg.sensors[i].sensorType);
		bool hasP = sensorHasChannel((SensorType)cfg.sensors[i].sensorType, CH_PRESS);
		float tRead = rs ? rs->avgValue[CH_TEMP] : NAN;
		float hRead = rs ? rs->avgValue[CH_HUM] : NAN;
		float pRead = rs ? rs->avgValue[CH_PRESS] : NAN;
		/* isfinite, not !isnan: the BMP280 humidity compensation produces
		 * +INF on a part with no humidity die, and isnan(inf) is false. */
		bool hOk = hasH && isfinite(hRead) && hRead < 1e9f;
		bool pOk = hasP && isfinite(pRead) && pRead < 1e9f;

		char romHex[17] = {0};
		romToHex(cfg.sensors[i].rom, romHex);
		char sName[40], sHwId[20];
		safeCopy(sName, cfg.sensors[i].friendlyName, sizeof(sName));
		safeCopy(sHwId, cfg.sensors[i].hwId, sizeof(sHwId));
		sanitizeName(sName);
		sanitizeName(sHwId); /* hwId accepts quotes (isValidCfgString) — they break the JSON */

		snprintf(buf, sizeof(buf),
		         "%s{\"slot\":%d,\"gpio\":%d,\"rom\":\"%s\",\"hwId\":\"%s\",\"name\":\"%s\","
		         "\"hasHum\":%s,\"hasPress\":%s,"
		         "\"tempRead\":%s,\"tempOffset\":%.2f,"
		         "\"humRead\":%s,\"humOffset\":%.2f,"
		         "\"pressRead\":%s,\"pressOffset\":%.2f",
		         first ? "" : ",", i, cfg.sensors[i].pins[0], romHex, sHwId, sName,
		         hasH ? "true" : "false", hasP ? "true" : "false",
		         isfinite(tRead) ? String(tRead, 2).c_str( ) : "null",
		         rs ? calibCurveOffsetAt(rs->calib[CH_TEMP], rs->rawValue[CH_TEMP]) : 0.0f,
		         hOk ? String(hRead, 2).c_str( ) : "null",
		         rs ? calibCurveOffsetAt(rs->calib[CH_HUM], rs->rawValue[CH_HUM]) : 0.0f,
		         pOk ? String(pRead, 2).c_str( ) : "null",
		         rs ? calibCurveOffsetAt(rs->calib[CH_PRESS], rs->rawValue[CH_PRESS]) : 0.0f);
		if (!safeSend(buf)) return;

		/* The generic form. Everything above is the closed set of fields the
		 * page used to need one of per quantity; a client that iterates this
		 * array instead needs no change when a channel is added. Kept side by
		 * side for one release so a cached page keeps working — the fixed
		 * fields go away once nothing reads them. */
		if (!safeSend(",\"channels\":[")) return;
		bool firstCh = true;
		for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
			if (!sensorHasChannel((SensorType)cfg.sensors[i].sensorType, c)) continue;
			const ChannelInfo& ci = channelInfo(c);
			float v = rs ? rs->avgValue[c] : NAN;
			/* isfinite, not !isnan: the BMP280 humidity compensation yields
			 * +INF on a part with no humidity die, and isnan(inf) is false. */
			bool ok = isfinite(v) && v < 1e9f;
			snprintf(buf, sizeof(buf),
			         "%s{\"ch\":%u,\"key\":\"%s\",\"unit\":\"%s\",\"dec\":%u,"
			         "\"label\":\"%s\",\"read\":%s,\"offset\":%.2f}",
			         firstCh ? "" : ",", (unsigned)c, ci.key, ci.display.unit,
			         (unsigned)ci.display.decimals, ci.i18nKey,
			         ok ? String(v, 2).c_str( ) : "null",
			         rs ? calibCurveOffsetAt(rs->calib[c], rs->rawValue[c]) : 0.0f);
			if (!safeSend(buf)) return;
			firstCh = false;
		}
		if (!safeSend("]}")) return;
		first = false;
	}
	safeSend("]}");
}


/* Changes requested via POST /api/calib (fixed array, no heap). */
namespace {
struct CalibChange {
	char key[17];
	char id[18];      /* id to WRITE:  't'/'u' + current hwId + NUL */
	char matchId[18]; /* id to MATCH:  same, but with the hwId as it was on disk */
	float offset;
	char name[32];
	bool written;
};
/* 18, not 14: a ROM-less slot can now emit three rows (t/u/p) instead of two,
 * so the old cap silently dropped changes one sensor earlier. Each entry is
 * ~92 B of stack in a web handler, which is why this tracks the realistic
 * number of slots edited at once rather than MAX_SENSORS * 3. */
const int MAX_CHANGES = 18;

/* id == nullptr matches on the key alone — that is the ROM scheme, where the
 * ROM is the identity and the id column merely carries the hwId.
 *
 * Board-serial rows all share one key, so there the id column IS what tells
 * them apart and has to match. It matches against matchId, not id: when the
 * hwId is being renamed those differ, and comparing against the NEW id would
 * miss the row on disk — leaving the old one behind as an orphan while a
 * second one was appended. */
int findChangeMatch(CalibChange* arr, int n, const char* key, const char* id) {
	for (int i = 0; i < n; i++) {
		if (arr[i].key[0] == '\0') continue;
		if (strcasecmp(arr[i].key, key) != 0) continue;
		if (id && strcasecmp(arr[i].matchId, id) != 0) continue;
		return i;
	}
	return -1;
}
}


/* ===== POST /api/calib =====
 * Body JSON: {"sensors":[{"slot":0,"hwId":"FRIDGE","name":"Geladeira",
 *                         "refTemp":4.0,"refHum":55.0}]}
 * Rewrites calib.csv via streaming 2-pass; VERSION=current epoch.
 * NTP required (503 if !synced).
 *
 * Two key schemes, both per sensor. Neither is device-wide:
 *   DS18B20 (has a ROM)   -> key = ROM hex,  id = hwId
 *   DHT22 / BMP280 (none) -> key = picoUID,  id = t<hwId> / u<hwId>
 *
 * The second scheme used to hold exactly ONE pair of rows per board, found
 * by "first line whose id starts with t/u" and applied to "the first DHT22
 * in the runtime list". A board with two DHT22s could therefore only ever
 * calibrate one of them, and which one depended on slot order. Tagging the
 * rows with the sensor's own hwId gives each ROM-less sensor its own pair
 * without changing the file format. */

void WebManager::handleApiCalibPost( ) {
	if (!(getAuthPerms( ) & PERM_CALIB)) {
		_server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}
	/* Rate-limit 5s — flash-heavy operation (reads/rewrites calib.csv +
	 * saveConfiguration + reload). Protects against UI loop bugs and
	 * intentional flash-wear attacks by a user with PERM_CALIB. */
	if (isRateLimited(5000)) {
		_server.sendHeader("Retry-After", "5");
		_server.send(429, "application/json", "{\"error\":\"rate limited\"}");
		return;
	}
	if (!_netRef->isTimeSynced( )) {
		_server.send(503, "application/json", "{\"error\":\"NTP not synced\"}");
		return;
	}

	String body = _server.hasArg("plain") ? _server.arg("plain") : "";
	if (body.length( ) == 0) {
		_server.send(400, "application/json", "{\"error\":\"empty body\"}");
		return;
	}

	SystemConfig& cfg = _storageRef->getConfig( );
	String picoUID = StorageManager::getBoardSerialNumber( );
	const auto& runtime = _sensorRef->getRuntimeSensors( );

	CalibChange changes[MAX_CHANGES];
	for (int i = 0; i < MAX_CHANGES; i++) { changes[i].key[0] = '\0'; changes[i].written = false; }
	int nChanges = 0;

	/* === One entry per slot, whatever the sensor type === */
	int sensStart = body.indexOf("\"sensors\"");
	if (sensStart >= 0) {
		int arrStart = body.indexOf('[', sensStart);
		int arrEnd = body.indexOf(']', arrStart);
		if (arrStart >= 0 && arrEnd > arrStart) {
			String arr = body.substring(arrStart, arrEnd + 1);
			int objStart = 0;
			int safety = 0; /* Cap iterations on adversarial payloads. */
			while ((objStart = arr.indexOf('{', objStart)) >= 0) {
				if (++safety > MAX_SENSORS + 4) break;
				int objEnd = arr.indexOf('}', objStart);
				if (objEnd < 0) break;
				String obj = arr.substring(objStart, objEnd + 1);
				float slotF = -1.0f; if (!jsonExtractFloat(obj, "slot", slotF)) jsonExtractFloat(obj, "gpio", slotF);
				int slot = (int)slotF;
				objStart = objEnd + 1;
				if (slot < 0 || slot >= MAX_SENSORS) continue;
				if (!cfg.sensors[slot].active) continue;
				char newId[16] = {0}, newName[32] = {0};
				jsonExtractCStr(obj, "hwId", newId, sizeof(newId));
				jsonExtractCStr(obj, "name", newName, sizeof(newName));

				/* References, one per channel. Read from the generic
				 * `"refs":{"press":1013.2}` form first, then from the legacy
				 * refTemp/refHum/refPress keys so a page served from an older
				 * cache keeps working. Legacy names are a closed set — the new
				 * form needs no firmware edit for a new quantity. */
				float ref[MAX_SENSOR_CHANNELS];
				for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) ref[c] = NAN;
				int refsAt = obj.indexOf("\"refs\"");
				if (refsAt >= 0) {
					int braceAt = obj.indexOf('{', refsAt);
					int braceEnd = (braceAt >= 0) ? obj.indexOf('}', braceAt) : -1;
					if (braceAt >= 0 && braceEnd > braceAt) {
						String refsObj = obj.substring(braceAt, braceEnd + 1);
						for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
							jsonExtractFloat(refsObj, channelInfo(c).key, ref[c]);
						}
					}
				}
				jsonExtractFloat(obj, "refTemp",  ref[CH_TEMP]);
				jsonExtractFloat(obj, "refHum",   ref[CH_HUM]);
				jsonExtractFloat(obj, "refPress", ref[CH_PRESS]);

				/* Kept before the overwrite: it is the id the rows on disk were
				 * written under, and a rename has to find them by it. */
				char oldHwId[16];
				safeCopy(oldHwId, cfg.sensors[slot].hwId, sizeof(oldHwId));

				if (newId[0] != '\0') safeCopy(cfg.sensors[slot].hwId, newId, sizeof(cfg.sensors[slot].hwId));
				if (newName[0] != '\0') safeCopy(cfg.sensors[slot].friendlyName, newName, sizeof(cfg.sensors[slot].friendlyName));

				float cur[MAX_SENSOR_CHANNELS], newOff[MAX_SENSOR_CHANNELS];
				for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) { cur[c] = NAN; newOff[c] = 0.0f; }
				for (const auto& s : runtime) {
					if (s.config.pins[0] == cfg.sensors[slot].pins[0]) {
						for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
							cur[c]    = s.avgValue[c];
							newOff[c] = calibCurveOffsetAt(s.calib[c], s.rawValue[c]);
						}
						break;
					}
				}
				/* offset += reference - reading. A channel with no reference keeps
				 * the offset it had; one whose sensor is not reading keeps it too,
				 * because there is nothing to measure the difference against. */
				for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
					if (!isnan(ref[c]) && isfinite(cur[c])) newOff[c] += (ref[c] - cur[c]);
				}

				char nameSan[32];
				safeCopy(nameSan, cfg.sensors[slot].friendlyName, sizeof(nameSan));
				sanitizeName(nameSan);

				if (!isAllZero(cfg.sensors[slot].rom)) {
					/* 1-Wire: keyed by its own ROM. Temperature only — the part
					 * has no second channel to calibrate. */
					if (nChanges < MAX_CHANGES) {
						char romHex[17]; romToHex(cfg.sensors[slot].rom, romHex);
						safeCopy(changes[nChanges].key, romHex, sizeof(changes[0].key));
						safeCopy(changes[nChanges].id, cfg.sensors[slot].hwId, sizeof(changes[0].id));
						changes[nChanges].matchId[0] = '\0'; /* matched by ROM */
						changes[nChanges].offset = newOff[CH_TEMP];
						safeCopy(changes[nChanges].name, nameSan, sizeof(changes[0].name));
						changes[nChanges].written = false;
						nChanges++;
					}
				} else {
					/* No ROM: keyed by the board serial, one row per quantity,
					 * each tagged with this slot's hwId so a second DHT22 on the
					 * same board gets its own pair instead of overwriting the
					 * first one's. */
					/* One row per channel the sensor declares, letter from the
					 * channel table. The quantities are not a prefix of a fixed
					 * list — a BMP280 is t+p, with a hole where humidity would be —
					 * so this iterates the mask rather than counting. A new
					 * quantity needs no edit here. */
					const SensorType sType = (SensorType)cfg.sensors[slot].sensorType;
					for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
						if (!sensorHasChannel(sType, c) || nChanges >= MAX_CHANGES) continue;
						const char letter = channelInfo(c).letter;
						char pref[18], prefOld[18];
						snprintf(pref, sizeof(pref), "%c%s", letter, cfg.sensors[slot].hwId);
						snprintf(prefOld, sizeof(prefOld), "%c%s", letter, oldHwId);
						safeCopy(changes[nChanges].key, picoUID.c_str( ), sizeof(changes[0].key));
						safeCopy(changes[nChanges].id, pref, sizeof(changes[0].id));
						safeCopy(changes[nChanges].matchId, prefOld, sizeof(changes[0].matchId));
						changes[nChanges].offset = newOff[c];
						safeCopy(changes[nChanges].name, nameSan, sizeof(changes[0].name));
						changes[nChanges].written = false;
						nChanges++;
					}
				}
			}
		}
	}

	/* === Streaming 2-pass: read calib.csv, write calib.tmp === */
	uint32_t version = (uint32_t)_netRef->getEpoch( );
	if (nChanges > 0) {
	/* The open, the row writes and the close each program flash, and this
	 * block ran them with Core 1 alive and rendering — the one write path
	 * left outside the e035791 discipline. Four exposed ops per save, and
	 * whichever one collided with a Core-1 XIP fetch froze the QSPI
	 * arbiter with the WDT unfed: the "first calibration POST after boot
	 * reboots the device" repro. Same guard the upload path already uses;
	 * processCalibrationUpload( ) and saveConfiguration( ) below carry
	 * their own. */
	RenderGuard rg(_displayRef);
	/* The commit in processCalibrationUpload( ) only renames when the new
	 * version beats the stored one, so the stamp has to move forward on every
	 * save. getEpoch( ) does not guarantee that: with NTP down it falls back to
	 * the virtual RTC or to SIMUT_BUILD_EPOCH, both behind the real epoch that a
	 * previous synced save already wrote. The comparison then failed, the commit
	 * deleted calib.tmp, and the calibration was lost with a 500 — exactly when
	 * the network was flaky. Force monotonicity: keep epoch semantics while the
	 * clock is trustworthy, bump past the stored version when it is not.
	 * Safe here because /api/calib rewrites the device's own file from the
	 * config it already holds — the version gate guards the upload path, where
	 * a stale file from another device can legitimately arrive, not this one.
	 * Inside the nChanges guard so a no-op save keeps reporting the plain epoch
	 * instead of an increment that never reached flash. */
	long storedVer = _storageRef->getCalibrationVersion("/calib.csv");
	if (storedVer >= (long)version) version = (uint32_t)(storedVer + 1);

	File fout = LittleFS.open("/calib.tmp", "w");
	if (!fout) {
		_server.send(500, "application/json", "{\"error\":\"calib write failed\"}");
		return;
	}
	fout.printf("VERSION,%lu\n", (unsigned long)version);

	File fin = LittleFS.open("/calib.csv", "r");
	if (fin) {
		char lineBuf[256];
		while (fin.available( )) {
			feedWdt( );
			size_t len = fin.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
			if (len == 0) continue;
			lineBuf[len] = '\0';
			if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
			if (lineBuf[0] == '\0') continue;
			if (strncmp(lineBuf, "VERSION,", 8) == 0) continue;

			char* p1 = strchr(lineBuf, ',');
			if (!p1) continue;
			*p1 = '\0';
			char* keyStr = lineBuf;
			char* idStr = p1 + 1;
			/* Terminate the id column too. Splitting off only the key left idStr
			 * as "<id>,<offset>,<name>", which never compares equal to a bare
			 * matchId — so every board-serial row missed its change, was copied
			 * through untouched, and the change was appended at the end instead.
			 * getCalibrationByHwId stops at the first match, so it kept reading
			 * the stale row: calibrating a ROM-less sensor silently did nothing
			 * while calib.csv grew by one row per sensor per save. Harmless
			 * before 4cff8ca, which only ever read idStr[0]. */
			char* p2 = strchr(idStr, ',');
			if (p2) *p2 = '\0';

			int idx;
			/* Any letter the channel table claims, rather than a literal list
			 * that has to grow with every quantity — the list form is what left
			 * pressure rows unrecognized here while they were being written. */
			if (strcasecmp(keyStr, picoUID.c_str( )) == 0
			    && channelByLetter(idStr[0]) >= 0) {
				/* Board-serial rows: every ROM-less sensor on this board shares
				 * the key, so the id column is what tells them apart and has to
				 * match in full. Matching only the leading 't'/'u' was safe while
				 * exactly one pair existed per device; with one pair per sensor it
				 * would overwrite the first DHT22's row using the second's offset. */
				idx = findChangeMatch(changes, nChanges, keyStr, idStr);
			} else {
				idx = findChangeMatch(changes, nChanges, keyStr, nullptr);
			}

			if (idx >= 0) {
				/* Duplicate rows left on disk by the bug above: the change was
				 * already emitted on the first row that matched it, so drop the
				 * rest rather than re-emitting them. A file written before the
				 * fix collapses back to one row per sensor on its next save. */
				if (changes[idx].written) continue;
				fout.printf("%s,%s,%.2f,%s\n",
				            changes[idx].key, changes[idx].id, changes[idx].offset, changes[idx].name);
				changes[idx].written = true;
			} else {
				*p1 = ',';
				if (p2) *p2 = ',';
				fout.printf("%s\n", lineBuf);
			}
		}
		fin.close( );
	}

	for (int i = 0; i < nChanges; i++) {
		if (!changes[i].written && changes[i].key[0] != '\0') {
			fout.printf("%s,%s,%.2f,%s\n",
			            changes[i].key, changes[i].id, changes[i].offset, changes[i].name);
		}
	}
	fout.close( );
	} /* end if (nChanges > 0) */

	if (nChanges > 0) {
	 if (!_storageRef->processCalibrationUpload( )) {
	  _server.send(500, "application/json", "{\"error\":\"calib commit failed\"}");
	  return;
	 }
	 _displayRef->requestQuietMode( );
	}
	_storageRef->saveConfiguration( );
	if (nChanges > 0) { app.loadAndCalibrateSensors( );_displayRef->releaseQuietMode( );}
  else { /* Sync hwId/name to runtime sensors without full reload */
   auto& runtime = _sensorRef->getRuntimeSensors( );
   for (auto& rs : runtime) {
    for (int i = 0; i < MAX_SENSORS; i++) {
     if (cfg.sensors[i].active && cfg.sensors[i].pins[0] == rs.config.pins[0]) {
      safeCopy((char*)rs.config.hwId, cfg.sensors[i].hwId, sizeof(rs.config.hwId));
      safeCopy((char*)rs.config.friendlyName, cfg.sensors[i].friendlyName, sizeof(rs.config.friendlyName));
     }
    }
   }
  }

	LOG_CODE(LOG_INFO, "WEB", APP_SENSORS_CALIBRATED, 0, "calib via /api/calib");

	char resp[80];
	snprintf(resp, sizeof(resp), "{\"ok\":true,\"version\":%lu}", (unsigned long)version);
	_server.send(200, "application/json", resp);
}

/* ─────────────────────────────────────────────────────────────────
 * POST /api/action?op=<...>
 *
 * The five maintenance operations that had no web equivalent when the serial
 * CLI was reduced to its emergency set. They are grouped behind one route and
 * an ?op= selector because each additional _server.on( ) costs flash for the
 * WebServer's per-route bookkeeping, and all six share the same preamble.
 *
 * The scan is driven straight through _sensorRef rather than through
 * AppManager: startScan( ) only arms a state machine that the main loop steps,
 * so the handler never blocks, and the CLI's follow-up loadAndCalibrateSensors( )
 * is not needed here — a scan discovers, it does not configure. (In a
 * SIMUT_CLI_FULL image a serial `sensor scan` running at the same time would
 * consume the results first; that is a test-image race with no user impact.)
 * ───────────────────────────────────────────────────────────────── */
void WebManager::handleApiAction( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_SYS_CONFIG)) {
		_server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}
	String op = _server.arg("op");

	/* ── Telemetry ── */
	if (op == "tel_sync") {
		if (!_telemetryRef) { _server.send(503, "application/json", "{\"error\":\"unavailable\"}"); return; }
		_telemetryRef->forceSync( );
		LOG_CODE(LOG_INFO, "WEB", TEL_FORCE_SYNC, _currentUserId, TRL("Forcing telemetry sync"));
		_server.send(200, "application/json", "{\"ok\":true}");
		return;
	}
	if (op == "tel_reset") {
		_storageRef->resetTelemetryCursor( );
		LOG_CODE(LOG_WARN, "WEB", SEC_CONFIG_CHANGED, _currentUserId,
		         TRL("Telemetry cursor reset via web"));
		_server.send(200, "application/json", "{\"ok\":true}");
		return;
	}

	/* ── Sensor discovery ── */
	if (op == "sensor_scan") {
		if (!_sensorRef) { _server.send(503, "application/json", "{\"error\":\"unavailable\"}"); return; }
		if (_sensorRef->isScanning( )) { _server.send(200, "application/json", "{\"busy\":true}"); return; }
		_sensorRef->startScan( );
		_server.send(202, "application/json", "{\"started\":true}");
		return;
	}
	if (op == "scan_results") {
		if (!_sensorRef) { _server.send(503, "application/json", "{\"error\":\"unavailable\"}"); return; }
		std::vector<ScanResult> results;
		if (!_sensorRef->getScanResults(results)) {
			_server.send(200, "application/json", "{\"scanning\":true}");
			return;
		}
		String out = "{\"scanning\":false,\"found\":[";
		for (size_t i = 0; i < results.size( ); i++) {
			char rom[20] = {0};
			if (results[i].type == TYPE_DS18B20) {
				for (int b = 0; b < 8; b++) snprintf(rom + b * 2, 3, "%02X", results[i].rom[b]);
			}
			char item[64];
			snprintf(item, sizeof(item), "%s{\"pin\":%u,\"type\":%d,\"rom\":\"%s\"}",
			         i ? "," : "", (unsigned)results[i].pin, (int)results[i].type, rom);
			out += item;
		}
		out += "]}";
		_server.send(200, "application/json", out);
		return;
	}

	/* ── Slot maintenance ──
	 * The op is validated before the slot. Checking the slot first made an
	 * unrecognised op answer {"error":"slot"}, which points the caller at the
	 * wrong parameter — it reads as "my slot is malformed" when the real
	 * problem is a typo in the op name. */
	if (op != "sensor_wipe" && op != "sensor_accept") {
		_server.send(400, "application/json", "{\"error\":\"op\"}");
		return;
	}
	int slot = _server.hasArg("slot") ? _server.arg("slot").toInt( ) : -1;
	if (slot < 0 || slot >= MAX_SENSORS) {
		_server.send(400, "application/json", "{\"error\":\"slot\"}");
		return;
	}
	SystemConfig& cfg = _storageRef->getConfig( );

	if (op == "sensor_wipe") {
		/* Same semantics as the old `sensor wipe <gpio> confirm`: move the
		 * slot's provisioning epoch to now, so history before this instant is
		 * no longer attributed to the sensor sitting there. */
		cfg.sensors[slot].provisionEpoch = _netRef ? (uint32_t)_netRef->getEpoch( ) : 0;
		_storageRef->saveConfiguration( );
		LOG_CODE(LOG_WARN, "WEB", SEC_CONFIG_CHANGED, _currentUserId,
		         TRL("Sensor history epoch reset via web"));
		_server.send(200, "application/json", "{\"ok\":true}");
		return;
	}

	if (op == "sensor_accept") {
#if SIMUT_SENSOR_DS18B20
		uint8_t foundRom[8];
		if (!_sensorRef || !_sensorRef->identifyPhysicalSensor((uint8_t)slot, foundRom)) {
			_server.send(404, "application/json", "{\"error\":\"nosensor\"}");
			return;
		}
		if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) {
			_server.send(422, "application/json", "{\"error\":\"badrom\"}");
			return;
		}
		String dbId, dbName; CalibCurve dbCurve;
		_storageRef->getCalibrationData(foundRom, dbId, dbCurve, dbName);

		String currentId = String(cfg.sensors[slot].hwId);
		cfg.sensors[slot].active = true;
		cfg.sensors[slot].pins[0] = (uint8_t)slot;
		cfg.sensors[slot].sensorType = TYPE_DS18B20;
		memcpy(cfg.sensors[slot].rom, foundRom, 8);
		safeCopy(cfg.sensors[slot].hwId,
		         dbId.length( ) ? dbId.c_str( ) : "LIB_SENS", sizeof(cfg.sensors[slot].hwId));
		if (dbName.length( )) {
			safeCopy(cfg.sensors[slot].friendlyName, dbName.c_str( ),
			         sizeof(cfg.sensors[slot].friendlyName));
		}
		/* A different hwId means a different physical part in that slot, so the
		 * history before now belongs to the old one. */
		bool epochMoved = (currentId != String(cfg.sensors[slot].hwId));
		if (epochMoved) {
			cfg.sensors[slot].provisionEpoch = _netRef ? (uint32_t)_netRef->getEpoch( ) : 0;
		}
		_storageRef->saveConfiguration( );
		LOG_CODE(LOG_WARN, "WEB", SEC_CONFIG_CHANGED, _currentUserId,
		         TRL("Hardware match restored"));
		char resp[64];
		snprintf(resp, sizeof(resp), "{\"ok\":true,\"epoch_moved\":%s}",
		         epochMoved ? "true" : "false");
		_server.send(200, "application/json", resp);
#else
		_server.send(501, "application/json", "{\"error\":\"nods18b20\"}");
#endif
		return;
	}

	/* Unreachable: the op was whitelisted above. Kept as a belt-and-braces
	 * answer so a future op added to that whitelist without a body here fails
	 * loudly instead of returning an empty 200. */
	_server.send(400, "application/json", "{\"error\":\"op\"}");
}
