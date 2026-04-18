/**
 * @file    NetworkManager.cpp
 * @brief   Implementation of NetworkManager — WiFi state machine, NTP, and time utilities.
 * @details Implements a multi-state WiFi connection manager with async scan,
 * exponential backoff, non-blocking disconnect, NTP sync with
 * provisional time correction callback, and timezone-aware
 * date/time formatting.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.8.0
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "NetworkManager.h"
#include <stdlib.h>

NetworkManager::NetworkManager() {
    _state = NET_OFFLINE;
    _stateTimer = 0;
    _reconnectTimer = 0;
}

/**
 * @brief Initialize WiFi in STA mode with optional static IP.
 * Starts the connection process; actual connection completes in update().
 */
void NetworkManager::begin(const SystemConfig &cfg) {
    safeCopy(_ssid, cfg.wifiSsid, sizeof(_ssid));
    safeCopy(_pass, cfg.wifiPass, sizeof(_pass));
    safeCopy(_deviceName, cfg.deviceName, sizeof(_deviceName));
    _tzOffset = cfg.timezoneOffset;

    /* Servidor NTP configurável (fallback para pool.ntp.org se vazio) */
    if (cfg.ntpServer[0] != '\0') {
        safeCopy(_ntpServer, cfg.ntpServer, sizeof(_ntpServer));
        _ntpServer[sizeof(_ntpServer) - 1] = '\0';
    } else {
        safeCopy(_ntpServer, "pool.ntp.org", sizeof(_ntpServer));
    }

    /* Aplica fuso horário globalmente para que localtime_r funcione */
    applyTimezone(_tzOffset);

    WiFi.mode(WIFI_STA);

    if (!cfg.useDhcp && strlen(cfg.staticIp) > 7) {
        IPAddress ip, gw, mask, dns;
        ip.fromString(cfg.staticIp); mask.fromString(cfg.staticMask);
        gw.fromString(cfg.staticGateway); dns.fromString(cfg.staticDns);
        WiFi.config(ip, gw, mask, dns);
        LOG_CODE(LOG_INFO, "NET", NET_STATIC_MODE, 0, "");
    } else {
        WiFi.config(IPAddress(), IPAddress(), IPAddress(), IPAddress());
        LOG_CODE(LOG_INFO, "NET", NET_DHCP_MODE, 0, "");
    }

    if (strlen(_ssid) == 0) {
        LOG_CODE(LOG_WARN, "NET", NET_SSID_MISSING, 0, "");
        _state = NET_OFFLINE;
    } else {
        LOG_CODE(LOG_INFO, "NET", NET_STARTING, 0, "");
        WiFi.begin(_ssid, _pass);
        _state = NET_CONNECTING;
        _stateTimer = millis();
    }
}

/**
 * @brief Start WiFi in Access Point mode for initial configuration.
 * Creates a captive portal on 192.168.4.1 with a DNS redirect.
 */
void NetworkManager::beginAP(const char* deviceName) {
    _state = NET_AP_CONFIG;
    _apStartTime = millis();
    WiFi.mode(WIFI_AP);
    IPAddress apIP(192, 168, 4, 1); IPAddress gateway(192, 168, 4, 1); IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);
    String apName = String(deviceName) + "_SETUP";
    WiFi.softAP(apName.c_str());
    _dnsServer.start(53, "*", apIP);
    LOG_CODE(LOG_INFO, "NET", SYS_AP_START, 0, "Access Point: " + apName);
}


/**
 * @brief Set provisional time from the last Flash-stored timestamp.
 * Provides approximate timestamps until NTP sync completes (Virtual RTC).
 */
void NetworkManager::setProvisionalTime(uint32_t lastTs) {
    if (lastTs > 1600000000) {
        _provisionalBase = lastTs + 60;
        _provisionalBootMillis = millis();
        _provisionalActive = true;
        LOG_CODE(LOG_INFO, "NET", NET_PROVISIONAL_TIME, 0, "Provisional: " + getFormattedDate() + " " + getFormattedTime());
    }
}

void NetworkManager::setTimeSyncCallback(TimeSyncCallback cb) { _timeSyncCb = cb; }

/**
 * @brief Network state machine — handles all connection states.
 * Must be called frequently from the main loop.
 */
