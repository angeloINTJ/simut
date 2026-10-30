// Draws every sample of one group of the functional build's screens
// (tools/tft_ui/amostras/<grupo>.cpp) into Adafruit GFX's own GFXcanvas16, in
// both themes, and writes, for host_telas.py to check and picture:
//   <dir>/<nome>_<tema>.565        the frame (RGB565, host byte order)
//   <dir>/<nome>_<tema>.textos     one line per string drawn: x0 x1 y0 y1 kind text
//                                  (kind t: the ink box of a text; s: the box of a badge, its label as text)
//   <dir>/<nome>_<tema>.controles  one line per control a finger is meant to press: x y w h estado tipo vivos [motivo]
//                                  (estado on / off: drawn enabled / disabled; sob: dimmed on purpose under a live target;
//                                  nao: the call said it is not a control. vivos: how many pixels of the box the hit test
//                                  itself, ua::alvoEm( ), answers with a live target, one that is not inert; motivo: the
//                                  call's reason for sob and nao)
//   <dir>/<nome>.alvos             one line per touch target: x y w h tipo indice
//   <dir>/<nome>.contagem          the targets listed, the targets addAlvo( ) dropped because the list was full, MAX_ALVOS
//   <dir>/<nome>_<tema>.faltam     the glyphs or icons the kit could not draw
//   <dir>/<nome>_<tema>.superficies  one line per surface drawn with fillRoundRect( ) or roundRect( ): x y w h
#include <Adafruit_GFX.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Amostras.h"
#include "Textos.h"

// The canvas read back, for the smoothed corners (ua::Leitor).
class LeitorCanvas : public ua::Leitor {
public:
  explicit LeitorCanvas( GFXcanvas16 &c ) : c_( c ) {}
  uint16_t ler( int16_t x, int16_t y ) override { return c_.getPixel( x, y ); }

private:
  GFXcanvas16 &c_;
};

constexpr int MAX_CONTROLES = 128;   // the recorder's own limit (Angulo.cpp): a sample over it stops the harness

// How many pixels of a control's box the hit test the firmware runs gives to a live target (not inert). The
// glue acts on that answer, so this is what a finger on the control would do.
static int pixelsVivos( const ua::Alvo *alvos, int n, const ua::ControleDesenhado &c ) {
  int vivos = 0;
  for ( int y = c.y; y < c.y + c.h; y++ )
    for ( int x = c.x; x < c.x + c.w; x++ ) {
      const ua::Alvo *a = ua::alvoEm( alvos, n, (int16_t)x, (int16_t)y );
      if ( a && a->tipo != ua::ALVO_INERTE ) vivos++;
    }
  return vivos;
}

// The pack's @ANGULO section (the body, as host_telas.py cut it out of the pack), through the code the device runs: what a screen draws through UA_TXT( )
// is then the pack's text. A section the device would refuse stops the harness.
static int carregarTextos( const char *caminho ) {
  FILE *f = fopen( caminho, "rb" );
  if ( !f ) { perror( caminho ); return 2; }
  fseek( f, 0, SEEK_END );
  const long n = ftell( f );
  fseek( f, 0, SEEK_SET );
  char *corpo = (char *)malloc( n > 0 ? (size_t)n : 1 );
  const size_t lidos = n > 0 ? fread( corpo, 1, (size_t)n, f ) : 0;
  fclose( f );
  if ( n < 0 || lidos != (size_t)n ) { fprintf( stderr, "%s: short read\n", caminho ); free( corpo ); return 2; }
  const ua::TextosResultado r = ua::textosCarregar( corpo, (size_t)n );
  free( corpo );
  static const char *const nomes[] = { "loaded", "empty", "over UA_TEXTOS_MAX", "malformed line", "repeated hash", "out of memory", "already loaded" };
  printf( "idioma: @ANGULO %s, %zu texts, %zu B in RAM, section of %ld B\n", nomes[r], ua::textosContagem( ), ua::textosBytes( ), n );
  if ( r != ua::TEXTOS_OK && r != ua::TEXTOS_VAZIO ) { fprintf( stderr, "the device would refuse this section: %s\n", nomes[r] ); return 2; }
  return 0;
}

