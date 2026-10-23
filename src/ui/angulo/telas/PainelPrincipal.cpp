// The panel's main screen. See PainelPrincipal.h.
#include "PainelPrincipal.h"

#include <stdio.h>
#include <string.h>

#include "PainelComum.h"
#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

namespace {

using painel::caixa;
using painel::cortar;
using Sensor = VmPainel::Sensor;
using Tipo = VmPainel::Tipo;
using NavTipo = VmPainel::NavTipo;

/* The plan's dashboard grid (dashboard_normal, 2026-10-14): the status bar, two
 * cards 8 px apart and the row of shortcuts. The upper card is the plan's
 * 64 px "Ambiente" strip, the lower one its 80 px sensor card. */
constexpr int16_t X_CARTAO = ESP2, W_CARTAO = 320 - 2 * ESP2;
constexpr int16_t Y_CIMA = Y0, H_CIMA = 64;
constexpr int16_t Y_BAIXO = Y_CIMA + H_CIMA + ESP2, H_BAIXO = 80;
constexpr int16_t X_INT = X_CARTAO + PAD, X_INT_DIR = X_CARTAO + W_CARTAO - PAD;   // inside a card: 20 .. 300

/* The kit's icon sizes and the plan's 16 px line box for its small texts. */
constexpr int16_t ICONE_P = 16, ICONE_M = 20, ICONE_G = 24;
constexpr int16_t H_TEXTO = 16;
constexpr int16_t RAIO_PONTO = 3;   // the alarm dot on a button (the plan's)

/* Five 56 px positions 4 px apart, 12 px from each edge (where the plan's row
 * starts): the old row's five positions, in the new grid. */
constexpr int NAV_N = 5;
constexpr int16_t Y_NAV = 196, H_NAV = 40, W_NAV = 56, X_NAV = ESP3, PASSO_NAV = W_NAV + ESP1;

/* The Gráfico button carries its word: an icon alone does not say what a
 * button does (Ângulo §6) and the panel has no tooltip to say it. 72 px wide:
 * the word is 62 px of ink in 9 pt bold, the floor of the panel's text since
 * 2026-10-15, and 56 px cut it on both sides. */
constexpr int16_t W_GRAFICO = 72, H_GRAFICO = 48;

/* The name row sits 4 px under the upper card's top edge. The lower card's
 * name is drawn in a 12 px box (its capital height), 8 px under the edge, so
 * that a descender (g, p, y) still clears the big number under it: DISP32 ink
 * starts 43 px above its baseline at 176 (measured 2026-10-14). The badge is
 * 16 px and shares the name's baseline. */
constexpr int16_t H_NOME_CIMA = H_TEXTO, H_NOME_BAIXO = 12, H_SELO = 16;

/* Where the numbers sit. The upper card's baseline is the plan's (92); the
 * lower card's big number keeps the plan's 176. */
constexpr int16_t BASE_CIMA = Y_CIMA + 56, BASE_NUMERO = Y_BAIXO + 68;

/* The lower card's right column: 18 px between baselines (the 9 pt text's 14 px
 * caps and 4 px apart). Two lines fit between the badge and the number's
 * baseline; a third would run into the badge (2026-10-15, when the floor of the
 * panel's text rose from 7 to 9 pt: its 7 pt caps were 2.4 mm on the 3.5-inch
 * 320x240 panel, under what reads at arm's length). */
constexpr int16_t PASSO_LINHA = ESP4 + 2;

/* The web-busy notice floats over the lower card's foot, its bottom on the
 * card's: the name and the badge above it stay on the glass. */
constexpr int16_t H_AVISO = 52;

/* Every box the screen paints or touches, from the view model alone. */
struct LayoutPainel {
  Caixa16 cima, baixo;
  Caixa16 vazio;                       // w != 0: no sensor at all, one card says so
  Caixa16 graficoCima, graficoBaixo;   // w == 0: not shown
  Caixa16 nav[NAV_N];                  // w == 0: the position is empty
  Caixa16 aviso;                       // the web-busy notice
};

LayoutPainel layoutPainel( const VmPainel &vm ) {
  LayoutPainel l{ };
  l.cima = caixa( X_CARTAO, Y_CIMA, W_CARTAO, H_CIMA );
  l.baixo = caixa( X_CARTAO, Y_BAIXO, W_CARTAO, H_BAIXO );
  // Two cards saying the same thing look like a fault: with no sensor in
  // either, the message gets the area of both.
  if ( vm.cima.tipo == Tipo::NENHUM && vm.baixo.tipo == Tipo::NENHUM )
    l.vazio = caixa( X_CARTAO, Y_CIMA, W_CARTAO, Y_BAIXO + H_BAIXO - Y_CIMA );
  // The button centred in its card, against the inner right edge.
  if ( vm.cima.minmax && vm.cima.tipo != Tipo::NENHUM )
    l.graficoCima = caixa( X_INT_DIR - W_GRAFICO, Y_CIMA + rd2( H_CIMA - H_GRAFICO ), W_GRAFICO, H_GRAFICO );
  if ( vm.baixo.minmax && vm.baixo.tipo != Tipo::NENHUM )
    l.graficoBaixo = caixa( X_INT_DIR - W_GRAFICO, Y_BAIXO + rd2( H_BAIXO - H_GRAFICO ), W_GRAFICO, H_GRAFICO );
  for ( int i = 0; i < NAV_N; i++ )
    if ( vm.nav[i].tipo != NavTipo::NADA ) l.nav[i] = caixa( X_NAV + i * PASSO_NAV, Y_NAV, W_NAV, H_NAV );
  // Over the lower card's foot, which the plan lets show above it (dashboard_ocupado).
  l.aviso = caixa( X_CARTAO, Y_BAIXO + H_BAIXO - H_AVISO, W_CARTAO, H_AVISO );
  return l;
}

/* How a card looks in the alarm's phases: the lit phase floods it in danger,
 * a silenced alarm only draws the border, and the dark phase is the card as it
 * is (the situation's colours already say what is wrong). */
struct Aspecto {
  bool cheio, silenciado;
  uint16_t tinta, tinta2;
};

Aspecto aspectoDe( const Tema &T, const VmPainel &vm, const Sensor &s ) {
  Aspecto a{ };
  a.cheio = s.alarme && !vm.silenciado && vm.faseAcesa;
  a.silenciado = s.alarme && vm.silenciado;
  a.tinta = a.cheio ? T.perigoTinta : T.tinta;
  a.tinta2 = a.cheio ? T.perigoTinta : T.tinta2;
  return a;
}

/* The lower card's situation, with the plan's rule: out of the range is Acima
 * or Abaixo, within a tenth of the range of either end is No limite. A reading
 * inside the range with the slot in alarm means another channel (humidity,
 * pressure) is the one out of its limits. */
enum class Sit : uint8_t { NA_FAIXA, NO_LIMITE, ACIMA, ABAIXO, EM_ALARME, ERRO };

Sit situacaoDe( const Sensor &s ) {
  if ( s.erro ) return Sit::ERRO;
  if ( s.temp > s.hi ) return Sit::ACIMA;
  if ( s.temp < s.lo ) return Sit::ABAIXO;
  if ( s.alarme ) return Sit::EM_ALARME;
  const int16_t margem = ( s.hi - s.lo ) / 10;
  if ( s.hi > s.lo && ( s.temp < s.lo + margem || s.temp > s.hi - margem ) ) return Sit::NO_LIMITE;
  return Sit::NA_FAIXA;
}

const char *textoDe( Sit s ) {
  switch ( s ) {
    case Sit::NA_FAIXA: return UA_TXT( "In range" );
    case Sit::NO_LIMITE: return UA_TXT( "At the limit" );
    case Sit::ACIMA: return UA_TXT( "Above" );
    case Sit::ABAIXO: return UA_TXT( "Below" );
    case Sit::EM_ALARME: return UA_TXT( "In alarm" );
    case Sit::ERRO: return UA_TXT( "Sensor error" );
  }
  return "";
}

Tom tomDe( Sit s ) {
  switch ( s ) {
    case Sit::NA_FAIXA: return Tom::POSITIVO;
    case Sit::NO_LIMITE: return Tom::ALERTA;
    default: return Tom::PERIGO;
  }
}

uint16_t corDe( const Tema &T, Sit s ) {
  switch ( s ) {
    case Sit::NA_FAIXA: return T.positivo;
    case Sit::NO_LIMITE: return T.alerta;
    default: return T.perigo;
  }
}

/* "4,6", "-18,5"; "--,-" for a value the sensor or the day does not have; the separator is the language's (n1( ) and the placeholder both ask the kit: "4.6" and "--.-" in English).
 * buf >= 16 bytes. */
const char *dec( int16_t v, bool semValor, char *buf ) {
  if ( semValor || v == VmPainel::SEM_VALOR ) {
    snprintf( buf, 16, "--%s-", separadorDecimal( ) );
    return buf;
  }
  return n1( v, buf );
}

/* Whole percent from tenths: "58", or "--". buf >= 16 bytes. */
const char *pct( int16_t v, char *buf ) {
  if ( v == VmPainel::SEM_VALOR ) {
    memcpy( buf, "--", 3 );
    return buf;
  }
  snprintf( buf, 16, "%d", (int)rdiv( v, 10 ) );
  return buf;
}

/* Ink width of a number and its unit as numero( ) draws them. */
int16_t larguraBloco( const char *valor, const char *un, Fonte f ) {
  return larguraTinta( valor, f ) + ESP1 + larguraTinta( un, tipo::botao );
}

/* What a card shows beside the temperature in normal mode: humidity for a
 * DHT22, pressure for the BMP280 and the BME280. Nothing for the DS18B20, nor
 * while the sensor has no reading. */
struct Segundo {
  bool tem;
  IconeNome icone;
  const char *un;
  char valor[16];
};

// A limit of the alarm's range, in tenths, without a trailing ",0": the range
// line has to leave the big number its size (2026-10-15: "faixa 15,0 a 30,0 °C"
// in 9 pt pushed a BME280 card's 22,4 from DISP32 down a size; "faixa 15 a
// 30 °C" does not). A limit with tenths keeps them.
char *limite( int32_t d, char *buf ) {
  if ( d % 10 ) return n1( d, buf );
  snprintf( buf, 16, "%ld", (long)( d / 10 ) );
  return buf;
}

Segundo segundoDe( const Sensor &s ) {
  Segundo g{ };
  g.icone = IconeNome::NENHUM;
  if ( s.erro ) return g;
  if ( s.tipo == Tipo::DHT22 ) {
    g.tem = true;
    g.icone = IconeNome::DROPLETS;
    g.un = UA_TXT( "%RH" );
    pct( s.umid, g.valor );
  } else if ( s.tipo == Tipo::BMP280 || s.tipo == Tipo::BME280 ) {
    g.tem = true;
    g.icone = IconeNome::GAUGE;
    g.un = "hPa";
    // 0 hPa is no reading (the glue sends 0 for a pressure that is not a number).
    if ( s.pres ) snprintf( g.valor, sizeof g.valor, "%u", (unsigned)s.pres );
    else memcpy( g.valor, "--", 3 );
  }
  return g;
}

/* A card whose slot has no sensor says so, once, in its middle. */
void desenharVazio( Pintor &p, const Caixa16 &c ) {
  const Tema &T = p.t( );
  const int16_t y = c.y + rd2( c.h - ( H_TEXTO + ESP2 + H_TEXTO ) );
  const int16_t x = c.x + rd2( c.w );
  textoCentrado( p, UA_TXT( "No active sensor" ), x, y, H_TEXTO, tipo::subtitulo, T.tinta, 'c' );
  textoCentrado( p, UA_TXT( "Activate a sensor on the web." ), x, y + H_TEXTO + ESP2, H_TEXTO, tipo::apoio, T.tinta2, 'c' );
}

/* min/max mode: a row per extreme, labelled min and max ("mín" and "máx" in pt-BR), the temperature and, for a
 * sensor that has it, the humidity, in columns. The fonts are tried from the
 * biggest and the first that fits between the card's inner edges wins. */
void desenharLinhasMinMax( Pintor &p, const Sensor &s, int16_t xEsq, int16_t xDir, int16_t base1, int16_t base2,
                           const Fonte *fontes, int nFontes, const Aspecto &a ) {
  char tmin[16], tmax[16], umin[16], umax[16];
  dec( s.tMin, false, tmin );
  dec( s.tMax, false, tmax );
  pct( s.uMin, umin );
  pct( s.uMax, umax );
  // The BME280 keeps its humidity for this mode; the DHT22 shows it always.
  const bool umid = s.tipo == Tipo::DHT22 || s.tipo == Tipo::BME280;
  const char *rot[2] = { UA_TXT( "min" ), UA_TXT( "max" ) };
  const char *unUmid = UA_TXT( "%RH" );
  const int16_t wMin = larguraTinta( rot[0], tipo::apoio ), wMax = larguraTinta( rot[1], tipo::apoio );
  const int16_t wRot = wMin > wMax ? wMin : wMax;
  Fonte f = fontes[nFontes - 1];
  int16_t wT = 0, wU = 0;
  for ( int i = 0; i < nFontes; i++ ) {
    const int16_t t1 = larguraBloco( tmin, "°C", fontes[i] ), t2 = larguraBloco( tmax, "°C", fontes[i] );
    const int16_t u1 = larguraBloco( umin, unUmid, fontes[i] ), u2 = larguraBloco( umax, unUmid, fontes[i] );
    wT = t1 > t2 ? t1 : t2;
    wU = u1 > u2 ? u1 : u2;
    if ( wRot + ESP3 + wT + ( umid ? ESP4 + wU : 0 ) <= xDir - xEsq ) {
      f = fontes[i];
      break;
    }
  }
  // Right edges of the columns, packed from the left so that the decimal
  // points of both rows line up.
  const int16_t xT = xEsq + wRot + ESP3 + wT;
  const int16_t xU = xT + ESP4 + wU;
  const int16_t bases[2] = { base1, base2 };
  const char *tv[2] = { tmin, tmax };
  const char *uv[2] = { umin, umax };
  for ( int i = 0; i < 2; i++ ) {
    p.texto( rot[i], xEsq, bases[i], tipo::apoio, a.tinta2, 'l' );
    numero( p, xT, bases[i], tv[i], "°C", f, a.tinta, 'r' );
    if ( umid ) numero( p, xU, bases[i], uv[i], unUmid, f, a.tinta, 'r' );
  }
}

void desenharBotaoGrafico( Pintor &p, const Caixa16 &c, const Aspecto &a ) {
  const Tema &T = p.t( );
  // The one accent of the card; in the lit phase it flips with the rest.
  const uint16_t fundo = a.cheio ? T.perigoTinta : T.acento, cor = a.cheio ? T.perigo : T.acentoTinta;
  UA_CONTROLE( c.x, c.y, c.w, c.h, true, "grafico" );
  p.fillRoundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, fundo );
  p.icone( IconeNome::CHART_LINE, ICONE_G, c.x + rd2( c.w - ICONE_G ), c.y + ESP1, cor );
  textoCentrado( p, UA_TXT( "Chart" ), c.x + rd2( c.w ), c.y + ICONE_G + ESP1, H_TEXTO, tipo::rotulo, cor, 'c' );
}

