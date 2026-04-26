/**
 * @file    SystemDefs.h
 * @brief   Facade — re-exports global type definitions, enums, structs e helpers.
 * @details Header histórico, agora dividido em sub-headers temáticos (EXT-003 /
 *          F17 etapa 4). Mantém backward compat: callers continuam fazendo
 *          `#include "SystemDefs.h"` sem mudança. Para builds incrementais
 *          mais rápidos, callers novos podem incluir somente o sub-header
 *          relevante (ex: `#include "SystemDefs_Validate.h"`).
 *
 *          Sub-headers:
 *            · SystemDefs_Limits.h   — limits, version, MinMaxSlot, PERM_*
 *            · SystemDefs_Time.h     — boot/UI/sensor timing + safeCopy + millis helpers
 *            · SystemDefs_Network.h  — rate-limit, login, BT/CLI/AP/cursor constants
 *            · SystemDefs_Logging.h  — TraceModule, LogCode, LogTagId, CompactLogRecord
 *            · SystemDefs_Records.h  — SystemConfig, sensor/UI/history structs
 *            · SystemDefs_Cli.h      — DemandType + CliDemand
 *            · SystemDefs_Validate.h — input validators
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once

#include "SystemDefs_Limits.h"
#include "SystemDefs_Time.h"
#include "SystemDefs_Network.h"
#include "SystemDefs_Logging.h"
#include "SystemDefs_Records.h"
#include "SystemDefs_Cli.h"
#include "SystemDefs_Validate.h"
