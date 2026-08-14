/**
 * @file DisplayManager_Dashboard.cpp
 * @brief Dashboard rendering: top bar, ambient/slot panels, bottom buttons.
 * @details Sub-file of DisplayManager.cpp.
 * Includes rounded corner helpers (fixCardCorners,
 * maskStripCorners), drawInterfaceFixed (fixed bg), and blitCanvas
 * (DMA push of canvas -> TFT). restoreNormalDashboard repositions
 * everything after modal events (auth/license/alarm).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "StorageManager.h"
#include "UiWidgets.h" /* blitTitleBar/blitFooterMenu compose the shared chrome */
#include "SensorPanelDispatch.h"
#include "hardware/dma.h" /* strip-blit fast path: SSP 16-bit + DMA */
#include "hardware/spi.h"

/* ── Dashboard layout: single source of truth ──────────────────────────────
 *
 * These were function-local constants and bare literals spread over three
 * drawers. They are collected here because drawInterfaceFixed( ) now paints
 * exactly the background these four widgets do NOT cover, which turns the
 * geometry into a shared invariant:
 *
 *   INVARIANT: the four widget rectangles below, plus the gaps and gutters
 *   drawInterfaceFixed( ) fills, must tile the whole 320x240 screen. Move or
 *   resize any widget and drawInterfaceFixed( ) must move with it, or the
 *   uncovered strip keeps whatever the previous screen left there.
 *
 * Coverage, for reference: rows 0-28 top bar, 35-109 top card, 115-189 bottom
 * card, 195-235 button bar; cards span x 4-315. */
namespace {
constexpr int16_t DASH_W        = 320;
constexpr int16_t DASH_H        = 240;
constexpr int16_t TOPBAR_H      = 29;   /* rows 0 .. 28 */
constexpr int16_t CARD_X        = 4;
constexpr int16_t CARD_W        = 312;  /* x 4 .. 315 */
constexpr int16_t CARD_H        = 75;
constexpr int16_t CARD_R        = 12;
constexpr int16_t CARD_TOP_Y    = 35;   /* rows 35 .. 109 */
constexpr int16_t CARD_BOTTOM_Y = 115;  /* rows 115 .. 189 */
constexpr int16_t BTNBAR_Y      = 195;  /* rows 195 .. 235 */
constexpr int16_t BTNBAR_H      = 41;
}  /* namespace */

void DisplayManager::fixCardCorners(int16_t x, int16_t y, int16_t w,
 int16_t h, int16_t r,
 uint16_t borderColor) {
 if (!_driver.tft) return;
 for (int16_t i = 0; i < r; i++) {
 int16_t span = (int16_t)(sqrtf(2.0f * r * i - (float)(i * i)) + 0.5f);
 int16_t gap = r - span;
 if (gap <= 0) continue;
 _driver.tft->drawFastHLine(x, y + i, gap, C_BG_MAIN);
 _driver.tft->drawFastHLine(x + w - gap, y + i, gap, C_BG_MAIN);
 _driver.tft->drawFastHLine(x, y + h - 1 - i, gap, C_BG_MAIN);
 _driver.tft->drawFastHLine(x + w - gap, y + h - 1 - i, gap, C_BG_MAIN);
 }
 _driver.tft->drawRoundRect(x, y, w, h, r, borderColor);
}


void DisplayManager::maskStripCorners(GFXcanvas16* canvas,
 int16_t stripRow, int16_t stripH,
 int16_t cardW, int16_t cardH,
 int16_t r, uint16_t bgColor,
 uint16_t borderColor) {
 if (!canvas || r <= 0) return;
 uint16_t* buf = canvas->getBuffer( );
 int16_t stride = canvas->width( );


 constexpr int16_t MAX_R = 24;
 int16_t borderMin[MAX_R], borderMax[MAX_R];
 int16_t rr = (r > MAX_R) ? MAX_R : r;

 for (int16_t i = 0; i < rr; i++) { borderMin[i] = rr; borderMax[i] = -1; }

 {

 int16_t f = 1 - rr;
 int16_t ddF_x = 1;
 int16_t ddF_y = -2 * rr;
 int16_t cx = 0;
 int16_t cy = rr;

 while (cx < cy) {
 if (f >= 0) { cy--; ddF_y += 2; f += ddF_y; }
 cx++; ddF_x += 2; f += ddF_x;


 int16_t row1 = rr - cx, col1 = rr - cy;
 int16_t row2 = rr - cy, col2 = rr - cx;

 if (row1 >= 0 && row1 < rr) {
 if (col1 < borderMin[row1]) borderMin[row1] = col1;
 if (col1 > borderMax[row1]) borderMax[row1] = col1;
 }
 if (row2 >= 0 && row2 < rr) {
 if (col2 < borderMin[row2]) borderMin[row2] = col2;
 if (col2 > borderMax[row2]) borderMax[row2] = col2;
 }
 }
 }


 for (int16_t row = 0; row < stripH; row++) {
 int16_t cardY = stripRow + row;
 uint16_t* rowPtr = buf + (row * stride);


 int16_t bMin = -1, bMax = -1;

 if (cardY < rr) {
 bMin = borderMin[cardY];
 bMax = borderMax[cardY];
 } else if (cardY >= cardH - rr) {
 int16_t mirror = cardH - 1 - cardY;
 bMin = borderMin[mirror];
 bMax = borderMax[mirror];
 }

 if (cardY == 0 || cardY == cardH - 1) {


 for (int16_t x = 0; x < bMin; x++)
 rowPtr[x] = bgColor;
 for (int16_t x = bMin; x < cardW - bMin; x++)
 rowPtr[x] = borderColor;
 for (int16_t x = cardW - bMin; x < cardW; x++)
 rowPtr[x] = bgColor;

 } else if (bMin >= 0) {


 for (int16_t x = 0; x < bMin; x++)
 rowPtr[x] = bgColor;
 for (int16_t x = bMin; x <= bMax; x++)
 rowPtr[x] = borderColor;

 int16_t rBMax = cardW - 1 - bMin;
 int16_t rBMin = cardW - 1 - bMax;
 for (int16_t x = rBMin; x <= rBMax; x++)
 rowPtr[x] = borderColor;
 for (int16_t x = cardW - bMin; x < cardW; x++)
 rowPtr[x] = bgColor;

 } else {

 rowPtr[0] = borderColor;
 rowPtr[cardW - 1] = borderColor;
 }
 }
}



