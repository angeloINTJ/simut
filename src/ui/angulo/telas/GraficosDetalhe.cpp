// The detail of the chart (MODE_GRAPH_DETAIL). See Graficos.h.
//
// Family A's `detalhe_temp` of the plan: six cards of 96 x 72, three to a row,
// and a pager between the quantities the sensor has.
#include <stdio.h>
#include <string.h>

#include "Graficos.h"
#include "GraficosComum.h"

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

using namespace graficos;

namespace {

// The plan's cards: 96 x 72, ESP2 apart, from Y0, three to a row. In a card the
// label sits ESP2 under the top, the value's baseline 44 px down and the line
// under it, a box of H_APOIO, at 48 (the plan's 48 and 52: the 9 pt line is 14
// px of caps and its descenders reached the card's border).
constexpr int16_t W_CARTAO = 96, H_CARTAO = 72;
constexpr int N_CARTOES = 6, COLUNAS = 3;
enum { C_MAXIMO, C_MINIMO, C_MEDIA, C_DESVIO, C_VARIACAO, C_AGORA };   // the cards, in reading order
constexpr int16_t Y_VALOR = 44, Y_SUB = 48;

// A card's texts start ESP2 in (the plan's PAD, 12, left 76 px, and the 9 pt
// "dispersão" is 76, "início→fim" 75, "Desvio σ" 75). The labels and the lines
// under the values may run to ESP2 short of the right edge: 80 px. The value
// and its unit may run to ESP1 short of it: 84, for "1009,1 hPa" (82 px,
// measured 2026-10-15), which 80 would cut.
constexpr int16_t X_TEXTO = ESP2;
constexpr int16_t LARGURA_TEXTO = W_CARTAO - X_TEXTO - ESP2;
constexpr int16_t LARGURA_VALOR = W_CARTAO - X_TEXTO - ESP1;

// The gap between a value and its unit, as the eye reads it. The kit's numero( ) puts the unit ESP1
// after the value's ink, and on the pixels that is 4 blank columns after every digit (measured
// 2026-10-15 on "1x" + "hPa" drawn as the card draws it), but the eye reads the mean distance
// down the digit's height, and that differs with the shape of its right edge: the stem of a "1"
// is 4.00 px from the "h" on all 12 rows, the diagonal of a "7" 4 at the top and 8 at the foot
// (6.42 on average), and "1009,1hPa" read as glued while "1015,7 hPa" did not. Mean over the
// rows, per last digit: 0 4.83, 1 4.00, 2 5.50, 3 5.08, 4 5.67, 5 5.00, 6 5.00, 7 6.42, 8 5.00,
// 9 5.17. One pixel more after the "1" brings it to 5.00, and every digit is then between 4.83
// and 6.42 (2.42 wide before). The "7" and the "4" are left wide, not pulled in to the mean:
// at 3 px the "7"'s top bar read as glued to the "h" on the frame, and no digit is closer to
// its unit than the 4 px they all have now. The widest value that keeps its unit still keeps
// it ("1009,1 hPa" is 83 px of 84).
int16_t folgaDaUnidade( const char *valor ) {
  const int n = (int)strlen( valor );
  return ( n > 0 && valor[n - 1] == '1' ) ? (int16_t)( ESP1 + 1 ) : (int16_t)ESP1;
}

struct Cartao {
  Caixa16 caixa;
  const char *rotulo;
  char valor[16];
  const char *unidade;
  char sub[32];
};

struct LayoutDetalhe {
  char titulo[32];
  Caixa16 cxVoltar;
  Caixa16 area;                 // where the cards go, for the empty message
  bool vazio;
  Cartao cartao[N_CARTOES];
  int pagina, paginas;          // 0-based, and how many
  const char *nome;             // of the page's quantity
};

void unidadeENome( Grandeza g, const char **unidade, const char **nome ) {
  switch ( g ) {
    case Grandeza::UMIDADE: *unidade = UA_TXT( "%RH" ); *nome = UA_TXT( "Humidity" ); break;
    case Grandeza::PRESSAO: *unidade = "hPa"; *nome = UA_TXT( "Pressure" ); break;
    default: *unidade = "°C"; *nome = UA_TXT( "Temperature" ); break;
  }
}

// The number of readings behind the mean, in the longest wording that fits.
void leituras( uint16_t n, char *buf, int cap ) {
  snprintf( buf, (size_t)cap, "%u %s", (unsigned)n, UA_TXT( "readings" ) );
  if ( larguraTinta( buf, tipo::apoio ) <= LARGURA_TEXTO ) return;
  snprintf( buf, (size_t)cap, "%s = %u", UA_TXT( "n" ), (unsigned)n );
}

// The two things the targets need from the layout, apart from the cards: the back button's box and the pager's state. alvosDetalhe( ) takes only these (the
// layout also writes the six cards' values and lines: 0.8 ms of the detail's 53.0 on the board, a second time, 2026-10-16).
Caixa16 caixaVoltar( ) {
  const Acao voltar = acaoVoltar( );   // the one barraTituloVoltar( ) draws below
  Caixa16 c;
  caixasDasAcoes( &voltar, 1, &c );
  return c;
}

void paginacao( const VmDetalhe &vm, int *paginas, int *pagina ) {
  *paginas = vm.paginas < 1 ? 1 : vm.paginas > 3 ? 3 : vm.paginas;
  *pagina = vm.pagina >= *paginas ? *paginas - 1 : vm.pagina;
}

LayoutDetalhe layoutDetalhe( const VmDetalhe &vm ) {
  LayoutDetalhe l{ };
  snprintf( l.titulo, sizeof l.titulo, "%s · %s", UA_TXT( "Detail" ), rotuloPeriodo( vm.periodo ) );
  l.cxVoltar = caixaVoltar( );
  l.area = Caixa16{ ESP2, Y0, 320 - 2 * ESP2, ( N_CARTOES / COLUNAS ) * H_CARTAO + ESP2 };

  paginacao( vm, &l.paginas, &l.pagina );
  const EstatDetalhe &e = vm.est[l.pagina];
  l.vazio = e.leituras == 0;
  const char *unidade;
  unidadeENome( e.grandeza, &unidade, &l.nome );

  // "Desvio (σ)" is 86 px in 9 pt bold, over the card's 80.
  const char *const rotulos[N_CARTOES] = { UA_TXT( "Maximum" ), UA_TXT( "Minimum" ), UA_TXT( "Average" ), UA_TXT( "Std dev σ" ), UA_TXT( "Change" ),
                                           vm.passado ? UA_TXT( "End" ) : UA_TXT( "Now" ) };   // looked up here, in the layout: the draw takes them from the cards it holds
  const int16_t v[N_CARTOES] = { e.maximo, e.minimo, e.media, e.desvio, e.variacao, e.agora };
  for ( int i = 0; i < N_CARTOES; i++ ) {
    Cartao &c = l.cartao[i];
    c.caixa = Caixa16{ (int16_t)( ESP2 + ( i % COLUNAS ) * ( W_CARTAO + ESP2 ) ), (int16_t)( Y0 + ( i / COLUNAS ) * ( H_CARTAO + ESP2 ) ),
                       W_CARTAO, H_CARTAO };
    c.rotulo = rotulos[i];
    c.unidade = unidade;
    if ( v[i] == SEM_DADO ) snprintf( c.valor, sizeof c.valor, "--%s-", separadorDecimal( ) );
    else if ( i == C_VARIACAO && v[i] > 0 ) { char t[16]; snprintf( c.valor, sizeof c.valor, "+%s", n1( v[i], t ) ); }   // the change keeps its sign
    else n1( v[i], c.valor );
  }
  // What each card says under its value. A date with its time ("30/09 14:17")
  // is 92 px in 9 pt, over the 80, so the extremes say what the chart's
  // statistics say: the time inside a day's window (it is unique there), the date
  // inside a week's.
  const bool semana = vm.periodo == N_PERIODOS - 1;
  if ( e.tMaximo.dia ) {
    if ( semana ) diaMes( e.tMaximo, l.cartao[C_MAXIMO].sub, sizeof l.cartao[C_MAXIMO].sub );
    else horaMin( e.tMaximo, l.cartao[C_MAXIMO].sub, sizeof l.cartao[C_MAXIMO].sub );
  }
  if ( e.tMinimo.dia ) {
    if ( semana ) diaMes( e.tMinimo, l.cartao[C_MINIMO].sub, sizeof l.cartao[C_MINIMO].sub );
    else horaMin( e.tMinimo, l.cartao[C_MINIMO].sub, sizeof l.cartao[C_MINIMO].sub );
  }
  leituras( e.leituras, l.cartao[C_MEDIA].sub, sizeof l.cartao[C_MEDIA].sub );
  copiar( l.cartao[C_DESVIO].sub, UA_TXT( "spread" ), sizeof l.cartao[C_DESVIO].sub );
  copiar( l.cartao[C_VARIACAO].sub, UA_TXT( "start→end" ), sizeof l.cartao[C_VARIACAO].sub );   // "início → fim" is 83 px
  if ( e.tAgora.dia ) {
    // In the past the date says which day "Fim" is (the time of a window's last
    // reading is the window's end, the same every day); now, the time is enough.
    if ( vm.passado ) diaMes( e.tAgora, l.cartao[C_AGORA].sub, sizeof l.cartao[C_AGORA].sub );
    else horaMin( e.tAgora, l.cartao[C_AGORA].sub, sizeof l.cartao[C_AGORA].sub );
  }
  return l;
}

}  // namespace

