/**
 * @file WebManager_Auth.cpp
 * @brief Authentication: login/logout, sessions, rate limiting, nonces, password handling, RBAC.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "Themes.h"
#include "TouchPriority.h"
#include <bearssl/bearssl_hash.h>

using ReadGuard = StorageManager::ReadGuard;

void WebManager::clearStaleSessions( ) {
	uint32_t now = millis( );
	for (int i = 0; i < 3; i++) {
		if (_activeSessions[i].token != "") {
			if (now - _activeSessions[i].lastActivity > 900000) {
				LOG_CODE(LOG_INFO, "SEC", SEC_SESSION_EXPIRE, i, String(TRL("Session expired: ")) + _activeSessions[i].username);

				memset((void*)_activeSessions[i].token.begin( ), 0, _activeSessions[i].token.length( ));
				_activeSessions[i].token = "";
			}
		}
	}
}

uint16_t WebManager::getAuthPerms( ) {
	clearStaleSessions( );

	if (!_server->hasHeader("Cookie")) return 0;
	String cookie = _server->header("Cookie");

	for (int i = 0; i < 3; i++) {
		if (_activeSessions[i].token != "" && cookie.indexOf("SIMUTSESS=" + _activeSessions[i].token) != -1) {
			_activeSessions[i].lastActivity = millis( );
			_currentUserId = _activeSessions[i].userId;
			_currentUserName = _activeSessions[i].username;
			_currentUserPerms = _activeSessions[i].perms;
			return _currentUserPerms;
		}
	}
	return 0;
}

bool WebManager::isPasswordChangeRequired( ) {
	if (_currentUserId >= 0 && _currentUserId < MAX_USERS) {
		return _storageRef->getConfig( ).users[_currentUserId].mustChangePassword;
	}
	return false;
}


/* Auth prelude shared by the PROGMEM and filesystem page servers. Returns false
 * when it has already answered (redirect or 403) and the caller must stop. */
bool WebManager::checkPageAccess(uint16_t requiredPerm) {
	uint16_t perms = getAuthPerms( );
	if (perms == 0) {
		_server->sendHeader("Location", "/login", true);
		_server->send(302, "text/plain", "");
		return false;
	}
	if (isPasswordChangeRequired( )) {
		_server->sendHeader("Location", "/force_chpass", true);
		_server->send(302, "text/plain", "");
		return false;
	}
	if (!(perms & requiredPerm)) {
		LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId, _currentUserName);
		_server->send(403, "text/html", "<h2>Access Denied</h2>");
		return false;
	}
	return true;
}


/* Serves a gzipped page held on LittleFS (see FS_PAGES in
 * tools/build_webui_gz.py). Same contract as serveProtectedPage — the browser
 * cannot tell the two apart.
 *
 * NO CALLERS as of 2026-07-26 — /config, its only user, moved back into flash.
 * Kept on purpose: it is the runtime half of the FS_PAGES mechanism in
 * tools/build_webui_gz.py, and --gc-sections drops it from the image, so an
 * unused copy costs nothing. Listing any page in FS_PAGES puts it back to
 * work; deleting this would mean rewriting a tested helper to do that. */
bool WebManager::serveProtectedFsPage(uint16_t requiredPerm, const char* path) {
	if (!checkPageAccess(requiredPerm)) return false;

	File f;
	{
		ReadGuard rg(_storageRef);
		f = LittleFS.open(path, "r");
	}
	if (!f) {
		/* The firmware flashed but the page was never uploaded. That is a
		 * missing deploy step, not a broken route, so say which file and
		 * where rather than answering a bare 404.
		 *
		 * The path is read from the argument rather than named literally: this
		 * used to say config.html.gz, which stopped being true when /config
		 * moved back into the firmware and FS_PAGES emptied. Whatever page is
		 * moved out next (build_webui_gz.py nominates HIST_PAGE) gets a correct
		 * message without anyone remembering to edit this string. */
		/* `path` already starts with /web/, so the prefix here is just "data" —
		 * it read "data/web" while the mechanism had no callers, which would
		 * have printed data/web/web/... the moment one came back. */
		String msg = "<h2>Page asset missing</h2><p>Upload <code>data";
		msg += path;
		msg += "</code> to <code>/web/</code> on the device (Files page), then reload."
		       "<br>Do not use <code>uploadfs</code> — it reformats the partition.</p>";
		_server->send(200, "text/html", msg);
		return false;
	}
	_server->sendHeader("Cache-Control", "no-store");
	_server->sendHeader("Content-Encoding", "gzip");
	safeStreamFile(f, "text/html");
	f.close( );
	return true;
}


