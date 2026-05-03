/**
 * @file    WebManager_Commit.cpp
 * @brief   Commit-all handler: batched config save + reboot, theme switch, touch cal reset.
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
#include <LittleFS.h>
#include <time.h>
#include <hardware/watchdog.h>

/* Extrai valor de string JSON "key":"..." com handling correto de escapes.
 *
 * Anterior (bug): indexOf('"', vStart) retornava na PRIMEIRA aspa após a
 * abertura, mas JSON.stringify no frontend emite \" para aspas internas
 * dos templates de payload (`{"dev":"{DEV}"}` → `"{\"dev\":\"{DEV}\"}"`).
 * Resultado: o valor extraído era truncado em `{\` — templates salvos como
 * 2 chars de lixo. Sintoma reportado: "construtor de payload não salva".
 *
 * Fix: anda char a char a partir do `"` de abertura; consome sequências
 * \" \\ \/ \n \t \r \b \f corretamente; para na PRIMEIRA aspa NÃO escapada.
 * Escapes desconhecidos (\x) são preservados lenientemente. */
static String jsonExtractStringValue(const String& src, const char* key) {
    String pat = String("\"") + key + "\":\"";
    int p = src.indexOf(pat);
    if (p < 0) return String();
    int i = p + pat.length();
    const int n = src.length();
    String out;
    while (i < n) {
        char c = src.charAt(i);
        if (c == '\\' && i + 1 < n) {
            char e = src.charAt(i + 1);
            switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                default:   out += c; out += e; break;   /* lenient */
            }
            i += 2;
            continue;
        }
        if (c == '"') return out;
        out += c;
        i++;
    }
    return String();   /* unterminated — drop silenciosamente */
}



using ReadGuard = StorageManager::ReadGuard;


/*
 * handleSaveSystem — versao minimal pos-U24.
 * Versao pre-U24 foi substituida por handleApiCommitAll. Mantida apenas
 * pra dashboard trocar tema (aplicacao imediata, sem reboot).
 */
void WebManager::handleSaveSystem() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "text/plain", "Forbidden"); return; }
    SystemConfig& cfg = _storageRef->getConfig();
    if (_server.hasArg("theme")) {
        int t = _server.arg("theme").toInt();
        if (t >= 0 && t < getThemeCount() && cfg.themeIndex != t) {
            cfg.themeIndex = t;
            loadTheme(t);
            _storageRef->saveConfiguration();
            if (_displayRef) _displayRef->refreshTheme();
        }
    }
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}


/*
 * U24 — Commit-all + reboot (save-and-restart pattern).
 *
 * Recebe POST com `_payload` = JSON contendo seções `sys` e/ou `alarms`.
 * Aplica todas as mudanças em memória, faz um único saveConfiguration(),
 * responde ao cliente, e agenda reboot. A rajada de saves concorrentes que
 * triggerava deadlocks de lockout em multicore deixa de existir — a escrita
 * acontece 1x e o sistema reinicia limpo.
 *
 * Cliente envia `_payload` urlencoded. Exemplo:
 *   _payload={"sys":{"name":"SIMUT","tz":"-3","log":"1",...}}
 *
 * Phase A.1: suporta apenas a seção `sys`. Extensão para `alarms` vem a
 * seguir (mesma abordagem de parsing usada em handleApiSaveAlarms).
 */
