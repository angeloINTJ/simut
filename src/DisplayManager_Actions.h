/**
 * @file DisplayManager_Actions.h
 * @brief The core's touch actions of the Painel group (the dashboard and the alarm's action screen), of the Base group (the
 *        confirmation dialogs), of the Menus group (the settings list, the alarms list and the licence's pages), of the Gráficos
 *        group (the graph, its detail, the calendar and the update screen's tap) and of the Ajustes 1 group (the themes, the language,
 *        the clock entry, the display alignment and the sounds with their melody picker) and of the Ajustes 2 group (the limits editor, the maintenance
 *        window, the PIN policy, the touch sensitivity's Cancelar and the calibration's result button) and of the Identidade group (the
 *        account picker and list, the account editor, the sensor menu and the text keyboard; the PIN keypad's are in DisplayManager_PinActions.h):
 *        what a touch on one of their zones does to the state, defined once, inline, so that the original's handleTouch( ) and
 *        handleTouchPanelV24( ) and a driver's own touch glue (the Ângulo's uaToquePainel( ), uaToqueBase( ), uaToqueMenus( ), uaToqueGraficos( ),
 *        uaToqueAjustes( ) and uaToqueIdentidade( )) run the same code and not a copy (stages S5 to S10a of docs/analysis/UI_DRIVERS.md).
 *
 * Each action is declared in class DisplayManager as `inline __attribute__((always_inline))`, next to the core's
 * ticks, and called where its code used to sit, so the compiled image keeps the code it had. The zone decides which
 * action; the action holds the gates (acceptTouch( ) and its kin), the state it moves and the events it raises. What
 * the action draws it asks of the driver through the hooks (uiChanged( ), redrawTopPanel( )'s uiTopPanel( )), never
 * by name.
 *
 * Included LAST, after ui/UiDriverHooks.h, by DisplayManager_Touch.cpp and DisplayManager_UsersUi.cpp, and by the Ângulo's
 * DisplayManager_UiA_Painel.cpp, DisplayManager_UiA.cpp, DisplayManager_UiA_Menus.cpp, DisplayManager_UiA_Graficos.cpp,
 * DisplayManager_UiA_Ajustes.cpp and DisplayManager_UiA_Identidade.cpp (none
 * includes a hooks header: the Ângulo's hooks are ordinary functions): an action leans on the hook definitions the original inlines,
 * so a unit that takes the actions takes the hooks first.
 */
#pragma once

#include "DisplayManager.h"
#include "simut_config.h"   /* SIMUT_TFT_GRAPH is tested below: included here, not left to the includer */
#include "display/DashHold.h"   /* dashHoldStep( ), dashNextPage( ): the hold's clock and the page's turn, without hardware */
#include "display/SettingsSteps.h"   /* volumeStep( ), offsetStep( ), limitStepMag( ): the settings screens' steps and the limits' ramp, without hardware */
#include "sensors/SensorHelpers.h"   /* sensorLimitChannel( ), channelInfo( ): the limits step (S9b) */

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
 * and showAlarmAction( ), the only writer of the slot, is called with a slot checked to be >= 0 (by actTopCardPress and by
 * actBottomCardTap, below), so the bottom card takes it even when the screen was opened from the top card: a defect of the
 * original, kept (docs/analysis/UI_DRIVERS.md, section 2.8). */
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

/* The Base group's confirmations (MODE_CONFIRM_MUTE_ALL, MODE_SETTINGS_USER_CONFIRM_DEL, MODE_CONFIRM_AP): what each button does once
 * its gate has let the touch through. Whoever decided which button was hit holds the gate its old zone held, acceptTouch( ) with the
 * zone's number, except where a comment below says the gate is inside the action. */

/* "Mute all", Back: cancel, and return to the sounds screen with nothing changed. The caller holds acceptTouch( 0xC0 ). */
inline __attribute__((always_inline)) void DisplayManager::actMuteAllBack( ) {
	/* Back — cancel, return to Sounds menu without changes. */
	_uiMode = MODE_SETTINGS_SOUNDS;
	_forceSettingsRedraw = true;
	_repaintSettings = true;
}

/* "Mute all", Confirm, its gate included. The gate is hand-written because acceptTouch( ) would fire the touch's click, and the
 * confirmation sound plays instead. Applies the mute to all seven sound settings (the web does the same) and returns to the sounds
 * screen, where Save sends them to Core 0. Moved as it stood, early return and all: nothing follows the mode chain of handleTouch( ),
 * so a touch the gate refuses ends there, as it did when the code sat in the branch. */
inline __attribute__((always_inline)) void DisplayManager::actMuteAllConfirm( ) {
	/* Manual acceptance gate (no acceptTouch which would fire
	 * SND_TOUCH_CLICK). We fire SND_CONFIRM before applying mute to
	 * give "action confirmed" feedback — the sound reaches Core 0 and is
	 * processed before save. _muted in SoundManager only changes on save. */
	if (!_touchReleased) return;
	_touchReleased = false;
	_lastTouchRegion = 0xC1;
	_lastRegionTouchTime = millis( );
	_lastTouchTimestamp = millis( );
	_touchSoundPending = false; /* no touch beep */
	requestPreviewSound(SND_CONFIRM, _soundSettings.confirmMelody);

	/* Confirm — applies mute=true and turns off all sounds (web parity). */
	_soundSettings.muted = true;
	_soundSettings.touchEnabled = false;
	_soundSettings.confirmEnabled = false;
	_soundSettings.errorEnabled = false;
	_soundSettings.alarmEnabled = false;
	_soundSettings.webEnabled = false;
	_soundSettings.attentionEnabled = false;
	_uiMode = MODE_SETTINGS_SOUNDS;
	_forceSettingsRedraw = true;
	_repaintSettings = true;
}

/* The account-deletion confirmation (MODE_SETTINGS_USER_CONFIRM_DEL, from the account's editor): its two buttons, once their gates have
 * let the touch through. The caller holds acceptTouch( 0 ) for Cancel and acceptTouch( 1 ) for Delete. */

/* Cancel: back to the account's editor, the account untouched. */
inline __attribute__((always_inline)) void DisplayManager::actUserDelCancel( ) {
	showUserEdit(_userEditSlot, false);
}

