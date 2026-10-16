// Times the compose of every sample of one group of the Ângulo's screens the way DisplayManager::uaPintar( ) does it, and proves that it draws
// the same frame. Built without UA_HOST, as the firmware is, by tools/tft_ui/perf_telas.py. It is not a check of the screens (that is
// host_telas_main.cpp) but a ruler for the speed-ups of the compose: microseconds on an x86 host show the ratio between two builds, not the
// panel's time.
//
// For each sample and theme, one frame is composed as uaPintar( ) composes it: twelve 320 x 20 strips (ua::Faixa, the firmware's own class), each
// cleared to the theme's background, drawn by the screen's function clipped to the strip, given the black margin, and hashed. Printed, in
// microseconds per frame (the best of three batches of <reps>): the whole, then its three parts (the clear, the draw with the margin, the hash),
// then the number of pixels in which the twelve strips, stitched, differ from the same sample drawn whole in a GFXcanvas16 by Adafruit GFX's own
// path (0 when the strips are the frame):
//   S <sample> <theme> <us> <clear> <draw> <hash> <differing pixels>
//   TOTAL <samples> <us> <clear> <draw> <hash> <differing pixels> <most calls in a record> <most bytes of text and calls in one> <frames that did not fit>
//         <most bytes of the list one frame takes>      (the last four are -1 with --direto, which records nothing)
// Usage: host_perf --hash     (the strip hash tests)
//        host_perf --vidro    (the full repaint without a clear, on a model of the glass)
//        host_perf --recorte  (the part of a strip on the glass at an alignment offset, on a model of the glass)
//        host_perf --guarda   (the chart's plot guard with the strips on every shift of their grid; the group that links the chart only)
//        host_perf --caixas   (the rounded boxes against a per-pixel reference, with controls)
//        host_perf <reps> [--only <part of a sample's name>] [--corrupt] [--direto] [--lista <bytes>] [--desloca]
//   --direto: do not record; every strip runs the screen's draw function (the compose before Pintor::gravar( ): the A of an A/B)
//   --corrupt: flips one pixel of one strip after it is drawn. The control: every frame must then report exactly 1 differing pixel.
//   --lista <bytes>: the record is lent that many bytes of the canvas's spare rows, not all 3,200 (0: none). A frame that needs more falls back to drawing
//                    every strip; the frames that fell back are counted in TOTAL, and every frame must still equal the frame drawn whole.
//   --desloca: also stitches each frame from strips on the 19 other grids (their edges 1 to 19 rows from the usual ones), which must equal the frame too.
#include <Adafruit_GFX.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>

#include "Amostras.h"
#include "Faixa.h"
#include "Graficos.h"
#include "Recorte.h"

namespace ua {
namespace tela {
// Weak: only the group that links the chart has it (--guarda); the others see a null.
void desenharGrafico( Pintor &p, const VmGrafico &vm ) __attribute__( ( weak ) );
}  // namespace tela
}  // namespace ua

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

// The firmware's canvas: 320 x 45, the two strips in its first 40 rows (strip k in the half k % 2) and the record's list in the other 5 (DisplayManager.cpp:1272).
// The host lays its buffer out the same way, so that a strip that wrote into the list's rows would corrupt a record and show as a frame that differs.
alignas( 8 ) uint16_t g_canvas[ua::LARGURA_PAINEL * ( ua::LINHAS_DAS_FAIXAS + ua::LINHAS_DA_LISTA )];

// --lista <bytes>: how much of the spare rows the record is lent (the firmware lends all 3,200 B). Less than a frame needs, the record overflows and every strip
// draws the screen again: the fallback of Pintor::gravar( ), which no sample exercises with the full list (the largest of the 560 frames takes 1,935 B).
long g_bytesLista = ua::BYTES_DA_LISTA;
bool g_desloca = false;   // --desloca: also the strips on the 19 other grids

void emprestarLista( ) {
  if ( g_bytesLista <= 0 ) ua::Pintor::lista( nullptr, 0 );
  else ua::Pintor::lista( g_canvas + ua::LARGURA_PAINEL * ua::LINHAS_DAS_FAIXAS, (size_t)( g_bytesLista < (long)ua::BYTES_DA_LISTA ? g_bytesLista : (long)ua::BYTES_DA_LISTA ) );
}

