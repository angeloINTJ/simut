/**
 * @file WiFi.h (native stub)
 * @brief A controllable radio, so the reconnect state machine can be tested.
 *
 * This is not a simulation of cyw43. It is a set of knobs shaped exactly like
 * the failures that were observed on real devices, because those are the ones
 * the state machine has to survive:
 *
 *   - `scanNeverCompletes` — scanComplete( ) answers -1 forever. This is the
 *     2026-09-08 field failure: cyw43_wifi_scan( ) sets wifi_scan_state = 1
 *     before issuing the low-level scan and never clears it if that call
 *     fails, and nothing in the SDK times the state out. The device sat in
 *     NET_SCANNING_RETRY for 3 h 41 min and came back only on a power cycle.
 *
 *   - `visible` — whether the access point is listed by a scan, kept separate
 *     from `joinable`, whether the device can actually associate with it.
 *     Splitting them is the whole point: at the edge of a cell, and for a
 *     hidden SSID at any signal, an AP is joinable while invisible, and the
 *     old reconnect path refused to try.
 *
 * Everything is observable: joinAttempts counts the calls the state machine
 * makes, so a test can assert that it tried at all, which is what the field
 * failure came down to.
 *
 * @project SIMUT
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <string.h>

typedef enum {
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL,
    WL_SCAN_COMPLETED,
    WL_CONNECTED,
    WL_CONNECT_FAILED,
    WL_CONNECTION_LOST,
    WL_DISCONNECTED,
    WL_NO_MODULE
} wl_status_t;

typedef enum { WIFI_OFF = 0, WIFI_STA, WIFI_AP, WIFI_AP_STA } WiFiMode_t;

/** Values scanComplete( ) can carry, named so tests read as prose. */
enum { SCAN_RUNNING = -1, SCAN_FAILED = -2 };

class FakeWiFiClass {
public:
    /* ── knobs ─────────────────────────────────────────────────────────── */
    bool     visible          = true;   /**< AP appears in a scan */
    bool     joinable         = true;   /**< association would succeed */
    bool     scanNeverCompletes = false;/**< the wedge: scanComplete( ) stays -1 */
    bool     scanFailsToStart = false;  /**< scanComplete( ) answers -2 */
    uint32_t scanDurationMs   = 1000;   /**< how long a healthy scan takes */
    uint32_t joinDurationMs   = 500;    /**< how long a successful join takes */
    int32_t  rssi             = -60;
    const char* apSsid        = "bench-ap";

    /* ── observations ──────────────────────────────────────────────────── */
    unsigned joinAttempts   = 0;  /**< begin( ) calls — did it even try? */
    unsigned scanStarts     = 0;
    unsigned disconnects    = 0;
    unsigned scanDeletes    = 0;
    WiFiMode_t lastMode     = WIFI_OFF;
    String   lastSsid;

    /** Put the radio back the way a fresh test finds it. */
    void reset( ) {
        visible = joinable = true;
        scanNeverCompletes = scanFailsToStart = false;
        scanDurationMs = 1000; joinDurationMs = 500;
        rssi = -60; apSsid = "bench-ap";
        joinAttempts = scanStarts = disconnects = scanDeletes = 0;
        lastMode = WIFI_OFF; lastSsid = String("");
        _joining = false; _associated = false;
        _scanStartedAt = _joinStartedAt = 0; _scanning = false;
    }

    /* ── the API NetworkManager uses ───────────────────────────────────── */
    void mode(WiFiMode_t m) { lastMode = m; }

    void config(IPAddress, IPAddress, IPAddress, IPAddress) {}

    int begin(const char* ssid, const char* /*pass*/) {
        joinAttempts++;
        lastSsid = String(ssid);
        _joining = true;
        _associated = false;
        _joinStartedAt = millis( );
        return WL_IDLE_STATUS;
    }

    int disconnect(bool /*wifiOff*/ = false) {
        disconnects++;
        _joining = false;
        _associated = false;
        return WL_DISCONNECTED;
    }

    uint8_t status( ) {
        /* A join that was going to succeed lands after joinDurationMs. One
         * that was not simply never lands, which is what a device at the edge
         * of a cell sees — no error, just no association. */
        if (_joining && joinable &&
            (uint32_t)(millis( ) - _joinStartedAt) >= joinDurationMs) {
            _associated = true;
            _joining = false;
        }
        return _associated ? WL_CONNECTED : WL_DISCONNECTED;
    }

    int8_t scanNetworks(bool /*async*/ = false) {
        scanStarts++;
        _scanning = true;
        _scanStartedAt = millis( );
        return SCAN_RUNNING;
    }

    int8_t scanComplete( ) {
        if (!_scanning) return 0;
        if (scanNeverCompletes) return SCAN_RUNNING;      /* the wedge */
        if ((uint32_t)(millis( ) - _scanStartedAt) < scanDurationMs) return SCAN_RUNNING;
        if (scanFailsToStart) return SCAN_FAILED;
        return visible ? 1 : 0;
    }

    void scanDelete( ) { scanDeletes++; _scanning = false; }

    const char* SSID(uint8_t i) { return i == 0 && visible ? apSsid : ""; }

    int32_t RSSI( ) { return rssi; }

    IPAddress localIP( )    { return _associated ? IPAddress(192,168,1,50) : IPAddress( ); }
    IPAddress subnetMask( ) { return IPAddress(255,255,255,0); }
    IPAddress gatewayIP( )  { return IPAddress(192,168,1,1); }
    IPAddress dnsIP( )      { return IPAddress(192,168,1,1); }
    IPAddress softAPIP( )   { return IPAddress(192,168,4,1); }
    String macAddress( )    { return String("AA:BB:CC:DD:EE:FF"); }

    void softAPConfig(IPAddress, IPAddress, IPAddress) {}
    bool softAP(const char*, const char* = nullptr) { return true; }

private:
    bool     _joining = false;
    bool     _associated = false;
    bool     _scanning = false;
    uint32_t _scanStartedAt = 0;
    uint32_t _joinStartedAt = 0;
};

extern FakeWiFiClass WiFi;

/** arduino-pico's NTP entry point; a host test has no SNTP client. */
inline void configTime(long, int, const char*, const char* = nullptr) {}
