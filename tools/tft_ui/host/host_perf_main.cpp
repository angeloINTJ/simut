// Times the compose of every sample of one group of the Ângulo's screens the way DisplayManager::uaPintar( ) does it, and proves that it draws
// the same frame. Built without UA_HOST, as the firmware is, by tools/tft_ui/perf_telas.py. It is not a check of the screens (that is
// host_telas_main.cpp) but a ruler for the speed-ups of the compose: microseconds on an x86 host show the ratio between two builds, not the
// panel's time.
//
// For each sample and theme, one frame is composed as uaPintar( ) composes it: six 320 x 40 strips (ua::Faixa, the firmware's own class), each
// cleared to the theme's background, drawn by the screen's function clipped to the strip, given the black margin, and hashed. Printed, in
// microseconds per frame (the best of three batches of <reps>): the whole, then its three parts (the clear, the draw with the margin, the hash),
// then the number of pixels in which the six strips, stitched, differ from the same sample drawn whole in a GFXcanvas16 by Adafruit GFX's own
// path (0 when the strips are the frame):
//   S <sample> <theme> <us> <clear> <draw> <hash> <differing pixels>
//   TOTAL <samples> <us> <clear> <draw> <hash> <differing pixels> <most calls in a record> <most bytes of text in one> <frames that did not fit>
// Usage: host_perf <reps> [--only <part of a sample's name>] [--corrupt] [--direto]
//   --direto: do not record; every strip runs the screen's draw function (the compose before Pintor::gravar( ): the A of an A/B)
//   --corrupt: flips one pixel of one strip after it is drawn. The control: every frame must then report exactly 1 differing pixel.
#include <Adafruit_GFX.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>

#include "Amostras.h"
#include "Faixa.h"

namespace {

using Relogio = std::chrono::steady_clock;
double micros( Relogio::time_point a, Relogio::time_point b ) { return std::chrono::duration<double, std::micro>( b - a ).count( ); }

class LeitorCanvas : public ua::Leitor {
public:
  explicit LeitorCanvas( GFXcanvas16 &c ) : c_( c ) {}
  uint16_t ler( int16_t x, int16_t y ) override { return c_.getPixel( x, y ); }

private:
  GFXcanvas16 &c_;
};

uint16_t g_tira[ua::LARGURA_PAINEL * ua::ALTURA_FAIXA];

// The strip's hash as DisplayManager_UiA.cpp computes it: FNV-1a over the buffer's bytes, one at a time.
uint32_t hashDaTira( ) {
  const uint8_t *b = reinterpret_cast<const uint8_t *>( g_tira );
  uint32_t h = 2166136261u;
  for ( size_t i = 0; i < sizeof g_tira; i++ ) { h ^= b[i]; h *= 16777619u; }
  return h;
}

struct Partes { double limpa = 0, desenha = 0, hash = 0; };

// One frame, composed as uaPintar( ) does. The sum of the hashes is returned so that none can be optimised away.
int g_maxOps = 0, g_maxTextos = 0, g_semGravacao = 0;

bool g_direto = false;   // --direto: no record, every strip runs the screen's draw (what uaPintar( ) did before Pintor::gravar( ))

uint32_t compor( const amostra::Amostra &a, ua::Faixa &faixa, ua::Pintor &p, const ua::Tema &tema, Partes *t ) {
  uint32_t soma = 0;
  if ( !g_direto ) p.gravar( );
  Relogio::time_point t0, t1, t2, t3;
  for ( int k = 0; k < ua::FAIXAS; k++ ) {
    if ( t ) t0 = Relogio::now( );
    faixa.para( k, p );
    faixa.fillScreen( tema.fundo );
    if ( t ) t1 = Relogio::now( );
    if ( p.gravado( ) ) p.repetir( ); else a.desenhar( p );
    p.parar( );
    if ( k == 0 ) {
      if ( p.gravadas( ) > g_maxOps ) g_maxOps = p.gravadas( );
      if ( p.bytesDeTextos( ) > g_maxTextos ) g_maxTextos = p.bytesDeTextos( );
      if ( !p.gravado( ) ) g_semGravacao++;
    }
    ua::margemPreta( p );
    if ( t ) t2 = Relogio::now( );
    soma += hashDaTira( );
    if ( t ) {
      t3 = Relogio::now( );
      t->limpa += micros( t0, t1 );
      t->desenha += micros( t1, t2 );
      t->hash += micros( t2, t3 );
    }
  }
  return soma;
}

// The pixels in which the six strips stitched differ from the sample drawn whole by Adafruit GFX's own path.
int diferentes( const amostra::Amostra &a, const ua::Tema &tema, ua::Faixa &faixa, bool corromper ) {
  GFXcanvas16 inteira( ua::LARGURA_PAINEL, ua::ALTURA_PAINEL );
  LeitorCanvas leitor( inteira );
  inteira.fillScreen( tema.fundo );
  ua::Pintor pi( inteira, tema, leitor );
  a.desenhar( pi );
  ua::margemPreta( pi );
  ua::Pintor ps( faixa, tema, faixa );
  int dif = 0;
  if ( !g_direto ) ps.gravar( );
  for ( int k = 0; k < ua::FAIXAS; k++ ) {
    faixa.para( k, ps );
    faixa.fillScreen( tema.fundo );
    if ( ps.gravado( ) ) ps.repetir( ); else a.desenhar( ps );
    ps.parar( );
    ua::margemPreta( ps );
    if ( corromper && k == 2 ) g_tira[5 * ua::LARGURA_PAINEL + 7] ^= 0x0841;
    for ( int y = 0; y < ua::ALTURA_FAIXA; y++ )
      for ( int x = 0; x < ua::LARGURA_PAINEL; x++ )
        if ( g_tira[y * ua::LARGURA_PAINEL + x] != inteira.getPixel( x, k * ua::ALTURA_FAIXA + y ) ) dif++;
  }
  return dif;
}

}  // namespace

