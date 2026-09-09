/**
 * @file NetworkManager.cpp
 * @brief Implementation of NetworkManager — WiFi state machine, NTP, and time utilities.
 * @details Implements a multi-state WiFi connection manager with async scan,
 * exponential backoff, non-blocking disconnect, NTP sync with
 * provisional time correction callback, and timezone-aware
 * date/time formatting.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "NetworkManager.h"
#include "MetricsManager.h"
#include <stdlib.h>
#include <sys/time.h> /* settimeofday for manual RTC */
#include <lwip/dns.h> /* dns_setserver for manual DNS */
#include <lwip/ip_addr.h>
#include "pico/unique_id.h"           /* the setup AP key is derived from the board id */
#include <bearssl/bearssl_hash.h>     /* br_sha256_* for that derivation (V-05) */

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

 /* Copy DNS flags and strings to apply in NET_CONNECTED_WAIT_IP. */
 _dnsAuto = dnsAuto;
 _ntpEnabled = ntpEnabled;
 _useDhcp = cfg.useDhcp;
 safeCopy(_staticDns1, cfg.staticDns, sizeof(_staticDns1));
 safeCopy(_staticDns2, dns2 ? dns2 : "", sizeof(_staticDns2));

 /* Configurable NTP server (fallback to pool.ntp.org if empty) */
 if (cfg.ntpServer[0] != '\0') {
 safeCopy(_ntpServer, cfg.ntpServer, sizeof(_ntpServer));
 _ntpServer[sizeof(_ntpServer) - 1] = '\0';
 } else {
 safeCopy(_ntpServer, "pool.ntp.org", sizeof(_ntpServer));
 }

 /* Apply timezone globally so localtime_r works */
 applyTimezone(_tzOffset);

 WiFi.mode(WIFI_STA);

 if (!cfg.useDhcp && strlen(cfg.staticIp) > 7) {
 IPAddress ip, gw, mask, dns;
 ip.fromString(cfg.staticIp); mask.fromString(cfg.staticMask);
 gw.fromString(cfg.staticGateway); dns.fromString(cfg.staticDns);
 /* arduino-pico WiFi.config has signature
 * (ip, dns_server, gateway, subnet) — different from ESP32/ESP8266
 * which is (ip, gw, mask, dns). Passing in ESP order makes the netmask
 * receive the DNS IP (ex: 8.8.8.8 as netmask is invalid),
 * lwIP cannot calculate routes and WiFi.begin( ) never associates.
 * Ref: WiFiClass.h:219 in rp2040/hardware/rp2040/5.5.1. */
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
 *
 * WPA2 since finding V-05. The key is derived from the board's unique id (see
 * ApPsk.h) rather than configured: AP mode is what an UNCONFIGURED device
 * boots into, so a key that lives in the configuration would not exist yet on
 * the one device that needs it. It is printed on the console next to the
 * SYS_AP_START line and shown on the display, which is the whole distribution
 * mechanism — whoever can see the device can read it, and whoever is merely in
 * radio range cannot.
 */
void NetworkManager::beginAP(const char* deviceName) {
 _state = NET_AP_CONFIG;
 _apStartTime = millis( );
 WiFi.mode(WIFI_AP);
 IPAddress apIP(192, 168, 4, 1); IPAddress gateway(192, 168, 4, 1); IPAddress subnet(255, 255, 255, 0);
 WiFi.softAPConfig(apIP, gateway, subnet);
 String apName = String(deviceName) + "_SETUP";

 _apPsk[0] = '\0';
#if !SIMUT_AP_OPEN
 {
  /* SHA-256(board id || domain). The domain string keeps this key from being
   * the same derivation as anything else that hashes the board id. */
  pico_unique_board_id_t bid;
  pico_get_unique_board_id(&bid);
  uint8_t digest[32];
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, bid.id, sizeof(bid.id));
  br_sha256_update(&ctx, "simut-ap-psk", 12);
  br_sha256_out(&ctx, digest);
  apPskFromDigest(digest, sizeof(digest), _apPsk, sizeof(_apPsk));
 }