void NetworkManager::update() {
    if (_state == NET_AP_CONFIG) {
        _dnsServer.processNextRequest();
        /* N5: timeout AP mode — reboot para STA se SSID configurado */
        if (millis() - _apStartTime > AP_MODE_TIMEOUT_MS && strlen(_ssid) > 0) {
            LOG_CODE(LOG_WARN, "NET", NET_CONNECT_TIMEOUT, 0, "AP mode timeout, rebooting to STA");
            watchdog_update();
            rp2040.reboot();
        }
        return;
    }

    /* mDNS: atualiza apenas quando conectado e com throttle de 2s */
    if (_state == NET_READY) {
        uint32_t now = millis();
        if (now - _lastMdnsUpdate >= MDNS_UPDATE_INTERVAL_MS) {
            _lastMdnsUpdate = now;
            MDNS.update();
        }
    }

    switch (_state) {
        case NET_OFFLINE:

            if (millis() - _reconnectTimer > _reconnectDelay) {
                if (strlen(_ssid) > 0) {
                    LOG_CODE(LOG_INFO, "NET", SYS_WIFI_SCAN, 0, "Scanning for SSID (backoff=" + String(_reconnectDelay/1000) + "s)");
                    WiFi.scanNetworks(true);
                    _state = NET_SCANNING_RETRY;
                } else { _reconnectTimer = millis(); }
            }
            break;

        case NET_SCANNING_RETRY: {
            int n = WiFi.scanComplete();
            if (n == -1) return;
            if (n < -1) { _state = NET_OFFLINE; _reconnectTimer = millis(); return; }

            bool found = false;
            for (int i = 0; i < n; i++) {
                if (strcmp(WiFi.SSID(i), _ssid) == 0) { found = true; break; }
            }
            WiFi.scanDelete();

            if (found) {
                LOG_CODE(LOG_INFO, "NET", SYS_WIFI_CONNECT, 0, "SSID found, connecting...");
                WiFi.begin(_ssid, _pass);
                _state = NET_CONNECTING; _stateTimer = millis();
            } else { _state = NET_OFFLINE; _reconnectTimer = millis(); }
            break;
        }

        case NET_CONNECTING: handleConnecting(); break;

        case NET_CONNECTED_WAIT_IP:
            if (WiFi.localIP().toString() != "0.0.0.0") {
                LOG_CODE(LOG_INFO, "NET", SYS_IP_ACQUIRED, 0, "IP: " + WiFi.localIP().toString());
                if (!MDNS.begin(_deviceName)) LOG_CODE(LOG_ERROR, "NET", SYS_OK, 0, "mDNS failed to start");
                syncNtp();
                _state = NET_CONNECTED_WAIT_NTP; _stateTimer = millis();
            }
            break;

        case NET_CONNECTED_WAIT_NTP:
            if (time(nullptr) > 1600000000) {
                LOG_CODE(LOG_INFO, "NET", SYS_NTP_SYNC, 0, "NTP OK: " + getFormattedDate() + " " + getFormattedTime());


                if (_provisionalActive) {
                    uint32_t realTime = time(nullptr);
                    uint32_t provTime = _provisionalBase + ((millis() - _provisionalBootMillis) / 1000);
                    int32_t delta = realTime - provTime;


                    if (_timeSyncCb && abs(delta) > 5) {
                        _timeSyncCb(_provisionalBase, delta);
                    }
                    _provisionalActive = false;
                }

                _state = NET_READY;
                _reconnectDelay = 5000;
                _connectCycles = 0;     /* Conexão completa: reseta ciclos */
                resetNtpBackoff();      /* Sync NTP bem-sucedido: reseta backoff  */
            }
            else if (millis() - _stateTimer > _ntpRetryDelay) {
                _ntpFailCount++;

                /* Fallback para pool.ntp.org após N falhas, se ainda não feito
                 * e o servidor configurado não for já o pool.ntp.org. */
                if (_ntpFailCount >= NTP_FAILS_BEFORE_FALLBACK &&
                    !_ntpFallbackDone &&
                    strcmp(_ntpServer, "pool.ntp.org") != 0) {
                    LOG_CODE(LOG_WARN, "NET", SYS_NTP_SYNC, _ntpFailCount,
                        "NTP fallback: " + String(_ntpServer) + " -> pool.ntp.org");
                    safeCopy(_ntpServer, "pool.ntp.org", sizeof(_ntpServer));
                    _ntpFallbackDone = true;
                }

                /* Backoff exponencial: 20s -> 60s -> 5min -> 15min (cap). */
                _ntpRetryDelay = min(_ntpRetryDelay * 3, NTP_MAX_RETRY_DELAY_MS);

                syncNtp();
                _stateTimer = millis();
            }
            break;

        case NET_READY:
            if (WiFi.status() != WL_CONNECTED) {
                LOG_CODE(LOG_WARN, "NET", SYS_WIFI_DISCONNECT, 0, "WiFi signal lost, entering stealth scan");


                WiFi.disconnect(false);
                _reconnectDelay = 5000;
                resetNtpBackoff();  /* Próxima reconexão parte do delay inicial */
                _state = NET_DISCONNECT_PENDING;
                _stateTimer = millis();
            }
            break;


        case NET_DISCONNECT_PENDING:
            if (millis() - _stateTimer > 200) {
                WiFi.mode(WIFI_STA);
                _state = NET_OFFLINE;
                _reconnectTimer = millis();
            }
            break;
        default: break;
    }
}