bool WebManager::serveProtectedPage(uint16_t requiredPerm, const uint8_t* gz_data, size_t gz_len) {
	if (!checkPageAccess(requiredPerm)) return false;
	/* no-store: forces bypass of the browser's back-forward cache (bfcache).
	 * Previously using "public, max-age=3600", returning from another page
	 * would restore the JS state snapshot (selects with data-cd="1", old
	 * wrappers, stale listeners), breaking dropdowns and controls. */
	_server->sendHeader("Cache-Control", "no-store");
	_server->sendHeader("Content-Encoding", "gzip");
	_server->setContentLength(gz_len);
	_server->send(200, "text/html", "");
	safeSend_GZ(gz_data, gz_len);
	return true;
}
void WebManager::handleLogin( ) {
	_server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	_server->sendHeader("Pragma", "no-cache");
	_server->sendHeader("Expires", "0");

	_server->sendHeader("Content-Encoding", "gzip");
	_server->setContentLength(WebUI_GZ::LOGIN_PAGE_GZ_LEN);
	_server->send(200, "text/html", "");
	safeSend_GZ(WebUI_GZ::LOGIN_PAGE_GZ, WebUI_GZ::LOGIN_PAGE_GZ_LEN);
}

void WebManager::handleRoot( ) { serveProtectedPage(PERM_DASHBOARD, WebUI_GZ::DASH_PAGE_GZ, WebUI_GZ::DASH_PAGE_GZ_LEN); }
void WebManager::handleHistory( ) { serveProtectedPage(PERM_HISTORY | PERM_LOGS, WebUI_GZ::HIST_PAGE_GZ, WebUI_GZ::HIST_PAGE_GZ_LEN); }
/* /config used to be the one page that did not live in the firmware image:
 * with 660 bytes of headroom it was gzipped into data/web/config.html.gz and
 * streamed from LittleFS. Dropping the unused Bluetooth stack (platformio.ini)
 * freed 64732 B, so it came back in on 2026-07-26 for 11544 B — which also
 * removed the bootstrap trap, where a freshly formatted device answered
 * /config with "Page asset missing" until someone uploaded the file by hand.
 * See FS_PAGES in tools/build_webui_gz.py and docs/ANALISE_FLASH_RAM.md. */
void WebManager::handleConfig( ) { serveProtectedPage(PERM_SYS_CONFIG, WebUI_GZ::CFG_PAGE_GZ, WebUI_GZ::CFG_PAGE_GZ_LEN); }
void WebManager::handleNetwork( ) { serveProtectedPage(PERM_NET_CONFIG, WebUI_GZ::NET_PAGE_GZ, WebUI_GZ::NET_PAGE_GZ_LEN); }
void WebManager::handleUsers( ) { serveProtectedPage(PERM_USER_MGR, WebUI_GZ::USR_PAGE_GZ, WebUI_GZ::USR_PAGE_GZ_LEN); }
void WebManager::handleFiles( ) { serveProtectedPage(PERM_FILE_READ, WebUI_GZ::FILE_PAGE_GZ, WebUI_GZ::FILE_PAGE_GZ_LEN); }
/* Alarms is in FS_PAGES: pico_w_test had 152 bytes of real headroom, and this
 * page is the only large one that passes all three parts of the test — big,
 * rarely opened, and not needed to bring the device up. A unit without the
 * file still samples, logs and fires the alarms it already has; it just cannot
 * edit them from the web. HIST_PAGE is bigger but is the hottest page there
 * is, and CFG_PAGE broke the bootstrap when it was tried in July. */