// The strip's hash as DisplayManager_UiA.cpp computes it: the kit's, two pixels to the word.
uint32_t hashDaTira( const ua::Faixa &f ) { return ua::hashFaixa( f.buffer( ), (size_t)ua::LARGURA_PAINEL * ua::ALTURA_FAIXA ); }

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
    ua::margemPreta( p );
    if ( t ) t2 = Relogio::now( );
    soma += hashDaTira( faixa );
    if ( t ) {
      t3 = Relogio::now( );
      t->limpa += micros( t0, t1 );
      t->desenha += micros( t1, t2 );
      t->hash += micros( t2, t3 );
    }
  }
  return soma;
}

// The sample drawn whole by Adafruit GFX's own path: what the strips must make up.
void desenharInteira( const amostra::Amostra &a, const ua::Tema &tema, GFXcanvas16 &inteira ) {
  LeitorCanvas leitor( inteira );
  inteira.fillScreen( tema.fundo );
  ua::Pintor pi( inteira, tema, leitor );
  a.desenhar( pi );
  ua::margemPreta( pi );
}

// The pixels in which the strips, stitched, differ from that frame. The strips tile the panel on the grid of 20 rows the firmware uses (desloc 0) or on that grid
// shifted down by 1 to 19 rows, with a strip above the first and one below the last to cover the panel; the rows a strip has beyond the panel are not compared.
int diferentes( const amostra::Amostra &a, const ua::Tema &tema, ua::Faixa &faixa, GFXcanvas16 &inteira, bool corromper, int desloc = 0 ) {
  ua::Pintor ps( faixa, tema, faixa );
  int dif = 0;
  if ( !g_direto ) ps.gravar( );
  for ( int k = desloc ? -1 : 0; k < ua::FAIXAS; k++ ) {
    const int y0 = k * ua::ALTURA_FAIXA + desloc;
    faixa.paraEm( y0, k, ps );
    faixa.fillScreen( tema.fundo );
    if ( ps.gravado( ) ) ps.repetir( ); else a.desenhar( ps );
    ps.parar( );
    if ( desloc == 0 && k == 0 && !g_direto ) {   // once a frame (compor( ) runs several times a frame): the record of the first strip
      if ( ps.gravadas( ) > g_maxOps ) g_maxOps = ps.gravadas( );
      if ( ps.bytesGravados( ) > g_maxTextos ) g_maxTextos = ps.bytesGravados( );
      if ( ps.usadosDaLista( ) > g_maxLista ) g_maxLista = ps.usadosDaLista( );
      if ( !ps.gravado( ) ) g_semGravacao++;
    }
    ua::margemPreta( ps );
    if ( corromper && k == 2 ) faixa.buffer( )[5 * ua::LARGURA_PAINEL + 7] ^= 0x0841;
    for ( int y = 0; y < ua::ALTURA_FAIXA; y++ ) {
      if ( y0 + y < 0 || y0 + y >= ua::ALTURA_PAINEL ) continue;
      for ( int x = 0; x < ua::LARGURA_PAINEL; x++ )
        if ( faixa.buffer( )[y * ua::LARGURA_PAINEL + x] != inteira.getPixel( x, y0 + y ) ) dif++;
    }
  }
  return dif;
}

// --vidro: the argument of DisplayManager::uaPintar( )'s full repaint, on a model of the glass. The repaint no longer clears the glass: it blackens the
// band the alignment offset uncovers (TftWithOffset::fillMarginsBlack( ), whose four conditions the model repeats) and pushes the strips
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
        for ( int k = 0; k < ( variante == 2 ? ua::FAIXAS - 1 : ua::FAIXAS ); k++ )
          for ( int j = 0; j < ua::ALTURA_FAIXA; j++ )
            for ( int i = 0; i < ua::LARGURA_PAINEL; i++ ) {
              const int x = i + ox, y = k * ua::ALTURA_FAIXA + j + oy;
              if ( x >= 0 && x < 320 && y >= 0 && y < 240 ) vidro[y][x] = TIRA;
            }
        for ( int y = 0; y < 240; y++ )
          for ( int x = 0; x < 320; x++ ) {
            const bool dentro = x - ox >= 0 && x - ox < 320 && y - oy >= 0 && y - oy < 240;
            const uint16_t esperado = dentro ? ( variante == 2 && y - oy >= ( ua::FAIXAS - 1 ) * ua::ALTURA_FAIXA ? VELHO : TIRA ) : 0;
            if ( vidro[y][x] == VELHO ) velhos++;
            if ( vidro[y][x] != esperado ) errados++;
          }
        casos++;
      }
    const bool ok = variante == 0 ? ( velhos == 0 && errados == 0 ) : ( velhos > 0 );
    if ( !ok ) falhas++;
    printf( "%s %s: %ld stale pixels, %ld pixels not as expected, in %ld offsets\n", ok ? "ok  " : "FAIL",
            variante == 0 ? "the repaint (band blackened, every strip)" : variante == 1 ? "control: without the band" : "control: the last strip missing", velhos, errados, casos );
  }
  return falhas;
}

