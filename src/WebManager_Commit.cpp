/**
 * @file WebManager_Commit.cpp
 * @brief Commit-all handler: batched config save + reboot, theme switch, touch cal reset.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "ParseFloat.h"
#include "WebJsonSlice.h"
#include "WebCommitSections.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "Themes.h"
#include "TouchPriority.h"
#include <LittleFS.h>
#include <time.h>
#include <hardware/watchdog.h>

/* Extracts a JSON string value for "key":"..." with correct escape handling.
 *
 * Previously (bug): indexOf('"', vStart) returned the FIRST quote after the
 * opening, but JSON.stringify from the frontend emits \" for internal quotes
 * in payload templates. Result: the extracted value was truncated — templates
 * saved as 2 chars of garbage.
 *
 * Fix: walks char by char from the opening "; consumes \" \\ \/ \n \t \r \b \f
 * correctly; stops at the FIRST unescaped quote. Unknown escapes (\x) are
 * preserved leniently. */
static String jsonExtractStringValue(const String& src, const char* key) {
	String pat = String("\"") + key + "\":";
	int p = src.indexOf(pat);
	if (p < 0) return String( );
	int i = p + pat.length( );
	const int n = src.length( );
	/* Whitespace between the colon and the opening quote is legal JSON the
	 * page never emits. With the quote baked into the needle, a spaced
	 * payload missed the match and the caller applied the empty result —
	 * on the bench, {"sys":{"t_srv": "192.168.3.31"}} ERASED the telemetry
	 * server. getNum below carries the numeric twin of this story; the
	 * string form is worse because absent and empty apply alike. */
	while (i < n && (src.charAt(i) == ' ' || src.charAt(i) == '\t' ||
	                 src.charAt(i) == '\r' || src.charAt(i) == '\n')) i++;
	if (i >= n || src.charAt(i) != '"') return String( );
	i++;
	String out;
	while (i < n) {
		char c = src.charAt(i);
		if (c == '\\' && i + 1 < n) {
			char e = src.charAt(i + 1);
			switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'n': out += '\n'; break;
				case 't': out += '\t'; break;
				case 'r': out += '\r'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				default: out += c; out += e; break; /* lenient */
			}
			i += 2;
			continue;
		}
		if (c == '"') return out;
		out += c;
		i++;
	}
	return String( ); /* unterminated — silently drop */
}



using ReadGuard = StorageManager::ReadGuard;


/*
 * handleSaveSystem — minimal version.
 * Used for dashboard theme switching (immediate application, no reboot).
 */
/* "<key>":[lo,hi] — the shape channel limits arrive in, under "lim" for the
 * sensors section and inline for the alarms one. Either element may be empty,
 * which leaves that bound alone. File scope because both sections parse it;
 * as a lambda it lived in the sensors block and was invisible to the other. */
static bool getPair(const String& o, const char* key, float& lo, float& hi) {
	String needle = String("\"") + key + "\":";
	int p = o.indexOf(needle);
	if (p < 0) return false;
	int s = o.indexOf('[', p);
	int e = (s >= 0) ? o.indexOf(']', s) : -1;
	if (s < 0 || e <= s) return false;
	int comma = o.indexOf(',', s + 1);
	if (comma < 0 || comma > e) return false;
	String a = o.substring(s + 1, comma); a.trim( );
	String b = o.substring(comma + 1, e); b.trim( );
	if (a.length( )) lo = parseFloat(a.c_str( ));
	if (b.length( )) hi = parseFloat(b.c_str( ));
	return true;
}

/* "key": true|false — the boolean cousin of the extractFloat/
 * jsonExtractStringValue scars above, found the same way: a spaced payload
 * (json.dumps' default) read every true as false, and on the bench that
 * silently disarmed a slot's alarms. It grew a second blind spot the same
 * way, on the other side: it knew only the literals, so the `1`/`0` the rest
 * of this very payload is written in fell through to `fallback` — no change,
 * no complaint, 200 OK. jsonFlag knows both spellings; `fallback` is still
 * the stored value at every call site, so absent still means keep. */
static bool jsonBoolValue(const String& src, const char* key, bool fallback) {
	const int b = jsonFlag(src, key);
	return (b < 0) ? fallback : (b == 1);
}

void WebManager::handleSaveSystem( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_SYS_CONFIG)) { _server->send(403, "text/plain", "Forbidden"); return; }
	SystemConfig& cfg = _storageRef->getConfig( );
	if (_server->hasArg("theme")) {
		int t = _server->arg("theme").toInt( );
		/* A theme index the build does not carry used to answer ok and change
		 * nothing, so the page had no way to tell "applied" from "ignored" —
		 * and neither did anyone driving this by hand. Nothing else happens in
		 * this handler, so refusing here is atomic by construction. */
		if (t < 0 || t >= getThemeCount( )) {
			_server->send(400, "application/json",
			             "{\"status\":\"error\",\"error\":\"Theme index out of range\"}");
			return;
		}
		if (cfg.themeIndex != t) {
			cfg.themeIndex = t;
			loadTheme(t);
			_storageRef->saveConfiguration( );
			if (_displayRef) _displayRef->refreshTheme( );
		}
	}
	_server->send(200, "application/json", "{\"status\":\"ok\"}");
}


/* Sets a random one-time temporary password on a user slot and records it in
 * outCreds for the commit response. Replaces the *PENDING* / Nome@DDMMYYYY
 * scheme (getDynamicExpectedHash), whose "temporary password" was
 * sha256(Capitalized@DDMMYYYY) — derivable by anyone who knew the username and
 * the date, and /api/users lists the usernames. docs/..._vibecoding.md §1/§5.
 *
 * This is the CLI's admin-reset (AppManager_CmdHandlers.cpp) applied to the web
 * path: 8 chars from a no-ambiguous-glyph alphabet (~40 bits), stored as a
 * normal V1 hash with a random salt, mustChangePassword forcing a change on
 * first login. The plaintext exists only long enough to reach the admin who
 * created the account — over the network here rather than on the serial line,
 * so it rides back in the commit response and nowhere else. */
void WebManager::assignTempPassword(int slot, String& outCreds) {
	SystemConfig& cfg = _storageRef->getConfig( );

	char temp[9];
	_storageRef->generateInitialAdminPassword(temp, sizeof(temp));   /* 8 chars, [A-HJ-NP-Z2-9] */
	_storageRef->generateSalt(cfg.users[slot].salt);
	/* Store the hash of sha256(temp): the browser sends sha256(typed) as the
	 * password field, so the stored value must be hashPasswordV1 over that, the
	 * same shape verifyPasswordFor recomputes on the ordinary V1 path. No
	 * special login branch is needed — the temp logs in like any password and
	 * mustChangePassword sends it straight to /force_chpass. */
	String preHash = _storageRef->sha256Hex(String(temp));
	String hashed = _storageRef->hashPasswordV1(String(cfg.users[slot].username),
	                                            preHash, cfg.users[slot].salt);
	safeCopy(cfg.users[slot].password, hashed.c_str( ), sizeof(cfg.users[slot].password));
	cfg.users[slot].hashVersion = 1;
	cfg.users[slot].mustChangePassword = true;

	if (outCreds.length( )) outCreds += ",";
	outCreds += "{\"u\":\"";
	outCreds += jsonEscape(cfg.users[slot].username);
	outCreds += "\",\"p\":\"";
	outCreds += temp;   /* alphabet has no JSON-hostile byte */
	outCreds += "\"}";

	/* Zero the stack copy. The String in outCreds still holds it until the
	 * response is sent and destroyed — that copy is the delivery, unavoidable;
	 * this just keeps the plaintext from lingering on the stack afterward. */
	volatile char* v = temp;
	for (size_t i = 0; i < sizeof(temp); i++) v[i] = 0;
}

/* Answers the client for the two refusal paths of commitScanSections and
 * returns false; on true, outStart[] is the parsers' map of the payload.
 * The decision itself is pure and lives in WebCommitSections.h, where
 * `pio test -e native` can reach it without a WebServer. */
bool WebManager::authorizeCommitSections(const String& body, uint16_t perms,
                                         int* outStart) {
	const int verdict = commitScanSections(body, perms, outStart);
	if (verdict == COMMIT_AUTH_OK) return true;

	if (verdict == COMMIT_AUTH_EMPTY) {
		_server->send(400, "application/json", "{\"error\":\"No section\"}");
		return false;
	}

	const char* section = kCommitSectionRules[verdict].name;
	LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId,
	         String("commit_all section refused: ") + section);
	char buf[72];
	snprintf(buf, sizeof(buf), "{\"error\":\"Forbidden\",\"section\":\"%s\"}", section);
	_server->send(403, "application/json", buf);
	return false;
}

/*
 * Commit-all + reboot (save-and-restart pattern).
 *
 * Receives POST with _payload = JSON containing `sys` and/or `alarms` sections.
 * Applies all changes in memory, does a single saveConfiguration( ),
 * responds to the client, and schedules a reboot. The burst of concurrent
 * saves that used to trigger multicore lockout deadlocks no longer exists —
 * writing happens once and the system restarts clean.
 *
 * Client sends _payload urlencoded. Example:
 * _payload={"sys":{"name":"SIMUT","tz":"-3","log":"1",...}}
 */
