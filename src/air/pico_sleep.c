/**
 * @file air/pico_sleep.c
 * @brief Vendored pico-sdk hardware_sleep dormant implementation (RP2040).
 *
 * Mirrors the upstream sleep_run_from_xosc + sleep_goto_dormant_until sequence:
 *   rtc_set_alarm -> xosc_init -> SLEEPDEEP -> switch clk_sys to XOSC ->
 *   xosc_dormant -> __wfi.
 * Wake is a full reset (dormant), so no recover_from_sleep is needed here.
 */

#include "pico_sleep.h"
#include "hardware/clocks.h"
#include "hardware/xosc.h"
#include "hardware/structs/scb.h"
#include "hardware/regs/m0plus.h"

#define XOSC_HZ 12000000u

static void sleep_run_from_xosc(void) {
    uint src_hz = XOSC_HZ;
    uint clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC;
    /* CLK SYS = CLK REF, CLK REF = XOSC */
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                    clk_ref_src,
                    src_hz,
                    src_hz);
    clock_stop(clk_usb);
    clock_stop(clk_adc);
    clock_configure(clk_rtc, 0, CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 0, 0);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, src_hz, src_hz);
}

void sleep_goto_dormant_until(datetime_t *t, dormant_wake_source_callback_t callback) {
    /* Program the RTC alarm (wake source). */
    rtc_set_alarm(t, callback);

    /* Ensure the crystal oscillator is running. */
    xosc_init( );

    /* Deep sleep on WFI (dormant). */
    scb_hw->scr |= M0PLUS_SCR_SLEEPDEEP_BITS;

    /* Run the system from the crystal so the PLLs can power down. */
    sleep_run_from_xosc( );

    /* Put the XOSC in dormant mode (keeps running for the RTC). */
    xosc_dormant( );

    /* Enter dormant; wake is a full reset. */
    __asm volatile("wfi");
}
