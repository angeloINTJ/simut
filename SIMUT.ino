/**
 * @file    SIMUT.ino
 * @brief   Main entry point for the SIMUT firmware.
 * @details Initializes the AppManager and runs the main loop with hardware
 * watchdog protection. The watchdog is ALWAYS fed unconditionally
 * to ensure Core 0 never dies due to isolated Core 1 issues.
 * Cross-core health is monitored separately by LogManager.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.8.0
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include <Arduino.h>
#include "AppManager.h"
#include "SystemDefs.h"
#include <hardware/watchdog.h>

/** Global application manager instance — orchestrates all subsystems. */
AppManager app;

/** @brief Arduino setup — initializes all subsystems. Watchdog starts in loop(). */
void setup() {
    app.setup();
    /* Watchdog NÃO é habilitado aqui. Entre setup() e loop(), o framework
     * Arduino-Pico pode executar housekeeping (USB, WiFi yield) por tempo
     * indeterminado. O watchdog é habilitado no primeiro loop(). */
}

/**
 * @brief Main loop — feeds watchdog unconditionally and runs application logic.
 *
 * GOLDEN RULE: The watchdog is ALWAYS fed unconditionally.
 * Display health is monitored separately via checkCrossCoreHealth().
 * This ensures Core 0 NEVER dies due to an isolated Core 1 problem.
 */
void loop() {
    static bool _wdtStarted = false;
    if (!_wdtStarted) {
        watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);
        _wdtStarted = true;
    }

    watchdog_update();

    app.loop();

    watchdog_update();
}
