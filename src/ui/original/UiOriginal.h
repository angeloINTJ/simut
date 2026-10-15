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
