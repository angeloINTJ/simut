/**
 * @file DisplayManager_Users.cpp
 * @brief v24 — identity at the panel: the PIN keypad, the per-sensor action
 *        menu, the maintenance-window entry, the user list/editor, and the
 *        result message every one of them ends on.
 *
 * @details Sub-file of DisplayManager.cpp, Core 1 only. Nothing here touches
 * the configuration: every action becomes a UiEvent that Core 0 authorises,
 * applies, saves, logs and reports through the alarm line (AppManager_Events).
 * The screens therefore show what Core 0 last handed them (setPanelSession,
 * showPanelMessage), never what they hope will be true.
 *
 * Why a numeric keypad replaced the scrambled 4-button keypad of the device
 * PIN: that keypad worked by KNOWING the expected PIN and asking, digit by
 * digit, which of four buttons holds it — 1/4 per digit, whatever the PIN.
 * With per-user PINs the panel does not know which PIN to expect, because
 * the PIN is what identifies the user. Resolving the 4^N button sequences
 * against every account would accept a random sequence with probability
 * (accounts / 256) per attempt — 12.5% at 32 accounts. A plain keypad makes
 * that accounts / 10^N, and the lockout below does the rest.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#if SIMUT_DISPLAY_TFT
#include "DisplayManager_Fonts.h"
#include "UiWidgets.h"
#include "SystemDefs_Validate.h"     /* PIN_MIN_LEN / PIN_MAX_LEN */
#include "StorageManager.h"          /* userHasPin */
#include "sensors/SensorChannelTable.h"

/* Geometry and character set: PinKeypad.h, shared with the CLI readout. */

/* ────────────────────────────────────────────────────────────────────────── */
/* PIN entry                                                                  */
/* ────────────────────────────────────────────────────────────────────────── */

/* Deals the ten digits and two decoy symbols over the four cards.
 *
 * Fisher-Yates over the twelve slots, so a digit is as likely to land in any
 * slot of any card. The two decoys exist to make every card show three glyphs:
 * ten digits over four cards is 3+3+2+2, and a card visibly shorter than its
 * neighbours would tell a watcher which cards carry more of the alphabet. They
 * are symbols because the PIN is digits only — a letter there would read as a
 * character someone might be expected to type.
 *
 * fastRandom( ) is the xorshift the pre-v24 scrambled keypad used, seeded from
 * micros( ). It is not a CSPRNG and does not need to be: what it hides is the
 * MEANING OF A TAP POSITION from someone watching the glass, and the PIN's
 * strength is its length, not this shuffle. Called with _stateMutex held. */
void DisplayManager::scramblePinKeys( ) {
	char all[PinKb::KEYS * PinKb::SLOTS];
	for (int i = 0; i < PinKb::CHARS; i++) all[i] = (char)(PinKb::FIRST + i);
	/* two distinct decoys, so a repeated symbol cannot be read as a pattern */
	for (int d = 0; d < PinKb::DECOYS; d++) {
		char c;
		bool dup;
		do {
			c = PinKb::DECOY_POOL[fastRandom(PinKb::DECOY_POOL_N)];
			dup = false;
			for (int p = 0; p < d; p++) if (all[PinKb::CHARS + p] == c) dup = true;
		} while (dup);
		all[PinKb::CHARS + d] = c;
	}
	for (int i = (int)sizeof(all) - 1; i > 0; i--) {
		const int j = (int)fastRandom((uint32_t)(i + 1));
		const char tmp = all[i]; all[i] = all[j]; all[j] = tmp;
	}
	for (int k = 0; k < PinKb::KEYS; k++) {
		for (int s = 0; s < PinKb::SLOTS; s++) _pinKeyChars[k][s] = all[k * PinKb::SLOTS + s];
		_pinKeyChars[k][PinKb::SLOTS] = '\0';
	}
}

/* The four cards, for `show display keypad`. The bench drives the panel from
 * outside and cannot find a digit whose position it was never told; the cards
 * are on the glass for anyone standing there, so printing them to a privileged
 * CLI gives an attacker nothing it does not already have. */
