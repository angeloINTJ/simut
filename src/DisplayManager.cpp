/**
 * @file DisplayManager.cpp
 * @brief Implementation of DisplayManager — Core 1 render loop, touch handling, and all UI screens.
 * @details Contains the complete rendering engine: Core 1 entry point, snapshot-
 * based dirty rendering, dashboard with ambient/slot panels, graph
 * plotting with dual Y-axis, settings menus (themes, alarms, sounds,
 * language, password, calibration, license), authentication keypad with
 * scrambled layout and lockout, alarm flash animation with per-slot
 * masking, and the i18n dictionary for 2 languages (EN + PT).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "LogManager.h"
#include "FlashIrqProbe.h" /* Core-1 exposure flags for the flash probe */
#if SIMUT_DISPLAY_TFT
#include "DisplayManager_Fonts.h"
#endif
#include "DisplayManager_FmtFloat.h"
#include "HelpLicenseEN.h" /* LICENSE_TEXT_EN inline in PROGMEM */
#include <LittleFS.h>

#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include "hardware/irq.h"   /* Core-1 private wait: exclusive alarm handler */
#include "hardware/timer.h" /* hardware_alarm_claim_unused, TIMER_IRQ_0 */
#include <stdio.h>
#include <stdlib.h>

#include "pico/multicore.h"


/* Reduced from 8 to 2 languages (EN + PT) to save flash.
 * The other 6 languages remain in git history. */


/* LICENSE preferred from .lng (@LICENSE), unaccented
 * to ASCII for display. Without .lng or for EN: fallback /license_en.txt.
 * Loaded into RAM (_licenseBuf) when the user changes language.
 * setLanguage called only by Core 0 — LittleFS free of race conditions. */
static char _licenseBuf[2048];

static void loadLicenseFromFs(int langIdx) {
	/* PT (and any non-EN): try @LICENSE from active .lng. */
	if (langIdx != LANG_EN) {
		const char* langLic = DisplayManager::getActiveLicenseText( );
		if (langLic) {
			DisplayManager::unaccent(langLic, _licenseBuf, sizeof(_licenseBuf));
			return;
		}
	}
	/* EN always from PROGMEM (LICENSE_TEXT_EN), no FS dependency. */
	size_t i = 0;
	char c;
	while (i + 1 < sizeof(_licenseBuf) &&
	       (c = (char)pgm_read_byte(&LICENSE_TEXT_EN[i])) != '\0') {
		_licenseBuf[i++] = c;
	}
	_licenseBuf[i] = '\0';
}

static int wrapLineCount(const char* text, int maxCols) {
	int lines = 1;
	int col = 0;
	while (*text) {
		if (*text == '\n') { lines++; col = 0; text++; continue; }
		if (*text == ' ') { if (col > 0 && col < maxCols) col++; text++; continue; }

		int wlen = 0;
		const char* w = text;
		while (*w && *w != ' ' && *w != '\n') { wlen++; w++; }

		if (col > 0 && col + wlen > maxCols) { lines++; col = 0; }
		col += wlen;
		text += wlen;
	}
	return lines;
}


#if !SIMUT_DISPLAY_ALPHA
static void renderWrapped(Adafruit_ILI9341* tft, const char* text,
                           int x0, int y0, int maxCols, int lineH,
                           int skip, int maxVis) {
	int curLine = 0;
	int col = 0;
	while (*text) {
		if (curLine >= skip + maxVis) break;
		if (*text == '\n') { curLine++; col = 0; text++; continue; }
		if (*text == ' ') { if (col > 0 && col < maxCols) col++; text++; continue; }

		char word[52];
		int wlen = 0;
		while (*text && *text != ' ' && *text != '\n' && wlen < 50) {
			word[wlen++] = *text++;
		}
		word[wlen] = '\0';

		if (col > 0 && col + wlen > maxCols) { curLine++; col = 0; }
		if (curLine >= skip + maxVis) break;

		if (curLine >= skip) {
			int sy = y0 + (curLine - skip) * lineH;
			tft->setCursor(x0 + col * 6, sy);
			tft->print(word);
		}
		col += wlen;
	}
}

#endif // !SIMUT_DISPLAY_ALPHA

DisplayManager* _instance = nullptr;


constexpr int16_t DisplayManager::CAL_SCR_X[4];
constexpr int16_t DisplayManager::CAL_SCR_Y[4];

DisplayManager::DisplayManager( ) {
	_instance = this;
	mutex_init(&_stateMutex);
	/* UI events: SPSC lock-free ring (see DisplayManager.h) — no init
	 * needed beyond the zeroed indices. */
	_sharedState.slotTemp = NAN;
	_sharedState.slotValid = false;
	_sharedState.topSlotTemp = NAN; _sharedState.topSlotValid = false;
	_sharedState.selectedSlotIdx = 0;
	_sharedState.wifiRssi = -100;
	_sharedState.btActive = false;
	_sharedState.isBooting = true;
	_sharedState.showSkipButton = false;
	_sharedState.apProgressPct = -1;
	for(int i = 0; i < 5; i++) {
		_sharedState.bootLogs[i].key = (int16_t)TR_KEYS_COUNT;
		_sharedState.bootLogs[i].suffix[0] = '\0';
	}
	strcpy(_sharedState.timeString, "--/-- --:--");
	strcpy(_sharedState.slotName, "Sensor 1");
	_lastRenderedState.isBooting = false;
	_lastRenderedState.apProgressPct = -2;
	_lastRenderedState.selectedSlotIdx = -1;
	_isDirty = true;
	_currentPage = 0;
	_lastTouchTime = 0;
	_btnHoldStartTime = 0;
	_lastPressedBtn = -1;
	_menuSelection = 0;
	_isPausedForFlash = false;
	_lastHeartbeat = millis( );
	_uiMode = MODE_DASHBOARD;
	_webBusyUser[0] = '\0';
	_repaintGraph = false;
	_repaintLoading = false;
	_loadingDrawn = false;
	_themeChanged = false;
	_forceFullRedraw = false;
	_rawTouchState = false;
	_skipPressed = false;
}

void DisplayManager::begin( ) {}

/* Wave 2 / invariant 3 (docs/CONCURRENCY.md): mutex ownership probe.
 * pico-sdk mutexes are non-recursive; mutex_try_enter by the owning core
 * fails and reports the owner — exactly the signal we need. If the try
 * SUCCEEDS, we did not previously hold it (release immediately). */
bool DisplayManager::stateMutexHeldByCurrentCore( ) {
	if (!_instance) return false;
	uint32_t owner = 0;
	if (mutex_try_enter(&_instance->_stateMutex, &owner)) {
		mutex_exit(&_instance->_stateMutex);
		return false;
	}
	return owner == get_core_num( );
}

#ifdef SIMUT_CONCURRENCY_ASSERTS
/* Free-function bridge declared in ConcurrencyAsserts.h so StorageManager
 * does not need to include DisplayManager.h. */
bool simutStateMutexHeldByCurrentCore( ) {
	return DisplayManager::stateMutexHeldByCurrentCore( );
}
#endif

/* THE single funnel for every Core-1 launch.
 *
 * multicore_launch_core1_raw( ) is an UNBOUNDED push/pop FIFO handshake: it
 * repeats a 6-word sequence until Core 1 echoes each word. A core that is already
 * running never echoes — its lockout victim handler drains the words and ignores
 * anything that is not a request_id — so Core 0 spins there forever with the
 * watchdog unfed. Field evidence: every [FTL] in the persisted log carries ctx=0,
 * the HW-watchdog branch of the autopsy (Core 0 stalled, NOT Core 1 declared
 * dead), and each one follows an APP_CORE1_DEAD — that is, follows a
 * restartCore1( ). Funnelling every launch here turns that hang into a no-op. */
void DisplayManager::launchCore1IfAbsent( ) {
	if (_core1Launched) return;
	_core1Launched = true;
	g_core1Launches++;
	/* From the first launched instruction the core fetches XIP — the flash
	 * exposure accounting must see this window, not just victim-ready. */
	g_core1MayExecute = 1;
	{ LogManager::TraceScope _t(0, MOD_C1_LAUNCH); multicore_launch_core1(core1Entry); }
}

/* Call from EVERY site that resets Core 1, immediately before the reset.
 *
 * Draining Core 0's inbox first is the other half of the reboot fix:
 * multicore_reset_core1( ) ends with an untimed multicore_fifo_pop_blocking( )
 * and does NOT drain beforehand, so a stale word left by an aborted lockout
 * handshake gets consumed in place of Core 1's post-reset zero (the SDK assert is
 * a no-op in release). The real zero then stays queued and every later handshake
 * is one word out of phase — which is how an untimed pop or launch waits forever.
 *
 * Clearing _core1Launched re-arms the funnel: miss it at a kill site and the
 * matching recovery launch silently becomes a no-op, leaving the display dead for
 * good. It deliberately does NOT touch _core1Ready, which also gates the RELEASE
 * direction of pauseRendering( ) — the path that relaunches Core 1 after the
 * lockout fallback. */
void DisplayManager::markCore1Down( ) {
	g_core1Running = 0;
	g_core1MayExecute = 0;
	_core1Launched = false;
	/* Invalidate the phase stamp at the kill, not at the relaunch.
	 * g_core1PhaseUs is an ordinary global and survives multicore_reset_core1,
	 * so without this the first C1_PHASE after a relaunch measures `now - the
	 * stamp from before the kill` and charges the entire dead window to whatever
	 * phase Core 1 happened to be in when it was killed. A run with 24 kills
	 * produced LOOP_TOP=62036 ms and PARK=62032 ms that way — numbers that read
	 * as spectacular new stalls and are only bookkeeping. The zero is honoured by
	 * the guard in C1_PHASE and by core1StallSample. */
	g_core1PhaseUs = 0;
	multicore_fifo_drain( );
}

void DisplayManager::startCore1( ) { launchCore1IfAbsent( ); }

