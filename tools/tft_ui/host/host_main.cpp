// Draws the plan's family A screens ported to the kit (tools/tft_ui/plano)
// into Adafruit GFX's own GFXcanvas16, both themes, and writes each frame
// (RGB565, host byte order) to argv[1]/A_<name>_<tema>.565.
#include <Adafruit_GFX.h>
#include <stdio.h>

#include "Angulo.h"
#include "Plano.h"

int main( int argc, char **argv ) {
  if ( argc < 2 ) { fprintf( stderr, "usage: %s <folder>\n", argv[0] ); return 2; }
  GFXcanvas16 canvas( 320, 240 );
  int n = 0;
  int faltam = 0;
  for ( const char *const *nome = plano::kNomes; *nome; nome++ ) {
    if ( !plano::portado( *nome ) ) { faltam++; continue; }
    for ( int t = 0; t < 2; t++ ) {
      const ua::Tema &tema = t ? ua::temaClaro( ) : ua::temaEscuro( );
      canvas.fillScreen( 0xF81F );
      ua::Pintor p( canvas, tema );
      if ( !plano::desenhar( p, *nome ) ) { fprintf( stderr, "not ported: %s\n", *nome ); return 1; }
      char path[512];
      snprintf( path, sizeof path, "%s/A_%s_%s.565", argv[1], *nome, t ? "claro" : "escuro" );
      FILE *f = fopen( path, "wb" );
      if ( !f ) { perror( path ); return 2; }
      fwrite( canvas.getBuffer( ), 2, 320 * 240, f );
      fclose( f );
      n++;
    }
  }
  for ( const char *const *nome = plano::kNomesTeclado; *nome; nome++ ) {
    if ( !plano::portado( *nome ) ) { faltam++; continue; }
    for ( int t = 0; t < 2; t++ ) {
      const ua::Tema &tema = t ? ua::temaClaro( ) : ua::temaEscuro( );
      canvas.fillScreen( 0xF81F );
      ua::Pintor p( canvas, tema );
      plano::desenhar( p, *nome );
      char path[512];
      snprintf( path, sizeof path, "%s/%s_%s.565", argv[1], *nome, t ? "claro" : "escuro" );
      FILE *f = fopen( path, "wb" );
      if ( !f ) { perror( path ); return 2; }
      fwrite( canvas.getBuffer( ), 2, 320 * 240, f );
      fclose( f );
      n++;
    }
  }
  printf( "%d frames (%d examples not ported yet), %u glyphs or icons missing\n", n, faltam,
          (unsigned)ua::glifosFaltando( ) );
  return ua::glifosFaltando( ) ? 1 : 0;
}
