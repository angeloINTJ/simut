/**
 * @file DisplayManager_Render.cpp
 * @brief The text truncation helpers and render( ) with the boot paint: the original share of DisplayManager.cpp, moved word for word in S10a (docs/analysis/UI_DRIVERS.md).
 */

#include "DisplayManager.h"
#include "simut_config.h"   /* SIMUT_NET_WIFI is tested below: included here, not left to another header */
#include "LogManager.h"
#include "UiWidgets.h"
#include "FlashIrqProbe.h" /* Core-1 exposure flags for the flash probe */
#include "DisplayManager_Fonts.h"
#include "DisplayManager_FmtFloat.h"
#include "HelpLicenseEN.h" /* LICENSE_TEXT_EN inline in PROGMEM */
#include "TextWrap.h"
#include "Utf8Fold.h"
#include <LittleFS.h>
#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include "hardware/irq.h"   /* Core-1 private wait: exclusive alarm handler */
#include "hardware/structs/io_bank0.h" /* PROC1_INTE: is the touch interrupt armed */
#include "hardware/timer.h" /* hardware_alarm_claim_unused, hardware_alarm_get_irq_num */
#include <stdio.h>
#include <stdlib.h>
#include "pico/multicore.h"
#include "hardware/structs/scb.h"
#include "ui/UiDriverHooks.h"   /* the UI driver's hook definitions and the core's ticks: after every other include, they need the complete class */

/**
 * @brief Truncates text to fit within maxPixelW pixels in the current GFX font.
 *
 * If the original text already fits, it is copied in full to out.
 * Otherwise, removes characters from the end and appends "..." so
 * the result fits within the maximum width. The font must already be set
 * in the GFX context before calling.
 */
void DisplayManager::truncateText(Adafruit_GFX* gfx, const char* src,
                                   char* out, size_t outSize, int16_t maxPixelW) {
	if (!gfx || !src || !out || outSize < 4) {
		if (out && outSize > 0) out[0] = '\0';
		return;
	}

	/* Measure original text width */
	int16_t bx, by;
	uint16_t tw, th;
	gfx->getTextBounds(src, 0, 0, &bx, &by, &tw, &th);

	/* If it fits entirely, copy and return */
	if ((int16_t)tw <= maxPixelW) {
		strncpy(out, src, outSize - 1);
		out[outSize - 1] = '\0';
		return;
	}

	/* Measure ellipsis width */
	uint16_t ellW, ellH;
	gfx->getTextBounds("...", 0, 0, &bx, &by, &ellW, &ellH);
	int16_t targetW = maxPixelW - (int16_t)ellW;
	if (targetW < 0) targetW = 0;

	/* Binary search for maximum length that fits */
	int srcLen = (int)strlen(src);
	int lo = 0, hi = srcLen;
	int best = 0;

	while (lo <= hi) {
		int mid = (lo + hi) / 2;

		/* Build candidate string in buffer */
		int copyLen = mid;
		if (copyLen > (int)(outSize - 4)) copyLen = (int)(outSize - 4);
		memcpy(out, src, copyLen);
		out[copyLen] = '\0';

		gfx->getTextBounds(out, 0, 0, &bx, &by, &tw, &th);

		if ((int16_t)tw <= targetW) {
			best = copyLen;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}

	/* Remove trailing spaces before ellipsis */
	while (best > 0 && out[best - 1] == ' ') best--;

	/* Build final result */
	memcpy(out, src, best);
	out[best] = '.';
	out[best + 1] = '.';
	out[best + 2] = '.';
	out[best + 3] = '\0';
}

/**
 * @brief Same as truncateText, but a suffix is reserved before the name
 *        competes for what is left.
 * @details truncateText shortens the whole string, so "Temperature MAX" loses
 *          the MAX first — the one part of an alarm-limit label that must never
 *          disappear, because it is what distinguishes the two rows of a
 *          channel. Here the suffix width is subtracted up front and only the
 *          name is searched, giving "Conduct... MAX" instead of "Conductivit...".
 *          A suffix that does not fit on its own is written alone.
 */
void DisplayManager::truncateTextKeepSuffix(Adafruit_GFX* gfx, const char* name,
                                             const char* suffix, char* out,
                                             size_t outSize, int16_t maxPixelW) {
	if (!gfx || !name || !suffix || !out || outSize < 5) {
		if (out && outSize > 0) out[0] = '\0';
		return;
	}

	const size_t sufLen = strlen(suffix);
	if (sufLen + 5 > outSize) { out[0] = '\0'; return; }

	int16_t bx, by;
	uint16_t tw, th;

	/* Fits whole? Then no ellipsis at all. */
	size_t nameLen = strlen(name);
	if (nameLen + sufLen < outSize) {
		memcpy(out, name, nameLen);
		memcpy(out + nameLen, suffix, sufLen + 1);
		gfx->getTextBounds(out, 0, 0, &bx, &by, &tw, &th);
		if ((int16_t)tw <= maxPixelW) return;
	}

	/* Budget for the name = width minus the suffix and the ellipsis. Both are
	 * measured in the context's current font, not assumed. */
	uint16_t sufW, ellW, dummyH;
	gfx->getTextBounds(suffix, 0, 0, &bx, &by, &sufW, &dummyH);
	gfx->getTextBounds("...", 0, 0, &bx, &by, &ellW, &dummyH);
	int16_t targetW = maxPixelW - (int16_t)sufW - (int16_t)ellW;
	if (targetW < 0) targetW = 0;

	const int maxName = (int)(outSize - sufLen - 4);
	int lo = 0, hi = (int)nameLen;
	int best = 0;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int copyLen = mid;
		if (copyLen > maxName) copyLen = maxName;
		memcpy(out, name, copyLen);
		out[copyLen] = '\0';
		gfx->getTextBounds(out, 0, 0, &bx, &by, &tw, &th);
		if ((int16_t)tw <= targetW) { best = copyLen; lo = mid + 1; }
		else { hi = mid - 1; }
	}

	while (best > 0 && out[best - 1] == ' ') best--;

	memcpy(out, name, best);
	out[best] = '.';
	out[best + 1] = '.';
	out[best + 2] = '.';
	memcpy(out + best + 3, suffix, sufLen + 1);
}

