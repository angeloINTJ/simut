/**
 * @file    CommandManager.h
 * @brief   CLI command parser and dual-channel console output (USB + Bluetooth).
 * @details Provides the text-based command interface for system configuration,
 * sensor diagnostics, and maintenance. All output is mirrored to
 * both USB Serial and authenticated Bluetooth sessions.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include "SystemDefs.h"
#include "BluetoothManager.h"

class CommandManager {
public:
    CommandManager();
    void begin();


    void setBtValidator(BtAuthValidator validator);


    bool processInput(CliDemand &demandOut);
    String getLastRawInput();


    void consolePrint(const String& msg);
    void consolePrintln(const String& msg);
    void consolePrintf(const char* format, ...);


    void printWelcome();
    void printPrompt();
    void printHelp();
    void printHelpExtras();  /**< Lista compartilhada dos comandos novos (IP/sensor/user) — sem descrições, só sintaxe. Economiza flash. */
    void printSuccess(String msg);
    void printError(String msg);
    void printInfo(String msg);

    void printLogEntry(String rawCsvLine);
    void renderSensorTable(const SensorRecord* sensors, int maxSensors);
    void renderScanResults(const std::vector<ScanResult> &results);
    void renderSystemInfo(const SystemConfig &cfg);
    void renderSensorReading(const SensorReading &reading);
    void renderMetrics();

    void printDivider();

    /** Modo de sessão — afeta prompt ('>' vs '#') e estado apresentado ao user. */
    void setDebugMode(bool enabled) { _debugMode = enabled; }
    bool isDebugMode() const        { return _debugMode; }

    /** Idioma da CLI — EN (default) ou PT. Reutiliza cfg.displayLang.
     *  Propaga também para BluetoothManager (banner pós-auth). */
    void setCliLang(uint8_t lang)   { _cliLang = lang; _btMgr.setLanguage(lang); }
    uint8_t cliLang() const         { return _cliLang; }
    bool isPt() const               { return _cliLang == LANG_PT; }

private:
    BluetoothManager _btMgr;
    bool _debugMode = false;
    uint8_t _cliLang = LANG_EN;


    String _usbBuffer;
    String _btBuffer;
    String _lastRawInput;
    /* SEC-005/F12.5: anti-spam — emite 1 warning por rajada de overflow
     *  em cada canal; resetado ao receber `\n` válido. */
    bool   _usbOverflowWarned = false;
    bool   _btOverflowWarned  = false;

    /** Acumula `c` em `buffer` com bound-check (CLI_LINE_MAX).
     *  Se buffer cheio: descarta linha, emite warning anti-spam no canal,
     *  protege heap de DoS via stream sem `\n`.
     *  `warnedFlag` é resetada em nova linha para permitir próximo warning. */
    void appendCharWithLimit(String& buffer, char c, bool& warnedFlag,
                             const char* channelName);


    CliDemand parseCommand(String input);

    String formatRom(const uint8_t* rom);
    void hexStringToBytes(String hex, uint8_t* out);
};
