/**
 * @file    WebManager_Auth.cpp
 * @brief   Authentication: login/logout, sessions, rate limiting, nonces, password handling, RBAC.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "Themes.h"
#include "TouchPriority.h"
#include <bearssl/bearssl_hash.h>

using ReadGuard = StorageManager::ReadGuard;

void WebManager::clearStaleSessions() {
    uint32_t now = millis();
    for (int i = 0; i < 3; i++) {
        if (_activeSessions[i].token != "") {
            if (now - _activeSessions[i].lastActivity > 900000) {
                LOG_CODE(LOG_INFO, "SEC", SEC_SESSION_EXPIRE, i, String(TRL("Session expired: ")) + _activeSessions[i].username);

                memset((void*)_activeSessions[i].token.begin(), 0, _activeSessions[i].token.length());
                _activeSessions[i].token = "";
            }
        }
    }
}

uint16_t WebManager::getAuthPerms() {
    clearStaleSessions();

    if (!_server.hasHeader("Cookie")) return 0;
    String cookie = _server.header("Cookie");

    for (int i = 0; i < 3; i++) {
        if (_activeSessions[i].token != "" && cookie.indexOf("SIMUTSESS=" + _activeSessions[i].token) != -1) {
            _activeSessions[i].lastActivity = millis();
            _currentUserId = _activeSessions[i].userId;
            _currentUserName = _activeSessions[i].username;
            _currentUserPerms = _activeSessions[i].perms;
            return _currentUserPerms;
        }
    }
    return 0;
}

bool WebManager::isPasswordChangeRequired() {
    if (_currentUserId >= 0 && _currentUserId < MAX_USERS) {
        return _storageRef->getConfig().users[_currentUserId].mustChangePassword;
    }
    return false;
}


bool WebManager::serveProtectedPage(uint16_t requiredPerm, const uint8_t* gz_data, size_t gz_len) {
    uint16_t perms = getAuthPerms();
    if (perms == 0) {
        _server.sendHeader("Location", "/login", true);
        _server.send(302, "text/plain", "");
        return false;
    }
    if (isPasswordChangeRequired()) {
        _server.sendHeader("Location", "/force_chpass", true);
        _server.send(302, "text/plain", "");
        return false;
    }
    if (!(perms & requiredPerm)) {
        LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId, _currentUserName);
        _server.send(403, "text/html", "<h2>Access Denied</h2>");
        return false;
    }
    /* no-store: força bypass do bfcache do browser (back-forward cache).
     * Antes era "public, max-age=3600" → ao voltar de outra pagina, o browser
     * restaurava o snapshot do JS state (selects com data-cd="1", wrappers
     * antigos, listeners obsoletos), travando dropdowns e controles. */
    _server.sendHeader("Cache-Control", "no-store");
    _server.sendHeader("Content-Encoding", "gzip");
    _server.setContentLength(gz_len);
    _server.send(200, "text/html", "");
    safeSend_GZ(gz_data, gz_len);
    return true;
}
void WebManager::handleLogin() {
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "0");

    _server.sendHeader("Content-Encoding", "gzip");
    _server.setContentLength(WebUI_GZ::LOGIN_PAGE_GZ_LEN);
    _server.send(200, "text/html", "");
    safeSend_GZ(WebUI_GZ::LOGIN_PAGE_GZ, WebUI_GZ::LOGIN_PAGE_GZ_LEN);
}

void WebManager::handleRoot()    { serveProtectedPage(PERM_DASHBOARD, WebUI_GZ::DASH_PAGE_GZ, WebUI_GZ::DASH_PAGE_GZ_LEN); }
void WebManager::handleHistory() { serveProtectedPage(PERM_HISTORY | PERM_LOGS, WebUI_GZ::HIST_PAGE_GZ, WebUI_GZ::HIST_PAGE_GZ_LEN); }
void WebManager::handleConfig()  { serveProtectedPage(PERM_SYS_CONFIG, WebUI_GZ::CFG_PAGE_GZ, WebUI_GZ::CFG_PAGE_GZ_LEN); }
void WebManager::handleNetwork() { serveProtectedPage(PERM_NET_CONFIG, WebUI_GZ::NET_PAGE_GZ, WebUI_GZ::NET_PAGE_GZ_LEN); }
void WebManager::handleUsers()   { serveProtectedPage(PERM_USER_MGR, WebUI_GZ::USR_PAGE_GZ, WebUI_GZ::USR_PAGE_GZ_LEN); }
void WebManager::handleFiles()   { serveProtectedPage(PERM_FILE_READ, WebUI_GZ::FILE_PAGE_GZ, WebUI_GZ::FILE_PAGE_GZ_LEN); }
void WebManager::handleAlarms()  { serveProtectedPage(PERM_SYS_CONFIG, WebUI_GZ::ALARMS_PAGE_GZ, WebUI_GZ::ALARMS_PAGE_GZ_LEN); }
void WebManager::handleLicense() { serveProtectedPage(PERM_DASHBOARD, WebUI_GZ::LICENSE_PAGE_GZ, WebUI_GZ::LICENSE_PAGE_GZ_LEN); }