uint8_t DisplayManager::getEnteredPinTaps(char out[][PinKb::SLOTS + 1], size_t cap) const {
	if (!out || cap == 0) return 0;
	const uint8_t n = (_pinLen > (uint8_t)cap) ? (uint8_t)cap : _pinLen;
	for (uint8_t i = 0; i < n; i++) memcpy(out[i], _pinTaps[i], PinKb::SLOTS + 1);
	return n;
}

uint8_t DisplayManager::pinKeyFace(int key, char* out, size_t cap) const {
	if (!out || cap == 0) return 0;
	out[0] = '\0';
	if (key < 0 || key >= PinKb::KEYS || _uiMode != MODE_AUTH || !pinIsIdentifying( )) return 0;
	if ((size_t)PinKb::SLOTS + 1 > cap) return 0;
	memcpy(out, _pinKeyChars[key], PinKb::SLOTS);
	out[PinKb::SLOTS] = '\0';
	return (uint8_t)PinKb::SLOTS;
}

void DisplayManager::showPinEntry(uint8_t purpose, int8_t targetUser) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_AUTH;
	_pinPurpose = purpose;
	_pinTarget = targetUser;
	_pinLen = 0; _pinBuf[0] = '\0';
	memset(_pinTaps, 0, sizeof(_pinTaps));
	_pinPhase = 0; _pinFirst[0] = '\0';
	_pinMsg = TR_KEYS_COUNT;
	_pinWaiting = false;
	_authFailed = false;
	_rngState = micros( ) ^ 0xA5A5A5A5; if (_rngState == 0) _rngState = 1;
	scramblePinKeys( );
	if (_permanentLockout) _lockoutUntil = millis( ) + 10000;
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
}


void DisplayManager::getEnteredPin(char* out, size_t cap) const {
	if (!out || cap == 0) return;
	strncpy(out, _pinBuf, cap - 1);
	out[cap - 1] = '\0';
}

void DisplayManager::clearEnteredPin( ) {
	volatile char* v = _pinBuf;
	for (size_t i = 0; i < sizeof(_pinBuf); i++) v[i] = 0;
	volatile char* w = _pinFirst;
	for (size_t i = 0; i < sizeof(_pinFirst); i++) w[i] = 0;
	_pinLen = 0;
}

/* Core 0's verdict on the PIN it took with getEnteredPin( ). The lockout
 * ladder is the one the device PIN always had: two free tries, then 5 s,
 * 15 s, 60 s, and a permanent lockout that only a reboot clears. The count
 * is returned so Core 0 can log the rung that was reached. */
int DisplayManager::authResult(bool ok) {
	mutex_enter_blocking(&_stateMutex);
	_pinWaiting = false;
	_pinLen = 0; _pinBuf[0] = '\0';
	memset(_pinTaps, 0, sizeof(_pinTaps));
	int failures = 0;
	if (ok) {
		_failedAttempts = 0;
		_authFailed = false;
	} else {
		_authFailed = true;
		_failedAttempts++;
		failures = _failedAttempts;
		_errorSoundPending = true;
		if (_failedAttempts <= 2) _lockoutUntil = 0;
		else if (_failedAttempts == 3) _lockoutUntil = millis( ) + 5000;
		else if (_failedAttempts == 4) _lockoutUntil = millis( ) + 15000;
		else if (_failedAttempts == 5) _lockoutUntil = millis( ) + 60000;
		else { _permanentLockout = true; _lockoutUntil = millis( ) + 10000; }
		_pinMsg = TR_INVALID_PIN;
		/* A refusal is exactly when someone is most likely to have been
		 * watching the taps that produced it. */
		scramblePinKeys( );
		_forceSettingsRedraw = true;
	}
	_repaintSettings = true;
	mutex_exit(&_stateMutex);
	return failures;
}

const char* DisplayManager::panelUserName( ) const {
	if (_panelUser < 0 || _panelUser >= MAX_USERS || !_sysConfigPtr) return "";
	const UserAccount& u = _sysConfigPtr->users[_panelUser];
	return u.active ? u.username : "";
}

void DisplayManager::setPanelSession(int8_t user, uint16_t perms) {
	mutex_enter_blocking(&_stateMutex);
	_panelUser = user;
	_panelPerms = perms;
	mutex_exit(&_stateMutex);
}

