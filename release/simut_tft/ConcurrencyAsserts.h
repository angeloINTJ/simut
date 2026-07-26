/**
 * @file ConcurrencyAsserts.h
 * @brief Opt-in tripwire for concurrency invariant 3 (docs/CONCURRENCY.md).
 * @details Invariant 3: never hold DisplayManager::_stateMutex across any
 * FLASH_OP/LittleFS call — Core 1 blocks on that mutex, and a flash
 * lockout waiting for Core 1 would then deadlock (→ WDT reboot).
 *
 * Enable in a debug build with:
 *     build_flags = -DSIMUT_CONCURRENCY_ASSERTS
 *
 * When enabled, FLASH_OP logs an ERROR the moment any code enters it
 * while the current core owns _stateMutex — turning a would-be
 * intermittent deadlock into a deterministic log line. Release builds
 * compile the macro to nothing (zero cost, zero coupling).
 *
 * Requires LogManager.h at the call site (StorageManager.cpp has it).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @license MIT License
 */

#pragma once

#ifdef SIMUT_CONCURRENCY_ASSERTS

/** Implemented in DisplayManager.cpp (needs private _stateMutex access).
 *  Uses mutex_try_enter owner introspection: if the try fails and the
 *  reported owner is this core, we are (re-)entering while holding it. */
bool simutStateMutexHeldByCurrentCore( );

#define SIMUT_ASSERT_NO_STATE_MUTEX() do { \
 if (simutStateMutexHeldByCurrentCore( )) { \
  LOG_CODE(LOG_ERROR, "APP", APP_DISPLAY_PAUSE_STUCK, 2, \
           TRL("INVARIANT-3 VIOLATED: _stateMutex held across FLASH_OP")); \
 } \
} while (0)

#else

#define SIMUT_ASSERT_NO_STATE_MUTEX() do { } while (0)

#endif /* SIMUT_CONCURRENCY_ASSERTS */
