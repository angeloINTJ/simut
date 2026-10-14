/**
 * @file NetworkManager_Cyw43.cpp
 * @brief The radio's side of NetworkManager: the CYW43 of the Pico W and the Pico 2 W
 *        (SIMUT_NET_CYW43), the network driver an image has unless the manifest names
 *        another.
 *
 * NetworkManager.cpp is the host: what any network needs, the NTP wait and its
 * backoff, the provisional and the manual clock, mDNS, the timezone, the formatted
 * time and the health. This unit is what only a radio is: it scans for the SSID,
 * joins it, climbs the reconnect ladder and rests in dormancy, counts the joins the
 * chip refuses, serves the setup access point, scans on demand for the page that
 * picks a network, and answers the addresses and the signal level from WiFi.*. The
 * cable's is net/NetworkManager_Wired.cpp. An image has one of the two (the manifest
 * leaves the other out of the build, tools/features.toml), and the host reaches
 * whichever it is through the three methods of the contract in NetworkManager.h:
 * beginLink( ), updateLinkFirst( ) and updateLinkStates( ).
 *
 * The radio states of update( ) are the switch of updateLinkStates( ) below, in the
 * order they had in NetworkManager.cpp; the AP branch and the scan poll that came
 * before that switch are updateLinkFirst( ), and so, since 2026-10-13, is the look at
 * the link inside the NTP wait. The NTP wait itself, between them, is the host's.
 *
 * @project SIMUT
 * @license MIT License
 */
#include "../NetworkManager.h"
#include "../simut_config.h"   /* SIMUT_NET_CYW43 is tested below: included here, not left to another header */

#if SIMUT_NET_CYW43
#include "../MetricsManager.h"
#include "pico/unique_id.h"           /* the setup AP key is derived from the board id */
#include <bearssl/bearssl_hash.h>     /* br_sha256_* for that derivation (V-05) */
#include "pico/cyw43_arch.h"           /* enable_sta_mode: scanning needs the STA itf */

/**
 * @brief The radio's half of begin( ): the station mode, the static address or DHCP, and the
 * join. begin( ) has copied the SSID, the password and the rest of the settings into the
 * members by the time it calls this, and applied the timezone.
 */
