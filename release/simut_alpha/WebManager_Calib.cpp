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


/* ===== GET /api/calib ===== */
void WebManager::handleApiCalibGet( ) {
	if (!(getAuthPerms( ) & PERM_CALIB)) {
		_server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}

	SystemConfig& cfg = _storageRef->getConfig( );
	bool ntpOk = _netRef->isTimeSynced( );
	long calibVer = _storageRef->getCalibrationVersion("/calib.csv");

	float runtimeT[MAX_SENSORS]; bool runtimeTValid[MAX_SENSORS];
	float runtimeH[MAX_SENSORS]; bool runtimeHValid[MAX_SENSORS];
	for (int i = 0; i < MAX_SENSORS; i++) { runtimeT[i] = NAN; runtimeTValid[i] = false; runtimeH[i] = NAN; runtimeHValid[i] = false; }
	float ambT = NAN, ambH = NAN;
	bool ambTValid = false, ambHValid = false;
	float ambOffT = 0.0f, ambOffH = 0.0f;
	float slotOff[MAX_SENSORS] = {0};
	float slotOffH[MAX_SENSORS] = {0};

	const auto& runtime = _sensorRef->getRuntimeSensors( );
	for (const auto& s : runtime) {
		if (s.type == TYPE_DHT22) {
			ambT = s.avgValue[0]; ambTValid = !isnan(s.avgValue[0]);
			ambH = s.avgValue[1]; ambHValid = !isnan(s.avgValue[1]);
			ambOffT = s.calibrationOffset[0];
			ambOffH = s.calibrationOffset[1];
		}
		if (s.config.pins[0] < MAX_SENSORS) {
			runtimeT[s.config.pins[0]] = s.avgValue[0];
			runtimeTValid[s.config.pins[0]] = !isnan(s.avgValue[0]);
			slotOff[s.config.pins[0]] = s.calibrationOffset[0];
			/* Humidity per-slot for DHT22/BME280 sensors */
			if (sensorHasHumidity(s.type)) {
				runtimeH[s.config.pins[0]] = s.avgValue[1];
				runtimeHValid[s.config.pins[0]] = !isnan(s.avgValue[1]);
				slotOffH[s.config.pins[0]] = s.calibrationOffset[1];
			}
		}
	}

	_server.sendHeader("Cache-Control", "no-store");
	_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	_server.send(200, "application/json", "");

	char buf[400];
	snprintf(buf, sizeof(buf),
	         "{\"ntp\":%s,\"calibVersion\":%ld,\"picoUID\":\"%s\",",
	         ntpOk ? "true" : "false", calibVer,
	         StorageManager::getBoardSerialNumber( ).c_str( ));
	if (!safeSend(buf)) return;

	/* Slot 10 (humidity-capable sensor) calibration data */
	{
		char ambName[40], ambHwId[20];
		safeCopy(ambName, cfg.sensors[10].friendlyName, sizeof(ambName));
		safeCopy(ambHwId, cfg.sensors[10].hwId, sizeof(ambHwId));
		sanitizeName(ambName);
		snprintf(buf, sizeof(buf),
		         "\"ambient\":{\"hwId\":\"%s\",\"name\":\"%s\","
		         "\"tempRead\":%s,\"humRead\":%s,\"tempOffset\":%.2f,\"humOffset\":%.2f},",
		         ambHwId, ambName,
		         ambTValid ? String(ambT, 2).c_str( ) : "null",
		         ambHValid ? String(ambH, 2).c_str( ) : "null",
		         ambOffT, ambOffH);
	}
	if (!safeSend(buf)) return;

	if (!safeSend("\"sensors\":[")) return;
	bool first = true;
	for (int i = 0; i < MAX_SENSORS; i++) {
		if (!cfg.sensors[i].active) continue;
		char romHex[17] = {0};
		romToHex(cfg.sensors[i].rom, romHex);
		char sName[40], sHwId[20];
		safeCopy(sName, cfg.sensors[i].friendlyName, sizeof(sName));
		safeCopy(sHwId, cfg.sensors[i].hwId, sizeof(sHwId));
		sanitizeName(sName);
		bool hasH = sensorHasHumidity((SensorType)cfg.sensors[i].sensorType);
		snprintf(buf, sizeof(buf),
		         "%s{\"gpio\":%d,\"rom\":\"%s\",\"hwId\":\"%s\",\"name\":\"%s\","
		         "\"hasHum\":%s,"
		         "\"tempRead\":%s,\"tempOffset\":%.2f,"
		         "\"humRead\":%s,\"humOffset\":%.2f}",
		         first ? "" : ",", i, romHex, sHwId, sName,
		         hasH ? "true" : "false",
		         runtimeTValid[i] ? String(runtimeT[i], 2).c_str( ) : "null",
		         slotOff[i],
		         runtimeHValid[i] ? String(runtimeH[i], 2).c_str( ) : "null",
		         slotOffH[i]);
		if (!safeSend(buf)) return;
		first = false;
	}
	safeSend("]}");
	safeSend("");
}