// --recorte: the glass after the twelve strips are pushed at an offset the way DisplayManager::uaEmpurrar( ) pushes them (ua::recortar( ) and
// ua::compactar( ), the code the firmware runs), against what each glass pixel must hold: the pixel of the image at (x - ox, y - oy). Every pixel of a strip
// carries a value of its own (a hash of its place), so a block taken from the wrong column or row shows. The control reads the part in place, without
// compactar( ): with a column offset it must get pixels wrong.
int provarRecorte( ) {
  static uint16_t vidro[ua::ALTURA_PAINEL][ua::LARGURA_PAINEL];
  static uint16_t tira[ua::LARGURA_PAINEL * ua::ALTURA_FAIXA];
  constexpr uint16_t VELHO = 0xDEAD;
  auto valor = []( int linha, int col ) { return (uint16_t)( ( (uint32_t)( linha * ua::LARGURA_PAINEL + col ) * 2654435761u ) >> 16 ); };
  int falhas = 0;
  for ( int variante = 0; variante < 2; variante++ ) {   // 0: the firmware's push, 1: the control
    long errados = 0, casos = 0;
    for ( int oy = -4; oy <= 4; oy++ )
      for ( int ox = -4; ox <= 4; ox++ ) {
        for ( auto &linha : vidro )
          for ( auto &v : linha ) v = VELHO;
        for ( int k = 0; k < ua::FAIXAS; k++ ) {
          for ( int j = 0; j < ua::ALTURA_FAIXA; j++ )
            for ( int i = 0; i < ua::LARGURA_PAINEL; i++ ) tira[j * ua::LARGURA_PAINEL + i] = valor( k * ua::ALTURA_FAIXA + j, i );
          const ua::Recorte r = ua::recortar( ox, k * ua::ALTURA_FAIXA + oy, ua::LARGURA_PAINEL, ua::ALTURA_FAIXA, ua::LARGURA_PAINEL, ua::ALTURA_PAINEL );
          if ( r.vazio ) continue;
          const uint16_t *bloco = variante == 0 ? ua::compactar( tira, ua::LARGURA_PAINEL, r ) : tira + r.fy * ua::LARGURA_PAINEL + r.fx;
          for ( int j = 0; j < r.h; j++ )
            for ( int i = 0; i < r.w; i++ ) vidro[r.y + j][r.x + i] = bloco[j * r.w + i];
        }
        for ( int y = 0; y < ua::ALTURA_PAINEL; y++ )
          for ( int x = 0; x < ua::LARGURA_PAINEL; x++ ) {
            const bool dentro = x - ox >= 0 && x - ox < ua::LARGURA_PAINEL && y - oy >= 0 && y - oy < ua::ALTURA_PAINEL;
            if ( vidro[y][x] != ( dentro ? valor( y - oy, x - ox ) : VELHO ) ) errados++;
          }
        casos++;
      }
    const bool ok = variante == 0 ? errados == 0 : errados > 0;
    if ( !ok ) falhas++;
    printf( "%s %s: %ld glass pixels wrong, in %ld offsets\n", ok ? "ok  " : "FAIL", variante == 0 ? "the push (recortar, compactar)" : "control: the part read in place", errados, casos );
  }
  const bool vazios = ua::recortar( 0, 240, 320, 20, 320, 240 ).vazio && ua::recortar( -320, 0, 320, 20, 320, 240 ).vazio && ua::recortar( 0, -20, 320, 20, 320, 240 ).vazio &&
                      !ua::recortar( 0, 239, 320, 20, 320, 240 ).vazio;
  printf( "%s a strip wholly off the glass is empty, one pixel on it is not\n", vazios ? "ok  " : "FAIL" );
  return falhas + ( vazios ? 0 : 1 );
}