/* ------------------------------------------------------------------ */
/* The upper card                                                       */
/* ------------------------------------------------------------------ */

void desenharCartaoCima( Pintor &p, const VmPainel &vm, const LayoutPainel &l ) {
  const Tema &T = p.t( );
  const Sensor &s = vm.cima;
  const Aspecto a = aspectoDe( T, vm, s );
  superficie( p, l.cima.x, l.cima.y, l.cima.w, l.cima.h, a.cheio, T.perigo, a.silenciado, T.perigo );
  if ( s.tipo == Tipo::NENHUM ) {
    desenharVazio( p, l.cima );
    return;
  }

  // The name gives way to the pin icon and, in min/max mode, to the button.
  const int16_t yNome = l.cima.y + ESP1;
  int16_t direita = l.graficoCima.w ? l.graficoCima.x - ESP3 : X_INT_DIR;
  if ( vm.cimaFixada ) direita -= ICONE_P + ESP1;
  char nome[40];
  cortar( nome, sizeof nome, s.nome, tipo::rotulo, direita - X_INT );
  const Tinta tn = textoCentrado( p, nome, X_INT, yNome, H_NOME_CIMA, tipo::rotulo, a.tinta2, 'l' );
  if ( vm.cimaFixada ) p.icone( IconeNome::PIN, ICONE_P, tn.x1 + ESP1, yNome, a.tinta2 );

  if ( s.minmax ) {
    static const Fonte fontes[] = { Fonte::DISP9 };
    desenharLinhasMinMax( p, s, X_INT, l.graficoCima.x - ESP3, BASE_CIMA - ESP4, BASE_CIMA, fontes, 1, a );
    desenharBotaoGrafico( p, l.graficoCima, a );
    return;
  }

  char t[16];
  dec( s.temp, s.erro, t );
  const Segundo g = segundoDe( s );
  // The value turns danger while the slot is in alarm or has no reading; the
  // rest of the time it is plain ink: the situation is the lower card's.
  const uint16_t cor = a.cheio ? T.perigoTinta : ( s.alarme || s.erro ) ? T.perigo : T.tinta;

  // Two numbers side by side with their icons: the biggest font in which both
  // fit (a negative temperature beside a pressure does not at DISP24).
  const Fonte fontes[] = { Fonte::DISP24, Fonte::DISP18 };
  Fonte f = Fonte::DISP18;
  int16_t wS = 0;
  for ( Fonte c : fontes ) {
    const int16_t wT = ICONE_M + ESP2 + larguraBloco( t, "°C", c );
    wS = g.tem ? ICONE_M + ESP2 + larguraBloco( g.valor, g.un, c ) : 0;
    if ( wT + ( g.tem ? ESP4 + wS : 0 ) <= X_INT_DIR - X_INT ) {
      f = c;
      break;
    }
  }
  // Icons centred on the number's capital height.
  const int16_t yIcone = BASE_CIMA - rd2( alturaCaixaAlta( f ) ) - ICONE_M / 2;
  p.icone( IconeNome::THERMOMETER, ICONE_M, X_INT, yIcone, a.tinta2 );
  numero( p, X_INT + ICONE_M + ESP2, BASE_CIMA, t, "°C", f, cor );
  if ( g.tem ) {
    const int16_t x = X_INT_DIR - wS;
    p.icone( g.icone, ICONE_M, x, yIcone, a.tinta2 );
    numero( p, x + ICONE_M + ESP2, BASE_CIMA, g.valor, g.un, f, cor );
  }
}