int main( int argc, char **argv ) {
  const int reps = argc > 1 ? atoi( argv[1] ) : 20;
  const char *so = nullptr;
  bool corromper = false;
  for ( int i = 2; i < argc; i++ ) {
    if ( !strcmp( argv[i], "--only" ) && i + 1 < argc ) so = argv[++i];
    else if ( !strcmp( argv[i], "--corrupt" ) ) corromper = true;
    else if ( !strcmp( argv[i], "--direto" ) ) g_direto = true;
  }
  ua::Faixa faixa( g_tira );
  uint32_t sorvedouro = 0;
  int n = 0, difTotal = 0;
  double total = 0;
  Partes soma;
  for ( int i = 0; i < amostra::kAmostrasN; i++ ) {
    const amostra::Amostra &a = amostra::kAmostras[i];
    if ( so && !strstr( a.nome, so ) ) continue;
    for ( int t = 0; t < 2; t++ ) {
      const ua::Tema &tema = t ? ua::temaClaro( ) : ua::temaEscuro( );
      ua::Pintor p( faixa, tema, faixa );
      sorvedouro += compor( a, faixa, p, tema, nullptr );
      double melhor = 1e18;
      for ( int b = 0; b < 3; b++ ) {
        const auto t0 = Relogio::now( );
        for ( int r = 0; r < reps; r++ ) sorvedouro += compor( a, faixa, p, tema, nullptr );
        const double us = micros( t0, Relogio::now( ) ) / reps;
        if ( us < melhor ) melhor = us;
      }
      Partes ph;
      for ( int r = 0; r < reps; r++ ) sorvedouro += compor( a, faixa, p, tema, &ph );
      ph.limpa /= reps; ph.desenha /= reps; ph.hash /= reps;
      const int dif = diferentes( a, tema, faixa, corromper );
      printf( "S %s %s %.1f %.1f %.1f %.1f %d\n", a.nome, t ? "claro" : "escuro", melhor, ph.limpa, ph.desenha, ph.hash, dif );
      n++;
      total += melhor;
      soma.limpa += ph.limpa; soma.desenha += ph.desenha; soma.hash += ph.hash;
      difTotal += dif;
    }
  }
  printf( "TOTAL %d %.1f %.1f %.1f %.1f %d %d %d %d\n", n, total, soma.limpa, soma.desenha, soma.hash, difTotal, g_maxOps, g_maxTextos, g_semGravacao );
  fprintf( stderr, "# %u\n", sorvedouro );
  return 0;
}