void DisplayManager::restartCore1( ) {
	/* Clean up lockout state before resetting Core 1.
	 * multicore_reset_core1() stops Core 1 immediately — if Core 1 was
	 * inside a multicore_lockout at reset time, the SDK's internal
	 * lockout mutex is left in an unbalanced state. Without this cleanup,
	 * ALL subsequent multicore_lockout_start_* calls on Core 0 hang
	 * (timeout after 500ms, retry loop), producing the recurring
	 * "[DSP] Lockout stuck >10s" error on every flash operation. */
	g_core1KillsHealth++;
	__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
	{ LogManager::TraceScope _t(0, MOD_C1_ENDLOCK); multicore_lockout_end_blocking(); }
	markCore1Down( );
	{ LogManager::TraceScope _t(0, MOD_C1_RESET); multicore_reset_core1( ); }
	delay(50);
	mutex_init(&_stateMutex);
	_pauseStartTime = 0;
	_isPausedForFlash = false;
	/* We relaunch below, so nothing is left for the unpause path to do.
	 * Leaving this set made pauseRendering(false) take its hard-reset branch
	 * and launch a SECOND time onto the core started here. */
	__atomic_store_n(&_core1HardReset, false, __ATOMIC_RELEASE);
	_lastHeartbeat = millis( );
	launchCore1IfAbsent( );
	/* Past every untimed SDK call. A stall traced here happened AFTER the kill
	 * sequence completed, which is a different bug from stalling inside it —
	 * launchCore1IfAbsent restores the caller's module on its way out, so
	 * without this the two were indistinguishable in the autopsy. */
	TRACE_MOD(0, MOD_C1_KILLED);
}

void DisplayManager::setLanguage(int langId) {
	if (langId >= 0 && langId < LANG_COUNT) _currentLangIdx = langId;
	else _currentLangIdx = 1;
	/* Loads license from FS into _licenseBuf. Called only on
	 * Core 0 (boot via setup, or EVT_APPLY_LANG via AppManager). LittleFS
	 * access is safe in these contexts. */
	loadLicenseFromFs(_currentLangIdx);
	/* Forces boot screen re-render to retranslate bootLogs already
	 * shown in EN before .lng loaded. Render() boot path detects
	 * the flag and sets fullRedraw. */
	_langChanged = true;
}



/**
 * @brief Truncates text to fit within maxPixelW pixels in the current GFX font.
 *
 * If the original text already fits, it is copied in full to out.
 * Otherwise, removes characters from the end and appends "..." so
 * the result fits within the maximum width. The font must already be set
 * in the GFX context before calling.
 */
#if SIMUT_DISPLAY_TFT
void DisplayManager::truncateText(Adafruit_GFX* gfx, const char* src,
                                   char* out, size_t outSize, int16_t maxPixelW) {
	if (!gfx || !src || !out || outSize < 4) {
		if (out && outSize > 0) out[0] = '\0';
		return;
	}

	/* Measure original text width */
	int16_t bx, by;
	uint16_t tw, th;
	gfx->getTextBounds(src, 0, 0, &bx, &by, &tw, &th);

	/* If it fits entirely, copy and return */
	if ((int16_t)tw <= maxPixelW) {
		strncpy(out, src, outSize - 1);
		out[outSize - 1] = '\0';
		return;
	}

	/* Measure ellipsis width */
	uint16_t ellW, ellH;
	gfx->getTextBounds("...", 0, 0, &bx, &by, &ellW, &ellH);
	int16_t targetW = maxPixelW - (int16_t)ellW;
	if (targetW < 0) targetW = 0;

	/* Binary search for maximum length that fits */
	int srcLen = (int)strlen(src);
	int lo = 0, hi = srcLen;
	int best = 0;

	while (lo <= hi) {
		int mid = (lo + hi) / 2;

		/* Build candidate string in buffer */
		int copyLen = mid;
		if (copyLen > (int)(outSize - 4)) copyLen = (int)(outSize - 4);
		memcpy(out, src, copyLen);
		out[copyLen] = '\0';

		gfx->getTextBounds(out, 0, 0, &bx, &by, &tw, &th);

		if ((int16_t)tw <= targetW) {
			best = copyLen;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}

	/* Remove trailing spaces before ellipsis */
	while (best > 0 && out[best - 1] == ' ') best--;

	/* Build final result */
	memcpy(out, src, best);
	out[best] = '.';
	out[best + 1] = '.';
	out[best + 2] = '.';
	out[best + 3] = '\0';
}

/**
 * @brief Same as truncateText, but a suffix is reserved before the name
 *        competes for what is left.
 * @details truncateText shortens the whole string, so "Temperature MAX" loses
 *          the MAX first — the one part of an alarm-limit label that must never
 *          disappear, because it is what distinguishes the two rows of a
 *          channel. Here the suffix width is subtracted up front and only the
 *          name is searched, giving "Conduct... MAX" instead of "Conductivit...".
 *          A suffix that does not fit on its own is written alone.
 */
void DisplayManager::truncateTextKeepSuffix(Adafruit_GFX* gfx, const char* name,
                                             const char* suffix, char* out,
                                             size_t outSize, int16_t maxPixelW) {
	if (!gfx || !name || !suffix || !out || outSize < 5) {
		if (out && outSize > 0) out[0] = '\0';
		return;
	}

	const size_t sufLen = strlen(suffix);
	if (sufLen + 5 > outSize) { out[0] = '\0'; return; }

	int16_t bx, by;
	uint16_t tw, th;

	/* Fits whole? Then no ellipsis at all. */
	size_t nameLen = strlen(name);
	if (nameLen + sufLen < outSize) {
		memcpy(out, name, nameLen);
		memcpy(out + nameLen, suffix, sufLen + 1);
		gfx->getTextBounds(out, 0, 0, &bx, &by, &tw, &th);
		if ((int16_t)tw <= maxPixelW) return;
	}

	/* Budget for the name = width minus the suffix and the ellipsis. Both are
	 * measured in the context's current font, not assumed. */
	uint16_t sufW, ellW, dummyH;
	gfx->getTextBounds(suffix, 0, 0, &bx, &by, &sufW, &dummyH);
	gfx->getTextBounds("...", 0, 0, &bx, &by, &ellW, &dummyH);
	int16_t targetW = maxPixelW - (int16_t)sufW - (int16_t)ellW;
	if (targetW < 0) targetW = 0;

	const int maxName = (int)(outSize - sufLen - 4);
	int lo = 0, hi = (int)nameLen;
	int best = 0;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int copyLen = mid;
		if (copyLen > maxName) copyLen = maxName;
		memcpy(out, name, copyLen);
		out[copyLen] = '\0';
		gfx->getTextBounds(out, 0, 0, &bx, &by, &tw, &th);
		if ((int16_t)tw <= targetW) { best = copyLen; lo = mid + 1; }
		else { hi = mid - 1; }
	}

	while (best > 0 && out[best - 1] == ' ') best--;

	memcpy(out, name, best);
	out[best] = '.';
	out[best + 1] = '.';
	out[best + 2] = '.';
	memcpy(out + best + 3, suffix, sufLen + 1);
}

#endif // SIMUT_DISPLAY_TFT
bool DisplayManager::isMenuActive( ) {
	mutex_enter_blocking(&_stateMutex);
	bool active = (_uiMode >= MODE_AUTH);
	mutex_exit(&_stateMutex);
	return active;
}


bool DisplayManager::isDisplayBusy( ) {
	mutex_enter_blocking(&_stateMutex);
	bool busy = (_uiMode != MODE_DASHBOARD);
	mutex_exit(&_stateMutex);
	return busy;
}


bool DisplayManager::isHeavyRendering( ) {
	mutex_enter_blocking(&_stateMutex);
	bool heavy = (_uiMode == MODE_GRAPH_LOADING || _uiMode == MODE_GRAPH_VIEW
	              || _uiMode == MODE_GRAPH_DETAIL);
	mutex_exit(&_stateMutex);
	return heavy;
}