// --guarda: the guard of the chart's plot, which skips the plot's three loops in a strip that holds none of the plot's rows (GraficosGrafico.cpp, foraDoPlot( )).
// A guard narrower than what the loops draw loses pixels, but only in a strip whose edge falls inside the margin, and the samples never put one there: a guard
// narrowed by 6 px a side passed every sample. So this draws a chart whose second curve is on the grid of its scale at both ends (40 and 60 %UR, multiples of
// PASSO_UMIDADE): isolated points, which are squares reaching one row beyond the plot, and a line, on the plot's first and last rows. It is stitched from strips on
// each of the 20 shifts of their grid, in both themes, and every one must equal the frame drawn whole. Only the group that links the chart has it.
ua::tela::VmGrafico g_vmGuarda;   // static: zero

void desenharGuarda( ua::Pintor &p ) { ua::tela::desenharGrafico( p, g_vmGuarda ); }

// A deterministic pseudo-random chart (an LCG, so that a failure repeats): the temperature as a walk with gaps and bands of random width, and a second curve of the
// humidity or the pressure whose values often sit on the grid of its scale. The plot's loops spare the buckets a strip cannot show by their values; this is the
// property test of that, against the frame drawn whole, on every grid of the strips.
uint32_t g_semente = 20261016u;
int aleatorio( int m ) {
  g_semente = g_semente * 1664525u + 1013904223u;
  return (int)( ( g_semente >> 8 ) % (uint32_t)m );
}
int limitar( int v, int a, int b ) { return v < a ? a : v > b ? b : v; }

void sortearGrafico( ua::tela::VmGrafico &vm ) {
  using namespace ua::tela;
  memset( &vm, 0, sizeof vm );
  snprintf( vm.sensor, sizeof vm.sensor, "Sorteio" );
  vm.periodo = (uint8_t)aleatorio( 5 );
  vm.passado = aleatorio( 2 ) != 0;
  vm.n = (uint8_t)( 2 + aleatorio( GRAFICO_BALDES - 1 ) );
  const int tipo = aleatorio( 3 );
  vm.serie2 = tipo == 0 ? Serie2::NENHUMA : tipo == 1 ? Serie2::UMIDADE : Serie2::PRESSAO;
  for ( int i = 0; i < GRAFICO_BALDES; i++ ) vm.bMedia[i] = vm.bMin[i] = vm.bMax[i] = vm.bMedia2[i] = SEM_DADO;
  // A flat series has a scale of 10 tenths, where a value 4,000 tenths away makes rdiv( ) overflow its int16 and the row wrap; an inverted bucket has its maximum
  // under its mean or its minimum over it, which faixa( ) ignores, so its value can be far outside the scale: Limiares::intervalo( ) must not spare it.
  const bool plano = aleatorio( 3 ) == 0, invertidos = aleatorio( 2 ) == 0;
  const int passo = plano ? 1 : 1 + aleatorio( 40 ), folga = plano ? aleatorio( 4 ) : aleatorio( 30 ), lacunas = aleatorio( 4 ), grade = aleatorio( 2 );
  int v = -300 + aleatorio( 700 ), v2 = tipo == 1 ? 200 + aleatorio( 600 ) : 9000 + aleatorio( 1200 );
  int mn = 32767, mx = -32768, soma = 0, nv = 0;
  for ( int i = 0; i < vm.n; i++ ) {
    v = limitar( v + aleatorio( 2 * passo + 1 ) - passo, -500, 800 );
    v2 = tipo == 1 ? limitar( v2 + aleatorio( 2 * passo + 1 ) - passo, 0, 1000 ) : limitar( v2 + aleatorio( 2 * passo + 1 ) - passo, 8000, 11000 );
    if ( lacunas && aleatorio( 5 * lacunas ) == 0 ) continue;
    vm.bMedia[i] = (int16_t)v;
    if ( aleatorio( 4 ) ) vm.bMin[i] = (int16_t)( v - aleatorio( folga + 1 ) );
    if ( aleatorio( 4 ) ) vm.bMax[i] = (int16_t)( v + aleatorio( folga + 1 ) );
    if ( invertidos && aleatorio( 4 ) == 0 ) {
      const int que = aleatorio( 3 );   // 0: the maximum under the mean, 1: the minimum over it, 2: both
      if ( que != 1 ) vm.bMax[i] = (int16_t)limitar( v - 1 - aleatorio( 30000 ), -32767, 32767 );
      if ( que != 0 ) vm.bMin[i] = (int16_t)limitar( v + 1 + aleatorio( 30000 ), -32767, 32767 );
    }
    if ( tipo && aleatorio( 4 ) ) vm.bMedia2[i] = (int16_t)( grade ? v2 / 50 * 50 : v2 );
    if ( v < mn ) mn = v;
    if ( v > mx ) mx = v;
    soma += v;
    nv++;
  }
  vm.temLimites = aleatorio( 2 ) != 0;
  vm.limiteInf = (int16_t)( -100 + aleatorio( 200 ) );
  vm.limiteSup = (int16_t)( 200 + aleatorio( 200 ) );
  if ( nv ) {
    vm.maximo = (int16_t)mx;
    vm.minimo = (int16_t)mn;
    vm.media = (int16_t)( soma / nv );
    vm.agora = (int16_t)v;
    vm.desvio = (int16_t)aleatorio( 50 );
    vm.variacao = (int16_t)( mx - mn );
  }
}

