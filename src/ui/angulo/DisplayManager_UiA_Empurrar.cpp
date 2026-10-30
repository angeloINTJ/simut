/**
 * @file DisplayManager_UiA_Empurrar.cpp
 * @brief The Ângulo build's pipelined strip push: DisplayManager::uaEmpurrar( ) starts the DMA burst of a strip and returns, and uaEmpurrarEsperar( ) waits for it, so
 *        that uaPintar( ) (DisplayManager_UiA.cpp) composes the next strip while this one streams. pico_wiretft_angulo only.
 *
 * blitCanvas( ) -> blitWindowDma( ) waits for its channel before it returns, so a strip's compose and its push added up (the calendar on the board, 2026-10-15:
 * 34 ms of compose + 21.5 ms of push, six strips of 3.5 ms). Here the burst is started and the caller composes the next strip in the canvas's other half
 * (Faixa::para( )); the next push, or the end of uaPintar( ), waits for it. The strips are 320 x 20, so a push is about half of a 40-row strip's 3.5 ms, less than a
 * strip's compose.
 *
 * Every user of spi0 and of the TFT's DMA channel, and how each stays off the bus while a push is in flight (read from the code, 2026-10-15):
 * - The XPT2046 touch read (loopCore1: ts->touched( ), on this SPI) runs on Core 1 at the top of each loop iteration, before uiPass( ) -> uaQuadro( ) -> uaPintar( ).
 *   uaPintar( ) returns with no burst in flight, so the read cannot overlap one. The touch pin's interrupt (isrPin) only sets a flag, and so does the alarm ISR Core 1
 *   owns; SPI.beginTransaction( ) masks only the interrupts registered with usingInterrupt( ), and nothing in src/ does.
 * - The GRAM readback of GET /api/screenshot and of the mirror (readRect, readRectDmaStart; Core 0) runs only with Core 1 parked at the top of loopCore1 (the "SPI-bus
 *   exclusion without the IRQ lockout" of DisplayManager.h; the flash pause uses the same park point), which Core 1 reaches after uaPintar( ) has returned. Their DMA
 *   channels are two others (dmaRdChannel, dmaRdTxChannel); this one is the driver's own (canalDma( )). The core's pushes (blitWindowDma( ), fillWindowDma( ), through
 *   blitCanvas( ), fastFillRect( ), fastClearScreen( )) are not linked into this image (`nm -C` lists none of them), and uaPintar( ) calls none of them.
 * - The update screen: uiPaintOta( ) -> uaPintar( ), on Core 1 from otaTick( ), or on Core 0 while Core 1 is parked (DisplayManager_OtaCore.cpp). The same function, the same
 *   rule: it returns with the bus free, and while Core 0 paints Core 1 is parked, so the bus has one user.
 * - fillMarginsBlack( ), at the start of a full repaint, is the library's synchronous path and runs before the frame's first burst.
 * While a burst is in flight the SPI transaction stays open and CS stays low: nothing on the composing core touches spi0 in that time.
 *
 * The alignment offset (Ajustes, Alinhar; +-4 px): blitCanvas( ) adds it to the destination, and blitWindowDma( ) refuses a strip with a pixel off the glass, which then
 * took drawRGBBitmap( ), about 2 us a pixel and synchronous: 154 ms a frame with a column offset (every strip is off the glass), 12.8 ms for the one 20-row strip a row
 * offset pushes off (25.6 ms with 40-row strips). Here the part of the strip on the glass is sent by DMA at any offset (ua::recortar( ), ua::compactar( ), Recorte.h: the
 * part's rows packed in place, a memmove of the strip and not a pixel loop).
 *
 * The open and close of the bus repeat dmaTftOpen( ) and dmaTftClose( ) of DisplayManager_Panel.cpp, which are static there; tools/check_ui_boundary.py keeps this
 * driver's names out of that file. A change to the panel's RAM-write sequence is made in both, and the two are kept the same. That includes g_tftSpiBaud: dmaTftOpen( )
 * writes the baud SPI0 was programmed to (spi_get_baudrate( spi0 ), from the PL022 registers; GET /api/status, sys.tft_hz) right after SPI.beginTransaction( ), and so does
 * uaEmpurrar( ) below. Every push of the Ângulo image goes through this copy, so without the write sys.tft_hz would read 0 on it (PR #278 put the write in dmaTftOpen( ) alone).
 */

