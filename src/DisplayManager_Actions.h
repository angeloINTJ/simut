/**
 * @file DisplayManager_Actions.h
 * @brief The core's touch actions of the Painel group (the dashboard and the alarm's action screen): what a touch
 *        on one of their zones does to the state, defined once, inline, so that the original's handleTouch( ) and a
 *        driver's own touch glue (the Ângulo's uaToquePainel( )) run the same code and not a copy (stage S5 of
 *        docs/analysis/UI_DRIVERS.md).
 *
 * Each action is declared in class DisplayManager as `inline __attribute__((always_inline))`, next to the core's
 * ticks, and called where its code used to sit, so the compiled image keeps the code it had. The zone decides which
 * action; the action holds the gates (acceptTouch( ) and its kin), the state it moves and the events it raises. What
 * the action draws it asks of the driver through the hooks (uiChanged( ), redrawTopPanel( )'s uiTopPanel( )), never
 * by name.
 *
 * Included LAST, after ui/UiDriverHooks.h, by DisplayManager_Touch.cpp and by the Ângulo's DisplayManager_UiA_Painel.cpp:
 * an action leans on the hook definitions the original inlines, so a unit that takes the actions takes the hooks first.
 */
#pragma once

#include "DisplayManager.h"

/* Repaint the top card: the core's half. It syncs topSlotIdx, moves the bottom card to another slot when the top is pinned
 * on the selected one (with the EVT_SLOT_SELECT that tells the web), mirrors the bottom card's data into the top's, and
 * hands the drawing to the driver (uiTopPanel( )). Not always_inline: the touch on release and the 3 s hold both call it,
 * and the compiler keeps one copy in the unit. It was defined in the class, where every unit parsed its body, and a unit
 * that does not include ui/UiDriverHooks.h cannot see the original's uiTopPanel( ): GCC stopped it on "inline function
 * used but never defined" (-Werror, pico_w_release, 2026-10-15). Moved here, only the units that call it parse it. */
inline void DisplayManager::redrawTopPanel( ) {
	SystemState snap;
	mutex_enter_blocking(&_stateMutex);
	/* Always sync topSlotIdx based on fixed/interactive */
	_sharedState.topSlotIdx = (_topPanel.fixed && _topPanel.fixedIdx >= 0)
	                        ? _topPanel.fixedIdx : _sharedState.selectedSlotIdx;
	/* When top panel is pinned, auto-switch bottom panel to the first
	 * available different slot to avoid showing the same sensor twice. */
	if (_topPanel.fixed && _topPanel.fixedIdx >= 0 &&
	    _topPanel.fixedIdx == _sharedState.selectedSlotIdx &&
	    _sysConfigPtr) {
		int8_t nextSlot = -1;
		for (int i = 0; i < MAX_SENSORS; i++) {
			if (i != _topPanel.fixedIdx && _sysConfigPtr->sensors[i].active) {
				nextSlot = (int8_t)i; break;
			}
		}
		if (nextSlot >= 0) {
			_sharedState.selectedSlotIdx = nextSlot;
			UiEvent ev; ev.type = UiEvent::EVT_SLOT_SELECT; ev.id = nextSlot;
			pushUiEvent(ev); /* single choke point — invariant 2. */
		}
	}
	/* Mirror slot* data when interactive or showing the same sensor.
	 * NOT when fixed on a different slot: a pinned sensor in ERROR (missing
	 * → topSlotValid=false) must keep its own panel, not mirror the bottom's. */
	if (!_topPanel.fixed ||
	    _sharedState.topSlotIdx == _sharedState.selectedSlotIdx) {
	_sharedState.topSlotTemp = _sharedState.slotTemp;
	_sharedState.topSlotHum  = _sharedState.slotHum;
	_sharedState.topSlotPres = _sharedState.slotPres;
	_sharedState.topSlotType = _sharedState.slotType;
	_sharedState.topSlotValid = _sharedState.slotValid;
	safeCopy(_sharedState.topSlotName, _sharedState.slotName, 31);
	}
	snap = _sharedState;
	mutex_exit(&_stateMutex);
	uiTopPanel(snap);   /* the card and the bottom buttons are the driver's to show */
}

/* The alarm's action screen (MODE_ALARM_ACTION, opened for the slot _alarmActionSlot): what each of its three buttons does
 * once its gate has let the touch through. Whoever decided which button was hit holds the gate, acceptTouch( 0 ), ( 1 ) or
 * ( 2 ): the original from the touch's y, the Ângulo from the target. */

/* Silence the slot's alarm for two minutes. */
inline __attribute__((always_inline)) void DisplayManager::actAlarmSilence( ) {
	UiEvent ev;
	ev.type = UiEvent::EVT_ALARM_SILENCE;
	ev.id = _alarmActionSlot;
	ev.param = 120;
	pushUiEvent(ev);
}

/* Turn the slot's alarm off. */
inline __attribute__((always_inline)) void DisplayManager::actAlarmDeactivate( ) {
	UiEvent ev;
	ev.type = UiEvent::EVT_ALARM_DEACTIVATE;
	ev.id = _alarmActionSlot;
	ev.param = 0;
	pushUiEvent(ev);
}

/* Back to the dashboard with a card in min/max. Moved as it stood. The top card's branch needs a negative _alarmActionSlot,
 * and showAlarmAction( ), the only writer of the slot, is called with a slot checked to be >= 0 (Touch.cpp, once for the top
 * card and once for the bottom card), so the bottom card takes it even when the screen was opened from the top card: a
 * defect of the original, kept (S5_PLANO.md section 6, item 10). */
inline __attribute__((always_inline)) void DisplayManager::actAlarmShowMinMax( ) {
	/* Return to dashboard with the panel in min/max mode */
	if (_alarmActionSlot < 0) {
		_topPanel.showMinMax = true;
	} else {
		_bottomPanel.showMinMax = true;
	}
	_uiMode = MODE_DASHBOARD;
	_forceFullRedraw = true;
	mutex_enter_blocking(&_stateMutex);
	_isDirty = true;
	mutex_exit(&_stateMutex);
}

/* The rest follow, one commit each (S5): the dashboard's cards and buttons, and the boot screen's skip. */
