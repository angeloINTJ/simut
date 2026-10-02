/**
 * @file DisplayManager_Ota.cpp
 * @brief The firmware-update screen on the TFT (OtaScreen.h).
 * @details Sub-file of DisplayManager.cpp, TFT only (tools/features.toml
 * leaves it out of the alpha and the headless builds). DisplayManager.cpp
 * publishes the phase; this file draws it, from Core 1 on its pass or from
 * Core 0 while the stage holds Core 1, and ends the screens that report an
 * ending.
 *
 * What each phase shows, in the 4 px safe area and on the 4 px grid:
 *   receiving   status, the bar and its percentage, "do not switch off"
 *   checking    status, the bar full, "do not switch off"
 *   ready       status, the bar full, "do not switch off"
 *   installing  status, "restarts by itself in about 30 s", "do not switch off"
 *   refused     status, the reason, "still on the current version"
 *   cut         status, "still on the current version"
 * A text too wide for the screen goes on two lines (otaBreak); the longest
 * ones, measured in the two fonts for the three languages, need two at most.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "FlashIrqProbe.h" /* C1_PHASE */
#include "UiWidgets.h"

/* The status ends at y 113 even on two lines, and the hint starts at 211; in
 * between, the bar and its percentage, each repainted in a box of its own
 * when the percentage changes (drawOtaScreen). */
static constexpr int16_t OTA_TEXT_W = 296;       /* x 12-307 */
static constexpr int16_t OTA_STATUS_Y = 96;      /* baseline of one line; two sit at 84 and 108 */
static constexpr int16_t OTA_DETAIL_Y = 156;     /* the same, at 144 and 168 */
static constexpr int16_t OTA_LINE_H = 24;
static constexpr int16_t OTA_BAR_X = 40, OTA_BAR_Y = 128, OTA_BAR_W = 240, OTA_BAR_H = 24;
static constexpr int16_t OTA_PCT_Y = 180;
static constexpr int16_t OTA_PCT_BOX_X = 128, OTA_PCT_BOX_Y = 160, OTA_PCT_BOX_W = 64, OTA_PCT_BOX_H = 24;
static constexpr int16_t OTA_KEPT_Y = 204;       /* under a reason on two lines */
static constexpr int16_t OTA_HINT_Y = 224;

static_assert(TR_OTA_WHY_DAMAGED - TR_OTA_WHY_MODEL == OTA_WHY_DAMAGED - OTA_WHY_MODEL,
              "one string per reason, in OtaWhy's order");

/* One block of text, copied out of tr( ) — whose scratch rotates — and split
 * in place where otaBreak says, so the strips only draw it. */
struct OtaText {
	char buf[96];
	const char* second;
};

static void otaPrepare(GFXcanvas16* cv, OtaText& t, const char* s, const GFXfont* font) {
	safeCopy(t.buf, s ? s : "", sizeof(t.buf));
	t.second = nullptr;
	cv->setFont(font);
	cv->setTextSize(1);
	const size_t at = otaBreak(t.buf, OTA_TEXT_W, [cv](const char* p, size_t n) -> int32_t {
		char part[sizeof(OtaText::buf)];
		if (n >= sizeof(part)) n = sizeof(part) - 1;
		memcpy(part, p, n);
		part[n] = '\0';
		int16_t bx, by; uint16_t bw, bh;
		cv->getTextBounds(part, 0, 0, &bx, &by, &bw, &bh);
		return (int32_t)bw;
	});
	if (at) { t.buf[at] = '\0'; t.second = t.buf + at + 1; }
}

static void otaCentred(GFXcanvas16* cv, const char* s, int16_t baseline) {
	int16_t bx, by; uint16_t bw, bh;
	cv->getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
	cv->setCursor((320 - (int16_t)bw) / 2 - bx, baseline);
	cv->print(s);
}

/* One line on its baseline y1, two around it, OTA_LINE_H apart. */
static void otaDraw(GFXcanvas16* cv, const OtaText& t, const GFXfont* font, uint16_t color,
	int16_t y1, int16_t yOff) {
	if (!t.buf[0]) return;
	cv->setFont(font);
	cv->setTextSize(1);
	cv->setTextColor(color);
	if (!t.second) { otaCentred(cv, t.buf, y1 + yOff); return; }
	otaCentred(cv, t.buf, y1 - OTA_LINE_H / 2 + yOff);
	otaCentred(cv, t.second, y1 + OTA_LINE_H / 2 + yOff);
}

