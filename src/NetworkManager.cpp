/**
 * @file NetworkManager.cpp
 * @brief Implementation of NetworkManager — WiFi state machine, NTP, and time utilities.
 * @details Implements a multi-state WiFi connection manager with async scan,
 * exponential backoff, non-blocking disconnect, NTP sync with
 * provisional time correction callback, and timezone-aware
 * date/time formatting.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "NetworkManager.h"
#include "MetricsManager.h"
#include <stdlib.h>
#include <sys/time.h> /* settimeofday para manual RTC */
#include <lwip/dns.h> /* dns_setserver para DNS manual */
#include <lwip/ip_addr.h>

NetworkManager::NetworkManager( ) {
 _state = NET_OFFLINE;
 _stateTimer = 0;
 _reconnectTimer = 0;
}

/**
 * @brief Initialize WiFi in STA mode with optional static IP.
 * Starts the connection process; actual connection completes in update( ).
 */
void NetworkManager::begin(const SystemConfig &cfg,
 bool dnsAuto, bool ntpEnabled, const char* dns2) {
 safeCopy(_ssid, cfg.wifiSsid, sizeof(_ssid));
 safeCopy(_pass, cfg.wifiPass, sizeof(_pass));
 safeCopy(_deviceName, cfg.deviceName, sizeof(_deviceName));
 _tzOffset = cfg.timezoneOffset;

 /* copia flags e strings de DNS para aplicar em NET_CONNECTED_WAIT_IP. */
 _dnsAuto = dnsAuto;
 _ntpEnabled = ntpEnabled;
 _useDhcp = cfg.useDhcp;
 safeCopy(_staticDns1, cfg.staticDns, sizeof(_staticDns1));
 safeCopy(_staticDns2, dns2 ? dns2 : "", sizeof(_staticDns2));

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
 /* arduino-pico WiFi.config tem assinatura
 * (ip, dns_server, gateway, subnet) — diferente do ESP32/ESP8266
 * que é (ip, gw, mask, dns). Passar na ordem ESP faz a netmask
 * receber o IP do DNS (ex: 8.8.8.8 como netmask é inválida),
 * lwIP não consegue calcular rotas e WiFi.begin( ) nunca associa.
 * Ref: WiFiClass.h:219 em rp2040/hardware/rp2040/5.5.1. */
 WiFi.config(ip, dns, gw, mask);
 LOG_CODE(LOG_INFO, "NET", NET_STATIC_MODE, 0, "");
 } else {
 WiFi.config(IPAddress( ), IPAddress( ), IPAddress( ), IPAddress( ));
 LOG_CODE(LOG_INFO, "NET", NET_DHCP_MODE, 0, "");
 }

 if (strlen(_ssid) == 0) {
 LOG_CODE(LOG_WARN, "NET", NET_SSID_MISSING, 0, "");
 _state = NET_OFFLINE;
 } else {
 LOG_CODE(LOG_INFO, "NET", NET_STARTING, 0, "");
 WiFi.begin(_ssid, _pass);
 _state = NET_CONNECTING;
 _stateTimer = millis( );
 }
}

/**
 * @brief Start WiFi in Access Point mode for initial configuration.
 * Creates a captive portal on 192.168.4.1 with a DNS redirect.
 */
void NetworkManager::beginAP(const char* deviceName) {
 _state = NET_AP_CONFIG;
 _apStartTime = millis( );
 WiFi.mode(WIFI_AP);
 IPAddress apIP(192, 168, 4, 1); IPAddress gateway(192, 168, 4, 1); IPAddress subnet(255, 255, 255, 0);
 WiFi.softAPConfig(apIP, gateway, subnet);
 String apName = String(deviceName) + "_SETUP";
 WiFi.softAP(apName.c_str( ));
 _dnsServer.start(53, "*", apIP);
 LOG_CODE(LOG_INFO, "NET", SYS_AP_START, 0, String(TRL("Access Point: ")) + apName);
}


/**
 * @brief Set provisional time from the last Flash-stored timestamp.
 * Provides approximate timestamps until NTP sync completes (Virtual RTC).
 */
void NetworkManager::setProvisionalTime(uint32_t lastTs) {
 if (lastTs > 1600000000) {
 _provisionalBase = lastTs + 60;
 _provisionalBootMillis = millis( );
 _provisionalActive = true;
 LOG_CODE(LOG_INFO, "NET", NET_PROVISIONAL_TIME, 0, String(TRL("Provisional: ")) + getFormattedDate( ) + " " + getFormattedTime( ));
 }
}

