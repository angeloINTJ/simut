/**
 * @file BluetoothManager.h
 * @brief Bluetooth Serial CLI interface — optional, disabled by default.
 * @details When SIMUT_BLUETOOTH=0 (default), all methods are no-op stubs
 * that compile to nothing. Enable with -DSIMUT_BLUETOOTH=1 to restore
 * full SerialBT functionality.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <functional>
#include "sensors/SensorConfig.h"
#if SIMUT_BLUETOOTH
#include <SerialBT.h>
#endif
#include "SystemDefs.h"

/** Callback type for password validation — returns true if credentials match. */
typedef std::function<bool(String)> BtAuthValidator;

#if SIMUT_BLUETOOTH

class BluetoothManager {
public:
 BluetoothManager( );
 void begin(const char* deviceName = "SIMUT_CLI");
 bool isInitialized( ) const { return _initialized; }
 void setValidator(BtAuthValidator validator);
 void setLanguage(uint8_t lang) { _language = lang; }
 void update( );
 bool isAuthenticated( );
 void print(const String& msg);
 void println(const String& msg);
 void write(uint8_t c);
 bool available( );
 char read( );
private:
 bool _authenticated;
 bool _promptSent;
 String _authBuffer;
 BtAuthValidator _validator;
 uint8_t _language = LANG_EN;
 uint32_t _lastActivityTime;
 const uint32_t _timeoutMs = 300000;
 bool _initialized = false;
};

#else /* SIMUT_BLUETOOTH=0 — stub all methods */

class BluetoothManager {
public:
 BluetoothManager( ) {}
 void begin(const char* = "SIMUT_CLI") {}
 bool isInitialized( ) const { return false; }
 void setValidator(BtAuthValidator) {}
 void setLanguage(uint8_t) {}
 void update( ) {}
 bool isAuthenticated( ) { return false; }
 void print(const String&) {}
 void println(const String&) {}
 void write(uint8_t) {}
 bool available( ) { return false; }
 char read( ) { return 0; }
};

#endif /* SIMUT_BLUETOOTH */