void WebManager::handleApiCommitAll() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }
    if (isPasswordChangeRequired()) return;
    if (rejectIfTouchPriority()) return;

    if (!_server.hasArg("_payload")) {
        _server.send(400, "application/json", "{\"error\":\"Missing _payload\"}");
        return;
    }

    String body = _server.arg("_payload");
    if (body.length() == 0 || body.length() > 6144) {
        _server.send(400, "application/json", "{\"error\":\"Bad payload\"}");
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();
    bool themeChanged = false;

    /* ── Seção sys: extrai o sub-objeto e aplica cada campo ───────────────
     * Parser manual por simplicidade. Formato esperado:
     *   "sys":{"name":"...","tz":"-3","log":"1",...}
     * Cada campo pode vir como string entre aspas ou número bruto. */
    int sysStart = body.indexOf("\"sys\"");
    if (sysStart >= 0) {
        int objStart = body.indexOf('{', sysStart);
        int objEnd = -1;
        if (objStart >= 0) {
            /* Busca o '}' pareado levando em conta nesting. */
            int depth = 0;
            for (int i = objStart; i < (int)body.length(); i++) {
                char c = body.charAt(i);
                if (c == '{') depth++;
                else if (c == '}') { depth--; if (depth == 0) { objEnd = i; break; } }
            }
        }
        if (objStart >= 0 && objEnd > objStart) {
            String sys = body.substring(objStart, objEnd + 1);

            /* Helper: extrai valor string "key":"..." com unescape correto
             * de \" \\ \/ \n \t \r \b \f (via jsonExtractStringValue). */
            auto getStr = [&](const char* key) -> String {
                return jsonExtractStringValue(sys, key);
            };
            /* Helper: extrai número bruto (não quoted) de "key":NNN.
             * Usado quando cliente envia int/float sem aspas. */
            auto getNum = [&](const char* key) -> String {
                String pat = String("\"") + key + "\":";
                int p = sys.indexOf(pat);
                if (p < 0) return String();
                int vStart = p + pat.length();
                /* Pula aspas se houver */
                if (sys.charAt(vStart) == '"') {
                    int vEnd = sys.indexOf('"', vStart + 1);
                    if (vEnd < 0) return String();
                    return sys.substring(vStart + 1, vEnd);
                }
                /* Lê até vírgula ou } */
                int vEnd = vStart;
                while (vEnd < (int)sys.length() && sys.charAt(vEnd) != ',' && sys.charAt(vEnd) != '}') vEnd++;
                return sys.substring(vStart, vEnd);
            };
            auto has = [&](const char* key) -> bool {
                String pat = String("\"") + key + "\":";
                return sys.indexOf(pat) >= 0;
            };

            /* Aplica cada campo (mirror of handleSaveSystem) */
            if (has("name")) {
                String n = getStr("name"); n.trim();
                if (n.length() > 0 && isValidName(n.c_str())) {
                    safeCopy(cfg.deviceName, n.c_str(), sizeof(cfg.deviceName));
                }
            }
            if (has("tz")) {
                cfg.timezoneOffset = (int8_t)getNum("tz").toInt();
                NetworkManager::applyTimezone(cfg.timezoneOffset);
            }
            if (has("log"))       cfg.loggingEnabled = (getNum("log") != "0");
            if (has("t_sec"))     cfg.telEncryption = (getNum("t_sec") != "0");
            if (has("t_key"))     safeCopy(cfg.telApiKey, getStr("t_key").c_str(), sizeof(cfg.telApiKey));
            if (has("res"))       { int r = getNum("res").toInt(); if (r >= 9 && r <= 12) cfg.ds18Resolution = (uint8_t)r; }
            if (has("s_int"))     cfg.sampleIntervalMs = getNum("s_int").toInt();
            if (has("t_srv"))     safeCopy(cfg.telServer, getStr("t_srv").c_str(), sizeof(cfg.telServer));
            if (has("t_port"))    { int p = getNum("t_port").toInt(); if (isInRange(p, 1, 65535)) cfg.telPort = (uint16_t)p; }
            if (has("t_path"))    safeCopy(cfg.telPath, getStr("t_path").c_str(), sizeof(cfg.telPath));
            if (has("t_int"))     cfg.telInterval = getNum("t_int").toInt();
            if (has("t_bat"))     cfg.telBatchSize = (uint8_t)getNum("t_bat").toInt();
            if (has("t_mode"))    cfg.telMode = (uint8_t)getNum("t_mode").toInt();
            if (has("t_transport")) cfg.telTransport = (uint8_t)getNum("t_transport").toInt();
            if (has("m_topic"))   safeCopy(cfg.mqttTopic, getStr("m_topic").c_str(), sizeof(cfg.mqttTopic));
            if (has("m_cid"))     safeCopy(cfg.mqttClientId, getStr("m_cid").c_str(), sizeof(cfg.mqttClientId));
            if (has("m_user"))    safeCopy(cfg.mqttUser, getStr("m_user").c_str(), sizeof(cfg.mqttUser));
            if (has("m_qos"))     cfg.mqttQos = (uint8_t)getNum("m_qos").toInt();
            if (has("m_retain"))  cfg.mqttRetain = (getNum("m_retain") != "0");
            if (has("m_ka"))      { int ka = getNum("m_ka").toInt(); if (isInRange(ka, 5, 600)) cfg.mqttKeepAlive = (uint16_t)ka; }
            if (has("t_glob"))    safeCopy(cfg.telGlobalTemplate, getStr("t_glob").c_str(), sizeof(cfg.telGlobalTemplate));
            if (has("t_line"))    safeCopy(cfg.telLineTemplate, getStr("t_line").c_str(), sizeof(cfg.telLineTemplate));
            if (has("t_sep"))     safeCopy(cfg.telLineSeparator, getStr("t_sep").c_str(), sizeof(cfg.telLineSeparator));
            /* F-NET-TIME.3a: flag NTP enable/disable (overlay NetworkTimeData). */
            if (has("ntp_enabled")) _storageRef->setNtpEnabled(getNum("ntp_enabled") != "0");
            if (has("h_int"))      { int hi = getNum("h_int").toInt(); if (isInRange(hi, 1, 1440)) _storageRef->setHistoryIntervalMin((uint16_t)hi); }
        }
    }

    /* ── Seção alarms: formato {sensors:[{idx,active,tmin,tmax,hmin,hmax}],
     * sounds:{...}}. Mesmo parsing manual usado em handleApiSaveAlarms. */
    int almStart = body.indexOf("\"alarms\"");
    if (almStart >= 0) {
        /* Sensors array */
        int sensorsStart = body.indexOf("\"sensors\"", almStart);
        int arrStart = (sensorsStart >= 0) ? body.indexOf('[', sensorsStart) : -1;
        int arrEnd = (arrStart >= 0) ? body.indexOf(']', arrStart) : -1;
        if (arrStart >= 0 && arrEnd > arrStart) {
            String arrStr = body.substring(arrStart, arrEnd + 1);
            int objStart = 0;
            while ((objStart = arrStr.indexOf('{', objStart)) >= 0) {
                int objEnd = arrStr.indexOf('}', objStart);
                if (objEnd < 0) break;
                String obj = arrStr.substring(objStart, objEnd + 1);

                int idxPos = obj.indexOf("\"idx\"");
                if (idxPos < 0) { objStart = objEnd + 1; continue; }
                int idxColon = obj.indexOf(':', idxPos);
                int idx = obj.substring(idxColon + 1).toInt();

                SensorRecord* rec = nullptr;
                if (idx == -1) rec = &cfg.ambientSensor;
                else if (idx >= 0 && idx < MAX_SENSORS && cfg.sensors[idx].active) rec = &cfg.sensors[idx];

                if (rec) {
                    auto extractFloat = [&](const char* key) -> float {
                        int kp = obj.indexOf(key);
                        if (kp < 0) return NAN;
                        int cp = obj.indexOf(':', kp + strlen(key));
                        if (cp < 0) return NAN;
                        return obj.substring(cp + 1).toFloat();
                    };
                    float tmin = extractFloat("\"tmin\"");
                    float tmax = extractFloat("\"tmax\"");
                    float hmin = extractFloat("\"hmin\"");
                    float hmax = extractFloat("\"hmax\"");
                    if (!isnan(tmin)) rec->tempMin = tmin;
                    if (!isnan(tmax)) rec->tempMax = tmax;
                    if (!isnan(hmin)) rec->humMin  = hmin;
                    if (!isnan(hmax)) rec->humMax  = hmax;
                    if (rec->tempMin >= rec->tempMax) {
                        rec->tempMax = roundf((rec->tempMin + 0.1f) * 10.0f) / 10.0f;
                    }
                    if (rec->humMin >= rec->humMax) {
                        rec->humMax = roundf((rec->humMin + 0.1f) * 10.0f) / 10.0f;
                        if (rec->humMax > 100.0f) { rec->humMax = 100.0f; rec->humMin = 99.9f; }
                    }
                    rec->alarmsActive = (obj.indexOf("\"active\":true") >= 0);
                }
                objStart = objEnd + 1;
            }
        }

        /* Sounds: mesmo parsing de handleApiSaveAlarms, aplica via
         * SoundSettingsState + fillConfig pra escrever no packed layout. */
        int soundsStart = body.indexOf("\"sounds\"", almStart);
        if (soundsStart >= 0) {
            int sObjStart = body.indexOf('{', soundsStart);
            int sObjEnd   = body.indexOf('}', sObjStart);
            if (sObjStart >= 0 && sObjEnd > sObjStart) {
                String sObj = body.substring(sObjStart, sObjEnd + 1);
                SoundSettingsState snd;
                snd.touchEnabled   = (sObj.indexOf("\"touch\":true")   >= 0);
                snd.confirmEnabled = (sObj.indexOf("\"confirm\":true") >= 0);
                snd.errorEnabled   = (sObj.indexOf("\"error\":true")   >= 0);
                snd.alarmEnabled   = (sObj.indexOf("\"alarm\":true")   >= 0);
                snd.webEnabled     = (sObj.indexOf("\"web\":true")     >= 0);
                snd.muted          = (sObj.indexOf("\"mute\":true")    >= 0);

                int volPos = sObj.indexOf("\"volume\"");
                if (volPos >= 0) { int vc = sObj.indexOf(':', volPos); snd.volume = (uint8_t)constrain(sObj.substring(vc + 1).toInt(), 0, 100); }
                else snd.volume = 70;

                int aVolPos = sObj.indexOf("\"alarmVolume\"");
                if (aVolPos >= 0) { int avc = sObj.indexOf(':', aVolPos); snd.alarmVolume = (uint8_t)constrain(sObj.substring(avc + 1).toInt(), 0, 100); }
                else snd.alarmVolume = 70;

                auto extractMelIdx = [&](const char* key) -> uint8_t {
                    int kp = sObj.indexOf(key);
                    if (kp < 0) return 0;
                    int cp = sObj.indexOf(':', kp);
                    if (cp < 0) return 0;
                    return (uint8_t)constrain(sObj.substring(cp + 1).toInt(), 0, 5);
                };
                snd.touchMelody   = extractMelIdx("\"melTouch\"");
                snd.confirmMelody = extractMelIdx("\"melConfirm\"");
                snd.errorMelody   = extractMelIdx("\"melError\"");
                snd.alarmMelody   = extractMelIdx("\"melAlarm\"");

                if (_soundRef) {
                    _soundRef->applySettingsState(snd);
                    SoundConfigData* sndCfg = reinterpret_cast<SoundConfigData*>(
                        cfg.reserved + sizeof(TouchCalData));
                    _soundRef->fillConfig(sndCfg);
                }
            }
        }
    }

    /* ── Seção users.actions: processa add/del/reset em ordem ───────────
     * Formato: {"users":{"actions":[{"type":"add","name":"x","perms":511},
     *                                 {"type":"del","id":3},
     *                                 {"type":"reset","id":5}]}} */
    int usrStart = body.indexOf("\"users\"");
    if (usrStart >= 0) {
        int actionsPos = body.indexOf("\"actions\"", usrStart);
        int arrStart = (actionsPos >= 0) ? body.indexOf('[', actionsPos) : -1;
        int arrEnd = (arrStart >= 0) ? body.indexOf(']', arrStart) : -1;
        if (arrStart >= 0 && arrEnd > arrStart) {
            String arr = body.substring(arrStart, arrEnd + 1);
            int objStart = 0;
            while ((objStart = arr.indexOf('{', objStart)) >= 0) {
                int objEnd = arr.indexOf('}', objStart);
                if (objEnd < 0) break;
                String obj = arr.substring(objStart, objEnd + 1);
                String type;
                int tp = obj.indexOf("\"type\":\"");
                if (tp >= 0) {
                    int vs = tp + 8;
                    int ve = obj.indexOf('"', vs);
                    if (ve > vs) type = obj.substring(vs, ve);
                }

                if (type == "add") {
                    /* find first inactive slot (skip slot 0 — admin) */
                    int slot = -1;
                    for (int i = 1; i < MAX_USERS; i++) {
                        if (!cfg.users[i].active) { slot = i; break; }
                    }
                    if (slot < 0) { objStart = objEnd + 1; continue; }

                    /* name */
                    String name;
                    int np = obj.indexOf("\"name\":\"");
                    if (np >= 0) {
                        int vs = np + 8;
                        int ve = obj.indexOf('"', vs);
                        if (ve > vs) name = obj.substring(vs, ve);
                    }
                    name.trim();
                    if (name.length() == 0 || !isValidName(name.c_str(), 15) || name.equalsIgnoreCase("admin")) {
                        objStart = objEnd + 1; continue;
                    }
                    /* dup check */
                    bool dup = false;
                    for (int i = 0; i < MAX_USERS; i++) {
                        if (cfg.users[i].active && name.equalsIgnoreCase(String(cfg.users[i].username))) { dup = true; break; }
                    }
                    if (dup) { objStart = objEnd + 1; continue; }

                    int perms = 0;
                    int pp = obj.indexOf("\"perms\":");
                    if (pp >= 0) perms = obj.substring(pp + 8).toInt();

                    safeCopy(cfg.users[slot].username, name.c_str(), sizeof(cfg.users[slot].username));
                    safeCopy(cfg.users[slot].password, "*PENDING*", sizeof(cfg.users[slot].password));
                    cfg.users[slot].permissions = (uint16_t)perms;
                    cfg.users[slot].mustChangePassword = true;
                    cfg.users[slot].active = true;
                }
                else if (type == "del" || type == "reset") {
                    int ip = obj.indexOf("\"id\":");
                    int id = (ip >= 0) ? obj.substring(ip + 5).toInt() : -1;
                    if (id > 0 && id < MAX_USERS && cfg.users[id].active) {
                        if (type == "del") {
                            cfg.users[id].active = false;
                            memset(cfg.users[id].username, 0, sizeof(cfg.users[id].username));
                            memset(cfg.users[id].password, 0, sizeof(cfg.users[id].password));
                            cfg.users[id].permissions = 0;
                        } else { /* reset */
                            safeCopy(cfg.users[id].password, "*PENDING*", sizeof(cfg.users[id].password));
                            cfg.users[id].mustChangePassword = true;
                        }
                    }
                }
                objStart = objEnd + 1;
            }
        }
    }

    /* ── Seção net: {ssid,pass,use_dhcp,ip,mask,gw,dns,ntp_server,web_port} ─ */
    uint16_t commitNewPort = 0;  /* 0 = sem mudança; != 0 = informa cliente */
    int netStart = body.indexOf("\"net\"");
    if (netStart >= 0) {
        int objStart = body.indexOf('{', netStart);
        int objEnd = -1;
        if (objStart >= 0) {
            int depth = 0;
            for (int i = objStart; i < (int)body.length(); i++) {
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
                String pat = String("\"") + key + "\":";
                int p = net.indexOf(pat);
                if (p < 0) return String();
                int vs = p + pat.length();
                if (net.charAt(vs) == '"') {
                    int ve = net.indexOf('"', vs + 1);
                    if (ve < 0) return String();
                    return net.substring(vs + 1, ve);
                }
                int ve = vs;
                while (ve < (int)net.length() && net.charAt(ve) != ',' && net.charAt(ve) != '}') ve++;
                return net.substring(vs, ve);
            };
            auto has = [&](const char* key) -> bool {
                return net.indexOf(String("\"") + key + "\":") >= 0;
            };

            if (has("ssid")) { String s = getS("ssid"); s.trim(); if (s.length() > 0) safeCopy(cfg.wifiSsid, s.c_str(), sizeof(cfg.wifiSsid)); }
            if (has("pass")) { String p = getS("pass"); p.trim(); if (p.length() > 0) safeCopy(cfg.wifiPass, p.c_str(), sizeof(cfg.wifiPass)); }
            if (has("use_dhcp")) cfg.useDhcp = (getN("use_dhcp") != "0");
            if (!cfg.useDhcp) {
                if (has("ip"))   { String s = getS("ip");   if (isValidIpv4(s.c_str())) safeCopy(cfg.staticIp, s.c_str(), sizeof(cfg.staticIp)); }
                if (has("mask")) { String s = getS("mask"); if (isValidIpv4(s.c_str())) safeCopy(cfg.staticMask, s.c_str(), sizeof(cfg.staticMask)); }
                if (has("gw"))   { String s = getS("gw");   if (isValidIpv4(s.c_str())) safeCopy(cfg.staticGateway, s.c_str(), sizeof(cfg.staticGateway)); }
                if (has("dns"))  { String s = getS("dns");  if (isValidIpv4(s.c_str())) safeCopy(cfg.staticDns, s.c_str(), sizeof(cfg.staticDns)); }
            }
            /* F-NET-TIME.3a: dns_auto + dns2 (primário manual reusa cfg.staticDns).
             * Aceita também `dns1` como atalho para `staticDns` quando user está
             * no modo manual com DHCP=true (sem os outros campos staticIp/mask/gw). */
            if (has("dns_auto")) _storageRef->setDnsAuto(getN("dns_auto") != "0");
            if (has("dns1")) { String s = getS("dns1"); if (isValidIpv4(s.c_str())) safeCopy(cfg.staticDns, s.c_str(), sizeof(cfg.staticDns)); }
            if (has("dns2")) {
                String s = getS("dns2"); s.trim();
                /* Vazio é válido (limpa secundário). Qualquer outro valor exige ser IPv4. */
                if (s.length() == 0 || isValidIpv4(s.c_str())) _storageRef->setSecondaryDns(s.c_str());
            }
            if (has("ntp_server")) {
                String ntp = getS("ntp_server"); ntp.trim();
                safeCopy(cfg.ntpServer, ntp.c_str(), sizeof(cfg.ntpServer));
                cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
            }
            if (has("web_port")) {
                WebConfigData* w = reinterpret_cast<WebConfigData*>(cfg.reserved + WEB_CONFIG_OFFSET);
                int p = getN("web_port").toInt();
                if (p >= 1 && p <= 65535) {
                    if (w->port != (uint16_t)p) commitNewPort = (uint16_t)p;
                    w->port = (uint16_t)p;
                }
            }
        }
    }

    if (themeChanged && _displayRef) _displayRef->refreshTheme();

    /* U24: mostra mensagem no display ANTES de qualquer flash I/O.
     * Core 1 vai pra tela de boot e renderiza, dando feedback visual
     * ao usuário de que o reinício está iminente.
     *
     * Tempos: ~1.5s em cada mensagem pra user ler com calma antes do
     * reboot. Alimentamos WDT em chunks pra não disparar durante o hold. */
    if (_displayRef) {
        _displayRef->setBootStatusKey(TR_BOOT_APPLYING_CFG);
        for (int i = 0; i < 15; i++) { delay(100); watchdog_update(); }
        _displayRef->setBootStatusKey(TR_BOOT_REBOOTING);
        for (int i = 0; i < 15; i++) { delay(100); watchdog_update(); }
    }

    /* Bufferiza o audit log — heavy task gate faz writeCompactToFlash
     * empilhar em _pendingLogs em vez de gravar direto. Evita um flash
     * write extra antes do save; saveConfiguration drena o buffer. */
    _storageRef->lockHeavyTask();
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId,
             TRL("Admin committed changes — rebooting"));
    _storageRef->unlockHeavyTask();

    /* Single atomic save — drena pending logs + escreve config */
    bool saved = _storageRef->saveConfiguration();

    if (!saved) {
        /* Save falhou: desfaz o boot screen e devolve erro ao cliente.
         * Sistema continua vivo, user pode tentar novamente. */
        if (_displayRef) _displayRef->endBoot();
        _server.send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }

    /* Resposta ao cliente antes do reboot. Inclui newPort se porta web
     * mudou — cliente usa pra redirecionar ao novo host:porta. */
    char resp[64];
    if (commitNewPort != 0) {
        snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"newPort\":%u}", (unsigned)commitNewPort);
    } else {
        snprintf(resp, sizeof(resp), "{\"status\":\"ok\"}");
    }
    _server.send(200, "application/json", resp);
    _server.client().stop();

    /*
     * Hard reboot via LogManager::safeReboot() — sequência consolidada:
     *   - markCleanReboot: autópsia NÃO reporta como HW_WATCHDOG (intencional)
     *   - Serial.flush + Serial.end: USB CDC desconecta limpo no host (resolve
     *     /dev/ttyACM0 não reaparecer após reboot — F-USB-REBOOT alpha21)
     *   - watchdog_enable(500, 1): 500ms WDT garante reset mesmo se algo
     *     travar (lockout, multicore, flash GC). Elimina o espaço onde U21
     *     (display congelado) ocorria entre save completo e o reset.
     */
    LogManager::instance().safeReboot();
}