void NetworkManager::setTimeSyncCallback(TimeSyncCallback cb) { _timeSyncCb = cb; }

/**
 * @brief F-NET-TIME.3a: set manual de RTC via settimeofday.
 *
 * Usado quando `isNtpEnabled( )=false` e o user setou data/hora pela web/CLI.
 * epoch deve ser UTC — conversão de hora local para epoch é responsabilidade
 * do cliente (JS/CLI já tem acesso ao timezone via cfg.timezoneOffset).
 * Limpa `_provisionalActive` porque agora temos hora "real" (manual).
 */
void NetworkManager::setManualTime(time_t epoch) {
 if (epoch <= 1600000000) return; /* Rejeita valor obviamente inválido. */
 struct timeval tv;
 tv.tv_sec = epoch;
 tv.tv_usec = 0;
 settimeofday(&tv, nullptr);
 _provisionalActive = false;
 LOG_CODE(LOG_INFO, "NET", SYS_NTP_SYNC, 0,
 TRL("RTC set manually"));
}

/**
 * @brief Network state machine — handles all connection states.
 * Must be called frequently from the main loop.
 */
void NetworkManager::update( ) {
 if (_state == NET_AP_CONFIG) {
 _dnsServer.processNextRequest( );
 /* N5: timeout AP mode — reboot para STA se SSID configurado */
 if (timeSince(_apStartTime, AP_MODE_TIMEOUT_MS) && strlen(_ssid) > 0) {
 LOG_CODE(LOG_WARN, "NET", NET_CONNECT_TIMEOUT, 0, TRL("AP mode timeout, rebooting to STA"));
 watchdog_update( );
 LogManager::instance( ).safeReboot( );
 }
 return;
 }

 /* mDNS: atualiza apenas quando conectado e com throttle de 2s */
 if (_state == NET_READY) {
 uint32_t now = millis( );
 if (now - _lastMdnsUpdate >= MDNS_UPDATE_INTERVAL_MS) {
 _lastMdnsUpdate = now;
 MDNS.update( );
 }
 }

 switch (_state) {
 case NET_OFFLINE:

 if (timeSince(_reconnectTimer, _reconnectDelay)) {
 if (strlen(_ssid) > 0) {
 LOG_CODE(LOG_INFO, "NET", SYS_WIFI_SCAN, 0, String(TRL("Scanning for SSID (backoff=")) + (_reconnectDelay/1000) + "s)");
 WiFi.scanNetworks(true);
 _state = NET_SCANNING_RETRY;
 } else { _reconnectTimer = millis( ); }
 }
 break;

 case NET_SCANNING_RETRY: {
 int n = WiFi.scanComplete( );
 if (n == -1) return;
 if (n < -1) { _state = NET_OFFLINE; _reconnectTimer = millis( ); return; }

 bool found = false;
 for (int i = 0; i < n; i++) {
 if (strcmp(WiFi.SSID(i), _ssid) == 0) { found = true; break; }
 }
 WiFi.scanDelete( );

 if (found) {
 LOG_CODE(LOG_INFO, "NET", SYS_WIFI_CONNECT, 0, TRL("SSID found, connecting..."));
 WiFi.begin(_ssid, _pass);
 _state = NET_CONNECTING; _stateTimer = millis( );
 } else { _state = NET_OFFLINE; _reconnectTimer = millis( ); }
 break;
 }

 case NET_CONNECTING: handleConnecting( ); break;

 case NET_CONNECTED_WAIT_IP:
 if (WiFi.localIP( ).toString( ) != "0.0.0.0") {
 LOG_CODE(LOG_INFO, "NET", SYS_IP_ACQUIRED, 0, "IP: " + WiFi.localIP( ).toString( ));
 MetricsManager::instance( ).data( ).wifiReconnects++;
 applyManualDnsIfNeeded( ); /* DNS manual pós-DHCP */
 if (!MDNS.begin(_deviceName)) LOG_CODE(LOG_ERROR, "NET", SYS_OK, 0, TRL("mDNS failed to start"));
 if (_ntpEnabled) {
 syncNtp( );
 _state = NET_CONNECTED_WAIT_NTP; _stateTimer = millis( );
 } else {
 /* NTP desabilitado pelo user; RTC fica com
 * valor manual/provisional. Pula o estado de espera NTP. */
 LOG_CODE(LOG_INFO, "NET", SYS_NTP_SYNC, 0,
 TRL("NTP disabled — manual RTC mode"));
 _state = NET_READY;
 _reconnectDelay = 5000;
 _connectCycles = 0;
 }
 }
 break;

 case NET_CONNECTED_WAIT_NTP:
 if (time(nullptr) > 1600000000) {
 LOG_CODE(LOG_INFO, "NET", SYS_NTP_SYNC, 0, "NTP OK: " + getFormattedDate( ) + " " + getFormattedTime( ));


 if (_provisionalActive) {
 uint32_t realTime = time(nullptr);
 uint32_t provTime = _provisionalBase + ((millis( ) - _provisionalBootMillis) / 1000);
 int32_t delta = realTime - provTime;


 if (_timeSyncCb && abs(delta) > 5) {
 _timeSyncCb(_provisionalBase, delta);
 }
 _provisionalActive = false;
 }

 _state = NET_READY;
 _reconnectDelay = 5000;
 _connectCycles = 0; /* Conexão completa: reseta ciclos */
 resetNtpBackoff( ); /* Sync NTP bem-sucedido: reseta backoff */
 }
 else if (timeSince(_stateTimer, _ntpRetryDelay)) {
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

 syncNtp( );
 _stateTimer = millis( );
 }
 break;

 case NET_READY:
 if (WiFi.status( ) != WL_CONNECTED) {
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_DISCONNECT, 0, TRL("WiFi signal lost, entering stealth scan"));


 WiFi.disconnect(false);
 _reconnectDelay = 5000;
 resetNtpBackoff( ); /* Próxima reconexão parte do delay inicial */
 _state = NET_DISCONNECT_PENDING;
 _stateTimer = millis( );
 } else {
 /* Amostra RSSI uma vez por minuto quando conectado. */
 static uint32_t _rssiSampleAt = 0;
 if (timeSince(_rssiSampleAt, 60000)) {
 _rssiSampleAt = millis( );
 MetricsManager::instance( ).observeRssi(WiFi.RSSI( ));
 }
 }
 break;


 case NET_DISCONNECT_PENDING:
 if (timeSince(_stateTimer, 200)) {
 WiFi.mode(WIFI_STA);
 _state = NET_OFFLINE;
 _reconnectTimer = millis( );
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
void NetworkManager::resetNtpBackoff( ) {
 _ntpRetryDelay = 20000;
 _ntpFailCount = 0;
}

/** @brief Handle WiFi connection timeout with async disconnect, backoff, and dormant mode. */
void NetworkManager::handleConnecting( ) {
 if (WiFi.status( ) == WL_CONNECTED) {
 _state = NET_CONNECTED_WAIT_IP;
 _connectCycles = 0; /* Sucesso: reseta contador de ciclos */
 }
 else if (timeSince(_stateTimer, 20000)) {

 WiFi.disconnect(false);

 _connectCycles++;

 if (_connectCycles >= WIFI_MAX_CONNECT_CYCLES) {
 /* Dormência longa: evita drenar bateria/CPU com reconexões inúteis */
 _reconnectDelay = WIFI_DORMANT_DELAY_MS;
 LOG_CODE(LOG_WARN, "NET", NET_DORMANT_MODE, _connectCycles, String(TRL("Dormant: retry in ")) + (_reconnectDelay / 1000) + "s");
 } else {
 _reconnectDelay = min(_reconnectDelay * 2, MAX_RECONNECT_DELAY);
 LOG_CODE(LOG_WARN, "NET", NET_CONNECT_TIMEOUT, _connectCycles, String(TRL("Retry in ")) + (_reconnectDelay / 1000) + "s");
 }

 _state = NET_DISCONNECT_PENDING;
 _stateTimer = millis( );
 }
}

/**
 * @brief Configura NTP com servidor customizável e timezone do sistema.
 *
 * O RTC interno permanece em UTC (configTime com offset 0).
 * O fuso horário é aplicado via setenv("TZ")/tzset( ) para que
 * localtime_r( ) retorne hora local em todo o sistema.
 */
void NetworkManager::syncNtp( ) {
 applyTimezone(_tzOffset);
 configTime(0, 0, _ntpServer, "time.nist.gov");
}

/**
 * @brief F-NET-TIME.2: aplica DNS manual via lwIP após IP adquirido.
 *
 * Chamado em NET_CONNECTED_WAIT_IP logo após o IP ser obtido (DHCP ou static).
 * No-op se `_dnsAuto=true` e `_useDhcp=true` (caso em que o DHCP define o DNS
 * e o user não quer override).
 *
 * Matriz de efeitos:
 * useDhcp=true, dnsAuto=true → no-op (DHCP dita).
 * useDhcp=true, dnsAuto=false → sobrescreve dns[0] e dns[1] manuais.
 * useDhcp=false → WiFi.config já setou dns[0]; aplica dns[1] se houver.
 * (dnsAuto ignorado sem servidor DHCP para prover DNS.)
 */
void NetworkManager::applyManualDnsIfNeeded( ) {
 /* Caso 1: DHCP + DNS manual → overrides DNS primário. */
 if (_useDhcp && !_dnsAuto) {
 IPAddress d1;
 if (_staticDns1[0] != '\0' && d1.fromString(_staticDns1) && (uint32_t)d1 != 0) {
 ip_addr_t a; a.addr = (uint32_t)d1;
 dns_setserver(0, &a);
 }
 }

 /* Caso 2 (comum aos dois cenários de DNS manual): aplica secundário se houver.
 * No modo static, WiFi.config já populou dns[0] — só falta dns[1].
 * No modo DHCP com dnsAuto=false, o primário foi tratado acima. */
 if (!_dnsAuto && _staticDns2[0] != '\0') {
 IPAddress d2;
 if (d2.fromString(_staticDns2) && (uint32_t)d2 != 0) {
 ip_addr_t a; a.addr = (uint32_t)d2;
 dns_setserver(1, &a);
 }
 }
}

/**
 * @brief Aplica fuso horário globalmente via variável de ambiente POSIX.
 *
 * Notação POSIX: sinal invertido — UTC3 = GMT-3 (Brasil).
 * Após chamada, toda localtime_r( ) retorna hora local corretamente.
 *
 * @param offset Offset em horas (ex: -3 para Brasil, +9 para Japão).
 */
void NetworkManager::applyTimezone(int8_t offset) {
 char tzStr[16];
 /* POSIX TZ: sinal invertido. offset=-3 → "UTC3" (3h a oeste) */
 snprintf(tzStr, sizeof(tzStr), "UTC%d", (int)(-offset));
 setenv("TZ", tzStr, 1);
 tzset( );
}

/**
 * @brief Get current epoch — real (NTP) or provisional (Virtual RTC).
 * Priority: real RTC > provisional > raw (1970).
 */
time_t NetworkManager::getEpoch( ) {
 time_t t = time(nullptr);
 if (t > 1600000000) return t;
 if (_provisionalActive) return _provisionalBase + ((millis( ) - _provisionalBootMillis) / 1000);
 return t;
}

bool NetworkManager::isConnected( ) { return (_state == NET_READY); }
bool NetworkManager::isTimeSynced( ) { return (getEpoch( ) > 1600000000); }

bool NetworkManager::isNetworkHealthy( ) {
 return isConnected( ) && getRssi( ) > RSSI_MIN_THRESHOLD;
}

String NetworkManager::getIpAddress( ) { return (_state == NET_AP_CONFIG) ? WiFi.softAPIP( ).toString( ) : WiFi.localIP( ).toString( ); }
void NetworkManager::getIpAddress(char* buf, size_t len) {
 IPAddress ip = (_state == NET_AP_CONFIG) ? WiFi.softAPIP( ) : WiFi.localIP( );
 snprintf(buf, len, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}
String NetworkManager::getMacAddress( ) { return WiFi.macAddress( ); }
void NetworkManager::getMacAddress(char* buf, size_t len) {
 String mac = WiFi.macAddress( );
 strncpy(buf, mac.c_str( ), len - 1);
 buf[len - 1] = '\0';
}
String NetworkManager::getSubnetMask( ) { return WiFi.subnetMask( ).toString( ); }
String NetworkManager::getGateway( ) { return WiFi.gatewayIP( ).toString( ); }
String NetworkManager::getDns( ) { return WiFi.dnsIP( ).toString( ); }
int32_t NetworkManager::getRssi( ) { return (!isConnected( )) ? -100 : WiFi.RSSI( ); }

String NetworkManager::getFormattedTime( ) {
 time_t now = getEpoch( );
 if (now < 1600000000) return String(millis( ));

 struct tm timeinfo;
 localtime_r(&now, &timeinfo);

 char buff[12];
 snprintf(buff, sizeof(buff), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
 return String(buff);
}

String NetworkManager::getFormattedDate( ) {
 time_t now = getEpoch( );
 if (now < 1600000000) return "01/01/1970";

 struct tm timeinfo;
 localtime_r(&now, &timeinfo);

 char buff[16];
 snprintf(buff, sizeof(buff), "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
 return String(buff);
}
