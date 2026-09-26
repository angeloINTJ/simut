/**
 * @file ConcurrencyAsserts.cpp
 * @brief The bridge behind SIMUT_ASSERT_NO_STATE_MUTEX (ConcurrencyAsserts.h).
 * @details Compiled only when the concurrency tripwire is on: tools/features.toml
 * excludes this file otherwise (off_exclude), so no image that ships carries it.
 *
 * The bridge lived at the bottom of DisplayManager.cpp until 2026-09-26, and the
 * SIMUT Air build does not compile that file — Air with the tripwire on failed
 * to link (undefined reference to simutStateMutexHeldByCurrentCore), found by
 * the configurator's cost matrix (tools/measure_savings.py --matrix). Every
 * display variant defines DisplayManager::stateMutexHeldByCurrentCore( ); Air's,
 * with no Core 1 renderer to deadlock against, answers false.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "ConcurrencyAsserts.h"
#include "DisplayManager.h"

/* Free-function bridge declared in ConcurrencyAsserts.h so StorageManager
 * does not need to include DisplayManager.h. */
bool simutStateMutexHeldByCurrentCore( ) {
	return DisplayManager::stateMutexHeldByCurrentCore( );
}
