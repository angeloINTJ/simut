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
#include "FlashIrqProbe.h" /* bench: readRect split into wire and conversion */
#include "LogManager.h"
#include <hardware/dma.h>
#include <hardware/spi.h>
#include "UiWidgets.h"

/* Volatile globals for incremental auth screen redraw.
 * Draw functions use these to detect state changes.
 * Volatile forces writes to remain (prevents DSE). */
namespace {
	volatile int g_lastAuthStep = -1;
	volatile bool g_lastAuthFailed = false;
	volatile bool g_keypadDirty = true;
}

/* No caller today — readRect( ) covers every capture path. It keeps the same
 * read clock as readRect( ) rather than the 2 MHz it used to spell out inline,
 * so that whoever does call it gets the clock this panel was measured at
 * (SIMUT_TFT_READ_HZ) and not a second convention nobody checked. */
uint16_t DisplayManager::readPixel(int16_t x, int16_t y) {
	if (!_driver.tft) return 0;
	_driver.tft->startWrite( ); _driver.tft->setAddrWindow(x, y, 1, 1); _driver.tft->endWrite( );
	SPI.beginTransaction(SPISettings(SIMUT_TFT_READ_HZ, MSBFIRST, SPI_MODE0));
	digitalWrite(TFT_CS, LOW);
	digitalWrite(TFT_DC, LOW); SPI.transfer(0x2E);
	digitalWrite(TFT_DC, HIGH); SPI.transfer(0x00);
	uint8_t r = SPI.transfer(0x00); uint8_t g = SPI.transfer(0x00); uint8_t b = SPI.transfer(0x00);
	digitalWrite(TFT_CS, HIGH); SPI.endTransaction( );
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}


/* Reads an arbitrary rectangle of panel GRAM into `out` as RGB565.
 *
 * Two things it does that readRow does not, and both are the point:
 *
 * ONE address window for the whole rectangle. The ILI9341's read counter walks
 * the window exactly as the write counter does, so a 320x8 strip costs one
 * CASET/PASET/RAMWR sequence instead of eight, and a narrow rectangle costs
 * only the pixels inside it — reading 6 blocks of 8x8 is 384 pixels where
 * eight readRow calls would be 5,120.
 *
 * ONE block SPI transfer per chunk instead of a call per byte. readRow spends
 * 5.79 ms on a row against 3.84 ms of clock at 2 MHz (measured over four
 * independent paths on the rig): the missing ~2 us per byte is the per-call
 * cost of SPI.transfer(uint8_t), and the PL022 FIFO does not get a chance to
 * stay fed. The block form hands the whole chunk to the driver at once.
 *
 * There is no transmit buffer at all: the two-buffer overload with a null tx
 * is the framework's receive-only case, and the SDK clocks out a constant 0xFF
 * while it reads. The panel ignores MOSI while RAMRD streams, so the byte on
 * the wire is don't-care and no second 960 B buffer has to exist to hold it.
 *
 * The chunk is one row wide so the stack cost stays at 960 B; the window is NOT
 * reopened between chunks, CS simply stays low. */