/* The bar, its top-left corner at (x, y). */
static void otaBar(GFXcanvas16* cv, int16_t x, int16_t y, uint8_t pct) {
	cv->drawRoundRect(x, y, OTA_BAR_W, OTA_BAR_H, 6, C_TEXT_SUB);
	const int16_t fill = otaBarFill(pct, OTA_BAR_W - 4);
	if (fill > 0) cv->fillRoundRect(x + 2, y + 2, fill, OTA_BAR_H - 4, 4, C_ACCENT);
}

/* The percentage, centred on the screen, drawn x0 to the left of where it sits. */
static void otaPercentText(GFXcanvas16* cv, uint8_t pct, int16_t x0, int16_t baseline) {
	char t[8];
	snprintf(t, sizeof(t), "%u%%", (unsigned)pct);
	cv->setFont(&simutFont12pt);
	cv->setTextSize(1);
	cv->setTextColor(C_TEXT_MAIN);
	int16_t bx, by; uint16_t bw, bh;
	cv->getTextBounds(t, 0, 0, &bx, &by, &bw, &bh);
	cv->setCursor((320 - (int16_t)bw) / 2 - bx - x0, baseline);
	cv->print(t);
}

/* Core 0 paints only while Core 1 is held where it cannot be inside an SPI
 * transfer: parked at the top of its loop by a flash pause (the stage's, for
 * the whole upload), or reset by one — a pause's fallback when Core 1 never
 * parked. A pause that froze Core 1 elsewhere is neither, and then nothing is
 * drawn until Core 1 runs again. Core 0 is the only core that pauses, so a
 * pause it started has finished its handshake by the time it gets here. */
bool DisplayManager::otaPaintFromCore0( ) {
	if (!_driver.tft || !_driver.canvas) return false;
	if (__atomic_load_n(&_pauseRefCount, __ATOMIC_ACQUIRE) <= 0) return false;
	if (!__atomic_load_n(&_core1Parked, __ATOMIC_ACQUIRE) &&
	    !__atomic_load_n(&_core1HardReset, __ATOMIC_ACQUIRE)) return false;
	drawOtaScreen( );
	return true;
}

/* Core 1, once a pass, before the screen dispatch. While a phase is published
 * the update screen has the panel, whatever screen Core 0 or a touch put up
 * meanwhile; the boot screen of a reboot outranks it. */
void DisplayManager::otaTick( ) {
	const uint32_t st = __atomic_load_n(&_otaState, __ATOMIC_ACQUIRE);
	const OtaPhase ph = otaPhaseOf(st);
	if (ph == OTA_PH_NONE) {
		if (_uiMode == MODE_OTA_UPDATE) forceDashboard( );
		return;
	}
	if (_sharedState.isBooting) return;
	if (_uiMode != MODE_OTA_UPDATE) {
		mutex_enter_blocking(&_stateMutex);
		_uiMode = MODE_OTA_UPDATE;
		mutex_exit(&_stateMutex);
		_otaRedrawAll = true;
	}
	const uint32_t hold = otaHoldMs(ph);
	if (hold && timeSince(__atomic_load_n(&_otaShownMs, __ATOMIC_ACQUIRE), hold)) {
		if (otaEnd(st)) forceDashboard( );
		return;
	}
	C1_PHASE(C1P_UI_SETTINGS);
	drawOtaScreen( );
}

