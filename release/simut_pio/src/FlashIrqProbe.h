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

/* For the C1_PHASE stamp below: timer_hw is a fixed MMIO address, so reading it
 * is a load with no call and no XIP fetch. */
#include <hardware/structs/timer.h>

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
extern volatile uint8_t  g_core1MayExecute;     /**< Set at launch, cleared at kill: covers the
                                                 *   launch->victim_init entry window, where the
                                                 *   core fetches XIP but g_core1Running is still 0. */
extern volatile int32_t  g_core1PauseRefCount;  /**< mirror of DisplayManager::_pauseRefCount for the panic ledger */
extern volatile uint8_t  g_core1QuietActive;    /**< mirror of _quietModeActive for the panic ledger */
extern volatile uint8_t  g_core1Fault;          /**< Core 1 took a hard fault (handler stamps and parks) */
extern volatile uint32_t g_core1FaultPc;        /**< stacked PC at the fault */
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
	C1P_RENDER,          /* render( ): entered, before any branch */
	/* ── Inside render( ), plus the two windows the old enum left uncovered ──
	 *
	 * `fase=9 (C1P_RENDER)` was as far as two sessions got, and it is weaker
	 * evidence than it reads as: render( ) is ~180 lines dispatching six
	 * different draw calls, and the value is STICKY, so it also covers the
	 * window between render( ) returning and the next loop top. A freeze in the
	 * alarm-flash repaint that runs just before it, or in the delay( ) at the
	 * bottom of the loop, would both have printed 9 as well.
	 *
	 * These name each region so no value is a catch-all. */
	C1P_ALARM_FLASH,     /* redrawAlarmFlash / restoreNormalDashboard (pre-render) */
	C1P_R_BOOT,          /* render: boot-screen branch */
	C1P_R_FULL,          /* render: full redraw (chrome + both panels + buttons) */
	C1P_R_TOPBAR,        /* render: drawTopBar */
	C1P_R_TOP_PANEL,     /* render: drawSlotPanel(top) */
	C1P_R_MINMAX,        /* render: min/max timeout repaint */
	C1P_R_BOT_PANEL,     /* render: drawSlotPanel(bottom) + drawBottomButtons */
	C1P_R_ALARM,         /* render: alarm-mask change repaint */
	C1P_LOOP_TAIL,       /* UI dispatch done, before the adaptive delay */
	C1P_LOOP_DELAY,      /* inside delay( ) at the bottom of the loop (SDK path) */
	C1P_W_WFE,           /* inside the private Core-1 wait: asleep in __wfe */
	/* The non-dashboard screens. Without these their cost is charged to whatever
	 * marker the loop set last, which is how a 183 ms graph repaint was reported
	 * as TOUCH_HDL. Split graph from settings because they are different code:
	 * DisplayManager_Graph.cpp draws straight to the TFT (100 direct tft-> calls,
	 * no canvas), the settings screens are partly canvas-based. */
	C1P_UI_GRAPH,        /* graph / stats / detail / loading screens */
	C1P_UI_SETTINGS,     /* settings, auth, calendar, alarm-edit, confirm */
	C1P_COUNT
};
extern volatile uint8_t  g_core1Phase;          /**< Core1Phase: where Core 1 is right now */
extern volatile uint8_t  g_core1StuckPhase;     /**< g_core1Phase at the instant the lockout gave up */

/** Names for the phases above, so a stall reads as a place and not as a number. */
extern const char* const C1P_NAMES[C1P_COUNT];

/* ── Is Core 1 still MOVING? ───────────────────────────────────────────────
 *
 * The phase alone says where Core 1 is, never whether it is making progress —
 * and that is the whole question for the 15 s freeze. Two live hypotheses
 * remain, and this stamp separates them:
 *
 *   - starved of XIP: render( ) crawls but keeps reaching markers, so the AGE
 *     of this stamp stays small while the iteration total grows to seconds;
 *   - blocked on something: the next marker is never reached, so the age grows
 *     to the full length of the stall and the phase names the line.
 *
 * g_core1PhaseSeq is the same signal sampled from outside: its delta between two
 * `show metrics` polls is Core 1's progress rate, in phase transitions. */
extern volatile uint32_t g_core1PhaseUs;        /**< timer_hw->timerawl at the last phase transition */
extern volatile uint32_t g_core1PhaseSeq;       /**< Transitions so far (delta between polls = progress rate) */

/* Worst age of that stamp — sampled BY CORE 0, which is the point.
 *
 * Core 1 cannot time its own freeze: anything it writes after the fact is
 * missing exactly in the run that matters. That blind spot is not hypothetical
 * here — `g_webHistScanMaxMs` reported a 308 ms worst case for a phase that was
 * hanging for 15 s, because it is only written when the scan COMPLETES. So Core 0
 * samples this stamp from feedWdt( ), which every flash-reading loop calls, and
 * keeps the worst age it ever sees together with the phase Core 1 was in. */
extern volatile uint32_t g_core1StallMaxUs;     /**< Longest age of g_core1PhaseUs seen by Core 0 */
extern volatile uint8_t  g_core1StallPhase;     /**< Phase Core 1 was in at that moment */