void WebManager::handleAlarms( ) { serveProtectedFsPage(PERM_SYS_CONFIG, "/web/alarms.html.gz"); }
/* Served from LittleFS, not linked into the image — see FS_PAGES in
 * tools/build_webui_gz.py for why this page and not another. */
void WebManager::handleLicense( ) { serveProtectedFsPage(PERM_DASHBOARD, "/web/license.html.gz"); }
void WebManager::handleForceChpass( ) {
	if (getAuthPerms( ) == 0) { _server->sendHeader("Location", "/login", true); _server->send(302, "text/plain", ""); return; }
	if (!isPasswordChangeRequired( )) { _server->sendHeader("Location", "/", true); _server->send(302, "text/plain", ""); return; }

	_server->sendHeader("Content-Encoding", "gzip");
	_server->setContentLength(WebUI_GZ::FORCE_CHPASS_PAGE_GZ_LEN);
	_server->send(200, "text/html", "");
	safeSend_GZ(WebUI_GZ::FORCE_CHPASS_PAGE_GZ, WebUI_GZ::FORCE_CHPASS_PAGE_GZ_LEN);
}
void WebManager::handleApiLoginInit( ) {
	uint32_t clientIP = (uint32_t)_server->client( ).remoteIP( );

	/* Looks for the client IP's own slot first; if not found, picks the LRU
	 * among evictable slots (free OR without active lockout). A slot under
	 * unexpired lockout CANNOT be overwritten — this prevents rate-limit
	 * bypass by IP rotation (an attacker locked in slot X cannot evict X
	 * by cycling through 8 new IPs). */
	int slot = -1;
	int oldestEvictable = -1;
	for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
		if (_loginStates[i].ip == clientIP) { slot = i; break; }
		bool evictable = (_loginStates[i].ip == 0)
		                 || (_loginStates[i].lockoutUntil == 0)
		                 || timeReached(_loginStates[i].lockoutUntil);
		if (evictable) {
			if (oldestEvictable == -1 ||
			    _loginStates[i].lastActivity < _loginStates[oldestEvictable].lastActivity) {
				oldestEvictable = i;
			}
		}
	}
	if (slot == -1) {
		if (oldestEvictable == -1) {
			/* All 8 slots under active lockout — extreme edge case (in
			 * normal operation, max 5 min lockouts expire sequentially).
			 * Refuse the request with 429 + suggested Retry-After. */
			uint32_t minRem = UINT32_MAX;
			for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
				uint32_t rem = timeRemaining(_loginStates[i].lockoutUntil);
				if (rem > 0 && rem < minRem) minRem = rem;
			}
			if (minRem == UINT32_MAX) minRem = 60000;
			uint32_t retryAfterSec = (minRem + 999) / 1000;
			LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0,
			         "Login init rejected: all slots locked");
			_server->sendHeader("Retry-After", String(retryAfterSec));
			char buf[64];
			snprintf(buf, sizeof(buf),
			         "{\"ok\":false,\"err\":3,\"retryAfter\":%lu}",
			         (unsigned long)retryAfterSec);
			_server->send(429, "application/json", buf);
			return;
		}
		slot = oldestEvictable;
		_loginStates[slot].ip = clientIP;
		_loginStates[slot].failCount = 0;
		_loginStates[slot].lockoutUntil = 0;
	}


	/* Temporary String destroyed after safeCopy — no residual heap. */
	safeCopy(_loginStates[slot].nonce, generateSecureToken( ).c_str( ),
	         sizeof(_loginStates[slot].nonce));
	_loginStates[slot].nonceCreatedAt = millis( );
	_loginStates[slot].lastActivity = millis( );

	uint32_t lockSec = 0;
	bool locked = false;
	/* Wrap-safe: millis( ) wraps every ~49.7 days; direct comparisons invert. */
	if (_loginStates[slot].lockoutUntil > 0 && !timeReached(_loginStates[slot].lockoutUntil)) {
		lockSec = timeRemaining(_loginStates[slot].lockoutUntil) / 1000;
		locked = true;
	}

	char json[128];
	snprintf(json, sizeof(json), "{\"nonce\":\"%s\",\"locked\":%s,\"lockSec\":%lu}",
	         _loginStates[slot].nonce, locked ? "true" : "false", (unsigned long)lockSec);

	_server->sendHeader("Cache-Control", "no-store");
	_server->send(200, "application/json", json);
}