void WebManager::handleApiCommitAll( ) {
	uint16_t perms = getAuthPerms( );
	/* Entry only proves there is a session holding at least one of the bits
	 * this route can act on. WHAT the payload may change is decided per
	 * section in authorizeCommitSections, below — checking PERM_SYS_CONFIG
	 * here and nothing after it is the bug this gate exists to close.
	 *
	 * It also widens the door on purpose: an account with PERM_USER_MGR and
	 * nothing else can open /users and stage an account, but its commit was
	 * refused here, so the role could not actually manage users. */
	if (!(perms & commitEntryPerms( ))) {
		_server->send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}
	if (isPasswordChangeRequired( )) return;
	if (rejectIfTouchPriority( )) return;

	if (!_server->hasArg("_payload")) {
		_server->send(400, "application/json", "{\"error\":\"Missing _payload\"}");
		return;
	}

	String body = _server->arg("_payload");
	if (body.length( ) == 0 || body.length( ) > 6144) {
		_server->send(400, "application/json", "{\"error\":\"Bad payload\"}");
		return;
	}

	int secStart[SEC_COUNT];
	if (!authorizeCommitSections(body, perms, secStart)) return;

	SystemConfig& cfg = _storageRef->getConfig( );
	bool themeChanged = false;

	/* Fields the sys section DISCARDS — out-of-range or unparsable values
	 * keep the stored setting, which is the right conservatism, but doing
	 * it silently under a 200 cost a bench session per occurrence ("my
	 * edit was ignored and nothing said so"). The names are echoed back in
	 * the response as "rejected":[...] so the page can say which fields
	 * did not take. */
	char rejectedList[128];
	rejectedList[0] = '\0';
	auto rejectField = [&](const char* k) {
		/* Idempotent and all-or-nothing. The users section below can hand the
		 * same reason up to eight times, and a token that only half fits would
		 * leave the array unterminated — a 200 carrying unparseable JSON, the
		 * same shape as the sec_status overflow. Both are cheaper to prevent
		 * here than to diagnose in the page. */
		char needle[40];
		snprintf(needle, sizeof(needle), "\"%s\"", k);
		if (strstr(rejectedList, needle)) return;
		size_t l = strlen(rejectedList);
		size_t need = strlen(needle) + (l ? 1 : 0);
		if (l + need + 1 > sizeof(rejectedList)) return;
		snprintf(rejectedList + l, sizeof(rejectedList) - l, "%s%s", l ? "," : "", needle);
	};

	/* ── alarms limits pre-validation ───────────────────────────────────────
	 * The alarms apply-pass below takes values raw (deliberate legacy) — which
	 * let hmax=300 %RH through as an alarm that can never trip, under 200 OK.
	 * Out-of-plausible-range is an error, not a preference: checked HERE,
	 * before any section mutates cfg, so the 400 stays atomic for the whole
	 * commit. Values are judged against the channel's [saneMin, saneMax]
	 * regardless of slot state — a bad number is bad even on a slot this same
	 * payload is about to activate. */
	{
		int vAlm = secStart[SEC_ALARMS];
		int vSens = (vAlm >= 0) ? body.indexOf("\"sensors\"", vAlm) : -1;
		int vArr = (vSens >= 0) ? body.indexOf('[', vSens) : -1;
		int vArrEnd = -1;
		if (vArr >= 0) {
			int depth = 0;
			for (int k = vArr; k < (int)body.length( ); k++) {
				char c = body.charAt(k);
				if (c == '[') depth++;
				else if (c == ']') { if (--depth == 0) { vArrEnd = k; break; } }
			}
		}
		if (vArr >= 0 && vArrEnd > vArr) {
			String arrStr = body.substring(vArr, vArrEnd + 1);
			int objStart = 0, safety = 0;
			while ((objStart = arrStr.indexOf('{', objStart)) >= 0) {
				if (++safety > MAX_SENSORS + 4) break;
				int objEnd = arrStr.indexOf('}', objStart);
				if (objEnd < 0) break;
				String obj = arrStr.substring(objStart, objEnd + 1);
				auto limOk = [&](uint8_t ch, float v) -> bool {
					if (isnan(v)) return true; /* absent keeps the stored bound */
					const ChannelInfo& ci = channelInfo(ch);
					return v >= ci.saneMin && v <= ci.saneMax;
				};
				/* Same reader shape as the apply-pass, so both passes see the
				 * same value (whitespace after the colon included). */
				auto exf = [&](const char* key) -> float {
					int kp = obj.indexOf(key);
					if (kp < 0) return NAN;
					int cp = obj.indexOf(':', kp + strlen(key));
					if (cp < 0) return NAN;
					int vs = cp + 1;
					while (vs < (int)obj.length( ) && (obj[vs] == ' ' || obj[vs] == '\t' ||
					       obj[vs] == '\r' || obj[vs] == '\n')) vs++;
					return parseFloat(obj.substring(vs).c_str( ));
				};
				bool bad = !limOk(CH_TEMP, exf("\"tmin\"")) || !limOk(CH_TEMP, exf("\"tmax\"")) ||
				           !limOk(CH_HUM, exf("\"hmin\"")) || !limOk(CH_HUM, exf("\"hmax\""));
				for (uint8_t c = 0; !bad && c < MAX_SENSOR_CHANNELS; c++) {
					if (!channelValid(c)) continue;
					float lo = NAN, hi = NAN;
					if (!getPair(obj, channelInfo(c).key, lo, hi)) continue;
					if (!limOk(c, lo) || !limOk(c, hi)) bad = true;
				}
				if (bad) {
					_server->send(400, "application/json",
					             "{\"error\":\"Alarm limit outside channel range\"}");
					return;
				}
				objStart = objEnd + 1;
			}
		}
	}

	/* ── slots section: sensor provisioning ─────────────────────────────────
	 * Format: "slots":{"s":[{"i":0,"a":true,"t":2,"p":[2,255,255,255],
	 *                        "hwId":"DHT0","name":"Sala",
	 *                        "tmin":-10,"tmax":50,"hmin":0,"hmax":100,"al":true}]}
	 * Only edited slots are sent — the payload cap is 6144 B and a full
	 * 16-slot dump plus the sys section would crowd it.
	 *
	 * The key is "slots", not "sensors", because "sensors" already appears
	 * nested inside both "alarms" and "calib"; a top-level indexOf("\"sensors\"")
	 * would latch onto whichever came first in the payload.
	 *
	 * This block and the alarm-limits pre-check above are the only gates that
	 * can reject the whole commit. Validation is a separate pass over the same text: on the first
	 * bad field we answer 400 with cfg still untouched, so a rejected commit
	 * cannot leave half-applied sensor state behind and then reboot into it.
	 * There is no shared pin validator in the firmware — the CLI open-codes
	 * the same range and uniqueness rules in AppManager_CmdHandlers.cpp. */
	int slotsStart = secStart[SEC_SLOTS];
	if (slotsStart >= 0) {
		/* Bound the search to this section first. An empty "slots":{} is a
		 * normal payload (the user staged a slot and then discarded it), and an
		 * unbounded indexOf('[') would sail past it and latch onto the array in
		 * a later "alarms" or "calib" section. */
		int secStart = body.indexOf('{', slotsStart);
		int secEnd = -1;
		if (secStart >= 0) {
			int d = 0;
			for (int k = secStart; k < (int)body.length( ); k++) {
				char c = body.charAt(k);
				if (c == '{') d++;
				else if (c == '}') { if (--d == 0) { secEnd = k; break; } }
			}
		}
		int arrStart = (secEnd > secStart) ? body.indexOf('[', secStart) : -1;
		if (arrStart > secEnd) arrStart = -1;
		int arrEnd = -1;
		/* The slot objects carry a nested "p":[...] array, so the end of the
		 * outer array is found by depth, not by the first ']'. */
		if (arrStart >= 0) {
			int depth = 0;
			for (int k = arrStart; k <= secEnd; k++) {
				char c = body.charAt(k);
				if (c == '[') depth++;
				else if (c == ']') { if (--depth == 0) { arrEnd = k; break; } }
			}
		}
		if (arrStart >= 0 && arrEnd > arrStart) {
			String arr = body.substring(arrStart, arrEnd + 1);

			/* Field readers. Each works on one slot object at a time.
			 *
			 * valuePos skips the whitespace JSON allows after the colon. The
			 * browser's JSON.stringify never emits it, but a hand-written
			 * payload legally can, and without this getBool silently answered
			 * "absent" for `"a": true` — which made the caller fall back to the
			 * stored active flag and skip the GPIO conflict check entirely.
			 * A validation pass that can be turned off by adding a space is
			 * not a validation pass. */
			auto valuePos = [](const String& o, const char* key) -> int {
				return jsonValuePos(o, key);
			};
			auto getInt = [&](const String& o, const char* key, long dflt) -> long {
				int v = valuePos(o, key);
				return (v < 0) ? dflt : o.substring(v).toInt( );
			};
			auto getFloat = [&](const String& o, const char* key) -> float {
				int v = valuePos(o, key);
				return (v < 0) ? NAN : parseFloat(o.substring(v).c_str( ));
			};
			(void)getFloat; /* lambda kept for future fields, suppress -Wunused */
			/* Tri-state: 1, 0, or negative for absent/unreadable — the caller
			 * keeps the stored flag on both. It used to answer `startsWith
			 * ("true") ? 1 : 0`, which made every spelling that is not the
			 * literal `true` mean FALSE: `{"a":1}` deactivated the slot it
			 * was asking to enable. */
			auto getBool = [&](const String& o, const char* key) -> int {
				return jsonFlag(o, key);
			};
			/* Whitespace-tolerant string reader. jsonExtractStringValue matches
			 * a literal "key":" and would miss `"name": "x"`. */
			auto getStr = [&](const String& o, const char* key) -> String {
				int v = valuePos(o, key);
				if (v < 0 || v >= (int)o.length( ) || o[v] != '"') return String( );
				String out;
				int i = v + 1;
				while (i < (int)o.length( )) {
					char c = o.charAt(i);
					if (c == '\\' && i + 1 < (int)o.length( )) { out += o.charAt(i + 1); i += 2; continue; }
					if (c == '"') break;
					out += c;
					i++;
				}
				return out;
			};
			/* "p":[a,b,c,d] → out[4], PIN_UNUSED for missing entries. */
			auto getPins = [](const String& o, uint8_t* out) -> bool {
				for (int k = 0; k < MAX_SENSOR_PINS; k++) out[k] = PIN_UNUSED;
				int p = o.indexOf("\"p\":");
				if (p < 0) return false;
				int s = o.indexOf('[', p);
				int e = (s >= 0) ? o.indexOf(']', s) : -1;
				if (s < 0 || e <= s) return false;
				int idx = 0, cur = s + 1;
				while (cur < e && idx < MAX_SENSOR_PINS) {
					int comma = o.indexOf(',', cur);
					if (comma < 0 || comma > e) comma = e;
					String tok = o.substring(cur, comma);
					tok.trim( );
					out[idx++] = (tok.length( ) == 0) ? PIN_UNUSED
					                                 : (uint8_t)tok.toInt( );
					cur = comma + 1;
				}
				return true;
			};

			/* ── pass 1: validate ──────────────────────────────────────────
			 * owner[] starts as the persisted GPIO ownership and is rewritten
			 * with the proposed assignments, so a commit that swaps GP2 and
			 * GP3 between two slots validates instead of colliding with the
			 * pre-edit state. */
			uint8_t owner[MAX_SENSORS];
			for (int g = 0; g < MAX_SENSORS; g++) owner[g] = 0xFF;
			for (int i = 0; i < MAX_SENSORS; i++) {
				if (!cfg.sensors[i].active) continue;
				for (int k = 0; k < MAX_SENSOR_PINS; k++) {
					uint8_t g = cfg.sensors[i].pins[k];
					if (g < MAX_SENSORS) owner[g] = (uint8_t)i;
				}
			}
			/* Then drop the claims of EVERY slot this payload touches.
			 *
			 * Releasing each slot as it came up in the validation loop was
			 * wrong: it only freed slots processed earlier, so the check ran
			 * against a half-old map. Clearing all slots and reassigning their
			 * GPIOs in one commit then failed whenever a new assignment reused
			 * a pin held by a slot appearing LATER in the array — the old owner
			 * was still holding it. That rejected a valid reconfiguration, and
			 * which pin it blamed depended on the order the page happened to
			 * stage them in. The payload is one atomic picture of the final
			 * state, so the whole picture must be cleared before any of it is
			 * checked. */
			{
				int p = 0, guard = 0;
				while ((p = arr.indexOf('{', p)) >= 0) {
					if (++guard > MAX_SENSORS + 4) break;
					int e = jsonMatchEnd(arr, p);
					if (e < 0) break;
					long sl = getInt(arr.substring(p, e + 1), "i", -1);
					p = e + 1;
					if (sl < 0 || sl >= MAX_SENSORS) continue;
					for (int g = 0; g < MAX_SENSORS; g++) if (owner[g] == sl) owner[g] = 0xFF;
				}
			}

			char err[96];
			err[0] = '\0';
			/* jsonMatchEnd, not indexOf('}'): the slot object carries a nested
			 * "lim":{...}, and the first closing brace is lim's — every key
			 * staged after it ("al") fell off the slice and was silently kept
			 * at its stored value, commit after commit. */
			int objStart = 0, safety = 0;
			while ((objStart = arr.indexOf('{', objStart)) >= 0) {
				if (++safety > MAX_SENSORS + 4) break;
				int objEnd = jsonMatchEnd(arr, objStart);
				if (objEnd < 0) break;
				String obj = arr.substring(objStart, objEnd + 1);
				objStart = objEnd + 1;

				long slot = getInt(obj, "i", -1);
				if (slot < 0 || slot >= MAX_SENSORS) {
					snprintf(err, sizeof(err), "slot %ld out of range", slot);
					break;
				}
				long type = getInt(obj, "t", cfg.sensors[slot].sensorType);
				int wantActive = getBool(obj, "a");
				if (wantActive < 0) wantActive = cfg.sensors[slot].active ? 1 : 0;

				/* SENSOR_TYPE_MAX, not the last name anyone happened to add: this
				 * said `> TYPE_BME280` and would have rejected TYPE_BMP280. */
				if (type < TYPE_NONE || type > SENSOR_TYPE_MAX) {
					snprintf(err, sizeof(err), "slot %ld: bad type %ld", slot, type);
					break;
				}
				if (type != TYPE_NONE && !sensorTypeEnabled((SensorType)type)) {
					snprintf(err, sizeof(err), "slot %ld: driver not in firmware", slot);
					break;
				}

				uint8_t pins[MAX_SENSOR_PINS];
				if (!getPins(obj, pins)) {
					for (int k = 0; k < MAX_SENSOR_PINS; k++) pins[k] = cfg.sensors[slot].pins[k];
				}

				/* This slot's old claims were dropped above, along with every
				 * other slot in this payload. */

				uint8_t need = 0;
				if (type != TYPE_NONE) need = SensorFormat::forType((SensorType)type).pinCount;

				for (int k = 0; k < MAX_SENSOR_PINS; k++) {
					if (pins[k] == PIN_UNUSED) continue;
					/* GP16+ belong to the TFT, touch and buzzer — see simut_config.h. */
					if (pins[k] >= MAX_SENSORS) {
						snprintf(err, sizeof(err), "slot %ld: GP%u not a sensor pin (0-%d)",
						         slot, pins[k], MAX_SENSORS - 1);
						break;
					}
					if (wantActive && owner[pins[k]] != 0xFF) {
						snprintf(err, sizeof(err), "slot %ld: GP%u already used by slot %u",
						         slot, pins[k], owner[pins[k]]);
						break;
					}
					if (wantActive) owner[pins[k]] = (uint8_t)slot;
				}
				if (err[0]) break;

				if (wantActive) {
					if (type == TYPE_NONE) {
						snprintf(err, sizeof(err), "slot %ld: set a type before enabling", slot);
						break;
					}
					for (uint8_t k = 0; k < need; k++) {
						if (pins[k] == PIN_UNUSED) {
							SensorFormat f = SensorFormat::forType((SensorType)type);
							snprintf(err, sizeof(err), "slot %ld: pin %u (%s) not assigned",
							         slot, k, f.pins[k].label);
							break;
						}
					}
					if (err[0]) break;
				}

				if (valuePos(obj, "name") >= 0) {
					String nm = getStr(obj, "name");
					if (nm.length( ) > 0 &&
					    !isValidName(nm.c_str( ), sizeof(cfg.sensors[slot].friendlyName) - 1)) {
						snprintf(err, sizeof(err), "slot %ld: invalid name", slot);
						break;
					}
				}
				if (valuePos(obj, "hwId") >= 0) {
					String hw = getStr(obj, "hwId");
					if (!isValidCfgString(hw.c_str( ), sizeof(cfg.sensors[slot].hwId) - 1)) {
						snprintf(err, sizeof(err), "slot %ld: invalid hwId", slot);
						break;
					}
					/* An active slot must carry an ID, and the commit is where to
					 * say so. Blank was accepted here and then quietly refilled on
					 * the next boot by the auto-ID in loadAndCalibrateSensors —
					 * <TYPE><2-digit slot>, e.g. DHT2202 for a DHT22 in slot 2. To
					 * the user that reads as "my edit was ignored and something
					 * put its own name back", with nothing in between to explain
					 * it. A freed slot (a:false) still clears to blank. */
					if (wantActive && hw.length( ) == 0) {
						snprintf(err, sizeof(err),
						         "slot %ld: hardware ID cannot be empty on an active slot", slot);
						break;
					}
				}
			}

			if (err[0]) {
				char resp[160];
				snprintf(resp, sizeof(resp), "{\"error\":\"%s\"}", err);
				_server->send(400, "application/json", resp);
				return; /* cfg untouched — nothing to roll back */
			}

			/* ── pass 2: apply ───────────────────────────────────────────── */
			objStart = 0; safety = 0;
			while ((objStart = arr.indexOf('{', objStart)) >= 0) {
				if (++safety > MAX_SENSORS + 4) break;
				int objEnd = jsonMatchEnd(arr, objStart);
				if (objEnd < 0) break;
				String obj = arr.substring(objStart, objEnd + 1);
				objStart = objEnd + 1;

				long slot = getInt(obj, "i", -1);
				if (slot < 0 || slot >= MAX_SENSORS) continue;
				SensorRecord& r = cfg.sensors[slot];

				long type = getInt(obj, "t", r.sensorType);
				bool typeChanged = ((uint8_t)type != r.sensorType);
				r.sensorType = (uint8_t)type;

				uint8_t pins[MAX_SENSOR_PINS];
				if (getPins(obj, pins)) {
					for (int k = 0; k < MAX_SENSOR_PINS; k++) r.pins[k] = pins[k];
				}

				if (valuePos(obj, "name") >= 0) safeCopy(r.friendlyName, getStr(obj, "name").c_str( ), sizeof(r.friendlyName));
				if (valuePos(obj, "hwId") >= 0) safeCopy(r.hwId, getStr(obj, "hwId").c_str( ), sizeof(r.hwId));

				/* Limits arrive under "lim", keyed by channel: {"temp":[0,40]}.
				 * Clamped like the CLI does (AppManager_CmdHandlers.cpp), which the
				 * older "alarms" section below deliberately does not do — the bound
				 * is now the channel's plausible range instead of a per-quantity
				 * literal, so a new measurement axis is clamped correctly the day
				 * its row lands in the table. */
				int limPos = valuePos(obj, "lim");
				if (limPos >= 0) {
					/* valuePos lands on lim's '{'; match it by depth so a future
					 * nested value inside lim cannot shear the slice. */
					int limEnd = jsonMatchEnd(obj, limPos);
					if (limEnd > limPos) {
						String lim = obj.substring(limPos, limEnd + 1);
						for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
							if (!channelValid(c)) continue;
							const ChannelInfo& ci = channelInfo(c);
							float lo = NAN, hi = NAN;
							if (!getPair(lim, ci.key, lo, hi)) continue;
							if (!isnan(lo)) r.chMin[c] = constrain(lo, ci.saneMin, ci.saneMax);
							if (!isnan(hi)) r.chMax[c] = constrain(hi, ci.saneMin, ci.saneMax);
						}
					}
				}
				int b;
				b = getBool(obj, "al"); if (b >= 0) r.alarmsActive = (b == 1);
				b = getBool(obj, "a");  if (b >= 0) r.active = (b == 1);

				/* A slot that changed chip is a different sensor: restamp the
				 * provisioning epoch so history graphs do not splice the old
				 * device's readings onto the new one. Same reason `sensor wipe`
				 * exists in the CLI. */
				if (typeChanged) {
					time_t now = time(nullptr);
					r.provisionEpoch = (now > 1600000000L) ? (uint32_t)now : 0;
					memset(r.rom, 0, sizeof(r.rom)); /* ROM belongs to the old chip */
				}
			}
		}
	}

	/* ── sys section: extracts sub-object and applies each field ────────────
	 * Manual parser for simplicity. Expected format:
	 * "sys":{"name":"...","tz":"-3","log":"1",...}
	 * Each field may come as a quoted string or bare number. */
	int sysStart = secStart[SEC_SYS];
	if (sysStart >= 0) {
		int objStart = body.indexOf('{', sysStart);
		int objEnd = -1;
		if (objStart >= 0) {
			/* Finds the matching '}' taking nesting into account. */
			int depth = 0;
			for (int i = objStart; i < (int)body.length( ); i++) {
				char c = body.charAt(i);
				if (c == '{') depth++;
				else if (c == '}') { depth--; if (depth == 0) { objEnd = i; break; } }
			}
		}
		if (objStart >= 0 && objEnd > objStart) {
			String sys = body.substring(objStart, objEnd + 1);

			/* Helper: extracts string value "key":"..." with correct unescaping
			 * of \" \\ \/ \n \t \r \b \f (via jsonExtractStringValue). */
			auto getStr = [&](const char* key) -> String {
				return jsonExtractStringValue(sys, key);
			};
			/* Helper: bare or quoted scalar from "key":NNN.
			 *
			 * The whitespace skip inside jsonRawToken is this section's scar:
			 * JSON.stringify writes {"t_int":0} and a pretty-printing client
			 * writes {"t_int": 0}, which used to hand parseIntStrict the
			 * string " 0" — rejected, field skipped, still a 200. Cost an
			 * hour to find with `tel_reset` running, and cost it again in the
			 * `net` section below, which had its own copy without the fix. */
			auto getNum = [&](const char* key) -> String {
				return jsonRawToken(sys, key);
			};
			auto has = [&](const char* key) -> bool {
				String pat = String("\"") + key + "\":";
				return sys.indexOf(pat) >= 0;
			};

			/* String fields used to go straight into safeCopy, which truncates
			 * to fit. A hostname or a template cut in half is not a partial
			 * preference — it is a wrong value stored under a 200, and the
			 * only field with a length the page enforces is the one the page
			 * happens to enforce. isValidCfgString is the same gate the CLI
			 * has always applied: no control bytes, must fit whole. Empty
			 * stays legal, because clearing a template or a server is a real
			 * edit; the fields where empty means "keep" say so themselves. */
			auto setStr = [&](const char* k, char* dst, size_t dstSize) {
				String v = getStr(k);
				if (isValidCfgString(v.c_str( ), dstSize - 1)) safeCopy(dst, v.c_str( ), dstSize);
				else rejectField(k);
			};

			/* Applies each field. */
			if (has("name")) {
				String n = getStr("name"); n.trim( );
				if (n.length( ) > 0 && isValidName(n.c_str( ))) {
					safeCopy(cfg.deviceName, n.c_str( ), sizeof(cfg.deviceName));
				} else rejectField("name");
			}
			/* Strict parsing of numeric fields.
			 * String::toInt( ) would silently return 0 on invalid input
			 * ("abc" → 0), potentially zeroing legitimate values. parseIntStrict
			 * rejects non-numeric; on invalid, preserves current value. */
			auto getInt = [&](const char* k, int& dst, int minV, int maxV) {
				int v;
				if (parseIntStrict(getNum(k), v) && v >= minV && v <= maxV) dst = v;
			};
			if (has("tz")) {
				int v;
				if (parseIntStrict(getNum("tz"), v) && v >= -12 && v <= 14) {
					cfg.timezoneOffset = (int8_t)v;
					NetworkManager::applyTimezone(cfg.timezoneOffset);
				} else rejectField("tz");
			}
			/* Boolean fields. These read `getNum(k) != "0"`, which answered
			 * TRUE for every spelling that is not the literal `0` — including
			 * the `false` this device's own /api/config emits. GET the config,
			 * flip one flag, POST it back, and all four booleans came back ON:
			 * `t_sec:false` armed telemetry encryption and `log:false` had no
			 * way at all to turn logging off. The page escaped because its
			 * forms emit 1/0. jsonFlag reads both spellings and reports the
			 * value it cannot read instead of guessing at it. */
			auto readFlag = [&](const char* k) -> int {
				const int b = jsonFlag(sys, k);
				if (b == JSON_FLAG_BAD) rejectField(k);
				return b;
			};
			int fl;
			fl = readFlag("log");   if (fl >= 0) cfg.loggingEnabled = (fl == 1);
			fl = readFlag("t_sec"); if (fl >= 0) cfg.telEncryption = (fl == 1);
			if (has("t_key")) {
				/* If value contains "***", it came from the masked GET
				 * and the user did not edit — keep current cfg.telApiKey. Otherwise overwrite. */
				String tk = getStr("t_key");
				if (tk.indexOf("***") < 0) setStr("t_key", cfg.telApiKey, sizeof(cfg.telApiKey));
			}
			#if SIMUT_SENSOR_DS18B20
			if (has("res")) { int v; if (parseIntStrict(getNum("res"), v) && v >= 9 && v <= 12) cfg.ds18Resolution = (uint8_t)v; else rejectField("res"); }
#endif
			/* 1000-60000 and 10-300: the page's ranges are now the contract
			 * (same unification as t_bat). The backend used to accept 100 ms
			 * sampling and 600 s keep-alives the page never offered — and a
			 * value like that, stored via API, jams the page's own form: the
			 * HTML input marks it invalid and refuses to resubmit. */
			if (has("s_int")) { int v; if (parseIntStrict(getNum("s_int"), v) && v >= 1000 && v <= 60000) cfg.sampleIntervalMs = (uint32_t)v; else rejectField("s_int"); }
			if (has("t_srv")) setStr("t_srv", cfg.telServer, sizeof(cfg.telServer));
			if (has("t_port")) { int v; if (parseIntStrict(getNum("t_port"), v) && isInRange(v, 1, 65535)) cfg.telPort = (uint16_t)v; else rejectField("t_port"); }
			if (has("t_path")) setStr("t_path", cfg.telPath, sizeof(cfg.telPath));
			if (has("t_int")) { int v; if (parseIntStrict(getNum("t_int"), v) && v >= 0 && v <= 86400000) cfg.telInterval = (uint32_t)v; else rejectField("t_int"); }
			/* 1..50 everywhere now: the CLI always said 1-50, the runtime clamps at
			 * HARD_CAP=50 (TelemetryManager), and the page's input agrees since the
			 * same commit — this was the field with four different ceilings, where
			 * a user typing 100 silently got 50 with no one saying so. */
			if (has("t_bat")) { int v; if (parseIntStrict(getNum("t_bat"), v) && isInRange(v, 1, 250)) cfg.telBatchSize = (uint8_t)v; else rejectField("t_bat"); }
			if (has("t_mode")) { int v; if (parseIntStrict(getNum("t_mode"), v) && isInRange(v, 0, 2)) cfg.telMode = (uint8_t)v; else rejectField("t_mode"); }
			if (has("t_transport")) { int v; if (parseIntStrict(getNum("t_transport"), v) && isInRange(v, 0, 1)) cfg.telTransport = (uint8_t)v; else rejectField("t_transport"); }
			if (has("m_topic")) setStr("m_topic", cfg.mqttTopic, sizeof(cfg.mqttTopic));
			if (has("m_cid")) setStr("m_cid", cfg.mqttClientId, sizeof(cfg.mqttClientId));
			if (has("m_user")) setStr("m_user", cfg.mqttUser, sizeof(cfg.mqttUser));
			/* The page has always had this field and the browser has always sent
			 * it (every input with an id inside #sysForm stages into Pending.sys)
			 * — nothing here read it, so the MQTT password went in the bin and
			 * a broker that wants credentials could never be reached. Empty
			 * means keep, which is what the field's placeholder promises and
			 * what t_key already does with its "***" mask. */
			if (has("m_pass")) {
				String mp = getStr("m_pass");
				if (mp.length( ) > 0) setStr("m_pass", cfg.mqttPass, sizeof(cfg.mqttPass));
			}
			/* QoS 1/2 não é entregável: PubSubClient 2.8 publica SÓ QoS 0
			 * (publish( ) sem argumento de QoS, sem PUBACK). Aceitar 1/2 aqui
			 * gravava um valor que nada lia — o knob prometia uma garantia de
			 * entrega que o transporte não tem (D-232-QOS). Rejeitar em vez de
			 * publicar silenciosamente em QoS 0. */
			if (has("m_qos")) { int v; if (parseIntStrict(getNum("m_qos"), v) && v == 0) cfg.mqttQos = 0; else rejectField("m_qos"); }
			fl = readFlag("m_retain"); if (fl >= 0) cfg.mqttRetain = (fl == 1);
			if (has("m_ka")) { int v; if (parseIntStrict(getNum("m_ka"), v) && isInRange(v, 10, 300)) cfg.mqttKeepAlive = (uint16_t)v; else rejectField("m_ka"); }
			/* HA Discovery toggle (overlay HaDiscoveryData). No refresh hook:
			 * commit_all reboots, and the MQTT connect after the reboot
			 * reconciles — publish when freshly on, clear when freshly off
			 * (FLAG_HA_PUBLISHED remembers there is something to clear). */
			fl = readFlag("m_had"); if (fl >= 0) _storageRef->setHaDiscoveryEnabled(fl == 1);
			if (has("t_glob")) setStr("t_glob", cfg.telGlobalTemplate, sizeof(cfg.telGlobalTemplate));
			if (has("t_line")) setStr("t_line", cfg.telLineTemplate, sizeof(cfg.telLineTemplate));
			if (has("t_sep")) setStr("t_sep", cfg.telLineSeparator, sizeof(cfg.telLineSeparator));
			/* 2ª linha (alarmes, v21): mesmos validadores da linha convencional.
			 * a_qmax usa as constantes do AlarmTelConfig (1..64). */
			fl = readFlag("a_en"); if (fl >= 0) cfg.alarmTel.enabled = (fl == 1);
			if (has("a_mode")) { int v; if (parseIntStrict(getNum("a_mode"), v) && isInRange(v, 0, 2)) cfg.alarmTel.mode = (uint8_t)v; else rejectField("a_mode"); }
			if (has("a_qmax")) { int v; if (parseIntStrict(getNum("a_qmax"), v) && isInRange(v, ALARM_QUEUE_MIN, ALARM_QUEUE_MAX)) cfg.alarmTel.queueMax = (uint8_t)v; else rejectField("a_qmax"); }
			if (has("a_path")) setStr("a_path", cfg.alarmTel.path, sizeof(cfg.alarmTel.path));
			if (has("a_glob")) setStr("a_glob", cfg.alarmTel.globalTemplate, sizeof(cfg.alarmTel.globalTemplate));
			if (has("a_line")) setStr("a_line", cfg.alarmTel.lineTemplate, sizeof(cfg.alarmTel.lineTemplate));
			if (has("a_sep")) setStr("a_sep", cfg.alarmTel.lineSeparator, sizeof(cfg.alarmTel.lineSeparator));
			/* NTP enable/disable flag (overlay NetworkTimeData). */
			fl = readFlag("ntp_enabled"); if (fl >= 0) _storageRef->setNtpEnabled(fl == 1);
			if (has("h_int")) { int v; if (parseIntStrict(getNum("h_int"), v) && isInRange(v, 1, 1440)) _storageRef->setHistoryIntervalMin((uint16_t)v); else rejectField("h_int"); }

			/* Syslog forwarder (overlay SyslogConfigData). The four fields read
			 * together so setSyslogConfig writes the overlay once; any absent
			 * field keeps its stored value. The enable is read RAW (intent),
			 * not effective, so "enable now, set server next commit" does not
			 * clear the toggle. The server is a dotted quad validated by the
			 * same isValidIpv4 as staticIp and stored as a 4-byte IPv4 — the
			 * overlay has no room for a hostname and a LAN collector is
			 * addressed by IP. Empty string clears it (disables). */
			{
				bool     slEn   = _storageRef->getSyslogEnabledFlag( );
				uint32_t slIp   = _storageRef->getSyslogServerIp( );
				uint16_t slPort = _storageRef->getSyslogPort( );
				uint8_t  slLvl  = _storageRef->getSyslogMinLevel( );
				bool touched = false;
				fl = readFlag("slog_en"); if (fl >= 0) { slEn = (fl == 1); touched = true; }
				if (has("slog_srv")) {
					String s = getStr("slog_srv"); s.trim( );
					if (s.length( ) == 0) { slIp = 0; touched = true; }
					else if (isValidIpv4(s.c_str( ))) {
						IPAddress a; a.fromString(s.c_str( )); slIp = (uint32_t)a; touched = true;
					} else rejectField("slog_srv");
				}
				if (has("slog_port")) { int v; if (parseIntStrict(getNum("slog_port"), v) && isInRange(v, 1, 65535)) { slPort = (uint16_t)v; touched = true; } else rejectField("slog_port"); }
				if (has("slog_lvl")) { int v; if (parseIntStrict(getNum("slog_lvl"), v) && isInRange(v, 0, 4)) { slLvl = (uint8_t)v; touched = true; } else rejectField("slog_lvl"); }
				if (touched) _storageRef->setSyslogConfig(slEn, slIp, slPort, slLvl);
			}
			(void)getInt; /* lambda kept for future fields, suppress -Wunused */
		}
	}

	/* ── alarms section: format {sensors:[{idx,active,tmin,tmax,hmin,hmax}],
	 * sounds:{...}}. Same manual parsing used in handleApiSaveAlarms. */
	int almStart = secStart[SEC_ALARMS];
	if (almStart >= 0) {
		/* Sensors array */
		int sensorsStart = body.indexOf("\"sensors\"", almStart);
		int arrStart = (sensorsStart >= 0) ? body.indexOf('[', sensorsStart) : -1;
		/* End of the outer array by depth, not by the first ']'. Each entry now
		 * carries "<channel>":[min,max], so the first bracket to close is a
		 * limit pair — taking it as the end truncated the array mid-object and
		 * the whole section applied nothing, silently. The slots section above
		 * already had to learn this when its objects gained "p":[...]. */
		int arrEnd = -1;
		if (arrStart >= 0) {
			int depth = 0;
			for (int k = arrStart; k < (int)body.length( ); k++) {
				char c = body.charAt(k);
				if (c == '[') depth++;
				else if (c == ']') { if (--depth == 0) { arrEnd = k; break; } }
			}
		}
		if (arrStart >= 0 && arrEnd > arrStart) {
			String arrStr = body.substring(arrStart, arrEnd + 1);
			int objStart = 0;
			int safety = 0; /* Cap iterations on adversarial payloads. */
			while ((objStart = arrStr.indexOf('{', objStart)) >= 0) {
				if (++safety > MAX_SENSORS + 4) break;
				int objEnd = arrStr.indexOf('}', objStart);
				if (objEnd < 0) break;
				String obj = arrStr.substring(objStart, objEnd + 1);

				int idxPos = obj.indexOf("\"idx\"");
				if (idxPos < 0) { objStart = objEnd + 1; continue; }
				int idxColon = obj.indexOf(':', idxPos);
				int idx = obj.substring(idxColon + 1).toInt( );

				/* idx is a slot, always. It used to accept -1 as "the ambient
				 * sensor" and write cfg.sensors[10] — including when slot 10 was
				 * inactive or held something else entirely. /api/alarms never
				 * emitted -1, so the only way in was a hand-built payload. */
				SensorRecord* rec = nullptr;
				if (idx >= 0 && idx < MAX_SENSORS && cfg.sensors[idx].active) rec = &cfg.sensors[idx];

				if (rec) {
					auto extractFloat = [&](const char* key) -> float {
						int kp = obj.indexOf(key);
						if (kp < 0) return NAN;
						int cp = obj.indexOf(':', kp + strlen(key));
						if (cp < 0) return NAN;
						/* Whitespace after the colon is legal JSON that the page's
						 * JSON.stringify never emits — but any other client can.
						 * parseFloat answers 0.0 for it, not NAN, so `"hmax": 80`
						 * wrote a 0 bound and the inverted-band fixer below then
						 * rewrote the pair to [min, min+0.1] — under a 200 OK.
						 * getNum in the sys section and jsonExtractFloat both
						 * already skip it; this reader was the one left behind. */
						int vs = cp + 1;
						while (vs < (int)obj.length( ) && (obj[vs] == ' ' || obj[vs] == '\t' ||
						       obj[vs] == '\r' || obj[vs] == '\n')) vs++;
						return parseFloat(obj.substring(vs).c_str( ));
					};
					float tmin = extractFloat("\"tmin\"");
					float tmax = extractFloat("\"tmax\"");
					float hmin = extractFloat("\"hmin\"");
					float hmax = extractFloat("\"hmax\"");
					if (!isnan(tmin)) rec->chMin[CH_TEMP] = tmin;
					if (!isnan(tmax)) rec->chMax[CH_TEMP] = tmax;
					if (!isnan(hmin)) rec->chMin[CH_HUM] = hmin;
					if (!isnan(hmax)) rec->chMax[CH_HUM] = hmax;

					/* Per-channel form, for anything the tmin/tmax/hmin/hmax keys
					 * above cannot name. Applied second so it wins where both are
					 * present — the fixed keys exist only for a cached page. */
					for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
						if (!channelValid(c)) continue;
						float lo = NAN, hi = NAN;
						if (!getPair(obj, channelInfo(c).key, lo, hi)) continue;
						if (!isnan(lo)) rec->chMin[c] = lo;
						if (!isnan(hi)) rec->chMax[c] = hi;
					}

					/* An inverted or empty band would never trip. Nudge the top
					 * above the bottom, then keep the pair inside the channel's
					 * plausible range — which is where the humidity-specific
					 * "clamp to 100" used to be written by hand. */
					for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
						if (!channelValid(c)) continue;
						const ChannelInfo& ci = channelInfo(c);
						if (rec->chMin[c] >= rec->chMax[c]) {
							rec->chMax[c] = roundf((rec->chMin[c] + 0.1f) * 10.0f) / 10.0f;
							if (rec->chMax[c] > ci.saneMax) {
								rec->chMax[c] = ci.saneMax;
								rec->chMin[c] = ci.saneMax - 0.1f;
							}
						}
					}
					rec->alarmsActive = jsonBoolValue(obj, "active", rec->alarmsActive);
				}
				objStart = objEnd + 1;
			}

			/* Reativar alarmes pelo web: o self-heal em checkAlarmConditions
			 * limpa o bit de desativação dos slots com alarmsActive=true — o
			 * âmbar de erro volta a reportar sem estado global. */
		}

		/* Sounds: parsed and applied via SoundSettingsState + fillConfig. */
		int soundsStart = body.indexOf("\"sounds\"", almStart);
		if (soundsStart >= 0) {
			int sObjStart = body.indexOf('{', soundsStart);
			int sObjEnd = body.indexOf('}', sObjStart);
			if (sObjStart >= 0 && sObjEnd > sObjStart) {
				String sObj = body.substring(sObjStart, sObjEnd + 1);
				/* Merge, not replace: start from what is configured and change
				 * only the keys the payload mentions — the same "absent keeps"
				 * the alarms section speaks. This block used to rebuild the
				 * state from scratch, so a partial {"volume":55} silently
				 * switched off every unmentioned toggle and reset the other
				 * volume to 70. The page always sends the full object and
				 * never noticed; an API client muting its own alarm sound as a
				 * side effect is the failure a monitoring device exists to not
				 * have. (Needles are quote-delimited, so "alarm" cannot latch
				 * onto "alarmVolume" or "melAlarm".) */
				SoundSettingsState snd = _soundRef ? _soundRef->getSettingsState( )
				                                   : SoundSettingsState{};
				auto sHas = [&](const char* k) { return sObj.indexOf(k) >= 0; };
				snd.touchEnabled = jsonBoolValue(sObj, "touch", snd.touchEnabled);
				snd.confirmEnabled = jsonBoolValue(sObj, "confirm", snd.confirmEnabled);
				snd.errorEnabled = jsonBoolValue(sObj, "error", snd.errorEnabled);
				snd.alarmEnabled = jsonBoolValue(sObj, "alarm", snd.alarmEnabled);
				snd.webEnabled = jsonBoolValue(sObj, "web", snd.webEnabled);
				snd.attentionEnabled = jsonBoolValue(sObj, "attention", snd.attentionEnabled);
				snd.muted = jsonBoolValue(sObj, "mute", snd.muted);

				int volPos = sObj.indexOf("\"volume\"");
				if (volPos >= 0) { int vc = sObj.indexOf(':', volPos); snd.volume = (uint8_t)constrain(sObj.substring(vc + 1).toInt( ), 0, 100); }

				int aVolPos = sObj.indexOf("\"alarmVolume\"");
				if (aVolPos >= 0) { int avc = sObj.indexOf(':', aVolPos); snd.alarmVolume = (uint8_t)constrain(sObj.substring(avc + 1).toInt( ), 0, 100); }

				auto extractMelIdx = [&](const char* key) -> uint8_t {
					int kp = sObj.indexOf(key);
					if (kp < 0) return 0;
					int cp = sObj.indexOf(':', kp);
					if (cp < 0) return 0;
					return (uint8_t)constrain(sObj.substring(cp + 1).toInt( ), 0, 5);
				};
				if (sHas("\"melTouch\"")) snd.touchMelody = extractMelIdx("\"melTouch\"");
				if (sHas("\"melConfirm\"")) snd.confirmMelody = extractMelIdx("\"melConfirm\"");
				if (sHas("\"melError\"")) snd.errorMelody = extractMelIdx("\"melError\"");
				if (sHas("\"melAlarm\"")) snd.alarmMelody = extractMelIdx("\"melAlarm\"");
				if (sHas("\"melAttention\"")) snd.attentionMelody = extractMelIdx("\"melAttention\"");

				if (_soundRef) {
					_soundRef->applySettingsState(snd);
					SoundConfigData* sndCfg = reinterpret_cast<SoundConfigData*>(
					                           cfg.reserved + sizeof(TouchCalData));
					_soundRef->fillConfig(sndCfg);
				}
			}
		}
	}

	/* ── users.actions section: processes add/del/reset in order ───────────
	 * Format: {"users":{"actions":[{"type":"add","name":"x","perms":511},
	 * {"type":"del","id":3},
	 * {"type":"reset","id":5}]}}
	 *
	 * Each add/reset mints a random one-time password (assignTempPassword) and
	 * appends it here; the response returns them ONCE so the admin can hand
	 * each new account its credential. */
	String tempCreds;
	int usrStart = secStart[SEC_USERS];
	if (usrStart >= 0) {
		int actionsPos = body.indexOf("\"actions\"", usrStart);
		int arrStart = (actionsPos >= 0) ? body.indexOf('[', actionsPos) : -1;
		int arrEnd = (arrStart >= 0) ? body.indexOf(']', arrStart) : -1;
		if (arrStart >= 0 && arrEnd > arrStart) {
			String arr = body.substring(arrStart, arrEnd + 1);
			int objStart = 0;
			int safety = 0; /* Cap iterations (users actions: max 8). */
			while ((objStart = arr.indexOf('{', objStart)) >= 0) {
				if (++safety > 16) break;
				int objEnd = arr.indexOf('}', objStart);
				if (objEnd < 0) break;
				String obj = arr.substring(objStart, objEnd + 1);
				/* type and name went through raw needles with the quote baked
				 * in ("\"type\":\""), so a payload with the space JSON allows
				 * after the colon matched nothing: type came back empty, both
				 * branches were skipped and the whole action evaporated under a
				 * 200. Same family as the float, string and boolean readers
				 * above — this section was simply never swept with them. */
				String type = jsonExtractStringValue(obj, "type");

				if (type == "add") {
					String name = jsonExtractStringValue(obj, "name");
					name.trim( );
					if (name.length( ) == 0 || !isValidName(name.c_str( ), 15) || name.equalsIgnoreCase("admin")) {
						rejectField("users.name"); objStart = objEnd + 1; continue;
					}
					/* dup check */
					bool dup = false;
					for (int i = 0; i < MAX_USERS; i++) {
						if (cfg.users[i].active && name.equalsIgnoreCase(String(cfg.users[i].username))) { dup = true; break; }
					}
					if (dup) { rejectField("users.dup"); objStart = objEnd + 1; continue; }

					/* Absent stays 0 — the page emits perms even with no box
					 * ticked, and an account that can log in and do nothing is
					 * a choice it offers. Out of the page's bit map is not. */
					long perms = 0;
					int pp = obj.indexOf("\"perms\":");
					if (pp >= 0) perms = obj.substring(pp + 8).toInt( );
					if (perms < 0 || perms > PERM_ALL_BITS) {
						rejectField("users.perms"); objStart = objEnd + 1; continue;
					}

					/* Capacity last: a full table is a state limit, and saying
					 * so is only useful once the request itself is sound. */
					int slot = -1;
					for (int i = 1; i < MAX_USERS; i++) {
						if (!cfg.users[i].active) { slot = i; break; }
					}
					if (slot < 0) { rejectField("users.full"); objStart = objEnd + 1; continue; }

					safeCopy(cfg.users[slot].username, name.c_str( ), sizeof(cfg.users[slot].username));
					cfg.users[slot].permissions = (uint16_t)perms;
					cfg.users[slot].active = true;
					/* Username must be set first — assignTempPassword salts and
					 * hashes over it. Sets password + salt + hashVersion +
					 * mustChangePassword and records the plaintext for the reply. */
					assignTempPassword(slot, tempCreds);
				}
				else if (type == "del" || type == "reset") {
					int ip = obj.indexOf("\"id\":");
					int id = (ip >= 0) ? obj.substring(ip + 5).toInt( ) : -1;
					if (!(id > 0 && id < MAX_USERS && cfg.users[id].active)) {
						/* Out of range, absent, already gone, or slot 0 — the
						 * admin, which no payload gets to delete. */
						rejectField("users.id"); objStart = objEnd + 1; continue;
					}
					if (type == "del") {
						cfg.users[id].active = false;
						memset(cfg.users[id].username, 0, sizeof(cfg.users[id].username));
						memset(cfg.users[id].password, 0, sizeof(cfg.users[id].password));
						cfg.users[id].permissions = 0;
					} else { /* reset */
						assignTempPassword(id, tempCreds);
					}
				}
				else {
					/* Neither add, del nor reset — a malformed entry, and the
					 * only signal that an action was read but not understood. */
					rejectField("users.type");
				}
				objStart = objEnd + 1;
			}
		}
	}

	/* ── net section: {ssid,pass,use_dhcp,ip,mask,gw,dns,ntp_server,web_port} ─ */
	uint16_t commitNewPort = 0; /* 0 = no change; != 0 = inform client. */
	int netStart = secStart[SEC_NET];
	if (netStart >= 0) {
		int objStart = body.indexOf('{', netStart);
		int objEnd = -1;
		if (objStart >= 0) {
			int depth = 0;
			for (int i = objStart; i < (int)body.length( ); i++) {
				char c = body.charAt(i);
				if (c == '{') depth++;
				else if (c == '}') { depth--; if (depth == 0) { objEnd = i; break; } }
			}
		}
		if (objStart >= 0 && objEnd > objStart) {
			String net = body.substring(objStart, objEnd + 1);
			auto getS = [&](const char* key) -> String {
				return jsonExtractStringValue(net, key);
			};
			auto getN = [&](const char* key) -> String {
				return jsonRawToken(net, key);
			};
			auto has = [&](const char* key) -> bool {
				return net.indexOf(String("\"") + key + "\":") >= 0;
			};

			if (has("ssid")) { String s = getS("ssid"); s.trim( ); if (s.length( ) > 0) safeCopy(cfg.wifiSsid, s.c_str( ), sizeof(cfg.wifiSsid)); }
			if (has("pass")) { String p = getS("pass"); p.trim( ); if (p.length( ) > 0) safeCopy(cfg.wifiPass, p.c_str( ), sizeof(cfg.wifiPass)); }
			/* Same `!= "0"` inversion as the sys section, doubled: this copy of
			 * the reader never learned to skip whitespace either, so a spaced
			 * `{"use_dhcp": 0}` produced the token " 0" and forced DHCP on for
			 * a payload asking for a static address. Both halves die with the
			 * shared reader. */
			auto readFlagN = [&](const char* k) -> int {
				const int b = jsonFlag(net, k);
				if (b == JSON_FLAG_BAD) rejectField(k);
				return b;
			};
			int nf;
			nf = readFlagN("use_dhcp"); if (nf >= 0) cfg.useDhcp = (nf == 1);
			if (!cfg.useDhcp) {
				if (has("ip")) { String s = getS("ip"); if (isValidIpv4(s.c_str( ))) safeCopy(cfg.staticIp, s.c_str( ), sizeof(cfg.staticIp)); }
				if (has("mask")) { String s = getS("mask"); if (isValidIpv4(s.c_str( ))) safeCopy(cfg.staticMask, s.c_str( ), sizeof(cfg.staticMask)); }
				if (has("gw")) { String s = getS("gw"); if (isValidIpv4(s.c_str( ))) safeCopy(cfg.staticGateway, s.c_str( ), sizeof(cfg.staticGateway)); }
				if (has("dns")) { String s = getS("dns"); if (isValidIpv4(s.c_str( ))) safeCopy(cfg.staticDns, s.c_str( ), sizeof(cfg.staticDns)); }
			}
			/* dns_auto + dns2 (primary manual reuses cfg.staticDns).
			 * Also accepts dns1 as shortcut for staticDns when user is in
			 * manual mode with DHCP=true (without the other staticIp/mask/gw fields). */
			nf = readFlagN("dns_auto"); if (nf >= 0) _storageRef->setDnsAuto(nf == 1);
			if (has("dns1")) { String s = getS("dns1"); if (isValidIpv4(s.c_str( ))) safeCopy(cfg.staticDns, s.c_str( ), sizeof(cfg.staticDns)); }
			if (has("dns2")) {
				String s = getS("dns2"); s.trim( );
				/* Empty is valid (clears secondary). Any other value must be IPv4. */
				if (s.length( ) == 0 || isValidIpv4(s.c_str( ))) _storageRef->setSecondaryDns(s.c_str( ));
			}
			if (has("ntp_server")) {
				String ntp = getS("ntp_server"); ntp.trim( );
				safeCopy(cfg.ntpServer, ntp.c_str( ), sizeof(cfg.ntpServer));
				cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
			}
			if (has("web_port")) {
				WebConfigData* w = reinterpret_cast<WebConfigData*>(cfg.reserved + WEB_CONFIG_OFFSET);
				int p = getN("web_port").toInt( );
				if (p >= 1 && p <= 65535) {
					if (w->port != (uint16_t)p) commitNewPort = (uint16_t)p;
					w->port = (uint16_t)p;
				}
			}
			/* Keep-alive opt-out (SetupFlagsData overlay). No live hook:
			 * commit_all reboots and beginServer( ) reads the flag at boot. */
			nf = readFlagN("web_ka"); if (nf >= 0) _storageRef->setWebKeepAliveEnabled(nf == 1);
		}
	}

	/* ── calib section: apply sensor hwId/name changes ──────────── */
 {
  int calStart = secStart[SEC_CALIB];
  if (calStart >= 0) {
   int objStart = body.indexOf('{', calStart);
   int objEnd = -1;
   if (objStart >= 0) {
    int depth = 0;
    for (int i = objStart; i < (int)body.length(); i++) {
     char c = body.charAt(i); if (c == '{') depth++; else if (c == '}') { depth--; if (depth == 0) { objEnd = i; break; } }
    }
   }
   if (objStart >= 0 && objEnd > objStart) {
    String calJson = body.substring(objStart, objEnd + 1);
    /* Extract sensors array: "sensors":[{...}] */
    int arrStart = calJson.indexOf("\"sensors\"");
    if (arrStart >= 0) {
     arrStart = calJson.indexOf('[', arrStart);
     /* Depth-aware on both axes: the staged entries carry "cal" point
      * arrays whose ']' and '}' would otherwise end the slice early. */
     int arrEnd = (arrStart >= 0) ? jsonMatchEnd(calJson, arrStart) : -1;
     if (arrStart >= 0 && arrEnd > arrStart) {
      String arr = calJson.substring(arrStart + 1, arrEnd);
      int objPos = 0; int safety = 0;
      while ((objPos = arr.indexOf('{', objPos)) >= 0 && ++safety < 20) {
       int objEnd2 = jsonMatchEnd(arr, objPos);
       if (objEnd2 < 0) break;
       String obj = arr.substring(objPos, objEnd2 + 1);
       objPos = objEnd2 + 1;
       /* Extract slot */
       float slotF = -1; int slot = -1;
       int slotPos = obj.indexOf("\"slot\":");
       if (slotPos >= 0) {
        slotF = obj.substring(slotPos + 7).toFloat();
        slot = (int)slotF;
       }
       if (slot < 0 || slot >= MAX_SENSORS || !cfg.sensors[slot].active) continue;
       /* Extract hwId */
       int hwPos = obj.indexOf("\"hwId\":\"");
       if (hwPos >= 0) {
        int hwStart = hwPos + 8; int hwEnd = obj.indexOf('"', hwStart);
        if (hwEnd > hwStart) {
         String hwId = obj.substring(hwStart, hwEnd);
         if (hwId.length() > 0) safeCopy(cfg.sensors[slot].hwId, hwId.c_str(), sizeof(cfg.sensors[slot].hwId));
        }
       }
       /* Extract name */
       int nmPos = obj.indexOf("\"name\":\"");
       if (nmPos >= 0) {
        int nmStart = nmPos + 8; int nmEnd = obj.indexOf('"', nmStart);
        if (nmEnd > nmStart) {
         String name = obj.substring(nmStart, nmEnd);
         if (name.length() > 0) safeCopy(cfg.sensors[slot].friendlyName, name.c_str(), sizeof(cfg.sensors[slot].friendlyName));
        }
       }
      }
     }
    }
   }
  }
 }

 if (themeChanged && _displayRef) _displayRef->refreshTheme( );

	/* Show status message on the display BEFORE any flash I/O.
	 * Core 1 goes to boot screen and renders, giving visual feedback
	 * to the user that the restart is imminent.
	 *
	 * Times: ~1.5s per message so the user can read before reboot.
	 * We feed WDT in chunks to avoid triggering during the hold. */
	if (_displayRef) {
		_displayRef->setBootStatusKey(TR_BOOT_APPLYING_CFG);
		for (int i = 0; i < 15; i++) { delay(100); watchdog_update( ); }
		_displayRef->setBootStatusKey(TR_BOOT_REBOOTING);
		for (int i = 0; i < 15; i++) { delay(100); watchdog_update( ); }
	}

	/* Buffer the audit log — heavy task gate causes writeCompactToFlash
	 * to stack in _pendingLogs instead of writing directly. Avoids an extra
	 * flash write before save; saveConfiguration drains the buffer. */
	_storageRef->lockHeavyTask( );
	LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId,
	         TRL("Admin committed changes — rebooting"));
	_storageRef->unlockHeavyTask( );

	/* Single atomic save — drains pending logs + writes config. */
	bool saved = _storageRef->saveConfiguration( );

	if (!saved) {
		/* Save failed: undo the boot screen and return error to client.
		 * System stays alive; user can try again. */
		if (_displayRef) _displayRef->endBoot( );
		_server->send(500, "application/json", "{\"error\":\"save failed\"}");
		return;
	}

	/* Response to client before reboot. Includes newPort if the web port
	 * changed (client redirects to the new host:port) and creds if any account
	 * was added or reset — the one-time passwords, delivered here and nowhere
	 * else because the reboot is seconds away and the hash is all that survives
	 * it. Built as a String: the creds array alone can reach ~5×30 B and would
	 * not fit the old 224-byte buffer alongside the other fields. */
	String resp = "{\"status\":\"ok\"";
	if (commitNewPort != 0) { resp += ",\"newPort\":"; resp += (unsigned)commitNewPort; }
	if (rejectedList[0])    { resp += ",\"rejected\":["; resp += rejectedList; resp += "]"; }
	if (tempCreds.length( )) { resp += ",\"creds\":["; resp += tempCreds; resp += "]"; }
	resp += "}";
	_server->send(200, "application/json", resp);
	tempCreds = "";   /* drop the plaintext copy the moment it is on the wire */
	_server->client( ).stop( );

	/*
	 * Hard reboot via LogManager::safeReboot( ) — consolidated sequence:
	 * - markCleanReboot: autopsy does NOT report as HW_WATCHDOG (intentional)
	 * - Serial.flush + Serial.end: USB CDC cleanly disconnects on host
	 * - watchdog_enable(500, 1): 500ms WDT guarantees reset even if something
	 * hangs (lockout, multicore, flash GC). Eliminates the window where
	 * a frozen display could occur between save completion and reset.
	 */
	LogManager::instance( ).safeReboot( );
}

