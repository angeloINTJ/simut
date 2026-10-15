// The smoothed corners and circles of ua::Pintor (src/ui/angulo/Angulo.cpp), checked on the host
// in Adafruit GFX's GFXcanvas16. Run by tools/tft_ui/test_cantos.py, which also builds it with
// -DCANTOS_SEM_LEITOR (the shapes drawn by Adafruit GFX itself, its all-or-nothing arcs) as the control
// that must fail the intermediate-tone checks.
#include <Adafruit_GFX.h>
#include <stdio.h>

#include "Angulo.h"

namespace {

class LeitorCanvas : public ua::Leitor {
public:
  explicit LeitorCanvas( GFXcanvas16 &c ) : c_( c ) {}
  uint16_t ler( int16_t x, int16_t y ) override { return c_.getPixel( x, y ); }

private:
  GFXcanvas16 &c_;
};

constexpr uint16_t FUNDO = 0xFFFF;   // white
constexpr uint16_t COR = 0x0000;     // black: the tone between them is the coverage
int falhas = 0;

void conferir( bool ok, const char *caso, const char *o_que ) {
  printf( "%s %s: %s\n", ok ? "ok  " : "FAIL", caso, o_que );
  if ( !ok ) falhas++;
}

// 0 = background, 16 = the colour, by the green channel (6 bits, the finest).
int nivel( GFXcanvas16 &c, int16_t x, int16_t y ) {
  const int g = ( c.getPixel( x, y ) >> 5 ) & 63;
  return ( ( 63 - g ) * 16 + 31 ) / 63;
}

ua::Tema tema( ) {
  ua::Tema t = ua::temaClaro( );
  return t;
}

void caixa( const char *caso, int16_t w, int16_t h, int16_t r, bool contorno ) {
  GFXcanvas16 cv( 160, 120 );
  LeitorCanvas leitor( cv );
  const ua::Tema t = tema( );
  ua::Pintor p( cv, t, leitor );
  p.clear( FUNDO );
  const int16_t x = 10, y = 10;
#ifdef CANTOS_SEM_LEITOR
  // The control: Adafruit GFX's own arcs, what the Pintor drew before 2026-10-15.
  const int16_t mm = ( w < h ? w : h ) / 2, rr = r > mm ? mm : r;
  if ( contorno ) cv.drawRoundRect( x, y, w, h, rr, COR );
  else cv.fillRoundRect( x, y, w, h, rr, COR );
#else
  if ( contorno ) p.roundRect( x, y, w, h, r, COR );
  else p.fillRoundRect( x, y, w, h, r, COR );
#endif
  const int16_t m = ( w < h ? w : h ) / 2, R = r > m ? m : r;
  // 1. Nothing outside the box [x, x + w) by [y, y + h).
  bool fora = true;
  for ( int16_t j = 0; j < 120; j++ )
    for ( int16_t i = 0; i < 160; i++ )
      if ( ( i < x || i >= x + w || j < y || j >= y + h ) && cv.getPixel( i, j ) != FUNDO ) fora = false;
  conferir( fora, caso, "nothing is drawn outside the box" );
  // 2. The straight parts as before: the middle of the top edge is the colour, and so is the
  //    middle of the left edge; for a fill the centre too, for an outline the centre is untouched.
  conferir( nivel( cv, x + w / 2, y ) == 16 && nivel( cv, x, y + h / 2 ) == 16, caso,
            "the straight edges are the colour" );
  conferir( contorno ? cv.getPixel( x + w / 2, y + h / 2 ) == FUNDO : nivel( cv, x + w / 2, y + h / 2 ) == 16, caso,
            contorno ? "an outline leaves the inside untouched" : "a fill covers the inside" );
  // 3. The corner has tones between the background and the colour (the smoothing).
  int intermedios = 0;
  for ( int16_t j = 0; j < R; j++ )
    for ( int16_t i = 0; i < R; i++ ) {
      const int n = nivel( cv, x + i, y + j );
      if ( n > 0 && n < 16 ) intermedios++;
    }
  char msg[96];
  snprintf( msg, sizeof msg, "the top-left corner has tones in between (%d pixels)", intermedios );
  conferir( intermedios >= ( R >= 4 ? 3 : 1 ), caso, msg );
  // 4. The four corners are mirror images.
  bool simetria = true;
  for ( int16_t j = 0; j < R; j++ )
    for ( int16_t i = 0; i < R; i++ ) {
      const uint16_t a = cv.getPixel( x + i, y + j );
      if ( a != cv.getPixel( x + w - 1 - i, y + j ) || a != cv.getPixel( x + i, y + h - 1 - j ) ||
           a != cv.getPixel( x + w - 1 - i, y + h - 1 - j ) )
        simetria = false;
    }
  conferir( simetria, caso, "the four corners are mirror images" );
  // 5. Along the top edge, from the corner inward, a fill only gets darker (coverage never drops).
  if ( !contorno ) {
    bool monotono = true;
    for ( int16_t i = 1; i <= R; i++ )
      if ( nivel( cv, x + i, y ) < nivel( cv, x + i - 1, y ) ) monotono = false;
    conferir( monotono, caso, "along the top edge the coverage never drops toward the middle" );
  }
}

void circulo( const char *caso, int16_t r, bool contorno ) {
  GFXcanvas16 cv( 64, 64 );
  LeitorCanvas leitor( cv );
  const ua::Tema t = tema( );
  ua::Pintor p( cv, t, leitor );
  p.clear( FUNDO );
  const int16_t cx = 30, cy = 30;
#ifdef CANTOS_SEM_LEITOR
  if ( contorno ) cv.drawCircle( cx, cy, r, COR );
  else cv.fillCircle( cx, cy, r, COR );
#else
  if ( contorno ) p.circle( cx, cy, r, COR );
  else p.fillCircle( cx, cy, r, COR );
#endif
  bool fora = true;
  for ( int16_t j = 0; j < 64; j++ )
    for ( int16_t i = 0; i < 64; i++ )
      if ( ( i < cx - r || i > cx + r || j < cy - r || j > cy + r ) && cv.getPixel( i, j ) != FUNDO ) fora = false;
  conferir( fora, caso, "nothing is drawn outside the circle's box" );
  conferir( nivel( cv, cx + r, cy ) >= 8 && nivel( cv, cx, cy - r ) >= 8, caso,
            "the points on the axes at the radius are mostly the colour" );
  conferir( contorno ? cv.getPixel( cx, cy ) == FUNDO : nivel( cv, cx, cy ) == 16, caso,
            contorno ? "an outline leaves the centre untouched" : "a fill covers the centre" );
  int intermedios = 0;
  for ( int16_t j = -r; j <= r; j++ )
    for ( int16_t i = -r; i <= r; i++ ) {
      const int n = nivel( cv, cx + i, cy + j );
      if ( n > 0 && n < 16 ) intermedios++;
    }
  char msg[96];
  snprintf( msg, sizeof msg, "the edge has tones in between (%d pixels)", intermedios );
  conferir( intermedios >= 4, caso, msg );
}

}  // namespace

int main( ) {
  caixa( "button 72x36 r6, fill", 72, 36, ua::RAIO_CONTROLE, false );
  caixa( "button 72x36 r6, outline", 72, 36, ua::RAIO_CONTROLE, true );
  caixa( "card 120x80 r12, fill", 120, 80, ua::RAIO_CARTAO, false );
  caixa( "card 120x80 r12, outline", 120, 80, ua::RAIO_CARTAO, true );
  caixa( "pill 60x24 full radius, fill", 60, 24, ua::RAIO_TOTAL, false );
  caixa( "pill 61x25 full radius, outline", 61, 25, ua::RAIO_TOTAL, true );
  circulo( "dot r3, fill", 3, false );
  circulo( "knob r8, fill", 8, false );
  circulo( "ring r6, outline", 6, true );
  printf( "%s: %d failure(s)\n", falhas ? "FAIL" : "OK", falhas );
  return falhas ? 1 : 0;
}
