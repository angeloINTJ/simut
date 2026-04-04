/**
 * @file    SIMUT.ino
 * @brief   Main entry point for the SIMUT firmware.
 * @details Initializes the AppManager and runs the main loop with hardware
 *          watchdog protection. The watchdog is fed unconditionally to ensure
 *          Core 0 never dies due to isolated Core 1 issues. Cross-core health
 *          is monitored separately by LogManager.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include <Arduino.h>
#include "AppManager.h"
#include <hardware/watchdog.h>

/** Global application manager instance — orchestrates all subsystems. */
AppManager app;

/**
 * @brief Arduino setup — initializes all subsystems and enables hardware watchdog.
 */
void setup() {
    app.setup();
    watchdog_enable(8300, 1);   /* 8.3s hardware watchdog — reboot if Core 0 hangs */
}

/**
 * @brief Main loop — feeds watchdog unconditionally and runs application logic.
 *
 * The watchdog is always fed unconditionally. Display health is monitored
 * separately via checkCrossCoreHealth(). This ensures Core 0 never dies
 * due to an isolated Core 1 problem.
 */
void loop() {
    watchdog_update();
    app.loop();
    watchdog_update();
}