void DisplayManager::restoreNormalDashboard( ) {
 if (!_driver.tft || !_driver.canvas) return;
 drawSlotPanel(_lastRenderedState.topSlotTemp, _lastRenderedState.topSlotHum,
 _lastRenderedState.topSlotType, _lastRenderedState.topSlotValid,
 _lastRenderedState.topSlotIdx, _lastRenderedState.topSlotName, true, _topPanel, _lastRenderedState.topSlotPres);
 drawSlotPanel(_lastRenderedState.slotTemp, _lastRenderedState.slotHum, _lastRenderedState.slotType,
 _lastRenderedState.slotValid,
 _lastRenderedState.selectedSlotIdx,
 _lastRenderedState.slotName, true, _bottomPanel, _lastRenderedState.slotPres);
 drawBottomButtons(_lastRenderedState.selectedSlotIdx);
}

void DisplayManager::drawInterfaceFixed( ) {
 if (!_driver.tft) return;
 /* Paints ONLY the background the four dashboard widgets do not cover — see the
  * layout invariant at the top of this file.
  *
  * This was `fillScreen(C_BG_MAIN)`, and measurement is why it is not any more.
  * A full redraw pushes 146,000 pixels to a 76,800-pixel screen, and 69,160 of
  * fillScreen's own pixels were overwritten by the four blits within
  * milliseconds. Worse, they went out through Adafruit_SPITFT::writeColor, whose
  * RP2040 branch issues one spi_write_blocking per pixel — each ending in a full
  * shift-register drain, so nothing pipelines (~2 us/px against 0.512 us of
  * wire). That one discarded fill was ~150 ms of the 254 ms measured for R_FULL.
  *
  * What is left is 7,640 pixels: four horizontal gaps and the two 4-px gutters
  * beside the 312-wide cards — filled at wire speed by fastFillRect now
  * (~15 ms -> ~4 ms).
  *
  * Every caller paints the full widget set immediately after (loopCore1 first
  * init, the theme-change branch, and render( )'s full-redraw path), so the
  * union still covers the screen and nothing stale survives. */
 constexpr int16_t CARD_BOT = CARD_BOTTOM_Y + CARD_H;   /* 190 */

 /* Horizontal gaps between the widgets. */
 fastFillRect(0, TOPBAR_H, DASH_W, CARD_TOP_Y - TOPBAR_H, C_BG_MAIN);
 fastFillRect(0, CARD_TOP_Y + CARD_H, DASH_W,
              CARD_BOTTOM_Y - (CARD_TOP_Y + CARD_H), C_BG_MAIN);
 fastFillRect(0, CARD_BOT, DASH_W, BTNBAR_Y - CARD_BOT, C_BG_MAIN);
 fastFillRect(0, BTNBAR_Y + BTNBAR_H, DASH_W,
              DASH_H - (BTNBAR_Y + BTNBAR_H), C_BG_MAIN);

 /* Gutters either side of the cards, which are narrower than the screen. The
  * span deliberately runs straight through the 110-114 gap already filled
  * above: the overlap is 40 pixels and costs less than getting it exact. */
 fastFillRect(0, CARD_TOP_Y, CARD_X, CARD_BOT - CARD_TOP_Y, C_BG_MAIN);
 fastFillRect(CARD_X + CARD_W, CARD_TOP_Y, DASH_W - (CARD_X + CARD_W),
              CARD_BOT - CARD_TOP_Y, C_BG_MAIN);
}

/* ── DMA fast path for canvas pushes and solid fills ────────────────────────
 *
 * Adafruit_SPITFT's write path on RP2040 tops out well under the wire rate
 * (~2 us/px measured against 0.512 us of wire at 31.25 MHz), and these
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
 * loop iteration that started them. Wire-limited: a 320x45 strip is
 * ~7.4 ms at 31.25 MHz, ~3.7 ms at the 62.5 MHz ceiling (SIMUT_TFT_SPI_HZ).
 */
static int dmaTftChannel( ) {
 static int s_ch = -1;
 if (s_ch < 0) s_ch = dma_claim_unused_channel(false);
 return s_ch; /* -1: no channel free, callers take the library path */
}

/* Latches the address window and leaves the bus open in 16-bit frame mode,
 * RAMWR issued, ready for a DMA stream. Must be paired with dmaTftClose. */