void DisplayManager::pauseRendering(bool pause) {

	if (!_core1Ready) return;
	if (pause) {

		int32_t prev = __atomic_fetch_add(&_pauseRefCount, 1, __ATOMIC_ACQ_REL);
		if (prev == 0) {
			_pauseStartTime = millis( );
			/* Who is freezing Core 1, and for how long. Recorded at the 0->1
			 * transition because that is the pause which actually holds the core. */
			g_core1PauseStartMs = _pauseStartTime;
			g_core1PauseLastMod0 = LogManager::instance( ).getModule(0);
			g_core1PauseCount++;
			LogManager::instance( ).setCorePaused(1, true);

			/*
			 * Timeout-based lockout instead of blocking forever.
			 * Consistent autopsy showed C0=[CORE1_LOCK] — Core 0
			 * stuck in `multicore_lockout_start_blocking` without WDT feed.
			 * Possible cause: SDK lockout_mutex stuck from an unbalanced
			 * start/end (e.g. restartCore1 mid-lockout).
			 *
			 * Fix: loop with `start_timeout_us(500ms)` + `watchdog_update`
			 * between attempts. If 5s passed without success, calls
			 * `end_blocking` to clean internal state (increments
			 * lockout_request_id, releases mutex if stuck) and retries.
			 * Retry forever — prefer a visibly "slow" system over
			 * a reboot with truncated autopsy.
			 */
			/* Quiesce BEFORE the IRQ lockout (same T1.1 handshake as
			 * requestQuietMode): park Core 1 at the top of its loop —
			 * outside malloc/free, the event-queue spinlock and any SPI
			 * burst — so the lockout freezes it at a point where it holds
			 * NO shared lock. A lockout landing mid-malloc/mid-log leaves
			 * that lock frozen-held; any later Core-0 attempt to take it
			 * inside the flash section blocks forever with the WDT unfed
			 * (autopsy: C0=[HIST_FLASH] C1=[DISPLAY]). Timeout 200 ms:
			 * the fallback is exactly the previous behavior (freeze
			 * wherever Core 1 happens to be). */
			if (__atomic_load_n(&_core1Ready, __ATOMIC_ACQUIRE)) {
				__atomic_store_n(&_quiescePlease, true, __ATOMIC_RELEASE);
				uint32_t q0 = millis( );
				while (!__atomic_load_n(&_core1Parked, __ATOMIC_ACQUIRE) &&
				       !timeSince(q0, 200)) {
					tight_loop_contents( );
				}
			}

			/* B: SHORT lockout budget.
			 * Measured on the bench: a lockout that is not granted almost at once is
			 * not granted at all, and the old 3 s budget was 3 s of Core 0 spinning —
			 * it stalls the transfer AND sits inside the 5-15 s freeze the user sees,
			 * because the kill that follows adds a Core-1 relaunch and a full repaint.
			 * g_core1LockWaitMaxMs measures what a GRANTED lockout really costs, so
			 * this number can be judged by data instead of taste. The hard reset stays
			 * as the fallback: program/erase with Core 1 loose in XIP is the reboot
			 * class of e035791, so proceeding unprotected is never an option. */
			const uint32_t lockBudgetMs = 400;
			uint32_t retryStart = millis( );
			uint32_t lastCleanup = retryStart;
			/* Phase-align the handshake before it starts, and again before
			 * every retry: a stale victim reply left in THIS core's inbox by
			 * an earlier timed-out attempt shifts every later dialogue one
			 * word out of phase. The launch funnel learned this in July; the
			 * lockout dance never did. Measured under storm as the residual
			 * W_WFE wedge: Core 1 parked in the victim spin awaiting an END
			 * the desynced dialogue never delivered, its own alarm long
			 * fired and auto-disarmed (fingerprint flags=4, delta −10…−13 s,
			 * five for five). Draining eats only replies on our side — the
			 * victim ignores any word that is not a protocol magic. */
			multicore_fifo_drain( );
			while (!multicore_lockout_start_timeout_us(100000)) {
				watchdog_update( );
				if (timeSince(lastCleanup, 200)) {
					/* Lockout state possibly corrupted: clean before
					 * new attempt. end_blocking is idempotent if
					 * mutex has already been released. */
					multicore_fifo_drain( );
					{ LogManager::TraceScope _t(0, MOD_C1_ENDLOCK); multicore_lockout_end_blocking( ); }
					lastCleanup = millis( );
					watchdog_update( );
				}
				multicore_fifo_drain( );
				/* After 3s without success, fall back to hard reset.
				 * multicore_reset_core1() stops Core 1 immediately - no
				 * handshake needed. All flash ops are safe. */
				if (timeSince(retryStart, lockBudgetMs)) {
					Serial.println("[DSP] Lockout nao concedido no orcamento; hard reset Core1");
					g_core1LockoutStuck++;
					g_core1KillsLockout++;
					/* Name the culprit: which Core-0 path requested this pause, and had
					 * Core 1 actually ACKed the quiesce? A stuck lockout with parked=1
					 * means Core 1 was responsive and the SDK handshake still failed;
					 * parked=0 means Core 1 never reached its park point. Different bugs. */
					g_core1StuckMod0 = LogManager::instance( ).getModule(0);
					g_core1StuckParked = __atomic_load_n(&_core1Parked, __ATOMIC_ACQUIRE) ? 1 : 0;
					g_core1StuckPhase = g_core1Phase;
					__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
					LogManager::instance( ).setCorePaused(1, true);
					{ LogManager::TraceScope _t(0, MOD_C1_ENDLOCK); multicore_lockout_end_blocking( ); }
					markCore1Down( );
	{ LogManager::TraceScope _t(0, MOD_C1_RESET); multicore_reset_core1( ); }
					/* Same rationale as requestQuietMode: Core 1 may have
					 * died holding _stateMutex — reinit at kill time so
					 * Core-0 setters can't block on a corpse-held mutex. */
					mutex_init(&_stateMutex);
					accountPauseEnd( );
					_pauseStartTime = 0;
					__atomic_store_n(&_quiescePlease, false, __ATOMIC_RELEASE);
					__atomic_store_n(&_core1Parked, false, __ATOMIC_RELEASE);
					_core1HardReset = true;
					/* Same reason as at the end of restartCore1( ): mark the window
					 * between "Core 1 is dead" and the caller's next marker, so the
					 * flash op this pause exists for can be told apart from the kill. */
					TRACE_MOD(0, MOD_C1_KILLED);
					return;
				}
			}
			{
				const uint32_t waited = millis( ) - retryStart;
				g_core1LockWaitLastMs = waited;
				if (waited > g_core1LockWaitMaxMs) g_core1LockWaitMaxMs = waited;
			}
			/* Lockout holds Core 1 frozen (inside the park loop if the
			 * quiesce succeeded). Release the park request now: when the
			 * lockout ends on unpause, Core 1 re-checks _quiescePlease,
			 * sees false, and resumes normally. */
			__atomic_store_n(&_quiescePlease, false, __ATOMIC_RELEASE);

			/* Core 1 is frozen at IRQ level from here until the unpause, so
			 * its loop — and with it _lastHeartbeat — stops advancing. Mark
			 * it, so getHeartbeat( ) does not report deliberate downtime as
			 * a stalled core. This flag was declared, cleared in five places
			 * and read by getHeartbeat( ), but never once set: the guard has
			 * been dead code, and every millisecond spent paused for flash
			 * counted against the 10 s health threshold. */
			_isPausedForFlash = true;
			g_core1FlashSafeDepth++;
		}
	} else {
		int32_t prev = __atomic_fetch_sub(&_pauseRefCount, 1, __ATOMIC_ACQ_REL);
		if (prev <= 1) {

			__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
			accountPauseEnd( );
			_pauseStartTime = 0;
			if (__atomic_exchange_n(&_core1HardReset, false, __ATOMIC_ACQ_REL)) {
				/* Core 1 was hard-reset by the lockout timeout: there is no
				 * live victim to release, so multicore_lockout_end_blocking
				 * must NOT run (it would handshake with a dead core). Reinit
				 * shared state exactly like releaseQuietMode() — Core 1 may
				 * have died holding _stateMutex — then relaunch fresh. */
				mutex_init(&_stateMutex);
				__atomic_store_n(&_quiescePlease, false, __ATOMIC_RELEASE);
				__atomic_store_n(&_core1Parked, false, __ATOMIC_RELEASE);
				_isPausedForFlash = false;
				_lastHeartbeat = millis( );
				LogManager::instance( ).setCorePaused(1, false);
				launchCore1IfAbsent( );
				/* core1Entry re-runs victim_init and sets _core1Ready. */
			} else {
				{ LogManager::TraceScope _t(0, MOD_C1_ENDLOCK); multicore_lockout_end_blocking( ); }
				/* Core 1 resumes here, but its first loop iteration — and so
				 * the next _lastHeartbeat write — is microseconds away, while
				 * _pauseStartTime has already been zeroed above. Stamp the
				 * heartbeat on release so the health watchdog never sees the
				 * frozen value in that gap and hard-resets a healthy core. */
				_lastHeartbeat = millis( );
				_isPausedForFlash = false;
				if (g_core1FlashSafeDepth > 0) g_core1FlashSafeDepth--;
				LogManager::instance( ).setCorePaused(1, false);
			}
		}
	}
}


/* Fold the finished pause into the max/owner accounting. MUST be called
 * before _pauseStartTime is zeroed, from every path that ends a pause. */
void DisplayManager::accountPauseEnd( ) {
	const uint32_t start = _pauseStartTime;
	if (start != 0) {
		const uint32_t held = millis( ) - start;
		if (held > g_core1PauseMaxMs) {
			g_core1PauseMaxMs = held;
			g_core1PauseMaxMod0 = g_core1PauseLastMod0;
		}
	}
	g_core1PauseStartMs = 0;
}

void DisplayManager::forceUnpause( ) {
	int32_t prev = __atomic_load_n(&_pauseRefCount, __ATOMIC_ACQUIRE);
	if (prev > 0) {
		LOG_CODE(LOG_ERROR, "DSP", DSP_FORCE_UNPAUSE, prev, String(TRL("forceUnpause: refCount=")) + prev);
		__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
		accountPauseEnd( );
		_pauseStartTime = 0;
		{ LogManager::TraceScope _t(0, MOD_C1_ENDLOCK); multicore_lockout_end_blocking( ); }
		_lastHeartbeat = millis( );
		_isPausedForFlash = false;
		g_core1FlashSafeDepth = 0;
		LogManager::instance( ).setCorePaused(1, false);
	}
}

uint32_t DisplayManager::getHeartbeat( ) {
	if (_isPausedForFlash) return millis( );
	return _lastHeartbeat;
}

void DisplayManager::refreshTheme( ) { _themeChanged = true; }




/* History calendar */

/* During web reboot, ensure the loopCore1 dispatcher
 * enters the MODE_DASHBOARD branch (the only branch that calls render(), which
 * detects isBooting=true and draws the boot screen). Without this, if the user
 * is in any settings screen when reboot is triggered,
 * the boot screen is not rendered and elements of the current screen persist. */
