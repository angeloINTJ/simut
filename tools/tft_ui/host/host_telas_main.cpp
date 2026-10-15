// Draws every sample of one group of the functional build's screens
// (tools/tft_ui/amostras/<grupo>.cpp) into Adafruit GFX's own GFXcanvas16, in
// both themes, and writes, for host_telas.py to check and picture:
//   <dir>/<nome>_<tema>.565        the frame (RGB565, host byte order)
//   <dir>/<nome>_<tema>.textos     one line per string drawn: x0 x1 y0 y1 text
//   <dir>/<nome>.alvos             one line per touch target: x y w h tipo indice
//   <dir>/<nome>_<tema>.faltam     the glyphs or icons the kit could not draw
#include <Adafruit_GFX.h>
#include <stdio.h>
#include <string.h>

#include "Amostras.h"

// The canvas read back, for the smoothed corners (ua::Leitor).
class LeitorCanvas : public ua::Leitor {
public:
  explicit LeitorCanvas( GFXcanvas16 &c ) : c_( c ) {}
  uint16_t ler( int16_t x, int16_t y ) override { return c_.getPixel( x, y ); }

private:
  GFXcanvas16 &c_;
};

int main( int argc, char **argv ) {
  if ( argc < 2 ) { fprintf( stderr, "usage: %s <folder>\n", argv[0] ); return 2; }
  GFXcanvas16 canvas( 320, 240 );
  LeitorCanvas leitor( canvas );
  char path[512];
  for ( int i = 0; i < amostra::kAmostrasN; i++ ) {
    const amostra::Amostra &a = amostra::kAmostras[i];
    for ( int t = 0; t < 2; t++ ) {
      const ua::Tema &tema = t ? ua::temaClaro( ) : ua::temaEscuro( );
      const char *nomeTema = t ? "claro" : "escuro";
      // The firmware clears each strip to the theme's background before a
      // screen draws (DisplayManager_UiA.cpp); the host does the same.
      canvas.fillScreen( tema.fundo );
      ua::Pintor p( canvas, tema, leitor );
      ua::hostLimparTextos( );
      const uint32_t faltavam = ua::glifosFaltando( );
      a.desenhar( p );
      snprintf( path, sizeof path, "%s/%s_%s.565", argv[1], a.nome, nomeTema );
      FILE *f = fopen( path, "wb" );
      if ( !f ) { perror( path ); return 2; }
      fwrite( canvas.getBuffer( ), 2, 320 * 240, f );
      fclose( f );
      snprintf( path, sizeof path, "%s/%s_%s.textos", argv[1], a.nome, nomeTema );
      f = fopen( path, "w" );
      if ( !f ) { perror( path ); return 2; }
      const ua::TextoDesenhado *txt;
      const int n = ua::hostTextos( &txt );
      for ( int k = 0; k < n; k++ )
        fprintf( f, "%d %d %d %d %s\n", txt[k].x0, txt[k].x1, txt[k].y0, txt[k].y1, txt[k].texto );
      fclose( f );
      snprintf( path, sizeof path, "%s/%s_%s.faltam", argv[1], a.nome, nomeTema );
      f = fopen( path, "w" );
      if ( !f ) { perror( path ); return 2; }
      fprintf( f, "%u\n", (unsigned)( ua::glifosFaltando( ) - faltavam ) );
      fclose( f );
    }
    snprintf( path, sizeof path, "%s/%s.alvos", argv[1], a.nome );
    FILE *f = fopen( path, "w" );
    if ( !f ) { perror( path ); return 2; }
    if ( a.alvos ) {
      ua::Alvo alvos[ua::MAX_ALVOS];
      const int n = a.alvos( alvos );
      for ( int k = 0; k < n; k++ )
        fprintf( f, "%d %d %d %d %u %d\n", alvos[k].x, alvos[k].y, alvos[k].w, alvos[k].h, alvos[k].tipo,
                 alvos[k].indice );
    }
    fclose( f );
  }
  printf( "%d samples, %d frames\n", amostra::kAmostrasN, 2 * amostra::kAmostrasN );
  return 0;
}
