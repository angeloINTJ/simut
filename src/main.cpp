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
 /* Watchdog is NOT enabled here. Between setup( ) and loop( ), the framework
 * Arduino-Pico may run housekeeping (USB, WiFi yield) for an
 * indeterminate time. The watchdog is enabled on the first loop( ). */
}

/**
 * @brief Main loop — feeds watchdog unconditionally and runs application logic.
 *
 * GOLDEN RULE: The watchdog is ALWAYS fed unconditionally.
 * Display health is monitored separately via checkCrossCoreHealth( ).
 * This ensures Core 0 NEVER dies due to an isolated Core 1 problem.
 */
void loop( ) {
 static bool _wdtStarted = false;
 if (!_wdtStarted) {
 watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);
 LogManager::markWdtActive( ); /* Flash paths may extend WDT from this point onward */
 _wdtStarted = true;
 }

 watchdog_update( );

 app.loop( );

 watchdog_update( );
}