void DisplayManager::setBootStatus(String msg, bool showSkip) {
	mutex_enter_blocking(&_stateMutex);
	if (msg.length( ) > 0) {
		for (int i = 0; i < 4; i++) _sharedState.bootLogs[i] = _sharedState.bootLogs[i+1];
		_sharedState.bootLogs[4].key = (int16_t)TR_KEYS_COUNT; /* raw, untranslated */
		safeCopy(_sharedState.bootLogs[4].suffix, msg.c_str( ), sizeof(_sharedState.bootLogs[4].suffix));
	}
	_sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
	_uiMode = MODE_DASHBOARD; _forceFullRedraw = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::replaceBootStatus(String msg, bool showSkip) {
	mutex_enter_blocking(&_stateMutex);
	if (msg.length( ) > 0) {
		_sharedState.bootLogs[4].key = (int16_t)TR_KEYS_COUNT;
		safeCopy(_sharedState.bootLogs[4].suffix, msg.c_str( ), sizeof(_sharedState.bootLogs[4].suffix));
	}
	_sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
	_uiMode = MODE_DASHBOARD; _forceFullRedraw = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::setBootStatusKey(LangKey key, const char* suffix, bool showSkip) {
	mutex_enter_blocking(&_stateMutex);
	for (int i = 0; i < 4; i++) _sharedState.bootLogs[i] = _sharedState.bootLogs[i+1];
	_sharedState.bootLogs[4].key = (int16_t)key;
	if (suffix && suffix[0]) {
		safeCopy(_sharedState.bootLogs[4].suffix, suffix, sizeof(_sharedState.bootLogs[4].suffix));
	} else {
		_sharedState.bootLogs[4].suffix[0] = '\0';
	}
	_sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
	_uiMode = MODE_DASHBOARD; _forceFullRedraw = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::replaceBootStatusKey(LangKey key, const char* suffix, bool showSkip) {
	mutex_enter_blocking(&_stateMutex);
	_sharedState.bootLogs[4].key = (int16_t)key;
	if (suffix && suffix[0]) {
		safeCopy(_sharedState.bootLogs[4].suffix, suffix, sizeof(_sharedState.bootLogs[4].suffix));
	} else {
		_sharedState.bootLogs[4].suffix[0] = '\0';
	}
	_sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
	_uiMode = MODE_DASHBOARD; _forceFullRedraw = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::setApProgress(int pct) {
	mutex_enter_blocking(&_stateMutex);
	_sharedState.apProgressPct = pct; _sharedState.isBooting = true; _isDirty = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::endBoot( ) {
	mutex_enter_blocking(&_stateMutex);
	_sharedState.isBooting = false; _isDirty = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::forceDashboard( ) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true;
	mutex_exit(&_stateMutex);
}

/* Forces graph screen (slot 0). Useful for screenshot
 * automation — bypasses touch to go directly to MODE_GRAPH_VIEW. */
void DisplayManager::forceGraphView( ) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_GRAPH_VIEW; _isDirty = true; _forceFullRedraw = true;
	/* Graph render path requires _repaintGraph.
	 * Without this, mode would change but drawGraphScreen() would never be called. */
	_repaintGraph = true;
	mutex_exit(&_stateMutex);
}

bool DisplayManager::isSkipPressed( ) {
	if (_skipPressed) { _skipPressed = false; return true; }
	return false;
}

#if !SIMUT_DISPLAY_ALPHA
bool DisplayManager::isScreenTouched( ) {
 /* Read PENIRQ directly: LOW = touched, HIGH = idle.
  * Works before Core 1 is running (no SPI/lib needed). */
 if (!gpio_get(TOUCH_IRQ)) return true;
 if (_driver.ts) return _driver.ts->touched( );
 return _rawTouchState;
}

void DisplayManager::beginTouch( ) {
 /* XPT2046 PENIRQ (GPIO 20): pulled LOW on touch, HIGH when idle.
  * Reading this pin directly is sufficient for AP-mode detection
  * during boot — no SPI or library initialization needed. */
 gpio_init(TOUCH_IRQ);
 gpio_set_dir(TOUCH_IRQ, GPIO_IN);
 gpio_pull_up(TOUCH_IRQ);
}

/* Inject simulated touch for automation (screenshot capture).
 * Set flag + coords; Core 1 sees it on the next handleTouch iteration.
 * Auto-clear after 100ms (1-2 frames @ 30 FPS) to simulate a tap. */
void DisplayManager::injectTouch(int16_t x, int16_t y) {
	__atomic_store_n(&_simTouchX, x, __ATOMIC_RELEASE);
	__atomic_store_n(&_simTouchY, y, __ATOMIC_RELEASE);
	__atomic_store_n(&_simTouchSetMs, millis( ), __ATOMIC_RELEASE);
	__atomic_store_n(&_simTouchActive, true, __ATOMIC_RELEASE);
}


void DisplayManager::setWebBusy(bool busy, const char* username) {
	mutex_enter_blocking(&_stateMutex);
	if (busy) {
		if (username) safeCopy(_webBusyUser, username, sizeof(_webBusyUser));
		else safeCopy(_webBusyUser, "web", sizeof(_webBusyUser));
		_webBusy = true;
	} else {
		_webBusy = false;
	}
	mutex_exit(&_stateMutex);
}

void DisplayManager::setSlotData(float t, float h, float p, SensorType type, bool isValid, int slotIdx, String name) {
	mutex_enter_blocking(&_stateMutex);
	_sharedState.slotTemp = t; _sharedState.slotHum = h; _sharedState.slotPres = p; _sharedState.slotValid = isValid; _sharedState.slotType = type; _sharedState.selectedSlotIdx = slotIdx;
	safeCopy(_sharedState.slotName, name.c_str( ), sizeof(_sharedState.slotName)); _isDirty = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::setTopSlotData(float t, float h, float p, SensorType type, bool isValid, int slotIdx, String name) {
	mutex_enter_blocking(&_stateMutex);
	_sharedState.topSlotTemp = t; _sharedState.topSlotHum = h; _sharedState.topSlotPres = p; _sharedState.topSlotValid = isValid; _sharedState.topSlotType = type; _sharedState.topSlotIdx = slotIdx;
	safeCopy(_sharedState.topSlotName, name.c_str( ), sizeof(_sharedState.topSlotName)); _isDirty = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::setSlotMinMax(float minT, float maxT, float minH, float maxH) {
	_bottomPanel.minTemp = minT;
	_bottomPanel.maxTemp = maxT;
	_bottomPanel.minHum = minH;
	_bottomPanel.maxHum = maxH;
}

void DisplayManager::setBottomSlotData(float t, float h, SensorType type, bool isValid, int slotIdx, String name) {
	mutex_enter_blocking(&_stateMutex);
	mutex_exit(&_stateMutex);
}

void DisplayManager::setTopSlotMinMax(float minT, float maxT, float minH, float maxH) {
	_topPanel.minTemp = minT;
	_topPanel.maxTemp = maxT;
	_topPanel.minHum = minH;
	_topPanel.maxHum = maxH;
}

#if !SIMUT_DISPLAY_ALPHA
void DisplayManager::setSystemStatus(int rssi, bool bt, String timeStr) {
	mutex_enter_blocking(&_stateMutex);
	_sharedState.wifiRssi = rssi; _sharedState.btActive = bt;
	safeCopy(_sharedState.timeString, timeStr.c_str( ), sizeof(_sharedState.timeString)); _isDirty = true;
	mutex_exit(&_stateMutex);
}
#endif




bool DisplayManager::getUiEvent(UiEvent& ev) {
	/* Core-0-only consumer of the SPSC ring. */
	uint32_t h = _evHead; /* single consumer: plain read of own index */
	uint32_t t = __atomic_load_n(&_evTail, __ATOMIC_ACQUIRE);
	if (h == t) return false;
	ev = _evRing[h % UI_EV_RING];
	__atomic_store_n(&_evHead, h + 1, __ATOMIC_RELEASE);
	return true;
}
void DisplayManager::core1Entry( ) { if (_instance) _instance->loopCore1( ); }

/* HARD-RESET approach (replaces the cooperative one
 * whose handshake failed with Core 1 in unexpected states).
 *
 * Flow: Core 0 does `multicore_reset_core1()` -> Core 1 stops immediately
 * (instruction halted, SPI idle, IRQs dead). Core 0 does all flash
 * ops without conflict because Core 1 is LITERALLY off. At the end,
 * Core 0 restarts via `multicore_launch_core1` -> Core 1 runs core1Entry
 * fresh, reinitializes TFT/canvases and redraws the entire screen.
 *
 * Advantage: deterministic, no timeout, no handshake, no dependency on
 * Core 1's state. Exact match with the user description: "core 1 freezes,
 * core 0 does work, releases and restarts the entire screen".
 *
 * Trade-off: Core 1 takes ~500ms-2s to reinitialize TFT and draw the
 * first post-resume frame. Acceptable — during reset, TFT retains the
 * last frame (ILI9341 controller memory), so the user sees the last
 * state (normally "Applying configuration...") until the new render.
 *
 * _runQuietLoop is no longer used (kept as stub). */
void __not_in_flash_func(DisplayManager::_runQuietLoop)( ) {
	/* No longer called — hard reset replaces this. */
}

/* Core 0 API: HARD-RESET of Core 1. RE-ENTRANT via refcount. Only the
 * first caller (refcount 0->1) performs the actual reset; nested calls
 * increment and return true immediately. */
bool DisplayManager::requestQuietMode(uint32_t /*timeoutMs*/) {
	int32_t prev = __atomic_fetch_add(&_quietModeRefCount, 1, __ATOMIC_ACQ_REL);
	if (prev > 0) {
		/* Already in quiet mode — external caller holds. */
		return true;
	}
	/* T1.1 QUIESCE (stability wave 1): ask Core 1 to park at the top of
	 * its loop — a point guaranteed to be outside malloc/free, outside
	 * the event-queue spinlock and outside any SPI burst — before the
	 * hard reset. A reset landing inside malloc leaves the allocator
	 * mutex held forever (Core 0 hangs on its next allocation → WDT);
	 * inside queue_try_add it leaks a spinlock (both cores hang).
	 * Timeout 200 ms: the fallback is exactly the previous behavior
	 * (reset wherever Core 1 is), so this can never be worse. */
	if (__atomic_load_n(&_core1Ready, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&_quiescePlease, true, __ATOMIC_RELEASE);
		uint32_t t0 = millis( );
		while (!__atomic_load_n(&_core1Parked, __ATOMIC_ACQUIRE) &&
		       !timeSince(t0, 200)) {
			tight_loop_contents( );
		}
		if (!__atomic_load_n(&_core1Parked, __ATOMIC_ACQUIRE)) {
			LOG_CODE(LOG_WARN, "DSP", DSP_FORCE_UNPAUSE, 1,
			         TRL("Quiesce timeout — hard reset fallback"));
		}
	}
	/* First level: hard-reset Core 1. Stops immediately; flash work safe. */
	g_core1KillsQuiet++;
	markCore1Down( );
	{ LogManager::TraceScope _t(0, MOD_C1_RESET); multicore_reset_core1( ); }
	delay(50); /* Short pause for SSI/SPI to stabilize. */
	/* Reinit _stateMutex AT KILL TIME, not only in releaseQuietMode:
	 * if the quiesce timed out, Core 1 may have died holding it (render
	 * copies state under mutex_try_enter). Any Core-0 setter called
	 * inside the quiet window — e.g. loadAndCalibrateSensors →
	 * setSlotData → mutex_enter_blocking — would block forever with the
	 * WDT unfed (save-storm autopsy: C0=[CLI] C1=[DISPLAY]). */
	mutex_init(&_stateMutex);
	/* Core 1 is now dead. Set flags for consumers:
	 * - _core1Ready = false: pauseRendering becomes no-op (no IRQ lockout).
	 * - _quietModeActive = true: isInQuietMode() returns true. */
	__atomic_store_n(&_core1Ready, false, __ATOMIC_RELEASE);
	__atomic_store_n(&_quietModeActive, true, __ATOMIC_RELEASE);
	__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&_quiescePlease, false, __ATOMIC_RELEASE);
	__atomic_store_n(&_core1Parked, false, __ATOMIC_RELEASE);
	_isPausedForFlash = false;
	_quietSince = millis( ); /* T1.5 leak watchdog anchor. */
	LogManager::instance( ).setCorePaused(1, true);
	return true;
}

/* Core 0 API: re-launch Core 1 after flash work. Only the last
 * release (refcount -> 0) performs the actual launch. */
void DisplayManager::releaseQuietMode( ) {
	int32_t prev = __atomic_fetch_sub(&_quietModeRefCount, 1, __ATOMIC_ACQ_REL);
	if (prev > 1) {
		/* Another external caller still holds — do not re-launch. */
		return;
	}
	if (prev <= 0) {
		__atomic_store_n(&_quietModeRefCount, 0, __ATOMIC_RELEASE);
		return;
	}
	/* Reinitialize mutex (Core 1 may have been reset while holding it) and
	 * zero pause flags. New Core 1 will redraw everything in core1Entry. */
	mutex_init(&_stateMutex);
	__atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&_quiescePlease, false, __ATOMIC_RELEASE);
	__atomic_store_n(&_core1Parked, false, __ATOMIC_RELEASE);
	_isPausedForFlash = false;
	_quietSince = 0; /* T1.5: leak watchdog disarmed. */
	_lastHeartbeat = millis( );
	__atomic_store_n(&_quietModeActive, false, __ATOMIC_RELEASE);
	LogManager::instance( ).setCorePaused(1, false);
	launchCore1IfAbsent( );
	/* Core 1 will set _core1Ready=true after victim_init in loopCore1. */
}

#if !SIMUT_DISPLAY_ALPHA

/* ── Private timed wait for Core 1 ─────────────────────────────────────────
 *
 * See FlashIrqProbe.h for the three couplings in delay( ) this removes and how
 * each was verified. In short: that path is entirely flash-resident, waits behind
 * a spinlock shared with Core 0 (which the SDK documents as disabling
 * interrupts), and is woken by an alarm whose IRQ is enabled on CORE 0. Core 1
 * was measured frozen in it for 14.3 s.
 *
 * This replacement owns one hardware alarm, arms it with a single MMIO write and
 * sleeps in __wfe. No lock is needed: an enabled interrupt taken on this core
 * wakes WFE, and the ARM event register makes "check then sleep" race-free — if
 * the alarm fires between the check and the __wfe, the pending IRQ makes the WFE
 * return immediately rather than sleep through it.
 *
 * SRAM-resident so it stays executable while a flash program/erase has XIP down.
 * __no_inline_not_in_flash_func and not __not_in_flash_func: the section attribute
 * only places an OUT-OF-LINE copy, and GCC inlined both of these into loopCore1 —
 * which lives in flash — silently voiding the one property under test. The build
 * is checked with nm afterwards rather than trusted.
 *
 * The claim happens from Core 1 precisely so irq_set_enabled lands on Core 1's
 * NVIC; doing it from Core 0 would rebuild the dependency being removed. */
static uint8_t s_c1AlarmNum = 0xFF;

static void __no_inline_not_in_flash_func(core1AlarmIsr)( ) {
	/* Ack only. The wake is the interrupt itself, not anything this writes. */
	timer_hw->intr = 1u << s_c1AlarmNum;
}

static void core1WaitInit( ) {
	/* The static guard may skip only the CLAIM. A core reset clears this
	 * core's NVIC and its vector table is rebuilt on launch, so a relaunched
	 * Core 1 came back with its own wake IRQ disabled: every core1WaitUs
	 * slept in __wfe with an alarm that could never be taken, advancing only
	 * on stray SEVs from Core 0's FIFO traffic. Flash pauses supply those
	 * SEVs constantly, which is what kept the display limping invisibly —
	 * until a long pure-read stream (a 1 MB history download) went lockout-
	 * free for 19 s, the heartbeat froze with phase=W_WFE, and the health
	 * check declared APP_CORE1_DEAD: the download-storm 502. Relaunches are
	 * routine (every quiet-mode save is a kill+relaunch), so this bit every
	 * boot's first save. */
	if (s_c1AlarmNum == 0xFF) {
		const int n = hardware_alarm_claim_unused(false);
		if (n < 0) return;                         /* none free: stay on delay( ) */
		s_c1AlarmNum = (uint8_t)n;
		g_core1WaitAlarm = s_c1AlarmNum;
	}
	const uint irqn = (uint)(TIMER_IRQ_0 + s_c1AlarmNum);
	if (irq_get_exclusive_handler(irqn) != core1AlarmIsr) {
		irq_set_exclusive_handler(irqn, core1AlarmIsr);
	}
	irq_set_enabled(irqn, true);
	hw_set_bits(&timer_hw->inte, 1u << s_c1AlarmNum);
}

static void __no_inline_not_in_flash_func(core1WaitUs)(uint32_t us) {
	const uint32_t t0     = timer_hw->timerawl;
	const uint32_t target = t0 + us;
	timer_hw->alarm[s_c1AlarmNum] = target;
	C1_PHASE(C1P_W_WFE);
	uint32_t wakes = 0;
	/* Signed compare: wrap-safe, and false immediately if the target already
	 * passed while we were arming, in which case we never sleep at all. */
	while ((int32_t)(timer_hw->timerawl - target) < 0) {
		__wfe( );
		wakes++;
	}
	/* Disarm: if the target had already passed, the comparator never matched and
	 * the alarm would otherwise still be armed ~71 min from now. */
	timer_hw->armed = 1u << s_c1AlarmNum;
	const uint32_t held = timer_hw->timerawl - t0;
	if (held > g_core1WaitMaxUs) g_core1WaitMaxUs = held;
	/* More than one wake means some other interrupt on this core returned from
	 * __wfe before our alarm did; the loop simply re-checked and slept again. It is
	 * expected and harmless — lateness would show up in g_core1WaitMaxUs, not here. */
	if (wakes > 1) g_core1WaitExtraWakes++;
}

void DisplayManager::loopCore1( ) {

	multicore_lockout_victim_init( );
	_core1Ready = true;
	g_core1Running = 1;   /* live from here: XIP fetches can now collide with flash */
	C1_PHASE(C1P_INIT);
	core1WaitInit( );     /* from Core 1, so the alarm IRQ is Core 1's */

	/* Heap allocations preserved across resets.
	 * Touch MUST be reinitialized on every launch — attachInterrupt connects
	 * handler on Core 1's NVIC, which is zeroed on multicore_reset. Without
	 * reinit, touch stops responding after the first save.
	 * TFT begin() does HW reset of ILI9341 (visible white flash); skipped
	 * on subsequent launches. */
	if (!_driver.tft) _driver.tft = new TftWithOffset(TFT_CS, TFT_DC, TFT_RST);
	if (!_driver.ts) _driver.ts = new XPT2046_Touchscreen(TOUCH_CS, TOUCH_IRQ);
	if (!_driver.canvas) _driver.canvas = new GFXcanvas16(320, 45);
	if (!_driver.canvasSmall) _driver.canvasSmall = new GFXcanvas16(140, 40);

	/* Touch: reattach IRQ every launch (Core 1's NVIC was zeroed). */
	_driver.ts->begin( );
	_driver.ts->setRotation(3);

	if (_driver.firstInit) {
		/* Explicit clock. begin( ) with no argument took Adafruit_ILI9341's
		 * SPI_DEFAULT_FREQ, and RP2040 matches none of that header's platform arms
		 * so it landed on the generic 24 MHz — which the hardware cannot produce.
		 * The PL022 divider only yields clk_peri / (prescale * postdiv), and with
		 * clk_peri at 125 MHz the reachable ladder is 62.5 / 31.25 / 20.83 / 15.6.
		 * A 24 MHz request therefore ran at 20.83 MHz, one rung below a free 1.5x.
		 *
		 * 31.25 MHz is the conservative rung: it is above Adafruit's own default
		 * for every other platform and well inside what ILI9341 modules take, but
		 * this is a breadboard with jumper wires, so it is a named constant and a
		 * one-line revert if the panel shows artefacts. 62.5 MHz is the PL022
		 * ceiling and would halve the wire time again — do not raise it without
		 * looking at the screen. */
		constexpr uint32_t TFT_SPI_HZ = 31250000u;
		_driver.tft->begin(TFT_SPI_HZ);
		_driver.tft->setRotation(3);
		_driver.tft->fillScreen(C_BG_MAIN);
		if (!_sharedState.isBooting) drawInterfaceFixed( );
		_lastRenderedState.selectedSlotIdx = -1;
		_driver.firstInit = false;
	} else {
		/* Post-reset resume: TFT retains last frame (ILI9341 memory).
		 * Force delta render on next iteration to update data. */
		C1_PHASE(C1P_RESUME_MUTEX);
		mutex_enter_blocking(&_stateMutex);
		_isDirty = true;
		mutex_exit(&_stateMutex);
	}

	SystemState currentSnapshot;

	while (true) {
		/* Cooperative approach removed (was not reliable — Core 1 would become
		 * unresponsive for >15s in flash GC + heavy render scenarios).
		 * Now Core 0 uses `multicore_reset_core1` to HARD-RESET Core 1
		 * before flash writes. This loop only runs if Core 1 is active. */

		TRACE_MOD(1, MOD_DISPLAY);
		TRACE_BEAT(1);
		C1_PHASE(C1P_LOOP_TOP);

		/* T1.1 SAFE PARK (stability wave 1): honored at the loop top —
		 * guaranteed outside malloc/free, the event-queue spinlock and
		 * any SPI transaction. Core 0 will hard-reset us while we spin
		 * here; the heartbeat keeps the Core-1 health watchdog quiet
		 * during the (sub-200 ms) wait. */
		if (__atomic_load_n(&_quiescePlease, __ATOMIC_ACQUIRE)) {
			C1_PHASE(C1P_PARK);
			__atomic_store_n(&_core1Parked, true, __ATOMIC_RELEASE);
			while (__atomic_load_n(&_quiescePlease, __ATOMIC_ACQUIRE)) {
				_lastHeartbeat = millis( );
			}
			__atomic_store_n(&_core1Parked, false, __ATOMIC_RELEASE);
		}

		_lastHeartbeat = millis( );
		/* Liveness for `show metrics`: age of this stamp is the only outside
		 * evidence that Core 1 is still completing loop iterations. Stamped
		 * HERE only — Core 0 also writes _lastHeartbeat on pause/release, and
		 * mirroring those would fake the very signal we need. */
		/* Fluidity: iteration count (delta = UI frame rate) and the worst single
		 * iteration (the perceived stutter). Measured from the previous stamp, so a
		 * lockout freeze lands in the iteration that was interrupted. */
		{
			const uint32_t nowMs = millis( );
			const uint32_t prevMs = g_core1HeartbeatMs;
			if (prevMs != 0) {
				const uint32_t iter = nowMs - prevMs;
				if (iter > g_core1IterMaxMs) g_core1IterMaxMs = iter;
			}
			g_core1HeartbeatMs = nowMs;
		}
		g_core1Iters++;
		g_core1UiMode = (uint8_t)_uiMode;
		/* One QSPI latency probe per iteration, on the same core whose fetches the
		 * starvation hypothesis is about. ~30 us against a 15 ms iteration, so it
		 * cannot itself be what makes the iteration slow. */
		core1XipProbe( );
		/* OR with simulated touch active flag.
		 * handleTouch and mapTouchPoint check _simTouchActive to use
		 * synthesized screen-space coords. Allows CLI 'touch sim X Y'
		 * for automation (screenshot capture). */
		C1_PHASE(C1P_TOUCH_READ);
		_rawTouchState = _driver.ts->touched( ) ||
		                 __atomic_load_n(&_simTouchActive, __ATOMIC_ACQUIRE);

		/* Process touch BEFORE rendering for same-frame response */
		C1_PHASE(C1P_TOUCH_HANDLE);
		handleTouch( );

		if (_themeChanged) {
			C1_PHASE(C1P_THEME_MUTEX);
			SystemState snap;
			mutex_enter_blocking(&_stateMutex);
			snap = _sharedState;
			mutex_exit(&_stateMutex);

			if (!snap.isBooting) {
				_driver.tft->fillScreen(C_BG_MAIN);
				_driver.tft->setFont(&simutFont12pt);
				_driver.tft->setTextColor(C_TEXT_MAIN);
				int16_t x1, y1; uint16_t w, h;
				/* T1.2: Core-1 render path is heap-free — tr( ) already
				 * returns const char*, the String wrapper was pure waste
				 * (and a reset-inside-malloc hazard). */
				const char* msg = tr(TR_APPLYING_THEME);
				_driver.tft->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
				_driver.tft->setCursor(160 - (w/2), 127);
				_driver.tft->print(msg);
				delay(200);

				mutex_enter_blocking(&_stateMutex);
				snap = _sharedState;
				if (!snap.topSlotValid) {
				snap.topSlotTemp = snap.slotTemp; snap.topSlotHum = snap.slotHum;
				snap.topSlotType = snap.slotType; snap.topSlotValid = snap.slotValid;
				safeCopy(snap.topSlotName, snap.slotName, 31);
				}
				_isDirty = false;
				mutex_exit(&_stateMutex);

				drawInterfaceFixed( );
				drawTopBar(snap);
				drawSlotPanel(snap.topSlotTemp, snap.topSlotHum, snap.topSlotType, snap.topSlotValid, snap.topSlotIdx, snap.topSlotName, true, _topPanel, snap.topSlotPres);
				drawSlotPanel(snap.slotTemp, snap.slotHum, snap.slotType, snap.slotValid, snap.selectedSlotIdx, snap.slotName, true, _bottomPanel, snap.slotPres);
				drawBottomButtons(snap.selectedSlotIdx);
				_lastRenderedState = snap;
				_uiMode = MODE_DASHBOARD;
			} else {
				_driver.tft->fillScreen(C_BG_MAIN);
				_lastRenderedState.isBooting = false;

				mutex_enter_blocking(&_stateMutex);
				_isDirty = true;
				mutex_exit(&_stateMutex);
			}
			_themeChanged = false;
		}

		if (_uiMode == MODE_DASHBOARD) {

			/* Fallback to _lastWebBusy instead of false when
			 * mutex_try_enter fails — avoids overlay flicker. */
			bool webBusyNow = _lastWebBusy;
			if (mutex_try_enter(&_stateMutex, NULL)) {
				webBusyNow = _webBusy;
				_lastWebBusy = webBusyNow;
				mutex_exit(&_stateMutex);
			}


			if (_alarmNavPending >= 0) {
				int8_t navTarget = _alarmNavPending;
				_alarmNavPending = -1;
				if (navTarget < 4) _currentPage = 0;
				else if (navTarget < 8) _currentPage = 1;
				else _currentPage = 2;
				_alarmRotateTimer = millis( );
				C1_PHASE(C1P_DASH_MUTEX);
				mutex_enter_blocking(&_stateMutex);
				_isDirty = true;
				mutex_exit(&_stateMutex);
			}


			if (_alarmSlotMask != 0 && !_alarmSilenced) {
				uint16_t m = _alarmSlotMask;
				int alarmCount = 0;
				while (m) { alarmCount += (m & 1); m >>= 1; }

				if (alarmCount >= 2 && timeSince(_alarmRotateTimer, ALARM_ROTATE_INTERVAL_MS)) {
					_alarmRotateTimer = millis( );
					int current = _lastRenderedState.selectedSlotIdx;
					for (int i = 1; i <= 10; i++) {
						int idx = (current + i) % 10;
						if (_alarmSlotMask & (1 << idx)) {
							if (idx < 4) _currentPage = 0;
							else if (idx < 8) _currentPage = 1;
							else _currentPage = 2;
							UiEvent ev;
							ev.type = UiEvent::EVT_SLOT_SELECT;
							ev.id = idx;
							pushUiEvent(ev);
							break;
						}
					}
				}
			}


			/* During boot screen (state.isBooting=true after
			 * web reboot), redrawAlarmFlash overwrites the boot screen with
			 * alarm cards — visually "alarm flashing over
			 * Rebooting...". Skip while isBooting. */
			if (isAnyAlarmActive( ) && !_lastRenderedState.isBooting) {
				uint32_t now = millis( );
				if (now - _alarmFlashTimer >= ALARM_FLASH_INTERVAL_MS) {
					_alarmFlashTimer = now;
					_alarmFlashPhase = !_alarmFlashPhase;
					C1_PHASE(C1P_ALARM_FLASH);
					redrawAlarmFlash( );
				}
			} else if (_alarmFlashPhase && !_lastRenderedState.isBooting) {

				_alarmFlashPhase = false;
				_alarmFlashTimer = 0;
				_alarmRotateTimer = 0;
				C1_PHASE(C1P_ALARM_FLASH);
				restoreNormalDashboard( );
			}


			/* Render unconditionally. This used to be gated on _webOverlayShown,
			 * which meant that once the user touched during a web transfer the
			 * dashboard STOPPED UPDATING until the transfer finished — the readings
			 * froze underneath a blanked screen. Touch is still rejected while a web
			 * client holds the device (aborting a download would truncate the
			 * caller's chart), but that is now said in a top-bar banner instead of
			 * by hiding everything. See drawTopBar( ). */
			C1_PHASE(C1P_SNAPSHOT);
			if (pullSnapshot(currentSnapshot)) { C1_PHASE(C1P_RENDER); render(currentSnapshot); }
		}
		else if (_uiMode == MODE_GRAPH_LOADING) {
			if (_repaintLoading) { C1_PHASE(C1P_UI_GRAPH); drawLoadingScreen( ); _repaintLoading = false; }
		}
		else if (_uiMode == MODE_STATS_VIEW) {
			if (_repaintGraph) { C1_PHASE(C1P_UI_GRAPH); drawStatsScreen( ); _repaintGraph = false; }
		}
		else if (_uiMode == MODE_GRAPH_VIEW) {
			if (_repaintGraph) { C1_PHASE(C1P_UI_GRAPH); drawGraphScreen( ); _repaintGraph = false; }
		}
		else if (_uiMode == MODE_GRAPH_DETAIL) {
			if (_repaintGraph) { C1_PHASE(C1P_UI_GRAPH); drawGraphDetailScreen( ); _repaintGraph = false; }
		}
		else if (_uiMode == MODE_CALENDAR) {
			if (_repaintCalendar) { C1_PHASE(C1P_UI_SETTINGS); drawCalendarScreen( ); _repaintCalendar = false; }
		}

		C1_PHASE(C1P_LOOP_TAIL);

		/* Revert header to date/time after 3s of showing the name */
		if ((_uiMode == MODE_GRAPH_VIEW || _uiMode == MODE_GRAPH_DETAIL)
		    && _headerShowName
		    && timeSince(_headerNameTimer, 3000))
		{
			_headerShowName = false;
			drawGraphHeaderBar( );
		}
		else if (_uiMode == MODE_SETTINGS_THEMES) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawSettingsThemes( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_SETTINGS_ALARMS) {
			if (_repaintSettings) {
				C1_PHASE(C1P_UI_SETTINGS);
				/* A flag that flipped in place repaints its own word; anything
				 * else goes through the full path. */
				if (_alarmStatusDirty && !_forceSettingsRedraw) drawAlarmStatusOnly( );
				else drawSettingsAlarms( );
				_alarmStatusDirty = false;
				_repaintSettings = false;
			}
		}
		else if (_uiMode == MODE_SETTINGS_ALARM_EDIT) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawAlarmEdit( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_AUTH) {
			if (_permanentLockout) {
				/* Wrap-safe: direct comparison with millis() fails on wrap every ~49.7 days. */
				if (timeReached(_lockoutUntil)) forceDashboard( );
			} else if (_lockoutUntil > 0) {
				if (!timeReached(_lockoutUntil)) _repaintSettings = true;
				else { _lockoutUntil = 0; _forceSettingsRedraw = true; _repaintSettings = true; }
			}
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawAuthScreen( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_SETTINGS_MAIN) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawSettingsMain( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_SETTINGS_LANG) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawSettingsLang( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_SETTINGS_PASSWORD) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawSettingsPassword( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_SETTINGS_TOUCH_CAL) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawTouchCalibration( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_SETTINGS_TOUCH_SENS) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawTouchSensitivity( ); _repaintSettings = false; }
			/* After 1.5s from completion, advance to position calibration */
			if (_sensDone && timeSince(_sensDoneTime, 1500)) {
				_uiMode = MODE_SETTINGS_TOUCH_CAL;
				_calStep = 0;
				_calPhase = 0;
				_forceSettingsRedraw = true;
				_repaintSettings = true;
			}
		}
		else if (_uiMode == MODE_SETTINGS_SOUNDS) {

			if (_repaintSettings) {
				if (_inMelodySelect) drawMelodySelect( );
				else drawSettingsSounds( );
				_repaintSettings = false;
			}
		}
		else if (_uiMode == MODE_SETTINGS_STATUS) {
			/* Renders every 1 second or when forced */
			if (_repaintSettings || timeSince(_statusLastDraw, 1000)) {
				drawSystemStatus( );
				_repaintSettings = false;
			}
		}
		else if (_uiMode == MODE_SETTINGS_LICENSE) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawSettingsLicense( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_SETTINGS_DISPLAY_OFFSET) {
			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawSettingsDisplayOffset( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_ALARM_ACTION) {

			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawAlarmAction( ); _repaintSettings = false; }
		}
		else if (_uiMode == MODE_CONFIRM_MUTE_ALL) {

			if (_repaintSettings) { C1_PHASE(C1P_UI_SETTINGS); drawMuteConfirm( ); _repaintSettings = false; }
		}

		/*
		 * Adaptive pause: minimum during interaction, larger when idle.
		 * - Active touch or pending repaint: 1ms (maximum responsiveness)
		 * - Idle: 2ms
		 *
		 * Core 1 was measured frozen HERE for up to 14.3 s during a history
		 * download (phase stamp caught it live at 5.0 -> 7.7 -> 13.6 s, per-phase
		 * table confirmed LOOP_DELAY=14286 ms against a 14336 ms worst iteration),
		 * so this call is where the R1 freeze happens. delay( ) resolves to
		 * sleep_ms -> sleep_until, which arms an alarm on the DEFAULT alarm pool
		 * (IRQ serviced on Core 0) and waits in a spin_lock_blocking / __wfe loop
		 * on a notifier shared with Core 0 — which also hammers it from
		 * streamBreath( ), one delay(2) per 512-byte packet, during exactly these
		 * downloads. spin_lock_blocking disables interrupts on the calling core,
		 * which would explain `parked=0` and the unanswerable lockout handshake.
		 *
		 * TREATMENT TRIED AND REVERTED (rc21): replacing this with
		 * busy_wait_us_32 — no lock, no alarm, no other core, interrupts left
		 * enabled. It did not fix the freeze and made the device worse:
		 * lockout-not-granted went from 1 to 24 events in a comparable storm, and
		 * the reboots turned into Core-0 stalls in WEB_HSCAN. A hot timer spin on
		 * Core 1 is the likely reason (it starves nothing in theory, but the
		 * device says otherwise). So the LOCATION is measured and certain; the
		 * MECHANISM is not, and the next attempt should establish it before
		 * swapping the primitive again.
		 */
		bool touchActive = _rawTouchState;
		bool repaintPending = _isDirty || _repaintGraph || _repaintSettings || _repaintLoading;
		/* Last markers of the iteration. Without them a freeze here reads as the
		 * previous phase — which for the dashboard is C1P_RENDER, and that is
		 * exactly the reading two sessions of this investigation worked from.
		 *
		 * Which primitive ran is never assumed: the private wait stamps W_WFE, the
		 * fallback stamps LOOP_DELAY, so the phase itself says which one a freeze
		 * happened in. */
		const uint32_t waitUs = (touchActive || repaintPending ? 1u : 2u) * 1000u;
		if (s_c1AlarmNum != 0xFF) {
			core1WaitUs(waitUs);
		} else {
			C1_PHASE(C1P_LOOP_DELAY);
			delay(waitUs / 1000u);
		}
	}
}

#endif // !SIMUT_DISPLAY_ALPHA

bool DisplayManager::pullSnapshot(SystemState& localSnapshot) {
	bool updated = false;


	if (mutex_enter_timeout_us(&_stateMutex, 1000)) {
		/* Keep topSlotIdx in sync with current panel mode */
		if (!_topPanel.fixed)
			_sharedState.topSlotIdx = _sharedState.selectedSlotIdx;
		else if (_topPanel.fixedIdx >= 0)
			_sharedState.topSlotIdx = _topPanel.fixedIdx;
		if (_isDirty) {
			localSnapshot = _sharedState;
			_isDirty = false;
			updated = true;
		}
		mutex_exit(&_stateMutex);
	}
	return updated;
}

#endif // !SIMUT_DISPLAY_ALPHA

#if !SIMUT_DISPLAY_ALPHA
void DisplayManager::render(const SystemState& state) {
 SystemState st = state;
 if (!st.topSlotValid) {
 st.topSlotTemp = st.slotTemp; st.topSlotHum = st.slotHum;
 st.topSlotType = st.slotType; st.topSlotValid = st.slotValid;
 safeCopy(st.topSlotName, st.slotName, 31);
 st.topSlotIdx = st.selectedSlotIdx;
 }
	if (state.isBooting) {
		C1_PHASE(C1P_R_BOOT);
		/* _langChanged forces fullRedraw to retranslate bootLogs already
		 * shown in EN before .lng loaded. */
		bool langJustChanged = _langChanged;
		if (langJustChanged) _langChanged = false;
		bool fullRedraw = (_lastRenderedState.isBooting == false) ||
		                  (_lastRenderedState.apProgressPct != state.apProgressPct) ||
		                  langJustChanged;
		if (state.apProgressPct >= 0) {
			if (fullRedraw) _driver.tft->fillScreen(C_BG_MAIN);
			_driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
			_driver.tft->setCursor(55, 120); _driver.tft->print(tr(TR_AP_MODE));
			_driver.tft->drawRoundRect(40, 140, 240, 20, 6, C_TEXT_SUB);
			int wBar = map(state.apProgressPct, 0, 100, 0, 236);
			if (wBar > 0) {
				_driver.tft->fillRoundRect(42, 142, wBar, 16, 4, C_ACCENT);
			}
			_lastRenderedState = state;
			return;
		}

		int boxY = 105;

		if (fullRedraw) {
			_driver.tft->fillScreen(C_BG_MAIN);
			_driver.tft->setFont(&simutFont24pt); _driver.tft->setTextColor(C_TEXT_MAIN);
			int16_t x1, y1; uint16_t w, h;
			_driver.tft->getTextBounds("SIMUT", 0, 0, &x1, &y1, &w, &h);
			_driver.tft->setCursor((320 - w) / 2, 60); _driver.tft->print("SIMUT");
			_driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_ACCENT);
			_driver.tft->getTextBounds(SIMUT_VERSION, 0, 0, &x1, &y1, &w, &h);
			_driver.tft->setCursor((320 - w) / 2, 85); _driver.tft->print(SIMUT_VERSION);

			_driver.tft->fillRoundRect(10, boxY, 300, 80, 8, C_CARD_BG);
			_driver.tft->drawRoundRect(10, boxY, 300, 80, 8, C_TEXT_OFF);
			_driver.tft->setFont(NULL);
			_driver.tft->setTextSize(1);
			_driver.tft->setTextColor(C_ACCENT_HIGH, C_CARD_BG);
			_driver.tft->setCursor(20, boxY + 8);
			_driver.tft->print("> system_init( ) ");
		}
		/* Per-line diff: only repaints lines that changed. Boot logs are
		 * BootLogEntry (key + suffix). In render, we resolve tr(key) +
		 * suffix. Comparison includes key, suffix AND active language
		 * (langJustChanged -> fullRedraw). */
		_driver.tft->setFont(NULL);
		_driver.tft->setTextSize(1);
		_driver.tft->setTextColor(C_TEXT_SUB, C_CARD_BG);
		for(int i=0; i<5; i++) {
			const BootLogEntry& cur = state.bootLogs[i];
			const BootLogEntry& prev = _lastRenderedState.bootLogs[i];
			if (!fullRedraw && cur.key == prev.key &&
			    strncmp(cur.suffix, prev.suffix, sizeof(cur.suffix)) == 0) continue;
			_driver.tft->setCursor(20, boxY + 22 + (i*10));
			/* T1.2: fixed buffer on the Core-1 render path (was 2-3 heap
			 * allocations per changed line). Pad to 46 columns preserved
			 * so shorter lines still overwrite older, longer ones. */
			char logLine[48];
			if (cur.key >= 0 && cur.key < (int16_t)TR_KEYS_COUNT) {
				snprintf(logLine, sizeof(logLine), "%s%s",
				         tr((LangKey)cur.key), cur.suffix);
			} else {
				snprintf(logLine, sizeof(logLine), "%s", cur.suffix); /* raw legacy */
			}
			size_t llen = strlen(logLine);
			while (llen < 46 && llen < sizeof(logLine) - 1) logLine[llen++] = ' ';
			logLine[llen] = '\0';
			_driver.tft->print(logLine);
		}

		/* Skip button: paint only on off->on transition. Idempotent otherwise. */
		bool skipOn = state.showSkipButton;
		bool wasOn = _lastRenderedState.showSkipButton;
		if (skipOn && (fullRedraw || !wasOn)) {
			_driver.tft->fillRoundRect(80, 195, 160, 35, 8, C_ACCENT_HIGH);
			_driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_BG_MAIN);
			int16_t x1, y1; uint16_t w, h;
			const char* skipLabel = tr(TR_SKIP);
			_driver.tft->getTextBounds(skipLabel, 0, 0, &x1, &y1, &w, &h);
			_driver.tft->setCursor(80 + (160 - w)/2, 218); _driver.tft->print(skipLabel);
		} else if (!skipOn && (fullRedraw || wasOn)) {
			_driver.tft->fillRect(80, 195, 160, 35, C_BG_MAIN);
		}

		_lastRenderedState = state;
		return;
	}
	if (_lastRenderedState.isBooting && !state.isBooting) {


		_forceFullRedraw = true;
	}

	bool full = _forceFullRedraw;
	if (full) {
		C1_PHASE(C1P_R_FULL);
		drawInterfaceFixed( );
		drawTopBar(state);


		drawSlotPanel(st.topSlotTemp, st.topSlotHum, st.topSlotType, st.topSlotValid, st.topSlotIdx, st.topSlotName, true, _topPanel, st.topSlotPres);
		drawSlotPanel(st.slotTemp, st.slotHum, st.slotType, st.slotValid, st.selectedSlotIdx, st.slotName, true, _bottomPanel, st.slotPres);
		drawBottomButtons(state.selectedSlotIdx);
		_forceFullRedraw = false;
		_lastRenderedState = state;
		return;
	}

	/* Barrier before reading _pktArrowState (published by Core 0
	 * along with flash vars in setTelemetrySendStatus). */
	__dmb( );
	if (state.wifiRssi != _lastRenderedState.wifiRssi ||
	    state.btActive != _lastRenderedState.btActive ||
	    strcmp(state.timeString, _lastRenderedState.timeString) != 0 ||
	    _webNotifyStartMs > 0 ||
	    _alarmSilenced ||
	    _pktArrowState == 3) {
		C1_PHASE(C1P_R_TOPBAR);
		drawTopBar(state);
	}

	/* Sync topSlot* from slot* whenever both panels show the same sensor */
	if (st.topSlotIdx == st.selectedSlotIdx) {
	 st.topSlotTemp = st.slotTemp; st.topSlotHum = st.slotHum;
	 st.topSlotType = st.slotType; st.topSlotValid = st.slotValid;
	 safeCopy(st.topSlotName, st.slotName, 31);
	}

	if (!_topPanel.showMinMax) {
		if (abs(st.topSlotTemp - _lastRenderedState.topSlotTemp) > 0.01 ||
		    abs(st.topSlotHum - _lastRenderedState.topSlotHum) > 0.01 ||
		    st.topSlotValid != _lastRenderedState.topSlotValid ||
		    st.topSlotIdx != _lastRenderedState.topSlotIdx) {
			C1_PHASE(C1P_R_TOP_PANEL);
			drawSlotPanel(st.topSlotTemp, st.topSlotHum, st.topSlotType, st.topSlotValid, st.topSlotIdx, st.topSlotName, true, _topPanel, st.topSlotPres);
		}
	}

	/* Return panels to normal mode after 30s without touch */
	if ((_topPanel.showMinMax || _bottomPanel.showMinMax) &&
	    timeSince(_lastTouchTime, 30000)) {
		C1_PHASE(C1P_R_MINMAX);
		if (_topPanel.showMinMax) {
			_topPanel.showMinMax = false;
			drawSlotPanel(st.topSlotTemp, st.topSlotHum, st.topSlotType, st.topSlotValid, st.topSlotIdx, st.topSlotName, true, _topPanel, st.topSlotPres);
		}
		if (_bottomPanel.showMinMax) {
			_bottomPanel.showMinMax = false;
			drawSlotPanel(st.slotTemp, st.slotHum, st.slotType, st.slotValid,
			              st.selectedSlotIdx, st.slotName, true, _bottomPanel, st.slotPres);
		}
	}

	bool slotChanged = (state.selectedSlotIdx != _lastRenderedState.selectedSlotIdx);
	bool bNameChanged = (strcmp(st.slotName, _lastRenderedState.slotName) != 0);
	bool bTempChanged = (abs(st.slotTemp - _lastRenderedState.slotTemp) > 0.01) || (st.slotValid != _lastRenderedState.slotValid);

	if (slotChanged || bNameChanged || (!_bottomPanel.showMinMax && bTempChanged)) {
		C1_PHASE(C1P_R_BOT_PANEL);
		if (slotChanged) {
			drawBottomButtons(state.selectedSlotIdx);
		}

		drawSlotPanel(st.slotTemp, st.slotHum, st.slotType, st.slotValid, st.selectedSlotIdx, st.slotName, (slotChanged || bNameChanged), _bottomPanel, st.slotPres);
	}

	/* Detect alarm state change and redraw buttons + panels */
	if (_alarmSlotMask != _prevAlarmSlotMask) {
		C1_PHASE(C1P_R_ALARM);
		drawBottomButtons(state.selectedSlotIdx);
		if (!_topPanel.showMinMax) {
			drawSlotPanel(st.topSlotTemp, st.topSlotHum, st.topSlotType, st.topSlotValid, st.topSlotIdx, st.topSlotName, true, _topPanel, st.topSlotPres);
		}
		if (!_bottomPanel.showMinMax) {
			drawSlotPanel(st.slotTemp, st.slotHum, st.slotType, st.slotValid,
			              st.selectedSlotIdx, st.slotName, true, _bottomPanel, st.slotPres);
		}
		_prevAlarmSlotMask = _alarmSlotMask;
	}

	_lastRenderedState = state;
}







void DisplayManager::setWebNotification(const char* username) {
	if (!username) return;
	safeCopy(_webNotifyUser, username, sizeof(_webNotifyUser));
	_webNotifyUser[sizeof(_webNotifyUser) - 1] = '\0';
	_webNotifyStartMs = millis( );
	if (_webNotifyStartMs == 0) _webNotifyStartMs = 1;
}


/* Real-time system status */


#if !SIMUT_DISPLAY_ALPHA
void DisplayManager::showSettingsLicense( ) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_SETTINGS_LICENSE;
	_licensePage = 0;
	_forceSettingsRedraw = true;
	_repaintSettings = true;
	mutex_exit(&_stateMutex);
}


