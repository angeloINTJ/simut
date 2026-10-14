/**
 * @file NetworkManager.cpp
 * @brief Implementation of NetworkManager — the host of the network: NTP and its
 *        backoff, the provisional and manual clock, mDNS, the timezone, and
 *        the formatted time.
 * @details What every network needs, whichever link the image has. The link
 * itself — how the image joins a network and stays on it — is a unit of
 * src/net/ that defines the contract in NetworkManager.h: the radio's
 * (net/NetworkManager_Cyw43.cpp: async scan, exponential backoff, dormancy,
 * the setup AP) or the cable's (net/NetworkManager_Wired.cpp). The manifest
 * leaves the one the image does not have out of the build.
 * NTP sync with provisional time correction callback, and timezone-aware
 * date/time formatting, are here.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "NetworkManager.h"
#include "simut_config.h"   /* SIMUT_MDNS and SIMUT_AIR are tested below: included here, not left to another header */
#include "SimutTime.h"   /* simutTimeSetOffset — the zone this applies */
#include <stdlib.h>
#include <sys/time.h> /* settimeofday for manual RTC */
#include <lwip/dns.h> /* dns_setserver for manual DNS */
#include <lwip/ip_addr.h>
#include "pico/unique_id.h"           /* the board id in the mDNS TXT record */
#include "BuildIdentity.h"            /* SIMUT_VERSION / env for the mDNS TXT record */

NetworkManager::NetworkManager( ) {
 _state = NET_OFFLINE;
 _stateTimer = 0;
 _reconnectTimer = 0;
}

/**
 * @brief Initialize the network: copy the settings every network uses, apply the
 * timezone, and start the link (beginLink( ), the unit's: the radio's station
 * mode with an optional static IP and the join, or the cable's module).
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

 beginLink(cfg);
}

/**
 * @brief Set provisional time from the last Flash-stored timestamp.
 * Provides approximate timestamps until NTP sync completes (Virtual RTC).
 */
void NetworkManager::setProvisionalTime(uint32_t lastTs, uint32_t elapsedSec) {
 if (lastTs > 1600000000) setProvisionalNow(lastTs + elapsedSec, 0);
}

void NetworkManager::setProvisionalNow(uint32_t sec, uint16_t ms) {
 if (sec <= 1600000000 || ms > 999) return;
 _provisionalBase = sec;
 /* Backdating the anchor by the fraction is the whole point: getEpoch( )
  * divides (millis( ) - anchor) by 1000, so the second rolls over exactly
  * (1000 - ms) ms from now instead of a full second from now. */
 _provisionalBootMillis = millis( ) - ms;
 _provisionalActive = true;
 LOG_CODE(LOG_INFO, "NET", NET_PROVISIONAL_TIME, 0, String(TRL("Provisional: ")) + getFormattedDate( ) + " " + getFormattedTime( ));
}