/* ===========================================================================
 * handleApiLogin decomposed into named private helpers.
 * ===========================================================================
 * The orchestrator delegates each step to a named private helper. Each helper
 * is responsible for its side effect (penalize/respond) when that keeps the
 * step atomic; the orchestrator only handles the early-exit flow.
 */

int WebManager::findLoginStateForIp(uint32_t clientIP) const {
	for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
		if (_loginStates[i].ip == clientIP) return i;
	}
	return -1;
}

uint32_t WebManager::applyExponentialPenalty(int ls) {
	if (ls < 0) return 0;
	_loginStates[ls].failCount++;
	uint32_t penaltyMs = (1U << _loginStates[ls].failCount) * 1000U;
	if (penaltyMs > 300000U) penaltyMs = 300000U;
	_loginStates[ls].lockoutUntil = millis( ) + penaltyMs;
	return penaltyMs;
}

bool WebManager::respondIfLockedOut(int ls, int httpCode) {
	if (ls < 0) return false;
	if (_loginStates[ls].lockoutUntil == 0) return false;
	if (timeReached(_loginStates[ls].lockoutUntil)) return false;
	uint32_t rem = timeRemaining(_loginStates[ls].lockoutUntil) / 1000;
	char buf[64];
	snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)rem);
	_server->send(httpCode, "application/json", buf);
	return true;
}

bool WebManager::validateNonceAndRespond(int ls) {
	/* expectedNonce is a pointer to the slot's fixed buffer (or empty string
	 * if no slot exists). Comparison via operator==(String, const char*). */
	const char* expectedNonce = (ls >= 0) ? _loginStates[ls].nonce : "";
	bool nonceExpired = (ls >= 0) && (_loginStates[ls].nonceCreatedAt > 0) &&
	                    timeSince(_loginStates[ls].nonceCreatedAt, NONCE_LIFETIME_MS);

	bool ok = _server->hasArg("nonce") &&
	          _server->arg("nonce") == expectedNonce &&
	          expectedNonce[0] != '\0' &&
	          !nonceExpired;
	if (ok) {
		if (ls >= 0) _loginStates[ls].nonce[0] = '\0';
		return true;
	}

	/* Failure: invalidate nonce + penalize expired nonce (not invalid nonce). */
	if (ls >= 0) {
		_loginStates[ls].nonce[0] = '\0';
		if (nonceExpired) applyExponentialPenalty(ls);
	}
	LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0,
	         nonceExpired ? "Login Rejected: Nonce Expired" : "Login Rejected: Invalid Nonce");
	if (!respondIfLockedOut(ls, 401)) {
		_server->send(401, "application/json", "{\"ok\":false,\"err\":1}");
	}
	return false;
}

