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

/* Deals the alphabet, plus whatever decoys the grid has room for, over the
 * cards of the current layout.
 *
 * Fisher-Yates over every slot, so a character is as likely to land anywhere.
 * Decoys exist to make every card show the same number of glyphs: ten digits
 * over four cards of three is 3+3+2+2, and a card visibly shorter than its
 * neighbours would tell a watcher which cards carry more of the alphabet. They
 * are symbols because a letter there would read as something someone might be
 * expected to type. With ALPHA_ALNUM the alphabet fills the grid exactly and
 * none is dealt — which is why the wider alphabet is also the fairer deal.
 *
 * fastRandom( ) is the xorshift the pre-v24 scrambled keypad used, seeded from
 * micros( ). It is not a CSPRNG and does not need to be: what it hides is the
 * MEANING OF A TAP POSITION from someone watching the glass, and the PIN's
 * strength is its length and its alphabet, not this shuffle. Called with
 * _stateMutex held. */
void DisplayManager::scramblePinKeys( ) {
	using namespace PinKb;
	uint8_t minLen, keypad, alphabet;
	pinPolicy(minLen, keypad, alphabet);
	const Grid g = gridFor(alphabet, keypad);

	/* One glyph per key, or a screen for CHOOSING a PIN: both want the
	 * characters where the operator expects them, so there is nothing to
	 * shuffle. A shuffle at S=1 hides the character from nobody — it is
	 * printed on the key that was pressed — and only costs muscle memory.
	 * The popup closes with the layout, so a re-deal can never leave one
	 * open over a keyboard that has changed under it. */
	if (isOrdered(keypad) || !pinIsIdentifying( )) {
		/* orderedGrid( ) and NOT `g`: when the policy names a DEALT keypad
		 * but this screen is the PIN-CHOOSING one, gridFor( ) answers with
		 * the dealt layout (4 cards) while the renderer asks pinGrid( ),
		 * which answers with the ordered one (12 keys). Filling `g.keys`
		 * faces would leave the last eight empty and draw a numeric pad with
		 * four keys on it. */
		const Grid og = orderedGrid(alphabet);
		for (int k = 0; k < (int)og.keys && k < KEYS_MAX; k++)
			orderedFace(alphabet, k, _pinKeyChars[k], FACE_MAX + 1);
		_pinPopup = -1;
		return;
	}

	const int total = (int)g.keys * (int)g.slots;
	const int nChars = alphaCount(alphabet);
	const char* set = alphaChars(alphabet);

	char all[DEAL_MAX];
	for (int i = 0; i < nChars; i++) all[i] = set[i];
	/* distinct decoys, so a repeated symbol cannot be read as a pattern */
	for (int d = 0; d < total - nChars; d++) {
		char c;
		bool dup;
		do {
			c = DECOY_POOL[fastRandom(DECOY_POOL_N)];
			dup = false;
			for (int p = 0; p < d; p++) if (all[nChars + p] == c) dup = true;
		} while (dup);
		all[nChars + d] = c;
	}
	for (int i = total - 1; i > 0; i--) {
		const int j = (int)fastRandom((uint32_t)(i + 1));
		const char tmp = all[i]; all[i] = all[j]; all[j] = tmp;
	}
	for (int k = 0; k < (int)g.keys; k++) {
		for (int sl = 0; sl < (int)g.slots; sl++) _pinKeyChars[k][sl] = all[k * g.slots + sl];
		_pinKeyChars[k][g.slots] = '\0';
	}
}

/* One character into whichever buffer this screen is filling. Both ordered
 * keyboards produce a CHARACTER rather than a card, so identification and
 * choosing differ only in where it lands — and a tap of one glyph leaves
 * Core 0's search a straight line, which is why KB_PLAIN's length ceiling is
 * the buffer (16) and not the clock. Called with _stateMutex held. */
void DisplayManager::pinAppendChar(char c) {
	using namespace PinKb;
	if (!c || _pinLen >= maxLenFor(pinKeypadMode( )) || _pinLen >= PIN_MAX_LEN) return;
	if (pinIsIdentifying( )) {
		_pinTaps[_pinLen][0] = c;
		_pinTaps[_pinLen][1] = '\0';
	} else {
		_pinBuf[_pinLen] = c;
		_pinBuf[_pinLen + 1] = '\0';
	}
	_pinLen++;
	_pinMsg = TR_KEYS_COUNT;
}

/* ── The policy, read once and clamped once ─────────────────────────────── */