// --caixas: the rounded boxes against a straightforward per-pixel reading of what they must draw. fillRoundRect( ) is a body of three rectangles and four corners of
// r x r cells, each cell mixing the colour into the pixel in proportion to a coverage of 16 samples; roundRect( ) is four 1 px lines and the same corners as a ring.
// P11b changed how the corners are painted (a run of covered cells as one span, the partial ones blended in place on the strip's raw buffer, the body in other
// pieces); the reference below is written from that definition, cell by cell, with its own copy of the coverage and of the mix, so that a change of either shows.
// Random boxes (radii 0 to 15, so that the tables' 12 and the computed path both run; off the panel and across the strips) over a noisy background are drawn on the
// strips of a grid shifted by 0 to 19 rows (the raw path) and on a whole canvas (the generic path), and every pixel must equal the reference's. The controls are
// references wrong on purpose (the mix rounded one off; a coverage that counts one more shell of samples): each must differ somewhere.
uint16_t g_fundo[ua::ALTURA_PAINEL][ua::LARGURA_PAINEL], g_ref[ua::ALTURA_PAINEL][ua::LARGURA_PAINEL];

int refCobertura( int dx8, int dy8, int out2, int in2, bool aro ) {
  int n = 0;
  for ( int l = 0; l < 4; l++ )
    for ( int k = 0; k < 4; k++ ) {
      const int dy = dy8 + 2 * l + 1, dx = dx8 + 2 * k + 1, d2 = dx * dx + dy * dy;
      if ( d2 <= ( aro ? out2 + 16 : out2 ) && d2 > in2 ) n++;
    }
  return n;
}

uint16_t refMisturar( uint16_t fg, uint16_t bg, int a, int arred ) {
  if ( a >= 16 ) return fg;
  auto ex5 = []( int v ) { return ( v << 3 ) | ( v >> 2 ); };
  auto ex6 = []( int v ) { return ( v << 2 ) | ( v >> 4 ); };
  const int r = ( ex5( fg >> 11 ) * a + ex5( bg >> 11 ) * ( 16 - a ) + arred ) / 16;
  const int g = ( ex6( ( fg >> 5 ) & 63 ) * a + ex6( ( bg >> 5 ) & 63 ) * ( 16 - a ) + arred ) / 16;
  const int b = ( ex5( fg & 31 ) * a + ex5( bg & 31 ) * ( 16 - a ) + arred ) / 16;
  return (uint16_t)( ( ( r & 0xF8 ) << 8 ) | ( ( g & 0xFC ) << 3 ) | ( b >> 3 ) );
}

void refRetangulo( int x, int y, int w, int h, uint16_t c ) {
  for ( int j = y < 0 ? 0 : y; j < y + h && j < ua::ALTURA_PAINEL; j++ )
    for ( int i = x < 0 ? 0 : x; i < x + w && i < ua::LARGURA_PAINEL; i++ ) g_ref[j][i] = c;
}

void refPixel( int x, int y, uint16_t c, int a, int arred ) {
  if ( a == 0 || x < 0 || y < 0 || x >= ua::LARGURA_PAINEL || y >= ua::ALTURA_PAINEL ) return;
  g_ref[y][x] = refMisturar( c, g_ref[y][x], a, arred );
}

