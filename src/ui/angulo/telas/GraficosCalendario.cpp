// The calendar of the days that have history (MODE_CALENDAR). See Graficos.h.
//
// Family A's `calendario_out` of the plan: the month and the arrows in the
// title bar, a card with the weekdays and the days, and in the footer what the
// dot means and "Hoje".
#include <stdio.h>
#include <string.h>

#include "Graficos.h"
#include "GraficosComum.h"

namespace ua {
namespace tela {

using namespace graficos;

namespace {

// The card holds the weekday letters and up to six weeks of 24 px: the old
// panel's grid ran from y 46 to 190, and a month that starts on a Friday or a
// Saturday needs all six. So the card starts ESP1 under the title bar (not at
// Y0) and the weeks start 16 px under its top, where the plan's hairline goes.
constexpr int DIAS_SEMANA = 7;
constexpr int16_t X_CARTAO = ESP2, Y_CARTAO = 32, W_CARTAO = 304, H_CARTAO = 160;
constexpr int16_t H_CABECALHO = 16, H_LINHA = 24, MAX_LINHAS = 6;
constexpr int16_t W_CEL = W_CARTAO / DIAS_SEMANA;       // 43, as the plan
constexpr int16_t Y_LINHAS = Y_CARTAO + H_CABECALHO;    // the first week
static_assert( Y_LINHAS + MAX_LINHAS * H_LINHA == Y_CARTAO + H_CARTAO, "six weeks fill the card" );
constexpr int16_t W_MARCA = 28;                         // today's box, 28 x 24 (the plan's)
constexpr int16_t Y_PONTO = H_LINHA - 3;                // the dot sits 3 px above its week's foot (the plan's 21)
constexpr int16_t R_PONTO = 2;
constexpr int16_t Y_PE = 196, H_PE = 40, W_HOJE = 64;

// The month in the title is its three letters, as the old panel wrote it. The
// plan drew only October, whole, and the longer names do not fit beside the
// arrows (measured 2026-10-14: "Novembro 2026" runs 135 px in the smaller title
// font, the room is about 120). The letters also let the title stay in the
// larger font in every month, which the bar decides by width (barraTitulo( ):
// the title must end 2 * ESP2 short of the first action).
const char *const kMeses[12] = { "Jan", "Fev", "Mar", "Abr", "Mai", "Jun", "Jul", "Ago", "Set", "Out", "Nov", "Dez" };

// The arrows are 32 px wide, not the plan's 36: with 36 the widest titles
// ("Mar 2026", "Ago 2026", "Nov 2026", 107 to 108 px measured 2026-10-14) are 2
// to 3 px too wide for the larger font and fall to the smaller, so the title
// would change size from month to month. With 32 they all fit, up to "Mar 2000"
// (111 px of 113).
constexpr int16_t W_SETA = 32;

struct LayoutCalendario {
  char titulo[32];
  Acao acoes[3];                // ‹, ›, Voltar
  Caixa16 cxAcao[3];
  int dias;                     // in the month
  int primeiro;                 // weekday of the 1st, 0 = Sunday
  bool temAlgum;                // some day of the month has history
  Caixa16 hoje;                 // the "Hoje" button
};

bool bissexto( int ano ) { return ( ano % 4 == 0 && ano % 100 != 0 ) || ano % 400 == 0; }

int diasNoMes( int ano, int mes ) {
  static const uint8_t kDias[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  return mes == 2 && bissexto( ano ) ? 29 : kDias[mes - 1];
}

// Weekday of a date, 0 = Sunday (Sakamoto's: the device's mktime( ) needs the
// time zone, a calendar page does not).
int diaDaSemana( int ano, int mes, int dia ) {
  static const int8_t kT[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if ( mes < 3 ) ano -= 1;
  return ( ano + ano / 4 - ano / 100 + ano / 400 + kT[mes - 1] + dia ) % DIAS_SEMANA;
}

// Where day d (1..31) sits: the card's column of its weekday, the week's row.
Caixa16 celula( const LayoutCalendario &l, int d ) {
  const int idx = l.primeiro + d - 1;
  return Caixa16{ (int16_t)( X_CARTAO + ( idx % DIAS_SEMANA ) * W_CEL ), (int16_t)( Y_LINHAS + ( idx / DIAS_SEMANA ) * H_LINHA ), W_CEL, H_LINHA };
}

bool temHistorico( const VmCalendario &vm, int d ) { return ( vm.dias >> d ) & 1u; }

LayoutCalendario layoutCalendario( const VmCalendario &vm ) {
  LayoutCalendario l{ };
  const int mes = vm.mes < 1 ? 1 : vm.mes > 12 ? 12 : vm.mes;
  l.acoes[0] = Acao{ "", Botao::SECUNDARIO, IconeNome::CHEVRON_LEFT, W_SETA };
  l.acoes[1] = Acao{ "", Botao::SECUNDARIO, IconeNome::CHEVRON_RIGHT, W_SETA };
  l.acoes[2] = Acao{ "Voltar", Botao::DISCRETO, IconeNome::ARROW_LEFT, 0 };
  caixasDasAcoes( l.acoes, 3, l.cxAcao );
  snprintf( l.titulo, sizeof l.titulo, "%s %u", kMeses[mes - 1], (unsigned)vm.ano );

  l.dias = diasNoMes( vm.ano, mes );
  l.primeiro = diaDaSemana( vm.ano, mes, 1 );
  for ( int d = 1; d <= l.dias; d++ )
    if ( temHistorico( vm, d ) ) l.temAlgum = true;
  l.hoje = Caixa16{ (int16_t)( 320 - ESP2 - W_HOJE ), Y_PE, W_HOJE, H_PE };
  return l;
}

}  // namespace

void desenharCalendario( Pintor &p, const VmCalendario &vm ) {
  const Tema &T = p.t( );
  const LayoutCalendario l = layoutCalendario( vm );
  Caixa16 doKit[3];
  barraTitulo( p, l.titulo, l.acoes, 3, doKit );
  conferirCaixas( "calendario", doKit, l.cxAcao, 3 );
  if ( vm.ocupado ) linhaOcupado( p );
  // The arrow to a month that cannot have history yet is drawn disabled, over
  // the one the bar drew: the same box, the same icon.
  if ( !vm.avanca )
    botao( p, l.cxAcao[1].x, l.cxAcao[1].y, l.cxAcao[1].w, l.cxAcao[1].h, "", Botao::SECUNDARIO, true, IconeNome::CHEVRON_RIGHT );

  superficie( p, X_CARTAO, Y_CARTAO, W_CARTAO, H_CARTAO );
  static const char *const kSemana[DIAS_SEMANA] = { "D", "S", "T", "Q", "Q", "S", "S" };
  for ( int i = 0; i < DIAS_SEMANA; i++ )
    textoCentrado( p, kSemana[i], (int16_t)( X_CARTAO + i * W_CEL + rd2( W_CEL ) ), Y_CARTAO, H_CABECALHO, tipo::rotulo, T.tinta2, 'c' );
  p.hline( X_CARTAO + PAD, Y_LINHAS, W_CARTAO - 2 * PAD, T.linha );

  for ( int d = 1; d <= l.dias; d++ ) {
    const Caixa16 c = celula( l, d );
    const int16_t cx = (int16_t)( c.x + rd2( c.w ) );
    const bool eHoje = d == vm.hoje, dados = temHistorico( vm, d );
    if ( eHoje ) p.fillRoundRect( (int16_t)( cx - W_MARCA / 2 ), c.y, W_MARCA, c.h, RAIO_CONTROLE, T.acento );
    char num[16];   // room for any int: with 8, a -fsanitize build fails -Wformat-truncation
    snprintf( num, sizeof num, "%d", d );
    textoCentrado( p, num, cx, c.y, c.h, dados ? tipo::botao : tipo::corpo, eHoje ? T.acentoTinta : dados ? T.tinta : T.tinta2, 'c' );
    if ( dados && !eHoje ) p.fillCircle( cx, (int16_t)( c.y + Y_PONTO ), R_PONTO, T.acento );
  }

  textoCentrado( p, l.temAlgum ? "Ponto = dia com histórico." : "Nenhum dia com histórico neste mês.", ESP2, Y_PE, H_PE, tipo::apoio,
                 T.tinta2, 'l' );
  botao( p, l.hoje.x, l.hoje.y, l.hoje.w, l.hoje.h, "Hoje", Botao::SECUNDARIO );
}

int alvosCalendario( const VmCalendario &vm, Alvo *out ) {
  const LayoutCalendario l = layoutCalendario( vm );
  int n = addAlvo( out, 0, alvoDaAcao( l.cxAcao[2] ), CAL_VOLTAR );
  n = addAlvo( out, n, alvoDaAcao( l.cxAcao[0] ), CAL_MES_ANTERIOR );
  if ( vm.avanca ) n = addAlvo( out, n, alvoDaAcao( l.cxAcao[1] ), CAL_MES_SEGUINTE );
  for ( int d = 1; d <= l.dias; d++ )
    if ( temHistorico( vm, d ) ) n = addAlvo( out, n, celula( l, d ), CAL_DIA, (int16_t)d );
  return addAlvo( out, n, l.hoje, CAL_HOJE );
}

}  // namespace tela
}  // namespace ua
