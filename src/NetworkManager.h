/**
 * @file NetworkManager.h
 * @brief WiFi connectivity, NTP synchronization, and Virtual RTC management.
 * @details Handles the complete network lifecycle: WiFi STA/AP modes, exponential
 * backoff reconnection, NTP time sync, mDNS, and a Virtual RTC that
 * provides provisional timestamps from the last flash-stored epoch
 * until real NTP synchronization completes.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include "simut_config.h"   /* SIMUT_MDNS — must precede the #if below */
#include <WiFi.h>
#include <time.h>
/* LEAmDNS is optional and ON by default (src/simut_config.h). Measured cost:
 * 15,272 B of flash, 238 symbols — the "~196KB" this comment used to claim was
 * wrong by 13x and had been repeated into the user manual.
 *
 * `#if`, not `#ifdef`: simut_config.h presents this as a 0/1 switch like every
 * other feature flag there, and `#ifdef` is true for `0` as well, so setting
 * SIMUT_MDNS=0 disabled nothing. */
#if SIMUT_MDNS
#include <LEAmDNS.h>
#endif
#include <DNSServer.h>
#include "SystemDefs.h"
#include "LogManager.h"


typedef void (*TimeSyncCallback)(uint32_t bootProvisionalTs, int32_t deltaSeconds);

class NetworkManager {
public:
 NetworkManager( );

 /* Additional flags read from the NetworkTimeData overlay in
 * StorageManager. Passed explicitly (instead of a reference to
 * StorageManager) to keep NetworkManager decoupled. */
 void begin(const SystemConfig &cfg,
 bool dnsAuto = true,
 bool ntpEnabled = true,
 const char* dns2 = "");
 void beginAP(const char* deviceName);
 void update( );


 /** Seed the provisional clock from the last timestamp on flash.
  *
  * @param lastTs     epoch of the last record written.
  * @param elapsedSec how much time is known to have passed since it. The 60 s
  *        default is the historical guess, adequate only because a device that
  *        boots normally reaches NTP within seconds. SIMUT Air passes the real
  *        figure: with the radio raised once every N wakes, most records are
  *        stamped by this clock and never corrected, so a fixed guess would
  *        write the interval it assumed instead of the one that elapsed. */
 void setProvisionalTime(uint32_t lastTs, uint32_t elapsedSec = 60);
 void setTimeSyncCallback(TimeSyncCallback cb);

 /* Manual RTC set (via settimeofday) for when NTP
 * is disabled. `epoch` is UTC time in seconds (client converts
 * local time to epoch using the agreed TZ). No-op if epoch <= 0. */
 void setManualTime(time_t epoch);


 bool isConnected( );
 bool isApConfig( ) const { return _state == NET_AP_CONFIG; } /**< True while serving the setup Access Point — forces HTTP so a bad TLS cert cannot lock the recovery UI. */
 bool isTimeSynced( );
 int32_t getRssi( );

 /**
 * @brief Check if network is healthy for heavy operations.
 *
 * Combines WiFi connection state with signal quality (RSSI).
 * Light operations (NTP, mDNS) use isConnected( ).
 * Heavy operations (telemetry, uploads) should use isNetworkHealthy( ).
 *
 * @return true if connected AND signal above RSSI_MIN_THRESHOLD.
 */
 bool isNetworkHealthy( );

 /** Consecutive failed connection attempts; reset to 0 on a full connection.
  *  Exposed so the Air cycle can stop pumping the network after a bounded
  *  number of tries instead of letting a missing SSID hold a wake open. */
 uint8_t getConnectCycles( ) const { return _connectCycles; }


 String getIpAddress( );
 void getIpAddress(char* buf, size_t len); /**< Buffer version. */
 String getMacAddress( );
 void getMacAddress(char* buf, size_t len); /**< Buffer version. */
 String getSubnetMask( );
 String getGateway( );
 String getDns( );


 String getFormattedTime( );
 String getFormattedDate( );
 time_t getEpoch( );

 /**
 * @brief Apply timezone globally via setenv("TZ", ...).
 *
 * After calling, all localtime_r( ) returns local time automatically.
 * Must be called on boot and whenever timezone is changed.
 *
 * @param offset Offset in hours relative to UTC (ex: -3 for Brazil).
 */
 static void applyTimezone(int8_t offset);

 /**
 * @brief True when the clock in force came from real time, not the seed.
 *
 * The provisional clock is a guess extrapolated from the newest record on
 * flash; NTP (or a manual set) replaces it with real time and clears the
 * flag. Everything stamped while this returns false inherits whatever error
 * the seed carried, which is why the history writer records the answer
 * alongside the block it stamps — at the next boot it is the only way to
 * tell a snapshot that may be believed from one that may not.
 * See H5_FLAG_CLOCK_SYNCED.
 */
 bool isTimeTrusted( ) const {
 return !_provisionalActive && time(nullptr) > 1600000000;
 }

private:
 enum NetState {
 NET_OFFLINE,
 NET_CONNECTING,
 NET_CONNECTED_WAIT_IP,
 NET_CONNECTED_WAIT_NTP,
 NET_READY,
 NET_AP_CONFIG,
 NET_SCANNING_RETRY,
 NET_DISCONNECT_PENDING
 };

 NetState _state;
 DNSServer _dnsServer;

 char _ssid[32];
 char _pass[32];
 char _deviceName[32];
 char _ntpServer[32];
 int8_t _tzOffset;

 /* Runtime flags applied on boot. */
 bool _dnsAuto = true;
 bool _ntpEnabled = true;
 bool _useDhcp = true;
 char _staticDns1[16] = {0};
 char _staticDns2[16] = {0};

 uint32_t _stateTimer;
 uint32_t _retryTimer;
 uint32_t _reconnectTimer;
 uint32_t _reconnectDelay = 5000;
 /* constexpr, not const: `min(x, MAX_RECONNECT_DELAY)` binds a reference and
  * therefore ODR-uses it. At -Os the value is folded and no symbol is needed;
  * at -O0 the debug build failed to link. constexpr members are implicitly
  * inline under gnu++17, so no out-of-line definition is required. */
 static constexpr uint32_t MAX_RECONNECT_DELAY = 120000;


 TimeSyncCallback _timeSyncCb = nullptr;
 uint32_t _provisionalBase = 0;
 uint32_t _provisionalBootMillis = 0;
 bool _provisionalActive = false;

 void handleConnecting( );
 void syncNtp( );
 void resetNtpBackoff( ); /**< Reset backoff after success/reconnect */

 /* Apply manual DNS (primary and/or secondary) via lwIP
 * after IP acquired. No-op when dnsAuto=true and useDhcp=true. */
 void applyManualDnsIfNeeded( );

#if SIMUT_MDNS
 uint32_t _lastMdnsUpdate = 0; /**< Throttle for MDNS.update( ) */
#endif
 uint8_t _connectCycles = 0; /**< Consecutive reconnection cycles */

 /* ── NTP retry with exponential backoff + fallback ── */
 uint32_t _ntpRetryDelay = 20000; /**< Current delay between retries (ms) */
 uint8_t _ntpFailCount = 0; /**< Consecutive failures (reset on sync) */
 bool _ntpFallbackDone = false; /**< True after fallback to pool.ntp.org */

 /* ── AP mode timeout ── */
 uint32_t _apStartTime = 0; /**< millis( ) when entering AP mode */
};