void refCaixa( int x, int y, int w, int h, int r, uint16_t c, bool contorno, int arred, bool aro ) {
  if ( w <= 0 || h <= 0 ) return;
  const int m = ( w < h ? w : h ) / 2;
  if ( r > m ) r = m;
  int in2 = -1;
  if ( !contorno ) {
    refRetangulo( x + r, y, w - 2 * r, h, c );
    refRetangulo( x, y + r, r, h - 2 * r, c );
    refRetangulo( x + w - r, y + r, r, h - 2 * r, c );
  } else {
    refRetangulo( x + r, y, w - 2 * r, 1, c );
    refRetangulo( x + r, y + h - 1, w - 2 * r, 1, c );
    refRetangulo( x, y + r, 1, h - 2 * r, c );
    refRetangulo( x + w - 1, y + r, 1, h - 2 * r, c );
    const int in = 8 * ( r - 1 );
    in2 = in > 0 ? in * in : -1;
  }
  const int out = 8 * r;
  for ( int j = 0; j < r; j++ )
    for ( int i = 0; i < r; i++ ) {
      const int a = refCobertura( 8 * ( i - r ), 8 * ( j - r ), out * out, in2, aro );
      refPixel( x + i, y + j, c, a, arred );
      refPixel( x + w - 1 - i, y + j, c, a, arred );
      refPixel( x + i, y + h - 1 - j, c, a, arred );
      refPixel( x + w - 1 - i, y + h - 1 - j, c, a, arred );
    }
}

// The pixels in which one random box, drawn by the Pintor on the strips and on a whole canvas, differs from the reference (the controls change arred and aro).
long caixaDiferente( int arred, bool aro ) {
  const bool grande = aleatorio( 8 ) == 0;
  const int x = -20 + aleatorio( 350 ), y = -20 + aleatorio( 270 ), w = 1 + aleatorio( grande ? 320 : 100 ), h = 1 + aleatorio( grande ? 240 : 60 ), r = aleatorio( 16 );
  const uint16_t c = (uint16_t)aleatorio( 65536 );
  const bool contorno = aleatorio( 2 ) != 0;
  const int desloc = aleatorio( ua::ALTURA_FAIXA );
  memcpy( g_ref, g_fundo, sizeof g_ref );
  refCaixa( x, y, w, h, r, c, contorno, arred, aro );
  long dif = 0;
  const ua::Tema &tema = ua::temaEscuro( );
  ua::Faixa faixa( g_canvas );
  ua::Pintor p( faixa, tema, faixa );
  for ( int k = desloc ? -1 : 0; k < ua::FAIXAS; k++ ) {
    const int y0 = k * ua::ALTURA_FAIXA + desloc;
    faixa.paraEm( y0, k, p );
    uint16_t *buf = faixa.buffer( );
    for ( int j = 0; j < ua::ALTURA_FAIXA; j++ ) {
      if ( y0 + j >= 0 && y0 + j < ua::ALTURA_PAINEL ) memcpy( buf + j * ua::LARGURA_PAINEL, g_fundo[y0 + j], sizeof g_fundo[0] );
      else memset( buf + j * ua::LARGURA_PAINEL, 0, sizeof g_fundo[0] );
    }
    if ( contorno ) p.roundRect( x, y, w, h, r, c ); else p.fillRoundRect( x, y, w, h, r, c );
    for ( int j = 0; j < ua::ALTURA_FAIXA; j++ )
      if ( y0 + j >= 0 && y0 + j < ua::ALTURA_PAINEL )
        for ( int i = 0; i < ua::LARGURA_PAINEL; i++ ) dif += buf[j * ua::LARGURA_PAINEL + i] != g_ref[y0 + j][i];
  }
  GFXcanvas16 inteira( ua::LARGURA_PAINEL, ua::ALTURA_PAINEL );
  memcpy( inteira.getBuffer( ), g_fundo, sizeof g_fundo );
  LeitorCanvas leitor( inteira );
  ua::Pintor pi( inteira, tema, leitor );
  if ( contorno ) pi.roundRect( x, y, w, h, r, c ); else pi.fillRoundRect( x, y, w, h, r, c );
  dif += memcmp( inteira.getBuffer( ), g_ref, sizeof g_ref ) != 0 ? [&] {
    long n = 0;
    for ( int j = 0; j < ua::ALTURA_PAINEL; j++ )
      for ( int i = 0; i < ua::LARGURA_PAINEL; i++ ) n += inteira.getPixel( i, j ) != g_ref[j][i];
    return n;
  }() : 0;
  return dif;
}

