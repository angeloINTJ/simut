/**
 * @file DisplayManager_Panel.cpp
 * @brief The panel's DMA bus primitives: the core share of DisplayManager_Dashboard.cpp, moved word for word in S10a (docs/analysis/UI_DRIVERS.md).
 */

#include "DisplayManager.h"
#include "simut_config.h"   /* SIMUT_NET_LAN is tested below: included here, not left to another header */
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "StorageManager.h"
#include "UiWidgets.h" /* blitTitleBar/blitFooterMenu compose the shared chrome */
#include "sensors/SensorPanelDispatch.h"
#include "display/PendingLabel.h" /* the pending count, as both displays print it */
#include "hardware/dma.h" /* strip-blit fast path: SSP 16-bit + DMA */
#include "hardware/spi.h"
#include "ui/UiDriverHooks.h"   /* pktArrowShown( ): the core's ticks, with the hook definitions they lean on; last, they need the complete class */

/* ── DMA fast path for canvas pushes and solid fills ────────────────────────
 *
 * Adafruit_SPITFT's write path on RP2040 tops out well under the wire rate
 * (~2 us/px measured against 0.667 us of wire at the 24 MHz the bus ran at, 0.24 us at 66.5), and these
 * pushes are the hot path of EVERY screen. Pixels go out with the SSP in
 * 16-bit frame mode fed by a DMA channel: the 16-bit frame sends the MSB of
 * each RGB565 VALUE first, so no byte-swap pass and no bounce buffer are
 * needed — the DMA reads the source memory directly.
 *
 * The CS/DC/command sequence mirrors readPixel( ) (the proven pattern in
 * this codebase for taking the bus over from the library): set the window
 * through the library, then re-assert CS, re-issue RAMWR, stream, restore.
 *
 * Synchronous by design: the caller reuses the canvas immediately after,
 * and the quiesce/flash-pause protocol assumes SPI bursts end within the
 * loop iteration that started them. Wire-limited: a 320x45 strip is 230,400
 * bits, 9.6 ms at the 24 MHz the bus ran at until 2026-10-15 (a 320x40 strip
 * measured 9.5 ms that day) and 3.5 ms at 66.5 MHz (SIMUT_TFT_SPI_HZ; computed).
 */
static int dmaTftChannel( ) {
 static int s_ch = -1;
 if (s_ch < 0) s_ch = dma_claim_unused_channel(false);
 return s_ch; /* -1: no channel free, callers take the library path */
}

/* The baud SPI0 was programmed to by the last panel push, read back from the
 * PL022 registers (GET /api/status, sys.tft_hz). The request is SIMUT_TFT_SPI_HZ;
 * the baud is the rung the clock allows. Written here on Core 1, read by the
 * status handler on Core 0 (one aligned word); 0 until the first push. Read on
 * the bench 2026-10-15, before clk_peri moved to clk_sys: 24000000. */
volatile uint32_t g_tftSpiBaud = 0;

/* Latches the address window and leaves the bus open in 16-bit frame mode,
 * RAMWR issued, ready for a DMA stream. Must be paired with dmaTftClose.
 * The Ângulo image pushes its strips through a copy of this open (src/ui/angulo/DisplayManager_UiA_Empurrar.cpp, which includes the write of g_tftSpiBaud
 * after SPI.beginTransaction( )): the two sequences are kept the same, and a change to one is made in both. */
