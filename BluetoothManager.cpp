/**
 * @file    BluetoothManager.cpp
 * @brief   Implementation of BluetoothManager — authentication state machine and I/O.
 * @details Handles the full authentication flow: prompt display, password masking,
 * validator callback invocation, and automatic session expiration.
 * All output methods are gated by authentication status.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.4.8
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "BluetoothManager.h"

BluetoothManager::BluetoothManager() {
    _authenticated = false;
    _promptSent = false;
    _validator = nullptr;
    _lastActivityTime = 0;
}

/**
 * @brief Start Bluetooth Serial at 115200 baud.
 * On the Pico W, SerialBT requires a baud rate (not a device name).
 * The network-visible name is managed by the radio firmware.
 */
void BluetoothManager::begin(const char* deviceName) {


    SerialBT.begin(115200);
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
void BluetoothManager::update() {

    if (_authenticated) {
        if (millis() - _lastActivityTime > _timeoutMs) {
            SerialBT.println("\n\r[ALERTA DE SEGURANCA] Sessao encerrada por inatividade (5 min).");
            _authenticated = false;
            _promptSent = false;
            _authBuffer = "";
        }
        return;
    }


    while (SerialBT.available()) {
        char c = (char)SerialBT.read();
        _lastActivityTime = millis();


        if (!_promptSent) {
            SerialBT.println("\n\r--- CONEXAO BLUETOOTH ESTABELECIDA ---");
            SerialBT.print("Admin Password: ");
            _promptSent = true;
            _authBuffer = "";
            continue;
        }


        if (c == '\n' || c == '\r') {
            if (_authBuffer.length() > 0) {
                SerialBT.println();

                bool valid = false;
                if (_validator != nullptr) {
                    valid = _validator(_authBuffer);
                }

                if (valid) {
                    _authenticated = true;
                    _lastActivityTime = millis();
                    SerialBT.println("Acesso Concedido! Bem-vindo ao SIMUT CLI.");
                    SerialBT.print("SIMUT> ");
                } else {
                    SerialBT.println("Acesso Negado.");
                    _promptSent = false;
                }
                _authBuffer = "";
            }
        } else if (c == 8 || c == 127) {
            if (_authBuffer.length() > 0) {
                _authBuffer.remove(_authBuffer.length() - 1);
            }
        } else {
            if (_authBuffer.length() < BT_AUTH_BUFFER_MAX) {
                _authBuffer += c;
                SerialBT.print("*");
            }
        }
    }
}

bool BluetoothManager::isAuthenticated() {
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

bool BluetoothManager::available() {
    if (_authenticated && SerialBT.available()) {
        _lastActivityTime = millis();
        return true;
    }
    return false;
}

char BluetoothManager::read() {
    return _authenticated ? SerialBT.read() : -1;
}
