/**
 * @file CommandManager.h
 * @brief CLI command parser and dual-channel console output (USB + Bluetooth).
 * @details Provides the text-based command interface for system configuration,
 * sensor diagnostics, and maintenance. All output is mirrored to
 * both USB Serial and authenticated Bluetooth sessions.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include "SystemDefs.h"
#include "BluetoothManager.h"

class CommandManager {
public:
 CommandManager( );
 /* btDeviceName defines the visible BT name ("PicoW Serial..." default
 * is replaced). Caller should pass `cfg.deviceName` (configurable on
 * the web at /config). Default "SIMUT" used as fallback.
 *
 * begin( ) now only initializes CLI parser + console sink.
 * BT init moved to beginBluetooth( ) — called AFTER WiFi.begin( )
 * initializes cyw43_arch (which resets the CYW43 chip). Without this,
 * SerialBT.begin before cyw43_arch_init via WiFi caused hardfault on
 * the first boot post-OTA when CYW43 was in a residual state. */
 void begin(const char* btDeviceName = "SIMUT");
 void beginBluetooth(const char* btDeviceName = "SIMUT");


 void setBtValidator(BtAuthValidator validator);


 bool processInput(CliDemand &demandOut);
 String getLastRawInput( );


 void consolePrint(const String& msg);
 void consolePrintln(const String& msg);
 void consolePrintf(const char* format, ...);


 void printWelcome( );
 void printPrompt( );
 void printHelp( );
 void printHelpExtras( ); /**< Shared list of new commands (IP/sensor/user) — no descriptions, syntax only. Saves flash. */
 void printSuccess(String msg);
 void printError(String msg);
 void printInfo(String msg);

 void printLogEntry(String rawCsvLine);
 void renderSensorTable(const SensorRecord* sensors, int maxSensors);
 void renderGpioMap(const SensorRecord* sensors, int maxSensors);
 void renderScanResults(const std::vector<ScanResult> &results);
 void renderSystemInfo(const SystemConfig &cfg);
 void renderSensorReading(const SensorReading &reading);
 void renderMetrics( );

 void printDivider( );

 /** Session mode — affects prompt ('>' vs '#') and state presented to user. */
 void setDebugMode(bool enabled) { _debugMode = enabled; }
 bool isDebugMode( ) const { return _debugMode; }

 /** CLI language — EN (default) or PT. Reuses cfg.displayLang.
 * Also propagates to BluetoothManager (post-auth banner). */
 void setCliLang(uint8_t lang) { _cliLang = lang; _btMgr.setLanguage(lang); }
 uint8_t cliLang( ) const { return _cliLang; }
 bool isPt( ) const { return _cliLang == LANG_PT; }

 /** Identifies whether the LAST processed command came from the
 * authenticated BT channel. Resets to false on each parse, set true
 * in the BT branch of processInput. Used by handlers that restrict
 * admin via BT. */
 bool wasLastInputFromBt( ) const { return _lastFromBt; }

private:
 BluetoothManager _btMgr;
 bool _debugMode = false;
 uint8_t _cliLang = LANG_EN;
 bool _lastFromBt = false; /**< set by processInput according to channel */


 String _usbBuffer;
 String _btBuffer;
 String _lastRawInput;
 /* Anti-spam — emits 1 warning per overflow burst on each channel;
 * reset upon receiving a valid `\n`. */
 bool _usbOverflowWarned = false;
 bool _btOverflowWarned = false;

 /** Accumulates `c` in `buffer` with bound-check (CLI_LINE_MAX).
 * If buffer is full: drops the line, emits anti-spam warning on the
 * channel, protects heap from DoS via stream without `\n`.
 * `warnedFlag` is reset on new line to allow the next warning. */
 void appendCharWithLimit(String& buffer, char c, bool& warnedFlag,
 const char* channelName);


 CliDemand parseCommand(String input);

 String formatRom(const uint8_t* rom);
 void hexStringToBytes(String hex, uint8_t* out);
};
