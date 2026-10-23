// The calendar of the days that have history (MODE_CALENDAR). See Graficos.h.
//
// Family A's `calendario_out` of the plan: the month and the arrows in the
// title bar, a card with the weekdays and the days, and in the footer what the
// dot means and "Hoje".
#include <stdio.h>
#include <string.h>

#include "Graficos.h"
#include "GraficosComum.h"

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

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
// The weekday letters are 13 px tall in 9 pt bold, in a 16 px header: with the
// hairline at its foot they had 1 px of air under them. The hairline 2 px lower
// (inside the first week's 5 px of air above its digits) leaves them 3. The
// letters cannot go lower instead: their ink would enter the first week's
// targets (host_telas.py failed on it, "crosses the edge of target 4/2").
constexpr int16_t Y_FIO = Y_LINHAS + 2;
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
// The names are the pack's, asked for when the title is built: English "Jan" .. "Dec", pt-BR "Jan Fev Mar Abr Mai Jun Jul Ago Set Out Nov Dez" as before, es-ES "Ene Feb
// Mar Abr May Jun Jul Ago Sep Oct Nov Dic". A table of them made at start-up would hold the English for good (Textos.h). layoutCalendario( ) keeps the month in 1..12.
const char *nomeDoMes( int mes ) {
  switch ( mes ) {
    case 1: return UA_TXT( "Jan" );
    case 2: return UA_TXT( "Feb" );
    case 3: return UA_TXT( "Mar" );
    case 4: return UA_TXT( "Apr" );
    case 5: return UA_TXT( "May" );
    case 6: return UA_TXT( "Jun" );
    case 7: return UA_TXT( "Jul" );
    case 8: return UA_TXT( "Aug" );
    case 9: return UA_TXT( "Sep" );
    case 10: return UA_TXT( "Oct" );
    case 11: return UA_TXT( "Nov" );
    default: return UA_TXT( "Dec" );
  }
}

// The weekday of column i (0 = Sunday), by the English two-letter names: the initials repeat in a language ("S" is Sunday and Saturday in English, Monday, Friday and Saturday
// in Portuguese) and one English gives one translation, so the key is the unambiguous name. pt-BR draws "D S T Q Q S S" as before, es-ES "D L M X J V S", English its two letters.
const char *inicialDaSemana( int i ) {
  switch ( i ) {
    case 0: return UA_TXT( "Su" );
    case 1: return UA_TXT( "Mo" );
    case 2: return UA_TXT( "Tu" );
    case 3: return UA_TXT( "We" );
    case 4: return UA_TXT( "Th" );
    case 5: return UA_TXT( "Fr" );
    default: return UA_TXT( "Sa" );
  }
}

// The arrows are 32 px wide, not the plan's 36: with 36 the widest titles
// ("Mar 2026", "Ago 2026", "Nov 2026", 107 to 108 px measured 2026-10-14) are 2
// to 3 px too wide for the larger font and fall to the smaller, so the title
// would change size from month to month. With 32 they all fit, up to "Mar 2000"
// (111 px of 113). That is the pt-BR names' measure; with the pack's names (2026-10-22, every year
// 2000 to 2099): English fits in every month ("May 2000", the widest, is 115 px of the 123 that
// its "Back" leaves); Spanish has "Volver" 4 px wider than "Voltar", leaves 109 px, and draws "May 2026"
// (112 px) in the smaller title font, as "Mar" and "Ago" in 3 of the years 2020 to 2099. Only a
// narrower arrow would hold them, and that moves the pt-BR frames.
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
  l.acoes[2] = acaoVoltar( );   // the kit's own, the label barraTitulo( ) draws and caixasDasAcoes( ) measures
  caixasDasAcoes( l.acoes, 3, l.cxAcao );
  char nome[16];   // the month in the pack's words, bounded whatever a pack gives: the title below then never cuts a character
  copiar( nome, nomeDoMes( mes ), sizeof nome );
  snprintf( l.titulo, sizeof l.titulo, "%s %u", nome, (unsigned)vm.ano );

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
  for ( int i = 0; i < DIAS_SEMANA; i++ )
    textoCentrado( p, inicialDaSemana( i ), (int16_t)( X_CARTAO + i * W_CEL + rd2( W_CEL ) ), Y_CARTAO, H_CABECALHO, tipo::rotulo, T.tinta2, 'c' );
  p.hline( X_CARTAO + PAD, Y_FIO, W_CARTAO - 2 * PAD, T.linha );

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

  // The empty month's note is the legend's own words: "Nenhum dia com histórico
  // neste mês." was 280 px of ink in 9 pt, under "Hoje" (the title already says
  // which month it is).
  textoCentrado( p, l.temAlgum ? UA_TXT( "Dot = day with history." ) : UA_TXT( "No days with history." ), ESP2, Y_PE, H_PE, tipo::apoio, T.tinta2,
                 'l' );
  botao( p, l.hoje.x, l.hoje.y, l.hoje.w, l.hoje.h, UA_TXT( "Today" ), Botao::SECUNDARIO );
}

int alvosCalendario( const VmCalendario &vm, Alvo *out ) {
  const LayoutCalendario l = layoutCalendario( vm );
  int n = addAlvo( out, 0, alvoDaAcao( l.cxAcao[2] ), CAL_VOLTAR );
  n = addAlvo( out, n, alvoDoTitulo( l.cxAcao[0] ), ALVO_INERTE );
  n = addAlvo( out, n, alvoDaAcao( l.cxAcao[0] ), CAL_MES_ANTERIOR );
  // In the current month the next arrow is drawn disabled: inert, with the box it has alive, so that a touch on it does not reach the
  // previous month's arrow or "Voltar", which it adjoins.
  n = addAlvoSe( out, n, vm.avanca, alvoDaAcao( l.cxAcao[1] ), CAL_MES_SEGUINTE );
  for ( int d = 1; d <= l.dias; d++ )
    if ( temHistorico( vm, d ) ) n = addAlvo( out, n, celula( l, d ), CAL_DIA, (int16_t)d );
  return addAlvo( out, n, l.hoje, CAL_HOJE );
}

}  // namespace tela
}  // namespace ua
