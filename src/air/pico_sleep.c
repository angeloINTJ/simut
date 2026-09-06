/**
 * @file air/pico_sleep.c
 * @brief Vendored RP2040 deep-sleep (SLEEP) implementation with RTC-alarm wake.
 *
 * The bundled pico-sdk (arduino-pico 5.6.1 / pico-sdk 2.x) does not ship the
 * hardware_sleep / pico_low_power library, so the sleep helpers are vendored
 * here, mirroring the SDK's sleep_run_from_xosc + sleep_goto_sleep_until.
 *
 * This uses the RP2040 SLEEP mode (WFI + SLEEPDEEP), not DORMANT:
 *   - clk_sys / clk_ref / clk_peri run from the XOSC and the PLLs are stopped;
 *   - clk_rtc keeps running from the XOSC;
 *   - sleep_en0 keeps only clk_rtc alive while the processor is halted;
 *   - the RTC alarm interrupt wakes the processor.
 *
 * SLEEP is chosen over DORMANT for reliability: DORMANT requires writing the
 * "coma" keyword to the ROSC DORMANT register and switching clk_sys onto the
 * ROSC, both of which race the slow clk_rtc / ROSC synchronisers on the Pico W
 * and wake at the wrong instant. Power is marginally higher (~1.2 mA vs
 * 0.95 mA) but the wake is deterministic.
 *
 * Waking is a RESUME (system state retained); the caller re-initialises the
 * clocks afterwards (here: a soft reset).
 */

#include "pico_sleep.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pll.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/m0plus.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/scb.h"

/* Run the system from the crystal and power down the PLLs. clk_rtc must
 * already be running from the XOSC (configured by the caller). */
static void sleep_run_from_xosc(void) {
    /* CLK_REF = XOSC (glitchless source 2). */
    clock_configure_undivided(clk_ref,
                              CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                              0,
                              XOSC_HZ);

    /* CLK_SYS = CLK_REF (now the XOSC, 12 MHz). */
    clock_configure_undivided(clk_sys,
                              CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                              0,
                              XOSC_HZ);

    /* Peripherals that cannot survive sleep are stopped. */
    clock_stop(clk_usb);
    clock_stop(clk_adc);

    /* CLK_PERI = clk_sys. */
    clock_configure_undivided(clk_peri,
                              0,
                              CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                              XOSC_HZ);

    /* Sleep keeps draining power from running PLLs; stop them. */
    pll_deinit(pll_sys);
    pll_deinit(pll_usb);

    /* The system is now running entirely off the XOSC, so the ROSC (ring
     * oscillator) is no longer needed. Disable it to save its quiescent current
     * for the whole sleep. MUST happen after clk_sys has moved off the ROSC
     * (done above), or the chip locks up (datasheet ROSC_CTRL.ENABLE). The boot
     * ROM re-enables the ROSC on the next reset, so this is safe across the
     * SYSRESETREQ wake. */
    uint32_t rosc_tmp = rosc_hw->ctrl;
    rosc_tmp &= ~ROSC_CTRL_ENABLE_BITS;
    rosc_tmp |= (ROSC_CTRL_ENABLE_VALUE_DISABLE << ROSC_CTRL_ENABLE_LSB);
    rosc_hw->ctrl = rosc_tmp;
}

void sleep_goto_sleep_until(datetime_t *t, dormant_wake_source_callback_t callback) {
    /* Move onto the XOSC and stop the PLLs. */
    sleep_run_from_xosc();

    /* Keep clk_rtc running while the rest of the chip sleeps. */
    clocks_hw->sleep_en0 = CLOCKS_SLEEP_EN0_CLK_RTC_RTC_BITS;
    clocks_hw->sleep_en1 = 0;

    /* Deep-sleep request for the processor. */
    scb_hw->scr |= M0PLUS_SCR_SLEEPDEEP_BITS;

    /* Clear any stale alarm/IRQ state left by the previous wake: we
     * SYSRESETREQ right after the WFI resumes, so the RTC IRQ handler may not
     * have run and the match/IRQ-enable bits from the prior alarm are still
     * set. Reset them (bounded wait for MATCH_ACTIVE to deassert) so
     * rtc_set_alarm() starts from a clean state on every cycle. */
    rtc_hw->irq_setup_0 = 0;
    rtc_hw->irq_setup_1 = 0;
    rtc_hw->inte = 0;
    for (uint32_t i = 0; i < 200000u && (rtc_hw->irq_setup_0 & RTC_IRQ_SETUP_0_MATCH_ACTIVE_BITS); i++) {
        tight_loop_contents();
    }

    /* Arm the RTC alarm as late as possible so a tiny interval cannot fire
     * during the clock switch above. */
    rtc_set_alarm(t, callback);

    /* Only the RTC alarm may wake the WFI. A pending USB/UART/inter-core IRQ
     * would otherwise wake it instantly. The reset after wake re-inits the
     * NVIC, so this does not need to be undone. */
    for (uint irq = 0; irq < 32; irq++) {
        if (irq != (uint)RTC_IRQ) irq_set_enabled(irq, false);
    }

    /* SLEEP until the RTC alarm fires. Execution resumes here on wake. */
    __asm volatile("wfi");
}
