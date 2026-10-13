/**
 * @file NetworkManager_Wired.cpp
 * @brief The SIMUT Wire's side of NetworkManager: the ENC28J60 in place of the
 *        CYW43 (SIMUT_NET_LAN).
 *
 * The Wi-Fi path finds an SSID, joins it and waits for an address. This one
 * starts the module (net/WiredLink) and waits for the link and the address,
 * by DHCP or the static one of the configuration. From the address on the two
 * paths meet: the NTP wait is the NET_CONNECTED_WAIT_NTP case of update( ),
 * which updateWired( ) leaves to it.
 *
 * The states are the Wi-Fi's, and only four of them happen here: NET_OFFLINE
 * (the module did not start; retried on the reconnect ladder), NET_CONNECTING
 * (started, waiting for the link and an address), NET_CONNECTED_WAIT_NTP and
 * NET_READY. A cable pulled out goes back to NET_CONNECTING and nothing more:
 * the interface stays up, and lwIP renews the lease when the link returns.
 *
 * @project SIMUT
 * @license MIT License
 */
#include "NetworkManager.h"

#if SIMUT_NET_LAN
#include "MetricsManager.h"
#include "net/WiredLink.h"

namespace {

/* A cable has no signal strength. A link that is up answers getRssi( ) with
 * this stand-in, and -100 means no network, as on Wi-Fi: the screens draw a
 * cable from it (above -100, the network glyph lit; DisplayManager_Dashboard.cpp,
 * BigFont_HD44780.h), and it clears RSSI_MIN_THRESHOLD (-78), which
 * isNetworkHealthy( ) applies, inside the implausibility bounds. */
constexpr int32_t RSSI_WIRED = -40;

/* The reconnect ladder's top for a module that does not start: a missing or
 * miswired ENC28J60 answers begin( ) in milliseconds, so a minute between tries
 * costs nothing. The log says it once per episode (_wiredFailLogged). */
constexpr uint32_t WIRED_RETRY_MAX_MS = 60000;

/* A link that has no address after this long says so, once: a cable into a
 * network with no DHCP server, or a static address on the wrong subnet, would
 * otherwise wait in silence. */
constexpr uint32_t WIRED_ADDRESS_WAIT_MS = 30000;

}  // namespace

void NetworkManager::beginWired(const SystemConfig& cfg) {
 _wiredStatic = !cfg.useDhcp && strlen(cfg.staticIp) > 7;
 if (_wiredStatic) {
  _wiredIp.fromString(cfg.staticIp);
  _wiredMask.fromString(cfg.staticMask);
  _wiredGw.fromString(cfg.staticGateway);
  _wiredDns.fromString(cfg.staticDns);
  LOG_CODE(LOG_INFO, "NET", NET_STATIC_MODE, 0, "");
 } else {
  LOG_CODE(LOG_INFO, "NET", NET_DHCP_MODE, 0, "");
 }
 startWired( );
}

/** Start the module: NET_CONNECTING, or NET_OFFLINE to try again later. */
void NetworkManager::startWired( ) {
 LOG_CODE(LOG_INFO, "NET", NET_STARTING, 0, "");
 const IPAddress none;
 const bool ok = _wiredStatic
  ? WiredLink::begin(_deviceName, _wiredIp, _wiredGw, _wiredMask, _wiredDns)
  : WiredLink::begin(_deviceName, none, none, none, none);
 if (ok) {
  _wiredFailLogged = false;
  _wiredWaitLogged = false;
  _state = NET_CONNECTING;
  _stateTimer = millis( );
  return;
 }
 if (!_wiredFailLogged) {
  LOG_CODE(LOG_ERROR, "NET", NET_CONNECT_TIMEOUT, 0, String("ENC28J60: ") + WiredLink::lastError( ));
  _wiredFailLogged = true;
 }
 _state = NET_OFFLINE;
 _reconnectTimer = millis( );
 _reconnectDelay = min(_reconnectDelay * 2, WIRED_RETRY_MAX_MS);
}

/**
 * One pass of the wired states. True when it handled the state; false leaves
 * it to the switch of update( ), which is the NTP wait both paths share.
 */