int WebManager::verifyPasswordFor(const String& u, const String& p) {
	SystemConfig& cfg = _storageRef->getConfig( );
	for (int i = 0; i < MAX_USERS; i++) {
		if (!cfg.users[i].active || String(cfg.users[i].username) != u) continue;

		String storedHash = String(cfg.users[i].password);
		bool passValid = false;
		bool needsMigration = false;

		/* The "*PENDING*" temporary-password branch is gone. It accepted
		 * sha256(Capitalized(username)@DDMMYYYY) as the first password after an
		 * add/reset — a value derivable by anyone who knew the username (which
		 * /api/users lists) and the date. Accounts are now created with a
		 * random one-time password (assignTempPassword, WebManager_Commit.cpp),
		 * stored as an ordinary V1 hash, so they verify on the V1 path below
		 * like any other account. A stale "*PENDING*" literal left on a device
		 * from an older build simply never matches here — that account must be
		 * reset by an admin, which is the intended outcome, not a lockout bug. */
		/* Legacy: hashVersion==0, 30 chars (120 bits), username-salt, 2500 rounds. */
		if (cfg.users[i].hashVersion == 0 && storedHash.length( ) == 30) {
			String legacyHash = _storageRef->hashPasswordLegacy(u, p);
			if (secureCompare(storedHash, legacyHash)) {
				passValid = true;
				needsMigration = true;
			}
		}
		/* V1: hashVersion>=1, 32 chars (128 bits), random salt, PASSWORD_HMAC_ROUNDS. */
		else {
			String inputHash = _storageRef->hashPasswordV1(u, p, cfg.users[i].salt);
			if (secureCompare(storedHash, inputHash)) passValid = true;
		}

		if (passValid) {
			/* Transparent migration: re-hash with random salt + 32 chars. */
			if (needsMigration) {
				_storageRef->generateSalt(cfg.users[i].salt);
				String newHash = _storageRef->hashPasswordV1(u, p, cfg.users[i].salt);
				safeCopy(cfg.users[i].password, newHash.c_str( ), sizeof(cfg.users[i].password));
				cfg.users[i].hashVersion = 1;
				_storageRef->saveConfiguration( );
			}
			return i;
		}
	}
	return -1;
}

int WebManager::allocSessionSlot(int foundId) {
	clearStaleSessions( );
	/* Reuses slot for the same user (consecutive logins from the same device). */
	for (int i = 0; i < 3; i++) {
		if (_activeSessions[i].token != "" && _activeSessions[i].userId == foundId) return i;
	}
	/* First empty slot. */
	for (int i = 0; i < 3; i++) {
		if (_activeSessions[i].token == "") return i;
	}
	return -1;
}

void WebManager::completeLogin(int slot, int foundId, int ls, const String& u) {
	if (ls >= 0) {
		_loginStates[ls].failCount = 0;
		_loginStates[ls].lockoutUntil = 0;
	}

	SystemConfig& cfg = _storageRef->getConfig( );
	String newToken = generateSecureToken( );

	_activeSessions[slot].token = newToken;
	_activeSessions[slot].userId = foundId;
	_activeSessions[slot].username = u;
	_activeSessions[slot].perms = cfg.users[foundId].permissions;
	_activeSessions[slot].lastActivity = millis( );

	_currentUserId = foundId;
	_currentUserName = u;
	_currentUserPerms = _activeSessions[slot].perms;

	LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, foundId, String(TRL("Login OK: ")) + u);
	if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_CONFIRM);

	if (_displayRef) _displayRef->setWebNotification(u.c_str( ));

	/* Secure keyed to the ACTUAL transport (a provisioned cert made the server
	 * HTTPS), not the dead cfg.useHttps flag. Over HTTPS the session cookie must
	 * carry Secure so a browser never replays it on a downgraded http:// origin;
	 * over HTTP it must NOT, or the browser drops the cookie and login loops —
	 * the very trap the old useHttps flag was stuck avoiding by staying false. */
	String cookieFlags = "SIMUTSESS=" + newToken + "; Path=/; HttpOnly; SameSite=Strict";
	if (_serverIsHttps) cookieFlags += "; Secure";
	_server->sendHeader("Set-Cookie", cookieFlags);

	const char* redirect = cfg.users[foundId].mustChangePassword ? "/force_chpass" : "/";
	char resp[64];
	snprintf(resp, sizeof(resp), "{\"ok\":true,\"redirect\":\"%s\"}", redirect);
	_server->send(200, "application/json", resp);
}