#endif

 if (_apPsk[0]) {
  WiFi.softAP(apName.c_str( ), _apPsk);
 } else {
  /* SIMUT_AP_OPEN=1 only, or a derivation that could not produce a full key.
   * Never silently: an open setup network is the finding, so it says so. */
  WiFi.softAP(apName.c_str( ));
 }
 _dnsServer.start(53, "*", apIP);

 /* Console, unconditionally and not through the log: this is the only place
  * the key is published, and a device in AP mode is a device whose operator is
  * standing at the serial port because nothing else works. */
 if (Serial) {
  Serial.printf("\n[AP] SSID: %s\n", apName.c_str( ));
  if (_apPsk[0]) Serial.printf("[AP] PSK : %s   (WPA2)\n", _apPsk);
  else Serial.println("[AP] PSK : (none — OPEN network, SIMUT_AP_OPEN build)");
  Serial.println("[AP] URL : http://192.168.4.1");
  Serial.flush( );
 }
 LOG_CODE(LOG_INFO, "NET", SYS_AP_START, _apPsk[0] ? 1 : 0,
          String(TRL("Access Point: ")) + apName);
}


/**
 * @brief Set provisional time from the last Flash-stored timestamp.
 * Provides approximate timestamps until NTP sync completes (Virtual RTC).
 */
void NetworkManager::setProvisionalTime(uint32_t lastTs, uint32_t elapsedSec) {
 if (lastTs > 1600000000) {
 _provisionalBase = lastTs + elapsedSec;
 _provisionalBootMillis = millis( );
 _provisionalActive = true;
 LOG_CODE(LOG_INFO, "NET", NET_PROVISIONAL_TIME, 0, String(TRL("Provisional: ")) + getFormattedDate( ) + " " + getFormattedTime( ));
 }
}

void NetworkManager::setTimeSyncCallback(TimeSyncCallback cb) { _timeSyncCb = cb; }

/**
 * @brief Set RTC manually via settimeofday.
 *
 * Used when `isNtpEnabled( )=false` and the user set date/time via web/CLI.
 * epoch must be UTC — local time to epoch conversion is the responsibility
 * of the client (JS/CLI already has access to timezone via cfg.timezoneOffset).
 * Clears `_provisionalActive` because we now have "real" (manual) time.
 */
void NetworkManager::setManualTime(time_t epoch) {
 if (epoch <= 1600000000) return; /* Reject obviously invalid value. */
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
 /* AP mode timeout — reboot to STA if SSID configured */
 if (timeSince(_apStartTime, AP_MODE_TIMEOUT_MS) && strlen(_ssid) > 0) {
 LOG_CODE(LOG_WARN, "NET", NET_CONNECT_TIMEOUT, 0, TRL("AP mode timeout, rebooting to STA"));
 watchdog_update( );
 LogManager::instance( ).safeReboot( );
 }
 return;
 }

 /* mDNS: update only when connected and throttled at 2s.
  * ON by default; set SIMUT_MDNS=0 in src/simut_config.h to drop it and
	 * recover 15,272 B — not the ~196KB this comment used to claim. */
#if SIMUT_MDNS
 if (_mdnsEnabled && _state == NET_READY) {
 uint32_t now = millis( );
 if (now - _lastMdnsUpdate >= MDNS_UPDATE_INTERVAL_MS) {
 _lastMdnsUpdate = now;
 MDNS.update( );
 }
 }