/* Cancel from the keypad: back to wherever this PIN was asked from. */
void DisplayManager::pinCancel( ) {
	clearEnteredPin( );
	switch (_pinPurpose) {
		case PIN_FOR_OWN:          showSettingsMain( ); break;
		case PIN_FOR_USER:         showUserEdit(_pinTarget, false); break;
		case PIN_FOR_NEW_ACCOUNT:  showUserEdit(-1, true); break;
		default:                   forceDashboard( ); break;
	}
}

/* OK from the keypad. For identification the PIN goes to Core 0 as is; for a
 * new PIN it is typed twice, compared here, and only then handed over. */
void DisplayManager::pinSubmit( ) {
	if (_pinLen < PIN_MIN_LEN) {
		_pinMsg = TR_PIN_TOO_SHORT;
		_errorSoundPending = true;
		_repaintSettings = true;
		return;
	}
	if (_pinPurpose == PIN_FOR_AUTH) {
		/* _pinTaps holds, per tap, the glyphs that were on the card; Core 0
		 * reads them with getEnteredPinTaps( ). Nothing here knows the PIN. */
		_pinWaiting = true;
		_pinMsg = TR_KEYS_COUNT;
		UiEvent ev; ev.type = UiEvent::EVT_AUTH_PIN; ev.id = (int)_pinLen; ev.param = 0;
		pushUiEvent(ev);
		_repaintSettings = true;
		return;
	}
	if (_pinPhase == 0) {
		strncpy(_pinFirst, _pinBuf, sizeof(_pinFirst) - 1);
		_pinFirst[sizeof(_pinFirst) - 1] = '\0';
		_pinPhase = 1;
		_pinLen = 0; _pinBuf[0] = '\0';
		_pinMsg = TR_KEYS_COUNT;
		/* Confirming on the same layout would let a watcher check the second
		 * run against the first; on a fresh deal the two look nothing alike. */
		scramblePinKeys( );
		_forceSettingsRedraw = true; _repaintSettings = true;
		return;
	}
	if (strcmp(_pinFirst, _pinBuf) != 0) {
		_pinPhase = 0;
		_pinLen = 0; _pinBuf[0] = '\0'; _pinFirst[0] = '\0';
		_pinMsg = TR_PIN_MISMATCH;
		_errorSoundPending = true;
		scramblePinKeys( );
		_forceSettingsRedraw = true; _repaintSettings = true;
		return;
	}
	/* Matched. The PIN stays in _pinBuf for Core 0 to read; the event says
	 * whose it is. A new account carries name + perms + PIN at once. */
	_pinWaiting = true;
	UiEvent ev;
	if (_pinPurpose == PIN_FOR_NEW_ACCOUNT) {
		ev.type = UiEvent::EVT_USER_ADD; ev.id = 0; ev.param = (int)_userEditPerms;
	} else {
		ev.type = UiEvent::EVT_USER_PIN;
		ev.id = (_pinPurpose == PIN_FOR_OWN) ? (int)_panelUser : (int)_pinTarget;
		ev.param = 0;
	}
	pushUiEvent(ev);
	_repaintSettings = true;
}

/* One card, into `cv` at its screen position shifted by `oy` (the canvas is a
 * 40-px strip of the screen, so everything outside clips away). */