static void dmaTftOpen(TftWithOffset* tft, int16_t x, int16_t y,
 int16_t w, int16_t h) {
 tft->startWrite( );
 tft->setAddrWindow(x, y, w, h);
 tft->endWrite( ); /* window latched; CS toggles, RAMWR re-sent below */

 SPI.beginTransaction(SPISettings(SIMUT_TFT_SPI_HZ, MSBFIRST, SPI_MODE0));
 g_tftSpiBaud = spi_get_baudrate(spi0); /* what the block really got, not what was asked */
 digitalWrite(TFT_CS, LOW);
 digitalWrite(TFT_DC, LOW);
 SPI.transfer(0x2C); /* RAMWR */
 digitalWrite(TFT_DC, HIGH);

 spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

static void dmaTftClose( ) {
 while (spi_get_hw(spi0)->sr & SPI_SSPSR_BSY_BITS) { tight_loop_contents( ); }
 spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
 digitalWrite(TFT_CS, HIGH);
 SPI.endTransaction( );
}

static bool blitWindowDma(TftWithOffset* tft, const uint16_t* buf,
 int16_t x, int16_t y, int16_t w, int16_t h) {
 /* Raw setAddrWindow does not clip; a display offset can push a strip
 * off-screen. Anything not fully on-panel takes the library path. */
 if (x < 0 || y < 0 || x + w > DisplayDriver::W || y + h > DisplayDriver::H) return false;

 int ch = dmaTftChannel( );
 if (ch < 0) return false;

 dmaTftOpen(tft, x, y, w, h);
 dma_channel_config c = dma_channel_get_default_config(ch);
 channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
 channel_config_set_dreq(&c, DREQ_SPI0_TX);
 channel_config_set_read_increment(&c, true);
 channel_config_set_write_increment(&c, false);
 dma_channel_configure(ch, &c, &spi_get_hw(spi0)->dr, buf,
 (uint32_t)w * (uint32_t)h, true);
 dma_channel_wait_for_finish_blocking(ch);
 dmaTftClose( );
 return true;
}

/* Solid fill at wire speed: same stream, but the DMA source is one 16-bit
 * color word with read_increment=false. The word lives on the stack — the
 * blocking wait below keeps it alive for the whole transfer. */
static bool fillWindowDma(TftWithOffset* tft, uint16_t color,
 int16_t x, int16_t y, int16_t w, int16_t h) {
 if (w <= 0 || h <= 0) return true; /* nothing to paint IS success */
 if (x < 0 || y < 0 || x + w > DisplayDriver::W || y + h > DisplayDriver::H) return false;

 int ch = dmaTftChannel( );
 if (ch < 0) return false;

 uint16_t colorWord = color;
 dmaTftOpen(tft, x, y, w, h);
 dma_channel_config c = dma_channel_get_default_config(ch);
 channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
 channel_config_set_dreq(&c, DREQ_SPI0_TX);
 channel_config_set_read_increment(&c, false);
 channel_config_set_write_increment(&c, false);
 dma_channel_configure(ch, &c, &spi_get_hw(spi0)->dr, &colorWord,
 (uint32_t)w * (uint32_t)h, true);
 dma_channel_wait_for_finish_blocking(ch);
 dmaTftClose( );
 return true;
}

void DisplayManager::fastFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
 uint16_t color) {
 if (!_driver.tft) return;
 /* Logical -> physical: apply the alignment offset here and clamp, the
 * same discipline blitCanvas uses. Clamping (instead of bailing to the
 * library) is safe for a FILL — dropping off-panel pixels of a solid
 * rect changes nothing visible. */
 int32_t px = (int32_t)x + _driver.tft->getOffsetX( );
 int32_t py = (int32_t)y + _driver.tft->getOffsetY( );
 int32_t pw = w, ph = h;
 if (px < 0) { pw += px; px = 0; }
 if (py < 0) { ph += py; py = 0; }
 if (px + pw > DisplayDriver::W) pw = DisplayDriver::W - px;
 if (py + ph > DisplayDriver::H) ph = DisplayDriver::H - py;
 if (pw <= 0 || ph <= 0) return;

 _driver.tft->setOffsetBypass(true);
 if (!fillWindowDma(_driver.tft, color, (int16_t)px, (int16_t)py,
                    (int16_t)pw, (int16_t)ph)) {
 _driver.tft->fillRect((int16_t)px, (int16_t)py, (int16_t)pw, (int16_t)ph,
                       color);
 }
 _driver.tft->setOffsetBypass(false);
}

void DisplayManager::fastClearScreen(uint16_t color) {
 if (!_driver.tft) return;
 /* Whole PHYSICAL panel in one burst, then the black alignment margins —
 * visually identical to TftWithOffset::fillScreen( ), ~4x faster. */
 _driver.tft->setOffsetBypass(true);
 if (!fillWindowDma(_driver.tft, color, 0, 0, DisplayDriver::W, DisplayDriver::H)) {
 _driver.tft->fillRect(0, 0, DisplayDriver::W, DisplayDriver::H, color);
 }
 _driver.tft->setOffsetBypass(false);
 _driver.tft->fillMarginsBlack( );
}