bool NetworkManager::updateWired( ) {
 const bool online = WiredLink::linkUp( ) && WiredLink::connected( );
 switch (_state) {
 case NET_OFFLINE:
  if (timeSince(_reconnectTimer, _reconnectDelay)) startWired( );
  return true;
 case NET_CONNECTING:
 case NET_CONNECTED_WAIT_IP:
  if (online) {
   wiredUp( );
  } else if (!_wiredWaitLogged && timeSince(_stateTimer, WIRED_ADDRESS_WAIT_MS)) {
   LOG_CODE(LOG_WARN, "NET", NET_CONNECT_TIMEOUT, (int)(WIRED_ADDRESS_WAIT_MS / 1000),
            WiredLink::linkUp( ) ? "Wired network: link up, no address yet"
                                 : "Wired network: no link (cable?)");
   _wiredWaitLogged = true;
  }
  return true;
 case NET_CONNECTED_WAIT_NTP:
  if (!online) {
   wiredLost( );
   return true;
  }
  return false;
 case NET_READY:
  if (!online) {
   wiredLost( );
  } else if (WiredLink::localIP( ) != _wiredAnnounced) {
   /* The lease changed under a link that stayed up, or the cable came back
    * on another network after lwIP had kept the old address: announce the
    * new one (log, mDNS, the clock) as if it had just arrived. */
   wiredUp( );
  }
  return true;
 default:
  return true;
 }
}

/**
 * The link is up and has an address. The NET_CONNECTED_WAIT_IP case of
 * update( ), in its order: the log line, the manual DNS, mDNS, then the clock.
 */
void NetworkManager::wiredUp( ) {
 _wiredAnnounced = WiredLink::localIP( );
 _wiredWaitLogged = false;
 LOG_CODE(LOG_INFO, "NET", SYS_IP_ACQUIRED, 0, "IP: " + _wiredAnnounced.toString( ));
 MetricsManager::instance( ).data( ).wifiReconnects++;
 applyManualDnsIfNeeded( );
#if SIMUT_MDNS
 if (_mdnsEnabled) {
  if (!MDNS.begin(_deviceName)) LOG_CODE(LOG_ERROR, "NET", NET_MDNS_FAIL, 0, TRL("mDNS failed to start"));
 }
 announceService( );
#endif
 if (_ntpEnabled) {
  syncNtp( );
  _state = NET_CONNECTED_WAIT_NTP;
  _stateTimer = millis( );
 } else {
  LOG_CODE(LOG_INFO, "NET", SYS_NTP_SYNC, 0, TRL("NTP disabled — manual RTC mode"));
  _state = NET_READY;
  resetReconnectLadder( );
  _connectCycles = 0;
 }
}

/** The cable came out, or the address went away: wait for both again. */
void NetworkManager::wiredLost( ) {
 LOG_CODE(LOG_WARN, "NET", SYS_WIFI_DISCONNECT, 0,
          WiredLink::linkUp( ) ? "Wired network: address lost" : "Wired network: link down");
 resetNtpBackoff( );
 _state = NET_CONNECTING;
 _stateTimer = millis( );
}

String NetworkManager::getIpAddress( ) { return WiredLink::localIP( ).toString( ); }
void NetworkManager::getIpAddress(char* buf, size_t len) {
 IPAddress ip = WiredLink::localIP( );
 snprintf(buf, len, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}
String NetworkManager::getMacAddress( ) {
 char buf[18];
 getMacAddress(buf, sizeof(buf));
 return String(buf);
}
/** The format of WiFi.macAddress( ), which the Wi-Fi images answer with. */
void NetworkManager::getMacAddress(char* buf, size_t len) {
 uint8_t m[6];
 WiredLink::macAddress(m);
 snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}
String NetworkManager::getSubnetMask( ) { return WiredLink::subnetMask( ).toString( ); }
String NetworkManager::getGateway( ) { return WiredLink::gatewayIP( ).toString( ); }
String NetworkManager::getDns( ) { return WiredLink::dnsIP( ).toString( ); }
int32_t NetworkManager::getRssi( ) { return isConnected( ) ? RSSI_WIRED : -100; }

#endif /* SIMUT_NET_LAN */
