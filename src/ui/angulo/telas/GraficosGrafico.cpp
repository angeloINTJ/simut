// The history chart (MODE_GRAPH_VIEW). See Graficos.h.
//
// Family A's `historico_24h` of the plan, drawn from the Core 0's buckets: the
// title bar with Dia and Voltar, two lines of statistics, the chart card, and
// in the footer the five periods between the two arrows that walk the window.
#include <stdio.h>
#include <string.h>

#include "Graficos.h"
#include "GraficosComum.h"

namespace ua {
namespace tela {

using namespace graficos;

namespace {

/* ------------------------------------------------------------------ */
/* Geometry                                                             */
/* ------------------------------------------------------------------ */

// The plan's chart card; the plot sits inside it, with the time axis under it.
// Since the panel's text floor is 9 pt (2026-10-15) the card is 4 px lower and 4
// px taller than the plan's (68, 112): the statistics' two lines are 18 px apart
// (14 px caps and 4 px of air), and the plot keeps the 88 px it had. Above it
// PAD_CIMA = 10: the right scale's top label is 14 px tall, centred on the
// plot's top, so it clears the card's border by 2 px. Under it 18: the axis
// labels' 14 px of ink start 1 px under the plot's foot (no line is drawn
// there, and the range leaves the data an eighth of itself above it, 8 px at
// least) and end 2 px above the border (the 7 pt labels, 11 px, had 16).
constexpr int16_t X_CARTAO = ESP2, Y_CARTAO = 72, W_CARTAO = 304, H_CARTAO = 116;
constexpr int16_t PAD_CIMA = 10, ALTO_PLOT = 88;

// The statistics, two lines under the title bar, 18 px between baselines. A
// line may run to ESP2 short of the glass's edge.
constexpr int16_t Y_STAT1 = 32, Y_STAT2 = Y_STAT1 + 18;
constexpr int16_t X_STAT_DIR = 320 - ESP2;

// The footer: an arrow, the five period chips, an arrow, 304 px in all. The
// groups are ESP2 apart and the chips ESP1.
constexpr int16_t Y_PE = 196, H_PE = 40, W_SETA = 36, W_CHIP = 40;
static_assert( 2 * W_SETA + N_PERIODOS * W_CHIP + ( N_PERIODOS - 1 ) * ESP1 + 4 * ESP2 == 320,
               "two arrows and five chips fill the footer between its ESP2 margins" );

// The scales, in tenths. At most MAX_GRADE horizontal grid lines: with 88 px of
// plot that keeps their labels more than 17 px apart, the 17 px of ink a 9 pt
// label with a comma has (measured 2026-10-15: 14 of caps, 3 of the comma); the
// 5 lines of the 7 pt labels would be 14,7 px apart and their ink boxes would
// overlap. The range is never
// narrower than FAIXA_MIN (1,0), so a steady sensor reads as steady and not as
// a magnified wobble, and has an eighth of itself (0,3 at least) as air above
// and below, so the line does not touch the card. The second curve's scale
// rounds to PASSO_UMIDADE (5 %UR) or PASSO_PRESSAO (1 hPa).
constexpr int MAX_GRADE = 4;
constexpr int32_t FAIXA_MIN = 10, AR_DIV = 8, AR_MIN = 3;
constexpr int32_t PASSO_UMIDADE = 50, PASSO_PRESSAO = 10;

// The last reading is a dot of R_PONTO in a ring of R_ANEL; a bucket alone
// between two gaps is a square of LADO_PONTO.
constexpr int16_t R_PONTO = 3, R_ANEL = R_PONTO + 1, LADO_PONTO = 3;

/* ------------------------------------------------------------------ */
/* Layout                                                               */
/* ------------------------------------------------------------------ */

// One statistic: its name in support type and its value in bold, side by side;
// an extreme also carries when it happened, in support type again.
struct Dado {
  int16_t x, y;
  char k[8];
  char v[16];
  char t[8];                     // "12:10" or "13/10"; empty for the others
};

struct LayoutGrafico {
  char titulo[64];
  Acao acoes[2];                 // Dia, Voltar
  Caixa16 cxAcao[2];
  int n;                         // buckets to draw
  bool temDados;
  Dado stat[6];
  int nStat;
  Caixa16 cartao;
  int16_t px0, px1, py0, py1;    // the plot
  int32_t lo, hi;                // what the plot's height spans, in tenths
  int nGrade;
  int32_t grade[MAX_GRADE + 1];
  char rotGrade[MAX_GRADE + 1][16];
  bool temV2;
  int32_t lo2, hi2;
  char rot2Cima[16], rot2Baixo[16];
  const char *un2;
  char eixo[3][16];
  Caixa16 periodo[N_PERIODOS], anterior, seguinte;
};

int32_t chao( int32_t a, int32_t b ) { return a >= 0 ? a / b : -( ( -a + b - 1 ) / b ); }   // floor( a / b ), b > 0

// "5,8 °C" with its unit, or "--,-" alone for what is not known, as the
// dashboard writes a sensor in error.
void comUnidade( int16_t v, const char *un, char *buf, int cap ) {
  char t[16];
  if ( v == SEM_DADO ) snprintf( buf, (size_t)cap, "--,-" );
  else snprintf( buf, (size_t)cap, "%s%s", n1( v, t ), un );
}

// A grid value in tenths: "-20,0", or "-20" when the step is whole.
char *rotuloGrade( int32_t v, bool inteiro, char *buf ) {
  if ( !inteiro ) return n1( v, buf );
  snprintf( buf, 16, "%ld", (long)( v / 10 ) );
  return buf;
}

// The range of the temperature: the extremes of the buckets, with air.
bool faixa( const VmGrafico &vm, int n, int32_t *lo, int32_t *hi ) {
  int32_t mn = INT32_MAX, mx = INT32_MIN;
  for ( int i = 0; i < n; i++ ) {
    const int32_t m = vm.bMedia[i];
    if ( m == SEM_DADO ) continue;
    const int32_t a = vm.bMin[i] != SEM_DADO && vm.bMin[i] < m ? vm.bMin[i] : m;
    const int32_t b = vm.bMax[i] != SEM_DADO && vm.bMax[i] > m ? vm.bMax[i] : m;
    if ( a < mn ) mn = a;
    if ( b > mx ) mx = b;
  }
  if ( mn > mx ) return false;
  if ( mx - mn < FAIXA_MIN ) {
    const int32_t c = chao( mn + mx, 2 );
    mn = c - FAIXA_MIN / 2;
    mx = c + FAIXA_MIN / 2;
  } else {
    int32_t ar = ( mx - mn ) / AR_DIV;
    if ( ar < AR_MIN ) ar = AR_MIN;
    mn -= ar;
    mx += ar;
  }
  *lo = mn;
  *hi = mx;
  return true;
}

// The second curve's own scale: whole percent or whole hPa around its values.
bool faixa2( const VmGrafico &vm, int n, int32_t *lo, int32_t *hi ) {
  int32_t mn = INT32_MAX, mx = INT32_MIN;
  for ( int i = 0; i < n; i++ ) {
    const int32_t v = vm.bMedia2[i];
    if ( v == SEM_DADO ) continue;
    if ( v < mn ) mn = v;
    if ( v > mx ) mx = v;
  }
  if ( mn > mx ) return false;
  const int32_t passo = vm.serie2 == Serie2::UMIDADE ? PASSO_UMIDADE : PASSO_PRESSAO;
  *lo = chao( mn, passo ) * passo;
  *hi = -chao( -mx, passo ) * passo;   // the ceiling
  if ( *hi == *lo ) *hi += passo;
  return true;
}

// How many multiples of `passo` fall strictly inside (lo, hi).
int contar( int32_t lo, int32_t hi, int32_t passo ) {
  const int32_t primeiro = ( chao( lo, passo ) + 1 ) * passo;
  return primeiro >= hi ? 0 : (int)( ( hi - 1 - primeiro ) / passo ) + 1;
}

// One line of the statistics, left to right, with the widest gap of the grid
// that keeps it inside the margins; a freezer's "máx -13,8 °C 03:10 mín -19,7 °C
// 21:32" is 303 px of items, 311 with the narrowest gap, in the 300 px of the
// line, and 289 without the minimum's unit (measured 2026-10-15, both freezer
// samples). Returns false when even the narrowest gap does not fit, and the
// caller writes the line again with fewer units.
bool linha( Dado *d, int n, int16_t y ) {
  int16_t soma = 0;
  int16_t w[6];
  for ( int i = 0; i < n; i++ ) {
    w[i] = (int16_t)( larguraTinta( d[i].k, tipo::apoio ) + ESP1 + larguraTinta( d[i].v, tipo::rotulo ) +
                      ( d[i].t[0] ? ESP2 + larguraTinta( d[i].t, tipo::apoio ) : 0 ) );
    soma += w[i];
  }
  static const int16_t kFolgas[] = { ESP5, ESP4, ESP3, ESP2 };
  const int16_t disponivel = X_STAT_DIR - PAD;
  int16_t folga = ESP2;
  bool coube = false;
  for ( int16_t f : kFolgas ) {
    if ( soma + f * ( n - 1 ) <= disponivel ) { folga = f; coube = true; break; }
  }
  int16_t x = PAD;
  for ( int i = 0; i < n; i++ ) {
    d[i].x = x;
    d[i].y = y;
    x = (int16_t)( x + w[i] + folga );
  }
  return coube;
}

// The first line is the two extremes, each with where it happened; the second
// is the other four, bare. At 9 pt a line holds 300 px, and the old first line,
// "máx 6,0 °C às 12:10 mín 3,0 °C às 22:57", is 326 (items of 161 and 157, and
// the narrowest gap): the words "às" and "em" are gone (the colon and the slash
// say it is a time and a date, and the value is bold where the time is not; the
// time stands ESP2 from the unit, at ESP1 "°C 12:10" read as one word),
// and the unit stands on the first line only, since the second one cannot hold
// it (the 7 pt line held "média 4,4 °C σ 0,7 Δ -0,7 °C agora 5,1 °C"; at 9 pt
// it is 338 px; widths measured with larguraTinta( ) on 2026-10-15).
void montarStats( const VmGrafico &vm, LayoutGrafico &l ) {
  for ( int tentativa = 0; tentativa < 2; tentativa++ ) {
    // The second attempt leaves the "°C" off the minimum, and the unit stands once.
    Dado *d = l.stat;
    int k = 0;
    char texto[16];
    char quando[8];
    auto novo = [&]( const char *nome ) {
      strcpy( d[k].k, nome );
      copiar( d[k].v, texto, sizeof d[k].v );
      copiar( d[k].t, quando, sizeof d[k].t );
      k++;
    };
    // The extremes, with where they happened: the time inside a day's window,
    // the date inside a week's (the time alone would not say which day).
    auto extremo = [&]( int16_t v, const Instante &t, const char *un ) {
      comUnidade( v, un, texto, sizeof texto );
      quando[0] = 0;
      if ( v == SEM_DADO || t.dia == 0 ) return;
      if ( vm.periodo == N_PERIODOS - 1 ) diaMes( t, quando, sizeof quando );
      else horaMin( t, quando, sizeof quando );
    };
    extremo( vm.maximo, vm.tMaximo, " °C" );
    novo( "máx" );
    extremo( vm.minimo, vm.tMinimo, tentativa == 0 ? " °C" : "" );
    novo( "mín" );
    const int linha1 = k;
    quando[0] = 0;
    comUnidade( vm.media, "", texto, sizeof texto );
    novo( "média" );
    comUnidade( vm.desvio, "", texto, sizeof texto );
    novo( "σ" );
    if ( vm.variacao > 0 ) {
      char t[16];
      snprintf( texto, sizeof texto, "+%s", n1( vm.variacao, t ) );
    } else {
      comUnidade( vm.variacao, "", texto, sizeof texto );
    }
    novo( "Δ" );
    comUnidade( vm.agora, "", texto, sizeof texto );
    // "agora" is the last reading of the window; in the past it is not now.
    novo( vm.passado ? "fim" : "agora" );
    l.nStat = k;
    const bool coube1 = linha( d, linha1, Y_STAT1 );
    const bool coube2 = linha( d + linha1, k - linha1, Y_STAT2 );
    if ( ( coube1 && coube2 ) || tentativa == 1 ) return;
  }
}

LayoutGrafico layoutGrafico( const VmGrafico &vm ) {
  LayoutGrafico l{ };
  l.acoes[0] = Acao{ "Dia", Botao::SECUNDARIO, IconeNome::CALENDAR, 0 };
  l.acoes[1] = Acao{ "Voltar", Botao::DISCRETO, IconeNome::ARROW_LEFT, 0 };
  caixasDasAcoes( l.acoes, 2, l.cxAcao );
  tituloComPeriodo( l.titulo, sizeof l.titulo, vm.sensor, vm.periodo, l.acoes, 2, true );

  l.n = vm.n > GRAFICO_BALDES ? GRAFICO_BALDES : vm.n;
  l.temDados = l.n >= 2 && faixa( vm, l.n, &l.lo, &l.hi );
  l.cartao = Caixa16{ X_CARTAO, Y_CARTAO, W_CARTAO, H_CARTAO };
  l.py0 = Y_CARTAO + PAD_CIMA;
  l.py1 = l.py0 + ALTO_PLOT;

  // The grid: whole multiples of the smallest step that leaves at most
  // MAX_GRADE lines inside the range.
  int16_t larguraEsq = 0, larguraDir = 0;
  if ( l.temDados ) {
    static const int32_t kPassos[] = { 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
    int32_t passo = kPassos[sizeof kPassos / sizeof kPassos[0] - 1];
    for ( int32_t s : kPassos ) {
      if ( contar( l.lo, l.hi, s ) <= MAX_GRADE ) { passo = s; break; }
    }
    // A whole step gives whole labels ("6", "-20"): the ",0" of "-20,0" is 15 px
    // of the plot's width at 9 pt, and the unit is on the statistics' line.
    const bool inteiros = passo % 10 == 0;
    for ( int32_t v = ( chao( l.lo, passo ) + 1 ) * passo; v < l.hi && l.nGrade < MAX_GRADE; v += passo ) {
      l.grade[l.nGrade] = v;
      rotuloGrade( v, inteiros, l.rotGrade[l.nGrade] );
      const int16_t w = larguraTinta( l.rotGrade[l.nGrade], tipo::apoio );
      if ( w > larguraEsq ) larguraEsq = w;
      l.nGrade++;
    }
    // The second curve, at the right, with its top and bottom values and the unit between them.
    int32_t lo2, hi2;
    if ( vm.serie2 != Serie2::NENHUMA && faixa2( vm, l.n, &lo2, &hi2 ) ) {
      l.temV2 = true;
      l.lo2 = lo2;
      l.hi2 = hi2;
      l.un2 = vm.serie2 == Serie2::UMIDADE ? "%UR" : "hPa";
      snprintf( l.rot2Cima, sizeof l.rot2Cima, "%d", (int)( hi2 / 10 ) );
      snprintf( l.rot2Baixo, sizeof l.rot2Baixo, "%d", (int)( lo2 / 10 ) );
      larguraDir = larguraTinta( l.rot2Cima, tipo::apoio );
      const int16_t wb = larguraTinta( l.rot2Baixo, tipo::apoio ), wu = larguraTinta( l.un2, tipo::apoio );
      if ( wb > larguraDir ) larguraDir = wb;
      if ( wu > larguraDir ) larguraDir = wu;
    }
  }
  l.px0 = (int16_t)( X_CARTAO + PAD + ( larguraEsq ? larguraEsq + ESP1 : 0 ) );
  l.px1 = (int16_t)( X_CARTAO + W_CARTAO - PAD - ( l.temV2 ? larguraDir + ESP2 : 0 ) );

  // The time axis: HH:MM in a day's window, DD/MM in a week's; the date joins
  // the first label when the window crosses midnight or lies in the past.
  const bool semana = vm.periodo == N_PERIODOS - 1;
  const bool cruza = vm.eixo[0].dia != vm.eixo[2].dia || vm.eixo[0].mes != vm.eixo[2].mes;
  for ( int k = 0; k < 3; k++ ) {
    const Instante &t = vm.eixo[k];
    if ( t.dia == 0 ) continue;
    if ( semana ) diaMes( t, l.eixo[k], sizeof l.eixo[k] );
    else if ( k == 0 && ( cruza || vm.passado ) ) diaMesHoraMin( t, l.eixo[k], sizeof l.eixo[k] );
    else horaMin( t, l.eixo[k], sizeof l.eixo[k] );
  }
  // Every other tick: the middle label goes when it would come within ESP2 of
  // the first or the last. The first one, "12/10 11:32", is 90 px at 9 pt and
  // the middle one meets it whenever the second curve's scale narrows the plot
  // to about 200 px.
  if ( l.eixo[1][0] ) {
    const int16_t meio = rd2( larguraTinta( l.eixo[1], tipo::apoio ) ), xm = rd2( l.px0 + l.px1 );
    const int16_t fimEsq = (int16_t)( l.px0 + larguraTinta( l.eixo[0], tipo::apoio ) );
    const int16_t iniDir = (int16_t)( l.px1 - larguraTinta( l.eixo[2], tipo::apoio ) );
    if ( xm - meio - fimEsq < ESP2 || iniDir - ( xm + meio ) < ESP2 ) l.eixo[1][0] = 0;
  }

  if ( l.temDados ) montarStats( vm, l );

  // The footer.
  const int16_t xChips = ESP2 + W_SETA + ESP2;
  l.anterior = Caixa16{ ESP2, Y_PE, W_SETA, H_PE };
  for ( int i = 0; i < N_PERIODOS; i++ )
    l.periodo[i] = Caixa16{ (int16_t)( xChips + i * ( W_CHIP + ESP1 ) ), Y_PE, W_CHIP, H_PE };
  l.seguinte = Caixa16{ (int16_t)( 320 - ESP2 - W_SETA ), Y_PE, W_SETA, H_PE };
  return l;
}

// The row of a value in the plot, py1 at the bottom of the range.
int16_t yDe( const LayoutGrafico &l, int32_t v, int32_t lo, int32_t hi ) {
  const int32_t alto = l.py1 - l.py0;
  int32_t y = l.py1 - rdiv( ( v - lo ) * alto, hi - lo );
  if ( y < l.py0 ) y = l.py0;
  if ( y > l.py1 ) y = l.py1;
  return (int16_t)y;
}

// The column of bucket i: even in time, so even in index.
int16_t xDe( const LayoutGrafico &l, int i ) { return (int16_t)( l.px0 + rdiv( (int32_t)i * ( l.px1 - l.px0 ), l.n - 1 ) ); }

/* ------------------------------------------------------------------ */
/* Drawing                                                              */
/* ------------------------------------------------------------------ */

// The plot: the band, the grid, the dashed lines, the curves, the last point.
void desenharPlot( Pintor &p, const VmGrafico &vm, const LayoutGrafico &l ) {
  const Tema &T = p.t( );
  const int n = l.n;
  auto yT = [&]( int32_t v ) { return yDe( l, v, l.lo, l.hi ); };

  // The band between each bucket's minimum and maximum, column by column
  // between neighbours: the range the signal really covered. A solid soft tint
  // of the accent's green (the kit has no soft accent of its own), not a
  // gradient. It goes first, so the grid and the lines stay crisp on it, and a
  // gap breaks it.
  for ( int i = 0; i + 1 < n; i++ ) {
    const int32_t ma = vm.bMedia[i], mb = vm.bMedia[i + 1];
    if ( ma == SEM_DADO || mb == SEM_DADO ) continue;
    const int16_t xa = xDe( l, i ), xb = xDe( l, i + 1 );
    if ( xb <= xa ) continue;
    const int16_t topA = yT( vm.bMax[i] != SEM_DADO ? vm.bMax[i] : ma ), botA = yT( vm.bMin[i] != SEM_DADO ? vm.bMin[i] : ma );
    const int16_t topB = yT( vm.bMax[i + 1] != SEM_DADO ? vm.bMax[i + 1] : mb ),
                  botB = yT( vm.bMin[i + 1] != SEM_DADO ? vm.bMin[i + 1] : mb );
    for ( int16_t x = xa; x < xb; x++ ) {
      int16_t top = (int16_t)( topA + rdiv( (int32_t)( topB - topA ) * ( x - xa ), xb - xa ) );
      int16_t bot = (int16_t)( botA + rdiv( (int32_t)( botB - botA ) * ( x - xa ), xb - xa ) );
      if ( bot < top ) { const int16_t t = top; top = bot; bot = t; }
      p.fillRect( x, top, 1, (int16_t)( bot - top + 1 ), T.positivoSuave );
    }
  }

  // The grid, with the temperature's values at the left.
  for ( int i = 0; i < l.nGrade; i++ ) {
    const int16_t y = yT( l.grade[i] );
    p.hline( l.px0, y, (int16_t)( l.px1 - l.px0 ), T.linha );
    textoCentrado( p, l.rotGrade[i], (int16_t)( l.px0 - ESP1 ), (int16_t)( y - rd2( H_APOIO ) ), H_APOIO, tipo::apoio, T.tinta2, 'r' );
  }

  // The alarm limits, when the range already reaches them, and the mean.
  if ( vm.temLimites ) {
    const int32_t lim[2] = { vm.limiteInf, vm.limiteSup };
    for ( int32_t v : lim )
      if ( v > l.lo && v < l.hi ) tracejada( p, l.px0, l.px1, yT( v ), T.perigo );
  }
  if ( vm.media != SEM_DADO && vm.media > l.lo && vm.media < l.hi ) tracejada( p, l.px0, l.px1, yT( vm.media ), T.tinta2 );

  // The second curve: thin, grey, under the temperature.
  if ( l.temV2 ) {
    auto yU = [&]( int32_t v ) { return yDe( l, v, l.lo2, l.hi2 ); };
    for ( int i = 0; i < n; i++ ) {
      if ( vm.bMedia2[i] == SEM_DADO ) continue;
      const bool esq = i > 0 && vm.bMedia2[i - 1] != SEM_DADO;
      const bool dir = i + 1 < n && vm.bMedia2[i + 1] != SEM_DADO;
      const int16_t x = xDe( l, i ), y = yU( vm.bMedia2[i] );
      if ( dir ) p.line( x, y, xDe( l, i + 1 ), yU( vm.bMedia2[i + 1] ), T.tinta2 );
      else if ( !esq ) p.fillRect( (int16_t)( x - LADO_PONTO / 2 ), (int16_t)( y - LADO_PONTO / 2 ), LADO_PONTO, LADO_PONTO, T.tinta2 );
    }
    // Its scale, at the right of the plot: top, unit, bottom.
    const int16_t xr = (int16_t)( l.px1 + ESP2 );
    // Each centred on its row (at 9 pt the top label's baseline 4 px under the
    // plot's top put its caps 10 px above it, across the card's border).
    textoCentrado( p, l.rot2Cima, xr, (int16_t)( l.py0 - rd2( H_APOIO ) ), H_APOIO, tipo::apoio, T.tinta2, 'l' );
    textoCentrado( p, l.un2, xr, (int16_t)( rd2( l.py0 + l.py1 ) - rd2( H_APOIO ) ), H_APOIO, tipo::apoio, T.tinta2, 'l' );
    textoCentrado( p, l.rot2Baixo, xr, (int16_t)( l.py1 - rd2( H_APOIO ) ), H_APOIO, tipo::apoio, T.tinta2, 'l' );
  }

  // The mean, 2 px thick; a bucket alone between two gaps gets a square, or it
  // would draw nothing and look like lost data.
  int ultimo = -1;
  for ( int i = 0; i < n; i++ ) {
    if ( vm.bMedia[i] == SEM_DADO ) continue;
    ultimo = i;
    const bool esq = i > 0 && vm.bMedia[i - 1] != SEM_DADO;
    const bool dir = i + 1 < n && vm.bMedia[i + 1] != SEM_DADO;
    const int16_t x = xDe( l, i ), y = yT( vm.bMedia[i] );
    if ( dir ) {
      const int16_t xb = xDe( l, i + 1 ), yb = yT( vm.bMedia[i + 1] );
      p.line( x, y, xb, yb, T.acento );
      p.line( x, (int16_t)( y + 1 ), xb, (int16_t)( yb + 1 ), T.acento );
    } else if ( !esq ) {
      p.fillRect( (int16_t)( x - LADO_PONTO / 2 ), (int16_t)( y - LADO_PONTO / 2 ), LADO_PONTO, LADO_PONTO, T.acento );
    }
  }

  // The last reading: a dot with a ring of the card's colour round it.
  if ( ultimo >= 0 ) {
    const int16_t x = xDe( l, ultimo ), y = yT( vm.bMedia[ultimo] );
    p.fillCircle( x, y, R_PONTO, T.acento );
    p.circle( x, y, R_ANEL, T.superficie );
  }
}

// An arrow of the footer: a secondary button with the chevron in its middle.
void seta( Pintor &p, const Caixa16 &c, IconeNome ic, bool ativa ) {
  const Tema &T = p.t( );
  botao( p, c.x, c.y, c.w, c.h, "", Botao::SECUNDARIO, !ativa );
  p.icone( ic, ICONE_M, (int16_t)( c.x + rd2( c.w - ICONE_M ) ), (int16_t)( c.y + rd2( c.h - ICONE_M ) ),
           ativa ? T.tinta : desab( T, T.tinta ) );
}

}  // namespace

void desenharGrafico( Pintor &p, const VmGrafico &vm ) {
  const Tema &T = p.t( );
  const LayoutGrafico l = layoutGrafico( vm );
  Caixa16 doKit[2];
  barraTitulo( p, l.titulo, l.acoes, 2, doKit );
  conferirCaixas( "grafico", doKit, l.cxAcao, 2 );
  if ( vm.ocupado ) linhaOcupado( p );

  for ( int i = 0; i < l.nStat; i++ ) {
    const Dado &d = l.stat[i];
    const int16_t xv = (int16_t)( d.x + larguraTinta( d.k, tipo::apoio ) + ESP1 );
    textoCentrado( p, d.k, d.x, d.y, H_APOIO, tipo::apoio, T.tinta2, 'l' );
    textoCentrado( p, d.v, xv, d.y, H_APOIO, tipo::rotulo, T.tinta, 'l' );
    if ( d.t[0] ) textoCentrado( p, d.t, (int16_t)( xv + larguraTinta( d.v, tipo::rotulo ) + ESP2 ), d.y, H_APOIO, tipo::apoio, T.tinta2, 'l' );
  }

  superficie( p, l.cartao.x, l.cartao.y, l.cartao.w, l.cartao.h );
  if ( l.temDados ) desenharPlot( p, vm, l );
  else semDados( p, l.cartao, "Escolha outro período ou outro dia." );

  // The time axis, inside the card's foot, on the plot's own columns.
  static const char alinha[3] = { 'l', 'c', 'r' };
  const int16_t xs[3] = { l.px0, rd2( l.px0 + l.px1 ), l.px1 };
  for ( int k = 0; k < 3; k++ )
    if ( l.eixo[k][0] ) textoCentrado( p, l.eixo[k], xs[k], l.py1, H_APOIO, tipo::apoio, T.tinta2, alinha[k] );

  // The footer: back in time, the periods, forward in time.
  seta( p, l.anterior, IconeNome::CHEVRON_LEFT, true );
  for ( int i = 0; i < N_PERIODOS; i++ ) {
    const Caixa16 &c = l.periodo[i];
    const bool on = i == vm.periodo;
    p.fillRoundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, on ? T.acento : T.superficie2 );
    if ( !on ) p.roundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, T.linha );
    textoCentrado( p, rotuloPeriodo( i ), (int16_t)( c.x + rd2( c.w ) ), c.y, c.h, tipo::rotulo, on ? T.acentoTinta : T.tinta, 'c' );
  }
  seta( p, l.seguinte, IconeNome::CHEVRON_RIGHT, vm.passado );
}

int alvosGrafico( const VmGrafico &vm, Alvo *out ) {
  const LayoutGrafico l = layoutGrafico( vm );
  int n = addAlvo( out, 0, alvoDaAcao( l.cxAcao[1] ), GRF_VOLTAR );
  n = addAlvo( out, n, alvoDaAcao( l.cxAcao[0] ), GRF_DIA );
  n = addAlvo( out, n, l.cartao, GRF_AREA );
  n = addAlvo( out, n, l.anterior, GRF_ANTERIOR );
  for ( int i = 0; i < N_PERIODOS; i++ ) n = addAlvo( out, n, l.periodo[i], GRF_PERIODO, (int16_t)i );
  if ( vm.passado ) n = addAlvo( out, n, l.seguinte, GRF_SEGUINTE );
  return n;
}

}  // namespace tela
}  // namespace ua
