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
#include <hardware/structs/watchdog.h>
#include "ota/staging.h"   /* the RP2350's update on trial */
#include <hardware/clocks.h>   /* clock_configure( ), clk_peri: periClockFromSys( ) */

/** Global application manager instance — orchestrates all subsystems. */
AppManager app;

/** @brief Puts clk_peri back on clk_sys. setup( ) calls it first, on the images that drive the TFT panel.
 * @details The framework's main( ) calls set_sys_clock_khz( F_CPU / 1000, true ) before setup( ) whenever
 * F_CPU is not the SDK's own, and the Pico W's 133 MHz is not. The SDK's set_sys_clock_pll( ) answers by moving
 * clk_peri to the 48 MHz USB PLL (PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK is 0, and the SDK is prebuilt
 * inside the core, out of reach of our flags). An SPI block divides clk_peri by an even prescale times a
 * post-divider, so the panel's bus could not pass 24 MHz whatever SIMUT_TFT_SPI_HZ asked: read on the bench
 * 2026-10-15, clk_sys 133 MHz, clk_peri 48 MHz, SPI0 24 MHz after a push.
 *
 * It must come before anything that is begun from clk_peri: a SPI or a UART computes its divider from
 * clk_peri when it is configured and keeps it. Nothing before this call does. The call graph from the
 * static initializers, from the SDK's runtime init and from main( ) up to setup( ), read on the
 * disassembly of the six panel images (2026-10-15), reaches no spi_init, uart_init or SerialUART::begin;
 * the first user is AppManager::setup( )'s uart_init( uart1 ), a few statements later, and Core 1
 * starts later still. clk_sys does not move, so the PIO blocks (the radio's, the ENC28J60's, the sensors')
 * and the I2C blocks, which the SDK divides from clk_sys, do not notice.
 *
 * The Pico 2 W is already there: its F_CPU is the SDK's own, set_sys_clock_khz( ) is not called, and clk_peri
 * is clk_sys, so the test below leaves its clocks alone. */
static inline void periClockFromSys( ) {
 const uint32_t sysHz = clock_get_hz(clk_sys);
 if (clock_get_hz(clk_peri) != sysHz) {
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, sysHz, sysHz);
 }
}

/** @brief Arduino setup — initializes all subsystems. Watchdog starts in loop( ). */
void setup( ) {
 /* The panel's bus clock, before anything is begun (periClockFromSys above). A constant condition:
  * the images without the panel (alpha, Air, the plain Wire) compile none of it and keep their bytes. */
 if (SIMUT_DISPLAY_TFT) periClockFromSys( );
#ifndef SIMUT_WDT_DISABLED
 /* setup( ) runs for tens of seconds and only loop( ) arms the watchdog, so a
  * watchdog left enabled by whatever reset brought us here would fire in the
  * middle of the boot — and do it again on the next one. SIMUT Air arms a
  * short guard across its wake path (air/pico_sleep.c) and disarms it on the
  * ordinary path; this is the backstop for the path where the guard itself
  * fired. Free otherwise: the watchdog is off during setup( ) by design. */
 hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
#if !OTA_RP2040_MAP
 /* An update on trial is the exception (ota/staging.h): a setup( ) that never
  * ends has to give the board back to the image it replaced. */
 ota::trial_guard_begin( );
#endif
#endif
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
#if !OTA_RP2040_MAP
 ota::trial_guard_end( );
#endif
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
  * on its way out, so the two uses of the register cannot collide. Neither can
  * the SIMUT Air clock carry: it is written on the way into sleep, which never
  * comes back through this loop, and zeroed at the top of the next setup( ). */
 watchdog_hw->scratch[6] = millis( );
#endif

 app.loop( );

#ifndef SIMUT_WDT_DISABLED
 watchdog_update( );
#endif
}