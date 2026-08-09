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

#include <hardware/regs/addressmap.h>  /* XIP_NOCACHE_NOALLOC_BASE, for the QSPI probe */
#include <hardware/structs/timer.h>
#include <pico/platform.h>

extern "C" {

volatile uint32_t g_flashIrqEraseCount = 0;
volatile uint32_t g_flashIrqProgCount  = 0;
volatile uint32_t g_flashIrqMaxUs      = 0;
volatile uint64_t g_flashIrqTotalUs    = 0;
volatile uint32_t g_flashIrqOver1msCount  = 0;

volatile uint8_t  g_core1Running          = 0;
volatile uint8_t  g_core1MayExecute       = 0;
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
volatile uint32_t g_webHistScanMaxMs      = 0;
volatile uint32_t g_core1LockWaitLastMs   = 0;

volatile uint32_t g_core1PhaseUs          = 0;
volatile uint32_t g_core1PhaseSeq         = 0;
volatile uint32_t g_core1StallMaxUs       = 0;
volatile uint8_t  g_core1StallPhase       = 0xFF;
volatile uint32_t g_core1PhaseMaxUs[C1P_COUNT] = {0};
volatile uint32_t g_core1XipLastUs        = 0;
volatile uint32_t g_core1XipMaxUs         = 0;
volatile uint8_t  g_core1WaitAlarm        = 0xFF; /* 0xFF until Core 1 claims one */
volatile uint32_t g_core1WaitMaxUs        = 0;
volatile uint32_t g_core1WaitExtraWakes   = 0;

/* Kept in step with enum Core1Phase. Short on purpose: these are printed in a
 * `show metrics` block that is already dense, and parsed by the bench scripts. */
const char* const C1P_NAMES[C1P_COUNT] = {
	"INIT", "RESUME_MTX", "LOOP_TOP", "PARK", "TOUCH_RD", "TOUCH_HDL",
	"THEME_MTX", "DASH_MTX", "SNAPSHOT", "RENDER",
	"ALARM_FLASH", "R_BOOT", "R_FULL", "R_TOPBAR", "R_TOP_PANEL",
	"R_MINMAX", "R_BOT_PANEL", "R_ALARM", "LOOP_TAIL", "LOOP_DELAY", "W_WFE",
	"UI_GRAPH", "UI_SETTINGS"
};
static_assert(sizeof(C1P_NAMES) / sizeof(C1P_NAMES[0]) == C1P_COUNT,
              "C1P_NAMES must name every Core1Phase");

/* Ordinary flash-resident code, unlike the two wrappers below: this runs from
 * Core 0's normal loops with XIP up, never from inside a flash operation. */
void core1StallSample(void) {
	if (!g_core1Running) return;
	/* C1P_INIT is startup, not a stall: driver allocations, the ILI9341 hardware
	 * reset and the first full paint legitimately take a few hundred ms, and
	 * counting them put a ~441 ms floor under a metric whose whole job is to
	 * report what the LOAD did. Core 1 hanging in INIT still shows up — in the
	 * per-phase table and in the heartbeat age. */
	if (g_core1Phase == C1P_INIT) return;
	const uint32_t stamp = g_core1PhaseUs;
	if (stamp == 0) return;                    /* Core 1 has not stamped yet */
	const uint32_t age = timer_hw->timerawl - stamp;
	/* timerawl wraps every ~71 min, and a Core 1 that has been dead longer than
	 * that would report a garbage age as a record. The soft panic fires at 15 s,
	 * so nothing legitimate lands above this bound. */
	if (age > 60u * 1000u * 1000u) return;
	if (age > g_core1StallMaxUs) {
		g_core1StallMaxUs = age;
		g_core1StallPhase = g_core1Phase;
	}
}

/* 32 reads 4 KB apart through the no-cache alias: the stride keeps them on
 * distinct flash pages and the alias keeps the 16 KB cache out of the number, so
 * each one is a real QSPI transaction. The window starts 128 KB into the image —
 * inside the application, so the traffic is read-only and has no side effects.
 * `sink` is volatile so the loop cannot be optimised away. */
void core1XipProbe(void) {
	static volatile uint32_t sink = 0;
	const volatile uint8_t* const p =
		(const volatile uint8_t*)(XIP_NOCACHE_NOALLOC_BASE + 0x20000u);
	const uint32_t t0 = timer_hw->timerawl;
	uint32_t acc = 0;
	for (uint32_t i = 0; i < 32u; i++) acc += p[i * 4096u];
	const uint32_t dt = timer_hw->timerawl - t0;
	sink = acc;
	g_core1XipLastUs = dt;
	if (dt > g_core1XipMaxUs) g_core1XipMaxUs = dt;
}

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
	/* g_core1MayExecute, not g_core1Running: the launch->victim_init entry
	 * window fetches XIP with Running still 0, and it is precisely the
	 * window where an unpaused flash op is the QSPI-arbiter freeze. The
	 * old predicate made that window invisible — "Core1 exposto = 0" was
	 * measured with this blind spot. */
	if (g_core1MayExecute && g_core1FlashSafeDepth <= 0) {
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