/* Worst time spent in each phase, accounted by Core 1 on the way OUT.
 *
 * Complements the pair above and shares its blind spot BY CONSTRUCTION: a phase
 * that never ends is never accounted here. Read them together — this table
 * localises a slow phase, g_core1StallMaxUs catches a stuck one. */
extern volatile uint32_t g_core1PhaseMaxUs[C1P_COUNT];

/** Core 0: sample the stamp above. Cheap, lock-free, safe from any context. */
void core1StallSample(void);

/* ── Uncached QSPI read latency, measured by Core 1 on itself ──────────────
 *
 * The live hypothesis for the freeze is that Core 0's continuous LittleFS reads
 * starve Core 1's fetches through the XIP/QSPI path — which would make render( )
 * crawl, not block. This tests it directly instead of by elimination: a fixed
 * number of reads through XIP_NOCACHE_NOALLOC_BASE, an alias that bypasses the
 * 16 KB cache and therefore always reaches the flash. Idle, the result is a
 * hardware constant; if contention is real it inflates under load, and by how
 * much says whether it can account for seconds of render time. */
extern volatile uint32_t g_core1XipLastUs;      /**< Most recent probe */
extern volatile uint32_t g_core1XipMaxUs;       /**< Worst probe since boot */

/** Core 1: run one probe. Read-only flash traffic, no side effects. */
void core1XipProbe(void);

/* ── The private Core-1 wait, and whether it is behaving ───────────────────
 *
 * Core 1 froze for 14.3 s inside delay( ). delay( ) is weak and resolves to
 * sleep_ms -> sleep_until, which couples the two cores three ways, all three
 * verified rather than assumed:
 *
 *   1. every symbol on that path links to flash (nm: delay, sleep_ms,
 *      sleep_until, alarm_pool_add_alarm_at, alarm_pool_irq_handler,
 *      sleep_until_callback — 0x10xxxxxx, none in SRAM), so it cannot run at all
 *      while a flash program/erase has XIP down;
 *   2. it waits behind spin_lock_blocking on a notifier shared with Core 0, and
 *      the SDK documents that primitive as always disabling interrupts — which
 *      is a mechanism for Core 1 being unable to answer the lockout handshake
 *      (`parked=0`, in every capture since the first session);
 *   3. the wake-up is an alarm on the DEFAULT pool, whose IRQ is enabled by
 *      runtime init on CORE 0, so Core 1 cannot wake until Core 0 services it.
 *
 * The replacement removes all three: SRAM-resident, no lock, and its own
 * hardware alarm whose handler is installed FROM Core 1, so the NVIC enable
 * lands on Core 1 and nothing about the wake involves Core 0.
 *
 * g_core1WaitAlarm is 0xFF when no alarm could be claimed, in which case the
 * loop falls back to delay( ) and the phase reads LOOP_DELAY instead of W_WFE —
 * so which path ran is always visible, never assumed. */
extern volatile uint8_t  g_core1WaitAlarm;      /**< hw alarm claimed; 0xFF = fell back to delay( ) */
extern volatile uint32_t g_core1WaitMaxUs;      /**< Longest single wait, end to end */
/* Waits that needed more than one WFE wake. This is NOT lateness: any interrupt
 * on Core 1 returns from __wfe early, the loop re-checks the target and sleeps
 * again, so a large count is normal and healthy. It is here as the residual
 * signal — read it together with g_core1WaitMaxUs, which is what would grow if
 * the wake ever actually ran late. */
extern volatile uint32_t g_core1WaitExtraWakes;

/* Set the phase, stamp when it changed, and account what the previous one cost.
 *
 * timer_hw->timerawl rather than millis( ): two MMIO loads and no 64-bit divide,
 * cheap enough for the ten calls a single render( ) now makes. Core 1 is the only
 * writer of all three; Core 0 only ever reads them, and a torn read across a
 * transition cannot manufacture the multi-second age this exists to catch.
 *
 * The g_core1PhaseUs != 0 guard is not defensive, it is a correction: on the very
 * first call there is no previous stamp, so `now - 0` is the raw timer, and the
 * table reported the whole interval from power-on to Core 1's launch as the worst
 * INIT — 7486 ms of pure artefact, on the same channel that is supposed to settle
 * whether a phase really held for seconds. */
#define C1_PHASE(p) do {                                                      \
	const uint32_t _c1pNow  = timer_hw->timerawl;                             \
	const uint8_t  _c1pPrev = g_core1Phase;                                   \
	const uint32_t _c1pWas  = g_core1PhaseUs;                                 \
	if (_c1pWas != 0 && _c1pPrev < C1P_COUNT) {                               \
		const uint32_t _c1pHeld = _c1pNow - _c1pWas;                          \
		if (_c1pHeld > g_core1PhaseMaxUs[_c1pPrev])                           \
			g_core1PhaseMaxUs[_c1pPrev] = _c1pHeld;                           \
	}                                                                         \
	g_core1Phase   = (uint8_t)(p);                                            \
	g_core1PhaseUs = _c1pNow ? _c1pNow : 1u;                                  \
	g_core1PhaseSeq++;                                                        \
} while (0)

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