/* handleSaveNetwork replaced by handleApiCommitAll. */

/**
 * @brief Resets touch calibration via web API.
 *
 * Clears TouchCalData in config (invalidates magic), resets parameters
 * in DisplayManager, and saves to flash.
 */
/**
 * @brief POST /api/set_time — set manual RTC immediately.
 *
 * Body JSON: {"epoch": <uint32_t UTC seconds>}
 * Response: {"ok":true,"now":<uint32_t epoch now>} or {"error":"..."}.
 *
 * Does NOT go through commit-all or require reboot — applies immediately via
 * NetworkManager::setManualTime( ). Useful when ntp_enabled=false;
 * with ntp_enabled=true, the next NTP sync overwrites.
 * Permission: PERM_SYS_CONFIG.
 */
void WebManager::handleApiSetTime( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_SYS_CONFIG)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

	String body = _server->arg("plain");
	int p = body.indexOf("\"epoch\"");
	if (p < 0) { _server->send(400, "application/json", "{\"error\":\"missing epoch\"}"); return; }
	int vs = body.indexOf(':', p);
	if (vs < 0) { _server->send(400, "application/json", "{\"error\":\"bad format\"}"); return; }
	vs++;
	while (vs < (int)body.length( ) && (body.charAt(vs) == ' ' || body.charAt(vs) == '"')) vs++;
	int ve = vs;
	while (ve < (int)body.length( ) && isDigit(body.charAt(ve))) ve++;
	if (ve == vs) { _server->send(400, "application/json", "{\"error\":\"bad epoch\"}"); return; }
	uint32_t epoch = (uint32_t)body.substring(vs, ve).toInt( );
	if (epoch <= 1600000000UL) { _server->send(400, "application/json", "{\"error\":\"epoch too low\"}"); return; }

	_netRef->setManualTime((time_t)epoch);

	char json[64];
	snprintf(json, sizeof(json), "{\"ok\":true,\"now\":%lu}", (unsigned long)time(nullptr));
	_server->send(200, "application/json", json);
}