void DisplayManager::readRect(int16_t x, int16_t y, int16_t w, int16_t h,
                              uint16_t* out) {
	if (!_driver.tft || !out || w <= 0 || h <= 0) return;
#if SIMUT_MIRROR_PROBE
	const uint32_t winUs0 = timer_hw->timerawl;
#endif

	_driver.tft->startWrite( );
	_driver.tft->setAddrWindow(x, y, w, h);
	_driver.tft->endWrite( );

	SPI.beginTransaction(SPISettings(_readHz, MSBFIRST, SPI_MODE0));
	digitalWrite(TFT_CS, LOW);
	digitalWrite(TFT_DC, LOW);
	SPI.transfer(0x2E);
	digitalWrite(TFT_DC, HIGH);
	SPI.transfer(0x00);            /* the dummy byte RAMRD always emits first */
#if SIMUT_MIRROR_PROBE
	g_capWinUs += timer_hw->timerawl - winUs0;
	g_capReadHz = _readHz;
#endif

	constexpr int32_t CHUNK_PX = 320;
	uint8_t buf[CHUNK_PX * 3];
	int32_t left = (int32_t)w * (int32_t)h;
	int32_t o = 0;
	while (left > 0) {
		const int32_t n = (left > CHUNK_PX) ? CHUNK_PX : left;
		/* The two-buffer overload with a null tx is the ONLY fast path in this
		 * framework: SPIClassRP2040::transfer(void*, size_t) is a byte loop
		 * calling the single-byte transfer, while this one lands in the SDK's
		 * spi_read_blocking and keeps the PL022 FIFO fed. Measured on the rig:
		 * the byte loop costs ~4.3 us per pixel of pure software, which at
		 * 6 MHz is more than the wire itself. */
#if SIMUT_MIRROR_PROBE
		const uint32_t wUs0 = timer_hw->timerawl;
#endif
		SPI.transfer(nullptr, buf, (size_t)(n * 3));
#if SIMUT_MIRROR_PROBE
		g_capWireUs += timer_hw->timerawl - wUs0;
		const uint32_t cUs0 = timer_hw->timerawl;
#endif
		/* Pointer walk, not buf[i*3]: the indexed form recomputes i*3 and three
		 * separate addresses per pixel, and this loop runs 76,800 times a frame.
		 * The panel hands back 6-6-6 in three bytes and the wire wants RGB565, so
		 * the shift-and-mask itself is unavoidable; the addressing is not. */
		const uint8_t* p = buf;
		uint16_t* dst = out + o;
		for (int32_t i = 0; i < n; i++) {
			const uint8_t r = *p++, g = *p++, b = *p++;
			*dst++ = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
		}
		o += n;
#if SIMUT_MIRROR_PROBE
		g_capConvUs += timer_hw->timerawl - cUs0;
#endif
		left -= n;
	}

	digitalWrite(TFT_CS, HIGH);
	SPI.endTransaction( );
}


/* ── GRAM read by DMA, so Core 0 can work while the pixels arrive ──────────
 *
 * WHY, with the numbers that justify it (rig, 2026-09-19, 12 MHz, 76,800 px):
 * spi_read_blocking costs 184 ms where the wire alone is 153.6 ms — the other
 * 20% is the per-byte FIFO poll, and it is CPU that Core 0 spends watching a
 * transfer it could have delegated. Delegating it buys that overhead back AND
 * frees Core 0 for the 62 ms of encode and send that were serialised behind it.
 *
 * TWO channels, because a read still has to clock: SPI is synchronous, so the
 * TX side must push a byte for every byte the RX side wants. The TX channel
 * therefore streams one constant 0xFF with read_increment off — the panel
 * ignores MOSI while RAMRD streams, so the byte on the wire is don't-care.
 *
 * The bus stays open between Start and Finish. Nothing else may touch spi0 in
 * that window: not Core 1 (it is parked), and not the caller. The CYW43 is on
 * its own PIO SPI and lwIP never reaches spi0, which is what makes the overlap
 * legal at all.
 *
 * Falls back to false if no DMA channel is free, and the caller takes the
 * blocking readRect. */
static int dmaRdChannel( ) {
	static int s_ch = -1;
	if (s_ch < 0) s_ch = dma_claim_unused_channel(false);
	return s_ch;
}
static int dmaRdTxChannel( ) {
	static int s_ch = -1;
	if (s_ch < 0) s_ch = dma_claim_unused_channel(false);
	return s_ch;
}