void WebManager::handleApiLogin( ) {
	uint32_t clientIP = (uint32_t)_server->client( ).remoteIP( );
	int ls = findLoginStateForIp(clientIP);

	/* Active lockout: immediate 403 (does not consume nonce). */
	if (respondIfLockedOut(ls, 403)) return;

	/* Nonce CSRF: validate + consume on success, penalize on expiry. */
	if (!validateNonceAndRespond(ls)) return;

	if (!_server->hasArg("user") || !_server->hasArg("pass")) {
		_server->send(400, "application/json", "{\"ok\":false,\"err\":1}");
		return;
	}

	String u = _server->arg("user");
	String p = _server->arg("pass");

	/* Size sanity check before invoking hashPassword (PASSWORD_HMAC_ROUNDS). */
	if (!isValidName(u.c_str( ), 31) || p.length( ) > 128) {
		applyExponentialPenalty(ls);
		LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Login Rejected: Invalid Input Size"));
		_server->send(401, "application/json", "{\"ok\":false,\"err\":1}");
		return;
	}

	int foundId = verifyPasswordFor(u, p);
	if (foundId < 0) {
		if (ls >= 0) {
			uint32_t penaltyMs = applyExponentialPenalty(ls);
			LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, String(TRL("Login Failed: ")) + u);
			if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_ERROR);
			char buf[64];
			snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)(penaltyMs/1000));
			_server->send(401, "application/json", buf);
		} else {
			_server->send(401, "application/json", "{\"ok\":false,\"err\":1}");
		}
		return;
	}

	int slot = allocSessionSlot(foundId);
	if (slot < 0) {
		LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Login Rejected: Max Sessions Reached"));
		_server->send(403, "application/json", "{\"ok\":false,\"err\":3}");
		return;
	}

	completeLogin(slot, foundId, ls, u);
}

void WebManager::handleLogout( ) {
	if (_server->hasHeader("Cookie")) {
		String cookie = _server->header("Cookie");
		for (int i = 0; i < 3; i++) {
			if (_activeSessions[i].token != "" && cookie.indexOf("SIMUTSESS=" + _activeSessions[i].token) != -1) {
				LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, 0, String(TRL("Logout: ")) + _activeSessions[i].username);

				memset((void*)_activeSessions[i].token.begin( ), 0, _activeSessions[i].token.length( ));
				_activeSessions[i].token = "";
				break;
			}
		}
	}

	_currentUserId = -1;
	_currentUserName = "";
	_currentUserPerms = 0;

	_server->sendHeader("Set-Cookie", "SIMUTSESS=0; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; SameSite=Strict");
	_server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	_server->sendHeader("Location", "/login", true);
	_server->send(302, "text/plain", "");
}