void WebManager::handleForceChpass() {
    if (getAuthPerms() == 0) { _server.sendHeader("Location", "/login", true); _server.send(302, "text/plain", ""); return; }
    if (!isPasswordChangeRequired()) { _server.sendHeader("Location", "/", true); _server.send(302, "text/plain", ""); return; }

    _server.sendHeader("Content-Encoding", "gzip");
    _server.setContentLength(WebUI_GZ::FORCE_CHPASS_PAGE_GZ_LEN);
    _server.send(200, "text/html", "");
    safeSend_GZ(WebUI_GZ::FORCE_CHPASS_PAGE_GZ, WebUI_GZ::FORCE_CHPASS_PAGE_GZ_LEN);
}
void WebManager::handleApiLoginInit() {
    uint32_t clientIP = (uint32_t)_server.client().remoteIP();

    /* SEC-006: busca slot do próprio IP primeiro; se não existe, escolhe LRU
     * mas apenas entre slots "evictáveis" (livres OU sem lockout ativo). Um
     * slot sob lockout não-expirado NÃO pode ser sobrescrito — isso evita
     * bypass do rate-limit por rotação de IPs (atacante lockado em slot X
     * não consegue evictar X cyclando por 8 IPs novos). */
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
            /* Todos os 8 slots sob lockout ativo — edge case extremo (em
             * operação normal, lockouts máx 5 min expiram em sequência).
             * Recusa o pedido com 429 + Retry-After sugerido. */
            uint32_t minRem = UINT32_MAX;
            for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
                uint32_t rem = timeRemaining(_loginStates[i].lockoutUntil);
                if (rem > 0 && rem < minRem) minRem = rem;
            }
            if (minRem == UINT32_MAX) minRem = 60000;
            uint32_t retryAfterSec = (minRem + 999) / 1000;
            LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0,
                     "Login init rejected: all slots locked");
            _server.sendHeader("Retry-After", String(retryAfterSec));
            char buf[64];
            snprintf(buf, sizeof(buf),
                     "{\"ok\":false,\"err\":3,\"retryAfter\":%lu}",
                     (unsigned long)retryAfterSec);
            _server.send(429, "application/json", buf);
            return;
        }
        slot = oldestEvictable;
        _loginStates[slot].ip = clientIP;
        _loginStates[slot].failCount = 0;
        _loginStates[slot].lockoutUntil = 0;
    }


    /* CON-005a: String temporária destruída após safeCopy — sem heap residual. */
    safeCopy(_loginStates[slot].nonce, generateSecureToken().c_str(),
             sizeof(_loginStates[slot].nonce));
    _loginStates[slot].nonceCreatedAt = millis();
    _loginStates[slot].lastActivity = millis();

    uint32_t lockSec = 0;
    bool locked = false;
    /* Wrap-safe: millis() sofre wrap a cada ~49,7d; comparações diretas invertem. */
    if (_loginStates[slot].lockoutUntil > 0 && !timeReached(_loginStates[slot].lockoutUntil)) {
        lockSec = timeRemaining(_loginStates[slot].lockoutUntil) / 1000;
        locked = true;
    }

    char json[128];
    snprintf(json, sizeof(json), "{\"nonce\":\"%s\",\"locked\":%s,\"lockSec\":%lu}",
             _loginStates[slot].nonce, locked ? "true" : "false", (unsigned long)lockSec);

    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/json", json);
}

