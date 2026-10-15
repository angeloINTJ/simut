/* Host stand-in for <Arduino.h>: enough of it to compile the framework's
 * WebServer (HTTPServer.cpp, detail/mimetable.cpp) on the host, for
 * tools/test_webserver_out.py. The String is the framework's own
 * (ArduinoCore-API), so the code under test builds its headers and bodies
 * with the class it ships with. */
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cassert>
#include <functional>
#include <memory>
#include <vector>
#include "api/String.h"
#include "api/itoa.h"
using namespace arduino;

#ifndef FPSTR
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper *>(p))
#endif

unsigned long millis();
void delay(unsigned long ms);
void yield();

/* HTTPServer::_getRandomHexString( ) (Digest auth) reads the chip's RNG. */
struct HostRp2040 {
    unsigned long hwrand32() {
        return 0x5a5a5a5aUL;
    }
};
extern HostRp2040 rp2040;