void WebManager::handleResetTouchCal( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_SYS_CONFIG)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }
	if (rejectIfTouchPriority( )) return;

	SystemConfig& cfg = _storageRef->getConfig( );
	TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
	memset(cal, 0, sizeof(TouchCalData));
	_displayRef->resetTouchCalibration( );
	_storageRef->saveConfiguration( );

	/* Same reasoning as the CLI path: the reset alone only restores default
	 * corner values, and telling the user to recalibrate from the display menu
	 * assumes a touchscreen usable enough to reach it. Start the wizard. */
	_displayRef->showTouchCalibration( );
	_displayRef->resetTouchIdle( );

	if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_CONFIRM);
	LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Touch calibration reset via web"));

	_server->send(200, "application/json", "{\"status\":\"ok\",\"wizard\":true}");
}

/* Rebind the day's history to the slots as they are SAVED right now.
 *
 * A .h5 appends a SCHEMA chunk on identity change, so a slot
 * added or renamed after the day's file exists has no column to write into:
 * the record is still appended, its channel just stays NaN, and nothing in the
 * log says so. The caller must have committed first — rebinding on top of
 * staged-but-unsaved edits would freeze the old schema all over again. The page
 * guards that; this handler cannot see Pending.
 *
 * DEFAULT PATH IS NON-DESTRUCTIVE. migrateSchema streams the day's file into
 * a verified replacement, carrying every column that still exists and filling
 * the new ones with the NaN sentinel back to 00:00. It only fails when the
 * source itself is unreadable, and it leaves the original in place when it does.
 *
 * ?force=1 selects the old destructive rebindSchema, which recreates the file
 * empty. It exists for the case migration cannot handle — a corrupt source —
 * and is never chosen on the client's behalf: throwing the day away is the
 * user's call, so the page asks first.
 *
 * On success the device reboots, which is what puts every sensor back on a
 * codec built from the file that is now live.
 *
 * rebindSchema and migrateSchema both carry their own Core1FlashPause and
 * WDT window; the quiet mode here parks Core 1's rendering for the rewrite. */
