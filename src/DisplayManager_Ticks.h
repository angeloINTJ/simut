/**
 * @file DisplayManager_Ticks.h
 * @brief The core's timers and transitions of the Core 1 pass: the part of the screen chain in loopCore1( ) that
 *        keeps time and moves the state whatever the UI driver draws. Defined here, inline, so that the original's
 *        chain and a driver's own pass (stage S4 of docs/analysis/UI_DRIVERS.md) run the same code and not a copy.
 *
 * Each tick is declared in class DisplayManager as `inline __attribute__((always_inline))`, the same for every
 * driver, and called where its code used to sit, so the compiled image keeps the code it had. Included by
 * DisplayManager.cpp only, inside its `#if !SIMUT_DISPLAY_ALPHA`: they are the panel's, and the alpha display has none.
 */
#pragma once

#include "DisplayManager.h"
#include "FlashIrqProbe.h"   /* C1_PHASE */

/* The PIN screen: a permanent lockout ends in the dashboard when its time is reached, a timed one repaints the
 * countdown each pass and clears itself when it ends. */
inline __attribute__((always_inline)) void DisplayManager::tickAuthLockout( ) {
	if (_permanentLockout) {
		/* Wrap-safe: direct comparison with millis() fails on wrap every ~49.7 days. */
		if (timeReached(_lockoutUntil)) forceDashboard( );
	} else if (_lockoutUntil > 0) {
		if (!timeReached(_lockoutUntil)) _repaintSettings = true;
		else { _lockoutUntil = 0; _forceSettingsRedraw = true; _repaintSettings = true; }
	}
}

/* The touch sensitivity screen: 1.5 s after it is done, go on to the position calibration. */
inline __attribute__((always_inline)) void DisplayManager::tickTouchSens( ) {
	/* After 1.5s from completion, advance to position calibration */
	if (_sensDone && timeSince(_sensDoneTime, 1500)) {
		_uiMode = MODE_SETTINGS_TOUCH_CAL;
		_calStep = 0;
		_calPhase = 0;
		_forceSettingsRedraw = true;
		_repaintSettings = true;
	}
}