void DisplayManager::drawSettingsLicense( ) {
	bool fullRedraw = _forceSettingsRedraw;

	/* licText comes from _licenseBuf (loaded in setLanguage
	 * by Core 0). Fallback is generated in _licenseBuf itself if FS missing. */
	const char* licText = _licenseBuf;

	const int MAX_COLS = 50;
	const int LINE_H = 9;
	const int TEXT_Y0 = 36;
	const int MAX_VIS = 17;

	/* Count total lines (license + acknowledgments already integrated) */
	int totalLines = wrapLineCount(licText, MAX_COLS);

	/* Calculate total pages */
	_licenseTotalPages = (totalLines + MAX_VIS - 1) / MAX_VIS;
	if (_licenseTotalPages < 1) _licenseTotalPages = 1;
	if (_licensePage >= _licenseTotalPages) _licensePage = _licenseTotalPages - 1;
	if (_licensePage < 0) _licensePage = 0;

	if (fullRedraw) {
		_driver.tft->fillScreen(C_BG_MAIN);

		/* Header with title and page counter */
		_driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
		_driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
		_driver.tft->setCursor(10, 22); _driver.tft->print(tr(TR_LICENSE_TITLE));

		char pgBuf[8];
		snprintf(pgBuf, sizeof(pgBuf), "%d/%d", _licensePage + 1, _licenseTotalPages);
		int16_t px, py; uint16_t pw, ph;
		_driver.tft->getTextBounds(pgBuf, 0, 0, &px, &py, &pw, &ph);
		_driver.tft->setTextColor(C_TEXT_SUB);
		_driver.tft->setCursor(310 - (int)pw, 22); _driver.tft->print(pgBuf);

		/* Bottom buttons */
		int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

		_driver.tft->fillRoundRect(5, btnY, 100, btnH, 8, C_CARD_BG);
		_driver.tft->fillTriangle(55, btnY + 12, 45, btnY + 26, 65, btnY + 26, C_TEXT_MAIN);

		_driver.tft->fillRoundRect(110, btnY, 100, btnH, 8, C_CARD_BG);
		_driver.tft->fillTriangle(160, btnY + 26, 150, btnY + 12, 170, btnY + 12, C_TEXT_MAIN);

		_driver.tft->fillRoundRect(215, btnY, 100, btnH, 8, C_ACCENT);
		_driver.tft->setTextColor(C_BG_MAIN);
		const char* backTxt = tr(TR_BACK); /* T1.2: heap-free render path. */
		_driver.tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
		_driver.tft->setCursor(215 + (100 - bw) / 2, btnY + 25); _driver.tft->print(backTxt);
	}

	/* Clear text area */
	_driver.tft->fillRect(0, TEXT_Y0, 320, MAX_VIS * LINE_H, C_BG_MAIN);
	_driver.tft->setFont(NULL); _driver.tft->setTextSize(1);
	_driver.tft->setTextColor(C_TEXT_SUB);

	/* Render current page */
	int startLine = _licensePage * MAX_VIS;
	renderWrapped(_driver.tft, licText, 10, TEXT_Y0, MAX_COLS, LINE_H,
	              startLine, MAX_VIS);

	/* "N/M" counter in the top right corner already indicates current page. */

	/* Update counter in header (without redrawing everything) */
	if (!fullRedraw) {
		_driver.tft->fillRect(240, 6, 75, 22, C_CARD_BG);
		_driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_SUB);
		char pgBuf[8];
		snprintf(pgBuf, sizeof(pgBuf), "%d/%d", _licensePage + 1, _licenseTotalPages);
		int16_t px, py; uint16_t pw, ph;
		_driver.tft->getTextBounds(pgBuf, 0, 0, &px, &py, &pw, &ph);
		_driver.tft->setCursor(310 - (int)pw, 22); _driver.tft->print(pgBuf);
	}

	_forceSettingsRedraw = false;
}
#endif // !SIMUT_DISPLAY_ALPHA
#endif // !SIMUT_DISPLAY_ALPHA
