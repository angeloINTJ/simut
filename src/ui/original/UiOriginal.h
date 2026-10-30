/**
 * @file UiOriginal.h
 * @brief The original UI driver's hooks, the panel of the Pico W release and of pico2_w_original: their definitions, as inline members
 *        of DisplayManager. With UI_HOOK (src/ui/UiDriver.h) they are always_inline, so the code of a hook is
 *        the code it replaced. Included by ui/UiDriverHooks.h, last, from each translation unit that calls
 *        a hook or a core tick.
 *
 * Stage S4 of docs/analysis/UI_DRIVERS.md adds the hooks one commit at a time; each is defined below.
 */
#pragma once

#include "../../DisplayManager.h"
#include "../../DisplayManager_Fonts.h"   /* simutFont12pt */
#include "../../FlashIrqProbe.h"         /* C1_PHASE */
#include "../../DisplayManager_Ticks.h"   /* the core's ticks, which the pass below calls */
#include "../../simut_config.h"          /* SIMUT_TFT_GRAPH and SIMUT_NET_WIFI, tested below */

/* The update screen. The hook is the one call the two callers made, and not drawOtaScreen( )'s body: that body is
 * long and would be copied into both. */
UI_HOOK void DisplayManager::uiPaintOta( ) {
	drawOtaScreen( );
}

/* Core 1, once, after the panel is initialised: clear the glass, and draw the fixed background unless the boot
 * terminal has the panel. */
UI_HOOK void DisplayManager::uiBegin( ) {
	fastClearScreen(C_BG_MAIN);
	if (!_sharedState.isBooting) drawInterfaceFixed( );
}

/* The core says what changed; the original draws it, as the core did before. `c` is a constant at each call, so the
 * switch is the one call it names. */
UI_HOOK void DisplayManager::uiChanged( UiChange c, int16_t arg ) {
	switch (c) {
	case UiChange::ALARM_FLASH:   redrawAlarmFlash( ); break;
	case UiChange::ALARM_RESTORE: restoreNormalDashboard( ); break;
	case UiChange::BOTTOM_PANEL: {
		/* The block handleTouch( ) ran where a tap toggled the bottom card's min/max, moved here as it was. */
		SystemState snap;
		mutex_enter_blocking(&_stateMutex);
		snap = _sharedState;
		mutex_exit(&_stateMutex);
		drawSlotPanel(snap.slotTemp, snap.slotHum, snap.slotType, snap.slotValid,
		              snap.selectedSlotIdx, snap.slotName, true, _bottomPanel);
		break;
	}
	case UiChange::BOTTOM_BUTTONS: drawBottomButtons( arg ); break;
	/* The graph's two immediate draws (stage S7): the header with the sensor's name, and the thin line that says a read has been asked of Core 0.
	 * Both functions are declared in every build and defined only where the graph is, and no call site raises these kinds in a build without it, so
	 * the switch folds there as it does here. */
	case UiChange::GRAPH_HEADER: drawGraphHeaderBar( ); break;
	case UiChange::GRAPH_BUSY:   drawGraphBusyHint( ); break;
	/* The touch calibration was entered, on Core 0 (showTouchCalibration( ), before the mode is published): the draw caches of its two screens go back to
	 * "nothing painted", the seven stores that function held until S10c D3. Nothing is drawn here: the glass is Core 1's. */
	case UiChange::TOUCH_CAL_ENTER: resetTouchCalCaches( ); break;
	/* The display offset screen was entered, on Core 0 and INSIDE _stateMutex (showSettingsDisplayOffset( )): the sentinels that force the redraw of the numeric values, the two stores that
	 * function held until S10c D3b. Written here, not in a member: only drawSettingsDisplayOffset( ) reads them. Nothing is drawn, and the mutex is not taken: it is held. */
	case UiChange::DISPLAY_OFFSET_ENTER: _lastOffsetDrawX = 99; _lastOffsetDrawY = 99; break;
	}
}

/* The top card is to be shown from `snap`: the card, then the bottom buttons. The two calls redrawTopPanel( ) made, moved
 * here as they were; redrawTopPanel( ) keeps the sync and the mirror, which are the core's. */
UI_HOOK void DisplayManager::uiTopPanel( const SystemState &snap ) {
	drawSlotPanel(snap.topSlotTemp, snap.topSlotHum, snap.topSlotType, snap.topSlotValid,
	              snap.topSlotIdx, snap.topSlotName, true, _topPanel, snap.topSlotPres);
	/* Redraw bottom buttons — fixed sensor button may appear/disappear */
	drawBottomButtons(snap.selectedSlotIdx);
}

/* A touch while the device boots with the skip button showing: did it land on the button? The rectangle handleTouch( ) tested
 * inline, moved here as it was. */
UI_HOOK bool DisplayManager::uiBootSkipHit( int16_t x, int16_t y ) {
	return y > 190 && x > 80 && x < 240;
}

/* A touch on the sensitivity screen, with enough pressure or injected (p.z >= 50 || simNow, main's #280): did it land on Cancelar? The rectangle handleTouch( ) tested inline, moved here as it was. */
UI_HOOK bool DisplayManager::uiSensCancelHit( int16_t x, int16_t y ) {
	return y > 195 && x < 125;
}

/* A touch on the calibration's verdict: did it land on the button that closes it? The band handleTouch( ) tested inline, moved here as it was (the x is not part of it). */
UI_HOOK bool DisplayManager::uiCalVerdictHit( int16_t, int16_t y ) {
	return y >= 185;
}

/* The theme changed: say "applying theme", then repaint the dashboard in full with the new palette. The block
 * loopCore1( ) ran inline, moved here as it was. */