void WebManager::handleApiHistoryRebind( ) {
	uint16_t perms = getAuthPerms( );
	if (!(perms & PERM_SYS_CONFIG)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }
	if (rejectIfTouchPriority( )) return;

	const bool force = _server->hasArg("force") && _server->arg("force") == "1";

	uint8_t mc = 0;
	uint8_t carried = 0;
	uint32_t records = 0;
	bool ok;

	_displayRef->requestQuietMode( );
	if (force) ok = _storageRef->rebindSchema(&mc);
	else       ok = _storageRef->migrateSchema(&mc, &records, &carried);
	_displayRef->releaseQuietMode( );

	if (!ok) {
		/* Distinct body for the migration case: the page turns this into the
		 * offer to recreate the file instead, rather than a dead end. */
		_server->send(500, "application/json",
		             force ? "{\"error\":\"rebind failed\"}"
		                   : "{\"error\":\"migrate failed\",\"canForce\":true}");
		return;
	}

	if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_CONFIRM);
	LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId,
	         force ? TRL("History schema rebuilt via web (day discarded)")
	               : TRL("History schema migrated via web (day kept)"));
	LOG_CODE(LOG_INFO, "HIST", APP_HIST_SCHEMA_MISMATCH, (int)mc, "");

	char json[96];
	snprintf(json, sizeof(json),
	         "{\"status\":\"ok\",\"meas\":%u,\"recs\":%lu,\"kept\":%u,\"forced\":%s,\"reboot\":true}",
	         (unsigned)mc, (unsigned long)records, (unsigned)carried, force ? "true" : "false");
	_server->send(200, "application/json", json);
	_server->client( ).stop( );

	/* Same consolidated sequence as commit-all: marks the reboot clean so the
	 * autopsy does not report a HW watchdog, flushes USB CDC, then resets. */
	LogManager::instance( ).safeReboot( );
}
