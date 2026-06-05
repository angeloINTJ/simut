/**
 * @file BluetoothManager.cpp
 * @brief Implementation of BluetoothManager — authentication state machine and I/O.
 * @details Handles the full authentication flow: prompt display, password masking,
 * validator callback invocation, and automatic session expiration.
 * All output methods are gated by authentication status.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "BluetoothManager.h"
#include "LogManager.h"

BluetoothManager::BluetoothManager( ) {
 _authenticated = false;
 _promptSent = false;
 _validator = nullptr;
 _lastActivityTime = 0;
}

/**
 * @brief Start Bluetooth Serial at 115200 baud.
 *
 * The visible BT network name (default from the library is "PicoW Serial XX:XX:...")
 * is now set via SerialBT.setName( ) before begin( ). The SerialBT library
 * (arduino-pico) only accepts setName( ) while _running==false, so the
 * order matters: setName then begin. To change post-boot requires a reboot
 * (consistent with the "Save and Restart" web flow).
 */
void BluetoothManager::begin(const char* deviceName) {
 if (deviceName && *deviceName) {
 SerialBT.setName(deviceName);
 }
 SerialBT.begin(115200);
 _initialized = true;
}

void BluetoothManager::setValidator(BtAuthValidator validator) {
 _validator = validator;
}

/**
 * @brief Main update loop — handles auto-logout and authentication flow.
 *
 * Section 1: Security management — auto-logout after 5min inactivity.
 * Section 2: Authentication state machine — prompt, password entry, validation.
 */
void BluetoothManager::update( ) {

 const bool pt = (_language == LANG_PT);

 if (_authenticated) {
 if (timeSince(_lastActivityTime, _timeoutMs)) {
 SerialBT.println(pt
 ? "\n\r[SEGURANCA] Sessao encerrada (5 min inativo)."
 : "\n\r[SECURITY] Session ended (5 min idle).");
 LOG_CODE(LOG_INFO, "SEC", SEC_SESSION_EXPIRE, 0,
 TRL("BT session timeout (5 min idle)"));
 _authenticated = false;
 _promptSent = false;
 _authBuffer = "";
 }
 return;
 }


 while (SerialBT.available( )) {
 char c = (char)SerialBT.read( );
 _lastActivityTime = millis( );


 if (!_promptSent) {
 SerialBT.println(pt
 ? "\n\r--- Conexao Bluetooth estabelecida ---"
 : "\n\r--- Bluetooth connection established ---");
 SerialBT.print(pt ? "Senha do admin: " : "Admin password: ");
 _promptSent = true;
 _authBuffer = "";
 /* Do NOT discard the char — it falls through to processing below.
 * If the first byte is \r/\n (terminal enter on connect),
 * the empty-buffer branch treats it as no-op. If printable,
 * it becomes the first char of the password — expected behavior. */
 }


 if (c == '\n' || c == '\r') {
 if (_authBuffer.length( ) > 0) {
 SerialBT.println( );

 bool valid = false;
 if (_validator != nullptr) {
 valid = _validator(_authBuffer);
 }

 if (valid) {
 _authenticated = true;
 _lastActivityTime = millis( );
 /* Banner FIRST: immediate response to the user.
 * LOG_CODE afterwards — flash write is buffered in RAM
 * via setForceBuffer and flushed asynchronously. */
 SerialBT.println( );
 SerialBT.println("===========================================");
 SerialBT.print (" SIMUT IoT CLI ");
 SerialBT.println(SIMUT_VERSION);
 if (pt) {
 SerialBT.println(" Acesso concedido. Digite 'help'.");
 SerialBT.println(" (For English: 'language en')");
 } else {
 SerialBT.println(" Access granted. Type 'help'.");
 SerialBT.println(" (Para Portugues: 'language pt')");
 }
 SerialBT.println("===========================================");
 SerialBT.print("SIMUT> ");
 LOG_CODE(LOG_INFO, "SEC", SEC_LOGIN_SUCCESS, 0,
 TRL("BT admin login"));
 } else {
 SerialBT.println(pt ? "Acesso negado." : "Access denied.");
 LOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0,
 TRL("BT admin password rejected"));
 _promptSent = false;
 }
 _authBuffer = "";
 }
 } else if (c == 8 || c == 127) {
 if (_authBuffer.length( ) > 0) {
 _authBuffer.remove(_authBuffer.length( ) - 1);
 }
 } else {
 if (_authBuffer.length( ) < BT_AUTH_BUFFER_MAX) {
 _authBuffer += c;
 SerialBT.print("*");
 }
 }
 }
}

bool BluetoothManager::isAuthenticated( ) {
 return _authenticated;
}

void BluetoothManager::print(const String& msg) {
 if(_authenticated) SerialBT.print(msg);
}

void BluetoothManager::println(const String& msg) {
 if(_authenticated) SerialBT.println(msg);
}

void BluetoothManager::write(uint8_t c) {
 if(_authenticated) SerialBT.write(c);
}

bool BluetoothManager::available( ) {
 if (_authenticated && SerialBT.available( )) {
 _lastActivityTime = millis( );
 return true;
 }
 return false;
}

char BluetoothManager::read( ) {
 return _authenticated ? SerialBT.read( ) : -1;
}
