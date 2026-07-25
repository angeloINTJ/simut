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
volatile uint64_t g_flashIrqTotalUs    = 0;
volatile uint32_t g_flashIrqOver1msCount  = 0;

volatile uint8_t  g_core1Running          = 0;
volatile int32_t  g_core1FlashSafeDepth   = 0;
volatile uint32_t g_flashIrqExposed       = 0;
volatile uint32_t g_flashIrqExposedMaxUs  = 0;

/* Core-1 lifecycle observability — see the header for why these are globals. */
volatile uint32_t g_core1HeartbeatMs      = 0;
volatile uint8_t  g_core1UiMode           = 0xFF; /* 0xFF = never reported */
volatile uint32_t g_core1LockoutStuck     = 0;
volatile uint32_t g_core1KillsLockout     = 0;
volatile uint32_t g_core1KillsHealth      = 0;
volatile uint32_t g_core1KillsQuiet       = 0;
volatile uint32_t g_core1Launches         = 0;
volatile uint8_t  g_core1StuckMod0        = 0xFF;
volatile uint8_t  g_core1StuckParked      = 0xFF;
volatile uint8_t  g_core1Phase            = 0;
volatile uint8_t  g_core1StuckPhase       = 0xFF;
volatile uint32_t g_core1PauseStartMs     = 0;
volatile uint32_t g_core1PauseCount       = 0;
volatile uint32_t g_core1PauseMaxMs       = 0;
volatile uint8_t  g_core1PauseMaxMod0     = 0xFF;
volatile uint8_t  g_core1PauseLastMod0    = 0xFF;
volatile uint32_t g_core1Iters            = 0;
volatile uint32_t g_core1IterMaxMs        = 0;
volatile uint32_t g_core1LockWaitMaxMs    = 0;
volatile uint32_t g_core1LockWaitLastMs   = 0;

void __real_flash_range_erase(uint32_t offset, size_t count);
void __real_flash_range_program(uint32_t offset, const uint8_t* data, size_t count);

/* Raw 32-bit microsecond counter. `timer_hw` is a fixed MMIO address, so this
 * is a literal load plus a read — no call, no XIP fetch, valid with interrupts
 * disabled. Wraps every ~71 min; unsigned subtraction stays correct for the
 * millisecond-scale intervals measured here. */
static __always_inline uint32_t probe_now_us(void) {
	return timer_hw->timerawl;
}

/* The total is 64-bit so a long soak cannot silently wrap it and turn the
 * average into nonsense — the exact run this metric exists for. Widening is
 * free here: 64-bit addition inlines to adds/adcs, so no flash-resident libgcc
 * helper is reached. The division stays in CommandManager, which is ordinary
 * flash-resident code and may call __aeabi_uldivmod freely. */
static __always_inline void probe_account(uint32_t us) {
	g_flashIrqTotalUs += us;
	if (us > g_flashIrqMaxUs) g_flashIrqMaxUs = us;
	if (us > 1000u) g_flashIrqOver1msCount++;
	/* Exposure: Core 1 alive and not frozen while XIP is down. */
	if (g_core1Running && g_core1FlashSafeDepth <= 0) {
		g_flashIrqExposed++;
		if (us > g_flashIrqExposedMaxUs) g_flashIrqExposedMaxUs = us;
	}
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
