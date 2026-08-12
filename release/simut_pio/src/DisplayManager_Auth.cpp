/**
 * @file DisplayManager_Auth.cpp
 * @brief Authentication keypad: PIN entry, scrambled layout, lockout.
 * @details readPixel/readRow are blur effect helpers; fastRandom is a local
 * PRNG for scramble; scrambleKeys reorganizes the keypad layout.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "UiWidgets.h"

/* Volatile globals for incremental auth screen redraw.
 * Draw functions use these to detect state changes.
 * Volatile forces writes to remain (prevents DSE). */
namespace {
	volatile int g_lastAuthStep = -1;
	volatile bool g_lastAuthFailed = false;
	volatile bool g_keypadDirty = true;
}

uint16_t DisplayManager::readPixel(int16_t x, int16_t y) {
	if (!_driver.tft) return 0;
	_driver.tft->startWrite( ); _driver.tft->setAddrWindow(x, y, 1, 1); _driver.tft->endWrite( );
	SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
	digitalWrite(TFT_CS, LOW);
	digitalWrite(TFT_DC, LOW); SPI.transfer(0x2E);
	digitalWrite(TFT_DC, HIGH); SPI.transfer(0x00);
	uint8_t r = SPI.transfer(0x00); uint8_t g = SPI.transfer(0x00); uint8_t b = SPI.transfer(0x00);
	digitalWrite(TFT_CS, HIGH); SPI.endTransaction( );
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}


