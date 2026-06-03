/**
 * @file BluetoothManager.h
 * @brief Bluetooth Serial interface with password authentication and auto-logout.
 * @details Manages BLE serial communication on the Pico W, providing a
 * password-protected CLI session with 5-minute inactivity timeout.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <functional>
#include <SerialBT.h>
#include "SystemDefs.h"


/** Callback type for password validation — returns true if credentials match. */
typedef std::function<bool(String)> BtAuthValidator;

class BluetoothManager {
public:
 BluetoothManager( );


 /** Initialize Bluetooth Serial with the given device name. */
 void begin(const char* deviceName = "SIMUT_CLI");

 /** alpha28: true depois de begin( ) — evita acessar SerialBT antes de init. */
 bool isInitialized( ) const { return _initialized; }

 void setValidator(BtAuthValidator validator);

 /** Idioma do banner pós-auth. Sincronizado com CommandManager::setCliLang. */
 void setLanguage(uint8_t lang) { _language = lang; }

 /** Process authentication state machine and inactivity timeout. */
 void update( );
 bool isAuthenticated( );

 void print(const String& msg);
 void println(const String& msg);
 void write(uint8_t c);

 bool available( );
 char read( );

private:


 bool _authenticated; /* Current session authentication state */
 bool _promptSent;
 String _authBuffer;
 BtAuthValidator _validator;
 uint8_t _language = LANG_EN; /* Default until CommandManager aplica preferência */


 uint32_t _lastActivityTime;
 const uint32_t _timeoutMs = 300000; /* 5-minute inactivity auto-logout */
 bool _initialized = false;
};