static void dmaTftOpen(TftWithOffset* tft, int16_t x, int16_t y,
 int16_t w, int16_t h) {
 tft->startWrite( );
 tft->setAddrWindow(x, y, w, h);
 tft->endWrite( ); /* window latched; CS toggles, RAMWR re-sent below */

 SPI.beginTransaction(SPISettings(SIMUT_TFT_SPI_HZ, MSBFIRST, SPI_MODE0));
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
 if (x < 0 || y < 0 || x + w > 320 || y + h > 240) return false;

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
 if (x < 0 || y < 0 || x + w > 320 || y + h > 240) return false;

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
 if (px + pw > 320) pw = 320 - px;
 if (py + ph > 240) ph = 240 - py;
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
 if (!fillWindowDma(_driver.tft, color, 0, 0, 320, 240)) {
 _driver.tft->fillRect(0, 0, 320, 240, color);
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

/* Full-screen render via 40px strips.
 * Reuses `_driver.canvas` (320x45, allocated at Core 1 boot for the dashboard top
 * bar). During full-screen renders (auth/settings/etc), the dashboard is not
 * active — canvas is free for reuse. Blits only 40 of the 45 canvas rows per
 * strip; 5 extra rows are ignored in the blit.
 *
 * No dynamic heap = zero risk of OOM/null-buffer crash. Telemetry runs
 * normally during render (free heap intact). */
GFXcanvas16* DisplayManager::beginScreenRender( ) {
 if (!_driver.canvas) return nullptr; /* Core 1 not initialized — unlikely during render */
 _driver.canvas->fillScreen(C_BG_MAIN);
 return _driver.canvas;
}

void DisplayManager::commitScreenStrip(int16_t stripIdx) {
 if (!_driver.canvas || !_driver.tft) return;
 int16_t stripY = stripIdx * RENDER_STRIP_H;
 /* Blit 40 of the 45 canvas rows (5 leftover ignored). No clear afterwards:
 * every strip loop starts with its own fillScreen (audited — Auth, mute
 * confirm, calibration x4, alarm action, and the pattern in the header
 * comment requires it), and the old post-blit clear was a second full
 * canvas pass thrown away per strip. */
 blitCanvas(_driver.canvas, 0, stripY, 320, RENDER_STRIP_H);
}

void DisplayManager::endScreenRender( ) {
 /* No-op: _driver.canvas is persistent, nothing to free.
 * Kept in API for consistency (caller still calls it at the end). */
}

/* ── Shared chrome strips ───────────────────────────────────────────────────
 *
 * Every menu-family screen used to paint its title bar and footer straight
 * onto the TFT through the library path (~20 ms each). These compose the
 * same widgets into the shared canvas and push one full-width DMA blit:
 * pixel-identical, ~6-7 ms per band, and the bands double as background
 * fill for the rows they cover. */
void DisplayManager::blitTitleBar(const char* title, int curPage,
 int totalPages) {
 if (!_driver.canvas || !_driver.tft) return;
 _driver.canvas->fillScreen(C_BG_MAIN);
 /* Same geometry as the direct call: bar at y=4, h=32. */
 uiTitleBar(_driver.canvas, 4, title, curPage, totalPages);
 blitCanvas(_driver.canvas, 0, 0, 320, 40);
}

void DisplayManager::blitFooterMenu(const char* exitLabel,
 const char* primaryLabel) {
 if (!_driver.canvas || !_driver.tft) return;
 _driver.canvas->fillScreen(C_BG_MAIN);
 uiFooterMenu(_driver.canvas, exitLabel, primaryLabel, /*yBase=*/0);
 /* 45 rows: buttons at 195..234 plus the bottom margin through 239. */
 blitCanvas(_driver.canvas, 0, 195, 320, 45);
}

void DisplayManager::drawTopBar(const SystemState& state) {
 if(!_driver.canvas) return;
 const int W = DASH_W, H = TOPBAR_H;

 /* Web-busy banner.
  *
  * Touch is rejected while a web client holds the device, by design — aborting a
  * transfer would truncate the chart the caller is downloading. The old feedback
  * for that was a full-screen overlay, and it had two faults: it appeared only if
  * the user actually touched (so the reason was learned by failing first), and
  * while it showed, rendering was suppressed entirely, so the readings froze.
  * This says the same thing in the bar the user is already looking at, and costs
  * one blit that was happening anyway.
  *
  * Hardcoded string rather than a TR key, deliberately: DisplayManager_LangParser
  * requires a pack to have EXACTLY TR_KEYS_COUNT lines, so adding a key
  * invalidates every installed .lng and drops the whole UI to English until the
  * packs are regenerated. Make it a key the next time they are.
  *
  * No new repaint trigger is needed — render( ) already redraws this bar whenever
  * the clock string changes, so the banner appears and clears within a second. */
 if (_lastWebBusy) {
  /* Bounded copy: _webBusyUser is written by Core 0 under _stateMutex and read
   * here without it. A torn read is cosmetic, but an unterminated one would run
   * snprintf off the end, so terminate it locally instead of taking the lock. */
  char user[sizeof(_webBusyUser)];
  memcpy(user, (const void*)_webBusyUser, sizeof(user));
  user[sizeof(user) - 1] = '\0';

  char msg[48];
  snprintf(msg, sizeof(msg), "WEB '%s' - toque bloqueado", user[0] ? user : "web");

  /* Banner as an inset chip (4..315 x 4..28) instead of a full-bleed fill:
   * at the panel edge, a display offset chopped the band asymmetrically.
   * The message is truncated to the chip's width for long usernames —
   * before, anything past x=320 was silently clipped mid-glyph. */
  _driver.canvas->fillScreen(C_BG_MAIN);
  _driver.canvas->fillRoundRect(4, 4, 312, 25, 8, C_TEMP_WARM);
  _driver.canvas->setFont(&simutFont9pt);
  _driver.canvas->setTextSize(1);
  _driver.canvas->setTextColor(C_BG_MAIN);
  char fitMsg[48];
  truncateText(_driver.canvas, msg, fitMsg, sizeof(fitMsg), 296);
  int16_t bx, by; uint16_t bw, bh;
  _driver.canvas->getTextBounds(fitMsg, 0, 0, &bx, &by, &bw, &bh);
  int16_t cx = (int16_t)((W - (int)bw) / 2);
  if (cx < 8) cx = 8;
  _driver.canvas->setCursor(cx, 21);
  _driver.canvas->print(fitMsg);
  blitCanvas(_driver.canvas, 0, 0, W, H);
  return;
 }

 _driver.canvas->fillScreen(C_BG_MAIN);


 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setTextSize(1);
 _driver.canvas->setTextColor(C_ACCENT);
 /* x=4: aligned with the cards' left edge AND inside the 4-px safe margin
  * (at x=3 a -4 horizontal offset shaved the S). */
 _driver.canvas->setCursor(4, 20);
 _driver.canvas->print("SIMUT");


 bool showingSilence = false;
 if (_alarmSilenced && _alarmSilenceEnd > 0) {
 uint32_t now = millis( );
 if (now < _alarmSilenceEnd) {
 showingSilence = true;
 uint32_t remaining = (_alarmSilenceEnd - now) / 1000;
 char silBuf[32];
 snprintf(silBuf, sizeof(silBuf), "%s: %lus", tr(TR_SILENCED), (unsigned long)remaining);
 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setTextColor(C_TEMP_WARM);
 _driver.canvas->setCursor(75, 20);
 _driver.canvas->print(silBuf);
 }
 }


 bool showingNotify = false;
 if (!showingSilence && _webNotifyStartMs > 0) {
 uint32_t elapsed = millis( ) - _webNotifyStartMs;
 if (elapsed < WEB_NOTIFY_DURATION_MS) {
 showingNotify = true;
 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setTextColor(C_ACCENT_HIGH);
 _driver.canvas->setCursor(75, 20);
 char notifyBuf[32];
 snprintf(notifyBuf, sizeof(notifyBuf), "Web: %s", _webNotifyUser);
 _driver.canvas->print(notifyBuf);
 } else {

 _webNotifyStartMs = 0;
 _webNotifyUser[0] = '\0';
 }
 }


 if (!showingSilence && !showingNotify) {
 /*
 * Date and time centered in the available area.
 * Format: "dd/mm/yy - HH:MM"
 * The " - " separator stays fixed in the center; the date grows to the
 * left and the time grows to the right, ensuring the text
 * does not jump when digits change.
 */
 _driver.canvas->setTextSize(1);
 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setTextColor(C_TITLE_TEXT);

 /* Separate date and time by " - ".
  * T1.2: fixed buffers — this runs EVERY dashboard frame on Core 1 and
  * was the single largest heap churn (3 String allocations per frame),
  * i.e. the widest window for the reset-inside-malloc hazard. */
 char datePart[24];
 char timePart[16];
 const char* sep = strstr(state.timeString, " - ");
 if (sep) {
  size_t dlen = (size_t)(sep - state.timeString);
  if (dlen >= sizeof(datePart)) dlen = sizeof(datePart) - 1;
  memcpy(datePart, state.timeString, dlen);
  datePart[dlen] = '\0';
  strncpy(timePart, sep + 3, sizeof(timePart) - 1);
  timePart[sizeof(timePart) - 1] = '\0';
 } else {
  strncpy(datePart, state.timeString, sizeof(datePart) - 1);
  datePart[sizeof(datePart) - 1] = '\0';
  timePart[0] = '\0';
 }

 /* Measure only sep and date — timeX = sepX + sepW (no need to measure timeW). */
 int16_t bx, by; uint16_t bw, bh;
 uint16_t sepW, dateW;

 _driver.canvas->getTextBounds(" - ", 0, 0, &bx, &by, &bw, &bh);
 sepW = bw;
 _driver.canvas->getTextBounds(datePart, 0, 0, &bx, &by, &bw, &bh);
 dateW = bw;

 /*
 * Separator center fixed at display middle (x=160).
 * Date grows left, time grows right.
 */
 const int centerX = 160;

 int sepX = centerX - (int)sepW / 2;
 int dateX = sepX - (int)dateW;
 int timeX = sepX + (int)sepW;

 _driver.canvas->setCursor(dateX, 20);
 _driver.canvas->print(datePart);
 _driver.canvas->setTextColor(C_TEXT_SUB);
 _driver.canvas->setCursor(sepX, 20);
 _driver.canvas->print(" - ");
 _driver.canvas->setTextColor(C_TITLE_TEXT);
 _driver.canvas->setCursor(timeX, 20);
 _driver.canvas->print(timePart);
 }


 int xIcon = 305;

 /* Memory barrier before reading _pktArrowState + flash vars
 * published by Core 0 in setTelemetrySendStatus. */
 __dmb( );
 if (state.pendingPkts > 0 || _pktArrowState > 0) {
 /*
 * NUMBER color: based on last send result.
 * state 1 or 3 -> blue (success / success flash)
 * state 2 -> red (failure)
 * state 0 -> blue (idle, never sent)
 */
 uint16_t numColor = (_pktArrowState == 2) ? C_TEMP_HOT : C_ACCENT_HIGH;

 /*
 * ARROW color: same as number, except during flash (state 3)
 * where it alternates blue/white every 300ms for 1 second.
 */
 uint16_t arrowColor = numColor;

 if (_pktArrowState == 3) {
 uint32_t now = millis( );
 if (now >= _pktArrowFlashEnd) {
 _pktArrowState = 1;
 arrowColor = C_ACCENT_HIGH;
 } else {
 if (now - _pktArrowFlashTime >= 300) {
 _pktArrowFlashOn = !_pktArrowFlashOn;
 _pktArrowFlashTime = now;
 }
 arrowColor = _pktArrowFlashOn ? RGB565(255, 255, 255) : C_ACCENT_HIGH;
 }
 }

 if (state.pendingPkts > 0) {
 char pktBuf[10];
 /* >=1000 abbreviates as "Nk" to fit in the top bar. */
 if (state.pendingPkts >= 1000) {
 snprintf(pktBuf, sizeof(pktBuf), "%uk", state.pendingPkts / 1000);
 } else {
 snprintf(pktBuf, sizeof(pktBuf), "%u", state.pendingPkts);
 }

 _driver.canvas->setFont(&simutFont9pt);

 int16_t tx1, ty1; uint16_t tw, th;
 _driver.canvas->getTextBounds(pktBuf, 0, 0, &tx1, &ty1, &tw, &th);

 /*
 * Layout: [number][gapNum][arrow][gapWifi][wifi]
 * Arrow: 12px. Gap between number and arrow: 4px.
 * Gap between arrow and wifi: 3px.
 * When number is wide (>=3 digits), xIcon backs up 1 character.
 */
 const int arrowTotalW = 12;
 const int gapToWifi = 3;
 const int gapNumArrow = 4;
 int effectiveXIcon = xIcon;
 if ((int)tw > 24) effectiveXIcon -= 8; /* back up for large numbers */

 int arrowRight = effectiveXIcon - gapToWifi;
 int arrowLeft = arrowRight - arrowTotalW;
 int textX = arrowLeft - gapNumArrow - (int)tw;

 /* Number — fixed color based on status */
 _driver.canvas->setTextColor(numColor);
 _driver.canvas->setCursor(textX, 20);
 _driver.canvas->print(pktBuf);

 /*
 * Right-pointing arrow:
 * - Rectangular shaft (6x3 px) at vertical center
 * - Triangular tip (6x8 px) on the right
 */
 int ay = 13;
 int shaftX = arrowLeft;
 int shaftW = 6;
 int tipX = shaftX + shaftW;
 int tipW = arrowTotalW - shaftW;

 _driver.canvas->fillRect(shaftX, ay - 1, shaftW, 3, arrowColor);
 _driver.canvas->fillTriangle(tipX, ay - 4,
 tipX, ay + 4,
 tipX + tipW, ay,
 arrowColor);

 /* Reposition wifi if needed */
 if ((int)tw > 24) xIcon = effectiveXIcon;
 }
 }


 int barras = 0;
 if (state.wifiRssi > -100) {
 if (state.wifiRssi > -55) barras = 4;
 else if (state.wifiRssi > -65) barras = 3;
 else if (state.wifiRssi > -75) barras = 2;
 else barras = 1;
 }
 for (int i = 0; i < 4; i++) {
 _driver.canvas->fillRect(xIcon + (i * 3), 20 - (4 + (i * 2)), 2, 4 + (i * 2),
 (i < barras) ? C_TEMP_OK : C_BAR_BG);
 }

 blitCanvas(_driver.canvas, 0, 0, W, H);
}


void DisplayManager::drawSlotPanel(float t, float h, SensorType type, bool isValid, int slotIdx, const char* name, bool forceNameRedraw, DashPanel& panel, float p) {
 if(!_driver.canvas) return;
 int16_t x1, y1; uint16_t h_bound;

 /* Top panel at Y=35, bottom at Y=115 */
 int16_t cardY = (&panel == &_topPanel) ? 35 : 115;

 uint16_t panelBg = slotAlarmBg(slotIdx);
 bool isRedPhase = _alarmFlashPhase && isSlotAlarming(slotIdx) && !_alarmSilenced;
 uint16_t nameColor = isRedPhase ? RGB565(255, 255, 255) : C_SENSOR_NAME;
 uint16_t unitColor = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_MAIN;
 if (isSlotAlarming(slotIdx)) forceNameRedraw = true;

 /* Top panel in interactive (selection) mode: dark theme */
 bool isSelecting = (&panel == &_topPanel && !_topPanel.fixed);
 if (isSelecting) {
 panelBg = RGB565(50, 50, 55);
 isRedPhase = true;  /* white rendering like alarm mode */
 nameColor = RGB565(255, 255, 255);
 unitColor = RGB565(255, 255, 255);
 }

 /* Geometry lives at the top of this file: drawInterfaceFixed( ) fills the
  * complement of these rectangles, so the two must not drift apart. */


 bool slotAlarm = isSlotAlarming(slotIdx) && _alarmFlashPhase;
 uint16_t borderColor = slotAlarm ? RGB565(255, 60, 60) : C_ACCENT_HIGH;
 if (isSelecting) borderColor = RGB565(130, 130, 140);

 if (panel.showMinMax) {
 /* Track mode transition */
 panel.lastMinMax = true;

 /* =============================================================
 * MIN/MAX MODE — 3 blits with embedded border
 * Slot has no humidity.
 * ============================================================= */

 uint16_t txtSub = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_MAIN;

 /* Blit 1: Name (20px) */
 {
 _driver.canvas->fillScreen(panelBg);
 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setTextSize(1);
 _driver.canvas->setTextColor(txtSub);
 const char* displayName = name;
 char buf[16];
 if (strlen(name) == 0) {
 snprintf(buf, 16, "Sensor %d", slotIdx);
 displayName = buf;
 }
 int16_t nx1, ny1; uint16_t nw, nh;
 _driver.canvas->getTextBounds(displayName, 0, 0, &nx1, &ny1, &nw, &nh);
 _driver.canvas->setCursor((CARD_W - (int)nw) / 2, 15);
 _driver.canvas->print(displayName);
 maskStripCorners(_driver.canvas, 0, 20, CARD_W, CARD_H, CARD_R,
 C_BG_MAIN, borderColor);
 blitCanvas(_driver.canvas, CARD_X, cardY, CARD_W, 20);
 }

 /* Blit 2: Min + Max together (43px) — driver-rendered */
 {
 _driver.canvas->fillScreen(panelBg);
 sensorRenderMinMax(_driver.canvas, type,
     panel.minTemp, panel.maxTemp, panel.minHum, panel.maxHum,
     isValid, CARD_W, isRedPhase, panelBg,
     simutFont9pt,
     txtSub, C_TEMP_OK, C_TEMP_HOT, C_HUMIDITY, C_TEXT_OFF,
     C_ACCENT_HIGH, C_BTN_TEXT_ACTIVE,
     tr(TR_MIN_LBL), tr(TR_MAX_LBL), tr(TR_HUM_SUFFIX));
 /* Side borders (intermediate strip, no corners) */
 {
 uint16_t* buf = _driver.canvas->getBuffer( );
 int stride = _driver.canvas->width( );
 for (int row = 0; row < 43; row++) {
 buf[row * stride] = borderColor;
 buf[row * stride + CARD_W - 1] = borderColor;
 }
 }
 blitCanvas(_driver.canvas, CARD_X, cardY + 20, CARD_W, 43);
 }



 /* Blit 3: Bottom fill (12px) — with bottom corners + borders */
 {
 _driver.canvas->fillScreen(panelBg);
 maskStripCorners(_driver.canvas, 63, 12, CARD_W, CARD_H, CARD_R,
 C_BG_MAIN, borderColor);
 blitCanvas(_driver.canvas, CARD_X, cardY + 63, CARD_W, 12);
 }

 } else {
 /* Force name redraw on min/max -> normal transition */
 if (panel.lastMinMax) forceNameRedraw = true;
 panel.lastMinMax = false;

 /* =============================================================
 * NORMAL MODE — centered temperature with large icon
 * ============================================================= */

 if (forceNameRedraw) {
 _driver.canvas->fillScreen(panelBg);
 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextSize(1);
 _driver.canvas->setTextColor(nameColor);
 const char* displayName = name;
 char buf[16];
 if (strlen(name) == 0) {
 snprintf(buf, 16, "Sensor %d", slotIdx);
 displayName = buf;
 }
 int16_t nx1, ny1; uint16_t nw, nh;
 _driver.canvas->getTextBounds(displayName, 0, 0, &nx1, &ny1, &nw, &nh);
 _driver.canvas->setCursor((CARD_W - (int)nw) / 2, 15);
 _driver.canvas->print(displayName);
 maskStripCorners(_driver.canvas, 0, 20, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
 blitCanvas(_driver.canvas, CARD_X, cardY, CARD_W, 20);
 }

 /* Gap strip to center content (8px) */
 {
 _driver.canvas->fillScreen(panelBg);
 uint16_t* buf = _driver.canvas->getBuffer( );
 int stride = _driver.canvas->width( );
 for (int row = 0; row < 8; row++) {
 buf[row * stride] = borderColor;
 buf[row * stride + CARD_W - 1] = borderColor;
 }
 blitCanvas(_driver.canvas, CARD_X, cardY + 20, CARD_W, 8);
 }

 _driver.canvas->fillScreen(panelBg);

 if (!isValid) {
 _driver.canvas->setFont(&simutFont12pt); _driver.canvas->setTextSize(1);
 _driver.canvas->setTextColor(isRedPhase ? RGB565(255,255,255) : C_TEMP_HOT);
 int16_t ex1, ey1; uint16_t ew, eh;
 _driver.canvas->getTextBounds(tr(TR_ERROR_LBL), 0, 0, &ex1, &ey1, &ew, &eh);
 _driver.canvas->setCursor((CARD_W - (int)ew) / 2, 28);
 _driver.canvas->print(tr(TR_ERROR_LBL));
 } else {
 /* No fillScreen here: the one above already cleared the canvas to panelBg and
  * nothing has drawn into it since — this was a second identical 14,400-pixel
  * fill, ~0.8 ms per panel thrown away on every redraw. */
 sensorRenderPanel(_driver.canvas, type, t, h, p, isValid, CARD_W, true,
                   isRedPhase, panelBg,
                   simutFont24pt, simutFont12pt, simutFont9pt,
                   C_TEXT_SUB, C_TEMP_OK, C_TEMP_HOT, C_HUMIDITY, C_TEXT_OFF, tr(TR_HUM_SUFFIX));
 maskStripCorners(_driver.canvas, 28, 40, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
 blitCanvas(_driver.canvas, CARD_X, cardY + 28, CARD_W, 40);
 goto _slot_bottom_fill;

 const int iconW = 20;
 const int iconGap = 8;
 const int unitGap = 3;
 const int dotGap = 4;

 _driver.canvas->setFont(&simutFont24pt); _driver.canvas->setTextSize(1);

 char intPart[10]; char decPart[5];
 bool isNan = isnan(t);
 uint16_t intW = 0, decW = 0;

 if (isNan) {
 _driver.canvas->getTextBounds("--.-", 0, 0, &x1, &y1, &intW, &h_bound);
 decW = 0;
 } else {
 int fractional = abs((int)(t * 10) % 10);
 snprintf(intPart, sizeof(intPart), "%d", (int)t);
 snprintf(decPart, sizeof(decPart), ".%d", fractional);
 _driver.canvas->getTextBounds(intPart, 0, 0, &x1, &y1, &intW, &h_bound);
 _driver.canvas->getTextBounds(decPart, 0, 0, &x1, &y1, &decW, &h_bound);
 }

 _driver.canvas->setFont(&simutFont9pt);
 uint16_t degW;
 _driver.canvas->getTextBounds("o", 0, 0, &x1, &y1, &degW, &h_bound);
 _driver.canvas->setFont(&simutFont12pt);
 uint16_t cW;
 _driver.canvas->getTextBounds("C", 0, 0, &x1, &y1, &cW, &h_bound);
 int unitTotalW = (int)degW + 8 + (int)cW;

 int numW = (int)intW + (isNan ? 0 : dotGap + (int)decW);
 int totalW = iconW + iconGap + numW + unitGap + unitTotalW;
 int offsetX = (CARD_W - totalW) / 2;

 int iconX = offsetX;
 int numAnchorX = iconX + iconW + iconGap + (int)intW;
 int unitX;

 _driver.canvas->setFont(&simutFont24pt);
 if (isNan) {
 _driver.canvas->setTextColor(isRedPhase ? RGB565(200,180,180) : C_TEXT_OFF);
 _driver.canvas->setCursor(iconX + iconW + iconGap, 35);
 _driver.canvas->print("--.-");
 unitX = iconX + iconW + iconGap + (int)intW + unitGap;
 } else {
 _driver.canvas->setTextColor(isRedPhase ? RGB565(255,255,255) : C_TEMP_OK);
 int numCursorX = numAnchorX - (int)intW;
 _driver.canvas->setCursor(numCursorX, 35);
 _driver.canvas->print(intPart);
 if (t < 0) {
 int16_t mx1, my1; uint16_t mw, mh;
 _driver.canvas->getTextBounds("-", 0, 0, &mx1, &my1, &mw, &mh);
 int eraseW = (int)mw / 3;
 if (eraseW < 2) eraseW = 2;
 _driver.canvas->fillRect(numCursorX, 0, eraseW, 45, panelBg);
 }
 _driver.canvas->setFont(&simutFont24pt);
 _driver.canvas->setCursor(numAnchorX + dotGap, 35);
 _driver.canvas->print(decPart);
 unitX = numAnchorX + dotGap + (int)decW + unitGap;
 }

 _driver.canvas->setFont(&simutFont9pt); _driver.canvas->setTextColor(unitColor);
 _driver.canvas->setCursor(unitX, 17); _driver.canvas->print("o");
 _driver.canvas->setFont(&simutFont12pt);
 _driver.canvas->setCursor(unitX + 8, 35); _driver.canvas->print("C");

 /* --- Humidity --- */
 if (!isnan(h)) {
 _driver.canvas->setFont(&simutFont12pt);
 _driver.canvas->setTextColor(isRedPhase ? RGB565(255,255,255) : C_HUMIDITY);
 _driver.canvas->setCursor(CARD_W - 56, 35);
 _driver.canvas->print((int)h);
 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setCursor(CARD_W - 22, 17);
 _driver.canvas->print("%");
 }

 /* Thermometer icon — drawn last (slot) */
 {
 uint16_t ic = isRedPhase ? RGB565(220, 200, 200) : C_TEXT_SUB;
 uint16_t merc = isRedPhase ? RGB565(255, 255, 255) : C_TEMP_HOT;
 int ix = iconX, iy = 4;
 _driver.canvas->fillCircle(ix + 10, iy + 26, 7, ic);
 _driver.canvas->fillRoundRect(ix + 6, iy, 8, 24, 4, ic);
 _driver.canvas->fillRoundRect(ix + 8, iy + 2, 4, 20, 2, panelBg);
 _driver.canvas->fillCircle(ix + 10, iy + 26, 5, panelBg);
 _driver.canvas->fillRect(ix + 9, iy + 10, 2, 14, merc);
 _driver.canvas->fillCircle(ix + 10, iy + 26, 4, merc);
 _driver.canvas->fillCircle(ix + 10, iy + 2, 2, ic);
 }
 }

 maskStripCorners(_driver.canvas, 28, 40, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
 blitCanvas(_driver.canvas, CARD_X, cardY + 28, CARD_W, 40);

_slot_bottom_fill:
 /* Strip 4: Bottom fill (7px) */
 {
 _driver.canvas->fillScreen(panelBg);
 maskStripCorners(_driver.canvas, 68, 7, CARD_W, CARD_H, CARD_R, C_BG_MAIN, borderColor);
 blitCanvas(_driver.canvas, CARD_X, cardY + 68, CARD_W, 7);
 }
 }
}

int DisplayManager::buildDashLayout(DashBtn out[5], int *totalPages, bool *hasPaging) {
 /* Builds layout of 5 fixed slots (left->right). kind=-1 = empty.
 * The pagination button ALWAYS stays at position 4 (right corner) when
 * it exists; partial page slots leave gaps instead of pushing
 * the page button left. */
 for (int i = 0; i < 5; i++) { out[i].kind = -1; out[i].slotId = -1; }

 if (!_sysConfigPtr) return 0;
 SystemConfig &cfg = *_sysConfigPtr;
 DashBtn all[MAX_SENSORS + 2]; /* 16 slots + 1 CFG + 1 margin */
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

void DisplayManager::drawBottomButtons(int selectedIdx) {
 if(!_driver.canvas) return;
 _driver.canvas->fillScreen(C_BG_MAIN);
 const int btnW = 58, gap = 5, xStart = 5, pitch = btnW + gap;

 DashBtn btns[5];
 int totalPages = 1;
 bool paging = false;
 int n = buildDashLayout(btns, &totalPages, &paging);

 /* Detects alarms in ACTIVE slots on other pages (to color the page btn) */
 if (!_sysConfigPtr) { blitCanvas(_driver.canvas, 0, BTNBAR_Y, DASH_W, BTNBAR_H); return; }
 SystemConfig &cfg = *_sysConfigPtr;
 bool hasAlarmsOnOtherPages = false;
 if (paging && _alarmSlotMask != 0) {
 for (int s = 0; s < MAX_SENSORS; s++) {
 if (!cfg.sensors[s].active) continue;
 if (!isSlotAlarming(s)) continue;
 bool inThisPage = false;
 for (int i = 0; i < n; i++) {
 if (btns[i].kind == 0 && btns[i].slotId == s) { inThisPage = true; break; }
 }
 if (!inThisPage) { hasAlarmsOnOtherPages = true; break; }
 }
 }

 for (int i = 0; i < 5; i++) {
 const DashBtn &b = btns[i];
 if (b.kind < 0) continue; /* gap between slots and page btn anchored to right */
 int x = xStart + (i * pitch);

 if (b.kind == 0) { /* SLOT */
 int realIdx = b.slotId;
 bool isActive = (realIdx == selectedIdx);
 bool btnAlarm = _alarmFlashPhase && isSlotAlarming(realIdx);
 uint16_t bgColor, txtColor;
 if (btnAlarm) {
 bgColor = RGB565(180, 30, 30);
 txtColor = RGB565(255, 255, 255);
 } else if (isActive) {
 bgColor = C_ACCENT_HIGH;
 txtColor = C_BTN_TEXT_ACTIVE;
 } else {
 bgColor = C_CARD_BG;
 txtColor = isSlotAlarming(realIdx) ? C_TEMP_HOT : C_BTN_TEXT;
 }
 _driver.canvas->fillRoundRect(x, 0, btnW, 40, 12, bgColor);
 _driver.canvas->setFont(&simutFont12pt); _driver.canvas->setTextSize(1); _driver.canvas->setTextColor(txtColor);
 char label[8]; snprintf(label, sizeof(label), "S%d", realIdx);
 int16_t x1, y1; uint16_t w, h;
 _driver.canvas->getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
 _driver.canvas->setCursor(x + (btnW - w)/2, 28);
 _driver.canvas->print(label);

 } else if (b.kind == 1) { /* CFG */
 _driver.canvas->fillRoundRect(x, 0, btnW, 40, 12, C_CARD_BG);
 _driver.canvas->setFont(&simutFont12pt); _driver.canvas->setTextSize(1); _driver.canvas->setTextColor(C_BTN_TEXT);
 int16_t x1, y1; uint16_t w, h;
 _driver.canvas->getTextBounds("CFG", 0, 0, &x1, &y1, &w, &h);
 _driver.canvas->setCursor(x + (btnW - w)/2, 28);
 _driver.canvas->print("CFG");

 } else { /* PAGE */
 uint16_t pagTxtCol = C_BTN_TEXT;
 if (hasAlarmsOnOtherPages && _alarmFlashPhase) {
 _driver.canvas->fillRoundRect(x, 0, btnW, 40, 12, RGB565(180, 30, 30));
 pagTxtCol = RGB565(255, 255, 255);
 } else if (hasAlarmsOnOtherPages) {
 _driver.canvas->fillRoundRect(x, 0, btnW, 40, 12, C_CARD_BG);
 _driver.canvas->drawRoundRect(x, 0, btnW, 40, 12, RGB565(255, 60, 60));
 } else {
 _driver.canvas->drawRoundRect(x, 0, btnW, 40, 12, C_TEXT_SUB);
 }
 char pageStr[4]; snprintf(pageStr, sizeof(pageStr), "%d", _currentPage + 1);
 char totStr[4]; snprintf(totStr, sizeof(totStr), "/%d", totalPages);
 _driver.canvas->setFont(&simutFont12pt); _driver.canvas->setTextColor(pagTxtCol);
 _driver.canvas->setCursor(x + 15, 28); _driver.canvas->print(pageStr);
 _driver.canvas->setFont(NULL); _driver.canvas->setCursor(x + 35, 8); _driver.canvas->print(totStr);
 }
 }
 /* h=41 instead of 45 ensures 4 px bottom margin (y+h=236 <= 236). */
 blitCanvas(_driver.canvas, 0, BTNBAR_Y, DASH_W, BTNBAR_H);
}
