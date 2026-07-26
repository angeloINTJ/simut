/**
 * @file SystemDefs.h
 * @brief Facade — re-exports global type definitions, enums, structs and helpers.
 * @details Historical header, now split into themed sub-headers.
 * Maintains backward compat: callers continue doing
 * `#include "SystemDefs.h"` without changes. For faster incremental
 * builds, new callers can include only the relevant sub-header
 * (e.g. `#include "SystemDefs_Validate.h"`).
 *
 * Sub-headers:
 * · SystemDefs_Limits.h — limits, version, MinMaxSlot, PERM_*
 * · SystemDefs_Time.h — boot/UI/sensor timing + safeCopy + millis helpers
 * · SystemDefs_Network.h — rate-limit, login, BT/CLI/AP/cursor constants
 * · SystemDefs_Logging.h — TraceModule, LogCode, LogTagId, CompactLogRecord
 * · SystemDefs_Records.h — SystemConfig, sensor/UI/history structs
 * · SystemDefs_Cli.h — DemandType + CliDemand
 * · SystemDefs_Validate.h — input validators
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include "simut_config.h"       /* centralized hw config — must come first */
#include "SystemDefs_Limits.h"
#include "SystemDefs_Time.h"
#include "SystemDefs_Network.h"
#include "SystemDefs_Logging.h"
#include "SystemDefs_Records.h"
#include "SystemDefs_Reserved.h"
#include "SystemDefs_Cli.h"
#include "SystemDefs_Validate.h"
