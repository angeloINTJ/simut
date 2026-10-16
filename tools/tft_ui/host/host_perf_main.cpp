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
// Usage: host_perf --hash   (the strip hash tests)
//        host_perf --vidro  (the full repaint without a clear, on a model of the glass)
//        host_perf <reps> [--only <part of a sample's name>] [--corrupt] [--direto]
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

// The strip's hash as DisplayManager_UiA.cpp computes it: the kit's, two pixels to the word.
uint32_t hashDaTira( ) { return ua::hashFaixa( g_tira, sizeof g_tira / sizeof g_tira[0] ); }

// --hash: the hash tells whether a strip changed, so it must not miss a change. On a strip of random pixels: every bit of every fifth pixel
// flipped alone; 100,000 pairs of flips anywhere; 20,000 pairs of flips of bit 15 of two odd pixels (bit 31 of their words: the weak spot of the
// plain FNV over words, which is the control and must collide on every one of them); and a start not on a word, which must hash as the same
// pixels do on one.
uint32_t hashFraco( const uint16_t *px, size_t n ) {
  uint32_t h = 2166136261u;
  for ( size_t i = 0; i + 1 < n; i += 2 ) h = ( h ^ ( (uint32_t)px[i] | (uint32_t)px[i + 1] << 16 ) ) * 16777619u;
  return h;
}

int provarHash( ) {
  constexpr size_t N = ua::LARGURA_PAINEL * ua::ALTURA_FAIXA;
  alignas( 4 ) static uint16_t px[N + 2], copia[N];
  uint32_t estado = 12345;
  auto sorteio = [&]( ) { estado = estado * 1664525u + 1013904223u; return estado >> 8; };
  for ( size_t i = 0; i < N + 2; i++ ) px[i] = (uint16_t)sorteio( );
  const uint32_t h0 = ua::hashFaixa( px, N ), f0 = hashFraco( px, N );
  int falhas = 0;
  auto confere = [&]( bool ok, const char *o_que ) { printf( "%s %s\n", ok ? "ok  " : "FAIL", o_que ); if ( !ok ) falhas++; };
  long unitarios = 0, iguais = 0;
  for ( size_t i = 0; i < N; i += 5 )
    for ( int b = 0; b < 16; b++ ) {
      px[i] ^= (uint16_t)( 1u << b );
      iguais += ua::hashFaixa( px, N ) == h0;
      px[i] ^= (uint16_t)( 1u << b );
      unitarios++;
    }
  char msg[160];
  snprintf( msg, sizeof msg, "one bit of one pixel flipped alone: %ld of %ld change the hash", unitarios - iguais, unitarios );
  confere( iguais == 0, msg );
  long pares = 0, colidem = 0;
  for ( int t = 0; t < 100000; t++ ) {
    const size_t i = sorteio( ) % N, j = sorteio( ) % N;
    const uint16_t bi = (uint16_t)( 1u << ( sorteio( ) % 16 ) ), bj = (uint16_t)( 1u << ( sorteio( ) % 16 ) );
    if ( i == j && bi == bj ) continue;
    px[i] ^= bi; px[j] ^= bj;
    colidem += ua::hashFaixa( px, N ) == h0;
    px[j] ^= bj; px[i] ^= bi;
    pares++;
  }
  snprintf( msg, sizeof msg, "two bits flipped anywhere: %ld of %ld pairs collide", colidem, pares );
  confere( colidem == 0, msg );
  long altos = 0, novos = 0, fracos = 0;
  for ( int t = 0; t < 20000; t++ ) {
    const size_t i = ( sorteio( ) % ( N / 2 ) ) * 2 + 1, j = ( sorteio( ) % ( N / 2 ) ) * 2 + 1;
    if ( i == j ) continue;
    px[i] ^= 0x8000; px[j] ^= 0x8000;
    novos += ua::hashFaixa( px, N ) == h0;
    fracos += hashFraco( px, N ) == f0;
    px[j] ^= 0x8000; px[i] ^= 0x8000;
    altos++;
  }
  snprintf( msg, sizeof msg, "bit 15 of two odd pixels: %ld of %ld collide (the plain FNV over words, the control: %ld)", novos, altos, fracos );
  confere( novos == 0 && fracos == altos, msg );
  memcpy( copia, px + 1, sizeof copia );
  confere( ua::hashFaixa( px + 1, N ) == ua::hashFaixa( copia, N ), "a start not on a word hashes as the same pixels on one" );
  return falhas;
}

struct Partes { double limpa = 0, desenha = 0, hash = 0; };