/* Delete: the panel only asks. Core 0 checks that the session holds PERM_USER_MGR, refuses the admin, an empty slot and the account in
 * use, and deletes (AppManager_Panel.cpp, EVT_USER_DEL); the answer comes back as a panel message. The permission is not checked here, so
 * a driver calls this only from the dialog's confirm button, behind acceptTouch( 1 ). */
inline __attribute__((always_inline)) void DisplayManager::actUserDelConfirm( ) {
	UiEvent ev; ev.type = UiEvent::EVT_USER_DEL; ev.id = _userEditSlot; ev.param = 0;
	pushUiEvent(ev);
}

/* The AP confirmation (MODE_CONFIRM_AP, a Wi-Fi build's), Confirm, once its gate (acceptTouch( 0xC3 )) has let the touch through. Its Back is
 * showSettingsMain( ), core code, behind acceptTouch( 0xC2 ). Defined in every build, with no #if of its own: only the Wi-Fi builds call it. */
inline __attribute__((always_inline)) void DisplayManager::actStartAp( ) {
	/* Core 0 owns the bit check and the radio; the panel only asks.
	 * Nothing is drawn from here: startApMode( ) puts the boot terminal
	 * up before it touches the radio and ends it with the network, the
	 * key and the address. This used to say beginAP( ) did that; nothing
	 * did, and the confirmation stayed on the glass (fixed 2026-09-24). */
	UiEvent ev; ev.type = UiEvent::EVT_START_AP; ev.id = 0; pushUiEvent(ev);
}

/* The settings list and the alarms list (MODE_SETTINGS_MAIN, MODE_SETTINGS_ALARMS): what a row's button does once its gate has let the touch
 * through. Whoever decided which row was hit holds the gate: the original acceptTouch( 13 ) for the list's Open button and
 * acceptTouch( clickedIndex + 4 ) for a second tap on the selected alarms row, the Ângulo acceptTouch( ZONA_LINHA ) for a tap on a row. */

/* The settings list's Open button, on row `idx` of the list (_menuItems[ ]): ask Core 0 to open the item the row stands for. The row is tested
 * against the list's length here, as it was in the branch; the Ângulo tests it ahead of its own gate as well (a tap on no row must not click),
 * so for it this is a second look. The event leaves with `param` as the stack had it: Core 0 reads only `id` for this event
 * (AppManager_Events.cpp). */
inline __attribute__((always_inline)) void DisplayManager::actMenuSelect( int idx ) {
	/* v24: the id is the ITEM (display/SettingsMenu.h), not the row —
	 * the list follows MENU_ORDER and the session's bits, so row 1 may be
	 * any item. */
	if (idx < _menuCount) {
		UiEvent ev; ev.type = UiEvent::EVT_MENU_SELECT; ev.id = _menuItems[idx]; pushUiEvent(ev);
	}
}

/* The alarms list, on row `idx` (a position in _activeSensorsMap[ ], which the caller has checked against _activeSensorCount): open that
 * sensor's action menu. In the original this is the second tap, on the row already selected; in the Ângulo a tap on a row. */
inline __attribute__((always_inline)) void DisplayManager::actAlarmListOpen( int idx ) {
	/* v24: the selected row opens the sensor's action menu — limits, block
	 * and maintenance live there, each behind the session's own bit, each
	 * applied and recorded by Core 0. The ON/OFF word is a status now. */
	showAlarmSensorMenu(_activeSensorsMap[idx]);
}

/* The licence screen (MODE_SETTINGS_LICENSE): one page back (`dir` < 0) or forward (any other `dir`; the callers pass -1 and +1), held at the ends, once the touch's gate has let
 * it through. The original calls it from four places, the text's upper and lower halves and the two arrows, each with a constant `dir`, so it
 * compiles to the four branches it replaced. The caller sets _repaintSettings: the text's halves share one store of it after their if/else,
 * and inside the action it would be repeated in both arms. The Ângulo sets none, because its frame follows the state. */
inline __attribute__((always_inline)) void DisplayManager::actLicensePage( int dir ) {
	if (dir < 0) {
		if (_licensePage > 0) _licensePage--;
	} else {
		if (_licensePage < _licenseTotalPages - 1) _licensePage++;
	}
}

/* The Gráficos group (stage S7: the graph, its detail, the calendar and the update screen): what a touch on one of their zones does once its
 * gate has let it through. Whoever decided which zone was hit holds the gate its old zone held (acceptTouch( ), acceptHoldTouch( ) or
 * acceptSlideTouch( ) with the zone's number), except where a comment below says the gate is inside the action. The actions that read the graph's
 * package take it as `g`, and the caller passes _graphData: the member exists only when SIMUT_TFT_GRAPH is set. */

/* The X of the graph and of its detail: back to the dashboard, with the time offset back at the present. The caller holds acceptTouch( 0 )
 * and returns; the dashboard repaints in full. */
inline __attribute__((always_inline)) void DisplayManager::actGraphClose( ) {
	_graphNavOffset = 0;
	_uiMode = MODE_DASHBOARD;
	_isDirty = true;
	_forceFullRedraw = true;
}

/* The chart's central zone: open the detail on the temperature (page 0). The caller holds acceptTouch( 10 ). _repaintGraph is volatile and
 * stays after the two plain stores, as it was. */
inline __attribute__((always_inline)) void DisplayManager::actGraphDetailOpen( ) {
	_detailPage = 0;
	_uiMode = MODE_GRAPH_DETAIL;
	_repaintGraph = true;
}

/* The detail screen shows page `p` (0 = temperature, 1 = humidity, 2 = pressure). The caller decides which page comes next: the original's
 * cycle from what the sensor has, the Ângulo's pager from the pages it offers. */
inline __attribute__((always_inline)) void DisplayManager::actDetailPage( uint8_t p ) {
	_detailPage = p;
	_repaintGraph = true;
}

/* The detail screen, past its last page: back to the chart. The original's cycle gets here from a tap on the central zone, the Ângulo's
 * "Voltar" from its own button; the caller holds the gate. */
inline __attribute__((always_inline)) void DisplayManager::actDetailBack( ) {
	_detailPage = 0;
	_uiMode = MODE_GRAPH_VIEW;
	_repaintGraph = true;
}