#endif

 switch (_state) {
 case NET_OFFLINE:

 if (timeSince(_reconnectTimer, _reconnectDelay)) {
 if (strlen(_ssid) > 0) {
 LOG_CODE(LOG_INFO, "NET", SYS_WIFI_SCAN, 0, String(TRL("Scanning for SSID (backoff=")) + (_reconnectDelay/1000) + "s)");
 WiFi.scanNetworks(true);
 _state = NET_SCANNING_RETRY;
 _stateTimer = millis( ); /* the scan deadline starts here */
 } else { _reconnectTimer = millis( ); }
 }
 break;

 case NET_SCANNING_RETRY: {
 int n = WiFi.scanComplete( );

 /* -1 is "still running", and it used to be returned on with nothing to bound
  * the wait — which made this a terminal state. Measured on a field device on
  * 2026-09-08: the link dropped at 11:54:47, one scan started at 11:54:53, and
  * the network state machine never emitted another record for 3 h 41 min while
  * the rest of the device stayed healthy (hourly heap reports, hourly history
  * snapshots, someone working the display at 13:15). It came back only when
  * the operator power-cycled it. SYS_WIFI_SCAN was unrouted in LogPolicy on the
  * firmware that produced that log, so nothing could have filtered a repeat:
  * the absence of a second scan line is positive evidence that the machine
  * never left this case, and not a gap in the record. (The routine line is
  * latched now — see LOGGRP_NETSCAN — which is why the deadline below reports
  * at WARN, a level the latch does not touch.)
  *
  * The wedge is reachable from the driver: cyw43_wifi_scan( ) sets
  * wifi_scan_state = 1 BEFORE issuing the low-level scan and does not clear it
  * if that call fails, and nothing in the SDK ever times the state out, so
  * scanComplete( ) keeps answering -1 for the rest of the boot. */
 if (n == -1) {
 if (timeSince(_stateTimer, WIFI_SCAN_TIMEOUT_MS)) {
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_SCAN, (int)(_blindScans + 1),
 TRL("Scan never finished — abandoning it"));
 WiFi.scanDelete( );
 afterFruitlessScan( );
 }
 return;
 }

 if (n < -1) { afterFruitlessScan( ); return; }

 bool found = false;
 for (int i = 0; i < n; i++) {
 if (strcmp(WiFi.SSID(i), _ssid) == 0) { found = true; break; }
 }
 WiFi.scanDelete( );

 if (found) {
 LOG_CODE(LOG_INFO, "NET", SYS_WIFI_CONNECT, 0, TRL("SSID found, connecting..."));
 _blindScans = 0;
 WiFi.begin(_ssid, _pass);
 _state = NET_CONNECTING; _stateTimer = millis( );
 } else { afterFruitlessScan( ); }
 break;
 }

 case NET_CONNECTING: handleConnecting( ); break;

 case NET_CONNECTED_WAIT_IP:
 if (WiFi.localIP( ).toString( ) != "0.0.0.0") {
 LOG_CODE(LOG_INFO, "NET", SYS_IP_ACQUIRED, 0, "IP: " + WiFi.localIP( ).toString( ));
 MetricsManager::instance( ).data( ).wifiReconnects++;
 applyManualDnsIfNeeded( ); /* Manual DNS post-DHCP */
#if SIMUT_MDNS
 if (_mdnsEnabled) {
 if (!MDNS.begin(_deviceName)) LOG_CODE(LOG_ERROR, "NET", NET_MDNS_FAIL, 0, TRL("mDNS failed to start"));
 }
#endif
 if (_ntpEnabled) {
 syncNtp( );
 _state = NET_CONNECTED_WAIT_NTP; _stateTimer = millis( );
 } else {
 /* NTP disabled by user; RTC stays with
 * manual/provisional value. Skip the NTP wait state. */
 LOG_CODE(LOG_INFO, "NET", SYS_NTP_SYNC, 0,
 TRL("NTP disabled — manual RTC mode"));
 _state = NET_READY;
 resetReconnectLadder( );
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
 resetReconnectLadder( );
 _connectCycles = 0; /* Full connection: reset cycles */
 resetNtpBackoff( ); /* NTP sync succeeded: reset backoff */
 }
 else if (timeSince(_stateTimer, _ntpRetryDelay)) {
 _ntpFailCount++;

 /* Fallback to pool.ntp.org after N failures, if not already done
 * and the configured server is not already pool.ntp.org. */
 if (_ntpFailCount >= NTP_FAILS_BEFORE_FALLBACK &&
 !_ntpFallbackDone &&
 strcmp(_ntpServer, "pool.ntp.org") != 0) {
 LOG_CODE(LOG_WARN, "NET", SYS_NTP_SYNC, _ntpFailCount,
 "NTP fallback: " + String(_ntpServer) + " -> pool.ntp.org");
 safeCopy(_ntpServer, "pool.ntp.org", sizeof(_ntpServer));
 _ntpFallbackDone = true;
 }

 /* Exponential backoff: 20s -> 60s -> 5min -> 15min (cap). */
 _ntpRetryDelay = min(_ntpRetryDelay * 3, NTP_MAX_RETRY_DELAY_MS);

 syncNtp( );
 _stateTimer = millis( );
 }
 break;

 case NET_READY:
 if (WiFi.status( ) != WL_CONNECTED) {
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_DISCONNECT, 0, TRL("WiFi signal lost, entering stealth scan"));


 WiFi.disconnect(false);
 resetReconnectLadder( );
 resetNtpBackoff( ); /* Next reconnection starts from initial delay */
 _state = NET_DISCONNECT_PENDING;
 _stateTimer = millis( );
 } else {
 /* Sample RSSI once per minute when connected. */
 static uint32_t _rssiSampleAt = 0;
 static uint8_t _rssiImplausible = 0;
 if (timeSince(_rssiSampleAt, 60000)) {
 _rssiSampleAt = millis( );
 const int32_t rssi = WiFi.RSSI( );
 MetricsManager::instance( ).observeRssi(rssi);

 /* Second liveness signal, because WiFi.status( ) above is not one.
  *
  * Measured 2026-08-02: after a burst of large HTTP responses the device
  * dropped off the network completely — host ARP went INCOMPLETE and ICMP
  * got 100% loss — while WiFi.status( ) still said WL_CONNECTED. Nothing
  * demoted the state, so nothing ever reconnected and the device stayed
  * dark until a reboot, still reporting its IP.
  *
  * The tell was in the reading right here: RSSI came back as +4 dBm.
  * Received signal strength is negative by definition, so a non-negative
  * value means the cyw43 ioctl is no longer returning real data — and it
  * is worse than useless, because isNetworkHealthy( ) compares it against
  * RSSI_MIN_THRESHOLD and an impossible +4 sails past, actively
  * confirming health.
  *
  * Two consecutive bad samples before acting: one could be a glitchy
  * read, and a needless reconnect costs more than a minute of waiting.
  * Detection lands within ~2 minutes instead of never. Sampling stays at
  * once a minute on purpose — RSSI is a live ioctl and hammering it is
  * its own hazard. */
 if (rssi >= RSSI_IMPLAUSIBLE_HIGH || rssi < RSSI_IMPLAUSIBLE_LOW) {
 if (++_rssiImplausible >= 2) {
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_DISCONNECT, (int)rssi,
 TRL("Implausible RSSI twice — link presumed dead, reconnecting"));
 _rssiImplausible = 0;
 WiFi.disconnect(false);
 resetReconnectLadder( );
 resetNtpBackoff( );
 _state = NET_DISCONNECT_PENDING;
 _stateTimer = millis( );
 }
 } else {
 _rssiImplausible = 0;
 }
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
 * @brief Reset the NTP retry backoff state.
 *
 * Called when NTP syncs successfully and when WiFi is lost
 * (so the next reconnection starts from the initial delay).
 * Does NOT revert _ntpFallbackDone — once the fallback to pool.ntp.org
 * was done, the custom server is considered failed until next boot
 * or manual change by the user.
 */
void NetworkManager::resetNtpBackoff( ) {
 _ntpRetryDelay = 20000;
 _ntpFailCount = 0;
}

/** @brief Put the reconnect ladder back on its first rung. */
void NetworkManager::resetReconnectLadder( ) {
 _reconnectDelay = WIFI_RECONNECT_BASE_MS;
 _backoffCycles = 0;
 _dormantWaits = 0;
 _blindScans = 0;
}

/**
 * @brief Leave a scan that did not hand us the SSID.
 *
 * "Did not hand us" covers all three ways a scan can come back useless: the
 * SSID was absent from the results, the scan failed outright, and the scan
 * never finished at all. They are one case here on purpose — what matters is
 * that the cheap check did not answer, and the expensive one has not been
 * tried. After WIFI_SCANS_BEFORE_BLIND_JOIN of them we associate without the
 * scan's blessing, which is both the reconnect path the boot path always had
 * and the only escape from a scanner the driver has wedged: WiFi.begin( ) goes
 * through cyw43_arch_enable_sta_mode( ) and cyw43_wifi_join( ), neither of
 * which reads wifi_scan_state.
 */
void NetworkManager::afterFruitlessScan( ) {
 if (++_blindScans >= WIFI_SCANS_BEFORE_BLIND_JOIN) {
 _blindScans = 0;
 /* LOG_WARN, and not the routine INFO the scan-confirmed branch uses, for
  * two reasons: this is the degraded path and deserves to be visible, and a
  * routine NET record here would be taken by LogPolicy for the family's
  * recovery transition and would eat the SYS_IP_ACQUIRED that actually says
  * the link came back. A WARN is persisted by the level shortcut without
  * touching the family latch. */
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_CONNECT, 1,
 TRL("SSID not in scan — associating anyway"));
 WiFi.begin(_ssid, _pass);
 _state = NET_CONNECTING;
 _stateTimer = millis( );
 } else {
 _state = NET_OFFLINE;
 _reconnectTimer = millis( );
 }
}