/* ------------------------------------------------------------------ */
/* The lower card                                                       */
/* ------------------------------------------------------------------ */

void desenharCartaoBaixo( Pintor &p, const VmPainel &vm, const LayoutPainel &l ) {
  const Tema &T = p.t( );
  const Sensor &s = vm.baixo;
  const Aspecto a = aspectoDe( T, vm, s );
  superficie( p, l.baixo.x, l.baixo.y, l.baixo.w, l.baixo.h, a.cheio, T.perigo, a.silenciado, T.perigo );
  // What the notice covers is not painted at all: the plan paints it and lets
  // the notice hide it, which leaves half a line of text at the notice's edge.
  const bool corpo = !vm.webOcupada;
  if ( s.tipo == Tipo::NENHUM ) {
    if ( corpo ) desenharVazio( p, l.baixo );
    return;
  }

  const int16_t yNome = l.baixo.y + ESP2;
  // The baseline the name and the situation's badge share: the badge's text is
  // centred in its box, so the box starts where the name's baseline says.
  const int16_t baseNome = yNome + rd2( H_NOME_BAIXO + alturaCaixaAlta( tipo::subtitulo ) );
  char nome[40];

  if ( s.minmax ) {
    // As the upper card: the name, the two extremes and the button; the live
    // value and its badge are back when the card leaves this mode.
    cortar( nome, sizeof nome, s.nome, tipo::subtitulo, l.graficoBaixo.x - ESP3 - X_INT );
    textoCentrado( p, nome, X_INT, yNome, H_NOME_BAIXO, tipo::subtitulo, a.tinta, 'l' );
    if ( !corpo ) return;
    static const Fonte fontes[] = { Fonte::DISP12, Fonte::DISP9 };
    desenharLinhasMinMax( p, s, X_INT, l.graficoBaixo.x - ESP3, BASE_NUMERO - ESP5, BASE_NUMERO, fontes, 2, a );
    desenharBotaoGrafico( p, l.graficoBaixo, a );
    return;
  }

  // Name and badge on one row, the badge against the right edge.
  const Sit sit = situacaoDe( s );
  const char *txt = textoDe( sit );
  const int16_t yBadge = baseNome - rd2( H_SELO + alturaCaixaAlta( tipo::rotulo ) );
  int16_t wBadge;
  if ( a.cheio ) {
    // The plan's inverted badge: the card is the danger colour, the badge is
    // the card's ink with the danger colour's text.
    wBadge = larguraTinta( txt, tipo::rotulo ) + 2 * ESP2;
    p.fillRoundRect( X_INT_DIR - wBadge, yBadge, wBadge, H_SELO, RAIO_TOTAL, T.perigoTinta );
    textoCentrado( p, txt, X_INT_DIR - rd2( wBadge ), yBadge, H_SELO, tipo::rotulo, T.perigo, 'c' );
  } else {
    wBadge = larguraSelo( txt );
    selo( p, X_INT_DIR - wBadge, yBadge, H_SELO, txt, tomDe( sit ) );
  }
  cortar( nome, sizeof nome, s.nome, tipo::subtitulo, X_INT_DIR - wBadge - ESP3 - X_INT );
  textoCentrado( p, nome, X_INT, yNome, H_NOME_BAIXO, tipo::subtitulo, a.tinta, 'l' );
  if ( !corpo ) return;

  // The right column, right-aligned, its last line on the number's baseline. At
  // 9 pt it is wide, and the big number takes what is left (the next block), so it
  // carries only what the badge needs: the sensor's second value by its unit alone
  // ("58 %UR", "1013 hPa") when it has one, and the alarm's range without the word
  // ("2 a 8 °C"; the badge above it says "Na faixa", "Acima" or "Abaixo"). With the
  // day's extremes in it too, a BME280 card's 22,4 and a 125,0 lost a size
  // (2026-10-15); the extremes are a tap away, in the card's min/max mode, and in
  // the graph.
  char b1[16], b2[16];
  char linhaSeg[40], linhaFaixa[40];
  const Segundo g = segundoDe( s );
  int nLinhas = 0;
  const char *linhas[2];
  if ( g.tem ) {
    snprintf( linhaSeg, sizeof linhaSeg, "%s %s", g.valor, g.un );
    linhas[nLinhas++] = linhaSeg;
  }
  snprintf( linhaFaixa, sizeof linhaFaixa, "%s %s %s °C", limite( s.lo, b1 ), UA_TXT( "to" ), limite( s.hi, b2 ) );
  linhas[nLinhas++] = linhaFaixa;
  int16_t wColuna = 0;
  for ( int i = 0; i < nLinhas; i++ ) {
    const int16_t w = larguraTinta( linhas[i], tipo::apoio );
    if ( w > wColuna ) wColuna = w;
  }
  for ( int i = 0; i < nLinhas; i++ )
    p.texto( linhas[i], X_INT_DIR, BASE_NUMERO - ( nLinhas - 1 - i ) * PASSO_LINHA, tipo::apoio, a.tinta2, 'r' );

  // The big number takes the biggest font that leaves 12 px before the column.
  char t[16];
  dec( s.temp, s.erro, t );
  const Fonte fontes[] = { Fonte::DISP32, Fonte::DISP24, Fonte::DISP18 };
  Fonte f = Fonte::DISP18;
  for ( Fonte c : fontes ) {
    if ( X_INT + larguraBloco( t, "°C", c ) + ESP3 <= X_INT_DIR - wColuna ) {
      f = c;
      break;
    }
  }
  numero( p, X_INT, BASE_NUMERO, t, "°C", f, a.cheio ? T.perigoTinta : corDe( T, sit ) );
}

