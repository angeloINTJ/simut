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

/* The dashboard branch: sample whether the web server holds the device, follow an alarm that asked for its page,
 * and rotate the bottom panel through the sensors in alarm every ALARM_ROTATE_INTERVAL_MS. */
inline __attribute__((always_inline)) void DisplayManager::tickDashboard( ) {
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


	if ((_alarmSlotMask != 0 || _alarmErrMask != 0) && !_alarmSilenced) {
		uint16_t m = _alarmSlotMask | _alarmErrMask;
		int alarmCount = 0;
		/* Slot fixado no painel superior não conta para a rotação:
		 * ele já está sempre visível no topo; rotacioná-lo também para
		 * o painel inferior duplicaria o sensor nos dois painéis. */
		for (int i = 0; i < 10; i++) {
			if ((m & (1 << i)) && !(_topPanel.fixed && _topPanel.fixedIdx == i)) alarmCount++;
		}

		if (alarmCount >= 2 && timeSince(_alarmRotateTimer, ALARM_ROTATE_INTERVAL_MS)) {
			_alarmRotateTimer = millis( );
			int current = _lastRenderedState.selectedSlotIdx;
			for (int i = 1; i <= 10; i++) {
				int idx = (current + i) % 10;
				if ((_alarmSlotMask | _alarmErrMask) & (1 << idx)) {
					/* Nunca rotacionar o slot fixado no topo para o
					 * painel inferior (duplicação). */
					if (_topPanel.fixed && _topPanel.fixedIdx == idx) continue;
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
}

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