/** @brief Handle WiFi connection timeout with async disconnect, backoff, and dormant mode. */
void NetworkManager::handleConnecting( ) {
 if (WiFi.status( ) == WL_CONNECTED) {
 _state = NET_CONNECTED_WAIT_IP;
 _connectCycles = 0; /* Success: reset cycle counter */
 resetReconnectLadder( );
 }
 else if (timeSince(_stateTimer, 20000)) {

 WiFi.disconnect(false);

 /* Saturates instead of wrapping: SIMUT Air stops pumping the network for
  * the rest of a wake once this passes AIR_MAX_CONNECT_ATTEMPTS, and a
  * uint8_t rolling over to 0 would silently retract that. */
 if (_connectCycles < 255) _connectCycles++;
 _backoffCycles++;

 if (_backoffCycles >= WIFI_MAX_CONNECT_CYCLES) {
 if (_dormantWaits >= WIFI_DORMANT_MAX_WAITS) {
 /* Dormancy is a rest, not a retirement. It used to be entered and never
  * left — _connectCycles was only ever cleared by a success, so the first
  * device to exhaust its attempts spent the rest of the boot on a
  * ten-minute grid no matter what happened on the air in between. */
 resetReconnectLadder( );
 LOG_CODE(LOG_INFO, "NET", NET_DORMANT_MODE, 0,
 TRL("Dormancy over — back to fast retries"));
 } else {
 /* Long dormancy: avoids draining battery/CPU with futile reconnections */
 _dormantWaits++;
 _reconnectDelay = WIFI_DORMANT_DELAY_MS;
 LOG_CODE(LOG_WARN, "NET", NET_DORMANT_MODE, _connectCycles, String(TRL("Dormant: retry in ")) + (_reconnectDelay / 1000) + "s");
 }
 } else {
 _reconnectDelay = min(_reconnectDelay * 2, MAX_RECONNECT_DELAY);
 LOG_CODE(LOG_WARN, "NET", NET_CONNECT_TIMEOUT, _connectCycles, String(TRL("Retry in ")) + (_reconnectDelay / 1000) + "s");
 }

 _state = NET_DISCONNECT_PENDING;
 _stateTimer = millis( );
 }
}