/* ------------------------------------------------------------------ */
/* The bottom row                                                       */
/* ------------------------------------------------------------------ */

void desenharNav( Pintor &p, const VmPainel &vm, const LayoutPainel &l ) {
  const Tema &T = p.t( );
  // Silenced, nothing flashes: a slot in alarm keeps its dot, the page button
  // its danger border.
  const bool acesa = vm.faseAcesa && !vm.silenciado;
  for ( int i = 0; i < NAV_N; i++ ) {
    const VmPainel::Nav &n = vm.nav[i];
    const Caixa16 &c = l.nav[i];
    if ( n.tipo == NavTipo::NADA ) continue;
    UA_CONTROLE( c.x, c.y, c.w, c.h, !vm.webOcupada, "nav" );
    const bool cheio = n.alarme && acesa;
    char rot[16];
    if ( n.tipo == NavTipo::SLOT ) {
      uint16_t fundo = n.selecionado ? T.acento : T.superficie2, cor = n.selecionado ? T.acentoTinta : T.tinta;
      bool borda = !n.selecionado;
      if ( cheio ) {
        fundo = T.perigo;
        cor = T.perigoTinta;
        borda = false;
      }
      p.fillRoundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, fundo );
      if ( borda ) p.roundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, T.linha );
      snprintf( rot, sizeof rot, "%s%u", UA_TXT( "S" ), (unsigned)n.slot );
      textoCentrado( p, rot, c.x + rd2( c.w ), c.y, c.h, tipo::botao, cor, 'c' );
      // The dark phase of an alarm: a dot at the corner, over any state.
      if ( n.alarme && !cheio ) p.fillCircle( c.x + c.w - ESP2, c.y + ESP2, RAIO_PONTO, T.perigo );
    } else if ( n.tipo == NavTipo::CFG ) {
      p.fillRoundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, T.superficie2 );
      p.roundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, T.linha );
      p.icone( IconeNome::SETTINGS, ICONE_M, c.x + rd2( c.w - ICONE_M ), c.y + rd2( c.h - ICONE_M ), T.tinta );
    } else {
      // Only an outline, so that the page button is not taken for a sensor.
      uint16_t cor = n.alarme ? T.perigo : T.tinta2;
      if ( cheio ) {
        p.fillRoundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, T.perigo );
        cor = T.perigoTinta;
      } else {
        p.roundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, n.alarme ? T.perigo : T.linhaForte );
      }
      snprintf( rot, sizeof rot, "%u/%u", (unsigned)vm.pagina, (unsigned)vm.paginas );
      textoCentrado( p, rot, c.x + rd2( c.w ), c.y, c.h, tipo::botao, cor, 'c' );
    }
  }
}