/* The calendar's way back to the chart, which keeps the data it has. The caller holds acceptTouch( 0 ) and returns. */
inline __attribute__((always_inline)) void DisplayManager::actCalendarBack( ) {
	_uiMode = MODE_GRAPH_VIEW;
	_repaintGraph = true;
}

/* A tap on the header of the graph or of its detail: the header shows the sensor's name in place of the date and time, and the stamp is what the
 * loop reads to put them back after 3 s. What it looks like is the driver's (UiChange::GRAPH_HEADER). The caller holds acceptTouch( 0 ) and
 * returns. The Ângulo has no header target and does not call it. */
inline __attribute__((always_inline)) void DisplayManager::actGraphShowName( ) {
	_headerShowName = true;
	_headerNameTimer = millis( );
	uiChanged( UiChange::GRAPH_HEADER, 0 );
}

/* A step in time on the graph or its detail: ask Core 0 for the window before (`dir` = -1) or after (+1) the one on the glass. The caller holds the
 * hold gate (acceptHoldTouch( 10 ) or ( 11 ) in the original, the Ângulo's own zones), says "busy" when its driver does, and tests the sign before
 * the gate: ▶ is offered only when the offset is below 0. Core 0 coalesces consecutive EVT_GRAPH_NAV events (AppManager_Events.cpp), so a held
 * button costs one event per accepted sample. `g` is the package on the glass. */
inline __attribute__((always_inline)) void DisplayManager::actGraphNav( const GraphDataPackage &g, int dir ) {
	UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = g.sensorIdx; ev.param = dir;
	pushUiEvent(ev);
}

/* The calendar: ask Core 0 for the month of the sensor on the glass, with the days that have history (a scan of /history, which is not instant).
 * The caller holds acceptTouch( 0 ). */
inline __attribute__((always_inline)) void DisplayManager::actGraphOpenCalendar( const GraphDataPackage &g ) {
	UiEvent ev; ev.type = UiEvent::EVT_OPEN_CALENDAR; ev.id = g.sensorIdx; ev.param = 0;
	pushUiEvent(ev);
}

/* Another period for the graph of the sensor on the glass: ask Core 0 to open it on `range`. The original steps from the period on the glass, one
 * shorter or one longer, and tests its bounds (> 0, < 4) before the gate; the Ângulo's chips send the period they stand for, and the one that
 * is lit is inert. The caller holds the gate (acceptHoldTouch( 12 ) or ( 13 ) in the original, a tap in the Ângulo). */
inline __attribute__((always_inline)) void DisplayManager::actGraphPeriod( const GraphDataPackage &g, int range ) {
	UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = g.sensorIdx; ev.param = range;
	pushUiEvent(ev);
}

/* The calendar's month arrows: ask Core 0 for the month before (`dir` = -1) or after (+1) the one on the glass, for the sensor of `g`. The caller
 * holds acceptSlideTouch( 20 ) or ( 21 ), which lets a finger slide from one arrow to the next, and says "busy" when its driver does. */
inline __attribute__((always_inline)) void DisplayManager::actCalendarMonth( const GraphDataPackage &g, int dir ) {
	UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = g.sensorIdx; ev.param = dir;
	pushUiEvent(ev);
}

/* A day of the calendar that has history: ask Core 0 for its 24 hours. The caller has tested the day against _calDaysMask and holds acceptTouch( 0 )
 * (the original) or the cell's zone (the Ângulo). Core 0 answers with the loading screen. */
inline __attribute__((always_inline)) void DisplayManager::actCalendarDay( const GraphDataPackage &g, int day ) {
	UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_DAY;
	ev.id = g.sensorIdx;
	ev.param = day;
	pushUiEvent(ev);
}

/* Today, on the calendar: the offset goes back to the present and Core 0 opens the chart again on the period it had. The caller holds acceptTouch( 0 ). */
inline __attribute__((always_inline)) void DisplayManager::actCalendarToday( const GraphDataPackage &g ) {
	_graphNavOffset = 0;
	UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = g.sensorIdx; ev.param = g.timeRange;
	pushUiEvent(ev);
}

/* The update screen's tap (MODE_OTA_UPDATE), its gate included. The gate has to follow the phase test, which is why the action holds both: a caller
 * that took acceptTouch( ) first would arm the touch-priority window on a tap during an update. Moved as it stood, early return and all: the branch is
 * the last of the mode chain in handleTouch( ), and nothing follows it. */
inline __attribute__((always_inline)) void DisplayManager::actOtaDismiss( ) {
	/* Deaf while an update runs, so a hand on the glass changes nothing; a
	 * tap anywhere leaves a screen that reports an ending. Through
	 * acceptTouch only then — it is what arms the touch-priority window,
	 * and that window would answer the apply with a 503. */
	const uint32_t st = __atomic_load_n(&_otaState, __ATOMIC_ACQUIRE);
	if (!otaTapDismisses(otaPhaseOf(st)) || !acceptTouch(0xC6)) return;
	if (otaEnd(st)) forceDashboard( );
}

/* The Ajustes 1 group (stage S8a: the themes, the language, the clock entry, the display alignment, and the sounds with their melody picker): what a touch
 * on one of their zones does once its gate has let it through. Whoever decided which zone was hit holds the gate its old zone held (acceptTouch( ),
 * acceptHoldTouch( ) or acceptSlideTouch( ) with the zone's number), except where a comment below says the gate is inside the action. What stays with each
 * driver is the choosing: the rows, the paging and the wraps of a list, and the bands and columns that tell one button from another. Where the branch of
 * handleTouch( ) ended with a repaint request (_repaintSettings, volatile), the action ends with it. */

/* The themes screen's Apply: ask Core 0 to apply theme `idx` and take the panel to the dashboard. The original passes the theme on show
 * (_previewThemeIdx), the Ângulo the row that was tapped. Core 0 reads only `id` of this event (AppManager_Events.cpp), so `param` is left as the stack
 * had it. The caller holds the gate (acceptTouch( 13 ) in the original). */
inline __attribute__((always_inline)) void DisplayManager::actApplyTheme( int idx ) {
	UiEvent ev; ev.type = UiEvent::EVT_APPLY_THEME; ev.id = idx; pushUiEvent(ev);
}