/* ===========================================================================
 * REF-007 / F17.4 — handleApiLogin decomposto
 * ===========================================================================
 * Orquestrador delega cada etapa a um helper privado nomeado. Cada helper
 * é responsável pelo seu efeito colateral (penaliza/responde) quando isso
 * mantém a etapa atômica; o orquestrador trata só o fluxo de saída antecipada.
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
    _loginStates[ls].lockoutUntil = millis() + penaltyMs;
    return penaltyMs;
}

bool WebManager::respondIfLockedOut(int ls, int httpCode) {
    if (ls < 0) return false;
    if (_loginStates[ls].lockoutUntil == 0) return false;
    if (timeReached(_loginStates[ls].lockoutUntil)) return false;
    uint32_t rem = timeRemaining(_loginStates[ls].lockoutUntil) / 1000;
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)rem);
    _server.send(httpCode, "application/json", buf);
    return true;
}

bool WebManager::validateNonceAndRespond(int ls) {
    /* CON-005a: expectedNonce é pointer pro buffer fixo do slot (ou string vazia
     * se não há slot). Comparação via operator==(String,const char*). */
    const char* expectedNonce = (ls >= 0) ? _loginStates[ls].nonce : "";
    bool nonceExpired = (ls >= 0) && (_loginStates[ls].nonceCreatedAt > 0) &&
                        timeSince(_loginStates[ls].nonceCreatedAt, NONCE_LIFETIME_MS);

    bool ok = _server.hasArg("nonce") &&
              _server.arg("nonce") == expectedNonce &&
              expectedNonce[0] != '\0' &&
              !nonceExpired;
    if (ok) {
        if (ls >= 0) _loginStates[ls].nonce[0] = '\0';
        return true;
    }

    /* falha: invalida nonce + penaliza nonce expirado (não nonce inválido) */
    if (ls >= 0) {
        _loginStates[ls].nonce[0] = '\0';
        if (nonceExpired) applyExponentialPenalty(ls);
    }
    LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0,
             nonceExpired ? "Login Rejected: Nonce Expired" : "Login Rejected: Invalid Nonce");
    if (!respondIfLockedOut(ls, 401)) {
        _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
    }
    return false;
}

int WebManager::verifyPasswordFor(const String& u, const String& p) {
    SystemConfig& cfg = _storageRef->getConfig();
    for (int i = 0; i < MAX_USERS; i++) {
        if (!cfg.users[i].active || String(cfg.users[i].username) != u) continue;

        String storedHash = String(cfg.users[i].password);
        bool passValid = false;
        bool needsMigration = false;

        /* *PENDING*: senha temporária time-based (primeiro login após criação/reset).
         * Ambos os lados computam fresh com o mesmo algoritmo — sem migração. */
        if (cfg.users[i].mustChangePassword && storedHash == "*PENDING*") {
            String expectedFrontendHash = getDynamicExpectedHash(u);
            String expectedFinalHash = _storageRef->hashPassword(u, expectedFrontendHash);
            String inputHash = _storageRef->hashPassword(u, p);
            if (secureCompare(inputHash, expectedFinalHash)) passValid = true;
        }
        /* Legado: hashVersion==0, 30 chars (120 bits), username-salt, 2500 rounds. */
        else if (cfg.users[i].hashVersion == 0 && storedHash.length() == 30) {
            String legacyHash = _storageRef->hashPasswordLegacy(u, p);
            if (secureCompare(storedHash, legacyHash)) {
                passValid = true;
                needsMigration = true;
            }
        }
        /* V1: hashVersion>=1, 32 chars (128 bits), salt random, PASSWORD_HMAC_ROUNDS. */
        else {
            String inputHash = _storageRef->hashPasswordV1(u, p, cfg.users[i].salt);
            if (secureCompare(storedHash, inputHash)) passValid = true;
        }

        if (passValid) {
            /* Migração transparente (SEC-007): re-hash com salt random + 32 chars. */
            if (needsMigration) {
                _storageRef->generateSalt(cfg.users[i].salt);
                String newHash = _storageRef->hashPasswordV1(u, p, cfg.users[i].salt);
                safeCopy(cfg.users[i].password, newHash.c_str(), sizeof(cfg.users[i].password));
                cfg.users[i].hashVersion = 1;
                _storageRef->saveConfiguration();
            }
            return i;
        }
    }
    return -1;
}

