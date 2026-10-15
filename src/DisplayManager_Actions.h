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
#include "simut_config.h"   /* SIMUT_TFT_GRAPH is tested below: included here, not left to the includer */
#include "display/DashHold.h"   /* dashHoldStep( ), dashNextPage( ): the hold's clock and the page's turn, without hardware */

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

/* The dashboard's lower card, once its gate (acceptTouch( 1 )) has let the touch through: on the graph button of a card in
 * min/max it opens the graph, on a card in alarm it opens the alarm's action screen, and otherwise it turns the card over
 * between its two faces. `onGraphBtn` is whether the touch fell on the button's strip, which the caller decides from x. */
inline __attribute__((always_inline)) void DisplayManager::actBottomCardTap( bool onGraphBtn ) {
	(void)onGraphBtn;   /* only a build with the graph reads it */
	/* slot selecionado — sem limite 0..10 aqui: o gráfico (EVT_OPEN_GRAPH)
	 * só cobre slots 0..10, mas a AÇÃO DE ALARME vale para qualquer slot
	 * (erro de um sensor em slot > 10 também deve abrir a tela de silenciar). */
	int sel = _sharedState.selectedSlotIdx;
	bool selOk = (sel >= 0 && sel < 16);

#if SIMUT_TFT_GRAPH
	/* Right corner: graph button (priority over alarm) */
	if (_bottomPanel.showMinMax && onGraphBtn) {
		_bottomPanel.showMinMax = false;
		if (sel >= 0 && sel <= 10) {
			UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = sel; ev.param = 0;
			pushUiEvent(ev);
		}
		return;
	}
#endif

	/* Alarme (LIMITE ou ERRO) não silenciado: toque → tela de silenciar.
	 * Silenciado → min/max normal. */
	if (selOk && !_alarmSilenced &&
	    (isSlotAlarming(sel) || isSlotErrAlarming(sel))) {
		showAlarmAction((int8_t)sel);
		return;
	}

	/* Toggle min/max */
	_bottomPanel.showMinMax = !_bottomPanel.showMinMax;
	uiChanged( UiChange::BOTTOM_PANEL, 0 );
}

/* A touch on the row of buttons under the cards, on button `btnIdx` (the caller maps x to it). The gate depends on the kind of
 * the button, so it is in here: the page button takes acceptTouch( 14 ); the settings button and the slot buttons take the
 * sliding gate (acceptSlideTouch( 20 ) and ( 10 + slot )), which lets a finger slide from one slot to the next. */
inline __attribute__((always_inline)) void DisplayManager::actNavButton( int btnIdx ) {
	DashBtn btns[5];
	int totalPages = 1; bool paging = false;
	(void)buildDashLayout(btns, &totalPages, &paging);
	if (btnIdx < 0 || btnIdx > 4) return;
	const DashBtn &b = btns[btnIdx];
	if (b.kind < 0) return; /* touch on empty position (gap) — ignore */
	if (b.kind == 2) { /* PAGE */
		if (!acceptTouch(14)) return;
		_currentPage = dashNextPage( _currentPage, totalPages );
		uiChanged( UiChange::BOTTOM_BUTTONS, _sharedState.selectedSlotIdx ); return;
	}
	if (b.kind == 1) { /* CFG */
		if (!acceptSlideTouch(20)) return;
		UiEvent ev; ev.type = UiEvent::EVT_OPEN_SETTINGS;
		pushUiEvent(ev); return;
	}
	/* SLOT */
	if (!acceptSlideTouch(10 + b.slotId)) return;
	_bottomPanel.showMinMax = false;
	if (!_topPanel.fixed) _topPanel.showMinMax = false;
	uiChanged( UiChange::BOTTOM_BUTTONS, b.slotId );
	UiEvent ev; ev.type = UiEvent::EVT_SLOT_SELECT; ev.id = b.slotId;
	pushUiEvent(ev);
}

/* A touch in the dashboard's upper card zone (y 36..109), its gate included. What happens is told apart by what the finger has been
 * doing: a touch that arrives while a long press is being let go of is swallowed (lockUntil); a first touch on the graph button
 * of a card in min/max opens the graph; a first touch on a card in alarm opens the alarm's action screen; any other first touch
 * starts the hold, and a touch that goes on, watched without a gap for DASH_HOLD_MS (3 s), toggles pinned <-> interactive. The
 * release before that is the short tap, which handleTouch( ) answers at its top. `onGraphBtn` is whether the touch fell on the
 * graph button's rectangle, which the caller decides from x. The gate, acceptTouch( 0 ), is in here: firstTouch decides three of
 * the things above, so it cannot stay outside. */