/* The language screen's Apply: ask Core 0 to apply language slot `idx` and leave for the dashboard. Core 0 reads only `id`, as for the theme. The caller
 * has tested the slot against the ones on offer (English alone until a pack is loaded) and holds the gate (acceptTouch( 13 ) in the original). */
inline __attribute__((always_inline)) void DisplayManager::actApplyLang( int idx ) {
	UiEvent ev; ev.type = UiEvent::EVT_APPLY_LANG; ev.id = idx; pushUiEvent(ev);
}

/* The clock entry (MODE_SET_CLOCK). */

/* Leave the entry, by Pular at boot or Voltar from the settings: at boot the panel goes on to the dashboard, otherwise back to the settings list. The boot
 * flag tells them apart, so the Ângulo's two buttons call the same action. Core 0 also writes _uiMode, so the boot case's three stores are under
 * _stateMutex. The caller holds acceptTouch( 0xC4 ). */
inline __attribute__((always_inline)) void DisplayManager::actClockLeave( ) {
	if (_clockAtBoot) {
		mutex_enter_blocking(&_stateMutex);
		_uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true;
		mutex_exit(&_stateMutex);
	} else {
		showSettingsMain( );
	}
}

/* Save: hand the fields to Core 0, which checks them again and decides whether this session may set the clock (AppManager_Events.cpp); the panel draws
 * nothing until it answers. The caller holds acceptTouch( 0xC5 ). */
inline __attribute__((always_inline)) void DisplayManager::actClockSave( ) {
	UiEvent ev; ev.type = UiEvent::EVT_SET_CLOCK;
	clockEntryPack(_clockEntry, ev.id, ev.param);
	pushUiEvent(ev);
}

/* One step of column `i` (a field of the entry), `delta` +1 or -1: the value moves within its limits (display/ClockEntry.h) and only the values are
 * repainted. The caller holds the hold gate of that column and direction, acceptHoldTouch( 0xD0 + 2 * i + (delta > 0) ), so a held button repeats. */
inline __attribute__((always_inline)) void DisplayManager::actClockStep( uint8_t i, int delta ) {
	clockEntryStep(_clockEntry, i, delta);
	_clockValuesDirty = true;
	_repaintSettings = true;
}

/* The display alignment (MODE_SETTINGS_DISPLAY_OFFSET). The four arrows move the preview by one step each and stop at the limit the panel holds the offset
 * to (DISPLAY_OFFSET_LIMIT, -4 and +4, TftWithOffset::setDisplayOffset( ); display/SettingsSteps.h); each says whether the preview moved, and the caller then calls actOffsetPreview( ) once, which applies
 * it to the panel at once. The caller holds the hold gate of the arrow: zone 20 for up, 21 for down, 22 for left and 23 for right. */
inline __attribute__((always_inline)) bool DisplayManager::actOffsetUp( ) {
	return offsetStep( _offsetPreviewY, -1 );
}

inline __attribute__((always_inline)) bool DisplayManager::actOffsetDown( ) {
	return offsetStep( _offsetPreviewY, +1 );
}

inline __attribute__((always_inline)) bool DisplayManager::actOffsetLeft( ) {
	return offsetStep( _offsetPreviewX, -1 );
}

inline __attribute__((always_inline)) bool DisplayManager::actOffsetRight( ) {
	return offsetStep( _offsetPreviewX, +1 );
}

/* Redefinir: both axes back to zero. True when the preview moved. The caller holds acceptTouch( 24 ). */
inline __attribute__((always_inline)) bool DisplayManager::actOffsetCentre( ) {
	if (_offsetPreviewX != 0 || _offsetPreviewY != 0) {
		_offsetPreviewX = 0; _offsetPreviewY = 0; return true;
	}
	return false;
}

/* The preview moved: apply it to the panel now, so the image shifts under the finger, and ask for a full repaint of the screen. */
inline __attribute__((always_inline)) void DisplayManager::actOffsetPreview( ) {
	if (_driver.tft) _driver.tft->setDisplayOffset(_offsetPreviewX, _offsetPreviewY);
	/* Full redraw: previous frame was drawn with a different offset,
	 * old pixels remain outside the new area and need to be cleared. */
	_forceSettingsRedraw = true;
	_repaintSettings = true;
}

/* Voltar: the preview is discarded, the saved offset goes back onto the panel (on Core 1, before the menu is shown) and the settings list comes up. The
 * caller holds acceptTouch( 25 ) and returns.
 * Every other exit from the screen leaves it to tickDisplayOffset( ) (CalibrationCore.cpp), which puts the saved offset back on Core 1's next pass. */
inline __attribute__((always_inline)) void DisplayManager::actOffsetBack( ) {
	/* Discard preview adjustment: restore saved offset before exiting. */
	_offsetPreviewX = _offsetSavedX;
	_offsetPreviewY = _offsetSavedY;
	if (_driver.tft) _driver.tft->setDisplayOffset(_offsetSavedX, _offsetSavedY);
	showSettingsMain( );
}

/* Salvar: the panel only asks. Core 0 saves the offset, zeroes the touch calibration and has the touch calibrated again (AppManager_Events.cpp), so a driver
 * calls this only from the Save button, behind acceptTouch( 26 ), and returns. */
inline __attribute__((always_inline)) void DisplayManager::actOffsetApply( ) {
	UiEvent ev;
	ev.type = UiEvent::EVT_APPLY_DISPLAY_OFFSET;
	ev.id = _offsetPreviewX;
	ev.param = _offsetPreviewY;
	pushUiEvent(ev);
}

/* The melody picker (MODE_SETTINGS_SOUNDS with _inMelodySelect set): which of the six melodies a sound plays. _melSelectType is the sound (0 touch,
 * 1 confirmation, 2 error, 3 alarm, 4 attention) and _melSelectIdx the melody on show; nothing reaches the settings until Use. */

/* Pick melody `idx` (0 to 5): it becomes the one on show and plays as a preview on the event of the sound being chosen, and the screen is repainted. The
 * caller does the choosing, the original's rows and wraps and the Ângulo's rows, and holds the gate. */