void desenharDetalhe( Pintor &p, const VmDetalhe &vm ) {
  const Tema &T = p.t( );
  const LayoutDetalhe l = layoutDetalhe( vm );
  Caixa16 doKit;
  barraTituloVoltar( p, l.titulo, &doKit );
  conferirCaixas( "detalhe", &doKit, &l.cxVoltar, 1 );

  if ( l.vazio ) {
    semDados( p, l.area, UA_TXT( "Go back and choose another period." ) );
  } else {
    for ( int i = 0; i < N_CARTOES; i++ ) {
      const Cartao &c = l.cartao[i];
      superficie( p, c.caixa.x, c.caixa.y, c.caixa.w, c.caixa.h );
      const int16_t x = (int16_t)( c.caixa.x + X_TEXTO );
      textoCentrado( p, c.rotulo, x, (int16_t)( c.caixa.y + ESP2 ), H_APOIO, tipo::rotulo, T.tinta2, 'l' );
      // The number in display type, the unit small and grey after it (in the
      // regular weight: "hPa" is 29 px, 32 in bold), with the gap the eye reads the
      // same after every digit (folgaDaUnidade( )); a value that would still run
      // past the card goes without its unit.
      const int16_t folga = folgaDaUnidade( c.valor );
      const int16_t w = (int16_t)( larguraTinta( c.valor, Fonte::DISP9 ) + folga + larguraTinta( c.unidade, tipo::apoio ) );
      const int16_t yValor = (int16_t)( c.caixa.y + Y_VALOR );
      const Tinta t = p.texto( c.valor, x, yValor, Fonte::DISP9, T.tinta, 'l' );
      if ( w <= LARGURA_VALOR ) p.texto( c.unidade, (int16_t)( t.x1 + folga ), yValor, tipo::apoio, T.tinta2, 'l' );
      if ( c.sub[0] ) textoCentrado( p, c.sub, x, (int16_t)( c.caixa.y + Y_SUB ), H_APOIO, tipo::apoio, T.tinta2, 'l' );
    }
  }
  if ( l.paginas > 1 ) paginadorComEstados( p, (int16_t)( l.pagina + 1 ), (int16_t)l.paginas, l.nome );
}

int alvosDetalhe( const VmDetalhe &vm, Alvo *out ) {
  struct { Caixa16 cxVoltar; int pagina, paginas; } l;
  l.cxVoltar = caixaVoltar( );
  paginacao( vm, &l.paginas, &l.pagina );
  int n = addAlvo( out, 0, alvoDaAcao( l.cxVoltar ), DET_VOLTAR );
  n = addAlvo( out, n, alvoDoTitulo( l.cxVoltar ), ALVO_INERTE );
  // The cards and the band around them turn the page, on every page and on the empty one (the glue says in which order).
  n = addAlvo( out, n, bandaCentral( ), DET_AREA );
  if ( l.paginas > 1 ) {
    // The arrow that has nowhere to go is drawn disabled: inert, so that a touch on it does not turn the page the other way.
    n = addAlvoSe( out, n, l.pagina > 0, paginadorEsquerda( ), DET_PAG_ANTERIOR );
    n = addAlvoSe( out, n, l.pagina < l.paginas - 1, paginadorDireita( ), DET_PAG_PROXIMA );
  }
  return n;
}

}  // namespace tela
}  // namespace ua