/**
 * @brief Configure NTP with customizable server and system timezone.
 *
 * The internal RTC stays in UTC (configTime with offset 0).
 * The timezone is applied via setenv("TZ")/tzset( ) so that
 * localtime_r( ) returns local time throughout the system.
 */
void NetworkManager::syncNtp( ) {
 applyTimezone(_tzOffset);
 configTime(0, 0, _ntpServer, "time.nist.gov");
}

/**
 * @brief Apply manual DNS via lwIP after IP is acquired.
 *
 * Called in NET_CONNECTED_WAIT_IP right after IP is obtained (DHCP or static).
 * No-op if `_dnsAuto=true` and `_useDhcp=true` (case where DHCP defines DNS
 * and user doesn't want override).
 *
 * Effect matrix:
 * useDhcp=true, dnsAuto=true → no-op (DHCP rules).
 * useDhcp=true, dnsAuto=false → overrides dns[0] and dns[1] manually.
 * useDhcp=false → WiFi.config already set dns[0]; applies dns[1] if present.
 * (dnsAuto ignored without DHCP server to provide DNS.)
 */
void NetworkManager::applyManualDnsIfNeeded( ) {
 /* Case 1: DHCP + manual DNS → overrides primary DNS. */
 if (_useDhcp && !_dnsAuto) {
 IPAddress d1;
 if (_staticDns1[0] != '\0' && d1.fromString(_staticDns1) && (uint32_t)d1 != 0) {
 ip_addr_t a; a.addr = (uint32_t)d1;
 dns_setserver(0, &a);
 }
 }

 /* Case 2 (common to both manual DNS scenarios): applies secondary if present.
 * In static mode, WiFi.config already populated dns[0] — only dns[1] missing.
 * In DHCP mode with dnsAuto=false, the primary was handled above. */
 if (!_dnsAuto && _staticDns2[0] != '\0') {
 IPAddress d2;
 if (d2.fromString(_staticDns2) && (uint32_t)d2 != 0) {
 ip_addr_t a; a.addr = (uint32_t)d2;
 dns_setserver(1, &a);
 }
 }
}

