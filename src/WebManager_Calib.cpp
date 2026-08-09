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
#include "WebJsonSlice.h"
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
			float rv = rs ? rs->rawValue[c] : NAN;
			/* isfinite, not !isnan: the BMP280 humidity compensation yields
			 * +INF on a part with no humidity die, and isnan(inf) is false. */
			bool ok = isfinite(v) && v < 1e9f;
			bool rawOk = isfinite(rv) && rv < 1e9f;
			/* raw feeds the editor's capture button; min/max echo the same
			 * bounds the POST enforces, so the page can warn before the 400. */
			snprintf(buf, sizeof(buf),
			         "%s{\"ch\":%u,\"key\":\"%s\",\"unit\":\"%s\",\"dec\":%u,"
			         "\"label\":\"%s\",\"read\":%s,\"raw\":%s,\"offset\":%.2f,"
			         "\"min\":%.2f,\"max\":%.2f,\"pts\":[",
			         firstCh ? "" : ",", (unsigned)c, ci.key, ci.display.unit,
			         (unsigned)ci.display.decimals, ci.i18nKey,
			         ok ? String(v, 2).c_str( ) : "null",
			         rawOk ? String(rv, 2).c_str( ) : "null",
			         rs ? calibCurveOffsetAt(rs->calib[c], rs->rawValue[c]) : 0.0f,
			         ci.saneMin, ci.saneMax);
			if (!safeSend(buf)) return;
			/* Points streamed one pair at a time — a 5-point entry inlined in
			 * the snprintf above would flirt with the buffer. An anchor-free
			 * n=1 (legacy offset column) sends pts:[] with offset != 0, which
			 * is the UI's "constant offset, origin unknown" state. */
			if (rs) {
				const CalibCurve& cv = rs->calib[c];
				if (!(cv.n == 1 && !isfinite(cv.raw[0]))) {
					for (uint8_t k = 0; k < cv.n; k++) {
						char pbuf[48];
						snprintf(pbuf, sizeof(pbuf), "%s[%.2f,%.2f]",
						         k ? "," : "", cv.raw[k], cv.raw[k] + cv.off[k]);
						if (!safeSend(pbuf)) return;
					}
				}
			}
			if (!safeSend("]}")) return;
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
	CalibCurve curve; /* n=0 identity, n=1 constant offset, n>=2 piecewise */
	bool drop;        /* identity on a ROM-less row: remove it instead of writing 0.00 */
	char name[32];
	bool written;
};
/* 18, not 14: a ROM-less slot can now emit three rows (t/u/p) instead of two,
 * so the old cap silently dropped changes one sensor earlier. It tracks the
 * realistic number of slots edited at once rather than MAX_SENSORS * 3. */
const int MAX_CHANGES = 18;

/* File-scope, not handler stack: with the curve inside, the array is ~2.4 KB,
 * and Core-0 handler stacks are a place this project has already bled
 * (250428e). The WebServer is single-threaded on Core 0, so one static
 * instance can never be entered twice. */
static CalibChange s_changes[MAX_CHANGES];

/* hwId/name renames parsed in the validation pass, applied only after the
 * whole payload validates — the walk used to write them into cfg as it went,
 * so a 400 halfway through left RAM config half-renamed and unsaved. */
struct PendIdent { int slot; char hwId[16]; char name[32]; };
static PendIdent s_idents[MAX_SENSORS];

/* One row of calib.csv. The offset column and the pts column are written to
 * back each other: n=1 mirrors its offset into the legacy column (older
 * firmware reads that row exactly as before), n>=2 has no single-offset
 * equivalent and writes 0.00 there. An empty pts keeps the row at 4 columns
 * with no trailing comma — older readers take everything after the third
 * comma as the name. */
static void emitCalibRow(File& f, const CalibChange& ch) {
	char pts[CALIB_PTS_BUF];
	calibCurveEncodePts(ch.curve, pts, sizeof(pts));
	const float offCol = (ch.curve.n == 1) ? ch.curve.off[0] : 0.0f;
	if (pts[0] != '\0') f.printf("%s,%s,%.2f,%s,%s\n", ch.key, ch.id, offCol, ch.name, pts);
	else                f.printf("%s,%s,%.2f,%s\n",    ch.key, ch.id, offCol, ch.name);
}

/* Parse `"<chKey>":[[raw,ref],...]` inside the cal{} object.
 * Returns 0 when the key is absent (channel untouched), 1 on success
 * (count may be 0 — an explicit [] means "clear the correction"), -1 on
 * anything malformed. A null (or empty) raw marks "capture the current
 * reading at save time"; the caller substitutes rawValue. */