int main( int argc, char **argv ) {
  if ( argc < 2 ) { fprintf( stderr, "usage: %s <folder> [accent [@ANGULO section file]]\n", argv[0] ); return 2; }
  const uint8_t acento = argc > 2 ? (uint8_t)atoi( argv[2] ) : 0;   // the palette entry; 0 is the stock one, the frames V-PIX holds
  if ( argc > 3 ) {
    const int rc = carregarTextos( argv[3] );
    if ( rc ) return rc;
  }
  GFXcanvas16 canvas( 320, 240 );
  LeitorCanvas leitor( canvas );
  char path[512];
  static ua::ControleDesenhado controles[2][MAX_CONTROLES];
  int nControles[2] = { 0, 0 };
  for ( int i = 0; i < amostra::kAmostrasN; i++ ) {
    const amostra::Amostra &a = amostra::kAmostras[i];
    for ( int t = 0; t < 2; t++ ) {
      const ua::Tema &tema = ua::tema( t == 0, acento );
      const char *nomeTema = t ? "claro" : "escuro";
      // The firmware clears each strip to the theme's background before a
      // screen draws (DisplayManager_UiA.cpp); the host does the same.
      canvas.fillScreen( tema.fundo );
      ua::Pintor p( canvas, tema, leitor );
      // The text goes straight into the canvas buffer, as it does into the firmware's strip: V-PIX holds these frames to the ones the GFX path drew.
      p.bruto( canvas.getBuffer( ), 320, 0, 240 );
      ua::hostLimparTextos( );
      ua::hostLimparControles( );
      ua::hostLimparSuperficies( );
      const uint32_t faltavam = ua::glifosFaltando( );
      a.desenhar( p );
      // And the margin black after it, the call uaPintar( ) makes after each strip: the frame written below is the glass's.
      ua::margemPreta( p );
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
        fprintf( f, "%d %d %d %d %c %s\n", txt[k].x0, txt[k].x1, txt[k].y0, txt[k].y1, txt[k].selo ? 's' : 't',
                 txt[k].texto );
      fclose( f );
      snprintf( path, sizeof path, "%s/%s_%s.superficies", argv[1], a.nome, nomeTema );
      f = fopen( path, "w" );
      if ( !f ) { perror( path ); return 2; }
      const ua::SuperficieDesenhada *sup;
      const int nSup = ua::hostSuperficies( &sup );
      for ( int k = 0; k < nSup; k++ ) fprintf( f, "%d %d %d %d\n", sup[k].x, sup[k].y, sup[k].w, sup[k].h );
      fclose( f );
      // The controls are written below, once the sample's targets are known.
      const ua::ControleDesenhado *ctl;
      nControles[t] = ua::hostControles( &ctl );
      for ( int k = 0; k < nControles[t]; k++ ) controles[t][k] = ctl[k];
      snprintf( path, sizeof path, "%s/%s_%s.faltam", argv[1], a.nome, nomeTema );
      f = fopen( path, "w" );
      if ( !f ) { perror( path ); return 2; }
      fprintf( f, "%u\n", (unsigned)( ua::glifosFaltando( ) - faltavam ) );
      fclose( f );
    }
    ua::Alvo alvos[ua::MAX_ALVOS];
    int nAlvos = 0;
    ua::hostLimparAlvosPerdidos( );
    if ( a.alvos ) nAlvos = a.alvos( alvos );
    const int perdidos = ua::hostAlvosPerdidos( );
    snprintf( path, sizeof path, "%s/%s.alvos", argv[1], a.nome );
    FILE *f = fopen( path, "w" );
    if ( !f ) { perror( path ); return 2; }
    for ( int k = 0; k < nAlvos; k++ )
      fprintf( f, "%d %d %d %d %u %d\n", alvos[k].x, alvos[k].y, alvos[k].w, alvos[k].h, alvos[k].tipo, alvos[k].indice );
    fclose( f );
    snprintf( path, sizeof path, "%s/%s.contagem", argv[1], a.nome );
    f = fopen( path, "w" );
    if ( !f ) { perror( path ); return 2; }
    fprintf( f, "%d %d %d\n", nAlvos, perdidos, (int)ua::MAX_ALVOS );
    fclose( f );
    for ( int t = 0; t < 2; t++ ) {
      snprintf( path, sizeof path, "%s/%s_%s.controles", argv[1], a.nome, t ? "claro" : "escuro" );
      f = fopen( path, "w" );
      if ( !f ) { perror( path ); return 2; }
      for ( int k = 0; k < nControles[t]; k++ ) {
        const ua::ControleDesenhado &c = controles[t][k];
        fprintf( f, "%d %d %d %d %s %s %d %s\n", c.x, c.y, c.w, c.h,
                 c.excluido[0] ? "nao" : c.sob[0] ? "sob" : c.habilitado ? "on" : "off", c.tipo, pixelsVivos( alvos, nAlvos, c ),
                 c.excluido[0] ? c.excluido : c.sob );
      }
      fclose( f );
    }
  }
  printf( "%d samples, %d frames\n", amostra::kAmostrasN, 2 * amostra::kAmostrasN );
  return 0;
}