/* Changes requested via POST /api/calib (fixed array, no heap). */
namespace {
struct CalibChange {
	char key[17];
	char id[16];
	float offset;
	char name[32];
	bool written;
};
const int MAX_CHANGES = 14;

int findChangeMatch(CalibChange* arr, int n, const char* key, char idPrefix) {
	for (int i = 0; i < n; i++) {
		if (arr[i].key[0] == '\0') continue;
		if (strcasecmp(arr[i].key, key) != 0) continue;
		if (idPrefix != 0 && arr[i].id[0] != idPrefix) continue;
		return i;
	}
	return -1;
}
}


/* ===== POST /api/calib =====
 * Body JSON: {"ambient":{"hwId":"01","name":"Sala","refTemp":25.0,"refHum":50.0},
 * "sensors":[{"gpio":0,"hwId":"FRIDGE","name":"Geladeira","refTemp":4.0}]}
 * Rewrites calib.csv via streaming 2-pass; VERSION=current epoch.
 * NTP required (503 if !synced). */
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

	CalibChange changes[MAX_CHANGES];
	for (int i = 0; i < MAX_CHANGES; i++) { changes[i].key[0] = '\0'; changes[i].written = false; }
	int nChanges = 0;

	/* === SLOT 10 (humidity-capable) === */
	int ambStart = body.indexOf("\"ambient\"");
	if (ambStart >= 0) {
		int objStart = body.indexOf('{', ambStart);
		int objEnd = body.indexOf('}', objStart);
		if (objStart >= 0 && objEnd > objStart) {
			String obj = body.substring(objStart, objEnd + 1);
			char newId[16] = {0}, newName[32] = {0};
			float refT = NAN, refH = NAN;
			jsonExtractCStr(obj, "hwId", newId, sizeof(newId));
			jsonExtractCStr(obj, "name", newName, sizeof(newName));
			jsonExtractFloat(obj, "refTemp", refT);
			jsonExtractFloat(obj, "refHum", refH);

			if (newId[0] != '\0') safeCopy(cfg.sensors[10].hwId, newId, sizeof(cfg.sensors[10].hwId));
			if (newName[0] != '\0') safeCopy(cfg.sensors[10].friendlyName, newName, sizeof(cfg.sensors[10].friendlyName));

			float curT = NAN, curH = NAN, offT = 0, offH = 0;
			const auto& runtime = _sensorRef->getRuntimeSensors( );
			for (const auto& s : runtime) {
				if (s.type == TYPE_DHT22) {
					curT = s.avgValue[0]; curH = s.avgValue[1];
					offT = s.calibrationOffset[0]; offH = s.calibrationOffset[1];
					break;
				}
			}
			float newOffT = offT, newOffH = offH;
			if (!isnan(refT) && !isnan(curT)) newOffT = offT + (refT - curT);
			if (!isnan(refH) && !isnan(curH)) newOffH = offH + (refH - curH);

			char idBase[16];
			safeCopy(idBase, cfg.sensors[10].hwId, sizeof(idBase));
			char prefT[18]; snprintf(prefT, sizeof(prefT), "t%s", idBase);
			char prefU[18]; snprintf(prefU, sizeof(prefU), "u%s", idBase);

			char nameSan[32]; safeCopy(nameSan, cfg.sensors[10].friendlyName, sizeof(nameSan)); sanitizeName(nameSan);
			for (int k = 0; k < 2 && nChanges < MAX_CHANGES; k++) {
				safeCopy(changes[nChanges].key, picoUID.c_str( ), sizeof(changes[0].key));
				safeCopy(changes[nChanges].id, k == 0 ? prefT : prefU, sizeof(changes[0].id));
				changes[nChanges].offset = (k == 0) ? newOffT : newOffH;
				safeCopy(changes[nChanges].name, nameSan, sizeof(changes[0].name));
				changes[nChanges].written = false;
				nChanges++;
			}
		}
	}

	/* === SENSORS DS18B20 === */
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
				float gpioF = -1.0f; jsonExtractFloat(obj, "gpio", gpioF);
				int gpio = (int)gpioF;
				objStart = objEnd + 1;
				if (gpio < 0 || gpio >= MAX_SENSORS) continue;
				if (!cfg.sensors[gpio].active) continue;
				if (isAllZero(cfg.sensors[gpio].rom)) continue;

				char newId[16] = {0}, newName[32] = {0};
				float refT = NAN;
				jsonExtractCStr(obj, "hwId", newId, sizeof(newId));
				jsonExtractCStr(obj, "name", newName, sizeof(newName));
				jsonExtractFloat(obj, "refTemp", refT);

				if (newId[0] != '\0') safeCopy(cfg.sensors[gpio].hwId, newId, sizeof(cfg.sensors[gpio].hwId));
				if (newName[0] != '\0') safeCopy(cfg.sensors[gpio].friendlyName, newName, sizeof(cfg.sensors[gpio].friendlyName));

				float curT = NAN, off = 0;
				const auto& runtime = _sensorRef->getRuntimeSensors( );
				for (const auto& s : runtime) {
					if (s.config.pins[0] == gpio) { curT = s.avgValue[0]; off = s.calibrationOffset[0]; break; }
				}
				float newOff = off;
				if (!isnan(refT) && !isnan(curT)) newOff = off + (refT - curT);

				if (nChanges < MAX_CHANGES) {
					char romHex[17]; romToHex(cfg.sensors[gpio].rom, romHex);
					safeCopy(changes[nChanges].key, romHex, sizeof(changes[0].key));
					safeCopy(changes[nChanges].id, cfg.sensors[gpio].hwId, sizeof(changes[0].id));
					changes[nChanges].offset = newOff;
					char nameSan[32]; safeCopy(nameSan, cfg.sensors[gpio].friendlyName, sizeof(nameSan));
					sanitizeName(nameSan);
					safeCopy(changes[nChanges].name, nameSan, sizeof(changes[0].name));
					changes[nChanges].written = false;
					nChanges++;
				}
			}
		}
	}

	/* === Streaming 2-pass: read calib.csv, write calib.tmp === */
	uint32_t version = (uint32_t)_netRef->getEpoch( );

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

			int idx;
			if (strcasecmp(keyStr, picoUID.c_str( )) == 0 && (idStr[0] == 't' || idStr[0] == 'u')) {
				idx = findChangeMatch(changes, nChanges, keyStr, idStr[0]);
			} else {
				idx = findChangeMatch(changes, nChanges, keyStr, 0);
			}

			if (idx >= 0) {
				fout.printf("%s,%s,%.2f,%s\n",
				            changes[idx].key, changes[idx].id, changes[idx].offset, changes[idx].name);
				changes[idx].written = true;
			} else {
				*p1 = ',';
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

	if (!_storageRef->processCalibrationUpload( )) {
		_server.send(500, "application/json", "{\"error\":\"calib commit failed\"}");
		return;
	}

	_displayRef->requestQuietMode( );
	_storageRef->saveConfiguration( );
	app.loadAndCalibrateSensors( );
	_displayRef->releaseQuietMode( );

	LOG_CODE(LOG_INFO, "WEB", APP_SENSORS_CALIBRATED, 0, "calib via /api/calib");

	char resp[80];
	snprintf(resp, sizeof(resp), "{\"ok\":true,\"version\":%lu}", (unsigned long)version);
	_server.send(200, "application/json", resp);
}
