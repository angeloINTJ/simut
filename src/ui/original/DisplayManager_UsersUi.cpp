/**
 * @file DisplayManager_UsersUi.cpp
 * @brief The user and PIN screens' draws and the panel's touch hand-off: the original share of DisplayManager_Users.cpp, moved word for word in S10c D6a (docs/analysis/UI_DRIVERS.md section 2.18).
 *
 * The PIN keypad (drawPinScreen( ) and its cards), the alarm-sensor menu, the maintenance entry, the user list, editor and delete
 * confirmation, the panel message and the PIN policy screen are drawn only by the original UI (ui/original/UiOriginal.h), and
 * handleTouchPanelV24( ), which maps a tap on them to a UiEvent for Core 0, is called only from its touch chain (ui/original/UiTouchChain.h).
 * The Ângulo draws these screens itself (ui/angulo/DisplayManager_UiA_Identidade.cpp and its other identity and settings screens), so this
 * unit is listed in [ui.original].units (tools/features.toml): the two Ângulo images do not compile it, and a dependency they gain on one of
 * these draws is a link error. The LCD builds exclude it too (it is TFT-only, so it has no #if frame). The PIN state and its fail ladder,
 * the session and the show*( ) calls that open each screen stay in DisplayManager_Users.cpp, which is core: both UIs run them.
 *
 * The move changes the products' compiled code, not only its place: with the callers of PinKb::orderedFace, clampPinPolicy and maxLenFor in
 * two units, GCC inlines these helpers into some of their callers. The commit that moved this unit (S10c D6a) lists the functions and the
 * byte deltas; the decoded argument that the behaviour is unchanged is in docs/analysis/UI_DRIVERS.md section 2.18.
 */
#include "DisplayManager.h"
#include "ui/original/PinKeypadLayout.h"   /* the keypad cards, the popup card, rows and hit test, the footer's zones and the dots: the original's alone, they left PinKeypad.h in S10c, C8 and S12, A3 (keyAt stays there); early, where PinKeypad.h defined them */
#include "DisplayManager_Fonts.h"
#include "UiWidgets.h"
#include "SystemDefs_Validate.h"     /* PIN_MIN_LEN / PIN_MAX_LEN */
#include "StorageManager.h"          /* userHasPin */
#include "display/CountInText.h"     /* the minimum inside the too-short line */
/* sensors/SensorChannelTable.h is not included here: no line of the user and PIN screens reads the channel table (it was dead before S10c D6a too, see DisplayManager_Users.cpp). Dropped in the docs step of S10c (2026-10-28): the eleven images stayed sha256-equal. */
#include "ui/UiDriverHooks.h"        /* the hook definitions Actions.h leans on (its redrawTopPanel( ) calls uiTopPanel( )): they need the complete class, so after the includes above; this unit is TFT-only by the LCD builds' exclusion lists (tools/features.toml), not by an #if, so the LCD images never parse them */
#include "DisplayManager_Actions.h"  /* the core's touch actions, which handleTouchPanelV24( ) runs; last, they lean on the hooks above */
#include "DisplayManager_PinActions.h"   /* the PIN keypad's: this unit is TFT-only, so a panel proves a PIN here */

/* One card, into `cv` at its screen position shifted by `oy` (the canvas is a
 * 40-px strip of the screen, so everything outside clips away). */
