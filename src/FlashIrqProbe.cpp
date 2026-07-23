/**
 * @file    FlashIrqProbe.cpp
 * @brief   Linker-wrapped flash primitives that time the IRQ-off window.
 * @details See FlashIrqProbe.h for why this exists.
 *
 *          SRAM RESIDENCY IS A CORRECTNESS REQUIREMENT, NOT AN OPTIMISATION.
 *          `ota_applier_run` (src/ota/applier.cpp) runs from SRAM and erases
 *          the entire application slot, then reprograms it from staging. Its
 *          `flash_range_erase` calls are redirected here by `--wrap`. A wrapper
 *          living in the app slot would be fetched from a just-erased sector on
 *          the very next call, hanging the chip with a half-written image — an
 *          unrecoverable brick. Hence `__not_in_flash_func` on both wrappers,
 *          a raw MMIO timer read instead of `time_us_32()`, and arithmetic
 *          restricted to 32-bit so the compiler cannot emit a call to a
 *          flash-resident libgcc helper (64-bit divide, in particular).
 *
 *          The build enforces this: tools/check_flash_probe.py fails the build
 *          if either wrapper links outside SRAM.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "FlashIrqProbe.h"

#include <stddef.h>

#include <hardware/structs/timer.h>
#include <pico/platform.h>

extern "C" {

volatile uint32_t g_flashIrqEraseCount = 0;
volatile uint32_t g_flashIrqProgCount  = 0;
volatile uint32_t g_flashIrqMaxUs      = 0;
volatile uint32_t g_flashIrqTotalUs    = 0;
volatile uint32_t g_flashIrqOver1msCount  = 0;

void __real_flash_range_erase(uint32_t offset, size_t count);
void __real_flash_range_program(uint32_t offset, const uint8_t* data, size_t count);

/* Raw 32-bit microsecond counter. `timer_hw` is a fixed MMIO address, so this
 * is a literal load plus a read — no call, no XIP fetch, valid with interrupts
 * disabled. Wraps every ~71 min; unsigned subtraction stays correct for the
 * millisecond-scale intervals measured here. */
static __always_inline uint32_t probe_now_us(void) {
	return timer_hw->timerawl;
}

static __always_inline void probe_account(uint32_t us) {
	g_flashIrqTotalUs += us;
	if (us > g_flashIrqMaxUs) g_flashIrqMaxUs = us;
	if (us > 1000u) g_flashIrqOver1msCount++;
}

void __not_in_flash_func(__wrap_flash_range_erase)(uint32_t offset, size_t count) {
	const uint32_t t0 = probe_now_us( );
	__real_flash_range_erase(offset, count);
	probe_account(probe_now_us( ) - t0);
	g_flashIrqEraseCount++;
}

void __not_in_flash_func(__wrap_flash_range_program)(uint32_t offset,
                                                     const uint8_t* data,
                                                     size_t count) {
	const uint32_t t0 = probe_now_us( );
	__real_flash_range_program(offset, data, count);
	probe_account(probe_now_us( ) - t0);
	g_flashIrqProgCount++;
}

} /* extern "C" */