/* ------------------------------------------------------------------ */
/* The web-busy notice                                                  */
/* ------------------------------------------------------------------ */

void desenharAvisoWeb( Pintor &p, const VmPainel &vm, const LayoutPainel &l ) {
  const Tema &T = p.t( );
  const Caixa16 &c = l.aviso;
  // A floating card: the strong line stands for the shadow the glass cannot have.
  p.fillRoundRect( c.x, c.y, c.w, c.h, RAIO_CARTAO, T.superficie );
  p.roundRect( c.x, c.y, c.w, c.h, RAIO_CARTAO, T.linhaForte );
  const int16_t xTexto = c.x + PAD + ICONE_M + ESP2;
  p.icone( IconeNome::USER, ICONE_M, c.x + PAD, c.y + rd2( c.h - ICONE_M ), T.tinta2 );
  // The account's name gives way so that the whole sentence fits.
  char frase[80];
  painel::nomeComFim( frase, sizeof frase, vm.contaWeb, UA_TXT( "is saving through the web." ), tipo::corpo,
                      c.x + c.w - PAD - xTexto );
  textoCentrado( p, frase, xTexto, c.y + ESP2, H_TEXTO, tipo::corpo, T.tinta, 'l' );
  textoCentrado( p, UA_TXT( "Wait before touching the screen." ), xTexto, c.y + ESP2 + H_TEXTO + ESP1, H_TEXTO, tipo::apoio, T.tinta2,
                 'l' );
}

}  // namespace

