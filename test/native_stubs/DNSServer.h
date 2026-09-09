/* Native stub — the captive-portal DNS server used only in AP mode. */
#pragma once
#include <Arduino.h>
class DNSServer {
public:
    bool start(uint16_t, const String&, const IPAddress&) { _running = true; return true; }
    void processNextRequest( ) { _polls++; }
    void stop( ) { _running = false; }
    bool _running = false;
    unsigned _polls = 0;
};