void DisplayManager::drawOtaScreen( ) {
	GFXcanvas16* const cv = _driver.canvas;
	if (!cv || !_driver.tft) return;
	const uint32_t st = __atomic_load_n(&_otaState, __ATOMIC_ACQUIRE);
	const OtaPhase ph = otaPhaseOf(st);
	if (ph == OTA_PH_NONE) return;
	const uint8_t pct = __atomic_load_n(&_otaPct, __ATOMIC_ACQUIRE);
	const bool full = _otaRedrawAll || otaSeqOf(st) != _otaDrawnSeq;
	if (!full) {
		/* A new percentage: the part of the fill that grew and the number,
		 * each composed at the canvas origin and pushed as its own small
		 * rectangle — the same pixels the full render puts there. Measured
		 * from Core 0 during the upload on the rig (2026-10-02): repainting
		 * the two 40-px strips around them took 22 ms a step, 2.2 s of a 34 s
		 * stage; the whole bar and the number, 8.4 ms; this, 3.0 ms. */
		if (ph != OTA_PH_RECEIVING || pct == _otaPctDrawn) return;
		const int16_t was = otaBarFill(_otaPctDrawn, OTA_BAR_W - 4);
		const int16_t now = otaBarFill(pct, OTA_BAR_W - 4);
		if (now > was && was < 8) {
			/* Under 8 px fillRoundRect still shrinks the radius of both ends,
			 * and the left corners are where the outline's arc reaches inside
			 * the fill's box (42,130 and 42,149): the whole bar again. */
			cv->fillRect(0, 0, OTA_BAR_W, OTA_BAR_H, C_BG_MAIN);
			otaBar(cv, 0, 0, pct);
			blitCanvas(cv, OTA_BAR_X, OTA_BAR_Y, OTA_BAR_W, OTA_BAR_H);
		} else if (now > was) {
			/* From 4 px before the old end, whose rounded corners become full
			 * columns. The right arc reaches in at x 277; at 99 % the fill
			 * ends at 274. */
			const int16_t from = (int16_t)(was - 4);
			cv->fillRect(0, 0, now - from, OTA_BAR_H - 4, C_BG_MAIN);
			cv->fillRoundRect(-from, 0, now, OTA_BAR_H - 4, 4, C_ACCENT);
			blitCanvas(cv, OTA_BAR_X + 2 + from, OTA_BAR_Y + 2, now - from, OTA_BAR_H - 4);
		}
		cv->fillRect(0, 0, OTA_PCT_BOX_W, OTA_PCT_BOX_H, C_BG_MAIN);
		otaPercentText(cv, pct, OTA_PCT_BOX_X, OTA_PCT_Y - OTA_PCT_BOX_Y);
		blitCanvas(cv, OTA_PCT_BOX_X, OTA_PCT_BOX_Y, OTA_PCT_BOX_W, OTA_PCT_BOX_H);
		_otaPctDrawn = pct;
		return;
	}

	const bool ended = (ph == OTA_PH_REFUSED || ph == OTA_PH_CUT);
	const bool bar = (ph == OTA_PH_RECEIVING || ph == OTA_PH_CHECKING || ph == OTA_PH_READY);
	OtaText title, status, detail, kept, hint;
	title.buf[0] = status.buf[0] = detail.buf[0] = kept.buf[0] = hint.buf[0] = '\0';
	title.second = status.second = detail.second = kept.second = hint.second = nullptr;
	static const LangKey STATUS[] = { TR_KEYS_COUNT, TR_OTA_RECEIVING, TR_OTA_CHECKING,
	                                  TR_OTA_READY, TR_OTA_INSTALLING, TR_OTA_REFUSED, TR_OTA_CUT };
	safeCopy(title.buf, tr(TR_OTA_TITLE), sizeof(title.buf));
	otaPrepare(cv, status, tr(STATUS[ph]), &simutFont12pt);
	if (ph == OTA_PH_INSTALLING) otaPrepare(cv, detail, tr(TR_OTA_RESTARTS), &simutFont9pt);
	if (ph == OTA_PH_REFUSED) {
		const OtaWhy why = otaWhyOf(st);
		if (why >= OTA_WHY_MODEL && why <= OTA_WHY_DAMAGED)
			otaPrepare(cv, detail, tr((LangKey)(TR_OTA_WHY_MODEL + (why - OTA_WHY_MODEL))), &simutFont9pt);
		otaPrepare(cv, kept, tr(TR_OTA_KEPT), &simutFont9pt);
	}
	if (ph == OTA_PH_CUT) otaPrepare(cv, detail, tr(TR_OTA_KEPT), &simutFont9pt);
	if (!ended) otaPrepare(cv, hint, tr(TR_OTA_KEEP_ON), &simutFont9pt);

	for (int strip = 0; strip < 6; strip++) {
		cv->fillScreen(C_BG_MAIN);
		const int16_t yOff = -strip * RENDER_STRIP_H;
		uiTitleBar(cv, 4 + yOff, title.buf);
		otaDraw(cv, status, &simutFont12pt, ended ? C_TEMP_WARM : C_TEXT_MAIN, OTA_STATUS_Y, yOff);
		otaDraw(cv, detail, &simutFont9pt, ph == OTA_PH_REFUSED ? C_TEXT_MAIN : C_TEXT_SUB,
		        OTA_DETAIL_Y, yOff);
		otaDraw(cv, kept, &simutFont9pt, C_TEXT_SUB, OTA_KEPT_Y, yOff);
		otaDraw(cv, hint, &simutFont9pt, C_TEMP_WARM, OTA_HINT_Y, yOff);
		if (bar) otaBar(cv, OTA_BAR_X, OTA_BAR_Y + yOff, ph == OTA_PH_RECEIVING ? pct : 100);
		if (ph == OTA_PH_RECEIVING) otaPercentText(cv, pct, 0, OTA_PCT_Y + yOff);
		commitScreenStrip(strip);
	}
	_otaPctDrawn = pct;
	_otaRedrawAll = false;
	__atomic_store_n(&_otaDrawnSeq, otaSeqOf(st), __ATOMIC_RELEASE);
}