static int extractCalPairs(const String& calObj, const char* chKey,
                           float* raws, float* refs, bool* rawIsNull, uint8_t& outCount) {
	outCount = 0;
	char needle[24]; snprintf(needle, sizeof(needle), "\"%s\":", chKey);
	int p = calObj.indexOf(needle);
	if (p < 0) return 0;
	int s = p + (int)strlen(needle);
	while (s < (int)calObj.length( ) && (calObj[s] == ' ' || calObj[s] == '\t')) s++;
	if (s >= (int)calObj.length( ) || calObj[s] != '[') return -1;
	int e = jsonMatchEnd(calObj, s);
	if (e < 0) return -1;

	int pos = s + 1;
	while (true) {
		int b = calObj.indexOf('[', pos);
		if (b < 0 || b >= e) break;
		int be = jsonMatchEnd(calObj, b);
		if (be < 0 || be >= e) return -1;
		if (outCount >= CALIB_MAX_POINTS) return -1; /* a sixth pair */
		String pair = calObj.substring(b + 1, be);
		int comma = pair.indexOf(',');
		if (comma < 0) return -1;
		String rawTok = pair.substring(0, comma); rawTok.trim( );
		String refTok = pair.substring(comma + 1); refTok.trim( );
		float rv = NAN, fv = NAN;
		bool rNull = (rawTok.length( ) == 0 || rawTok == "null");
		if (!rNull && !parseFloatStrict(rawTok, rv)) return -1;
		if (!parseFloatStrict(refTok, fv)) return -1;
		rawIsNull[outCount] = rNull;
		raws[outCount] = rv;
		refs[outCount] = fv;
		outCount++;
		pos = be + 1;
	}
	return 1;
}

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
 *                         "cal":{"temp":[[20.10,20.00],[35.40,35.00]],
 *                                "hum":[]}}]}
 *
 * "cal" carries up to CALIB_MAX_POINTS [raw,ref] pairs per channel key:
 *   - channel absent from cal{}  -> its stored curve is carried through
 *   - explicit []                -> correction removed (sensor default)
 *   - raw null                   -> captured from the live raw reading at save
 * Legacy refTemp/refHum/refPress and "refs":{...} (cached pages) still parse
 * and now mean an ABSOLUTE one-point set at the current raw reading — the old
 * `offset += ref - reading` accumulator is gone with the offsets themselves.
 *
 * Validation is a separate first pass: nothing touches cfg and nothing
 * touches flash until the whole payload has parsed and range-checked, so a
 * 400 always leaves the device exactly as it was.
 *
 * Rewrites calib.csv via streaming 2-pass; VERSION=current epoch.
 * NTP required (503 if !synced).
 *
 * Two key schemes, both per sensor. Neither is device-wide:
 *   DS18B20 (has a ROM)   -> key = ROM hex,  id = hwId   (row kept even with
 *                            no correction — it doubles as the ROM->hwId/name
 *                            identity database `sensor accept` reads)
 *   DHT22 / BMP280 (none) -> key = picoUID,  id = t<hwId> / u<hwId>
 *                            (identity rows are dropped instead: the id
 *                            column already carries the hwId)
 *
 * The second scheme used to hold exactly ONE pair of rows per board, found
 * by "first line whose id starts with t/u" and applied to "the first DHT22
 * in the runtime list". A board with two DHT22s could therefore only ever
 * calibrate one of them, and which one depended on slot order. Tagging the
 * rows with the sensor's own hwId gives each ROM-less sensor its own rows
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
	/* The page sends only edited slots, so a legitimate body is well under a
	 * kilobyte. Anything bigger is a bug or an attack; bounce it before the
	 * substring copies below double it on the heap. */
	if (body.length( ) > 8192) {
		_server.send(413, "application/json", "{\"error\":\"payload too large\"}");
		return;
	}

	SystemConfig& cfg = _storageRef->getConfig( );
	String picoUID = StorageManager::getBoardSerialNumber( );
	const auto& runtime = _sensorRef->getRuntimeSensors( );

	CalibChange* const changes = s_changes;
	for (int i = 0; i < MAX_CHANGES; i++) { changes[i].key[0] = '\0'; changes[i].written = false; changes[i].drop = false; }
	int nChanges = 0;
	int nIdents = 0;
	char err[96];
	err[0] = '\0';

	/* === Pass A: parse + validate. No cfg writes, no flash. ===
	 * One entry per slot, whatever the sensor type. */
	int sensStart = body.indexOf("\"sensors\"");
	if (sensStart >= 0) {
		int arrStart = body.indexOf('[', sensStart);
		/* jsonMatchEnd on both cuts: the cal{} pair arrays put ']' and '}'
		 * inside the elements, where indexOf found the "end" of everything. */
		int arrEnd = (arrStart >= 0) ? jsonMatchEnd(body, arrStart) : -1;
		if (arrStart >= 0 && arrEnd > arrStart) {
			String arr = body.substring(arrStart, arrEnd + 1);
			int objStart = 0;
			int safety = 0; /* Cap iterations on adversarial payloads. */
			while ((objStart = arr.indexOf('{', objStart)) >= 0) {
				if (++safety > MAX_SENSORS + 4) break;
				int objEnd = jsonMatchEnd(arr, objStart);
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
				if (nIdents < MAX_SENSORS) {
					s_idents[nIdents].slot = slot;
					safeCopy(s_idents[nIdents].hwId, newId, sizeof(s_idents[0].hwId));
					safeCopy(s_idents[nIdents].name, newName, sizeof(s_idents[0].name));
					nIdents++;
				}

				/* Legacy references, one per channel: the generic
				 * `"refs":{"press":1013.2}` form first, then refTemp/refHum/
				 * refPress, so a page served from an older cache keeps working. */
				float ref[MAX_SENSOR_CHANNELS];
				for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) ref[c] = NAN;
				int refsAt = obj.indexOf("\"refs\"");
				if (refsAt >= 0) {
					int braceAt = obj.indexOf('{', refsAt);
					int braceEnd = (braceAt >= 0) ? jsonMatchEnd(obj, braceAt) : -1;
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

				/* The cal{} object, if any. */
				String calObj = "";
				{
					int calAt = obj.indexOf("\"cal\"");
					if (calAt >= 0) {
						int b = obj.indexOf('{', calAt);
						int e = (b >= 0) ? jsonMatchEnd(obj, b) : -1;
						if (b >= 0 && e > b) calObj = obj.substring(b, e + 1);
					}
				}

				/* Kept from the stored config: it is the id the rows on disk
				 * were written under, and a rename has to find them by it. */
				char oldHwId[16];
				safeCopy(oldHwId, cfg.sensors[slot].hwId, sizeof(oldHwId));

				const RuntimeSensor* rsp = nullptr;
				for (const auto& s : runtime) {
					if (s.config.pins[0] == cfg.sensors[slot].pins[0]) { rsp = &s; break; }
				}

				/* Per channel: cal{} wins, legacy ref falls back to a one-point
				 * ABSOLUTE set at the current raw reading, anything else carries
				 * the stored curve through — a pure rename must not lose it. */
				const SensorType sType = (SensorType)cfg.sensors[slot].sensorType;
				CalibCurve newCurve[MAX_SENSOR_CHANNELS];
				for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
					if (!sensorHasChannel(sType, c)) continue;
					const ChannelInfo& ci = channelInfo(c);
					if (rsp) newCurve[c] = rsp->calib[c];

					float praw[CALIB_MAX_POINTS], pref[CALIB_MAX_POINTS];
					bool pnull[CALIB_MAX_POINTS];
					uint8_t pcount = 0;
					int got = (calObj.length( ) > 0)
					          ? extractCalPairs(calObj, ci.key, praw, pref, pnull, pcount) : 0;
					if (got < 0) {
						snprintf(err, sizeof(err), "slot %d %s: bad calibration points", slot, ci.key);
						break;
					}
					if (got == 1) {
						for (uint8_t k = 0; k < pcount; k++) {
							if (pnull[k]) {
								const float rv = rsp ? rsp->rawValue[c] : NAN;
								if (!isfinite(rv) || rv >= 1e9f) {
									snprintf(err, sizeof(err), "slot %d %s: no live reading to capture", slot, ci.key);
									break;
								}
								praw[k] = rv;
							}
						}
						if (err[0]) break;
						for (uint8_t k = 0; k < pcount; k++) {
							if (praw[k] < ci.saneMin || praw[k] > ci.saneMax ||
							    pref[k] < ci.saneMin || pref[k] > ci.saneMax) {
								snprintf(err, sizeof(err), "slot %d %s: point out of range", slot, ci.key);
								break;
							}
						}
						if (err[0]) break;
						if (!calibCurveBuild(newCurve[c], praw, pref, pcount)) {
							snprintf(err, sizeof(err), "slot %d %s: duplicate calibration points", slot, ci.key);
							break;
						}
					} else if (!isnan(ref[c])) {
						/* Legacy path. No reading -> no change, same as always:
						 * there is nothing to anchor the point to. */
						const float rv = rsp ? rsp->rawValue[c] : NAN;
						if (isfinite(rv) && rv < 1e9f) {
							float r1 = rv, v1 = ref[c];
							if (!calibCurveBuild(newCurve[c], &r1, &v1, 1)) {
								snprintf(err, sizeof(err), "slot %d %s: bad reference", slot, ci.key);
								break;
							}
						}
					}
				}
				if (err[0]) break;

				char nameSan[32];
				safeCopy(nameSan, (newName[0] != '\0') ? newName : cfg.sensors[slot].friendlyName,
				         sizeof(nameSan));
				sanitizeName(nameSan);
				char effId[16];
				safeCopy(effId, (newId[0] != '\0') ? newId : oldHwId, sizeof(effId));

				if (!isAllZero(cfg.sensors[slot].rom)) {
					/* 1-Wire: keyed by its own ROM. Temperature only — the part
					 * has no second channel to calibrate. The row is written even
					 * for an identity curve: it is also the ROM->hwId/name
					 * identity database that `sensor accept` reads. */
					if (nChanges >= MAX_CHANGES) {
						snprintf(err, sizeof(err), "too many changes in one save");
						break;
					}
					char romHex[17]; romToHex(cfg.sensors[slot].rom, romHex);
					safeCopy(changes[nChanges].key, romHex, sizeof(changes[0].key));
					safeCopy(changes[nChanges].id, effId, sizeof(changes[0].id));
					changes[nChanges].matchId[0] = '\0'; /* matched by ROM */
					changes[nChanges].curve = newCurve[CH_TEMP];
					changes[nChanges].drop = false;
					safeCopy(changes[nChanges].name, nameSan, sizeof(changes[0].name));
					changes[nChanges].written = false;
					nChanges++;
				} else {
					/* No ROM: keyed by the board serial, one row per quantity the
					 * sensor declares, each tagged with this slot's hwId so a
					 * second DHT22 on the same board gets its own rows. Driven by
					 * the channel mask — a BMP280 is t+p with a hole at humidity —
					 * so a new quantity needs no edit here. An identity curve
					 * drops the row instead of parking a 0.00 in the file. */
					const SensorType sT = (SensorType)cfg.sensors[slot].sensorType;
					for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
						if (!sensorHasChannel(sT, c)) continue;
						if (nChanges >= MAX_CHANGES) {
							snprintf(err, sizeof(err), "too many changes in one save");
							break;
						}
						const char letter = channelInfo(c).letter;
						char pref2[18], prefOld[18];
						snprintf(pref2, sizeof(pref2), "%c%s", letter, effId);
						snprintf(prefOld, sizeof(prefOld), "%c%s", letter, oldHwId);
						safeCopy(changes[nChanges].key, picoUID.c_str( ), sizeof(changes[0].key));
						safeCopy(changes[nChanges].id, pref2, sizeof(changes[0].id));
						safeCopy(changes[nChanges].matchId, prefOld, sizeof(changes[0].matchId));
						changes[nChanges].curve = newCurve[c];
						changes[nChanges].drop = calibCurveIsIdentity(newCurve[c]);
						safeCopy(changes[nChanges].name, nameSan, sizeof(changes[0].name));
						changes[nChanges].written = false;
						nChanges++;
					}
					if (err[0]) break;
				}
			}
		}
	}

	if (err[0]) {
		char resp[160];
		snprintf(resp, sizeof(resp), "{\"error\":\"%s\"}", err);
		_server.send(400, "application/json", resp);
		return; /* cfg and calib.csv untouched */
	}

	/* === Pass B: the payload validated — now the renames may land. === */
	for (int k = 0; k < nIdents; k++) {
		SensorRecord& sr = cfg.sensors[s_idents[k].slot];
		if (s_idents[k].hwId[0] != '\0') safeCopy(sr.hwId, s_idents[k].hwId, sizeof(sr.hwId));
		if (s_idents[k].name[0] != '\0') safeCopy(sr.friendlyName, s_idents[k].name, sizeof(sr.friendlyName));
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
		char lineBuf[320]; /* sized for a 5-point pts column, see StorageManager */
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
				/* A dropped row (correction removed on a ROM-less sensor) is
				 * consumed without being copied — that IS the removal. */
				if (!changes[idx].drop) emitCalibRow(fout, changes[idx]);
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
		if (!changes[i].written && changes[i].key[0] != '\0' && !changes[i].drop) {
			emitCalibRow(fout, changes[i]);
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