void WebManager::handleApiSecStatus( ) {
	if (!(getAuthPerms( ) & PERM_USER_MGR)) {
		_server->send(403, "application/json", "{\"error\":\"Forbidden\"}");
		return;
	}

	uint32_t now = millis( );
	char buf[512];
	int pos = 0;

	/* snprintf returns what WOULD have been written, so `pos` can run past the
	 * buffer the moment one entry truncates. `sizeof(buf) - pos` is size_t
	 * arithmetic: past the end it does not go negative, it wraps to about 4
	 * billion, and the next snprintf writes happily outside the array. With
	 * eight slots filled by long dotted-quad addresses the entries do reach
	 * 512 B, so the old bottom-of-loop guard fired one write too late. Clamp
	 * the room before every write instead, and treat "no room" as done. */
	#define SEC_ROOM() (int)(sizeof(buf) - (size_t)pos)
	pos += snprintf(buf + pos, SEC_ROOM(), "{\"slots\":[");

	bool first = true;
	for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
		if (_loginStates[i].ip == 0) continue;
		/* Leave room for the separator plus the "]}" terminator. */
		if (SEC_ROOM() < 4) break;
		if (!first) buf[pos++] = ',';
		first = false;

		uint32_t ip = _loginStates[i].ip;
		uint32_t lockSec = 0;
		bool locked = (_loginStates[i].lockoutUntil > 0 && !timeReached(_loginStates[i].lockoutUntil));
		if (locked) lockSec = timeRemaining(_loginStates[i].lockoutUntil) / 1000;
		uint32_t ageSec = (now - _loginStates[i].lastActivity) / 1000;

		int wrote = snprintf(buf + pos, SEC_ROOM(),
		                "{\"ip\":\"%lu.%lu.%lu.%lu\",\"fails\":%u,\"lockSec\":%lu,\"ageSec\":%lu}",
		                (unsigned long)(ip & 0xFF), (unsigned long)((ip >> 8) & 0xFF),
		                (unsigned long)((ip >> 16) & 0xFF), (unsigned long)((ip >> 24) & 0xFF),
		                _loginStates[i].failCount, (unsigned long)lockSec, (unsigned long)ageSec);
		if (wrote < 0 || wrote >= SEC_ROOM()) {
			/* Truncated: back out the partial entry (and its separator) so the
			 * list stays parseable, and stop — reporting fewer slots beats
			 * emitting JSON nobody can read. */
			buf[pos] = '\0';
			if (pos > 0 && buf[pos - 1] == ',') buf[--pos] = '\0';
			break;
		}
		pos += wrote;
	}

	pos += snprintf(buf + pos, SEC_ROOM(), "]}");
	#undef SEC_ROOM
	_server->sendHeader("Cache-Control", "no-store");
	_server->send(200, "application/json", buf);
}

void WebManager::handleApiForceChpass( ) {
	if (getAuthPerms( ) == 0 || !isPasswordChangeRequired( )) { _server->send(403, "text/plain", "Forbidden"); return; }
	if (rejectIfTouchPriority( )) return;

	String p1 = _server->arg("p1");
	String p2 = _server->arg("p2");

	/* preHash is the sha256(plaintext) that goes into hashPasswordV1, so the
	 * stored format is identical either way. Over HTTPS the client sends the
	 * PLAINTEXT (A-5): the server can finally judge strength — passwordPolicyOk
	 * runs on it — and does the sha256 the browser used to do. Over HTTP the
	 * client still sends the sha256 (no plaintext to leak on a cleartext link),
	 * and the server cannot enforce the policy; the length check there is on the
	 * 64-char digest and is kept only to reject an empty/garbage field. */
	String preHash;
#ifdef SIMUT_WEB_HTTPS
	/* HTTPS-only, so it compiles out of the flash-tight HTTP-only builds where
	 * _serverIsHttps can never be true anyway. */
	if (_serverIsHttps) {
		if (p1 != p2 || !passwordPolicyOk(p1.c_str( ))) {
			if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_ERROR);
			_server->send(400, "application/json", "{\"error\":\"Weak password: min 8 chars with a letter and a digit\"}");
			return;
		}
		preHash = _storageRef->sha256Hex(p1);
	} else
#endif
	{
		if (p1.length( ) != 64 || p1 != p2) {
			if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_ERROR);
			_server->send(400, "application/json", "{\"error\":\"Invalid payload\"}");
			return;
		}
		preHash = p1;
	}

	SystemConfig& cfg = _storageRef->getConfig( );

	_storageRef->generateSalt(cfg.users[_currentUserId].salt);
	String hashedNewPass = _storageRef->hashPasswordV1(
	                           _currentUserName, preHash, cfg.users[_currentUserId].salt);
	safeCopy(cfg.users[_currentUserId].password, hashedNewPass.c_str( ), sizeof(cfg.users[_currentUserId].password));
	cfg.users[_currentUserId].hashVersion = 1;
	cfg.users[_currentUserId].mustChangePassword = false;
	_storageRef->saveConfiguration( );

	if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_CONFIRM);
	LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, String(TRL("Password Reset Success: ")) + _currentUserName);

	_server->send(200, "application/json", "{\"status\":\"ok\"}");
}