inline __attribute__((always_inline)) void DisplayManager::actTopCardPress( bool onGraphBtn ) {
	(void)onGraphBtn;   /* only a build with the graph reads it */
	/* Deaf while a long press is being let go of. Anything arriving here is
	 * the lift itself, and its coordinates are not to be trusted. */
	if (!timeReached(_topPanel.lockUntil)) {
		(void)acceptTouch(0);      /* consume, so it cannot land as a first touch */
		_topPanel.holdStart = 0;
		return;
	}
	bool firstTouch = acceptTouch(0);

	/* There WAS a shortcut here: a touch-down anywhere past x = 280 toggled
	 * pinned/interactive immediately, because that corner draws the [Amb]/[Sx]
	 * indicator. It is gone (2026-09-18).
	 *
	 * It had already been narrowed once — it was eating the right third of the
	 * min/max graph button — and the narrowing did not fix what people actually
	 * hit: outside min/max the same quick tap on the right still jumped the
	 * panel into selection mode, while the identical tap two centimetres to the
	 * left opened min/max. A 40-pixel strip that answers a different gesture
	 * than the rest of the panel it is drawn on is not a shortcut, it is a trap,
	 * and this is the second report of it.
	 *
	 * The indicator is still drawn and still means what it meant; it is now
	 * read-only, like the rest of the panel's decoration. Pinning is the long
	 * press, over the whole panel. */

	/* The min/max graph button, on touch-down: the caller tests x against the rectangle it is painted on (handleTouch( )). */
#if SIMUT_TFT_GRAPH
	if (_topPanel.showMinMax && firstTouch && onGraphBtn) {
		_topPanel.showMinMax = false;
		/* topSlotIdx, not -1. The hardcoded -1 was the sentinel from when this panel
		 * was always the ambient sensor; it now follows _topPanel.fixedIdx or mirrors
		 * the selection, and pullSnapshot keeps topSlotIdx in step with both.
		 *
		 * -1 did not crash — renderGraphOptimized bounds-checks it — it just left
		 * pkg.hwId empty, so no record in any history file matched and the graph came
		 * up "No Data". Worse, an hwId that matches nothing never fills pkg.count, so
		 * the V4 read loop ran to the end of every file and rebooted the device. The
		 * guard mirrors the bottom panel's, which had it all along. */
		int topIdx = _sharedState.topSlotIdx;
		if (topIdx >= 0 && topIdx <= 10) {
			UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = topIdx; ev.param = 0;
			pushUiEvent(ev);
		}
		return;
	}
#endif

	/* Alarme (LIMITE ou ERRO) NÃO silenciado: toque → tela de silenciar.
	 * Ação por slot: abre a ação do sensor EXIBIDO no painel tocado (o topo
	 * fixado num sensor em erro não pode cair no min/max). Silenciado →
	 * comportamento normal (pin / min-max). */
	if (firstTouch && !_alarmSilenced) {
		int8_t tSlot = _sharedState.topSlotIdx;
		if (tSlot >= 0 && tSlot < 16 &&
		    (isSlotAlarming(tSlot) || isSlotErrAlarming(tSlot))) {
			showAlarmAction(tSlot);
			return;
		}
	}

	if (firstTouch) {
		/* Start hold tracking — action deferred to release or DASH_HOLD_MS (3s) */
		_topPanel.holdStart = millis();
		_topPanel.holdSeen = _topPanel.holdStart;
		_topPanel.holdFired = false;
		return;
	}

	/* Holding. The finger is only "still down" for spans this loop actually
	 * watched: a gap means it was either lifted or Core 1 was paused, and
	 * neither is evidence of a continuous press. Restart rather than count
	 * time nobody observed. */
	const uint32_t nowMs = millis();

	/* Long press toggles pinned <-> interactive, from ANY state including
	 * min/max — the gesture is the whole contract now, so refusing it while
	 * min/max is up would leave the user holding a finger on a panel that does
	 * nothing, with no way to tell why. Entering the selection also drops
	 * min/max, because the numbers it was showing belong to the sensor the
	 * panel is about to stop following.
	 *
	 * The guard that used to exclude min/max here existed so the mode would not
	 * swap out from under the numbers being read. That risk is now the point of
	 * the gesture: three seconds of deliberate hold is not a gesture anyone
	 * performs while reading. */
	/* dashHoldStep( ) (display/DashHold.h): the gap restart described above, the note of this sample, and whether the three
	 * seconds are up. */
	if (dashHoldStep( nowMs, _topPanel.holdStart, _topPanel.holdSeen, _topPanel.holdFired, _lastTouchRegion,
	                  DASH_HOLD_MS, DASH_HOLD_GAP_MS )) {
		_topPanel.holdFired = true;
		/* Everything this region produces from here to the end of the lift is
		 * discarded. Survives the release reset on purpose: it is a deadline. */
		_topPanel.lockUntil = nowMs + DASH_HOLD_LOCK_MS;
		_touchSoundPending = true;
		_topPanel.showMinMax = false;
		_topPanel.fixed = !_topPanel.fixed;
		if (_topPanel.fixed)
			_topPanel.fixedIdx = _sharedState.selectedSlotIdx;
		redrawTopPanel( );
	}
}

/* The boot screen's skip button was touched (uiBootSkipHit( ) said so): the boot terminal polls the flag (isSkipPressed( )),
 * clears it and ends its wait. There is no gate: the flag is set again on every sample of the press and read once. */
inline __attribute__((always_inline)) void DisplayManager::actBootSkip( ) {
	_skipPressed = true;
}