int WebManager::allocSessionSlot(int foundId) {
    clearStaleSessions();
    /* reaproveita slot do mesmo user (logins consecutivos do mesmo dispositivo) */
    for (int i = 0; i < 3; i++) {
        if (_activeSessions[i].token != "" && _activeSessions[i].userId == foundId) return i;
    }
    /* primeiro slot vazio */
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

    SystemConfig& cfg = _storageRef->getConfig();
    String newToken = generateSecureToken();

    _activeSessions[slot].token = newToken;
    _activeSessions[slot].userId = foundId;
    _activeSessions[slot].username = u;
    _activeSessions[slot].perms = cfg.users[foundId].permissions;
    _activeSessions[slot].lastActivity = millis();

    _currentUserId = foundId;
    _currentUserName = u;
    _currentUserPerms = _activeSessions[slot].perms;

    LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, foundId, String(TRL("Login OK: ")) + u);
    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);

    if (_displayRef) _displayRef->setWebNotification(u.c_str());

    String cookieFlags = "SIMUTSESS=" + newToken + "; Path=/; HttpOnly; SameSite=Strict";
    if (cfg.useHttps) cookieFlags += "; Secure";
    _server.sendHeader("Set-Cookie", cookieFlags);

    const char* redirect = cfg.users[foundId].mustChangePassword ? "/force_chpass" : "/";
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"redirect\":\"%s\"}", redirect);
    _server.send(200, "application/json", resp);
}

void WebManager::handleApiLogin() {
    uint32_t clientIP = (uint32_t)_server.client().remoteIP();
    int ls = findLoginStateForIp(clientIP);

    /* lockout ativo: 403 imediato (não consome nonce) */
    if (respondIfLockedOut(ls, 403)) return;

    /* nonce CSRF: valida + consome em sucesso, penaliza em expiração */
    if (!validateNonceAndRespond(ls)) return;

    if (!_server.hasArg("user") || !_server.hasArg("pass")) {
        _server.send(400, "application/json", "{\"ok\":false,\"err\":1}");
        return;
    }

    String u = _server.arg("user");
    String p = _server.arg("pass");

    /* D13: tamanhos sanos antes de invocar hashPassword (PASSWORD_HMAC_ROUNDS) */
    if (!isValidName(u.c_str(), 31) || p.length() > 128) {
        applyExponentialPenalty(ls);
        LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Login Rejected: Invalid Input Size"));
        _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
        return;
    }

    int foundId = verifyPasswordFor(u, p);
    if (foundId < 0) {
        if (ls >= 0) {
            uint32_t penaltyMs = applyExponentialPenalty(ls);
            LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, String(TRL("Login Failed: ")) + u);
            if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_ERROR);
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)(penaltyMs/1000));
            _server.send(401, "application/json", buf);
        } else {
            _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
        }
        return;
    }

    int slot = allocSessionSlot(foundId);
    if (slot < 0) {
        LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Login Rejected: Max Sessions Reached"));
        _server.send(403, "application/json", "{\"ok\":false,\"err\":3}");
        return;
    }

    completeLogin(slot, foundId, ls, u);
}

void WebManager::handleLogout() {
    if (_server.hasHeader("Cookie")) {
        String cookie = _server.header("Cookie");
        for (int i = 0; i < 3; i++) {
            if (_activeSessions[i].token != "" && cookie.indexOf("SIMUTSESS=" + _activeSessions[i].token) != -1) {
                LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, 0, String(TRL("Logout: ")) + _activeSessions[i].username);

                memset((void*)_activeSessions[i].token.begin(), 0, _activeSessions[i].token.length());
                _activeSessions[i].token = "";
                break;
            }
        }
    }

    _currentUserId = -1;
    _currentUserName = "";
    _currentUserPerms = 0;

    _server.sendHeader("Set-Cookie", "SIMUTSESS=0; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; SameSite=Strict");
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Location", "/login", true);
    _server.send(302, "text/plain", "");
}

void WebManager::handleApiSecStatus() {
    if (!(getAuthPerms() & PERM_USER_MGR)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }

    uint32_t now = millis();
    char buf[512];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"slots\":[");

    bool first = true;
    for (int i = 0; i < LOGIN_STATE_SLOTS; i++) {
        if (_loginStates[i].ip == 0) continue;
        if (!first) buf[pos++] = ',';
        first = false;

        uint32_t ip = _loginStates[i].ip;
        uint32_t lockSec = 0;
        bool locked = (_loginStates[i].lockoutUntil > 0 && !timeReached(_loginStates[i].lockoutUntil));
        if (locked) lockSec = timeRemaining(_loginStates[i].lockoutUntil) / 1000;
        uint32_t ageSec = (now - _loginStates[i].lastActivity) / 1000;

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"ip\":\"%lu.%lu.%lu.%lu\",\"fails\":%u,\"lockSec\":%lu,\"ageSec\":%lu}",
            (unsigned long)(ip & 0xFF), (unsigned long)((ip >> 8) & 0xFF),
            (unsigned long)((ip >> 16) & 0xFF), (unsigned long)((ip >> 24) & 0xFF),
            _loginStates[i].failCount, (unsigned long)lockSec, (unsigned long)ageSec);

        if (pos >= (int)sizeof(buf) - 2) break;
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/json", buf);
}