void DisplayManager::pinPolicy(uint8_t& minLen, uint8_t& keypad, uint8_t& alphabet) const {
	minLen = keypad = alphabet = 0;
	if (_sysConfigPtr) {
		minLen   = _sysConfigPtr->pinAuth.pinMinLen;
		keypad   = _sysConfigPtr->pinAuth.pinKeypad;
		alphabet = _sysConfigPtr->pinAuth.pinAlphabet;
	}
	clampPinPolicy(minLen, keypad, alphabet);
}

/* The layout ON THE GLASS, which is not always the policy's. CHOOSING a PIN
 * is ordered whatever keypad the policy names — the operator is picking a PIN,
 * not proving one, and hunting a character through a shuffled deal only costs
 * taps. So this asks the purpose before it asks the policy. */
PinKb::Grid DisplayManager::pinGrid( ) const {
	uint8_t minLen, keypad, alphabet;
	pinPolicy(minLen, keypad, alphabet);
	if (!pinIsIdentifying( )) return PinKb::orderedGrid(alphabet);
	return PinKb::gridFor(alphabet, keypad);
}

/** Is the ordered keyboard the one on screen right now? */
bool DisplayManager::pinOrderedNow( ) const {
	return !pinIsIdentifying( ) || PinKb::isOrdered(pinKeypadMode( ));
}

uint8_t DisplayManager::pinKeypadMode( ) const {
	uint8_t minLen, keypad, alphabet; pinPolicy(minLen, keypad, alphabet); return keypad;
}
uint8_t DisplayManager::pinAlphabet( ) const {
	uint8_t minLen, keypad, alphabet; pinPolicy(minLen, keypad, alphabet); return alphabet;
}
uint8_t DisplayManager::pinMinLen( ) const {
	uint8_t minLen, keypad, alphabet; pinPolicy(minLen, keypad, alphabet); return minLen;
}

/* The four cards, for `show display keypad`. The bench drives the panel from
 * outside and cannot find a digit whose position it was never told; the cards
 * are on the glass for anyone standing there, so printing them to a privileged
 * CLI gives an attacker nothing it does not already have. */
uint8_t DisplayManager::getEnteredPinTaps(char out[][PinKb::SLOTS_MAX + 1], size_t cap) const {
	if (!out || cap == 0) return 0;
	const uint8_t n = (_pinLen > (uint8_t)cap) ? (uint8_t)cap : _pinLen;
	for (uint8_t i = 0; i < n; i++) memcpy(out[i], _pinTaps[i], PinKb::SLOTS_MAX + 1);
	return n;
}