/**
 * @brief Reseta o estado de backoff de retry do NTP.
 *
 * Chamado quando o NTP sincroniza com sucesso e quando o WiFi é perdido
 * (para que a próxima reconexão parta do delay inicial).
 * NÃO reverte o _ntpFallbackDone — uma vez que o fallback para pool.ntp.org
 * foi feito, o servidor customizado é considerado falho até próximo boot
 * ou alteração manual pelo usuário.
 */
void NetworkManager::resetNtpBackoff() {
    _ntpRetryDelay = 20000;
    _ntpFailCount  = 0;
}

/** @brief Handle WiFi connection timeout with async disconnect, backoff, and dormant mode. */
void NetworkManager::handleConnecting() {
    if (WiFi.status() == WL_CONNECTED) {
        _state = NET_CONNECTED_WAIT_IP;
        _connectCycles = 0;     /* Sucesso: reseta contador de ciclos */
    }
    else if (millis() - _stateTimer > 20000) {

        WiFi.disconnect(false);

        _connectCycles++;

        if (_connectCycles >= WIFI_MAX_CONNECT_CYCLES) {
            /* Dormência longa: evita drenar bateria/CPU com reconexões inúteis */
            _reconnectDelay = WIFI_DORMANT_DELAY_MS;
            LOG_CODE(LOG_WARN, "NET", NET_DORMANT_MODE, _connectCycles, "Dormant: retry in " + String(_reconnectDelay / 1000) + "s");
        } else {
            _reconnectDelay = min(_reconnectDelay * 2, MAX_RECONNECT_DELAY);
            LOG_CODE(LOG_WARN, "NET", NET_CONNECT_TIMEOUT, _connectCycles, "Retry in " + String(_reconnectDelay / 1000) + "s");
        }

        _state = NET_DISCONNECT_PENDING;
        _stateTimer = millis();
    }
}

/**
 * @brief Configura NTP com servidor customizável e timezone do sistema.
 *
 * O RTC interno permanece em UTC (configTime com offset 0).
 * O fuso horário é aplicado via setenv("TZ")/tzset() para que
 * localtime_r() retorne hora local em todo o sistema.
 */
void NetworkManager::syncNtp() {
    applyTimezone(_tzOffset);
    configTime(0, 0, _ntpServer, "time.nist.gov");
}

/**
 * @brief Aplica fuso horário globalmente via variável de ambiente POSIX.
 *
 * Notação POSIX: sinal invertido — UTC3 = GMT-3 (Brasil).
 * Após chamada, toda localtime_r() retorna hora local corretamente.
 *
 * @param offset  Offset em horas (ex: -3 para Brasil, +9 para Japão).
 */
void NetworkManager::applyTimezone(int8_t offset) {
    char tzStr[16];
    /* POSIX TZ: sinal invertido. offset=-3 → "UTC3" (3h a oeste) */
    snprintf(tzStr, sizeof(tzStr), "UTC%d", (int)(-offset));
    setenv("TZ", tzStr, 1);
    tzset();
}

/**
 * @brief Get current epoch — real (NTP) or provisional (Virtual RTC).
 * Priority: real RTC > provisional > raw (1970).
 */
time_t NetworkManager::getEpoch() {
    time_t t = time(nullptr);
    if (t > 1600000000) return t;
    if (_provisionalActive) return _provisionalBase + ((millis() - _provisionalBootMillis) / 1000);
    return t;
}

bool NetworkManager::isConnected() { return (_state == NET_READY); }
bool NetworkManager::isTimeSynced() { return (getEpoch() > 1600000000); }

bool NetworkManager::isNetworkHealthy() {
    return isConnected() && getRssi() > RSSI_MIN_THRESHOLD;
}

String NetworkManager::getIpAddress() { return (_state == NET_AP_CONFIG) ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }
String NetworkManager::getMacAddress() { return WiFi.macAddress(); }
String NetworkManager::getSubnetMask() { return WiFi.subnetMask().toString(); }
String NetworkManager::getGateway() { return WiFi.gatewayIP().toString(); }
String NetworkManager::getDns() { return WiFi.dnsIP().toString(); }
int32_t NetworkManager::getRssi() { return (!isConnected()) ? -100 : WiFi.RSSI(); }

String NetworkManager::getFormattedTime() {
    time_t now = getEpoch();
    if (now < 1600000000) return String(millis());

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char buff[12];
    snprintf(buff, sizeof(buff), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buff);
}

String NetworkManager::getFormattedDate() {
    time_t now = getEpoch();
    if (now < 1600000000) return "01/01/1970";

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char buff[16];
    snprintf(buff, sizeof(buff), "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    return String(buff);
}