// One frame, composed as uaPintar( ) does. The sum of the hashes is returned so that none can be optimised away.
int g_maxOps = 0, g_maxTextos = 0, g_semGravacao = 0, g_maxLista = 0;
alignas( 8 ) uint8_t g_lista[ua::BYTES_DA_LISTA];   // the firmware's is the canvas's spare rows: the same size, so a frame that fits here fits there

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
      if ( p.bytesGravados( ) > g_maxTextos ) g_maxTextos = p.bytesGravados( );
      if ( p.usadosDaLista( ) > g_maxLista ) g_maxLista = p.usadosDaLista( );
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

// --vidro: the argument of DisplayManager::uaPintar( )'s full repaint, on a model of the glass. The repaint no longer clears the glass: it blackens the
// band the alignment offset uncovers (TftWithOffset::fillMarginsBlack( ), whose four conditions the model repeats) and pushes the six strips
// at the offset, clipped to the glass. For every offset from -4 to 4 in x and in y, from a glass full of stale pixels, every pixel must end black
// (the band) or a strip's (the rest), none stale. The controls leave the band out, or push five strips: both must leave stale pixels.
int provarVidro( ) {
  static uint16_t vidro[ua::ALTURA_PAINEL][ua::LARGURA_PAINEL];
  constexpr uint16_t VELHO = 0xDEAD, TIRA = 0xAAAA;
  int falhas = 0;
  for ( int variante = 0; variante < 3; variante++ ) {   // 0: the repaint, 1: without the band, 2: five strips
    long velhos = 0, errados = 0, casos = 0;
    for ( int oy = -4; oy <= 4; oy++ )
      for ( int ox = -4; ox <= 4; ox++ ) {
        for ( auto &linha : vidro )
          for ( auto &v : linha ) v = VELHO;
        auto enche = [&]( int x, int y, int w, int h ) {
          for ( int j = y; j < y + h; j++ )
            for ( int i = x; i < x + w; i++ ) vidro[j][i] = 0;
        };
        if ( variante != 1 ) {
          if ( ox > 0 ) enche( 0, 0, ox, 240 ); else if ( ox < 0 ) enche( 320 + ox, 0, -ox, 240 );
          if ( oy > 0 ) enche( 0, 0, 320, oy ); else if ( oy < 0 ) enche( 0, 240 + oy, 320, -oy );
        }
        for ( int k = 0; k < ( variante == 2 ? 5 : ua::FAIXAS ); k++ )
          for ( int j = 0; j < ua::ALTURA_FAIXA; j++ )
            for ( int i = 0; i < ua::LARGURA_PAINEL; i++ ) {
              const int x = i + ox, y = k * ua::ALTURA_FAIXA + j + oy;
              if ( x >= 0 && x < 320 && y >= 0 && y < 240 ) vidro[y][x] = TIRA;
            }
        for ( int y = 0; y < 240; y++ )
          for ( int x = 0; x < 320; x++ ) {
            const bool dentro = x - ox >= 0 && x - ox < 320 && y - oy >= 0 && y - oy < 240;
            const uint16_t esperado = dentro ? ( variante == 2 && y - oy >= 200 ? VELHO : TIRA ) : 0;
            if ( vidro[y][x] == VELHO ) velhos++;
            if ( vidro[y][x] != esperado ) errados++;
          }
        casos++;
      }
    const bool ok = variante == 0 ? ( velhos == 0 && errados == 0 ) : ( velhos > 0 );
    if ( !ok ) falhas++;
    printf( "%s %s: %ld stale pixels, %ld pixels not as expected, in %ld offsets\n", ok ? "ok  " : "FAIL",
            variante == 0 ? "the repaint (band blackened, six strips)" : variante == 1 ? "control: without the band" : "control: five strips", velhos, errados, casos );
  }
  return falhas;
}

}  // namespace

int main( int argc, char **argv ) {
  if ( argc > 1 && !strcmp( argv[1], "--hash" ) ) return provarHash( );
  if ( argc > 1 && !strcmp( argv[1], "--vidro" ) ) return provarVidro( );
  const int reps = argc > 1 ? atoi( argv[1] ) : 20;
  const char *so = nullptr;
  bool corromper = false;
  for ( int i = 2; i < argc; i++ ) {
    if ( !strcmp( argv[i], "--only" ) && i + 1 < argc ) so = argv[++i];
    else if ( !strcmp( argv[i], "--corrupt" ) ) corromper = true;
    else if ( !strcmp( argv[i], "--direto" ) ) g_direto = true;
  }
  ua::Pintor::lista( g_lista, sizeof g_lista );
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
  printf( "TOTAL %d %.1f %.1f %.1f %.1f %d %d %d %d %d\n", n, total, soma.limpa, soma.desenha, soma.hash, difTotal, g_maxOps, g_maxTextos, g_semGravacao,
          g_maxLista );
  fprintf( stderr, "# %u\n", sorvedouro );
  return 0;
}