inline __attribute__((always_inline)) void DisplayManager::actMelodyPick( uint8_t idx ) {
	_melSelectIdx = idx;
	SoundEvent evType = SND_NONE;
	switch (_melSelectType) {
	case 0: evType = SND_TOUCH_CLICK; break;
	case 1: evType = SND_CONFIRM; break;
	case 2: evType = SND_ERROR; break;
	case 3: evType = SND_ALARM_START; break;
	case 4: evType = SND_ATTENTION; break;
	}
	if (evType != SND_NONE) requestPreviewSound(evType, _melSelectIdx);
	_repaintSettings = true;
}

/* Voltar: back to the sounds list, nothing taken. The caller holds acceptTouch( 0x92 ). */
inline __attribute__((always_inline)) void DisplayManager::actMelodyBack( ) {
	_inMelodySelect = false;
	_forceSettingsRedraw = true;
	_repaintSettings = true;
}

/* Usar: the picker's own Save. The sound being chosen is turned on with the melody on show, in the copy only (the list's Save writes it), the global mute
 * goes off, as the web does, the confirmation sound plays, and the list comes back. The stores come before the preview, which hands the sound to
 * Core 0. The caller holds acceptTouch( 0x93 ). */
inline __attribute__((always_inline)) void DisplayManager::actMelodyUse( ) {
	switch (_melSelectType) {
	case 0:
		_soundSettings.touchEnabled = true;
		_soundSettings.touchMelody = _melSelectIdx;
		break;
	case 1:
		_soundSettings.confirmEnabled = true;
		_soundSettings.confirmMelody = _melSelectIdx;
		break;
	case 2:
		_soundSettings.errorEnabled = true;
		_soundSettings.errorMelody = _melSelectIdx;
		break;
	case 3:
		_soundSettings.alarmEnabled = true;
		_soundSettings.alarmMelody = _melSelectIdx;
		break;
	case 4:
		_soundSettings.attentionEnabled = true;
		_soundSettings.attentionMelody = _melSelectIdx;
		break;
	}
	/* Parity with web /alarms: turning on any individual sound turns off global mute. */
	_soundSettings.muted = false;
	requestPreviewSound(SND_CONFIRM, _soundSettings.confirmMelody);

	_inMelodySelect = false;
	_forceSettingsRedraw = true;
	_repaintSettings = true;
}

/* The sounds list (MODE_SETTINGS_SOUNDS): its volumes, its switches and its Save. */

/* Open the melody picker from the list, on the sound `type` (0 touch, 1 confirmation, 2 error, 3 alarm, 4 attention) with the melody it has now, `cur`. The
 * caller has decided that it opens, and holds the gate: the original opens it for a sound that is off (a sound that is on is turned off by the row), the
 * Ângulo from the row's name and chevron. */
inline __attribute__((always_inline)) void DisplayManager::actMelodyOpen( uint8_t type, uint8_t cur ) {
	_inMelodySelect = true;
	_melSelectType = type;
	_melSelectIdx = cur;
	_forceSettingsRedraw = true;
	_repaintSettings = true;
}

/* The system volume, one step of 10 down (`down`) or up, held inside 0 and 100; the touch beep is cancelled so that it does not overlap the preview, which
 * plays at the new level. _touchSoundPending is stored before requestVolumePreview( ), whose barrier publishes the level to Core 0. The caller holds the
 * hold gate, acceptHoldTouch( 20 ). */
inline __attribute__((always_inline)) void DisplayManager::actSoundVolume( bool down ) {
	volumeStep( _soundSettings.volume, down );
	_touchSoundPending = false; /* cancel beep to avoid overlapping preview */
	requestVolumePreview(_soundSettings.volume);
	_repaintSettings = true;
}

/* The alarm volume, the same step and the same order (the hold gate is acceptHoldTouch( 21 )). */
inline __attribute__((always_inline)) void DisplayManager::actAlarmVolume( bool down ) {
	volumeStep( _soundSettings.alarmVolume, down );
	_touchSoundPending = false;
	requestAlarmVolumePreview(_soundSettings.alarmVolume);
	_repaintSettings = true;
}

/* A sound's switch flips in the copy (`flag` is one of the six enable flags of _soundSettings), and turning one on turns the global mute off, as the web does:
 * the original runs it for the web row, the Ângulo for all six switches. The caller holds the gate. */
inline __attribute__((always_inline)) void DisplayManager::actSoundToggle( bool &flag ) {
	flag = !flag;
	/* Parity with web /alarms: turning on Web turns off global mute. */
	if (flag) _soundSettings.muted = false;
	_repaintSettings = true;
}

/* Silenciar tudo: turned on, it asks first (the confirmation screen applies the mute); turned off, it is direct. The caller holds the gate. */
inline __attribute__((always_inline)) void DisplayManager::actMuteToggle( ) {
	if (!_soundSettings.muted) {
		/* Will ENABLE Mute All -> confirmation screen. */
		showMuteConfirm( );
	} else {
		/* Disabling is direct, no confirmation. */
		_soundSettings.muted = false;
		_repaintSettings = true;
	}
}

/* Salvar: Core 0 takes the copy (getSoundSettings( ), AppManager_Events.cpp), applies it and writes it. The caller holds acceptTouch( 13 ). */
inline __attribute__((always_inline)) void DisplayManager::actSaveSounds( ) {
	UiEvent ev; ev.type = UiEvent::EVT_SAVE_SOUNDS; ev.id = 0; ev.param = 0;
	pushUiEvent(ev);
}

/* The Ajustes 2 group, the limits editor, the maintenance window and the PIN policy (S8b of docs/analysis/UI_DRIVERS.md). Each action is the code that
 * the original's handleTouch( ) (the editor) or handleTouchPanelV24( ) (the window and the policy) ran after the gate of its zone, moved here word for word;
 * the Ângulo's uaToqueAjustes( ) runs it behind its own gates, which are the same zones. The caller holds the gate. */

/* A limit gets the focus: a tap on its bar away from the arrows in the original (behind acceptSlideTouch( row )), a page of the channel pager in the Ângulo
 * (behind acceptTouch( 0x60 )). A tap is not a hold, so the arrows' repeat is forgotten. `idx` is the limit's index (2n the n-th channel's MIN, 2n + 1 its MAX). */
inline __attribute__((always_inline)) void DisplayManager::actLimitFocus( int idx ) {
	_lastPressedBtn = -1;
	_editFieldFocus = idx;
	_repaintSettings = true;
}