void WebManager::handleApiForceChpass() {
    if (getAuthPerms() == 0 || !isPasswordChangeRequired()) { _server.send(403, "text/plain", "Forbidden"); return; }
    if (rejectIfTouchPriority()) return;

    String p1 = _server.arg("p1");
    String p2 = _server.arg("p2");

    if (p1.length() < 8 || p1 != p2) {
        if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_ERROR);
        _server.send(400, "application/json", "{\"error\":\"Invalid payload\"}");
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();

    _storageRef->generateSalt(cfg.users[_currentUserId].salt);
    String hashedNewPass = _storageRef->hashPasswordV1(
        _currentUserName, p1, cfg.users[_currentUserId].salt);
    safeCopy(cfg.users[_currentUserId].password, hashedNewPass.c_str(), sizeof(cfg.users[_currentUserId].password));
    cfg.users[_currentUserId].hashVersion = 1;
    cfg.users[_currentUserId].mustChangePassword = false;
    _storageRef->saveConfiguration();

    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, _currentUserId, String(TRL("Password Reset Success: ")) + _currentUserName);

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

/* Self-service password change pre-auth (acessivel da tela de login).
 * Reaproveita lockout/nonce/verifyPasswordFor do fluxo de login normal.
 * Nao cria sessao — usuario faz login fresh com a senha nova. */
void WebManager::handleApiLoginChpass() {
    uint32_t clientIP = (uint32_t)_server.client().remoteIP();
    int ls = findLoginStateForIp(clientIP);

    if (respondIfLockedOut(ls, 403)) return;
    if (!validateNonceAndRespond(ls)) return;

    if (!_server.hasArg("user") || !_server.hasArg("oldpass") || !_server.hasArg("newpass")) {
        _server.send(400, "application/json", "{\"ok\":false,\"err\":1}");
        return;
    }

    String u  = _server.arg("user");
    String op = _server.arg("oldpass");
    String np = _server.arg("newpass");

    /* Cliente envia sha256 (64 hex chars). Sanity de tamanho/forma. */
    if (!isValidName(u.c_str(), 31) || op.length() != 64 || np.length() != 64) {
        applyExponentialPenalty(ls);
        LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, TRL("Chpass Rejected: Invalid Input"));
        _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
        return;
    }

    /* Bloqueia no-op (mesmo hash). Strength real e validada no client (UX);
     * server confia que cliente honesto cumpre regra de complexidade. */
    if (np == op) {
        _server.send(400, "application/json", "{\"ok\":false,\"err\":5}");
        return;
    }

    int foundId = verifyPasswordFor(u, op);
    if (foundId < 0) {
        if (ls >= 0) {
            uint32_t penaltyMs = applyExponentialPenalty(ls);
            LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, String(TRL("Chpass Failed: ")) + u);
            if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_ERROR);
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":2,\"lockSec\":%lu}", (unsigned long)(penaltyMs/1000));
            _server.send(401, "application/json", buf);
        } else {
            _server.send(401, "application/json", "{\"ok\":false,\"err\":1}");
        }
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();
    _storageRef->generateSalt(cfg.users[foundId].salt);
    String hashedNewPass = _storageRef->hashPasswordV1(u, np, cfg.users[foundId].salt);
    safeCopy(cfg.users[foundId].password, hashedNewPass.c_str(), sizeof(cfg.users[foundId].password));
    cfg.users[foundId].hashVersion = 1;
    cfg.users[foundId].mustChangePassword = false;
    _storageRef->saveConfiguration();

    if (ls >= 0) { _loginStates[ls].failCount = 0; _loginStates[ls].lockoutUntil = 0; }

    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, foundId, String(TRL("Login Chpass OK: ")) + u);

    _server.send(200, "application/json", "{\"ok\":true}");
}