void DisplayManager::readRow(int16_t y, uint16_t* buffer, int16_t w) {
	if (!_driver.tft || !buffer) return;


	_driver.tft->startWrite( );
	_driver.tft->setAddrWindow(0, y, w, 1);
	_driver.tft->endWrite( );


	SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
	digitalWrite(TFT_CS, LOW);
	digitalWrite(TFT_DC, LOW);
	SPI.transfer(0x2E);
	digitalWrite(TFT_DC, HIGH);
	SPI.transfer(0x00);

	for (int16_t x = 0; x < w; x++) {
		uint8_t r = SPI.transfer(0x00);
		uint8_t g = SPI.transfer(0x00);
		uint8_t b = SPI.transfer(0x00);
		buffer[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
	}

	digitalWrite(TFT_CS, HIGH);
	SPI.endTransaction( );
}

uint32_t DisplayManager::fastRandom(uint32_t maxVal) {
	_rngState ^= _rngState << 13; _rngState ^= _rngState >> 17; _rngState ^= _rngState << 5;
	return _rngState % maxVal;
}

void DisplayManager::scrambleKeys( ) {
	const char poolNum[] = "0123456789";
	const char poolUpper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char poolLower[] = "abcdefghijklmnopqrstuvwxyz";
	const char poolSpec[] = "!@#$%^&*( )_+-=[]{}|;':\",./<>?\\~";
	char expected = '\0'; if ((size_t)_authStep < _expectedPin.length( )) { expected = _expectedPin[_authStep]; }
	int expectedType = -1;
	if (expected >= '0' && expected <= '9') expectedType = 0;
	else if (expected >= 'A' && expected <= 'Z') expectedType = 1;
	else if (expected >= 'a' && expected <= 'z') expectedType = 2;
	else if (expected != '\0') expectedType = 3;
	int correctBtn = -1; if (expected != '\0') correctBtn = fastRandom(4);
	for (int i = 0; i < 4; i++) {
		char chars[4];
		if (i == correctBtn && expectedType == 0) chars[0] = expected; else do { chars[0] = poolNum[fastRandom(sizeof(poolNum)-1)]; } while(chars[0] == expected);
		if (i == correctBtn && expectedType == 1) chars[1] = expected; else do { chars[1] = poolUpper[fastRandom(sizeof(poolUpper)-1)]; } while(chars[1] == expected);
		if (i == correctBtn && expectedType == 2) chars[2] = expected; else do { chars[2] = poolLower[fastRandom(sizeof(poolLower)-1)]; } while(chars[2] == expected);
		if (i == correctBtn && expectedType == 3) chars[3] = expected; else do { chars[3] = poolSpec[fastRandom(sizeof(poolSpec)-1)]; } while(chars[3] == expected);
		for (int k = 3; k > 0; k--) { int j = fastRandom(k + 1); char temp = chars[k]; chars[k] = chars[j]; chars[j] = temp; }
		_keypadChars[i][0] = chars[0]; _keypadChars[i][1] = chars[1]; _keypadChars[i][2] = chars[2]; _keypadChars[i][3] = chars[3]; _keypadChars[i][4] = '\0';
	}
	g_keypadDirty = true;
}

/* Public setter to repaint the keypad without resetting the PIN/state. */
void DisplayManager::requestAuthKeypadRedraw( ) {
	g_keypadDirty = true;
	g_lastAuthStep = -1; /* forces redraw of PIN dots too */
	g_lastAuthFailed = false;
}

void DisplayManager::showAuthScreen(String expectedPin) {
	mutex_enter_blocking(&_stateMutex);
	_uiMode = MODE_AUTH; _forceSettingsRedraw = true; _repaintSettings = true;
	g_lastAuthStep = -1; g_lastAuthFailed = false; g_keypadDirty = true;
	if (_permanentLockout) { _lockoutUntil = millis( ) + 10000; } else {
		_expectedPin = expectedPin; _authStep = 0; _authFailed = false; _isCurrentAttemptValid = true;
		_rngState = micros( ) ^ 0xA5A5A5A5; if (_rngState == 0) _rngState = 1;
		scrambleKeys( );
	}
	mutex_exit(&_stateMutex);
}

/* 6 strips of 40px (= 240px). Caller decides bgColor
 * (red for permanent lockout, BG_MAIN for others) and whether central messages
 * are needed (permanent lockout only). Returns true if via canvas, false if OOM
 * (caller continues with _tft directly).
 *
 * Strip map (each 40px of screen):
 * 0 (y=0..39): title bar (CARD_BG rect y=4..36, text y=22)
 * 1 (y=40..79): empty bg
 * 2 (y=80..119): optional msg1 (TR_ACCESS_BLOCKED, font 12pt at y=110 -> canvas y=30)
 * 3 (y=120..159): optional msg2 (TR_REBOOT_REQ, font 9pt at y=140 -> canvas y=20)
 * 4 (y=160..199): empty bg
 * 5 (y=200..239): cancel button (y=202..234 -> canvas y=2..34) + license button */
static inline bool drawAuthChromeViaStrips(DisplayManager* dm, GFXcanvas16* cv,
                                            uint16_t bgColor,
                                            const String& titleTxt,
                                            const String& cancelTxt,
                                            const String& licTxt,
                                            const String* msg1, const String* msg2) {
	if (!cv) return false;
	int16_t bx, by; uint16_t bw, bh;

	/* Strip 0 (y=0..39): title bar */
	cv->fillScreen(bgColor);
	uiTitleBar(cv, 4, titleTxt.c_str( ));
	dm->commitScreenStrip(0);

	/* Strip 1 (y=40..79): empty */
	cv->fillScreen(bgColor);
	dm->commitScreenStrip(1);

	/* Strip 2 (y=80..119): msg1 at y=110 (font 12pt) -> canvas y=30 */
	cv->fillScreen(bgColor);
	if (msg1) {
		cv->setFont(&simutFont12pt); cv->setTextColor(C_BG_MAIN);
		cv->getTextBounds(*msg1, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((320 - bw) / 2, 30); cv->print(*msg1);
	}
	dm->commitScreenStrip(2);

	/* Strip 3 (y=120..159): msg2 at y=140 (font 9pt) -> canvas y=20 */
	cv->fillScreen(bgColor);
	if (msg2) {
		cv->setFont(&simutFont9pt); cv->setTextColor(C_BG_MAIN);
		cv->getTextBounds(*msg2, 0, 0, &bx, &by, &bw, &bh);
		cv->setCursor((320 - bw) / 2, 20); cv->print(*msg2);
	}
	dm->commitScreenStrip(3);

	/* Strip 4 (y=160..199): empty */
	cv->fillScreen(bgColor);
	dm->commitScreenStrip(4);

	/* Strip 5 (y=200..239): cancel + license buttons at y=202 -> canvas y=2..34 */
	cv->fillScreen(bgColor);
	uiButton(cv, 10, 2, 110, 32, cancelTxt.c_str( ), UI_BTN_SECONDARY);
	uiButton(cv, 200, 2, 110, 32, licTxt.c_str( ), UI_BTN_SECONDARY);
	dm->commitScreenStrip(5);

	dm->endScreenRender( );
	return true;
}

void DisplayManager::drawAuthScreen( ) {
	int16_t bx, by; uint16_t bw, bh;
	String titleTxt = tr(TR_AUTH_TITLE); String cancelTxt = tr(TR_CANCEL);
	String licTxt = tr(TR_LICENSE_TITLE);

	if (_permanentLockout) {
		if (_forceSettingsRedraw) {
			String msg1 = tr(TR_ACCESS_BLOCKED);
			String msg2 = tr(TR_REBOOT_REQ);
			drawAuthChromeViaStrips(this, beginScreenRender( ),
			                        C_TEMP_HOT, titleTxt, cancelTxt, licTxt, &msg1, &msg2);
			_forceSettingsRedraw = false;
		}
		return;
	}

	if (_lockoutUntil > 0 && !timeReached(_lockoutUntil)) {
		static long lastSec = -1;
		if (_forceSettingsRedraw) {
			drawAuthChromeViaStrips(this, beginScreenRender( ),
			                        C_BG_MAIN, titleTxt, cancelTxt, licTxt, nullptr, nullptr);
			_forceSettingsRedraw = false; lastSec = -1;
		}
		long secondsLeft = (long)(timeRemaining(_lockoutUntil) / 1000) + 1;
		if (secondsLeft != lastSec) {
			lastSec = secondsLeft;
			_driver.canvas->fillScreen(C_BG_MAIN); _driver.canvas->setFont(&simutFont12pt); _driver.canvas->setTextColor(C_TEMP_WARM);
			String txt1 = tr(TR_ATTEMPTS_EXCEEDED); _driver.canvas->getTextBounds(txt1, 0, 0, &bx, &by, &bw, &bh);
			_driver.canvas->setCursor((320 - bw) / 2, 25); _driver.canvas->print(txt1); blitCanvas(_driver.canvas, 0, 90, 320, 45);
			_driver.canvas->fillScreen(C_BG_MAIN); char timeStr[64]; snprintf(timeStr, sizeof(timeStr), tr(TR_WAIT_SECONDS), secondsLeft);
			_driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(C_TEXT_SUB);
			_driver.canvas->getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh); _driver.canvas->setCursor((320 - bw) / 2, 25); _driver.canvas->print(timeStr);
			blitCanvas(_driver.canvas, 0, 135, 320, 45);
		}
		return;
	}

	if (_forceSettingsRedraw) {
		drawAuthChromeViaStrips(this, beginScreenRender( ),
		                        C_BG_MAIN, titleTxt, cancelTxt, licTxt, nullptr, nullptr);
		_forceSettingsRedraw = false;
	}

	/* Dots only repaint when _authStep or _authFailed change. */
	bool dotsChanged = (g_lastAuthStep != _authStep) || (g_lastAuthFailed != _authFailed);
	if (dotsChanged) {
		_driver.canvas->fillScreen(C_BG_MAIN);
		if (_authFailed) {
			_driver.canvas->setFont(&simutFont9pt);
			_driver.canvas->setTextColor(C_TEMP_HOT);
			String invMsg = tr(TR_INVALID_PASSWORD);
			_driver.canvas->getTextBounds(invMsg, 0, 0, &bx, &by, &bw, &bh);
			_driver.canvas->setCursor((320 - bw) / 2, 20);
			_driver.canvas->print(invMsg);
		} else {
			int pinLen = (int)_expectedPin.length( );
			int dotSpacing = 20;
			int dotsStartX = (320 - (pinLen * dotSpacing)) / 2 + dotSpacing / 2;
			for (int i = 0; i < pinLen; i++) {
				int cx = dotsStartX + (i * dotSpacing);
				if (i < _authStep) _driver.canvas->fillCircle(cx, 15, 6, C_ACCENT);
				else _driver.canvas->drawCircle(cx, 15, 6, C_TEXT_SUB);
			}
		}
		blitCanvas(_driver.canvas, 0, 35, 320, 30);
		g_lastAuthStep = _authStep;
		g_lastAuthFailed = _authFailed;
	}

	/* Keypad only repaints after scrambleKeys()/showAuthScreen()/fullRedraw. */
	if (!g_keypadDirty) return;
	g_keypadDirty = false;

	/* Keypad buttons via canvas — 2 buttons per row, 2 rows */
	for (int row = 0; row < 2; row++) {
		int rowY = 80 + (row * 60);
		_driver.canvas->fillScreen(C_BG_MAIN);
		_driver.canvas->setFont(&simutFont12pt);

		for (int col = 0; col < 2; col++) {
			int btnIdx = (row * 2) + col;
			int bx0 = (col == 0) ? 15 : 165;

			/* Button with finished rounded corners */
			_driver.canvas->fillRoundRect(bx0, 0, 140, 45, 10, C_CARD_BG);
			_driver.canvas->drawRoundRect(bx0, 0, 140, 45, 10, C_TEXT_SUB);

			/* Characters distributed on the button */
			_driver.canvas->setTextColor(C_TEXT_MAIN);
			String chars = String(_keypadChars[btnIdx]);
			int slotWidth = 35;
			for (int j = 0; j < 4; j++) {
				String singleChar = String(chars.charAt(j));
				int16_t cbx, cby; uint16_t cbw, cbh;
				_driver.canvas->getTextBounds(singleChar, 0, 0, &cbx, &cby, &cbw, &cbh);
				int charX = bx0 + (j * slotWidth) + ((slotWidth - cbw) / 2) - cbx;
				_driver.canvas->setCursor(charX, 31);
				_driver.canvas->print(singleChar);
			}
		}
		blitCanvas(_driver.canvas, 0, rowY, 320, 45);
	}
}
