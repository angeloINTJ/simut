/**
 * @file air/pico_sleep.c
 * @brief Vendored RP2040 DORMANT implementation with RTC-alarm wake.
 *
 * The bundled pico-sdk (arduino-pico 5.6.1 / pico-sdk 2.x) does not ship the
 * hardware_sleep / pico_low_power library, so the dormant sequence is vendored
 * here, mirroring the upstream pico_low_power RP2040
 * DORMANT_CLOCK_SOURCE_RTC path:
 *
 *   1. Run clk_sys / clk_ref / clk_peri from the ROSC (a stoppable source) and
 *      stop clk_usb / clk_adc and both PLLs.
 *   2. Leave clk_rtc running from the XOSC so the RTC keeps counting in
 *      DORMANT. The Pico W has no external 32 kHz crystal, so the XOSC itself
 *      is NOT stopped (only the ROSC is put to sleep).
 *   3. Arm the RTC alarm, then write the "coma" keyword to the ROSC DORMANT
 *      register. That write halts all processor execution until the RTC alarm
 *      restarts the ROSC (RP2040 datasheet 2.11.3 / 2.17.7).
 *
 * Waking from DORMANT is a RESUME: system state is retained and execution
 * continues right after the DORMANT-register write. The caller decides what
 * to do next (here: soft-reset so the boot ROM re-initialises the clocks).
 */

#include "pico_sleep.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/m0plus.h"
#include "hardware/regs/rosc.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/rosc.h"
#include "hardware/structs/scb.h"

/* Measure the ROSC frequency with the frequency counter so the reported
 * clk_ref/clk_sys/clk_peri values match reality. The ROSC is left at its
 * power-on default (no external crystal trimming), nominally ~6.5 MHz. */
static uint32_t rosc_freq_hz(void) {
    uint32_t khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC);
    if (khz < 1000u || khz > 20000u) {
        khz = 6500u; /* nominal RP2040 ROSC default */
    }
    return khz * 1000u;
}

/* Switch the running clocks onto the ROSC and power down the PLLs. */
static void sleep_run_from_rosc(void) {
    const uint32_t rosc_hz = rosc_freq_hz();

    /* CLK_REF = ROSC (source 0 of the clk_ref glitchless mux). */
    clock_configure_undivided(clk_ref,
                              CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH,
                              0,
                              rosc_hz);

    /* CLK_SYS = CLK_REF (now the ROSC). */
    clock_configure_undivided(clk_sys,
                              CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                              0,
                              rosc_hz);

    /* Peripherals that cannot survive DORMANT are stopped. */
    clock_stop(clk_usb);
    clock_stop(clk_adc);

    /* CLK_PERI = clk_sys. */
    clock_configure_undivided(clk_peri,
                              0,
                              CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                              rosc_hz);

    /* DORMANT does not halt the PLLs; stop them to avoid wasting power. */
    pll_deinit(pll_sys);
    pll_deinit(pll_usb);
}

/* Put the ROSC into DORMANT. Blocks until the RTC alarm restarts it. */
static void rosc_set_dormant(void) {
    /* RP2040 datasheet 2.17.7: write the keyword to pause the oscillator. */
    rosc_hw->dormant = ROSC_DORMANT_VALUE_DORMANT; /* 0x636f6d61 "coma" */
    while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS)) {
        tight_loop_contents();
    }
}

void sleep_goto_dormant_until(datetime_t *t, dormant_wake_source_callback_t callback) {
    /* Move onto the stoppable ROSC and stop the PLLs. */
    sleep_run_from_rosc();

    /* Keep clk_rtc running while the rest of the chip sleeps. */
    clocks_hw->sleep_en0 = CLOCKS_SLEEP_EN0_CLK_RTC_RTC_BITS;
    clocks_hw->sleep_en1 = 0;

    /* Deep-sleep request for the processor (parity with the SDK path). */
    scb_hw->scr |= M0PLUS_SCR_SLEEPDEEP_BITS;

    /* Arm the RTC alarm as late as possible so a tiny interval cannot fire
     * during the clock switch above and strand us asleep. clk_rtc must already
     * be running from the XOSC (done by the caller). */
    rtc_set_alarm(t, callback);

    /* Enter DORMANT. Execution resumes here once the alarm fires and the ROSC
     * restarts. */
    rosc_set_dormant();
}
