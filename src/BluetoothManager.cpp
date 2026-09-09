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
#if SIMUT_BLUETOOTH
#include <BluetoothLock.h>   /* the framework's own guard around BTstack calls */
#endif

#if SIMUT_BLUETOOTH

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
 /* begin( ) has just called gap_discoverable_control(1). Start the clock that
  * turns it back off (V-01b); update( ) does the actual call, because it must
  * not happen before the stack has finished coming up. */
 _discoverableUntil = millis( ) + BT_DISCOVERABLE_MS;
 _discoverableClosed = false;
}

/**
 * @brief Close the Bluetooth discovery window once it has elapsed.
 *
 * Called from update( ), which the main loop drives. gap_discoverable_control
 * is a BTstack call, so it is made under the same lock the framework's own
 * SerialBT uses — without it this races the CYW43 async context.
 *
 * Connectability is untouched on purpose: a phone that already paired keeps
 * working, and a unit that has been noted by an attacker is still reachable.
 * The point is to stop advertising to every scan in range for the whole
 * uptime of the device.
 */
void BluetoothManager::closeDiscoveryIfDue( ) {
 if (_discoverableClosed || _discoverableUntil == 0) return;
 if (!timeReached(_discoverableUntil)) return;
 {
  BluetoothLock l;
  gap_discoverable_control(0);
 }
 _discoverableClosed = true;
 LOG_CODE(LOG_INFO, "SEC", SEC_BT_LOCKOUT, -1,
          TRL("BT discovery window closed"));
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

 closeDiscoveryIfDue( );

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

 /* Locked out after failed passwords. Everything the link offers is dropped
  * on the floor, unread, so the cost of a wrong guess is real time and not a
  * round trip. The notice goes out once per lockout: repeating it on every
  * byte would turn the block into an amplifier, and would also tell the
  * attacker exactly when the window reopens. */
 if (_lockedUntil != 0 && !timeReached(_lockedUntil)) {
 if (!_lockNoticeSent) {
 SerialBT.println(pt ? "\n\rBloqueado. Tente mais tarde."
 : "\n\rLocked. Try later.");
 _lockNoticeSent = true;
 }
 while (SerialBT.available( )) SerialBT.read( );
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
 _failCount = 0;
 _lockedUntil = 0;
 _lockNoticeSent = false;
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
 if (_failCount < AUTH_FAIL_CAP) _failCount++;
 uint32_t penaltyMs = authLockoutMs(_failCount);
 _lockedUntil = millis( ) + penaltyMs;
 /* millis() + penalty can legitimately land on 0 once every 49.7 days;
  * 0 is this field's "not locked" sentinel, so nudge it off. */
 if (_lockedUntil == 0) _lockedUntil = 1;
 _lockNoticeSent = false;
 LOG_CODE(LOG_WARN, "SEC", SEC_BT_LOCKOUT, (int)(penaltyMs / 1000),
 TRL("BT lockout"));
 _promptSent = false;
 _authBuffer = "";
 return; /* stop reading: the rest of this burst is already locked out */
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

#endif /* SIMUT_BLUETOOTH */
