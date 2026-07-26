/**
 * @file CommandParser.h
 * @brief Pure CLI text → CliDemand parser (host-testable, no I/O deps).
 * @details Extracted from CommandManager so `pio test -e native_cli` can
 * compile production parsing logic without Bluetooth/Serial/LittleFS.
 *
 * @project SIMUT
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include "SystemDefs_Cli.h"

/** Parse one CLI line into a CliDemand. USB and BT share this path. */
CliDemand parseCliCommand(String input);
