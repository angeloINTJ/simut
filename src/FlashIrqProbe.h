/**
 * @file    FlashIrqProbe.h
 * @brief   T0.1 (completed) — direct measurement of the Core-0 IRQ-off window.
 * @details The wave-1 metric (`flashOpMaxMs`) times the whole FLASH_OP block,
 *          which includes LittleFS bookkeeping and mutex handling. The plan's
 *          acceptance criterion is stated on the *interrupt-off* window, which
 *          is a strict subset of that: LittleFS wraps every program/erase in
 *          `noInterrupts()` / `interrupts()` (framework LittleFS.cpp:181-212).
 *
 *          These counters close that gap by intercepting the two SDK entry
 *          points via `-Wl,--wrap`, so the number reported is the real stall
 *          seen by cyw43/lwIP rather than a proxy for it.
 *
 *          Counters are plain globals, not `SystemMetrics` fields: the probe
 *          runs with interrupts disabled and XIP potentially down, where the
 *          singleton accessor is not reachable. Core 1 never writes flash
 *          (concurrency invariant 1), so no cross-core synchronisation is
 *          needed.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t g_flashIrqEraseCount;  /**< flash_range_erase calls */
extern volatile uint32_t g_flashIrqProgCount;   /**< flash_range_program calls */
extern volatile uint32_t g_flashIrqMaxUs;       /**< Longest single IRQ-off window */
extern volatile uint64_t g_flashIrqTotalUs;     /**< Sum, for the average (64-bit: a long soak wraps 32) */
extern volatile uint32_t g_flashIrqOver1msCount;   /**< Windows longer than 1 ms */

/* Core-1 exposure tracking.
 *
 * A flash program/erase disables XIP. If Core 1 is executing from flash at
 * that moment its fetch stalls on the QSPI arbiter — the mechanism behind the
 * WDT reboots fixed in e035791. Core1FlashPause exists to prevent it, but the
 * raw op count cannot tell a protected write from an exposed one, and most of
 * the boot-time writes are harmless simply because Core 1 has not launched.
 *
 * So count the case that actually matters: a program/erase issued while Core 1
 * is running AND not frozen. Any non-zero value here is a real exposure window
 * and names a code path that is missing its Core1FlashPause. */
extern volatile uint8_t  g_core1Running;        /**< Core 1 launched and looping */
extern volatile int32_t  g_core1FlashSafeDepth; /**< >0 => Core 1 locked out or parked */
extern volatile uint32_t g_flashIrqExposed;     /**< Ops with Core 1 running and unfrozen */
extern volatile uint32_t g_flashIrqExposedMaxUs;/**< Longest such window */

/* ── Core-1 lifecycle observability ────────────────────────────────────────
 *
 * The Core-1 lifecycle was invisible in the field. A stuck lockout only ever
 * reached a `Serial.println`, and every kill/relaunch was silent, so a STALLED
 * display was indistinguishable from a healthy one in `show metrics` — and in
 * every automated download test, all of which reported PASS while never
 * checking that Core 1 was alive at all.
 *
 * These are plain globals for the same reason as the probe counters above:
 * `show metrics` is assembled in CommandManager, which holds no DisplayManager
 * reference, and the increments sit on paths where the singleton accessor is
 * not worth the coupling. Read-mostly, single-writer per counter.
 *
 * Diagnostics only — nothing here changes behaviour. */
extern volatile uint32_t g_core1HeartbeatMs;    /**< millis( ) stamped by Core 1 each loop; age = liveness */
extern volatile uint8_t  g_core1UiMode;         /**< _uiMode as last seen by Core 1 (resolves touch-probe ambiguity) */
extern volatile uint32_t g_core1LockoutStuck;   /**< Lockout retry budget exhausted (3 s) */
extern volatile uint32_t g_core1KillsLockout;   /**< Hard resets from the lockout timeout (P1) */
extern volatile uint32_t g_core1KillsHealth;    /**< restartCore1( ) from the health watchdog (P2) */
extern volatile uint32_t g_core1KillsQuiet;     /**< Quiet-mode resets (P3) */
extern volatile uint32_t g_core1Launches;       /**< multicore_launch_core1 calls actually issued */
extern volatile uint8_t  g_core1StuckMod0;      /**< Core-0 TraceModule when the lockout got stuck (names the pause requester) */
extern volatile uint8_t  g_core1StuckParked;    /**< 1 = Core 1 had ACKed the quiesce; 0 = quiesce also timed out */

