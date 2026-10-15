/**
 * @file UiOriginal.h
 * @brief The original UI driver's hooks, the panel every release ships: their definitions, as inline members
 *        of DisplayManager. With UI_HOOK (src/ui/UiDriver.h) they are always_inline, so the code of a hook is
 *        the code it replaced. Included by ui/UiDriverHooks.h, last, from the two translation units that call
 *        a hook.
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
	(void)arg;
	switch (c) {
	case UiChange::ALARM_FLASH:   redrawAlarmFlash( ); break;
	case UiChange::ALARM_RESTORE: restoreNormalDashboard( ); break;
	}
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
/* v24 — identity at the panel (DisplayManager_Users.cpp) */
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
