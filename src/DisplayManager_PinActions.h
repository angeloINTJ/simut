/**
 * @file DisplayManager_PinActions.h
 * @brief The PIN keypad's touch actions, which both UI drivers run: the predicate that holds every key but Cancelar while a lockout runs or
 *        Core 0 reads the entry, and the six controls (stage S9 of docs/analysis/UI_DRIVERS.md, section 2.13).
 *
 * What makes the keypad safe is not here and does not move: the lockout ladder (pinFailLadder( ), authResult( )), the deal
 * (scramblePinKeys( )), Core 0's digest comparison (panelIdentify( ) and StorageManager::pinSetMatches( )) and what is logged.
 *
 * The keypad exists only where a panel proves a PIN, and SIMUT_PANEL_PIN follows the TFT panel (SystemDefs_Limits.h). So this header has
 * no #if: only TFT-only units include it, DisplayManager_Users.cpp inside its SIMUT_DISPLAY_TFT section and the Ângulo's
 * DisplayManager_UiA_Identidade.cpp, after DisplayManager_Actions.h in both. Until 2026-10-21 the block stood under an #if in
 * DisplayManager_Actions.h, which every image compiles through DisplayManager_Touch.cpp; the feature-sprawl gate
 * (tools/check_feature_sprawl.py) refuses a new site in a shared file, and the static_assert says to the compiler what the #if said.
 */
#pragma once

#include "DisplayManager.h"

static_assert(SIMUT_PANEL_PIN, "the PIN keypad's actions exist only where a panel proves a PIN: include this header from a TFT-only unit");

/* Every control of the keypad but Cancelar waits while a lockout runs (a timed rung or the permanent one) or while Core 0 reads the entry
 * (_pinWaiting): Cancelar is the way out of the screen and the only control that has to work there, so each driver tests it first. */
inline __attribute__((always_inline)) bool DisplayManager::pinInputBlocked( ) const {
	return _permanentLockout || !timeReached(_lockoutUntil) || _pinWaiting;
}

/* A touch on the footer, which lies outside the group popup, means the popup was abandoned: close it before acting, or the next full redraw
 * would bring it back over the keyboard. The caller has passed pinInputBlocked( ). */
inline __attribute__((always_inline)) void DisplayManager::actPinPopupDrop( ) {
	if (_pinPopup >= 0) { _pinPopup = -1; _forceSettingsRedraw = true; }
}

/* Apagar (the caller holds acceptTouch( 1 )): one tap, or one character, less. On a dealt keypad the deal is rolled again, so the cards left on
 * the glass say nothing about the tap taken back; an ordered keyboard has nothing to re-deal and no cards to blit. */
inline __attribute__((always_inline)) void DisplayManager::actPinBackspace( ) {
	if (_pinLen > 0) {
		_pinLen--;
		if (pinIsIdentifying( )) {
			_pinTaps[_pinLen][0] = '\0';
			if (!pinOrderedNow( )) { scramblePinKeys( ); _pinCardsDirty = true; }
		}
		else _pinBuf[_pinLen] = '\0';
	}
	_pinMsg = TR_KEYS_COUNT;
	_repaintSettings = true;
}

/* Entrar (the caller holds acceptTouch( 2 )): pinSubmit( ) hands the entry to Core 0, or asks for the second pass of a new PIN. */
inline __attribute__((always_inline)) void DisplayManager::actPinOk( ) {
	pinSubmit( );
	_repaintSettings = true;
}

/* A touch while the group popup of the alphanumeric keyboard is up (the caller holds acceptTouch( 40 + i + 1 ), or 40 for no key): key i of
 * the popup's n characters yields its character, any other touch (i < 0) only closes it. The popup owns every touch, so a fat finger on the
 * edge of the card cannot type a character from the group behind it. mem and n are the caller's reads of PinKb::GROUP_CHARS[_pinPopup]. */
inline __attribute__((always_inline)) void DisplayManager::actPinPopupKey( const char *mem, int n, int i ) {
	if (i >= 0 && i < n) pinAppendChar(mem[i]);
	_pinPopup = -1;
	_forceSettingsRedraw = true;
	_repaintSettings = true;
}

/* Key k of the ordered keyboard (the caller holds acceptTouch( 20 + k ) and has refused an empty face, one of the numeric pad's two blank
 * cells): on the alphanumeric alphabet a key is a group, and the first tap opens its popup; otherwise the key's character is typed. */
inline __attribute__((always_inline)) void DisplayManager::actPinOrderedKey( int k, uint8_t alphabet ) {
	if (alphabet == PinKb::ALPHA_ALNUM) {
		_pinPopup = (int8_t)k;          /* first tap: pick the group */
		_forceSettingsRedraw = true;
	} else {
		pinAppendChar(_pinKeyChars[k][0]);
	}
	_repaintSettings = true;
}

/* Card k of a dealt keypad (the caller holds acceptTouch( 10 + k )). The whole card is one target: the tap says "one of these glyphs", and
 * which one is never asked. What the tap keeps is the glyphs themselves, because the deal is rolled right after and the card's index would
 * stop meaning anything; Core 0 resolves the sequence (StorageManager::pinSetMatches( )). The ceiling is the KEYPAD's, not the buffer's:
 * resolving the tap tree costs S^n hashes, and maxLenFor( ) is where that budget is written down, so past it the tap is not taken. Every tap
 * gets its own deal: two taps on the same card are not the same glyphs, so a watcher cannot even tell whether two digits of the PIN are equal. */
inline __attribute__((always_inline)) void DisplayManager::actPinCard( const PinKb::Grid &g, int k ) {
	if (_pinLen < PinKb::maxLenFor(pinKeypadMode( ))) {
		memcpy(_pinTaps[_pinLen], _pinKeyChars[k], (size_t)g.slots + 1);
		_pinLen++;
		_pinMsg = TR_KEYS_COUNT;
	}
	scramblePinKeys( );
	_pinCardsDirty = true;
	_repaintSettings = true;
}