uint8_t DisplayManager::pinKeyFace(int key, char* out, size_t cap) const {
	if (!out || cap == 0) return 0;
	out[0] = '\0';
	const PinKb::Grid g = pinGrid( );
	/* Not gated on pinIsIdentifying( ) any more: since the ordered keyboards
	 * arrived, the PIN-CHOOSING screen also has faces worth reading, and a
	 * bench that had to hard-code the numeric pad's coordinates is a bench
	 * that breaks the day the layout moves. */
	if (key < 0 || key >= (int)g.keys || _uiMode != MODE_AUTH) return 0;
	const size_t n = strlen(_pinKeyChars[key]);
	if (n + 1 > cap) return 0;
	memcpy(out, _pinKeyChars[key], n + 1);
	return (uint8_t)n;
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

/* The ladder, v25: it is the account's, not the panel's.
 *
 * Two free tries, then 5 s, 15 s, 60 s, and at SLOT_FAIL_MAX the ACCOUNT is
 * out until reboot. The panel-wide count survives as a ceiling only
 * (PANEL_FAIL_CEILING), because the two failure modes are different attacks:
 * until v25 the ladder was global, so six wrong taps from anybody shut the
 * panel for the whole shift — a denial of service that cost an attacker six
 * touches — while a purely per-account ladder would hand that same attacker
 * MAX_USERS x 6 attempts, which is the table-size factor the account picker
 * exists to remove. @return the rung reached, for the log. */
int DisplayManager::pinFailLadder(int8_t slot) {
	_failedAttempts++;
	int fails = _failedAttempts;
	if (slot >= 0 && slot < MAX_USERS) {
		if (_failBySlot[slot] < 255) _failBySlot[slot]++;
		fails = (int)_failBySlot[slot];
		if (fails >= (int)PinKb::SLOT_FAIL_MAX) _slotLocked |= (1u << (unsigned)slot);
	}
	if (_failedAttempts >= (int)PinKb::PANEL_FAIL_CEILING) {
		_permanentLockout = true;
		_lockoutUntil = millis( ) + 10000;
		return fails;
	}
	if (fails <= 2)      _lockoutUntil = 0;
	else if (fails == 3) _lockoutUntil = millis( ) + 5000;
	else if (fails == 4) _lockoutUntil = millis( ) + 15000;
	else                 _lockoutUntil = millis( ) + 60000;
	return fails;
}

/* Core 0's verdict on the PIN it took. The count is returned so Core 0 can
 * log the rung that was reached. */
int DisplayManager::authResult(bool ok) {
	mutex_enter_blocking(&_stateMutex);
	_pinWaiting = false;
	_pinLen = 0; _pinBuf[0] = '\0';
	memset(_pinTaps, 0, sizeof(_pinTaps));
	int failures = 0;
	if (ok) {
		_failedAttempts = 0;
		if (_pinTarget >= 0 && _pinTarget < MAX_USERS) _failBySlot[_pinTarget] = 0;
		_authFailed = false;
	} else {
		_authFailed = true;
		_errorSoundPending = true;
		failures = pinFailLadder(pinIsIdentifying( ) ? _pinTarget : (int8_t)-1);
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
	if (_pinLen < pinMinLen( )) {
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
		/* NOT a re-deal: this screen is the ORDERED keyboard, and it always
		 * was — the comment here used to claim the confirmation ran on a
		 * fresh deal so a watcher could not match it against the first pass,
		 * which was never true of a screen that does not draw the deal. What
		 * the call does is reset the layout state, which is what closes an
		 * alphanumeric popup left open between the two passes. */
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
		cv->getTextBounds(m, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((320 - bw) / 2, (int16_t)(y + 19));
		cv->print(m);
		return;
	}
	/* The row is as wide as the policy's minimum, or as what has been typed
	 * past it. Sixteen dots at the old 20-px pitch is 320 px, one wider than
	 * the safe area allows (measured 2026-09-20), so both pitch and radius
	 * step down past fourteen. */
	const int floorLen = (int)pinMinLen( );
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
/* v25 — who is at the panel                                                  */
/* ────────────────────────────────────────────────────────────────────────── */

/* The account comes first now, and the PIN only proves it.
 *
 * Identifying BY the PIN made one entry a search over the whole table: the
 * S^n strings a tap sequence can spell were being matched against 32 digests
 * at once, so a blind entry landed on SOMEBODY 20.2% of the time with four
 * digits and a full table, and two accounts inside the same entry refused
 * both (13.5% expected, 15% measured on the rig 2026-09-19). Picking the
 * account divides the first number by the table size and takes the second to
 * zero — the tree is the same size, it is just compared against one digest.
 *
 * What it costs is a list of names on the glass, which the PIN-as-identity
 * design did not publish. That is the trade, and it is the admin's to see. */
void DisplayManager::showAuthUser( ) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_AUTH_USER;
	_usersCount = 0;
	if (_sysConfigPtr) {
		/* Only accounts that can finish the flow are listed: one without a PIN
		 * has nothing to type. The admin IS listed — this is the one screen
		 * where it has to be, because it is the one screen that asks who. */
		for (int i = 0; i < MAX_USERS; i++) {
			if (_sysConfigPtr->users[i].active &&
			    StorageManager::userHasPin(_sysConfigPtr->users[i])) {
				_usersMap[_usersCount++] = i;
			}
		}
	}
	_usersSel = 0; _usersPage = 0;
	_lastUsersPage = -1; _lastUsersSel = -1;
	_pinTarget = -1;
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::drawAuthUser( ) { drawUserList(true); }

/* Open the keypad for `slot`, unless that account is out of tries. The guard
 * lives here and not after the PIN: taps refused afterwards read to the
 * operator as a wrong PIN, which is the one thing they are not. */
void DisplayManager::enterPinFor(int slot) {
	if (slot < 0 || slot >= MAX_USERS || (_slotLocked & (1u << (unsigned)slot))) {
		_errorSoundPending = true;
		return;
	}
	showPinEntry(PIN_FOR_AUTH, (int8_t)slot);
}

/* ────────────────────────────────────────────────────────────────────────── */
/* v25 — the PIN policy editor                                                */
/* ────────────────────────────────────────────────────────────────────────── */

/* The editor works on a COPY of the stored policy, taken here; Core 0 sees the
 * result only when SAVE sends it. It used to edit the live struct, so that the
 * rows could show the clamping as it happened, and each consequence of that
 * was measured on the rig on 2026-09-24:
 *  - SAVE compared the new policy with ITSELF. Core 0 reads the old values out
 *    of that same struct to decide whose PIN no longer complies, so raising the
 *    length 4 -> 5 here logged "0 to renew" with six accounts holding a PIN.
 *    The web and `user policy` read the old values first and were not affected.
 *  - one tap, unsaved, already WAS the policy: /api/config read 5 before SAVE,
 *    and still read 5 after the 30 s idle guard had taken the panel home.
 *  - leaving without saving had to put it back, and did it by re-reading the
 *    whole config from flash: an account added over the CLI and not yet
 *    written was gone after one BACK.
 * A copy has none of the three and costs three bytes. */
void DisplayManager::showPinPolicy( ) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_SETTINGS_PIN_POLICY;
	_policySel = 0; _lastPolicySel = -1;
	pinPolicy(_policyMin, _policyKb, _policyAlpha);
	_forceSettingsRedraw = true; _repaintSettings = true;
	mutex_exit(&_stateMutex);
}

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
	 * "0-9 A-Z" for the alphabet. They read the same in all eight languages,
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
			/* The footer is outside the popup card, so reaching it means the
			 * popup was abandoned: close it before acting, or the next full
			 * redraw would bring it back over the keyboard. */
			if (_pinPopup >= 0) { _pinPopup = -1; _forceSettingsRedraw = true; }
			if (x < FOOT_BACK_X + FOOT_BACK_W) {
				if (!acceptTouch(1)) return true;
				if (_pinLen > 0) {
					_pinLen--;
					if (pinIsIdentifying( )) {
						_pinTaps[_pinLen][0] = '\0';
						/* Nothing to re-deal on an ordered keyboard, and no
						 * cards to blit over it. */
						if (!pinOrderedNow( )) { scramblePinKeys( ); _pinCardsDirty = true; }
					}
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
				if (i >= 0 && i < n) pinAppendChar(mem[i]);
				_pinPopup = -1;
				_forceSettingsRedraw = true;
				_repaintSettings = true;
				return true;
			}

			const int k = keyAt(g, x, y);
			/* An empty face is one of the numeric pad's two blank cells. */
			if (k < 0 || !_pinKeyChars[k][0]) return true;
			if (!acceptTouch((uint8_t)(20 + k))) return true;
			if (alphabet == ALPHA_ALNUM) {
				_pinPopup = (int8_t)k;          /* first tap: pick the group */
				_forceSettingsRedraw = true;
			} else {
				pinAppendChar(_pinKeyChars[k][0]);
			}
			_repaintSettings = true;
			return true;
		}

		{
			/* The whole card is one target: the tap says "one of these three",
			 * and which one is never asked. What the tap keeps is the three
			 * glyphs themselves, because the deal is rolled right after and
			 * the card index would stop meaning anything. Core 0 resolves the
			 * sequence (StorageManager::pinSetMatches). */
			const Grid g = pinGrid( );
			const int k = keyAt(g, x, y);
			if (k < 0) return true;
			if (!acceptTouch((uint8_t)(10 + k))) return true;
			/* The ceiling is the KEYPAD's, not the buffer's: resolving the tap
			 * tree costs S^n hashes and maxLenFor( ) is where that budget is
			 * written down. Past it the tap is simply not taken. */
			if (_pinLen < maxLenFor(pinKeypadMode( ))) {
				memcpy(_pinTaps[_pinLen], _pinKeyChars[k], (size_t)g.slots + 1);
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
				if (picking) enterPinFor(_usersMap[_usersSel]);
				else         showUserEdit(_usersMap[_usersSel], false);
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
					if (!(_panelPerms & PERM_USER_MGR)) { _errorSoundPending = true; return true; }
					/* new account: name first, on the text keyboard */
					_userEditPerms = 0;
					_newUserName[0] = '\0';
					showSettingsPassword(1);
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
			clampPinPolicy(minLen, keypad, alphabet);
			_policyMin = minLen; _policyKb = keypad; _policyAlpha = alphabet;
			/* The rows only: see drawPinPolicy( ). */
			_repaintSettings = true;
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
				UiEvent ev; ev.type = UiEvent::EVT_PIN_POLICY;
				ev.id = (int)_policyMin; ev.param = (int)_policyKb * 16 + (int)_policyAlpha;
				pushUiEvent(ev);
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
