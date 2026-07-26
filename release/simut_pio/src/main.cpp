/**
 * @file SIMUT.ino
 * @brief Main entry point for the SIMUT firmware.
 * @details Initializes the AppManager and runs the main loop with hardware
 * watchdog protection. The watchdog is ALWAYS fed unconditionally
 * to ensure Core 0 never dies due to isolated Core 1 issues.
 * Cross-core health is monitored separately by LogManager.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include <Arduino.h>
#include "AppManager.h"
#include "SystemDefs.h"
#include "LogManager.h"
#include <hardware/watchdog.h>

/** Global application manager instance — orchestrates all subsystems. */
AppManager app;

/** @brief Arduino setup — initializes all subsystems. Watchdog starts in loop( ). */
void setup( ) {
 app.setup( );
}

/**
 * @brief Main loop — feeds watchdog unconditionally and runs application logic.
 *
 * GOLDEN RULE: The watchdog is ALWAYS fed unconditionally.
 * Display health is monitored separately via checkCrossCoreHealth( ).
 * This ensures Core 0 NEVER dies due to an isolated Core 1 problem.
 */
void loop( ) {
#ifndef SIMUT_WDT_DISABLED
 static bool _wdtStarted = false;
 if (!_wdtStarted) {
 watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);
 LogManager::markWdtActive( );
 _wdtStarted = true;
 }
 watchdog_update( );
 /* Uptime at the last watchdog feed, for the next boot's autopsy. The FATAL is
  * written after the reset, so its own uptime field is 0 and the crash has no
  * timestamp at all — which leaves "stalled after seven minutes of download
  * load" and "reset while idling during a firmware upload" reading identically.
  * Only Core 0 reaches here, and the soft-panic payload overwrites scratch[6]
  * on its way out, so the two uses of the register cannot collide. */
 watchdog_hw->scratch[6] = millis( );
#endif

 app.loop( );

#ifndef SIMUT_WDT_DISABLED
 watchdog_update( );
#endif
}