/* Where Core 1 is executing, sampled continuously by Core 1 itself.
 *
 * `parked=0` at every stuck lockout proved Core 1 is genuinely blocked for
 * seconds during a download rather than ignoring the SDK handshake — but not
 * WHERE. These phases answer that: g_core1StuckPhase is the snapshot taken at
 * the instant the lockout gave up, which is the moment that matters. */
enum Core1Phase {
	C1P_INIT = 0,        /* entry: driver allocations, ts->begin, first paint */
	C1P_RESUME_MUTEX,    /* post-reset resume: mutex_enter_blocking(_stateMutex) */
	C1P_LOOP_TOP,        /* top of the render loop */
	C1P_PARK,            /* parked for the quiesce handshake */
	C1P_TOUCH_READ,      /* XPT2046 SPI read */
	C1P_TOUCH_HANDLE,    /* handleTouch (may draw) */
	C1P_THEME_MUTEX,     /* theme-change branch: blocking mutex + full repaint */
	C1P_DASH_MUTEX,      /* dashboard alarm-nav branch: blocking mutex */
	C1P_SNAPSHOT,        /* pullSnapshot (1 ms timeout, should never block) */
	C1P_RENDER           /* render( ): SPI burst */
};
extern volatile uint8_t  g_core1Phase;          /**< Core1Phase: where Core 1 is right now */
extern volatile uint8_t  g_core1StuckPhase;     /**< g_core1Phase at the instant the lockout gave up */

/* How long Core 1 is actually held frozen, and by whom.
 *
 * The phase markers showed Core 1 stalls INSIDE render( ) for 5-7 s, and
 * render( ) has no blocking primitive — so it is being frozen, not running
 * slowly. The open question is whether the freeze is simply a pause that Core 0
 * holds for seconds (a scope far longer than any flash op needs). These answer
 * it directly: duration of the longest completed pause, the Core-0 trace module
 * that requested it, and — for sampling — the start stamp of a pause still in
 * flight, which is what a coarse `show metrics` poll otherwise misses. */
extern volatile uint32_t g_core1PauseStartMs;   /**< millis( ) when the current pause began; 0 = none in flight */
extern volatile uint32_t g_core1PauseCount;     /**< Pauses taken (refcount 0 -> 1 transitions) */
extern volatile uint32_t g_core1PauseMaxMs;     /**< Longest pause held, end to end */
extern volatile uint8_t  g_core1PauseMaxMod0;   /**< Core-0 TraceModule that requested that longest pause */
extern volatile uint8_t  g_core1PauseLastMod0;  /**< Core-0 TraceModule of the most recent pause request */

/* Display fluidity, as a number.
 *
 * Heartbeat AGE only says "stalled or not" at the instant of a poll. These two
 * give the shape of it: iterations completed (delta over an interval = the
 * effective frame rate of the UI loop) and the worst single iteration, which is
 * the stutter a user actually perceives. Both are written by Core 1 only. */
extern volatile uint32_t g_core1Iters;          /**< loopCore1 iterations completed */
extern volatile uint32_t g_core1IterMaxMs;      /**< Longest single iteration (stutter) */

/* How long a SUCCESSFUL lockout takes to be granted, and how many pause
 * requests were abandoned. Shortening the retry budget is only safe if
 * successful lockouts land well inside it — otherwise a shorter budget just
 * converts successes into Core-1 kills. Measure before trusting. */
extern volatile uint32_t g_core1LockWaitMaxMs;  /**< Worst wait for a granted lockout */
/* Longest run of the history handler's file-scan + decimation-estimate phase.
 * That phase ran with no watchdog feed and no handler-deadline check, and three
 * successive autopsies walked the Core-0 stall down to it (WEB_SERVER ->
 * WEB_HIST -> WEB_HSCAN). Feeding the watchdog stops the reboot; this number
 * says whether the phase is ALSO a multi-second freeze of the whole device,
 * which the reboot was previously hiding. */
extern volatile uint32_t g_webHistScanMaxMs;
extern volatile uint32_t g_core1LockWaitLastMs; /**< Wait of the most recent granted lockout */

#ifdef __cplusplus
}
#endif
