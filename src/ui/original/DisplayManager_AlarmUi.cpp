/**
 * @file DisplayManager_AlarmUi.cpp
 * @brief The alarm screen's draws: the original share of DisplayManager_Alarm.cpp, moved word for word in S10c D5 (docs/analysis/UI_DRIVERS.md).
 *
 * drawAlarmAction( ), slotAlarmBg( ) and redrawAlarmFlash( ) are called only by the original UI (UiOriginal.h and the Dashboard's draws).
 * The Ângulo does not draw the original's alarm screen, so this unit is listed in [ui.original].units (tools/features.toml) and the two
 * Ângulo images do not compile it; the LCD builds exclude it with the rest of the TFT's draws. The alarm state these functions read (the
 * masks, the silence, isSlotAlarming( )) stays in DisplayManager_Alarm.cpp, which is core.
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"

/* drawAlarmAction via strips (header + 3 buttons).
 * "Draw everything with offset per strip" pattern — each strip draws ALL
 * elements on the canvas (320x40), with appropriate Y offset per strip.
 * Adafruit_GFX auto-clips pixels outside the canvas -> each strip
 * shows only its corresponding portion. ~5x more CPU vs single-pass but eliminates
 * top-down visual tearing (~80ms total render). */
void DisplayManager::drawAlarmAction( ) {
	if (!_driver.canvas) return;
	if (!_forceSettingsRedraw) return;
	_forceSettingsRedraw = false;

	/* Header text — computed once, used in all strips. */
	char headerBuf[40];
	mutex_enter_blocking(&_stateMutex);
	char friendlyName[32];
	safeCopy(friendlyName, _sharedState.slotName, sizeof(friendlyName));
	mutex_exit(&_stateMutex);
	if (strlen(friendlyName) > 0) {
		snprintf(headerBuf, sizeof(headerBuf), "! %s", friendlyName);
	} else {
		snprintf(headerBuf, sizeof(headerBuf), "! Slot %d", _alarmActionSlot);
	}
	String silTxt = tr(TR_SILENCE_120S);
	String deactTxt = tr(TR_DEACTIVATE);
	String mmTxt = tr(TR_MINMAX);

	const int btnX = 20, btnW = 280, btnH = 45, btnR = 10;

	GFXcanvas16* cv = beginScreenRender( );
	if (!cv) return;

	int16_t bx, by; uint16_t bw, bh;

	for (int strip = 0; strip < 6; strip++) {
		cv->fillScreen(C_BG_MAIN);
		const int16_t yOff = -strip * RENDER_STRIP_H;

		/* Header (y_screen=4..52) — visible in strips 0 and 1 */
		cv->fillRect(4, 4 + yOff, 312, 48, C_ALARM_BG);
		cv->setFont(&simutFont12pt);
		cv->setTextColor(C_ALARM_TEXT);
		cv->getTextBounds(headerBuf, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((320 - bw) / 2, 32 + yOff);
		cv->print(headerBuf);

		/* Silence button (y_screen=60..105) — visible in strips 1 and 2.
		 * cautionBg é laranja-escuro: usa o branco do erro (C_ALARM_ERR_TEXT),
		 * não o alarmText (que virou PRETO para o fundo amarelo). */
		cv->fillRoundRect(btnX, 60 + yOff, btnW, btnH, btnR, C_CAUTION_BG);
		cv->setFont(&simutFont12pt);
		cv->setTextColor(C_ALARM_ERR_TEXT);
		cv->getTextBounds(silTxt, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor(btnX + (btnW - bw) / 2, 90 + yOff);
		cv->print(silTxt);

		/* Deactivate button (y_screen=115..160) — visible in strips 2 and 3 */
		cv->fillRoundRect(btnX, 115 + yOff, btnW, btnH, btnR, C_ALARM_BG);
		cv->getTextBounds(deactTxt, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor(btnX + (btnW - bw) / 2, 145 + yOff);
		cv->print(deactTxt);

		/* Main menu button (y_screen=170..215) — visible in strips 4 and 5 */
		cv->fillRoundRect(btnX, 170 + yOff, btnW, btnH, btnR, C_ACCENT);
		cv->setTextColor(C_BG_MAIN);
		cv->getTextBounds(mmTxt, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor(btnX + (btnW - bw) / 2, 200 + yOff);
		cv->print(mmTxt);

		commitScreenStrip(strip);
	}
	endScreenRender( );
}

uint16_t DisplayManager::slotAlarmBg(int slotIdx) const {
	if (_alarmSilenced) return C_CARD_BG;

	/* Erro de sensor tem prioridade visual: âmbar brilhante no lugar do
	 * vermelho de limite. Um slot não está nos dois ao mesmo tempo —
	 * inErrorState sobrepõe os limites. */
	if (isSlotErrAlarming(slotIdx)) {
		return _alarmFlashPhase ? C_ALARM_ERR_BG : C_CARD_BG;
	}
	if (!isSlotAlarming(slotIdx)) return C_CARD_BG;
	return _alarmFlashPhase ? C_ALARM_BG : C_CARD_BG;
}

void DisplayManager::redrawAlarmFlash( ) {
	if (!_driver.tft || !_driver.canvas) return;

	if (isSlotAlarming(_lastRenderedState.topSlotIdx) ||
	    isSlotErrAlarming(_lastRenderedState.topSlotIdx)) {
		drawSlotPanel(_lastRenderedState.topSlotTemp, _lastRenderedState.topSlotHum,
		                 _lastRenderedState.topSlotType, _lastRenderedState.topSlotValid,
		                 _lastRenderedState.topSlotIdx, _lastRenderedState.topSlotName, true, _topPanel);
	}

	int sel = _lastRenderedState.selectedSlotIdx;
	if (isSlotAlarming(sel) || isSlotErrAlarming(sel)) {
		drawSlotPanel(_lastRenderedState.slotTemp, _lastRenderedState.slotHum, _lastRenderedState.slotType, _lastRenderedState.slotValid,
		              sel, _lastRenderedState.slotName, true, _bottomPanel);
	}


	bool pageHasAlarm = false;
	bool otherPageHasAlarm = false;
	for (int i = 0; i < 10; i++) {
		if (!isSlotAlarming(i) && !isSlotErrAlarming(i)) continue;
		int slotPage = (i < 4) ? 0 : (i < 8) ? 1 : 2;
		if (slotPage == _currentPage) pageHasAlarm = true;
		else otherPageHasAlarm = true;
	}
	if (pageHasAlarm || otherPageHasAlarm) {
		drawBottomButtons(sel);
	}
}