bool NetworkManager::getEpochMs(uint32_t& sec, uint16_t& ms) {
 struct timeval tv;
 gettimeofday(&tv, nullptr);
 if (tv.tv_sec > 1600000000) {
  sec = (uint32_t)tv.tv_sec;
  ms  = (uint16_t)(tv.tv_usec / 1000);
  return true;
 }
 if (!_provisionalActive) return false;
 const uint32_t el = millis( ) - _provisionalBootMillis;
 sec = _provisionalBase + el / 1000UL;
 ms  = (uint16_t)(el % 1000UL);
 return sec > 1600000000;
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
void NetworkManager::setManualTime(time_t epoch, uint8_t source) {
 if (epoch <= 1600000000) return; /* Reject obviously invalid value. */
 /* Read the seed BEFORE the system clock moves: endProvisional( ) compares the
  * two. It used to be `_provisionalActive = false` and nothing else, so what
  * the seed had stamped this boot kept the seed's error — while the clock, now
  * trusted, sealed the block holding it as synced. NTP has corrected that since
  * V5; a person setting the clock is the same event and now takes the same
  * path. Found 2026-10-01 (test_network) while making the panel ask for the
  * time on a unit with no network, where a person is the only clock there is. */
 const uint32_t realNow = (uint32_t)epoch;
 struct timeval tv;
 tv.tv_sec = epoch;
 tv.tv_usec = 0;
 settimeofday(&tv, nullptr);
 endProvisional(realNow);
 LOG_CODE(LOG_INFO, "NET", SYS_NTP_SYNC, source,
 TRL("RTC set manually"));
}

void NetworkManager::endProvisional(uint32_t realNow) {
 if (!_provisionalActive) return;
 const uint32_t provTime = _provisionalBase + ((millis( ) - _provisionalBootMillis) / 1000);
 const int32_t delta = (int32_t)(realNow - provTime);
 /* Five seconds is not worth rewriting a day file for. */
 if (_timeSyncCb && abs(delta) > 5) {
  _timeSyncCb(_provisionalBase, delta);
 }
 _provisionalActive = false;
}

/**
 * @brief Network state machine — the part every network shares.
 * Must be called frequently from the main loop.
 *
 * The link itself is the unit's: updateLinkFirst( ) before everything and
 * updateLinkStates( ) after the NTP wait (the contract is in NetworkManager.h).
 * In between, the same for every driver and in this order: the mDNS update, and
 * the wait for NTP, which both reach from NET_CONNECTED_WAIT_IP (the radio in
 * updateLinkStates( ), the cable in wiredUp( )).
 */
void NetworkManager::update( ) {
 const LinkFirst first = updateLinkFirst( );
 if (first == LINK_DONE_NOW) return;

 /* mDNS: update only when connected and throttled at 2s.
  * ON by default; set SIMUT_MDNS=0 in src/simut_config.h to drop it and
	 * recover 15,272 B — not the ~196KB this comment used to claim. */
#if SIMUT_MDNS
 if (_mdnsEnabled && _state == NET_READY) {
 uint32_t now = millis( );
 if (now - _lastMdnsUpdate >= MDNS_UPDATE_INTERVAL_MS) {
 _lastMdnsUpdate = now;
 MDNS.update( );
#if !SIMUT_AIR
	 /* Depois do update, não antes: o serviço registrado enquanto a sondagem
	  * do hostname ainda corria ficava mudo (bancada, 11/09). Idempotente —
	  * a partir do primeiro sucesso isto é um teste de bool. */
 announceService( );
#endif
 }
 }
#endif

 if (first == LINK_DONE_AFTER_MDNS) return;

 if (_state == NET_CONNECTED_WAIT_NTP) {
 if (time(nullptr) > 1600000000) {
 LOG_CODE(LOG_INFO, "NET", SYS_NTP_SYNC, 0, "NTP OK: " + getFormattedDate( ) + " " + getFormattedTime( ));


 endProvisional((uint32_t)time(nullptr));

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
 return;
 }
 updateLinkStates( );
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
 _silentScans = 0; /* a new episode starts with the radio presumed to answer */
}

/**
 * @brief Configure NTP with customizable server and system timezone.
 *
 * The internal RTC stays in UTC (configTime with offset 0).
 * The timezone is applied via applyTimezone( ) so that localtime_r( ) returns
 * local time throughout the system.
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
 * The sign here is the natural one — -3 for Brazil — and no longer the
 * inverted POSIX TZ notation, because no TZ string is built any more.
 * After calling, every localtime_r( ) returns local time correctly.
 *
 * @param offset Offset in hours (ex: -3 for Brazil, +9 for Japan).
 */
void NetworkManager::applyTimezone(int8_t offset) {
 /* This used to write a POSIX TZ string ("UTC3" for -03, sign inverted) into
  * the environment and call tzset( ). newlib then parsed that string back with
  * siscanf( ) on every zone change, which is how the integer scanf engine, the
  * TZ state machine and newlib's localtime/mktime — 13,188 B — ended up in an
  * image that only ever needed one whole-hour number. See SimutTime.h. */
 simutTimeSetOffset(offset);
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
/* Synced = NTP or a manual `time` set the clock: isTimeTrusted( ). It was
 * `getEpoch( ) > 1600000000`, and getEpoch( ) never answers less — with no NTP
 * it falls back to the provisional clock or to SIMUT_BUILD_EPOCH — so this was
 * true on every device, always: /api/status, /api/perms, the panel's status
 * screen and simut_ntp_synced said "synced" on units that never reached a time
 * server, and the calibration page's "needs NTP" refusal could not fire. Found
 * by reading while the v2.7.1 manual was written (finding 66); pinned by
 * test_network. */
bool NetworkManager::isTimeSynced( ) { return isTimeTrusted( ); }

bool NetworkManager::isNetworkHealthy( ) {
 return isConnected( ) && getRssi( ) > RSSI_MIN_THRESHOLD;
}

bool NetworkManager::isLinkHealthy( ) {
 return isLinkUp( ) && getRssi( ) > RSSI_MIN_THRESHOLD;
}

#if SIMUT_MDNS && !SIMUT_AIR
/**
 * Registra `_simut._tcp` com uid/ver/env/tls no TXT — descoberta por browse,
 * passiva, sem rota nova e sem tocar no servidor web.
 *
 * **Por que não fica junto do MDNS.begin( ).** Na bancada, registrar o serviço
 * na mesma passagem do `begin( )` deixou o responder de pé — `simuttft.local`
 * resolvia — e o serviço mudo: nenhuma resposta a um PTR de `_simut._tcp`, e
 * nenhum erro em lugar nenhum. O LEAmDNS sondeia o hostname de forma assíncrona
 * pelo `update( )`, e um serviço acrescentado antes disso terminar não é
 * anunciado. Aqui a chamada é idempotente e roda também quando a rede chega em
 * NET_READY, que é depois de a sondagem ter tido tempo.
 *
 * O `LOG_CODE` na falha carrega a porta no ctx de propósito: sem ele, "não
 * apareceu no browse" tem dois significados indistinguíveis — não chegamos
 * aqui, ou chegamos e o handle voltou nulo.
 */
void NetworkManager::announceService( ) {
 if (_advertised || !_mdnsEnabled || !_advPort) return;
 auto h = MDNS.addService(nullptr, "simut", "tcp", _advPort);
 if (!h) {
  LOG_CODE(LOG_WARN, "NET", NET_MDNS_FAIL, (int)_advPort, TRL("mDNS failed to start"));
  return;
 }
 pico_unique_board_id_t bid;
 pico_get_unique_board_id(&bid);
 char uid[17];
 snprintf(uid, sizeof(uid), "%02X%02X%02X%02X%02X%02X%02X%02X",
          bid.id[0], bid.id[1], bid.id[2], bid.id[3], bid.id[4], bid.id[5], bid.id[6], bid.id[7]);
 MDNS.addServiceTxt(h, "uid", uid);
 MDNS.addServiceTxt(h, "ver", SIMUT_VERSION);
 MDNS.addServiceTxt(h, "env", simut_env_name( ));
 MDNS.addServiceTxt(h, "tls", _advTls ? "1" : "0");
 _advertised = true;
}
#endif

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