int provarCaixas( ) {
  for ( auto &linha : g_fundo )
    for ( auto &v : linha ) v = (uint16_t)aleatorio( 65536 );
  const int casos = 4000, controle = 400;
  long dif = 0, arredondado = 0, aro = 0;
  for ( int s = 0; s < casos; s++ ) dif += caixaDiferente( 8, false );
  for ( int s = 0; s < controle; s++ ) arredondado += caixaDiferente( 7, false );
  for ( int s = 0; s < controle; s++ ) aro += caixaDiferente( 8, true );
  const bool ok = dif == 0 && arredondado > 0 && aro > 0;
  printf( "%s --caixas: %d random boxes (fills and outlines, radii 0 to 15, off the panel and across the strips) on strips of shifted grids and on a whole canvas, against a per-pixel reference: %ld pixels differ; controls (the mix rounded one off, a coverage with one more shell): %ld and %ld pixels differ in %d boxes each\n",
          ok ? "ok  " : "FAIL", casos, dif, arredondado, aro, controle );
  return ok ? 0 : 1;
}

int provarGuarda( ) {
  if ( !ua::tela::desenharGrafico ) {
    printf( "skip: this group links no chart\n" );
    return 0;
  }
  using namespace ua::tela;
  VmGrafico &vm = g_vmGuarda;
  snprintf( vm.sensor, sizeof vm.sensor, "Guarda" );
  vm.periodo = 3;
  vm.serie2 = Serie2::UMIDADE;
  vm.n = 40;
  for ( int i = 0; i < GRAFICO_BALDES; i++ ) vm.bMedia[i] = vm.bMin[i] = vm.bMax[i] = vm.bMedia2[i] = SEM_DADO;
  for ( int i = 0; i < vm.n; i++ ) {
    vm.bMedia[i] = (int16_t)( 200 + 3 * i );
    vm.bMin[i] = (int16_t)( vm.bMedia[i] - 4 );
    vm.bMax[i] = (int16_t)( vm.bMedia[i] + 4 );
  }
  const int topo[] = { 3, 12, 31 }, base[] = { 7, 20, 35 };   // isolated: the neighbours are empty, so each is a square
  for ( int i : topo ) vm.bMedia2[i] = 600;
  for ( int i : base ) vm.bMedia2[i] = 400;
  vm.bMedia2[24] = 600;   // and a line from one end of the scale to the other
  vm.bMedia2[25] = 400;
  vm.maximo = vm.agora = 317;
  vm.minimo = 200;
  vm.media = 258;
  vm.desvio = 20;
  vm.variacao = 117;
  emprestarLista( );
  ua::Faixa faixa( g_canvas );
  const amostra::Amostra a = { "guarda", desenharGuarda, nullptr };
  long dif = 0;
  int grades = 0;
  for ( int t = 0; t < 2; t++ ) {
    const ua::Tema &tema = t ? ua::temaClaro( ) : ua::temaEscuro( );
    GFXcanvas16 inteira( ua::LARGURA_PAINEL, ua::ALTURA_PAINEL );
    desenharInteira( a, tema, inteira );
    for ( int d = 0; d < ua::ALTURA_FAIXA; d++, grades++ ) dif += diferentes( a, tema, faixa, inteira, false, d );
  }
  printf( "%s --guarda: the chart with its second curve on the plot's first and last rows, strips on %d grids: %ld pixels differ\n", dif == 0 ? "ok  " : "FAIL", grades, dif );
  // a flat chart (a scale of 10 tenths) with inverted buckets, values outside the scale whose row wraps: the stretch that spans one is drawn from the top of the plot,
  // which a guard that spared it by its other value would lose
  for ( int i = 0; i < GRAFICO_BALDES; i++ ) vm.bMedia[i] = vm.bMin[i] = vm.bMax[i] = vm.bMedia2[i] = SEM_DADO;
  vm.serie2 = Serie2::NENHUMA;
  vm.n = 40;
  for ( int i = 0; i < vm.n; i++ ) {
    vm.bMedia[i] = 200;
    vm.bMin[i] = 198;
    vm.bMax[i] = 202;
  }
  vm.bMax[10] = -5000;   // under its mean, no minimum
  vm.bMin[10] = SEM_DADO;
  vm.bMin[20] = 5000;    // over its mean, no maximum
  vm.bMax[20] = SEM_DADO;
  vm.bMax[30] = -5000;   // both
  vm.bMin[30] = 5000;
  long difInv = 0;
  for ( int t = 0; t < 2; t++ ) {
    const ua::Tema &tema = t ? ua::temaClaro( ) : ua::temaEscuro( );
    GFXcanvas16 inteira( ua::LARGURA_PAINEL, ua::ALTURA_PAINEL );
    desenharInteira( a, tema, inteira );
    for ( int d = 0; d < ua::ALTURA_FAIXA; d++ ) difInv += diferentes( a, tema, faixa, inteira, false, d );
  }
  printf( "%s --guarda: a flat chart with inverted buckets (values outside the scale), strips on 20 grids, both themes: %ld pixels differ\n", difInv == 0 ? "ok  " : "FAIL", difInv );
  long difSorteio = 0;
  const int sorteios = 300;
  for ( int s = 0; s < sorteios; s++ ) {
    sortearGrafico( g_vmGuarda );
    const ua::Tema &tema = s & 1 ? ua::temaClaro( ) : ua::temaEscuro( );
    GFXcanvas16 inteira( ua::LARGURA_PAINEL, ua::ALTURA_PAINEL );
    desenharInteira( a, tema, inteira );
    for ( int d = 0; d < ua::ALTURA_FAIXA; d++ ) difSorteio += diferentes( a, tema, faixa, inteira, false, d );
  }
  printf( "%s --guarda: %d random charts (gaps, isolated points, second curves on their scale's edges), strips on 20 grids: %ld pixels differ\n", difSorteio == 0 ? "ok  " : "FAIL", sorteios, difSorteio );
  return dif == 0 && difInv == 0 && difSorteio == 0 ? 0 : 1;
}

}  // namespace