bool DisplayManager::readRectDmaStart(int16_t x, int16_t y, int16_t w, int16_t h,
                                      uint8_t* dst3) {
	if (!_driver.tft || !dst3 || w <= 0 || h <= 0) return false;
	const int rx = dmaRdChannel( ), tx = dmaRdTxChannel( );
	if (rx < 0 || tx < 0 || rx == tx) return false;

	_driver.tft->startWrite( );
	_driver.tft->setAddrWindow(x, y, w, h);
	_driver.tft->endWrite( );

	SPI.beginTransaction(SPISettings(_readHz, MSBFIRST, SPI_MODE0));
	digitalWrite(TFT_CS, LOW);
	digitalWrite(TFT_DC, LOW);
	SPI.transfer(0x2E);            /* RAMRD */
	digitalWrite(TFT_DC, HIGH);
	SPI.transfer(0x00);            /* the dummy byte RAMRD always emits first */
#if SIMUT_MIRROR_PROBE
	/* This path does not go through readRect, so without this the bench reports
	 * whatever clock the LAST blocking read used — which is how a DMA sweep came
	 * back labelled 6 MHz while actually running at 12. */
	g_capReadHz = _readHz;
#endif

	/* Drain anything the two priming bytes left in RX, or the first pixel byte
	 * of every strip would be an echo of the command instead of a colour. */
	while (spi_get_hw(spi0)->sr & SPI_SSPSR_RNE_BITS) (void)spi_get_hw(spi0)->dr;

	const uint32_t n = (uint32_t)w * (uint32_t)h * 3u;

	dma_channel_config rc = dma_channel_get_default_config(rx);
	channel_config_set_transfer_data_size(&rc, DMA_SIZE_8);
	channel_config_set_dreq(&rc, DREQ_SPI0_RX);
	channel_config_set_read_increment(&rc, false);
	channel_config_set_write_increment(&rc, true);
	dma_channel_configure(rx, &rc, dst3, &spi_get_hw(spi0)->dr, n, false);

	static const uint8_t kIdle = 0xFF;
	dma_channel_config tc = dma_channel_get_default_config(tx);
	channel_config_set_transfer_data_size(&tc, DMA_SIZE_8);
	channel_config_set_dreq(&tc, DREQ_SPI0_TX);
	channel_config_set_read_increment(&tc, false);
	channel_config_set_write_increment(&tc, false);
	dma_channel_configure(tx, &tc, &spi_get_hw(spi0)->dr, &kIdle, n, false);

	/* RX armed before TX: the receiver must be waiting before a single byte is
	 * clocked, or the first bytes land in the FIFO and the count goes out by
	 * however many the FIFO swallowed. */
	dma_start_channel_mask((1u << rx) | (1u << tx));
	return true;
}

void DisplayManager::readRectDmaFinish( ) {
	const int rx = dmaRdChannel( ), tx = dmaRdTxChannel( );
	if (rx >= 0) dma_channel_wait_for_finish_blocking(rx);
	if (tx >= 0) dma_channel_wait_for_finish_blocking(tx);
	while (spi_get_hw(spi0)->sr & SPI_SSPSR_BSY_BITS) tight_loop_contents( );
	digitalWrite(TFT_CS, HIGH);
	SPI.endTransaction( );
}

/* The 6-6-6 the panel returns, packed into the RGB565 the wire format wants.
 * Split out of readRect so the DMA path can run it on the PREVIOUS strip while
 * the next one is still arriving. */
void DisplayManager::convert3to565(const uint8_t* src, uint16_t* out, size_t n) {
	const uint8_t* p = src;
	for (size_t i = 0; i < n; i++) {
		const uint8_t r = *p++, g = *p++, b = *p++;
		*out++ = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	}
}

void DisplayManager::readRow(int16_t y, uint16_t* buffer, int16_t w) {
	/* One row is a one-row rectangle. It used to be its own copy of the read
	 * protocol with a 320-iteration loop of one-byte SPI calls, and that loop
	 * was most of what a capture cost: /api/screenshot reads 720 rows and took
	 * 4.26 s on the rig. */
	readRect(0, y, w, 1, buffer);
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