void NetworkManager::beginLink(const SystemConfig &cfg) {
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
 beginJoin( );
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
bool NetworkManager::beginAP(const char* deviceName, const char* stationSsid) {
 /* The AP's time limit gives way to the configured network, and asks for it
  * by its name (update( ), NET_AP_CONFIG). The name came only from begin( ),
  * and the boot that the gesture sends into AP mode never calls begin( ): its
  * limit read 32 bytes nothing had written, and what the heap held decided.
  * Zeros kept the AP of a unit with a network up for good; anything else
  * closed the AP of a unit with none, the one way to give it a network
  * (finding 25 of docs/analysis/PLANO_REVISAO_EXTERNA.md). On the rig, on
  * 2026-10-04, main's gesture AP closed at 900 s: not zeros, that boot.
  * Written here every time, empty for a unit with none. */
 safeCopy(_ssid, stationSsid, sizeof(_ssid));
 /* Take the radio away from the station FIRST, and wait for it to let go.
  *
  * Measured on the rig 2026-09-22: `ap` issued while the station was hunting
  * for an SSID that does not exist brought up an access point the host could
  * SEE at 94% signal and could NOT associate with — 45 s and a timeout, twice
  * — against 2,6 s from the same build with the station connected. One radio
  * serves both, WiFi.mode(WIFI_AP) only assigns a field, and the framework's
  * beginAP tears the station down but cannot cancel a sweep already issued to
  * the driver: cyw43_wifi_scan( ) owns the chip until it finishes, and its
  * wifi_scan_state is the same flag that made NET_SCANNING_RETRY terminal on
  * 2026-09-08. Starting an AP on top of that produced beacons and no
  * association.
  *
  * This matters more than the command: the 2.7.1 fallback opens the AP
  * exactly when the ladder has been failing, which is exactly when a sweep is
  * in flight. Bounded, because a sweep that never finishes is the documented
  * wedge and must not hold the recovery path hostage. */
 {
  const unsigned long t0 = millis( );
  while (WiFi.scanComplete( ) == -1 && millis( ) - t0 < 4000) delay(50);
  WiFi.scanDelete( );
  _scanState = SCAN_IDLE;
  _scanStaForced = false;
  WiFi.disconnect(false);
  delay(200);
 }

 _state = NET_AP_CONFIG;
 _apStartTime = millis( );
 WiFi.mode(WIFI_AP);
 IPAddress apIP(192, 168, 4, 1); IPAddress gateway(192, 168, 4, 1); IPAddress subnet(255, 255, 255, 0);
 WiFi.softAPConfig(apIP, gateway, subnet);
 String apName = String(deviceName) + "_SETUP";
 safeCopy(_apSsid, apName.c_str( ), sizeof(_apSsid));

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

 /* The return value is read. It used to be dropped, so a softAP( ) that
  * failed still set NET_AP_CONFIG and still printed "AP mode started" — the
  * operator was told to join a network that was not on the air. Seen on the
  * rig on 2026-09-22 before the teardown above was added. */
 bool up;
 if (_apPsk[0]) {
  up = WiFi.softAP(apName.c_str( ), _apPsk);
 } else {
  /* SIMUT_AP_OPEN=1 only, or a derivation that could not produce a full key.
   * Never silently: an open setup network is the finding, so it says so. */
  up = WiFi.softAP(apName.c_str( ));
 }
 if (!up) {
  _state = NET_OFFLINE;
  _reconnectTimer = millis( );
  if (Serial) { Serial.println("\n[AP] FAILED to start"); Serial.flush( ); }
  LOG_CODE(LOG_ERROR, "NET", SYS_AP_START, -1, TRL("Access Point failed to start"));
  return false;
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
 return true;
}

/* ── On-demand scan ────────────────────────────────────────────────────────
 *
 * The radio has ONE scanner, and the reconnect path already uses it
 * (NET_SCANNING_RETRY). Whoever asks second is REFUSED rather than queued: a
 * refusal is something the page can show, and a queue is just a second scan
 * waiting to collide. Two in flight is the documented way to wedge this chip.
 */
bool NetworkManager::startScan( ) {
 if (_scanState == SCAN_RUNNING) return false;
 if (_state == NET_SCANNING_RETRY) return false;   /* the other scanner owns it */

 /* cyw43_wifi_scan( ) sweeps on the STA interface. In AP mode that interface
  * is DOWN, and asking anyway is the documented way to leave wifi_scan_state
  * stuck at 1 for the rest of the boot. Bring it up first; the AP stays up
  * beside it, because the browser asking for this list is connected to it. */
 if (_state == NET_AP_CONFIG && !_scanStaForced) {
/* The one call into the CYW43 control code that this unit makes outside the
 * framework's WiFi class. In a build for board = rpipico (any driver but the
 * CYW43) the radio's driver is not linked, and this call alone pulls
 * cyw43_ctrl.c.o in, which wants the __wrap_cyw43_cb_* that only the Pico W's
 * libraries provide (2026-09-26 study: kept out, the release links for the plain
 * Pico). A guard on the chip kept it out of NetworkManager.cpp; since 2026-10-13
 * the whole unit is out of such a build, by [net.cyw43].units in
 * tools/features.toml. */
  cyw43_arch_enable_sta_mode( );
  WiFi.mode(WIFI_AP_STA);
  _scanStaForced = true;
 }

 WiFi.scanDelete( );             /* an older result set is not this one */
 _scanCount = 0;
 /* -1 means the sweep started; 0 means cyw43_wifi_scan( ) refused, and THAT
  * is the call whose failure leaves the state stuck. Reporting it now beats
  * a 15 s wait for a scan that was never running. */
 if (WiFi.scanNetworks(true) != -1) {
  _scanState = SCAN_FAILED;
  /* One TRL( ) for both scan records, the distinguishing part as an
   * untranslated marker: the es-ES pack's resident sections have ~110 B of
   * headroom and two strings would be most of it. */
  LOG_CODE(LOG_WARN, "NET", SYS_WIFI_SCAN, -1, String(TRL("Network scan")) + " [refused]");
  return false;
 }
 _scanState = SCAN_RUNNING;
 _scanStarted = millis( );
 return true;
}

/* Called from updateLinkFirst( ), first in every pass of update( ), in every state. */
void NetworkManager::pollScan( ) {
 if (_scanState != SCAN_RUNNING) return;

 const int n = WiFi.scanComplete( );
 if (n == -1) {
  /* Same deadline as the reconnect scan, and for the same reason: nothing in
   * the SDK times this out, so without a bound this is a terminal state. */
  if (timeSince(_scanStarted, WIFI_SCAN_TIMEOUT_MS)) {
   WiFi.scanDelete( );
   _scanState = SCAN_FAILED;
   LOG_CODE(LOG_WARN, "NET", SYS_WIFI_SCAN, -2, TRL("Scan never finished — abandoning it"));
  }
  return;
 }

 /* Copy OUT: scanDelete( ) frees the driver's map, and the web reads this one
  * request later. Hidden SSIDs are dropped — there is nothing to tap — and a
  * mesh answering on three radios collapses to its strongest, or one network
  * takes all WIFI_SCAN_MAX_NETS slots and hides the rest. */
 _scanCount = 0;
 for (int i = 0; i < n; i++) {
  const char* ss = WiFi.SSID((uint8_t)i);
  if (!ss || !ss[0]) continue;
  WifiNet w;
  safeCopy(w.ssid, ss, sizeof(w.ssid));
  w.rssi    = (int16_t)WiFi.RSSI((uint8_t)i);
  w.enc     = (uint8_t)WiFi.encryptionType((uint8_t)i);
  w.channel = (uint8_t)WiFi.channel((uint8_t)i);

  int8_t at = -1;
  for (uint8_t j = 0; j < _scanCount; j++)
   if (strcmp(_scanNets[j].ssid, w.ssid) == 0) { at = (int8_t)j; break; }
  if (at >= 0) {
   if (w.rssi <= _scanNets[at].rssi) continue;   /* the one we kept is stronger */
   for (uint8_t j = (uint8_t)at; j + 1 < _scanCount; j++) _scanNets[j] = _scanNets[j + 1];
   _scanCount--;
  } else if (_scanCount == WIFI_SCAN_MAX_NETS) {
   if (w.rssi <= _scanNets[_scanCount - 1].rssi) continue;   /* weaker than the weakest kept */
   _scanCount--;
  }

  /* Insert by descending RSSI: 12 entries at most, and signal is what someone
   * picking a network on a phone actually sorts by. */
  uint8_t k = _scanCount;
  while (k > 0 && _scanNets[k - 1].rssi < w.rssi) { _scanNets[k] = _scanNets[k - 1]; k--; }
  _scanNets[k] = w;
  _scanCount++;
 }
 WiFi.scanDelete( );
 _scanState = SCAN_DONE;
 LOG_CODE(LOG_INFO, "NET", SYS_WIFI_SCAN, (int)_scanCount, String(TRL("Network scan")) + " [ready]");
}

uint8_t NetworkManager::scanResults(WifiNet* out, uint8_t cap) const {
 if (!out || !cap) return 0;
 const uint8_t n = (_scanCount < cap) ? _scanCount : cap;
 for (uint8_t i = 0; i < n; i++) out[i] = _scanNets[i];
 return n;
}

/**
 * @brief The radio's first word in update( ): the on-demand scan, the setup AP, and
 * the link inside the NTP wait. LINK_DONE_NOW in AP mode (update( ) has nothing more
 * to do), LINK_GO_ON otherwise.
 */
NetworkManager::LinkFirst NetworkManager::updateLinkFirst( ) {
 /* BEFORE the AP branch, which returns. Scanning from the setup portal is
  * the case this exists for, and a poll placed after that return would run
  * in every mode except the one that needs it. */
 pollScan( );

 if (_state == NET_AP_CONFIG) {
 _dnsServer.processNextRequest( );
 /* AP mode timeout — reboot to STA if SSID configured */
 if (timeSince(_apStartTime, AP_MODE_TIMEOUT_MS) && strlen(_ssid) > 0) {
 LOG_CODE(LOG_WARN, "NET", NET_CONNECT_TIMEOUT, 0, TRL("AP mode timeout, rebooting to STA"));
 watchdog_update( );
 LogManager::instance( ).safeReboot( );
 }
 return LINK_DONE_NOW;
 }
 /* The NTP wait is the host's and does not look at the link, so the radio
  * watches it here, as the cable's updateWired( ) does. Only NET_READY used to:
  * on the bench on 2026-10-13 the access point went away 1.1 s after the
  * address, before the first NTP answer, and the device sat in the wait for
  * 371 s with no network record at all, three and a half minutes of them with
  * the access point back, until a reboot. An NTP answer is what ends the
  * wait, and none comes without a link. The record is NET_READY's: an address
  * and no SYS_NTP_SYNC before it in the log say which state it came from. */
 if (_state == NET_CONNECTED_WAIT_NTP && WiFi.status( ) != WL_CONNECTED) wifiLost( );
 return LINK_GO_ON;
}

/**
 * @brief The radio's states: the scan and the join, the wait for an address, the link
 * watch of NET_READY, the pause before a retry. NET_CONNECTED_WAIT_NTP is the host's,
 * which runs it before this and for every driver.
 */
void NetworkManager::updateLinkStates( ) {
 switch (_state) {
 case NET_OFFLINE:

 if (timeSince(_reconnectTimer, _reconnectDelay)) {
 if (strlen(_ssid) > 0) {
 LOG_CODE(LOG_INFO, "NET", SYS_WIFI_SCAN, 0, String(TRL("Scanning for SSID (backoff=")) + (_reconnectDelay/1000) + "s)");
 /* Timed, because the return value cannot tell a silent radio from a
  * working one (CYW43lwIP::scanNetworks answers -1 for any async start,
  * and 0 for a refusal the driver already swallowed). The duration can:
  * the "escan" iovar is one synchronous round trip, milliseconds on a chip
  * that answers and the driver's full timeout on one that does not. The
  * value is only read if this scan never finishes — see
  * WIFI_SCAN_START_SILENT_MS for the field log that fixed the threshold. */
 const uint32_t scanAskedAt = millis( );
 WiFi.scanNetworks(true);
 _scanStartTookMs = millis( ) - scanAskedAt;
 if (_scanStartTookMs < WIFI_SCAN_START_SILENT_MS) {
 _silentScans = 0; /* the radio answered: a silent streak, if any, is over */
 }
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
 /* Two radios reach this line and only one of them may be joined. A
  * start the chip answered (milliseconds) and a completion it lost is
  * the wedge above: the chip is fine, and the blind join is the escape.
  * A start the chip did NOT answer (the driver's own timeout) is a
  * silent radio, and WiFi.begin( ) on it is up to 15 synchronous ioctl
  * timeouts of 1 s before any bounded wait — the HW-watchdog reboots of
  * 2026-09-30 and 2026-10-11 09:28 (WIFI_SCAN_START_SILENT_MS). The ctx
  * counts the streak the record belongs to: misses toward the blind
  * join, or silent starts toward the planned restart. */
 const bool silent = (_scanStartTookMs >= WIFI_SCAN_START_SILENT_MS);
 /* The duration rides on the console text only (the TRL literal and
  * its pack entries are untouched): it is the number that says which
  * of the two radios this was, and the bench will want to read it. */
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_SCAN,
 (int)((silent ? _silentScans : _blindScans) + 1),
 String(TRL("Scan never finished — abandoning it")) +
 " (start took " + _scanStartTookMs + " ms)");
 WiFi.scanDelete( );
 if (silent) afterSilentScan( );
 else afterFruitlessScan( );
 }
 return;
 }

 /* The scan came back, with or without the SSID: the radio answered. */
 _silentScans = 0;

 if (n < -1) { afterFruitlessScan( ); return; }

 bool found = false;
 for (int i = 0; i < n; i++) {
 if (strcmp(WiFi.SSID(i), _ssid) == 0) { found = true; break; }
 }
 WiFi.scanDelete( );

 if (found) {
 LOG_CODE(LOG_INFO, "NET", SYS_WIFI_CONNECT, 0, TRL("SSID found, connecting..."));
 _blindScans = 0;
 beginJoin( );
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
#if !SIMUT_AIR
 announceService( );
#endif
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

 case NET_READY:
 if (WiFi.status( ) != WL_CONNECTED) {
 wifiLost( );
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
 * @brief The link is gone, from NET_READY or from the NTP wait: say so, let go of
 * the association, and start the ladder from its first rung (the cable's is
 * wiredLost( )).
 */
void NetworkManager::wifiLost( ) {
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_DISCONNECT, 0, TRL("WiFi signal lost, entering stealth scan"));
 WiFi.disconnect(false);
 resetReconnectLadder( );
 resetNtpBackoff( ); /* Next reconnection starts from initial delay */
 _state = NET_DISCONNECT_PENDING;
 _stateTimer = millis( );
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
 beginJoin( );
 _state = NET_CONNECTING;
 _stateTimer = millis( );
 } else {
 _state = NET_OFFLINE;
 _reconnectTimer = millis( );
 }
}

/**
 * @brief Leave a scan that never finished on a radio that did not answer it.
 *
 * Kept apart from afterFruitlessScan( ) on purpose. A miss is a radio that
 * answered without the SSID, and associating anyway is the right escape. A
 * silent radio is one that did not answer the scan's own ioctl, and
 * WiFi.begin( ) on it holds Core 0 through up to 15 synchronous ioctl timeouts
 * of 1 s before any bounded wait: the HW-watchdog reboots of 2026-09-30 and
 * 2026-10-11 09:28 (WIFI_SCAN_START_SILENT_MS has the log and the count). So
 * no join from here, ever. The second silent scan in a row restarts the radio
 * by the planned path — log persisted, Core 1 parked — under the same
 * half-hour guard as the refused joins (takeJoinAnswer); inside the guard the
 * ladder keeps scanning, one bounded ioctl per attempt, the streak keeps
 * counting, and the restart comes on the first silent scan past the guard.
 */
void NetworkManager::afterSilentScan( ) {
 if (_silentScans < 255) _silentScans++; /* saturates, like the other counters */

 if (_silentScans >= WIFI_SILENT_SCANS_BEFORE_RESTART &&
     millis( ) >= WIFI_RADIO_RESTART_MIN_UPTIME_MS) {
 _silentScans = 0;
 /* Fixed English like the ctx 2/3 lines of takeJoinAnswer: an operator's
  * line, and the language packs are at their RAM ceiling. The duration is
  * the evidence the bench will want — it survives on the console only. */
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_CONNECT, 4,
          String("Radio silent on two scans (start took ") + _scanStartTookMs +
          " ms) - restarting to recover it");
 watchdog_update( );
 LogManager::instance( ).safeReboot( ); /* [[noreturn]] */
 }

 /* Back to the ladder. Deliberately NOT afterFruitlessScan( ): a silent
  * start must never count toward the blind join. */
 _state = NET_OFFLINE;
 _reconnectTimer = millis( );
}

/**
 * @brief Ask the radio to associate, and plan the restart a refusing radio needs.
 *
 * WiFi.begin( ) queues the join and returns; update( ) polls for the outcome.
 * WL_IDLE_STATUS back means the radio did not take the request at all, which a
 * weak signal never causes (see WIFI_JOIN_REFUSALS_BEFORE_RESTART). On the
 * field device of 2026-09-30 the refusal held Core 0 inside the framework past
 * the watchdog, and the watchdog's reboot is what recovered the radio. With
 * the framework's wait now bounded, the same recovery is planned here: logged
 * (SYS_WIFI_CONNECT ctx 2 per refusal, ctx 3 before the restart), persisted by
 * safeReboot( )'s pre-reboot hook, and not within the first half hour after a
 * boot. The text is fixed English: the language packs are at their RAM
 * ceiling, and this is an operator's line.
 */
void NetworkManager::beginJoin( ) {
 takeJoinAnswer(WiFi.begin(_ssid, _pass));
}

/** @brief What a join's answer means: WL_IDLE_STATUS is a refusal, counted and
 *  logged, and the third in a row past the first half hour restarts. Apart
 *  from beginJoin( ) so that the rule can be fed an answer without a join of
 *  its own; the bench feeds it one the radio is sure to refuse. */
void NetworkManager::takeJoinAnswer(int status) {
 if (status != WL_IDLE_STATUS) {
 _joinRefusals = 0;
 return;
 }
 if (_joinRefusals < 255) _joinRefusals++;
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_CONNECT, 2, "The radio refused the join");
 if (_joinRefusals >= WIFI_JOIN_REFUSALS_BEFORE_RESTART &&
     millis( ) >= WIFI_RADIO_RESTART_MIN_UPTIME_MS) {
 _joinRefusals = 0;
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_CONNECT, 3,
          "The radio refused 3 joins in a row - restarting to recover it");
 watchdog_update( );
 LogManager::instance( ).safeReboot( );
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
 /* Until 2026-10-01 a whole round of the ladder ended here by asking for the
  * setup AP (2.7.1), and the first dormancy of a unit that never had an
  * address asked for it at once — measured on the rig at 6–7 min. The
  * maintainer's decision that day: the AP opens when a person asks, from
  * Settings or with the `ap` command, never because the network is away. A
  * unit whose network is away keeps measuring and keeps trying it, on this
  * ladder, for as long as it takes. */
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

#endif /* SIMUT_NET_CYW43 */
