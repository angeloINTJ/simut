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
#include <WiFi.h>
#include <time.h>
#include <LEAmDNS.h>
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


 void setProvisionalTime(uint32_t lastTs);
 void setTimeSyncCallback(TimeSyncCallback cb);

 /* Manual RTC set (via settimeofday) for when NTP
 * is disabled. `epoch` is UTC time in seconds (client converts
 * local time to epoch using the agreed TZ). No-op if epoch <= 0. */
 void setManualTime(time_t epoch);


 bool isConnected( );
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
 static const uint32_t MAX_RECONNECT_DELAY = 120000;


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

 uint32_t _lastMdnsUpdate = 0; /**< Throttle for MDNS.update( ) */
 uint8_t _connectCycles = 0; /**< Consecutive reconnection cycles */

 /* ── NTP retry with exponential backoff + fallback ── */
 uint32_t _ntpRetryDelay = 20000; /**< Current delay between retries (ms) */
 uint8_t _ntpFailCount = 0; /**< Consecutive failures (reset on sync) */
 bool _ntpFallbackDone = false; /**< True after fallback to pool.ntp.org */

 /* ── AP mode timeout ── */
 uint32_t _apStartTime = 0; /**< millis( ) when entering AP mode */
};