UI_HOOK void DisplayManager::uiThemeChanged( ) {
	C1_PHASE(C1P_THEME_MUTEX);
	SystemState snap;
	mutex_enter_blocking(&_stateMutex);
	snap = _sharedState;
	mutex_exit(&_stateMutex);

	if (!snap.isBooting) {
		fastClearScreen(C_BG_MAIN);
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
		/* Mesmo guarda do render(): espelha só se interativo — topo fixado
		 * em sensor de erro (inválido) mantém o próprio painel. */
		if (!_topPanel.fixed && !snap.topSlotValid) {
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
		fastClearScreen(C_BG_MAIN);
		_lastRenderedState.isBooting = false;

		mutex_enter_blocking(&_stateMutex);
		_isDirty = true;
		mutex_exit(&_stateMutex);
	}
}

/* The screen chain of Core 1's pass, moved from loopCore1( ) as it was: the dashboard branch (the core's timers, then the
 * snapshot and render( )), the graph screens, the header-name timer and one branch per settings mode. */
UI_HOOK void DisplayManager::uiPass( SystemState &currentSnapshot ) {
if (_uiMode == MODE_DASHBOARD) {

	tickDashboard( );


	tickAlarmFlash( );


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
#if SIMUT_TFT_GRAPH
else if (_uiMode == MODE_GRAPH_LOADING) {
	if (_repaintLoading) { C1_PHASE(C1P_UI_GRAPH); drawLoadingScreen( ); _repaintLoading = false; }
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
#endif

C1_PHASE(C1P_LOOP_TAIL);

/* Revert header to date/time after 3s of showing the name */
#if SIMUT_TFT_GRAPH
if ((_uiMode == MODE_GRAPH_VIEW || _uiMode == MODE_GRAPH_DETAIL)
    && _headerShowName
    && timeSince(_headerNameTimer, 3000))
{
	_headerShowName = false;
	drawGraphHeaderBar( );
}
else
#endif
if (_uiMode == MODE_SETTINGS_THEMES) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawSettingsThemes( ); }
}
else if (_uiMode == MODE_SETTINGS_ALARMS) {
	if (_repaintSettings) {
		_repaintSettings = false;
		C1_PHASE(C1P_UI_SETTINGS);
		/* A flag that flipped in place repaints its own word; anything
		 * else goes through the full path. */
		if (_alarmStatusDirty && !_forceSettingsRedraw) drawAlarmStatusOnly( );
		else drawSettingsAlarms( );
		_alarmStatusDirty = false;
	}
}
else if (_uiMode == MODE_SETTINGS_ALARM_EDIT) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawAlarmEdit( ); }
}
else if (_uiMode == MODE_AUTH) {
	tickAuthLockout( );
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawPinScreen( ); }
}
else if (_uiMode == MODE_AUTH_USER) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawAuthUser( ); }
}
else if (_uiMode == MODE_SETTINGS_PIN_POLICY) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawPinPolicy( ); }
}
else if (_uiMode == MODE_SETTINGS_MAIN) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawSettingsMain( ); }
}
else if (_uiMode == MODE_SETTINGS_LANG) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawSettingsLang( ); }
}
else if (_uiMode == MODE_SETTINGS_PASSWORD) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawSettingsPassword( ); }
}
else if (_uiMode == MODE_SETTINGS_TOUCH_CAL) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawTouchCalibration( ); }
}
else if (_uiMode == MODE_SETTINGS_TOUCH_SENS) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawTouchSensitivity( ); }
	tickTouchSens( );
}
else if (_uiMode == MODE_SETTINGS_SOUNDS) {

	if (_repaintSettings) {
		_repaintSettings = false;
		if (_inMelodySelect) drawMelodySelect( );
		else drawSettingsSounds( );
	}
}
else if (_uiMode == MODE_SETTINGS_STATUS) {
	/* Renders every 1 second or when forced */
	if (_repaintSettings || timeSince(_statusLastDraw, 1000)) {
		_repaintSettings = false;
		drawSystemStatus( );
	}
}
else if (_uiMode == MODE_SETTINGS_LICENSE) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawSettingsLicense( ); }
}
else if (_uiMode == MODE_SETTINGS_DISPLAY_OFFSET) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawSettingsDisplayOffset( ); }
}
else if (_uiMode == MODE_ALARM_ACTION) {

	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawAlarmAction( ); }
}
/* v24 — identity at the panel (DisplayManager_UsersUi.cpp) */
else if (_uiMode == MODE_SETTINGS_ALARM_SENSOR) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawAlarmSensorMenu( ); }
}
else if (_uiMode == MODE_SETTINGS_MAINT) {
	/* an open window counts down on screen: once a second, like the status screen */
	if (_repaintSettings || timeSince(_maintLastDraw, 1000)) {
		_repaintSettings = false;
		C1_PHASE(C1P_UI_SETTINGS); drawMaintEntry( ); _maintLastDraw = millis( );
	}
}
else if (_uiMode == MODE_SETTINGS_USERS) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawSettingsUsers( ); }
}
else if (_uiMode == MODE_SETTINGS_USER_EDIT) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawUserEdit( ); }
}
else if (_uiMode == MODE_SETTINGS_USER_CONFIRM_DEL) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawUserConfirmDel( ); }
}
else if (_uiMode == MODE_PANEL_MESSAGE) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawPanelMessage( ); }
}
else if (_uiMode == MODE_CONFIRM_MUTE_ALL) {

	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawMuteConfirm( ); }
}
#if SIMUT_NET_WIFI
else if (_uiMode == MODE_CONFIRM_AP) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawApConfirm( ); }
}
#endif
else if (_uiMode == MODE_SET_CLOCK) {
	if (_repaintSettings) { _repaintSettings = false; C1_PHASE(C1P_UI_SETTINGS); drawClockEntry( ); }
}
}