void desenharPainel( Pintor &p, const VmPainel &vm ) {
  const LayoutPainel l = layoutPainel( vm );
  desenharStatus( p, vm.status );
  if ( l.vazio.w ) {
    superficie( p, l.vazio.x, l.vazio.y, l.vazio.w, l.vazio.h );
    desenharVazio( p, l.vazio );
  } else {
    desenharCartaoCima( p, vm, l );
    desenharCartaoBaixo( p, vm, l );
  }
  desenharNav( p, vm, l );
  if ( vm.webOcupada ) desenharAvisoWeb( p, vm, l );
}

int alvosPainel( const VmPainel &vm, Alvo *out ) {
  // While the web saves, the old handler beeps and ignores every touch (the web-busy refusal in handleTouch( ), ahead of this
  // glue). The screen is drawn as it is, so every control on it is still listed, inert: a touch on one does nothing, and the
  // host check that every drawn control has a target holds here too.
  const bool viva = !vm.webOcupada;
  const LayoutPainel l = layoutPainel( vm );
  int n = 0;
  // A card with no sensor has nothing to open; the buttons come after their
  // card, so that alvoEm( ) answers the button where the two overlap.
  if ( vm.cima.tipo != Tipo::NENHUM ) {
    n = addAlvoSe( out, n, viva, l.cima, PNL_CARTAO_CIMA );
    if ( l.graficoCima.w ) n = addAlvoSe( out, n, viva, l.graficoCima, PNL_GRAFICO_CIMA );
  }
  if ( vm.baixo.tipo != Tipo::NENHUM ) {
    n = addAlvoSe( out, n, viva, l.baixo, PNL_CARTAO_BAIXO );
    if ( l.graficoBaixo.w ) n = addAlvoSe( out, n, viva, l.graficoBaixo, PNL_GRAFICO_BAIXO );
  }
  for ( int i = 0; i < NAV_N; i++ )
    if ( l.nav[i].w ) n = addAlvoSe( out, n, viva, l.nav[i], PNL_NAV, (int16_t)i );
  return n;
}

}  // namespace tela
}  // namespace ua