void DisplayManager::drawPinCardInto(GFXcanvas16* cv, int key, int16_t ox, int16_t oy) {
	using namespace PinKb;
	if (!cv || key < 0 || key >= KEYS) return;
	int16_t bx, by; uint16_t bw, bh;
	const int16_t x = (int16_t)(KEY_X[key] + ox), y = (int16_t)(KEY_Y[key] + oy);

	cv->fillRoundRect(x, y, KEY_W, KEY_H, 8, C_CARD_BG);
	cv->drawRoundRect(x, y, KEY_W, KEY_H, 8, C_TEXT_SUB);
	cv->setFont(&simutFont12pt);
	/* One ink for everything on the card. A decoy drawn dimmer would tell a
	 * watcher which glyphs can be part of a PIN, and on the identification
	 * keypad that is exactly the thing the card is hiding. */
	cv->setTextColor(C_TEXT_MAIN);
	for (int s = 0; s < SLOTS; s++) {
		const char c = _pinKeyChars[key][s];
		if (!c) continue;
		const char str[2] = { c, '\0' };
		cv->getTextBounds(str, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((int16_t)(x + SLOT_X0 + s * SLOT_W + (SLOT_W - (int16_t)bw) / 2 - bx),
		              (int16_t)(y + (KEY_H - (int16_t)bh) / 2 - by));
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
	for (int k = 0; k < KEYS; k++) {
		_driver.canvas->fillScreen(C_BG_MAIN);
		drawPinCardInto(_driver.canvas, k, (int16_t)(-KEY_X[k]), (int16_t)(-KEY_Y[k]));
		blitCanvas(_driver.canvas, KEY_X[k], KEY_Y[k], KEY_W, KEY_H);
	}
}

/* The ordered pad: digits where a numeric pad puts them. Setting a PIN needs
 * the exact digits, so there is nothing to scramble and nothing to hide — the
 * operator is choosing, not proving. */
void DisplayManager::drawPinPadInto(GFXcanvas16* cv, int16_t oy) {
	using namespace PinKb;
	if (!cv) return;
	int16_t bx, by; uint16_t bw, bh;
	cv->setFont(&simutFont12pt);
	for (int r = 0; r < NUM_ROWS; r++) {
		for (int c = 0; c < NUM_COLS; c++) {
			const char k = numKeyAt((int16_t)(NUM_X0 + c * NUM_COL_PITCH + 2),
			                        (int16_t)(NUM_Y0 + r * NUM_ROW_PITCH + 2));
			if (!k) continue;
			const int16_t x = (int16_t)(NUM_X0 + c * NUM_COL_PITCH);
			const int16_t y = (int16_t)(NUM_Y0 + r * NUM_ROW_PITCH + oy);
			cv->fillRoundRect(x, y, NUM_KEY_W, NUM_KEY_H, 8, C_CARD_BG);
			cv->drawRoundRect(x, y, NUM_KEY_W, NUM_KEY_H, 8, C_TEXT_SUB);
			cv->setTextColor(C_TEXT_MAIN);
			const char str[2] = { k, '\0' };
			cv->getTextBounds(str, 0, 0, &bx, &by, &bw, &bh);
			cv->setCursor((int16_t)(x + (NUM_KEY_W - (int16_t)bw) / 2 - bx),
			              (int16_t)(y + (NUM_KEY_H - (int16_t)bh) / 2 - by));
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
		cv->getTextBounds(m, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((320 - bw) / 2, (int16_t)(y + 19));
		cv->print(m);
		return;
	}
	const int n = (_pinLen > PIN_MIN_LEN) ? _pinLen : PIN_MIN_LEN;
	const int spacing = 20;
	const int x0 = (320 - n * spacing) / 2 + spacing / 2;
	for (int i = 0; i < n; i++) {
		const int cx = x0 + i * spacing;
		if (i < _pinLen) cv->fillCircle(cx, (int16_t)(y + DOTS_H / 2), 6, C_ACCENT);
		else cv->drawCircle(cx, (int16_t)(y + DOTS_H / 2), 6, C_TEXT_SUB);
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
					if (pinIsIdentifying( )) {
						for (int k = 0; k < KEYS; k++) drawPinCardInto(cv, k, 0, oy);
					} else {
						drawPinPadInto(cv, oy);
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

	if (_pinCardsDirty) { _pinCardsDirty = false; if (pinIsIdentifying( )) blitPinCards( ); }
}

/* ────────────────────────────────────────────────────────────────────────── */
/* Per-sensor action menu                                                     */
/* ────────────────────────────────────────────────────────────────────────── */

void DisplayManager::showAlarmSensorMenu(int sensorIdx) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_SETTINGS_ALARM_SENSOR;
	_sensorMenuIdx = (int8_t)sensorIdx;
	/* open on the first row this session may use — a maintenance-only
	 * account landed on a padlocked "Limits" and its ENTER only beeped */
	const uint16_t need[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
	_sensorMenuSel = 0;
	for (int i = 0; i < 3; i++) { if (_panelPerms & need[i]) { _sensorMenuSel = (int8_t)i; break; } }
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
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

/* ────────────────────────────────────────────────────────────────────────── */
/* Maintenance window entry                                                   */
/* ────────────────────────────────────────────────────────────────────────── */

void DisplayManager::showMaintEntry(int sensorIdx) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_SETTINGS_MAINT;
	_maintSlot = (int8_t)sensorIdx;
	_maintHours = 1; _maintMinutes = 0; _maintFocus = 0;
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
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

/* ────────────────────────────────────────────────────────────────────────── */
/* User list                                                                  */
/* ────────────────────────────────────────────────────────────────────────── */

void DisplayManager::showSettingsUsers( ) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_SETTINGS_USERS;
	_usersCount = 0;
	if (_sysConfigPtr) {
		/* Slot 0, the admin, is not listed. Nothing here applies to it: its
		 * bits are every bit, it cannot be deleted, and its own PIN is the
		 * "change PIN" item of its own menu. A row that can only refuse is
		 * not a row. */
		for (int i = 1; i < MAX_USERS; i++) {
			if (_sysConfigPtr->users[i].active) _usersMap[_usersCount++] = i;
		}
	}
	_usersSel = 0; _usersPage = 0;
	_lastUsersPage = -1; _lastUsersSel = -1;
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
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

void DisplayManager::drawSettingsUsers( ) {
	if (!_driver.canvas || !_sysConfigPtr) return;
	const bool fullRedraw = _forceSettingsRedraw;
	const bool pageChanged = (_usersPage != _lastUsersPage);
	int totalPages = (_usersCount + 3) / 4; if (totalPages == 0) totalPages = 1;
	if (_usersPage >= totalPages) _usersPage = totalPages - 1;
	if (_usersPage < 0) _usersPage = 0;

	if (fullRedraw) {
		fastClearScreen(C_BG_MAIN);
		blitTitleBar(tr(TR_USERS_TITLE));
		blitFooterMenu(tr(TR_BACK), tr(TR_NEW_LBL));
	}
	if (fullRedraw || pageChanged) uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, _usersPage);

	const int startIdx = _usersPage * 4;
	const int itemW = 285;
	for (int i = 0; i < 4; i++) {
		const int mapIdx = startIdx + i;
		const int y = 40 + i * 38;
		if (!fullRedraw && !pageChanged && mapIdx != _usersSel && mapIdx != _lastUsersSel) continue;
		_driver.canvas->fillScreen(C_BG_MAIN);
		if (mapIdx < _usersCount) {
			const int slot = _usersMap[mapIdx];
			const UserAccount& u = _sysConfigPtr->users[slot];
			const bool sel = (mapIdx == _usersSel);
			const uint16_t bg = sel ? C_ACCENT : C_CARD_BG;
			const uint16_t txt = sel ? C_BG_MAIN : C_TEXT_MAIN;
			_driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
			if (!sel) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);
			_driver.canvas->setFont(&simutFont9pt);
			_driver.canvas->setTextColor(txt);
			char nameBuf[24];
			truncateText(_driver.canvas, u.username, nameBuf, sizeof(nameBuf), itemW - 120);
			_driver.canvas->setCursor(10, 24);
			_driver.canvas->print(nameBuf);
			/* a dot after the name says "has a PIN" — an account without one
			 * cannot act here, whatever its bits */
			if (StorageManager::userHasPin(u)) {
				int16_t bx, by; uint16_t bw, bh;
				_driver.canvas->getTextBounds(nameBuf, 0, 0, &bx, &by, &bw, &bh);
				_driver.canvas->fillCircle(10 + (int)bw + 8, 19, 3, sel ? C_BG_MAIN : C_ACCENT);
			}
			drawPermLetters(_driver.canvas, itemW - 10, 24, u.permissions, sel);
		}
		blitCanvas(_driver.canvas, 10, y, itemW, 34);
	}
	_forceSettingsRedraw = false; _lastUsersPage = _usersPage; _lastUsersSel = _usersSel;
}

/* ────────────────────────────────────────────────────────────────────────── */
/* User editor (existing account) / composer (new account)                    */
/* ────────────────────────────────────────────────────────────────────────── */

void DisplayManager::getNewName(char* out, size_t cap) const {
	if (!out || cap == 0) return;
	strncpy(out, _newUserName, cap - 1);
	out[cap - 1] = '\0';
}

void DisplayManager::showUserEdit(int slot, bool isNew) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_SETTINGS_USER_EDIT;
	_userEditNew = isNew;
	_userEditSlot = (int8_t)(isNew ? -1 : slot);
	/* an existing account starts from its stored bits; a new one keeps the
	 * bits chosen so far (the flow may come back here from a refused PIN) */
	if (!isNew && _sysConfigPtr && slot >= 0 && slot < MAX_USERS) {
		_userEditPerms = _sysConfigPtr->users[slot].permissions;
	}
	_userEditSel = 0; _userEditPage = 0; _lastUserEditPage = -1; _lastUserEditSel = -1;
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
}

/* Rows of the editor: the three panel bits, then Set PIN and Delete for an
 * existing account (the admin keeps its bits and cannot be deleted). */
int DisplayManager::userEditRowCount( ) const {
	if (_userEditNew) return 3;
	if (_userEditSlot == 0) return 4;      /* bits shown locked + set PIN */
	return 5;
}

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

/* ────────────────────────────────────────────────────────────────────────── */
/* Result message — what Core 0 says happened                                 */
/* ────────────────────────────────────────────────────────────────────────── */

void DisplayManager::showPanelMessage(bool ok, LangKey msg, UiMode returnTo) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_PANEL_MESSAGE;
	_msgOk = ok; _msgKey = msg; _msgReturn = returnTo;
	_pinWaiting = false;
	if (!ok) _errorSoundPending = true;
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
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

/* Where the message returns to. Only the modes a message can be reached
 * from; anything else is the dashboard, which is always safe. */
void DisplayManager::leavePanelMessage( ) {
	switch (_msgReturn) {
		case MODE_SETTINGS_MAIN:         showSettingsMain( ); break;
		case MODE_SETTINGS_USERS:        showSettingsUsers( ); break;
		case MODE_SETTINGS_ALARM_SENSOR: showAlarmSensorMenu(_sensorMenuIdx); break;
		case MODE_SETTINGS_ALARMS:       showSettingsAlarms(_sysConfigPtr); break;
		case MODE_SETTINGS_USER_EDIT:    showUserEdit(_userEditSlot, _userEditNew); break;
		default:                         forceDashboard( ); break;
	}
}

/* ────────────────────────────────────────────────────────────────────────── */
/* Touch — one function for every mode above; returns false when the mode is  */
/* not one of them so the historical chain in handleTouch( ) carries on.      */
/* ────────────────────────────────────────────────────────────────────────── */

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
			if (_permanentLockout || !timeReached(_lockoutUntil) || _pinWaiting) return true;
			if (x < FOOT_BACK_X + FOOT_BACK_W) {
				if (!acceptTouch(1)) return true;
				if (_pinLen > 0) {
					_pinLen--;
					if (pinIsIdentifying( )) { _pinTaps[_pinLen][0] = '\0'; scramblePinKeys( ); _pinCardsDirty = true; }
					else _pinBuf[_pinLen] = '\0';
				}
				_pinMsg = TR_KEYS_COUNT;
				_repaintSettings = true; return true;
			}
			if (x >= FOOT_OK_X) {
				if (!acceptTouch(2)) return true;
				pinSubmit( );
				_repaintSettings = true; return true;
			}
			return true;
		}
		if (_permanentLockout || !timeReached(_lockoutUntil) || _pinWaiting) return true;

		if (pinIsIdentifying( )) {
			/* The whole card is one target: the tap says "one of these three",
			 * and which one is never asked. What the tap keeps is the three
			 * glyphs themselves, because the deal is rolled right after and
			 * the card index would stop meaning anything. Core 0 resolves the
			 * sequence (StorageManager::findUserByPinSet). */
			const int k = keyAt(x, y);
			if (k < 0) return true;
			if (!acceptTouch((uint8_t)(10 + k))) return true;
			if (_pinLen < PIN_MAX_LEN) {
				memcpy(_pinTaps[_pinLen], _pinKeyChars[k], SLOTS + 1);
				_pinLen++;
				_pinMsg = TR_KEYS_COUNT;
			}
			/* Every tap gets its own deal: two taps on the same card are not
			 * the same three digits, so a watcher cannot even tell whether two
			 * digits of the PIN are equal. */
			scramblePinKeys( );
			_pinCardsDirty = true;
			_repaintSettings = true;
			return true;
		}

		/* Choosing a PIN: the ordered pad, one key one digit. */
		const char c = numKeyAt(x, y);
		if (!c) return true;
		if (!acceptTouch((uint8_t)(20 + (c - '0')))) return true;
		if (_pinLen < PIN_MAX_LEN) {
			_pinBuf[_pinLen++] = c; _pinBuf[_pinLen] = '\0';
			_pinMsg = TR_KEYS_COUNT;
		}
		_repaintSettings = true;
		return true;
	}

	if (_uiMode == MODE_PANEL_MESSAGE) {
		if (y >= 185) { if (!acceptTouch(0)) return true; leavePanelMessage( ); }
		return true;
	}

	if (_uiMode == MODE_SETTINGS_ALARM_SENSOR) {
		const uint16_t need[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
		auto activate = [&](int row) {
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
				if (!(_panelPerms & PERM_MAINT)) { _errorSoundPending = true; return true; }
				const int secs = open ? 0 : (_maintHours * 3600 + _maintMinutes * 60);
				if (!open && secs == 0) { _errorSoundPending = true; return true; }
				UiEvent ev; ev.type = UiEvent::EVT_MAINT_SET; ev.id = _maintSlot; ev.param = secs;
				pushUiEvent(ev);
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
		if (row != _maintFocus) { if (!acceptSlideTouch((uint8_t)(20 + row))) return true; _maintFocus = row; _repaintSettings = true; return true; }
		int delta = 0;
		if (x < ALARM_EDIT_HIT_DEC) delta = -1;
		else if (x > ALARM_EDIT_HIT_INC) delta = +1;
		if (delta == 0) return true;
		if (!acceptHoldTouch((uint8_t)(30 + row * 2 + (delta > 0)))) return true;
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
		return true;
	}

	if (_uiMode == MODE_SETTINGS_USERS) {
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
				showUserEdit(_usersMap[_usersSel], false);
			}
		} else if (y > 185) {
			if (x < 70) {
				if (!acceptHoldTouch(10)) return true;
				if (_usersSel > 0) _usersSel--; else _usersSel = (_usersCount > 0) ? _usersCount - 1 : 0;
				_usersPage = _usersSel / 4; _repaintSettings = true;
			} else if (x < 138) {
				if (!acceptHoldTouch(11)) return true;
				if (_usersSel < _usersCount - 1) _usersSel++; else _usersSel = 0;
				_usersPage = _usersSel / 4; _repaintSettings = true;
			} else if (x < 219) {
				if (!acceptTouch(12)) return true;
				showSettingsMain( );
			} else {
				if (!acceptTouch(13)) return true;
				if (!(_panelPerms & PERM_USER_MGR)) { _errorSoundPending = true; return true; }
				/* new account: name first, on the text keyboard */
				_userEditPerms = 0;
				_newUserName[0] = '\0';
				showSettingsPassword(1);
			}
		}
		return true;
	}

	if (_uiMode == MODE_SETTINGS_USER_EDIT) {
		const int rows = userEditRowCount( );
		const bool lockedBits = (!_userEditNew && _userEditSlot == 0);
		const uint16_t bits[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
		auto activate = [&](int idx) {
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
				if (_userEditNew) { showPinEntry(PIN_FOR_NEW_ACCOUNT); return true; }
				if (lockedBits) { showSettingsUsers( ); return true; }
				UiEvent ev; ev.type = UiEvent::EVT_USER_PERMS; ev.id = _userEditSlot;
				ev.param = (int)(_userEditPerms & PERM_PANEL_ALARM_ANY);
				pushUiEvent(ev);
			}
		}
		return true;
	}

	if (_uiMode == MODE_SETTINGS_USER_CONFIRM_DEL) {
		if (y >= 195) {
			if (x < 160) { if (!acceptTouch(0)) return true; showUserEdit(_userEditSlot, false); }
			else {
				if (!acceptTouch(1)) return true;
				UiEvent ev; ev.type = UiEvent::EVT_USER_DEL; ev.id = _userEditSlot; ev.param = 0;
				pushUiEvent(ev);
			}
		}
		return true;
	}

	return false;
}

#endif /* SIMUT_DISPLAY_TFT */