void DisplayManager::drawPinCardInto(GFXcanvas16* cv, int key, int16_t ox, int16_t oy) {
	using namespace PinKb;
	const Grid g = pinGrid( );
	if (!cv || key < 0 || key >= (int)g.keys) return;
	int16_t bx, by; uint16_t bw, bh;
	const int16_t x = (int16_t)(keyX(g, key) + ox), y = (int16_t)(keyY(g, key) + oy);

	cv->fillRoundRect(x, y, g.w, g.h, 8, C_CARD_BG);
	cv->drawRoundRect(x, y, g.w, g.h, 8, C_TEXT_SUB);
	/* The 12 pt face is the one the four-card layout was drawn with. The
	 * denser grids put three glyphs in 74 px (ALPHA_ALNUM, KB_SET3) or two in
	 * 49, so they step down to 9 pt — at 12 pt the glyphs would touch. */
	cv->setFont((g.w / (g.slots ? g.slots : 1) >= 50) ? &simutFont12pt : &simutFont9pt);
	/* One ink for everything on the card. A decoy drawn dimmer would tell a
	 * watcher which glyphs can be part of a PIN, and on the identification
	 * keypad that is exactly the thing the card is hiding. */
	cv->setTextColor(C_TEXT_MAIN);
	const int16_t sw = slotW(g);
	for (int sl = 0; sl < (int)g.slots; sl++) {
		const char c = _pinKeyChars[key][sl];
		if (!c) continue;
		const char str[2] = { c, '\0' };
		cv->getTextBounds(str, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((int16_t)(x + 1 + sl * sw + (sw - (int16_t)bw) / 2 - bx),
		              (int16_t)(y + (g.h - (int16_t)bh) / 2 - by));
		cv->print(str);
	}
}

/* The four cards alone, one canvas blit each. This is the per-tap path: the
 * deal is rolled after every tap, and repainting the whole screen for that
 * would be six blits where four will do — and each card is written in one
 * blit, so there is no moment where a card is half old and half new. */
void DisplayManager::blitPinCards( ) {
	using namespace PinKb;
	if (!_driver.canvas) return;
	const Grid g = pinGrid( );
	for (int k = 0; k < (int)g.keys; k++) {
		const int16_t kx = keyX(g, k), ky = keyY(g, k);
		_driver.canvas->fillScreen(C_BG_MAIN);
		drawPinCardInto(_driver.canvas, k, (int16_t)(-kx), (int16_t)(-ky));
		blitCanvas(_driver.canvas, kx, ky, g.w, g.h);
	}
}

/* The ORDERED keyboard: characters where the operator expects them. Two
 * screens use it — CHOOSING a PIN, always, and IDENTIFYING under KB_PLAIN,
 * because a set of one hides nothing and a shuffle would only cost taps.
 * It draws whatever pinGrid( ) says, from the same _pinKeyChars a dealt
 * layout fills, so there is one renderer and not two. */
void DisplayManager::drawPinPadInto(GFXcanvas16* cv, int16_t oy) {
	using namespace PinKb;
	if (!cv) return;
	int16_t bx, by; uint16_t bw, bh;
	const Grid g = pinGrid( );
	const uint8_t alphabet = pinAlphabet( );
	char label[FACE_MAX + 1];
	cv->setFont(&simutFont12pt);
	cv->setTextColor(C_TEXT_MAIN);
	for (int k = 0; k < (int)g.keys; k++) {
		orderedLabel(alphabet, k, label, sizeof(label));
		if (!label[0]) continue;      /* the numeric pad's two empty cells */
		const int16_t x = keyX(g, k), y = (int16_t)(keyY(g, k) + oy);
		cv->fillRoundRect(x, y, g.w, g.h, 8, C_CARD_BG);
		cv->drawRoundRect(x, y, g.w, g.h, 8, C_TEXT_SUB);
		cv->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((int16_t)(x + (g.w - (int16_t)bw) / 2 - bx),
		              (int16_t)(y + (g.h - (int16_t)bh) / 2 - by));
		cv->print(label);
	}
}

/* The second tap of the ALPHA_ALNUM keyboard: the characters of the group
 * that was chosen, on a card over the keys. Drawn AFTER the keyboard in the
 * same strip sweep, so it covers rather than competes. A tap anywhere off a
 * key closes it, which is the rule PasswordKeyboard.h's popups already use. */
void DisplayManager::drawPinPopupInto(GFXcanvas16* cv, int16_t oy) {
	using namespace PinKb;
	if (!cv || _pinPopup < 0 || _pinPopup >= GROUP_COUNT) return;
	const char* mem = GROUP_CHARS[_pinPopup];
	const int n = (int)strlen(mem);
	int16_t bx, by; uint16_t bw, bh;
	const int16_t cy = (int16_t)(popupCardY(n) + oy);
	cv->fillRoundRect(POP_CARD_X, cy, POP_CARD_W, popupCardH(n), 10, C_CARD_BG);
	cv->drawRoundRect(POP_CARD_X, cy, POP_CARD_W, popupCardH(n), 10, C_ACCENT);
	cv->setFont(&simutFont12pt);
	cv->setTextColor(C_TEXT_MAIN);
	int idx = 0;
	for (int r = 0; r < popupRows(n); r++) {
		const int m = popupRowKeys(n, r);
		const int16_t rx = popupRowX0(m), ry = (int16_t)(popupRowY(n, r) + oy);
		for (int i = 0; i < m; i++, idx++) {
			const int16_t x = (int16_t)(rx + i * (POP_KEY_W + POP_GAP));
			cv->fillRoundRect(x, ry, POP_KEY_W, POP_KEY_H, 8, C_BG_MAIN);
			cv->drawRoundRect(x, ry, POP_KEY_W, POP_KEY_H, 8, C_TEXT_SUB);
			const char str[2] = { mem[idx], '\0' };
			cv->getTextBounds(str, 0, 0, &bx, &by, &bw, &bh);
			cv->setCursor((int16_t)(x + (POP_KEY_W - (int16_t)bw) / 2 - bx),
			              (int16_t)(ry + (POP_KEY_H - (int16_t)bh) / 2 - by));
			cv->print(str);
		}
	}
}

/* The dots row, or the message that replaced it. Same offset convention. */
void DisplayManager::drawPinDotsInto(GFXcanvas16* cv, int16_t oy) {
	using namespace PinKb;
	if (!cv) return;
	int16_t bx, by; uint16_t bw, bh;
	const int16_t y = (int16_t)(DOTS_Y + oy);

	if (_pinMsg != TR_KEYS_COUNT || _pinWaiting) {
		const bool err = (_pinMsg != TR_KEYS_COUNT);
		cv->setFont(&simutFont9pt);
		cv->setTextColor(err ? C_TEMP_HOT : C_TEXT_SUB);
		String m = tr(err ? _pinMsg : TR_LOADING);
		if (_pinMsg == TR_PIN_TOO_SHORT) {
			/* The line says where its minimum goes ("{n}"); it used to carry a
			 * 4 that no policy could change. */
			char line[64];
			countInText(m.c_str( ), pinEntryMinLen( ), line, sizeof(line));
			m = line;
		}
		cv->getTextBounds(m, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((320 - bw) / 2, (int16_t)(y + 19));
		cv->print(m);
		return;
	}
	/* The row is as wide as the shortest entry this screen takes, or as what
	 * has been typed past it: the policy's minimum to choose a PIN, the
	 * absolute floor to prove one, which a PIN set under an older policy may
	 * be. Sixteen dots at the old 20-px pitch is 320 px, one wider than the
	 * safe area allows (measured 2026-09-20), so both pitch and radius step
	 * down past fourteen. */
	const int floorLen = (int)pinEntryMinLen( );
	const int n = (_pinLen > floorLen) ? _pinLen : floorLen;
	const int spacing = dotSpacing(n);
	const int16_t r = dotRadius(n);
	const int x0 = (320 - n * spacing) / 2 + spacing / 2;
	for (int i = 0; i < n; i++) {
		const int cx = x0 + i * spacing;
		if (i < _pinLen) cv->fillCircle(cx, (int16_t)(y + DOTS_H / 2), r, C_ACCENT);
		else cv->drawCircle(cx, (int16_t)(y + DOTS_H / 2), r, C_TEXT_SUB);
	}
}

/* A full redraw paints the whole screen in six 40-px strips, top to bottom,
 * one DMA blit each. It used to clear the screen and then blit the title, the
 * footer and the four cards one at a time, and the panel was visibly dark
 * between the clear and the last blit — the maintainer saw it flicker on every
 * entry (2026-09-19). A strip sweep never shows a hole: each row is written
 * once, already composed.
 *
 * Afterwards only the dots row repaints, once per tap, and it is its own
 * 320x28 blit that no card overlaps. */
void DisplayManager::drawPinScreen( ) {
	using namespace PinKb;
	if (!_driver.canvas) return;
	int16_t bx, by; uint16_t bw, bh;

	const bool locked = _permanentLockout || (_lockoutUntil > 0 && !timeReached(_lockoutUntil));

	if (_forceSettingsRedraw) {
		GFXcanvas16* cv = beginScreenRender( );
		if (!cv) return;
		const uint16_t bg = _permanentLockout ? C_TEMP_HOT : C_BG_MAIN;
		LangKey titleKey = TR_AUTH_TITLE;
		if (_pinPurpose != PIN_FOR_AUTH) titleKey = (_pinPhase == 0) ? TR_NEW_PIN : TR_CONFIRM_PIN;

		for (int strip = 0; strip < 6; strip++) {
			const int16_t oy = (int16_t)(-strip * 40);
			cv->fillScreen(bg);
			if (strip == 0) uiTitleBar(cv, 4, tr(titleKey));

			if (_permanentLockout) {
				cv->setFont(&simutFont12pt); cv->setTextColor(C_BG_MAIN);
				String m1 = tr(TR_ACCESS_BLOCKED);
				cv->getTextBounds(m1, 0, 0, &bx, &by, &bw, &bh);
				cv->setCursor((320 - bw) / 2, (int16_t)(110 + oy)); cv->print(m1);
				cv->setFont(&simutFont9pt);
				String m2 = tr(TR_REBOOT_REQ);
				cv->getTextBounds(m2, 0, 0, &bx, &by, &bw, &bh);
				cv->setCursor((320 - bw) / 2, (int16_t)(145 + oy)); cv->print(m2);
			} else {
				drawPinDotsInto(cv, oy);
				if (!locked) {
					if (pinOrderedNow( )) {
						drawPinPadInto(cv, oy);
						drawPinPopupInto(cv, oy);
					} else {
						const Grid g = pinGrid( );
						for (int k = 0; k < (int)g.keys; k++) drawPinCardInto(cv, k, 0, oy);
					}
				}
			}

			/* The footer of every other screen: backspace over the two
			 * nav-arrow slots, then BACK and ENTER where they always are.
			 * BACK answers under a lockout — it is the way out of the screen. */
			if (!locked) {
				const int16_t fy = (int16_t)(FOOT_Y + oy);
				cv->fillRoundRect(FOOT_BACK_X, fy, FOOT_BACK_W, FOOT_H, 10, C_BAR_BG);
				cv->drawRoundRect(FOOT_BACK_X, fy, FOOT_BACK_W, FOOT_H, 10, C_TEXT_SUB);
				const int16_t cx = (int16_t)(FOOT_BACK_X + FOOT_BACK_W / 2);
				const int16_t cy = (int16_t)(fy + FOOT_H / 2);
				cv->fillTriangle((int16_t)(cx - 12), cy, (int16_t)(cx - 4), (int16_t)(cy - 8),
				                 (int16_t)(cx - 4), (int16_t)(cy + 8), C_TEXT_MAIN);
				cv->fillRect((int16_t)(cx - 4), (int16_t)(cy - 5), 15, 10, C_TEXT_MAIN);
			}
			uiButton(cv, FOOT_EXIT_X, (int16_t)(FOOT_Y + oy), FOOT_EXIT_W, FOOT_H,
			         tr(TR_BACK), UI_BTN_SECONDARY);
			if (!locked)
				uiButton(cv, FOOT_OK_X, (int16_t)(FOOT_Y + oy), FOOT_OK_W, FOOT_H,
				         tr(TR_ENTER), UI_BTN_PRIMARY);
			commitScreenStrip((int16_t)strip);
		}
		endScreenRender( );
		_forceSettingsRedraw = false;
		if (_permanentLockout) return;
	}

	/* Timed lockout countdown, repainted once a second (the render loop sets
	 * _repaintSettings while the lockout runs). */
	if (locked && !_permanentLockout) {
		static long lastSec = -1;
		long secondsLeft = (long)(timeRemaining(_lockoutUntil) / 1000) + 1;
		if (secondsLeft != lastSec) {
			lastSec = secondsLeft;
			_driver.canvas->fillScreen(C_BG_MAIN);
			_driver.canvas->setFont(&simutFont12pt); _driver.canvas->setTextColor(C_TEMP_WARM);
			String t1 = tr(TR_ATTEMPTS_EXCEEDED);
			_driver.canvas->getTextBounds(t1, 0, 0, &bx, &by, &bw, &bh);
			_driver.canvas->setCursor((320 - bw) / 2, 25); _driver.canvas->print(t1);
			blitCanvas(_driver.canvas, 0, 90, 320, 45);
			_driver.canvas->fillScreen(C_BG_MAIN);
			char timeStr[64];
			snprintf(timeStr, sizeof(timeStr), tr(TR_WAIT_SECONDS), secondsLeft);
			_driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(C_TEXT_SUB);
			_driver.canvas->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
			_driver.canvas->setCursor((320 - bw) / 2, 25); _driver.canvas->print(timeStr);
			blitCanvas(_driver.canvas, 0, 135, 320, 45);
		}
		return;
	}

	if (_permanentLockout) return;

	/* The dots row alone: one blit, and no card is inside it. */
	_driver.canvas->fillScreen(C_BG_MAIN);
	drawPinDotsInto(_driver.canvas, (int16_t)(-DOTS_Y));
	blitCanvas(_driver.canvas, 0, DOTS_Y, 320, DOTS_H);

	/* Cards only. An ordered keyboard never gets here: blitPinCards( ) spreads
	 * g.slots glyphs across each key, which would paint one character where a
	 * ten-character group key is. */
	if (_pinCardsDirty) { _pinCardsDirty = false; if (!pinOrderedNow( )) blitPinCards( ); }
}

/* Minutes left in a slot's maintenance window, 0 when closed. Uses the
 * wall clock the same way Core 0 does; the panel only displays it. */
static uint32_t maintMinutesLeft(const SystemConfig* cfg, int slot) {
	if (!cfg || slot < 0 || slot >= MAX_SENSORS) return 0;
	const uint32_t now = (uint32_t)time(nullptr);
	const uint32_t until = cfg->maint.until[slot];
	if (until <= now) return 0;
	return (until - now + 59u) / 60u;
}

static void fmtHoursMinutes(char* dst, size_t cap, uint32_t minutes) {
	snprintf(dst, cap, "%luh%02lu", (unsigned long)(minutes / 60u), (unsigned long)(minutes % 60u));
}

void DisplayManager::drawAlarmSensorMenu( ) {
	if (!_driver.canvas || !_sysConfigPtr || _sensorMenuIdx < 0) return;
	const SensorRecord& rec = _sysConfigPtr->sensors[_sensorMenuIdx];
	const bool fullRedraw = _forceSettingsRedraw;

	if (fullRedraw) {
		fastClearScreen(C_BG_MAIN);
		blitTitleBar(rec.friendlyName[0] ? rec.friendlyName : tr(TR_ALARMS_TITLE));
		/* footer: arrows + a wide BACK, like the alarms list */
		_driver.canvas->fillScreen(C_BG_MAIN);
		uiNavArrow(_driver.canvas, 5, 0, 62, 40, UI_UP);
		uiNavArrow(_driver.canvas, 73, 0, 62, 40, UI_DOWN);
		uiButton(_driver.canvas, 141, 0, 75, 40, tr(TR_BACK), UI_BTN_SECONDARY);
		uiButton(_driver.canvas, 222, 0, 93, 40, tr(TR_ENTER), UI_BTN_PRIMARY);
		blitCanvas(_driver.canvas, 0, 195, 320, 45);
	}

	/* Three rows: limits, alarms on/off, maintenance. A row the session's
	 * bits do not cover is drawn dimmed and ignores taps — the operator sees
	 * what exists and learns who to ask, instead of a menu that changes shape
	 * per account. */
	const uint16_t need[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
	const LangKey label[3] = { TR_ROW_LIMITS, TR_ROW_ALARMS, TR_ROW_MAINT };
	const int itemW = 285;
	for (int i = 0; i < 3; i++) {
		const int y = 40 + i * 38;
		const bool allowed = (_panelPerms & need[i]) != 0;
		const bool sel = (i == _sensorMenuSel);
		_driver.canvas->fillScreen(C_BG_MAIN);
		const uint16_t bg = sel ? C_ACCENT : C_CARD_BG;
		const uint16_t txt = sel ? C_BG_MAIN : (allowed ? C_TEXT_MAIN : C_TEXT_OFF);
		_driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
		if (!sel) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
		_driver.canvas->setFont(&simutFont9pt);
		_driver.canvas->setTextColor(txt);
		_driver.canvas->setCursor(10, 24);
		_driver.canvas->print(tr(label[i]));

		/* right side: the state, or a chevron */
		char val[24] = "";
		if (i == 1) {
			strlcpy(val, rec.alarmsActive ? tr(TR_ON) : tr(TR_OFF), sizeof(val));
		} else if (i == 2) {
			const uint32_t left = maintMinutesLeft(_sysConfigPtr, _sensorMenuIdx);
			if (left) fmtHoursMinutes(val, sizeof(val), left);
			else strlcpy(val, tr(TR_OFF), sizeof(val));
		}
		int16_t bx, by; uint16_t bw = 0, bh;
		if (val[0]) {
			_driver.canvas->getTextBounds(val, 0, 0, &bx, &by, &bw, &bh);
			uint16_t vc = txt;
			if (!sel && allowed) {
				if (i == 1) vc = rec.alarmsActive ? C_TEMP_OK : C_TEXT_OFF;
				if (i == 2) vc = maintMinutesLeft(_sysConfigPtr, _sensorMenuIdx) ? C_TEMP_WARM : C_TEXT_OFF;
			}
			_driver.canvas->setTextColor(vc);
			_driver.canvas->setCursor(itemW - 10 - (int)bw, 24);
			_driver.canvas->print(val);
		} else {
			_driver.canvas->fillTriangle(itemW - 20, 11, itemW - 20, 23, itemW - 10, 17, sel ? C_BG_MAIN : C_TEXT_SUB);
			bw = 10;
		}
		if (!allowed) {
			/* a small padlock to the LEFT of the value — over it, it ate the
			 * first letter of "SIM" (rig, 2026-09-19). Drawn on the selected
			 * row as well: the highlight used to hide the one cue that said
			 * why ENTER only beeps. */
			uiMenuIcon(_driver.canvas, (int16_t)(itemW - 10 - (int)bw - 26), 9, 4, sel ? C_BG_MAIN : C_TEXT_OFF);
		}
		blitCanvas(_driver.canvas, 10, y, itemW, 34);
	}
	_forceSettingsRedraw = false;
}

void DisplayManager::drawMaintEntry( ) {
	if (!_driver.canvas || !_sysConfigPtr || _maintSlot < 0) return;
	const SensorRecord& rec = _sysConfigPtr->sensors[_maintSlot];
	const uint32_t left = maintMinutesLeft(_sysConfigPtr, _maintSlot);
	int16_t bx, by; uint16_t bw, bh;

	if (_forceSettingsRedraw) {
		fastClearScreen(C_BG_MAIN);
		char title[48];
		snprintf(title, sizeof(title), "%s", tr(TR_MAINT_TITLE));
		blitTitleBar(title);
		blitFooterMenu(tr(TR_BACK), left ? tr(TR_END_MAINT) : tr(TR_START_LBL));
	}

	if (left) {
		/* An open window: how long is left, and the one thing to do about it
		 * is in the footer. Repainted each second by the render loop. */
		_driver.canvas->fillScreen(C_BG_MAIN);
		_driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(C_TEXT_SUB);
		char nameBuf[40];
		truncateText(_driver.canvas, rec.friendlyName, nameBuf, sizeof(nameBuf), 300);
		_driver.canvas->getTextBounds(nameBuf, 0, 0, &bx, &by, &bw, &bh);
		_driver.canvas->setCursor((320 - bw) / 2, 28); _driver.canvas->print(nameBuf);
		blitCanvas(_driver.canvas, 0, 50, 320, 40);

		_driver.canvas->fillScreen(C_BG_MAIN);
		char hm[16]; fmtHoursMinutes(hm, sizeof(hm), left);
		char line[40]; snprintf(line, sizeof(line), "%s %s", hm, tr(TR_REMAINING));
		_driver.canvas->setFont(&simutFont12pt); _driver.canvas->setTextColor(C_TEMP_WARM);
		_driver.canvas->getTextBounds(line, 0, 0, &bx, &by, &bw, &bh);
		_driver.canvas->setCursor((320 - bw) / 2, 30); _driver.canvas->print(line);
		blitCanvas(_driver.canvas, 0, 100, 320, 45);
		_forceSettingsRedraw = false;
		return;
	}

	/* Two bars, the limit editor's idiom: focused bar in accent, step buttons
	 * at the ends, the value between them. */
	const LangKey lbl[2] = { TR_HOURS, TR_MINUTES };
	for (int i = 0; i < 2; i++) {
		const int y = ALARM_EDIT_Y0 + 20 + i * (ALARM_EDIT_STEP + 10);
		const bool focused = (i == _maintFocus);
		_driver.canvas->fillScreen(C_BG_MAIN);
		const uint16_t bg = focused ? C_ACCENT : C_CARD_BG;
		const uint16_t txt = focused ? C_BG_MAIN : C_TEXT_MAIN;
		_driver.canvas->fillRoundRect(0, 0, ALARM_EDIT_BAR_W, ALARM_EDIT_BAR_H, ALARM_EDIT_BAR_R, bg);
		if (!focused) _driver.canvas->drawRoundRect(0, 0, ALARM_EDIT_BAR_W, ALARM_EDIT_BAR_H, ALARM_EDIT_BAR_R, C_TEXT_SUB);
		const int lx = ALARM_EDIT_INSET;
		const int rx = ALARM_EDIT_BAR_W - ALARM_EDIT_INSET - ALARM_EDIT_BTN_W;
		const uint16_t btnBg = focused ? C_BG_MAIN : C_BAR_BG;
		const uint16_t arrow = focused ? C_ACCENT : C_TEXT_MAIN;
		_driver.canvas->fillRoundRect(lx, ALARM_EDIT_INSET, ALARM_EDIT_BTN_W, ALARM_EDIT_BTN_H, ALARM_EDIT_BTN_R, btnBg);
		_driver.canvas->fillRoundRect(rx, ALARM_EDIT_INSET, ALARM_EDIT_BTN_W, ALARM_EDIT_BTN_H, ALARM_EDIT_BTN_R, btnBg);
		const int cy = ALARM_EDIT_INSET + ALARM_EDIT_BTN_H / 2;
		_driver.canvas->fillTriangle(lx + 8, cy, lx + 18, cy - 7, lx + 18, cy + 7, arrow);
		_driver.canvas->fillTriangle(rx + 18, cy, rx + 8, cy - 7, rx + 8, cy + 7, arrow);
		char numBuf[8];
		snprintf(numBuf, sizeof(numBuf), "%d", (i == 0) ? _maintHours : _maintMinutes);
		_driver.canvas->setFont(&simutFont9pt);
		_driver.canvas->setTextColor(txt);
		_driver.canvas->setCursor(lx + ALARM_EDIT_BTN_W + 10, 23);
		_driver.canvas->print(tr(lbl[i]));
		_driver.canvas->getTextBounds(numBuf, 0, 0, &bx, &by, &bw, &bh);
		_driver.canvas->setCursor(rx - 10 - (int)bw, 23);
		_driver.canvas->print(numBuf);
		blitCanvas(_driver.canvas, ALARM_EDIT_BAR_X, y, ALARM_EDIT_BAR_W, ALARM_EDIT_BAR_H);
	}
	/* the sensor's name under the bars, so the operator knows whose window */
	_driver.canvas->fillScreen(C_BG_MAIN);
	_driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(C_TEXT_SUB);
	char nameBuf[40];
	truncateText(_driver.canvas, rec.friendlyName, nameBuf, sizeof(nameBuf), 300);
	_driver.canvas->getTextBounds(nameBuf, 0, 0, &bx, &by, &bw, &bh);
	_driver.canvas->setCursor((320 - bw) / 2, 22); _driver.canvas->print(nameBuf);
	blitCanvas(_driver.canvas, 0, 158, 320, 34);
	_forceSettingsRedraw = false;
}

/* Three letters for the three panel bits, dim when absent; "ADM" for the
 * built-in admin. Compact enough to sit beside a 15-char name. */
static void drawPermLetters(GFXcanvas16* cv, int16_t right, int16_t baseline,
                            uint16_t perms, bool selected) {
	const char letters[3] = { 'L', 'B', 'M' };
	const uint16_t bits[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
	cv->setFont(&simutFont9pt);
	int16_t x = right - 3 * 14;
	for (int i = 0; i < 3; i++) {
		const bool on = (perms & bits[i]) != 0;
		cv->setTextColor(selected ? C_BG_MAIN : (on ? C_TEMP_OK : C_TEXT_OFF));
		char s[2] = { letters[i], '\0' };
		cv->setCursor(x, baseline);
		cv->print(s);
		x += 14;
	}
}

/* One account list for two screens.
 *
 * The picker (MODE_AUTH_USER) and the editor list (MODE_SETTINGS_USERS) draw
 * the same rows from the same _usersMap; only the right-hand column and the
 * footer differ. They were written twice and the second copy cost 
 * ~700 B of an image that had 5.9 kB left (pico_w_test_https, 2026-09-20),
 * which is a good enough reason on its own — but the better one is that a
 * change to row layout now cannot land on one screen and miss the other. */
/* One row, at (x, y) of whatever canvas it is handed. Pulled out of
 * drawUserList( ) so the strip sweep and the single-row repaint compose the
 * SAME row: the sweep draws it four times at its screen position, the repaint
 * once at the canvas origin. */
void DisplayManager::drawUserRowInto(GFXcanvas16* cv, bool picking, int mapIdx,
                                     int16_t x, int16_t y, int16_t itemW) {
	if (!cv || !_sysConfigPtr || mapIdx < 0 || mapIdx >= _usersCount) return;
	const int slot = _usersMap[mapIdx];
	const UserAccount& u = _sysConfigPtr->users[slot];
	const bool sel = (mapIdx == _usersSel);
	const bool out = picking && (_slotLocked & (1u << (unsigned)slot)) != 0;
	const uint16_t bg = sel ? C_ACCENT : C_CARD_BG;
	const uint16_t txt = sel ? C_BG_MAIN : (out ? C_TEXT_OFF : C_TEXT_MAIN);
	cv->fillRoundRect(x, y, itemW, 34, 8, bg);
	if (!sel) cv->drawRoundRect(x, y, itemW, 34, 8, C_TEXT_SUB);
	cv->setFont(&simutFont9pt);
	cv->setTextColor(txt);
	char nameBuf[24];
	truncateText(cv, u.username, nameBuf, sizeof(nameBuf),
	             itemW - (picking ? 90 : 120));
	cv->setCursor((int16_t)(x + 10), (int16_t)(y + 24));
	cv->print(nameBuf);
	if (picking) {
		/* An account out of tries says so HERE and not after the PIN: that is
		 * the difference between "you typed it wrong" and "this account is
		 * done until the panel reboots". */
		if (out) {
			cv->setTextColor(sel ? C_BG_MAIN : C_TEMP_HOT);
			int16_t bx, by; uint16_t bw, bh;
			cv->getTextBounds(tr(TR_LOCKED_LBL), 0, 0, &bx, &by, &bw, &bh);
			cv->setCursor((int16_t)(x + itemW - 10 - (int16_t)bw), (int16_t)(y + 24));
			cv->print(tr(TR_LOCKED_LBL));
		}
	} else {
		/* a dot after the name says "has a PIN" — an account without one
		 * cannot act here, whatever its bits */
		if (StorageManager::userHasPin(u)) {
			int16_t bx, by; uint16_t bw, bh;
			cv->getTextBounds(nameBuf, 0, 0, &bx, &by, &bw, &bh);
			cv->fillCircle((int16_t)(x + 10 + (int)bw + 8), (int16_t)(y + 19), 3,
			               sel ? C_BG_MAIN : C_ACCENT);
		}
		drawPermLetters(cv, (int16_t)(x + itemW - 10), (int16_t)(y + 24),
		                u.permissions, sel);
	}
}

void DisplayManager::drawUserList(bool picking) {
	if (!_driver.canvas || !_sysConfigPtr) return;
	const bool fullRedraw = _forceSettingsRedraw;
	const bool pageChanged = (_usersPage != _lastUsersPage);
	int totalPages = (_usersCount + 3) / 4; if (totalPages == 0) totalPages = 1;
	if (_usersPage >= totalPages) _usersPage = totalPages - 1;
	if (_usersPage < 0) _usersPage = 0;

	const int startIdx = _usersPage * 4;
	const int16_t itemW = 285;

	if (fullRedraw) {
		/* Six 40-px strips, top to bottom, one DMA blit each — the same sweep
		 * drawPinScreen( ) uses, and for the same reason. This screen used to
		 * fastClearScreen( ) and then blit the title, the footer, the
		 * scrollbar and four rows one at a time, so the panel was visibly
		 * DARK between the clear and the last blit: the maintainer saw it
		 * flicker on every entry to the account picker (2026-09-20). A strip
		 * is written once, already composed, so there is never a hole. */
		GFXcanvas16* cv = beginScreenRender( );
		if (!cv) return;
		for (int strip = 0; strip < 6; strip++) {
			const int16_t oy = (int16_t)(-strip * RENDER_STRIP_H);
			cv->fillScreen(C_BG_MAIN);
			if (strip == 0)
				uiTitleBar(cv, (int16_t)(4 + oy), tr(picking ? TR_WHO_LBL : TR_USERS_TITLE));
			uiScrollbar(cv, 302, (int16_t)(40 + oy), 8, 146, totalPages, _usersPage);
			for (int i = 0; i < 4; i++)
				drawUserRowInto(cv, picking, startIdx + i, 10,
				                (int16_t)(40 + i * 38 + oy), itemW);
			uiFooterMenu(cv, tr(TR_BACK), tr(picking ? TR_ENTER : TR_NEW_LBL),
			             (int16_t)(195 + oy));
			commitScreenStrip((int16_t)strip);
		}
		endScreenRender( );
		_forceSettingsRedraw = false; _lastUsersPage = _usersPage; _lastUsersSel = _usersSel;
		return;
	}

	if (pageChanged) uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, _usersPage);

	/* Only what moved: the row that gained the selection and the one that lost
	 * it (a page change repaints all four). */
	for (int i = 0; i < 4; i++) {
		const int mapIdx = startIdx + i;
		if (!pageChanged && mapIdx != _usersSel && mapIdx != _lastUsersSel) continue;
		_driver.canvas->fillScreen(C_BG_MAIN);
		drawUserRowInto(_driver.canvas, picking, mapIdx, 0, 0, itemW);
		blitCanvas(_driver.canvas, 10, (int16_t)(40 + i * 38), itemW, 34);
	}
	_lastUsersPage = _usersPage; _lastUsersSel = _usersSel;
}

void DisplayManager::drawSettingsUsers( ) { drawUserList(false); }

void DisplayManager::drawUserEdit( ) {
	if (!_driver.canvas || !_sysConfigPtr) return;
	const bool fullRedraw = _forceSettingsRedraw;
	const int rows = userEditRowCount( );
	int totalPages = (rows + 3) / 4; if (totalPages == 0) totalPages = 1;
	const bool pageChanged = (_userEditPage != _lastUserEditPage);

	if (fullRedraw) {
		fastClearScreen(C_BG_MAIN);
		const char* title = _userEditNew ? _newUserName
		                  : ((_userEditSlot >= 0) ? _sysConfigPtr->users[_userEditSlot].username : "");
		blitTitleBar(title[0] ? title : tr(TR_USERS_TITLE));
		blitFooterMenu(tr(TR_BACK), _userEditNew ? tr(TR_CONTINUE_LBL) : tr(TR_SAVE));
	}
	if (fullRedraw || pageChanged) uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, _userEditPage);

	const uint16_t bits[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
	const LangKey label[5] = { TR_PERM_LIMITS, TR_PERM_BLOCK, TR_PERM_MAINT, TR_SET_PIN, TR_DELETE_USER };
	const bool lockedBits = (!_userEditNew && _userEditSlot == 0);
	const int itemW = 285;
	const int startIdx = _userEditPage * 4;
	for (int i = 0; i < 4; i++) {
		const int idx = startIdx + i;
		const int y = 40 + i * 38;
		if (!fullRedraw && !pageChanged && idx != _userEditSel && idx != _lastUserEditSel) continue;
		_driver.canvas->fillScreen(C_BG_MAIN);
		if (idx < rows) {
			const bool sel = (idx == _userEditSel);
			const uint16_t bg = sel ? C_ACCENT : C_CARD_BG;
			const uint16_t txt = sel ? C_BG_MAIN : C_TEXT_MAIN;
			_driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
			if (!sel) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
			_driver.canvas->setFont(&simutFont9pt);
			_driver.canvas->setTextColor(txt);
			_driver.canvas->setCursor(10, 24);
			_driver.canvas->print(tr(label[idx]));
			int16_t bx, by; uint16_t bw, bh;
			if (idx < 3) {
				const bool on = lockedBits || (_userEditPerms & bits[idx]);
				const char* st = on ? tr(TR_ON) : tr(TR_OFF);
				_driver.canvas->getTextBounds(st, 0, 0, &bx, &by, &bw, &bh);
				uint16_t sc = sel ? C_BG_MAIN : (on ? C_TEMP_OK : C_TEXT_OFF);
				if (lockedBits && !sel) sc = C_TEXT_SUB;
				_driver.canvas->setTextColor(sc);
				_driver.canvas->setCursor(itemW - 10 - (int)bw, 24);
				_driver.canvas->print(st);
			} else {
				_driver.canvas->fillTriangle(itemW - 20, 11, itemW - 20, 23, itemW - 10, 17, sel ? C_BG_MAIN : C_TEXT_SUB);
			}
		}
		blitCanvas(_driver.canvas, 10, y, itemW, 34);
	}
	_forceSettingsRedraw = false; _lastUserEditPage = _userEditPage; _lastUserEditSel = _userEditSel;
}

void DisplayManager::drawUserConfirmDel( ) {
	if (!_driver.canvas || !_sysConfigPtr || !_forceSettingsRedraw) return;
	_forceSettingsRedraw = false;
	fastClearScreen(C_BG_MAIN);
	blitTitleBar(tr(TR_DELETE_USER));
	int16_t bx, by; uint16_t bw, bh;
	_driver.canvas->fillScreen(C_BG_MAIN);
	_driver.canvas->setFont(&simutFont12pt); _driver.canvas->setTextColor(C_TEXT_MAIN);
	String q = tr(TR_CONFIRM_DELETE);
	_driver.canvas->getTextBounds(q, 0, 0, &bx, &by, &bw, &bh);
	_driver.canvas->setCursor((320 - bw) / 2, 30); _driver.canvas->print(q);
	blitCanvas(_driver.canvas, 0, 60, 320, 45);
	_driver.canvas->fillScreen(C_BG_MAIN);
	_driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(C_TEMP_HOT);
	const char* name = (_userEditSlot >= 0) ? _sysConfigPtr->users[_userEditSlot].username : "";
	_driver.canvas->getTextBounds(name, 0, 0, &bx, &by, &bw, &bh);
	_driver.canvas->setCursor((320 - bw) / 2, 25); _driver.canvas->print(name);
	blitCanvas(_driver.canvas, 0, 105, 320, 40);
	/* two buttons, cancel on the left like every footer here */
	_driver.canvas->fillScreen(C_BG_MAIN);
	uiButton(_driver.canvas, 20, 0, 130, 40, tr(TR_CANCEL), UI_BTN_SECONDARY);
	_driver.canvas->fillRoundRect(170, 0, 130, 40, 10, C_ALARM_BG);
	_driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(C_ALARM_ERR_TEXT);
	String d = tr(TR_DELETE_LBL);
	_driver.canvas->getTextBounds(d, 0, 0, &bx, &by, &bw, &bh);
	_driver.canvas->setCursor(170 + (130 - (int)bw) / 2 - bx, (40 - (int)bh) / 2 - by); _driver.canvas->print(d);
	blitCanvas(_driver.canvas, 0, 195, 320, 45);
}

void DisplayManager::drawPanelMessage( ) {
	if (!_driver.tft || !_forceSettingsRedraw) return;
	_forceSettingsRedraw = false;
	int16_t x1, y1; uint16_t w, h;
	fastClearScreen(C_BG_MAIN);
	const uint16_t iconColor = _msgOk ? C_TEMP_OK : C_TEMP_WARM;
	if (_msgOk) {
		_driver.tft->drawLine(130, 90, 150, 110, iconColor);
		_driver.tft->drawLine(131, 90, 151, 110, iconColor);
		_driver.tft->drawLine(150, 110, 190, 70, iconColor);
		_driver.tft->drawLine(151, 110, 191, 70, iconColor);
	} else {
		_driver.tft->drawLine(145, 70, 175, 100, iconColor);
		_driver.tft->drawLine(146, 70, 176, 100, iconColor);
		_driver.tft->drawLine(175, 70, 145, 100, iconColor);
		_driver.tft->drawLine(176, 70, 146, 100, iconColor);
	}
	const char* msg = (_msgKey < TR_KEYS_COUNT) ? tr(_msgKey) : "";
	_driver.tft->setFont(&simutFont9pt);
	_driver.tft->setTextColor(C_TEXT_MAIN);
	_driver.tft->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
	_driver.tft->setCursor((320 - w) / 2, 130);
	_driver.tft->print(msg);
	_driver.tft->fillRoundRect(60, 185, 200, 40, 12, C_ACCENT);
	_driver.tft->setFont(&simutFont12pt);
	_driver.tft->setTextColor(C_BG_MAIN);
	const char* btn = tr(TR_UNDERSTOOD);
	_driver.tft->getTextBounds(btn, 0, 0, &x1, &y1, &w, &h);
	_driver.tft->setCursor(160 - (w / 2), 212);
	_driver.tft->print(btn);
}

void DisplayManager::drawAuthUser( ) { drawUserList(true); }

/* Three rows, because there are exactly three knobs and each one buys a
 * different thing:
 *   length   — every character multiplies a blind guess by C/S
 *   keypad   — S, which is the watcher's uncertainty AND the searcher's
 *              branching factor, so moving it trades one for the other
 *   alphabet — C, which is the only one that is not a trade
 * The value column shows what the choice costs where it costs something: the
 * keypad row carries the length ceiling it imposes, because choosing
 * "Scrambled 3" silently caps the length at 8 and a knob that changes another
 * knob without saying so is how a policy screen lies. */
void DisplayManager::drawPinPolicy( ) {
	if (!_driver.canvas) return;
	const uint8_t minLen = _policyMin, keypad = _policyKb, alphabet = _policyAlpha;

	/* Title and footer never change on this screen, so they are painted on the
	 * way in and nowhere else. A tap on a value used to take this branch too —
	 * a full clear plus both bands to repaint three rows, which is the
	 * whole-screen flash the operator saw on every tap (reported 2026-09-24). */
	if (_forceSettingsRedraw) {
		fastClearScreen(C_BG_MAIN);
		blitTitleBar(menuLabelNoNumber(tr(TR_PIN_POLICY)));
		blitFooterMenu(tr(TR_BACK), tr(TR_SAVE));
	}

	/* The values are glyphs and numbers, not sentences: "2" glyphs per key,
	 * "0-9 A-Z" for the alphabet. They read the same in every language,
	 * they are what the web page shows, and they are five strings that do not
	 * have to fit under the es-ES pack's 16 KB resident ceiling. The keypad
	 * row carries the length ceiling it imposes, because choosing 3 silently
	 * caps the length at 8 and a knob that moves another knob without saying
	 * so is how a policy screen lies. */
	const LangKey rowKey[3] = { TR_PIN_MIN_LEN, TR_PIN_KEYPAD, TR_PIN_ALPHABET };
	char value[3][16];
	snprintf(value[0], sizeof(value[0]), "%u", (unsigned)minLen);
	snprintf(value[1], sizeof(value[1]), "%u  (max %u)", (unsigned)keypad,
	         (unsigned)PinKb::maxLenFor(keypad));
	safeCopy(value[2], (alphabet == PinKb::ALPHA_ALNUM) ? "0-9 A-Z" : "0-9", sizeof(value[2]));

	/* All three rows on every repaint, one canvas pass and one blit each (the
	 * canvas is 320x45 and a row is 38). Not one row at a time, because a
	 * change to one can move another: the keypad caps the length. */
	const int itemW = 300;
	int16_t bx, by; uint16_t bw, bh;
	for (int i = 0; i < 3; i++) {
		const bool sel = (i == _policySel);
		_driver.canvas->fillScreen(C_BG_MAIN);
		_driver.canvas->fillRoundRect(0, 0, itemW, 38, 8, sel ? C_ACCENT : C_CARD_BG);
		if (!sel) _driver.canvas->drawRoundRect(0, 0, itemW, 38, 8, C_TEXT_SUB);
		_driver.canvas->setFont(&simutFont9pt);
		_driver.canvas->setTextColor(sel ? C_BG_MAIN : C_TEXT_MAIN);
		_driver.canvas->setCursor(10, 25);
		_driver.canvas->print(tr(rowKey[i]));
		_driver.canvas->getTextBounds(value[i], 0, 0, &bx, &by, &bw, &bh);
		_driver.canvas->setTextColor(sel ? C_BG_MAIN : C_ACCENT);
		_driver.canvas->setCursor((int16_t)(itemW - 12 - (int16_t)bw), 25);
		_driver.canvas->print(value[i]);
		blitCanvas(_driver.canvas, 10, (int16_t)(46 + i * 44), itemW, 38);
	}
	_forceSettingsRedraw = false;
	_lastPolicySel = _policySel;
}

bool DisplayManager::handleTouchPanelV24(int16_t x, int16_t y) {
	using namespace PinKb;

	if (_uiMode == MODE_AUTH) {
		/* Footer first: BACK answers even under a lockout — it is the way out
		 * of the screen, and the only control that has to work there. */
		if (y >= FOOT_Y) {
			if (x >= FOOT_EXIT_X && x < FOOT_EXIT_X + FOOT_EXIT_W) {
				if (!acceptTouch(0)) return true;
				pinCancel( ); return true;
			}
			if (pinInputBlocked( )) return true;
			actPinPopupDrop( );
			if (x < FOOT_BACK_X + FOOT_BACK_W) {
				if (!acceptTouch(1)) return true;
				actPinBackspace( ); return true;
			}
			if (x >= FOOT_OK_X) {
				if (!acceptTouch(2)) return true;
				actPinOk( ); return true;
			}
			return true;
		}
		if (pinInputBlocked( )) return true;

		/* The ORDERED keyboard answers first: it is what is on the glass both
		 * when a PIN is being CHOSEN and when one is being proved under
		 * KB_PLAIN. `pinGrid( )` already knows which of the two it is. */
		if (pinOrderedNow( )) {
			const Grid g = pinGrid( );
			const uint8_t alphabet = pinAlphabet( );

			/* ALPHA_ALNUM costs two taps. While a popup is up it owns every
			 * tap: one of its keys yields the character, anything else closes
			 * it. Nothing underneath answers, so a fat finger on the edge of
			 * the card cannot type a character from the group behind it. */
			if (alphabet == ALPHA_ALNUM && _pinPopup >= 0) {
				const char* mem = GROUP_CHARS[_pinPopup];
				const int n = (int)strlen(mem);
				const int i = popupKeyAt(n, x, y);
				if (!acceptTouch((uint8_t)(40 + (i < 0 ? 0 : i + 1)))) return true;
				actPinPopupKey(mem, n, i);
				return true;
			}

			const int k = keyAt(g, x, y);
			/* An empty face is one of the numeric pad's two blank cells. */
			if (k < 0 || !_pinKeyChars[k][0]) return true;
			if (!acceptTouch((uint8_t)(20 + k))) return true;
			actPinOrderedKey(k, alphabet);
			return true;
		}

		{
			/* A dealt keypad: the whole card is one target (actPinCard( )). */
			const Grid g = pinGrid( );
			const int k = keyAt(g, x, y);
			if (k < 0) return true;
			if (!acceptTouch((uint8_t)(10 + k))) return true;
			actPinCard(g, k);
			return true;
		}
	}

	if (_uiMode == MODE_PANEL_MESSAGE) {
		if (y >= 185) { if (!acceptTouch(0)) return true; leavePanelMessage( ); }
		return true;
	}

	if (_uiMode == MODE_SETTINGS_ALARM_SENSOR) {
		auto activate = [&](int row) {
			actSensorMenuActivate(row);
		};
		if (y >= 40 && y <= 154) {
			int row = 0;
			if (y < 80) row = 0; else if (y < 118) row = 1; else row = 2;
			if (row != _sensorMenuSel) {
				if (!acceptSlideTouch((uint8_t)row)) return true;
				_sensorMenuSel = row; _repaintSettings = true;
			} else {
				if (!acceptTouch((uint8_t)(4 + row))) return true;
				activate(row);
			}
		} else if (y > 185) {
			if (x < 70) {
				if (!acceptHoldTouch(10)) return true;
				_sensorMenuSel = (_sensorMenuSel > 0) ? _sensorMenuSel - 1 : 2; _repaintSettings = true;
			} else if (x < 138) {
				if (!acceptHoldTouch(11)) return true;
				_sensorMenuSel = (_sensorMenuSel < 2) ? _sensorMenuSel + 1 : 0; _repaintSettings = true;
			} else if (x < 219) {
				if (!acceptTouch(12)) return true;
				showSettingsAlarms(_sysConfigPtr);
			} else {
				if (!acceptTouch(13)) return true;
				activate(_sensorMenuSel);
			}
		}
		return true;
	}

	if (_uiMode == MODE_SETTINGS_MAINT) {
		const bool open = maintMinutesLeft(_sysConfigPtr, _maintSlot) != 0;
		if (y > 185) {
			if (x >= 138 && x < 219) { if (!acceptTouch(12)) return true; showAlarmSensorMenu(_maintSlot); return true; }
			if (x >= 219) {
				if (!acceptTouch(13)) return true;
				actMaintPrimary( open );
				return true;
			}
			/* arrows move the focus between the two bars */
			if (x < 138 && !open) { if (!acceptHoldTouch(10)) return true; _maintFocus ^= 1; _repaintSettings = true; }
			return true;
		}
		if (open) return true;
		/* the bars: which one, and which end */
		const int y0 = ALARM_EDIT_Y0 + 20;
		const int pitch = ALARM_EDIT_STEP + 10;
		if (y < y0 || y >= y0 + 2 * pitch) return true;
		const int row = (y - y0) / pitch;
		if (row != _maintFocus) { if (!acceptSlideTouch((uint8_t)(20 + row))) return true; actMaintFocus( row ); return true; }
		int delta = 0;
		if (x < ALARM_EDIT_HIT_DEC) delta = -1;
		else if (x > ALARM_EDIT_HIT_INC) delta = +1;
		if (delta == 0) return true;
		if (!acceptHoldTouch((uint8_t)(30 + row * 2 + (delta > 0)))) return true;
		actMaintStep( row, delta );
		return true;
	}

	if (_uiMode == MODE_AUTH_USER || _uiMode == MODE_SETTINGS_USERS) {
		const bool picking = (_uiMode == MODE_AUTH_USER);
		if (y >= 40 && y <= 185) {
			int clicked = 0;
			if (y < 80) clicked = 0; else if (y < 118) clicked = 1; else if (y < 156) clicked = 2; else clicked = 3;
			const int mapIdx = _usersPage * 4 + clicked;
			if (mapIdx >= _usersCount) return true;
			if (mapIdx != _usersSel) {
				if (!acceptSlideTouch((uint8_t)clicked)) return true;
				_usersSel = mapIdx; _usersPage = _usersSel / 4; _repaintSettings = true;
			} else {
				if (!acceptTouch((uint8_t)(4 + clicked))) return true;
				actUserOpen(picking);
			}
		} else if (y > 185) {
			/* The arrows step one row on a short list and one PAGE once the
			 * list is longer than two pages. On the picker with a full table
			 * that is the difference between 28 taps and 7 for whoever holds
			 * the last slot — and the rows are still individually tappable,
			 * so nothing is lost: the arrows are for getting to the page, the
			 * row is for choosing. The editor list inherits it for free. */
			const int step = (_usersCount > 8) ? 4 : 1;
			if (x < 70) {
				if (!acceptHoldTouch(10)) return true;
				_usersSel -= step;
				if (_usersSel < 0) _usersSel = (_usersCount > 0) ? _usersCount - 1 : 0;
				_usersPage = _usersSel / 4; _repaintSettings = true;
			} else if (x < 138) {
				if (!acceptHoldTouch(11)) return true;
				_usersSel += step;
				if (_usersSel > _usersCount - 1) _usersSel = 0;
				_usersPage = _usersSel / 4; _repaintSettings = true;
			} else if (x < 219) {
				if (!acceptTouch(12)) return true;
				if (picking) forceDashboard( );
				else         showSettingsMain( );
			} else {
				if (!acceptTouch(13)) return true;
				if (picking) {
					if (_usersCount > 0) enterPinFor(_usersMap[_usersSel]);
					else _errorSoundPending = true;
				} else {
					actUserNew( );
				}
			}
		}
		return true;
	}

	if (_uiMode == MODE_SETTINGS_PIN_POLICY) {
		if (y >= 46 && y < 178) {
			const int row = (y - 46) / 44;
			if (row < 0 || row > 2) return true;
			if (row != _policySel) {
				if (!acceptSlideTouch((uint8_t)row)) return true;
				_policySel = row; _repaintSettings = true;
				return true;
			}
			if (!acceptTouch((uint8_t)(4 + row))) return true;
			/* Tapping the selected row cycles it. The length wraps at the
			 * CEILING OF THE CURRENT KEYPAD, and changing the keypad re-clamps
			 * the length, so the pair on screen is always one the panel can
			 * actually run — the screen cannot offer 10 characters on a keypad
			 * that would take 3.2 s to resolve them. */
			uint8_t minLen = _policyMin, keypad = _policyKb, alphabet = _policyAlpha;
			if (row == 0) {
				minLen = (minLen >= PinKb::maxLenFor(keypad)) ? (uint8_t)PinKb::PIN_LEN_MIN
				                                              : (uint8_t)(minLen + 1);
			} else if (row == 1) {
				do {
					keypad = (keypad >= PinKb::KB_MAX) ? (uint8_t)PinKb::KB_MIN : (uint8_t)(keypad + 1);
				} while (!PinKb::comboSupported(alphabet, keypad));
			} else {
				alphabet = (uint8_t)((alphabet + 1) % PinKb::ALPHA_COUNT);
				/* Every pair has a layout since KB_PLAIN became ordered, so this
				 * does not step today. It stays so that a pair without one would
				 * move the keypad instead of refusing the alphabet just asked for. */
				while (!PinKb::comboSupported(alphabet, keypad)) keypad++;
			}
			actPolicySet( minLen, keypad, alphabet );
			return true;
		}
		/* The footer is the standard one (uiFooterMenu: up 5..67, down 73..135,
		 * back 141..216, save 222..315), so it answers in the standard zones,
		 * the ones MODE_SETTINGS_MAIN uses. Everything left of x=219 used to be
		 * BACK, and the two arrows drawn there closed the screen — measured on
		 * the rig on 2026-09-24: UI mode 28 -> 6 on a tap of the down arrow. */
		if (y > 185) {
			if (x < 70) {
				if (!acceptHoldTouch(10)) return true;
				_policySel = (_policySel > 0) ? _policySel - 1 : 2;
				_repaintSettings = true;
			} else if (x < 138) {
				if (!acceptHoldTouch(11)) return true;
				_policySel = (_policySel < 2) ? _policySel + 1 : 0;
				_repaintSettings = true;
			} else if (x < 219) {
				if (!acceptTouch(12)) return true;
				/* Nothing to put back: the rows edited a copy. */
				showSettingsMain( );
			} else {
				if (!acceptTouch(13)) return true;
				actPolicySave( );
			}
		}
		return true;
	}


	if (_uiMode == MODE_SETTINGS_USER_EDIT) {
		const int rows = userEditRowCount( );
		const bool lockedBits = (!_userEditNew && _userEditSlot == 0);
		auto activate = [&](int idx) {
			actUserEditActivate(idx, lockedBits);
		};
		if (y >= 40 && y <= 185) {
			int clicked = 0;
			if (y < 80) clicked = 0; else if (y < 118) clicked = 1; else if (y < 156) clicked = 2; else clicked = 3;
			const int idx = _userEditPage * 4 + clicked;
			if (idx >= rows) return true;
			if (idx != _userEditSel) {
				if (!acceptSlideTouch((uint8_t)clicked)) return true;
				_userEditSel = idx; _userEditPage = idx / 4; _repaintSettings = true;
			} else {
				if (!acceptTouch((uint8_t)(4 + clicked))) return true;
				activate(idx);
			}
		} else if (y > 185) {
			if (x < 70) {
				if (!acceptHoldTouch(10)) return true;
				if (_userEditSel > 0) _userEditSel--; else _userEditSel = rows - 1;
				_userEditPage = _userEditSel / 4; _repaintSettings = true;
			} else if (x < 138) {
				if (!acceptHoldTouch(11)) return true;
				if (_userEditSel < rows - 1) _userEditSel++; else _userEditSel = 0;
				_userEditPage = _userEditSel / 4; _repaintSettings = true;
			} else if (x < 219) {
				if (!acceptTouch(12)) return true;
				showSettingsUsers( );
			} else {
				if (!acceptTouch(13)) return true;
				actUserEditSave(lockedBits);
			}
		}
		return true;
	}

	if (_uiMode == MODE_SETTINGS_USER_CONFIRM_DEL) {
		if (y >= 195) {
			if (x < 160) { if (!acceptTouch(0)) return true; actUserDelCancel( ); }
			else {
				if (!acceptTouch(1)) return true;
				actUserDelConfirm( );
			}
		}
		return true;
	}

	return false;
}