/* handleSaveNetwork removido em U24 Phase C — substituido por handleApiCommitAll. */

/**
 * @brief Reseta calibração do touch via API web.
 *
 * Limpa TouchCalData no config (invalida magic), reseta parâmetros
 * no DisplayManager e salva no flash.
 */
/**
 * @brief F-NET-TIME.3a: POST /api/set_time — set manual RTC imediato.
 *
 * Body JSON: {"epoch": <uint32_t UTC seconds>}
 * Resposta: {"ok":true,"now":<uint32_t epoch now>} ou {"error":"..."}.
 *
 * NÃO passa pelo commit-all nem exige reboot — aplica agora via
 * NetworkManager::setManualTime(). Útil quando ntp_enabled=false;
 * com ntp_enabled=true, o próximo sync NTP sobrescreve.
 * Permissão: PERM_SYS_CONFIG.
 */
void WebManager::handleApiSetTime() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    String body = _server.arg("plain");
    int p = body.indexOf("\"epoch\"");
    if (p < 0) { _server.send(400, "application/json", "{\"error\":\"missing epoch\"}"); return; }
    int vs = body.indexOf(':', p);
    if (vs < 0) { _server.send(400, "application/json", "{\"error\":\"bad format\"}"); return; }
    vs++;
    while (vs < (int)body.length() && (body.charAt(vs) == ' ' || body.charAt(vs) == '"')) vs++;
    int ve = vs;
    while (ve < (int)body.length() && isDigit(body.charAt(ve))) ve++;
    if (ve == vs) { _server.send(400, "application/json", "{\"error\":\"bad epoch\"}"); return; }
    uint32_t epoch = (uint32_t)body.substring(vs, ve).toInt();
    if (epoch <= 1600000000UL) { _server.send(400, "application/json", "{\"error\":\"epoch too low\"}"); return; }

    _netRef->setManualTime((time_t)epoch);

    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":true,\"now\":%lu}", (unsigned long)time(nullptr));
    _server.send(200, "application/json", json);
}

void WebManager::handleResetTouchCal() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }
    if (rejectIfTouchPriority()) return;

    SystemConfig& cfg = _storageRef->getConfig();
    TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
    memset(cal, 0, sizeof(TouchCalData));
    _displayRef->resetTouchCalibration();
    _storageRef->saveConfiguration();

    if (_soundRef->isWebSoundsEnabled()) _soundRef->play(SND_CONFIRM);
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Touch calibration reset via web"));

    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}
