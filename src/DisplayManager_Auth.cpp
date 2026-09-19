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

/* The scrambled 4-button keypad that lived here (scrambleKeys, showAuthScreen,
 * drawAuthScreen) is gone with the device PIN: it worked by knowing the
 * expected PIN and could not identify a user. The numeric keypad is
 * drawPinScreen( ) in DisplayManager_Users.cpp — see its header for why. */