int main( int argc, char **argv ) {
  if ( argc > 1 && !strcmp( argv[1], "--guarda" ) ) return provarGuarda( );
  if ( argc > 1 && !strcmp( argv[1], "--caixas" ) ) return provarCaixas( );
  if ( argc > 1 && !strcmp( argv[1], "--recorte" ) ) return provarRecorte( );
  if ( argc > 1 && !strcmp( argv[1], "--hash" ) ) return provarHash( );
  if ( argc > 1 && !strcmp( argv[1], "--vidro" ) ) return provarVidro( );
  const int reps = argc > 1 ? atoi( argv[1] ) : 20;
  const char *so = nullptr;
  bool corromper = false;
  for ( int i = 2; i < argc; i++ ) {
    if ( !strcmp( argv[i], "--only" ) && i + 1 < argc ) so = argv[++i];
    else if ( !strcmp( argv[i], "--corrupt" ) ) corromper = true;
    else if ( !strcmp( argv[i], "--direto" ) ) g_direto = true;
    else if ( !strcmp( argv[i], "--desloca" ) ) g_desloca = true;
    else if ( !strcmp( argv[i], "--lista" ) && i + 1 < argc ) g_bytesLista = atol( argv[++i] );
  }
  emprestarLista( );
  ua::Faixa faixa( g_canvas );
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
      GFXcanvas16 inteira( ua::LARGURA_PAINEL, ua::ALTURA_PAINEL );
      desenharInteira( a, tema, inteira );
      int dif = diferentes( a, tema, faixa, inteira, corromper );
      if ( g_desloca )
        for ( int d = 1; d < ua::ALTURA_FAIXA; d++ ) dif += diferentes( a, tema, faixa, inteira, corromper, d );
      printf( "S %s %s %.1f %.1f %.1f %.1f %d\n", a.nome, t ? "claro" : "escuro", melhor, ph.limpa, ph.desenha, ph.hash, dif );
      n++;
      total += melhor;
      soma.limpa += ph.limpa; soma.desenha += ph.desenha; soma.hash += ph.hash;
      difTotal += dif;
    }
  }
  printf( "TOTAL %d %.1f %.1f %.1f %.1f %d %d %d %d %d\n", n, total, soma.limpa, soma.desenha, soma.hash, difTotal, g_direto ? -1 : g_maxOps,
          g_direto ? -1 : g_maxTextos, g_direto ? -1 : g_semGravacao, g_direto ? -1 : g_maxLista );
  fprintf( stderr, "# %u\n", sorvedouro );
  return 0;
}
