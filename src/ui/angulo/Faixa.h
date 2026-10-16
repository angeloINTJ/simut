#pragma once
// The Ângulo's strip: Adafruit_GFX over one 40-row band of the panel's 320 x 240, the surface a screen is composed on, one band at a time
// (DisplayManager::uaPintar( ), DisplayManager_UiA.cpp). It lived in that file until 2026-10-16. It is here so that the host draws through the
// very class the firmware does: tools/tft_ui/host/host_perf_main.cpp times the compose in six strips and proves the six strips equal the
// frame drawn whole, and the speed-ups of the compose are measured on the code that ships.
//
// Each strip draws the whole screen and keeps what falls in it, the price of a flicker-free frame without a 150 kB frame buffer; so the pixel
// path writes the buffer itself (GFXcanvas16's layout: row-major, native order) instead of a second virtual call per pixel.
#include <Adafruit_GFX.h>
#include <stdint.h>

#include "Angulo.h"

namespace ua {

// The panel the strips tile (DisplayDriver::W and H; DisplayManager_UiA.cpp asserts they are these) and the strip's height.
constexpr int LARGURA_PAINEL = 320, ALTURA_PAINEL = 240;
constexpr int16_t ALTURA_FAIXA = 40;
static_assert( ALTURA_PAINEL % ALTURA_FAIXA == 0, "the strips tile the panel's height" );
constexpr int FAIXAS = ALTURA_PAINEL / ALTURA_FAIXA;

// The record of a frame (Pintor::lista( )) lives in the firmware's canvas, under the strip: the canvas is 320 x 45 (DisplayManager.cpp), the strip takes
// 40 of its rows and the other 5 were idle during a paint, 3,200 B. The host harness lends an array of this size, so that a frame that fits there
// fits in the canvas. A record's offsets into the list are 16 bits.
constexpr int LINHAS_DA_LISTA = 5;
constexpr size_t BYTES_DA_LISTA = (size_t)LINHAS_DA_LISTA * LARGURA_PAINEL * sizeof( uint16_t );
static_assert( BYTES_DA_LISTA <= 32767, "a record's offsets into the list are 16 bits" );

class Faixa : public Adafruit_GFX, public Leitor {
public:
  // buf: LARGURA_PAINEL x ALTURA_FAIXA pixels (the firmware's is the old screens' canvas, 45 rows of which 40 are used).
  explicit Faixa( uint16_t *buf ) : Adafruit_GFX( LARGURA_PAINEL, ALTURA_PAINEL ), buf_( buf ) {}
  // Aims at strip k (0 to FAIXAS - 1): its rows of the panel, for this surface and for the Pintor's window.
  void para( int k, Pintor &p ) {
    y0_ = (int16_t)( k * ALTURA_FAIXA );
    p.janela( y0_, (int16_t)( y0_ + ALTURA_FAIXA ) );
    p.bruto( buf_, LARGURA_PAINEL, y0_, ALTURA_FAIXA );
  }
  void drawPixel( int16_t x, int16_t y, uint16_t c ) override {
    y = (int16_t)( y - y0_ );
    if ( (uint16_t)x >= (unsigned)LARGURA_PAINEL || (uint16_t)y >= (uint16_t)ALTURA_FAIXA ) return;
    buf_[(int32_t)y * LARGURA_PAINEL + x] = c;
  }
  void fillRect( int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c ) override {
    int16_t x0 = x < 0 ? 0 : x, x1 = x + w > LARGURA_PAINEL ? LARGURA_PAINEL : (int16_t)( x + w );
    int16_t r0 = (int16_t)( y - y0_ ), r1 = (int16_t)( r0 + h );
    if ( r0 < 0 ) r0 = 0;
    if ( r1 > ALTURA_FAIXA ) r1 = ALTURA_FAIXA;
    if ( x0 >= x1 || r0 >= r1 ) return;
    // Two pixels to a 32-bit store wherever the run is on a word (a row of this buffer is 320 pixels, and the buffer is word aligned), four stores
    // a pass: the clear of a strip is 12,800 pixels, six times a frame, and the screens fill rectangles all the time. The ends go one pixel at a time.
    const uint32_t dupla = (uint32_t)c | (uint32_t)c << 16;
    for ( int16_t r = r0; r < r1; r++ ) {
      uint16_t *p = buf_ + (int32_t)r * LARGURA_PAINEL + x0;
      int n = x1 - x0;
      if ( ( reinterpret_cast<uintptr_t>( p ) & 2 ) && n > 0 ) {
        *p++ = c;
        n--;
      }
      Palavra *q = reinterpret_cast<Palavra *>( p );
      for ( ; n >= 8; n -= 8, q += 4 ) {
        q[0] = dupla;
        q[1] = dupla;
        q[2] = dupla;
        q[3] = dupla;
      }
      for ( ; n >= 2; n -= 2 ) *q++ = dupla;
      if ( n ) *reinterpret_cast<uint16_t *>( q ) = c;
    }
  }
  void drawFastHLine( int16_t x, int16_t y, int16_t w, uint16_t c ) override { fillRect( x, y, w, 1, c ); }
  void drawFastVLine( int16_t x, int16_t y, int16_t h, uint16_t c ) override { fillRect( x, y, 1, h, c ); }
  void fillScreen( uint16_t c ) override { fillRect( 0, y0_, LARGURA_PAINEL, ALTURA_FAIXA, c ); }
  // The pixel already in the strip, for the smoothed corners to mix with; a row outside the strip is not kept, so what it reads there does
  // not matter.
  uint16_t ler( int16_t x, int16_t y ) override {
    y = (int16_t)( y - y0_ );
    if ( (uint16_t)x >= (unsigned)LARGURA_PAINEL || (uint16_t)y >= (uint16_t)ALTURA_FAIXA ) return 0;
    return buf_[(int32_t)y * LARGURA_PAINEL + x];
  }

private:
  uint16_t *buf_;
  int16_t y0_ = 0;
};

}  // namespace ua