/* Self-service password change pre-auth (accessible from the login screen).
 * Reuses lockout/nonce/verifyPasswordFor from the normal login flow.
 * Does not create a session — user logs in fresh with the new password. */
void WebManager::handleApiLoginChpass( ) {
	uint32_t clientIP = (uint32_t)_server->client( ).remoteIP( );
	int ls = findLoginStateForIp(clientIP);

	if (respondIfLockedOut(ls, 403)) return;
	if (!validateNonceAndRespond(ls)) return;

	if (!_server->hasArg("user") || !_server->hasArg("oldpass") || !_server->hasArg("newpass")) {
		_server->send(400, "application/json", "{\"ok\":false,\"err\":1}");
		return;
	}

	String u = _server->arg("user");
	String op = _server->arg("oldpass");
	String np = _server->arg("newpass");

	/* opHash/npHash are the sha256(plaintext) the rest of the flow works in.
	 * Over HTTPS the client sends the PLAINTEXT (A-5): the server enforces the
	 * strength floor on the new one and does the sha256 itself. Over HTTP the
	 * client sends the sha256 already and no policy can be enforced. */
	String opHash, npHash;
#ifdef SIMUT_WEB_HTTPS
	if (_serverIsHttps) {
		if (!isValidName(u.c_str( ), 31) || op.length( ) == 0 || !passwordPolicyOk(np.c_str( ))) {
			applyExponentialPenalty(ls);
			LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Chpass Rejected: Weak/Invalid Input"));
			_server->send(401, "application/json", "{\"ok\":false,\"err\":1}");
			return;
		}
		opHash = _storageRef->sha256Hex(op);
		npHash = _storageRef->sha256Hex(np);
	} else
#endif
	{
		if (!isValidName(u.c_str( ), 31) || op.length( ) != 64 || np.length( ) != 64) {
			applyExponentialPenalty(ls);
			LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Chpass Rejected: Invalid Input"));
			_server->send(401, "application/json", "{\"ok\":false,\"err\":1}");
			return;
		}
		opHash = op;
		npHash = np;
	}

	/* Blocks a no-op change (new == old). */
	if (npHash == opHash) {
		_server->send(400, "application/json", "{\"ok\":false,\"err\":5}");
		return;
	}

	int foundId = verifyPasswordFor(u, opHash);
	if (foundId < 0) {
		if (ls >= 0) {
			uint32_t penaltyMs = applyExponentialPenalty(ls);
			LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, String(TRL("Chpass Failed: ")) + u);
			if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_ERROR);
			char buf[64];
			snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)(penaltyMs/1000));
			_server->send(401, "application/json", buf);
		} else {
			_server->send(401, "application/json", "{\"ok\":false,\"err\":1}");
		}
		return;
	}

	SystemConfig& cfg = _storageRef->getConfig( );
	_storageRef->generateSalt(cfg.users[foundId].salt);
	String hashedNewPass = _storageRef->hashPasswordV1(u, npHash, cfg.users[foundId].salt);
	safeCopy(cfg.users[foundId].password, hashedNewPass.c_str( ), sizeof(cfg.users[foundId].password));
	cfg.users[foundId].hashVersion = 1;
	cfg.users[foundId].mustChangePassword = false;
	_storageRef->saveConfiguration( );

	if (ls >= 0) { _loginStates[ls].failCount = 0; _loginStates[ls].lockoutUntil = 0; }

	if (_soundRef->isWebSoundsEnabled( )) _soundRef->play(SND_CONFIRM);
	LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, foundId, String(TRL("Login Chpass OK: ")) + u);

	_server->send(200, "application/json", "{\"ok\":true}");
}