/* Voltar: the edit is discarded (the working copy was never written back) and the sensor's menu comes back. The caller holds acceptTouch( 12 ). */
inline __attribute__((always_inline)) void DisplayManager::actLimitsBack( ) {
	_lastPressedBtn = -1;
	showAlarmSensorMenu(_editSensorIdx);
}

/* Salvar: the working copy stays here for Core 0 to diff against the config (editedAlarmRecord( )); it decides, saves, logs and emits the alarm_lim record, then
 * brings the sensor menu back. The caller holds acceptTouch( 13 ). */
inline __attribute__((always_inline)) void DisplayManager::actLimitsSave( ) {
	_lastPressedBtn = -1;
	UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
	ev.id = _editSensorIdx; ev.param = 0; pushUiEvent(ev);
}

/* The limits editor's step (UI_DRIVERS.md 2.14, form Fb): the three lambdas of handleTouch( )'s limits branch, moved word for word into a function
 * of their own with internal linkage, so that their closures stay local and called once, as they were there; _tempAlarmConfig is `cfg`. */
/* Recorded deviation (UI_DRIVERS.md 2.14): a static inline helper called from an inline member with external linkage is ill-formed, no diagnostic required ([basic.def.odr]);
 * harmless here, because actLimitStep( ) is always_inline and the branch has no LTO, and the internal linkage is what keeps the closures local. */
static inline void limitStepOn( SensorRecord &cfg, SensorType sType, uint8_t nLimits, int at, float by ) {
	auto adjustVal = [](float val, float step, float minV, float maxV) -> float {
		val += step; val = round(val * 10.0f) / 10.0f;
		if (val < minV) val = minV;
		if (val > maxV) val = maxV;
		return val;
	};

	/* MIN and MAX of one channel may not cross. Push the one the user is NOT
	 * holding, so the digit under their finger keeps moving the way they
	 * pressed. Runs over every channel, not the first two. */
	auto enforceInterlock = [&](int heldIdx) {
		for (uint8_t i = 0; i < nLimits; i += 2) {
			const uint8_t c = sensorLimitChannel(sType, i);
			if (!channelValid(c)) continue;
			const ChannelInfo& ci = channelInfo(c);
			if (cfg.chMin[c] >= cfg.chMax[c]) {
				if (heldIdx == (int)i) /* holding this channel's MIN */
					cfg.chMax[c] = round((cfg.chMin[c] + 0.1f) * 10.0f) / 10.0f;
				else
					cfg.chMin[c] = round((cfg.chMax[c] - 0.1f) * 10.0f) / 10.0f;
			}
			if (cfg.chMax[c] > ci.saneMax) cfg.chMax[c] = ci.saneMax;
			if (cfg.chMin[c] < ci.saneMin) cfg.chMin[c] = ci.saneMin;
		}
	};

	auto stepLimit = [&](int idx, float step) {
		const uint8_t c = sensorLimitChannel(sType, (uint8_t)idx);
		if (!channelValid(c)) return;
		const ChannelInfo& ci = channelInfo(c);
		const bool isMax = sensorLimitIsMax((uint8_t)idx);
		/* Read-modify-write rather than a reference: chMin/chMax live in a packed
		 * struct, whose fields have no alignment guarantee to bind a float& to. */
		float v = isMax ? cfg.chMax[c] : cfg.chMin[c];
		v = adjustVal(v, step, ci.saneMin, ci.saneMax);
		if (isMax) cfg.chMax[c] = v; else cfg.chMin[c] = v;
		enforceInterlock(idx);
	};
	stepLimit(at, by);
}

/* An arrow on a limit's bar, held (the caller holds acceptHoldTouch( zone ), the zone of the bar's row and the direction): the hold's ramp,
 * the focus, one step of the limit with the interlock, and the repaint request, which runs on every path, as it did. */
inline __attribute__((always_inline)) void DisplayManager::actLimitStep( SensorType sType, uint8_t nLimits, int idx, uint8_t zone, bool inc ) {
	if (_lastPressedBtn != (int)zone) { _btnHoldStartTime = millis( ); _lastPressedBtn = (int)zone; }
	uint32_t holdTime = millis( ) - _btnHoldStartTime;
	float mag = limitStepMag( holdTime );
	_editFieldFocus = idx;
	limitStepOn(_tempAlarmConfig, sType, nLimits, idx, inc ? mag : -mag);
	_repaintSettings = true;
}

/* The maintenance window's bar gets the focus: a tap on the other bar in the original (behind acceptSlideTouch( 20 + row )), a press of an arrow in the Ângulo (behind the
 * arrow's hold zone). `row` is 0 for the hours and 1 for the minutes. */
inline __attribute__((always_inline)) void DisplayManager::actMaintFocus( int row ) {
	_maintFocus = row;
	_repaintSettings = true;
}

/* A bar's end: the hours go one at a time from 0 to MAINT_MAX_SEC / 3600 without wrapping, the minutes five at a time and wrap 55 <-> 0. The caller holds the hold zone
 * and has checked that the window is closed and that `delta` is -1 or +1. */
inline __attribute__((always_inline)) void DisplayManager::actMaintStep( int row, int delta ) {
	if (row == 0) {
		_maintHours += delta;
		if (_maintHours < 0) _maintHours = 0;
		if (_maintHours > (int)(MAINT_MAX_SEC / 3600u)) _maintHours = (int)(MAINT_MAX_SEC / 3600u);
	} else {
		_maintMinutes += delta * 5;
		if (_maintMinutes < 0) _maintMinutes = 55;
		if (_maintMinutes > 55) _maintMinutes = 0;
	}
	_repaintSettings = true;
}

/* Iniciar or Encerrar. `open` is whether the window is open NOW, which the caller read from the clock: without PERM_MAINT, or opening for 0 h 0 min, it asks for the
 * error sound and sends nothing; otherwise Core 0 gets the seconds (0 closes) and checks the permission again, saves, sends the record and checks the alarms. The
 * caller holds acceptTouch( 13 ). It returns where the original returned `true`: the call stands before that `return true;`. */
