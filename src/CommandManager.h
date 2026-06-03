/**
 * @file CommandManager.h
 * @brief CLI command parser and dual-channel console output (USB + Bluetooth).
 * @details Provides the text-based command interface for system configuration,
 * sensor diagnostics, and maintenance. All output is mirrored to
 * both USB Serial and authenticated Bluetooth sessions.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
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
 /* btDeviceName define o nome BT visível ("PicoW Serial..." default
 * é substituído). Caller deve passar `cfg.deviceName` (configurável na
 * web em /config). Default "SIMUT" usado em fallback.
 *
 * alpha28 split: begin( ) agora só inicializa CLI parser + console sink.
 * BT init movido pra beginBluetooth( ) — chamado APÓS WiFi.begin( ) inicializar
 * o cyw43_arch (que reseta o chip CYW43). Sem isso, SerialBT.begin antes
 * do cyw43_arch_init via WiFi causava hardfault no boot 1 pós-OTA quando
 * CYW43 estava em estado residual. */
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
 void printHelpExtras( ); /**< Lista compartilhada dos comandos novos (IP/sensor/user) — sem descrições, só sintaxe. Economiza flash. */
 void printSuccess(String msg);
 void printError(String msg);
 void printInfo(String msg);

 void printLogEntry(String rawCsvLine);
 void renderSensorTable(const SensorRecord* sensors, int maxSensors);
 void renderScanResults(const std::vector<ScanResult> &results);
 void renderSystemInfo(const SystemConfig &cfg);
 void renderSensorReading(const SensorReading &reading);
 void renderMetrics( );

 void printDivider( );

 /** Modo de sessão — afeta prompt ('>' vs '#') e estado apresentado ao user. */
 void setDebugMode(bool enabled) { _debugMode = enabled; }
 bool isDebugMode( ) const { return _debugMode; }

 /** Idioma da CLI — EN (default) ou PT. Reutiliza cfg.displayLang.
 * Propaga também para BluetoothManager (banner pós-auth). */
 void setCliLang(uint8_t lang) { _cliLang = lang; _btMgr.setLanguage(lang); }
 uint8_t cliLang( ) const { return _cliLang; }
 bool isPt( ) const { return _cliLang == LANG_PT; }

 /** /identifica se o ÚLTIMO comando processado veio do canal BT
 * autenticado. Reseta para false em cada parse, setado true no ramo
 * BT de processInput. Usado por handlers que restringem a admin via
 * BT (ex: CMD_DBG_SENSOR_HISTORY_ALL). */
 bool wasLastInputFromBt( ) const { return _lastFromBt; }

private:
 BluetoothManager _btMgr;
 bool _debugMode = false;
 uint8_t _cliLang = LANG_EN;
 bool _lastFromBt = false; /**< setado por processInput conforme canal */


 String _usbBuffer;
 String _btBuffer;
 String _lastRawInput;
 /* SEC-005/F12.5: anti-spam — emite 1 warning por rajada de overflow
 * em cada canal; resetado ao receber `\n` válido. */
 bool _usbOverflowWarned = false;
 bool _btOverflowWarned = false;

 /** Acumula `c` em `buffer` com bound-check (CLI_LINE_MAX).
 * Se buffer cheio: descarta linha, emite warning anti-spam no canal,
 * protege heap de DoS via stream sem `\n`.
 * `warnedFlag` é resetada em nova linha para permitir próximo warning. */
 void appendCharWithLimit(String& buffer, char c, bool& warnedFlag,
 const char* channelName);


 CliDemand parseCommand(String input);

 String formatRom(const uint8_t* rom);
 void hexStringToBytes(String hex, uint8_t* out);
};
