/**
 * @file    NetworkManager.cpp
 * @brief   Implementation of NetworkManager — WiFi state machine, NTP, and time utilities.
 * @details Implements a multi-state WiFi connection manager with async scan,
 *          exponential backoff, non-blocking disconnect, NTP sync with
 *          provisional time correction callback, and timezone-aware
 *          date/time formatting.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
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
    strncpy(_ssid, cfg.wifiSsid, 31);
    strncpy(_pass, cfg.wifiPass, 31);
    strncpy(_deviceName, cfg.deviceName, 31);
    _tzOffset = cfg.timezoneOffset;

    WiFi.mode(WIFI_STA);

    if (!cfg.useDhcp && strlen(cfg.staticIp) > 7) {
        IPAddress ip, gw, mask, dns;
        ip.fromString(cfg.staticIp); mask.fromString(cfg.staticMask);
        gw.fromString(cfg.staticGateway); dns.fromString(cfg.staticDns);
        WiFi.config(ip, gw, mask, dns);
        LOG_INF("NET", "Static IP Mode Enabled.");
    } else {
        WiFi.config(IPAddress(), IPAddress(), IPAddress(), IPAddress());
        LOG_INF("NET", "DHCP Mode Enabled.");
    }

    if (strlen(_ssid) == 0) {
        LOG_WRN("NET", "WiFi SSID not configured.");
        _state = NET_OFFLINE;
    } else {
        LOG_INF("NET", "Starting WiFi Manager...");
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
        LOG_INF("NET", "Provisional time set from Flash: " + getFormattedDate() + " " + getFormattedTime());
    }
}

void NetworkManager::setTimeSyncCallback(TimeSyncCallback cb) { _timeSyncCb = cb; }

/**
 * @brief Network state machine — handles all connection states.
 * Must be called frequently from the main loop.
 */
void NetworkManager::update() {
    if (_state == NET_AP_CONFIG) { _dnsServer.processNextRequest(); return; }
    MDNS.update();

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
            }
            else if (millis() - _stateTimer > 20000) {
                 syncNtp(); _stateTimer = millis();
            }
            break;

        case NET_READY:
            if (WiFi.status() != WL_CONNECTED) {
                LOG_CODE(LOG_WARN, "NET", SYS_WIFI_DISCONNECT, 0, "WiFi signal lost, entering stealth scan");


                WiFi.disconnect(false);
                _reconnectDelay = 5000;
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

/** @brief Handle WiFi connection timeout with async disconnect and backoff. */
void NetworkManager::handleConnecting() {
    if (WiFi.status() == WL_CONNECTED) { _state = NET_CONNECTED_WAIT_IP; }
    else if (millis() - _stateTimer > 20000) {

        WiFi.disconnect(false);

        _reconnectDelay = min(_reconnectDelay * 2, MAX_RECONNECT_DELAY);
        LOG_WRN("NET", "Connect timeout. Next retry in " + String(_reconnectDelay/1000) + "s");
        _state = NET_DISCONNECT_PENDING;
        _stateTimer = millis();
    }
}

/**
 * @brief Configure NTP client in strict UTC mode.
 * Timezone offset is applied at display time, not at the RTC level,
 * ensuring telemetry timestamps are always universal.
 */
void NetworkManager::syncNtp() {


    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
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

String NetworkManager::getIpAddress() { return (_state == NET_AP_CONFIG) ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }
String NetworkManager::getMacAddress() { return WiFi.macAddress(); }
String NetworkManager::getSubnetMask() { return WiFi.subnetMask().toString(); }
String NetworkManager::getGateway() { return WiFi.gatewayIP().toString(); }
String NetworkManager::getDns() { return WiFi.dnsIP().toString(); }
int32_t NetworkManager::getRssi() { return (!isConnected()) ? -100 : WiFi.RSSI(); }

String NetworkManager::getFormattedTime() {
    time_t now = getEpoch();
    if (now < 1600000000) return String(millis());

    now += (_tzOffset * 3600);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    char buff[12];
    snprintf(buff, sizeof(buff), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buff);
}

String NetworkManager::getFormattedDate() {
    time_t now = getEpoch();
    if (now < 1600000000) return "01/01/1970";

    now += (_tzOffset * 3600);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    char buff[16];
    snprintf(buff, sizeof(buff), "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    return String(buff);
}