inline __attribute__((always_inline)) void DisplayManager::actMaintPrimary( bool open ) {
	if (!(_panelPerms & PERM_MAINT)) { _errorSoundPending = true; return; }
	const int secs = open ? 0 : (_maintHours * 3600 + _maintMinutes * 60);
	if (!open && secs == 0) { _errorSoundPending = true; return; }
	UiEvent ev; ev.type = UiEvent::EVT_MAINT_SET; ev.id = _maintSlot; ev.param = secs;
	pushUiEvent(ev);
}

/* A control of the policy's copy has set the three values: the clamp makes them a pair the panel can run (the length at most the ceiling of the chosen keypad, the keypad
 * one that has a layout for the alphabet) and they are stored. The rows only repaint. The caller holds the gate. */
inline __attribute__((always_inline)) void DisplayManager::actPolicySet( uint8_t minLen, uint8_t keypad, uint8_t alphabet ) {
	clampPinPolicy(minLen, keypad, alphabet);
	_policyMin = minLen; _policyKb = keypad; _policyAlpha = alphabet;
	/* The rows only: see drawPinPolicy( ). */
	_repaintSettings = true;
}

/* Salvar: the policy goes to Core 0 (id the minimum length, param the keypad * 16 + the alphabet), which checks PERM_USER_MGR, clamps it, marks every account whose PIN no
 * longer complies and saves. The caller holds acceptTouch( 13 ). */
inline __attribute__((always_inline)) void DisplayManager::actPolicySave( ) {
	UiEvent ev; ev.type = UiEvent::EVT_PIN_POLICY;
	ev.id = (int)_policyMin; ev.param = (int)_policyKb * 16 + (int)_policyAlpha;
	pushUiEvent(ev);
}

/* Cancelar on the touch sensitivity screen (the caller holds acceptTouch( 0 ), and uiSensCancelHit( ) said the touch landed on it). At boot the screen is the first run's calibration, so
 * Cancelar takes the default calibration and the dashboard (the mode is written with no _stateMutex, as it always was); otherwise it goes back to the settings. */
inline __attribute__((always_inline)) void DisplayManager::actSensCancel( ) {
	if (_sharedState.isBooting) {
		_calXMin = 300; _calXMax = 3800;
		_calYMin = 200; _calYMax = 3700;
		_sensZThreshold = 400;
		_calValid = true;
		_calSwapXY = false;
		_uiMode = MODE_DASHBOARD;
		_forceSettingsRedraw = true;
		_repaintSettings = true;
	} else {
		showSettingsMain( );
	}
}

/* The button the calibration shows with its verdict (uiCalVerdictHit( ) said the touch landed on it). After a success it leaves, at boot to the dashboard (the mode is written with no
 * _stateMutex, as it always was) and otherwise to the settings; after a refusal it starts the four-point run over. */
inline __attribute__((always_inline)) void DisplayManager::actCalVerdictTap( ) {
	if (_calPhase == 2) {
		if (_sharedState.isBooting) {
			_uiMode = MODE_DASHBOARD;
			_isDirty = true;
			_forceFullRedraw = true;
		} else {
			showSettingsMain( );
		}
	} else {
		_calStep = 0;
		_calPhase = 0;
		memset(_calRawX, 0, sizeof(_calRawX));
		memset(_calRawY, 0, sizeof(_calRawY));
		_forceSettingsRedraw = true;
		_repaintSettings = true;
	}
}

/* The Identidade group (S9): the account picker and list, the account editor, the sensor menu and the text keyboard. The PIN keypad's
 * actions are in DisplayManager_PinActions.h: they exist only where a panel proves a PIN, and only the units that have one include them. */

/* The account picker's and the users list's row, opened (the caller holds acceptTouch( 4 + row ) on the original, its own zone on the Angulo,
 * and has selected the row: _usersSel). On the picker the account's PIN entry, which refuses a locked account (enterPinFor( )); on the list,
 * the account's editor. */
inline __attribute__((always_inline)) void DisplayManager::actUserOpen( bool picking ) {
	if (picking) enterPinFor(_usersMap[_usersSel]);
	else         showUserEdit(_usersMap[_usersSel], false);
}

/* Nova conta on the users list (the caller holds acceptTouch( 13 ), or the Angulo's zone): only a session that may manage accounts
 * (PERM_USER_MGR; Core 0 checks again when the account is saved). A new account is named first, on the text keyboard, with no panel bits. */
inline __attribute__((always_inline)) void DisplayManager::actUserNew( ) {
	if (!(_panelPerms & PERM_USER_MGR)) { _errorSoundPending = true; return; }
	/* new account: name first, on the text keyboard */
	_userEditPerms = 0;
	_newUserName[0] = '\0';
	showSettingsPassword(1);
}

/* A row of the sensor menu, run (the caller holds its zone): each row behind its permission bit, which Core 0 checks again. Row 0 opens the
 * limits editor, row 2 the maintenance window, row 1 asks Core 0 to flip the sensor's alarms. */