#include "../../DisplayManager.h"
#include "../../simut_config.h"   /* TFT_CS, TFT_DC */
#include "../../FlashIrqProbe.h"   /* g_tftSpiBaud */

#include <SPI.h>
#include <hardware/dma.h>
#include <hardware/spi.h>
#include <string.h>

#include "Recorte.h"

namespace {

/* This driver's own channel, claimed on first use; -1 when none is free and the library pushes. */
int canalDma( ) {
  static int s_ch = -1;
  if ( s_ch < 0 ) s_ch = dma_claim_unused_channel( false );
  return s_ch;
}

bool s_voando = false;   /* the bus is open for a burst (set before the transaction begins): one at a time, on Core 1 or on Core 0 while Core 1 is parked */

}  // namespace

void DisplayManager::uaEmpurrarEsperar( ) {
  if ( !s_voando ) return;
  dma_channel_wait_for_finish_blocking( canalDma( ) );
  while ( spi_get_hw( spi0 )->sr & SPI_SSPSR_BSY_BITS ) tight_loop_contents( );   /* the last pixels are out of the shift register */
  spi_set_format( spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST );
  digitalWrite( TFT_CS, HIGH );
  SPI.endTransaction( );
  s_voando = false;
}

/* Starts the push of the w x h strip at (x, y) of the image; true when a burst is in flight, reading `buf`, which must not be written until uaEmpurrarEsperar( ) (the part
 * of the strip on the glass is packed into `buf` first, so its pixels are consumed). False when nothing is in flight: the strip is wholly off the glass, or no DMA channel
 * was free and the library pushed it (synchronously, as blitCanvas( ) does). */
bool DisplayManager::uaEmpurrar( uint16_t *buf, int16_t x, int16_t y, int16_t w, int16_t h ) {
  uaEmpurrarEsperar( );
  if ( !buf || !_driver.tft ) return false;
  const ua::Recorte r = ua::recortar( (int32_t)x + _driver.tft->getOffsetX( ), (int32_t)y + _driver.tft->getOffsetY( ), w, h, DisplayDriver::W, DisplayDriver::H );
  if ( r.vazio ) return false;
  const uint16_t *const bloco = ua::compactar( buf, w, r );
  const int ch = canalDma( );
  _driver.tft->setOffsetBypass( true );   /* the offset is applied above: setAddrWindow( ) must not add it again */
  if ( ch < 0 ) {
    _driver.tft->drawRGBBitmap( r.x, r.y, const_cast<uint16_t *>( bloco ), r.w, r.h );
    _driver.tft->setOffsetBypass( false );
    return false;
  }
  /* The window, then the RAM write, as dmaTftOpen( ) does it: the library latches the window, and CS is asserted again for RAMWR and the stream. */
  _driver.tft->startWrite( );
  _driver.tft->setAddrWindow( r.x, r.y, r.w, r.h );
  _driver.tft->endWrite( );
  _driver.tft->setOffsetBypass( false );
  /* The flag before the bus is touched: from here until uaEmpurrarEsperar( ) closes it the transaction is open, CS low and the format 16 bits, so a restart of
   * Core 1 anywhere after this line finds the flag set and the next call closes the bus. Closing what was not yet opened is harmless: the wait returns at once,
   * the format and CS are already as the close leaves them. */
  s_voando = true;
  SPI.beginTransaction( SPISettings( SIMUT_TFT_SPI_HZ, MSBFIRST, SPI_MODE0 ) );
  g_tftSpiBaud = spi_get_baudrate( spi0 );   /* what the block really got, not what was asked: the same line as in dmaTftOpen( ), which these two sequences keep */
  digitalWrite( TFT_CS, LOW );
  digitalWrite( TFT_DC, LOW );
  SPI.transfer( 0x2C ); /* RAMWR */
  digitalWrite( TFT_DC, HIGH );
  spi_set_format( spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST );
  dma_channel_config c = dma_channel_get_default_config( ch );
  channel_config_set_transfer_data_size( &c, DMA_SIZE_16 );
  channel_config_set_dreq( &c, DREQ_SPI0_TX );
  channel_config_set_read_increment( &c, true );
  channel_config_set_write_increment( &c, false );
  dma_channel_configure( ch, &c, &spi_get_hw( spi0 )->dr, bloco, (uint32_t)r.w * (uint32_t)r.h, true );
  return true;
}