void DisplayManager::blitCanvas(GFXcanvas16* canvas, int16_t dstX, int16_t dstY,
 int16_t w, int16_t h, int16_t srcX) {
 if (!canvas || !_driver.tft) return;

 /*
 * Applies LCD alignment offset explicitly here because the
 * drawRGBBitmap routine of Adafruit_SPITFT may devirtualize (or inline) the
 * internal setAddrWindow call depending on version/toolchain, bypassing the
 * TftWithOffset override. We apply the offset directly to the destination
 * coordinates and enable the bypass flag on _tft to ensure that, if the
 * override IS called virtually, it won't apply the offset again (without
 * bypass a double offset would occur on libraries where dispatch works).
 */
 const int8_t ox = _driver.tft->getOffsetX( );
 const int8_t oy = _driver.tft->getOffsetY( );
 dstX += ox;
 dstY += oy;

 int16_t cw = canvas->width( );
 _driver.tft->setOffsetBypass(true);
 if (w == cw && srcX == 0) {
 if (!blitWindowDma(_driver.tft, canvas->getBuffer( ), dstX, dstY, w, h)) {
 _driver.tft->drawRGBBitmap(dstX, dstY, canvas->getBuffer( ), w, h);
 }
 } else {
 /* Sub-width slice (dashboard cards at 312, menu rows at 285, the
  * srcX status slice): the DMA has no 2D stride, so the rows are
  * compacted IN PLACE into a contiguous w*h block and pushed as one
  * burst. This took the cards and menu rows off the ~2 us/px library
  * path — they were the last big consumers of it (a dashboard card is
  * 23,400 px: ~40 ms per redraw, now ~12 ms).
  *
  * The compaction CONSUMES the canvas content (see the contract at the
  * declaration): with w < cw the destination row always starts at or
  * before its source row, and memmove handles the in-row overlap of
  * the early rows. Row 0 with srcX == 0 is a same-address no-op. */
 uint16_t* buf = canvas->getBuffer( );
 for (int16_t row = 0; row < h; row++) {
 memmove(buf + (int32_t)row * w, buf + (int32_t)row * cw + srcX,
 (size_t)w * 2u);
 }
 if (!blitWindowDma(_driver.tft, buf, dstX, dstY, w, h)) {
 /* Off-panel (display offset) or no DMA channel: the compacted
  * buffer is a plain w*h bitmap — the library path clips per row. */
 _driver.tft->drawRGBBitmap(dstX, dstY, buf, w, h);
 }
 }
 _driver.tft->setOffsetBypass(false);
}

int DisplayManager::buildDashLayout(DashBtn out[5], int *totalPages, bool *hasPaging) {
 /* Builds layout of 5 fixed slots (left->right). kind=-1 = empty.
 * The pagination button ALWAYS stays at position 4 (right corner) when
 * it exists; partial page slots leave gaps instead of pushing
 * the page button left. */
 for (int i = 0; i < 5; i++) { out[i].kind = -1; out[i].slotId = -1; }

 if (!_sysConfigPtr) return 0;
 SystemConfig &cfg = *_sysConfigPtr;
 DashBtn all[MAX_SENSORS + 2]; /* the slots + 1 CFG + 1 margin */
 int total = 0;
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfg.sensors[i].active) {
 /* Hide button when top panel is fixed on this sensor */
 if (_topPanel.fixed && _topPanel.fixedIdx == i) continue;
 all[total].kind = 0;
 all[total].slotId = (int8_t)i;
 total++;
 }
 }
 all[total].kind = 1; /* CFG always present */
 all[total].slotId = -1;
 total++;

 const int LINE_CAP = 5;
 bool paging = (total > LINE_CAP);
 int perPage = paging ? 4 : LINE_CAP; /* paging reserves pos 4 for page btn */
 int pages = (total + perPage - 1) / perPage;
 if (_currentPage >= pages) _currentPage = 0; /* clamp after config change */

 int firstIdx = _currentPage * perPage;
 int lastIdx = firstIdx + perPage;
 if (lastIdx > total) lastIdx = total;

 int pos = 0;
 for (int i = firstIdx; i < lastIdx; i++) out[pos++] = all[i];
 if (paging) { out[4].kind = 2; out[4].slotId = -1; } /* always position 4 */

 if (totalPages) *totalPages = pages;
 if (hasPaging) *hasPaging = paging;
 return paging ? 5 : pos;
}