void DisplayManager::render(const SystemState& state) {
 SystemState st = state;
 /* Espelho do painel inferior para o topo SÓ quando o topo é interativo
 * (segue a seleção). O espelho por topSlotValid=false era o bug: um topo
 * FIXADO num sensor em erro (sensor ausente → inválido) tem topSlotValid
 * falso e o render substituía o painel do erro pelos dados do painel
 * inferior — "trazendo o sensor de baixo para cima". Com o topo fixado,
 * o painel superior mantém os próprios dados (mesmo inválidos). */
 if (!_topPanel.fixed && !st.topSlotValid) {
 st.topSlotTemp = st.slotTemp; st.topSlotHum = st.slotHum;
 st.topSlotType = st.slotType; st.topSlotValid = st.slotValid;
 safeCopy(st.topSlotName, st.slotName, 31);
 st.topSlotIdx = st.selectedSlotIdx;
 }
	if (state.isBooting) {
		C1_PHASE(C1P_R_BOOT);
		/* _langChanged forces fullRedraw to retranslate bootLogs already
		 * shown in EN before .lng loaded. */
		bool langJustChanged = _langChanged;
		if (langJustChanged) _langChanged = false;
		bool fullRedraw = (_lastRenderedState.isBooting == false) ||
		                  (_lastRenderedState.apProgressPct != state.apProgressPct) ||
		                  langJustChanged;
		if (state.apProgressPct >= 0) {
			/* Until 2.7.1 this branch was unreachable: the AP-hold window ran
			 * before startCore1( ), so nothing rendered it. Now that it does
			 * run, the redraw condition above is true on EVERY step — the
			 * percentage is what changed — and clearing 320x240 plus
			 * re-stamping the caption twenty times a second is a 3-second
			 * flicker. Only the bar moves, so only the bar is painted after
			 * the first frame. */
			const bool enterAp = fullRedraw || (_lastRenderedState.apProgressPct < 0);
			if (enterAp) {
				fastClearScreen(C_BG_MAIN);
				_driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_TEXT_MAIN);
				_driver.tft->setCursor(55, 120); _driver.tft->print(tr(TR_AP_MODE));
				_driver.tft->drawRoundRect(40, 140, 240, 20, 6, C_TEXT_SUB);
			}
			int wBar = map(state.apProgressPct, 0, 100, 0, 236);
			if (wBar > 0) {
				_driver.tft->fillRoundRect(42, 142, wBar, 16, 4, C_ACCENT);
			}
			_lastRenderedState = state;
			_bootPaintSeq++;
			return;
		}

		int boxY = 105;

		if (fullRedraw) {
			fastClearScreen(C_BG_MAIN);
			_driver.tft->setFont(&simutFont24pt); _driver.tft->setTextColor(C_TEXT_MAIN);
			int16_t x1, y1; uint16_t w, h;
			_driver.tft->getTextBounds("SIMUT", 0, 0, &x1, &y1, &w, &h);
			_driver.tft->setCursor((320 - w) / 2, 60); _driver.tft->print("SIMUT");
			_driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_ACCENT);
			_driver.tft->getTextBounds(SIMUT_VERSION, 0, 0, &x1, &y1, &w, &h);
			_driver.tft->setCursor((320 - w) / 2, 85); _driver.tft->print(SIMUT_VERSION);

			_driver.tft->fillRoundRect(10, boxY, 300, 80, 8, C_CARD_BG);
			_driver.tft->drawRoundRect(10, boxY, 300, 80, 8, C_TEXT_OFF);
			_driver.tft->setFont(NULL);
			_driver.tft->setTextSize(1);
			_driver.tft->setTextColor(C_ACCENT_HIGH, C_CARD_BG);
			_driver.tft->setCursor(20, boxY + 8);
			_driver.tft->print("> system_init( ) ");
		}
		/* Per-line diff: only repaints lines that changed. Boot logs are
		 * BootLogEntry (key + suffix). In render, we resolve tr(key) +
		 * suffix. Comparison includes key, suffix AND active language
		 * (langJustChanged -> fullRedraw). */
		_driver.tft->setFont(NULL);
		_driver.tft->setTextSize(1);
		_driver.tft->setTextColor(C_TEXT_SUB, C_CARD_BG);
		for(int i=0; i<5; i++) {
			const BootLogEntry& cur = state.bootLogs[i];
			const BootLogEntry& prev = _lastRenderedState.bootLogs[i];
			if (!fullRedraw && cur.key == prev.key &&
			    strncmp(cur.suffix, prev.suffix, sizeof(cur.suffix)) == 0) continue;
			_driver.tft->setCursor(20, boxY + 22 + (i*10));
			/* T1.2: fixed buffer on the Core-1 render path (was 2-3 heap
			 * allocations per changed line). Pad to 46 columns preserved
			 * so shorter lines still overwrite older, longer ones.
			 *
			 * This box draws with setFont(NULL) — the built-in 5x7 font,
			 * which is NOT Latin-1: byte 0xE7 is a math symbol there, not
			 * 'c-cedilla'. tr() hands back Latin-1 (the TFT fonts carry it),
			 * so every accented boot line rendered as tau/theta/pi. Fold to
			 * ASCII the way the license screen already does.
			 *
			 * The fold has to come BEFORE the padding, too: an accent is 2
			 * bytes in and 1 out, so padding measured on the pre-fold length
			 * left the tail of a longer previous line on screen. */
			char logRaw[96];
			char logLine[48];
			if (cur.key >= 0 && cur.key < (int16_t)TR_KEYS_COUNT) {
				snprintf(logRaw, sizeof(logRaw), "%s%s",
				         tr((LangKey)cur.key), cur.suffix);
			} else {
				snprintf(logRaw, sizeof(logRaw), "%s", cur.suffix); /* raw legacy */
			}
			unaccent(logRaw, logLine, sizeof(logLine));
			size_t llen = strlen(logLine);
			while (llen < 46 && llen < sizeof(logLine) - 1) logLine[llen++] = ' ';
			logLine[llen] = '\0';
			_driver.tft->print(logLine);
		}

		/* Skip button: paint only on off->on transition. Idempotent otherwise. */
		bool skipOn = state.showSkipButton;
		bool wasOn = _lastRenderedState.showSkipButton;
		if (skipOn && (fullRedraw || !wasOn)) {
			_driver.tft->fillRoundRect(80, 195, 160, 35, 8, C_ACCENT_HIGH);
			_driver.tft->setFont(&simutFont9pt); _driver.tft->setTextColor(C_BG_MAIN);
			int16_t x1, y1; uint16_t w, h;
			const char* skipLabel = tr(TR_SKIP);
			_driver.tft->getTextBounds(skipLabel, 0, 0, &x1, &y1, &w, &h);
			_driver.tft->setCursor(80 + (160 - w)/2, 218); _driver.tft->print(skipLabel);
		} else if (!skipOn && (fullRedraw || wasOn)) {
			_driver.tft->fillRect(80, 195, 160, 35, C_BG_MAIN);
		}

		_lastRenderedState = state;
		_bootPaintSeq++;
		return;
	}
	if (_lastRenderedState.isBooting && !state.isBooting) {


		_forceFullRedraw = true;
	}

	bool full = _forceFullRedraw;
	if (full) {
		C1_PHASE(C1P_R_FULL);
		drawInterfaceFixed( );
		drawTopBar(state);


		drawSlotPanel(st.topSlotTemp, st.topSlotHum, st.topSlotType, st.topSlotValid, st.topSlotIdx, st.topSlotName, true, _topPanel, st.topSlotPres);
		drawSlotPanel(st.slotTemp, st.slotHum, st.slotType, st.slotValid, st.selectedSlotIdx, st.slotName, true, _bottomPanel, st.slotPres);
		drawBottomButtons(state.selectedSlotIdx);
		_forceFullRedraw = false;
		_lastRenderedState = state;
		return;
	}

	/* Barrier before reading _pktArrowState (published by Core 0
	 * along with flash vars in setTelemetrySendStatus). */
	__dmb( );
	if (state.wifiRssi != _lastRenderedState.wifiRssi ||
	    state.btActive != _lastRenderedState.btActive ||
	    strcmp(state.timeString, _lastRenderedState.timeString) != 0 ||
	    _webNotifyStartMs > 0 ||
	    _alarmSilenced ||
	    _pktArrowState == 3) {
		C1_PHASE(C1P_R_TOPBAR);
		drawTopBar(state);
	}

	/* Sync topSlot* from slot* whenever both panels show the same sensor */
	if (st.topSlotIdx == st.selectedSlotIdx) {
	 st.topSlotTemp = st.slotTemp; st.topSlotHum = st.slotHum;
	 st.topSlotType = st.slotType; st.topSlotValid = st.slotValid;
	 safeCopy(st.topSlotName, st.slotName, 31);
	}

	if (!_topPanel.showMinMax) {
		if (abs(st.topSlotTemp - _lastRenderedState.topSlotTemp) > 0.01 ||
		    abs(st.topSlotHum - _lastRenderedState.topSlotHum) > 0.01 ||
		    st.topSlotValid != _lastRenderedState.topSlotValid ||
		    st.topSlotIdx != _lastRenderedState.topSlotIdx) {
			C1_PHASE(C1P_R_TOP_PANEL);
			drawSlotPanel(st.topSlotTemp, st.topSlotHum, st.topSlotType, st.topSlotValid, st.topSlotIdx, st.topSlotName, true, _topPanel, st.topSlotPres);
		}
	}

	/* Return panels to normal mode after 30s without touch (minMaxIdleDue) */
	if (minMaxIdleDue( )) {
		C1_PHASE(C1P_R_MINMAX);
		if (_topPanel.showMinMax) {
			_topPanel.showMinMax = false;
			drawSlotPanel(st.topSlotTemp, st.topSlotHum, st.topSlotType, st.topSlotValid, st.topSlotIdx, st.topSlotName, true, _topPanel, st.topSlotPres);
		}
		if (_bottomPanel.showMinMax) {
			_bottomPanel.showMinMax = false;
			drawSlotPanel(st.slotTemp, st.slotHum, st.slotType, st.slotValid,
			              st.selectedSlotIdx, st.slotName, true, _bottomPanel, st.slotPres);
		}
	}

	bool slotChanged = (state.selectedSlotIdx != _lastRenderedState.selectedSlotIdx);
	bool bNameChanged = (strcmp(st.slotName, _lastRenderedState.slotName) != 0);
	bool bTempChanged = (abs(st.slotTemp - _lastRenderedState.slotTemp) > 0.01) || (st.slotValid != _lastRenderedState.slotValid);

	if (slotChanged || bNameChanged || (!_bottomPanel.showMinMax && bTempChanged)) {
		C1_PHASE(C1P_R_BOT_PANEL);
		if (slotChanged) {
			drawBottomButtons(state.selectedSlotIdx);
		}

		drawSlotPanel(st.slotTemp, st.slotHum, st.slotType, st.slotValid, st.selectedSlotIdx, st.slotName, (slotChanged || bNameChanged), _bottomPanel, st.slotPres);
	}

	/* Detect alarm state change and redraw buttons + panels */
	if (_alarmSlotMask != _prevAlarmSlotMask) {
		C1_PHASE(C1P_R_ALARM);
		drawBottomButtons(state.selectedSlotIdx);
		if (!_topPanel.showMinMax) {
			drawSlotPanel(st.topSlotTemp, st.topSlotHum, st.topSlotType, st.topSlotValid, st.topSlotIdx, st.topSlotName, true, _topPanel, st.topSlotPres);
		}
		if (!_bottomPanel.showMinMax) {
			drawSlotPanel(st.slotTemp, st.slotHum, st.slotType, st.slotValid,
			              st.selectedSlotIdx, st.slotName, true, _bottomPanel, st.slotPres);
		}
		_prevAlarmSlotMask = _alarmSlotMask;
	}

	_lastRenderedState = state;
}