/**
 * @brief Apply timezone globally via POSIX environment variable.
 *
 * POSIX notation: inverted sign — UTC3 = GMT-3 (Brazil).
 * After calling, every localtime_r( ) returns local time correctly.
 *
 * @param offset Offset in hours (ex: -3 for Brazil, +9 for Japan).
 */
void NetworkManager::applyTimezone(int8_t offset) {
 char tzStr[16];
 /* POSIX TZ: inverted sign. offset=-3 → "UTC3" (3h west) */
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
 /* No NTP and no provisional time from flash — seed with compile timestamp
  * so history/logs work immediately on first boot. Records will carry
  * approximate timestamps until NTP sync corrects the system clock. */
 _provisionalBase = SIMUT_BUILD_EPOCH;
 _provisionalBootMillis = millis( );
 _provisionalActive = true;
 return _provisionalBase;
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
int32_t NetworkManager::getRssi( ) {
 if (!isConnected( )) return -100;
 /* An implausible reading means the radio is not answering with real data, so
  * report it as a dead link rather than letting a positive dBm pass the
  * RSSI_MIN_THRESHOLD gate in isNetworkHealthy( ). See the NET_READY case. */
 const int32_t rssi = WiFi.RSSI( );
 if (rssi >= RSSI_IMPLAUSIBLE_HIGH || rssi < RSSI_IMPLAUSIBLE_LOW) return -100;
 return rssi;
}

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
