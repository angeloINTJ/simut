/* Host stand-in for the framework's WiFiClient. HTTPServer asks its client
 * for write( ), connected( ) and stop( ) only (grep _currentClient-> in
 * HTTPServer.h, HTTPServer.cpp and detail/RequestHandlersImpl.h); the test's
 * FakeClient implements the three and records what every write( ) carried. */
#pragma once
#include <Arduino.h>

class WiFiClient {
public:
    virtual ~WiFiClient() {}
    virtual size_t write(const uint8_t* buf, size_t size) = 0;
    size_t write(const char* buf, size_t size) {
        return write(reinterpret_cast<const uint8_t*>(buf), size);
    }
    /* streamFile( ): the framework's write(Stream&) copies the source through
     * a small buffer, one write( ) per read. */
    template <typename T>
    size_t write(T& source) {
        uint8_t b[256];
        size_t sent = 0;
        while (source.available() > 0) {
            const size_t n = source.read(b, sizeof(b));
            const size_t w = write(b, n);
            sent += w;
            if (w != n) {
                break;
            }
        }
        return sent;
    }
    virtual uint8_t connected() = 0;
    virtual bool stop(unsigned int maxWaitMs) = 0;
};
