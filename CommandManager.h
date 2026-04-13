/**
 * @file    CommandManager.h
 * @brief   CLI command parser and dual-channel console output (USB + Bluetooth).
 * @details Provides the text-based command interface for system configuration,
 * sensor diagnostics, and maintenance. All output is mirrored to
 * both USB Serial and authenticated Bluetooth sessions.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.4.7
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
    void printSuccess(String msg);
    void printError(String msg);
    void printInfo(String msg);

    void printLogEntry(String rawCsvLine);
    void renderSensorTable(const SensorRecord* sensors, int maxSensors);
    void renderScanResults(const std::vector<ScanResult> &results);
    void renderSystemInfo(const SystemConfig &cfg);
    void renderSensorReading(const SensorReading &reading);

private:
    BluetoothManager _btMgr;


    String _usbBuffer;
    String _btBuffer;
    String _lastRawInput;


    CliDemand parseCommand(String input);

    void printDivider();
    String formatRom(const uint8_t* rom);
    void hexStringToBytes(String hex, uint8_t* out);
};
