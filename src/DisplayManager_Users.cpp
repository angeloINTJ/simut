/**
 * @file DisplayManager_Users.cpp
 * @brief v24 — identity at the panel, the core half (both UIs run it): the PIN
 *        state and its fail ladder, the session and the show*( ) entry points.
 *        The original's draws and touch hand-off: DisplayManager_UsersUi.cpp.
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
#include "display/CountInText.h"     /* the minimum inside the too-short line */
#include "sensors/SensorChannelTable.h"
/* Not included here: the core's touch actions (DisplayManager_Actions.h, DisplayManager_PinActions.h) and the hooks they lean on (ui/UiDriverHooks.h).
 * handleTouchPanelV24( ), the only code of this unit that ran them, moved to DisplayManager_UsersUi.cpp (S10c, D6a), which takes the three, hooks first;
 * a function added here that calls an action takes them the same way. Dropped in D6b: the eleven images stayed sha256-equal to D6a's (2026-10-28). */

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
uint8_t DisplayManager::pinEntryMinLen( ) const {
	return PinKb::entryMinLen(pinIsIdentifying( ), pinMinLen( ));
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

void DisplayManager::showPinEntry(uint8_t purpose, int8_t targetUser, bool forced) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_AUTH;
	_pinPurpose = purpose;
	_pinTarget = targetUser;
	_pinForced = forced;
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

/* Cancel from the keypad: back to wherever this PIN was asked from. A change
 * the account was sent to by force was asked from nowhere: the menu used to
 * open behind it, with the session's every bit and the PIN still the one the
 * policy or the factory left (finding 9 of docs/analysis/PLANO_REVISAO_EXTERNA.md).
 * It leaves to the dashboard, and Settings asks for the PIN again from there,
 * which sends the account back to the change. */
void DisplayManager::pinCancel( ) {
	clearEnteredPin( );
	switch (_pinPurpose) {
		case PIN_FOR_OWN:          if (_pinForced) forceDashboard( ); else showSettingsMain( ); break;
		case PIN_FOR_USER:         showUserEdit(_pinTarget, false); break;
		case PIN_FOR_NEW_ACCOUNT:  showUserEdit(-1, true); break;
		default:                   forceDashboard( ); break;
	}
}

/* OK from the keypad. For identification the PIN goes to Core 0 as is; for a
 * new PIN it is typed twice, compared here, and only then handed over. */
void DisplayManager::pinSubmit( ) {
	if (_pinLen < pinEntryMinLen( )) {
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

/* ────────────────────────────────────────────────────────────────────────── */
/* Touch — handleTouchPanelV24( ), one function for every mode above, is in   */
/* DisplayManager_UsersUi.cpp: only the original's touch chain calls it.      */
/* ────────────────────────────────────────────────────────────────────────── */

#endif /* SIMUT_DISPLAY_TFT */
