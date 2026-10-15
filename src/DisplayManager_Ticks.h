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
#include "sensors/SensorHelpers.h"   /* sensorLimitChannel( ), channelValid( ), channelInfo( ) for repairAlarmLimits( ) */

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

/* The dashboard branch: while a slot is in alarm, flip the flash phase every ALARM_FLASH_INTERVAL_MS and tell the
 * driver (uiChanged); when the alarm ends, put the phase and the timers back and tell it again. What the flash looks
 * like is the driver's: the original's uiChanged calls redrawAlarmFlash( ) and restoreNormalDashboard( ), the Ângulo
 * repaints from the state. */
inline __attribute__((always_inline)) void DisplayManager::tickAlarmFlash( ) {
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
			uiChanged( UiChange::ALARM_FLASH, 0 );
		}
	} else if (_alarmFlashPhase && !_lastRenderedState.isBooting) {

		_alarmFlashPhase = false;
		_alarmFlashTimer = 0;
		_alarmRotateTimer = 0;
		C1_PHASE(C1P_ALARM_FLASH);
		uiChanged( UiChange::ALARM_RESTORE, 0 );
	}
}

/* The panels are in min/max mode and nobody has touched the screen for MINMAX_IDLE_MS: they go back to normal mode.
 * render( ) asks and does the return itself: its drawSlotPanel calls need its own `st`. */
inline __attribute__((always_inline)) bool DisplayManager::minMaxIdleDue( ) const {
	return (_topPanel.showMinMax || _bottomPanel.showMinMax) &&
	    timeSince(_lastTouchTime, MINMAX_IDLE_MS);
}

/* The limits of the alarm being edited: MIN >= MAX is not representable on screen and would arm an alarm that can
 * never clear, so every channel is pulled apart before anything is drawn. The original does it in drawAlarmEdit( ), the
 * Ângulo's pass while its edit screen is up. */
inline __attribute__((always_inline)) void DisplayManager::repairAlarmLimits( SensorType sType, uint8_t nLimits ) {
	for (uint8_t i = 0; i < nLimits; i += 2) {
		const uint8_t c = sensorLimitChannel(sType, i);
		if (!channelValid(c)) continue;
		if (_tempAlarmConfig.chMin[c] < _tempAlarmConfig.chMax[c]) continue;
		const ChannelInfo& ci = channelInfo(c);
		_tempAlarmConfig.chMax[c] = round((_tempAlarmConfig.chMin[c] + 0.1f) * 10.0f) / 10.0f;
		if (_tempAlarmConfig.chMax[c] > ci.saneMax) {
			_tempAlarmConfig.chMax[c] = ci.saneMax;
			_tempAlarmConfig.chMin[c] = ci.saneMax - 0.1f;
		}
	}
}

/* The focused limit stays inside the sensor's limit list. */
inline __attribute__((always_inline)) void DisplayManager::clampEditFocus( uint8_t nLimits ) {
	if (_editFieldFocus >= (int)nLimits) _editFieldFocus = (int)nLimits - 1;
	if (_editFieldFocus < 0) _editFieldFocus = 0;
}

/* The packet arrow flashes (state 3) for a second after a packet leaves: it alternates every 300 ms and goes back to state 1
 * when the second is over. Says what it found, for the one that draws it: a flash that goes on and one that has just ended
 * each have a colour of their own, and neither is the colour of no flash (the volatile state is read here once, as the old
 * block read it, and the answer carries the decision). */
inline __attribute__((always_inline)) DisplayManager::ArrowFlash DisplayManager::pktArrowShown( ) {
	if (_pktArrowState == 3) {
		uint32_t now = millis( );
		if (now >= _pktArrowFlashEnd) {
			_pktArrowState = 1;
			return ArrowFlash::OVER;
		} else {
			if (now - _pktArrowFlashTime >= 300) {
				_pktArrowFlashOn = !_pktArrowFlashOn;
				_pktArrowFlashTime = now;
			}
			return ArrowFlash::GOING;
		}
	}
	return ArrowFlash::NONE;
}