inline __attribute__((always_inline)) void DisplayManager::actSensorMenuActivate( int row ) {
	const uint16_t need[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
	if (!(_panelPerms & need[row])) { _errorSoundPending = true; return; }
	const int slot = _sensorMenuIdx;
	if (row == 0) { showAlarmEdit(slot); return; }
	if (row == 1) {
		/* the flip is Core 0's: it checks the bit again, saves, logs
		 * and puts the record on the alarm line, then repaints us */
		UiEvent ev; ev.type = UiEvent::EVT_ALARM_BLOCK; ev.id = slot;
		ev.param = _sysConfigPtr->sensors[slot].alarmsActive ? 0 : 1;
		pushUiEvent(ev);
		return;
	}
	showMaintEntry(slot);
}

/* A row of the account editor, run (the caller holds acceptTouch( 4 + row ) on the original, its own zone on the Angulo). Rows 0-2 flip a
 * panel bit of the copy being edited, refused on the admin's account, whose bits are locked (lockedBits: slot 0, not new); row 3 opens the
 * account's PIN entry; row 4 asks before the delete, the mode stored under _stateMutex. The original has no row 4 for the admin (4 rows) nor
 * rows 3-4 for a new account (3 rows); the Angulo guards its Excluir on the admin itself (UI_DRIVERS.md 2.13, D3). */
inline __attribute__((always_inline)) void DisplayManager::actUserEditActivate( int idx, bool lockedBits ) {
	const uint16_t bits[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
	if (idx < 3) {
		if (lockedBits) { _errorSoundPending = true; return; }
		_userEditPerms ^= bits[idx];
		_repaintSettings = true;
		return;
	}
	if (idx == 3) { showPinEntry(PIN_FOR_USER, _userEditSlot); return; }
	/* delete: confirm first */
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_SETTINGS_USER_CONFIRM_DEL;
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
}

/* Salvar on the account editor (the caller holds acceptTouch( 13 ) on the original, its own zone on the Angulo). A new account goes on to
 * its PIN entry; the admin's account, whose bits are locked, only goes back to the list; any other asks Core 0 to store its panel bits
 * (EVT_USER_PERMS: id = the slot, param = the bits; Core 0 checks the grant again, and slot 0 is refused there too). */
inline __attribute__((always_inline)) void DisplayManager::actUserEditSave( bool lockedBits ) {
	if (_userEditNew) { showPinEntry(PIN_FOR_NEW_ACCOUNT); return; }
	if (lockedBits) { showSettingsUsers( ); return; }
	UiEvent ev; ev.type = UiEvent::EVT_USER_PERMS; ev.id = _userEditSlot;
	ev.param = (int)(_userEditPerms & PERM_PANEL_ALARM_ANY);
	pushUiEvent(ev);
}

/* The result screen's one button on the password keyboard (the caller holds acceptTouch( 0 ) on the original, Z_TXT_RESULTADO on the Angulo). A saved password
 * goes back to the settings menu; a refusal (too short, mismatch) empties both buffers and starts the first pass again. */
inline __attribute__((always_inline)) void DisplayManager::actPwdResult( ) {
	if (_kbPhase == 3) {
		showSettingsMain( );
	} else {
		_kbPhase = 0;
		_kbCursor = 0;
		_kbShowRaw = false;
		memset(_kbBuffer, 0, sizeof(_kbBuffer));
		memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
		_forceSettingsRedraw = true;
		_repaintSettings = true;
	}
}

/* The X of the password keyboard (the caller holds acceptTouch( 1 ) on the original, Z_TXT_CANCELAR on the Angulo, which resets its own layer first): back to the
 * accounts when the keyboard was asking for a new account's name (_kbPurpose == 1), to the settings menu otherwise. */
inline __attribute__((always_inline)) void DisplayManager::actPwdLeave( ) {
	if (_kbPurpose == 1) showSettingsUsers( ); else showSettingsMain( );
}

/* OK on the password keyboard when it asks for a new account's name (_kbPurpose == 1; the caller holds acceptTouch( 54 ) on the original, Z_TXT_SALVAR on the
 * Angulo, and has checked that there is a name: the original one character, the Angulo UI_DRIVERS.md 2.13, D2's rules). The name goes into _newUserName and the
 * editor of the new account opens; Core 0 applies the real rule (isValidName, no duplicate, not "admin") after both PIN passes. */
inline __attribute__((always_inline)) void DisplayManager::actPwdNameAccept( const char *name ) {
	strncpy(_newUserName, name, sizeof(_newUserName) - 1);
	_newUserName[sizeof(_newUserName) - 1] = '\0';
	showUserEdit(-1, true);
}

/* OK on the legacy password keyboard (`screen pwd`, a purpose other than 1; the caller holds acceptTouch( 54 ) on the original, Z_TXT_SALVAR on the Angulo): two
 * passes of at least 4 characters, the second equal to the first. A refusal goes to the result screen (_kbPhase 2); a match goes to it too (_kbPhase 3) and asks
 * Core 0 to set the SESSION user's PIN (EVT_SAVE_PASSWORD, id and param 0; UI_DRIVERS.md 2.13, D6). */
inline __attribute__((always_inline)) void DisplayManager::actPwdLegacyOk( ) {
	if (_kbPhase == 0) {
		if (_kbCursor < 4) {
			_kbPhase = 2;
			_kbMsgKey = TR_PWD_TOO_SHORT;
			_forceSettingsRedraw = true;
			_repaintSettings = true;
		} else {
			_kbPhase = 1;
			_kbCursor = 0;
			_kbShowRaw = false;
			memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
			_repaintSettings = true;
		}
	}
	else if (_kbPhase == 1) {
		if (_kbCursor < 4) {
			_kbPhase = 2;
			_kbMsgKey = TR_PWD_TOO_SHORT;
			_forceSettingsRedraw = true;
			_repaintSettings = true;
		}
		else if (strcmp(_kbBuffer, _kbConfirmBuf) != 0) {
			_kbPhase = 2;
			_kbMsgKey = TR_PWD_MISMATCH;
			_forceSettingsRedraw = true;
			_repaintSettings = true;
		}
		else {
			_kbPhase = 3;
			_kbMsgKey = TR_PWD_SAVED;
			_forceSettingsRedraw = true;
			_repaintSettings = true;

			UiEvent ev;
			ev.type = UiEvent::EVT_SAVE_PASSWORD;
			ev.id = 0;
			ev.param = 0;
			pushUiEvent(ev);
		}
	}
}

/* The password boxes (the caller holds acceptTouch( 2 ) on the original, Z_TXT_VER on the Angulo): show the typed text or hide it again. */
inline __attribute__((always_inline)) void DisplayManager::actPwdToggleShow( ) {
	_kbShowRaw = !_kbShowRaw;
	_repaintSettings = true;
}

/* One typed character into the password keyboard's active buffer, at the cursor, and the terminator after it (the popup's key and the space on the original; the keys and the
 * space on the Angulo). The caller holds the zone (0x60 + the key for a popup key, 20 for the space on the original) and the room test, `_kbCursor < kbMaxLen( )`, which stays with
 * it: the original puts it after `hit >= 0` in one condition and reads the character (popupChar( )) only once it passed. */
inline __attribute__((always_inline)) void DisplayManager::actPwdAppend( char *buf, char c ) {
	buf[_kbCursor++] = c;
	buf[_kbCursor] = '\0';
}

/* The backspace of the password keyboard's grid (zone 21 on the original, the held TXT_APAGAR on the Angulo): the last character of the active buffer goes. The caller holds the
 * test that there is one, `_kbCursor > 0`. */
inline __attribute__((always_inline)) void DisplayManager::actPwdBackspace( char *buf ) {
	buf[--_kbCursor] = '\0';
}
