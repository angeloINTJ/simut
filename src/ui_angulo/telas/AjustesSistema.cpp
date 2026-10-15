// The PIN policy and the clock. See AjustesSistema.h.
#include "AjustesSistema.h"

#include <stdio.h>

#include "AjustesComum.h"

namespace ua {
namespace tela {

namespace {

using namespace ajustes;

/* ------------------------------------------------------------------ */
/* Segurança do PIN                                                     */
/* ------------------------------------------------------------------ */

constexpr int16_t X_CARTAO = ESP2, W_CARTAO = L_TELA - 2 * ESP2;
constexpr int16_t X_DIR = X_CARTAO + W_CARTAO - PAD;      // where a row's controls end: 12 px inside the card
constexpr int16_t H_SEG = 28;                             // a segmented control
constexpr int16_t H_LINHA_PASSO = L_BOTAO + 2 * ESP1;     // the stepper's row: 40 px buttons with 4 px clear above and below
constexpr int16_t H_LINHA_SEG = H_SEG + 2 * ESP1;         // a segmented control's row, likewise
constexpr int16_t W_VALOR = 40;                           // the room the stepper's number has between its buttons
// The keypad control takes the room its row's label leaves: segments of 72 px,
// which is 5 px of air each side of "18 cartões" in the regular face (62 px of
// ink). The alphabet control's two segments are wider than their labels need, so
// that both controls end at the same edge.
constexpr int16_t W_SEG_TECLADO = 72, W_SEG_CARACTERES = 100;

const char *const kTeclado09[3] = { "Ordenado", "6 cartões", "4 cartões" };
const char *const kTecladoAz[3] = { "Ordenado", "18 cartões", "12 cartões" };
const char *const kCaracteres[2] = { "0-9", "0-9 e A-Z" };

struct LayoutPolitica {
  Caixa16 linha[3];                 // the three rows' cards
  Caixa16 menos, mais;              // the stepper's buttons
  int16_t xValor;                   // the centre of the stepper's number
  bool menosAtivo, maisAtivo;
  Caixa16 teclado[3], caracteres[2];   // the segments, stretched to the whole row for the finger
  Caixa16 grupoTeclado, grupoCaracteres;   // the controls as segmentadoLeve( ) draws them
  int16_t yAjuda;
  CaixasRodape rodape;
};

// The kit's segmentado( ) splits the width evenly with 4 px between (floor), and
// so does segmentadoLeve( ): the segments' boxes, from the same arithmetic.
void segmentos( const Caixa16 &grupo, int n, const Caixa16 &linha, Caixa16 *out ) {
  const int16_t sw = ( grupo.w - ( n - 1 ) * ESP1 ) / n;
  for ( int i = 0; i < n; i++ ) out[i] = Caixa16{ (int16_t)( grupo.x + i * ( sw + ESP1 ) ), linha.y, sw, linha.h };
}

LayoutPolitica layoutPolitica( const VmPoliticaPin &vm ) {
  LayoutPolitica l{ };
  const int16_t y1 = Y0 + H_LINHA_PASSO + ESP2, y2 = y1 + H_LINHA_SEG + ESP2;
  l.linha[0] = Caixa16{ X_CARTAO, Y0, W_CARTAO, H_LINHA_PASSO };
  l.linha[1] = Caixa16{ X_CARTAO, y1, W_CARTAO, H_LINHA_SEG };
  l.linha[2] = Caixa16{ X_CARTAO, y2, W_CARTAO, H_LINHA_SEG };
  l.mais = Caixa16{ (int16_t)( X_DIR - L_BOTAO ), (int16_t)( Y0 + ESP1 ), L_BOTAO, L_BOTAO };
  l.menos = Caixa16{ (int16_t)( l.mais.x - ESP1 - W_VALOR - ESP1 - L_BOTAO ), l.mais.y, L_BOTAO, L_BOTAO };
  l.xValor = l.menos.x + L_BOTAO + ESP1 + rd2( W_VALOR );
  l.menosAtivo = vm.minimo > vm.piso;
  l.maisAtivo = vm.minimo < vm.teto;
  const int16_t wT = 3 * W_SEG_TECLADO + 2 * ESP1, wC = 2 * W_SEG_CARACTERES + ESP1;
  l.grupoTeclado = Caixa16{ (int16_t)( X_DIR - wT ), (int16_t)( y1 + ESP1 ), wT, H_SEG };
  l.grupoCaracteres = Caixa16{ (int16_t)( X_DIR - wC ), (int16_t)( y2 + ESP1 ), wC, H_SEG };
  segmentos( l.grupoTeclado, 3, l.linha[1], l.teclado );
  segmentos( l.grupoCaracteres, 2, l.linha[2], l.caracteres );
  l.yAjuda = y2 + H_LINHA_SEG + ESP1;
  l.rodape = caixasRodape( "Salvar", "Cancelar" );
  return l;
}

// What the keypad and the alphabet allow, in a sentence: the ceiling is what the
// PIN's length can be set up to, and how many cards the keypad has says what the
// segment of the same name does.
void textoAjuda( const VmPoliticaPin &vm, char *dst, int cap ) {
  const char *unidade = vm.alfabeto ? "caracteres" : "dígitos";
  if ( vm.teclado == 1 ) {
    snprintf( dst, (size_t)cap, "No teclado ordenado, o PIN vai até %u %s.", (unsigned)vm.teto, unidade );
  } else {
    // The cards each keypad has (PinKeypad.h, GRIDS), by alphabet and keypad (2, 3).
    static const uint8_t kCartoes[2][2] = { { 6, 4 }, { 18, 12 } };
    const int cartoes = kCartoes[vm.alfabeto ? 1 : 0][vm.teclado == 3 ? 1 : 0];
    snprintf( dst, (size_t)cap, "Com %d cartões, o PIN vai até %u %s.", cartoes, (unsigned)vm.teto, unidade );
  }
}

// A row's label, in the quiet type: the bold small face would run 54 px for
// "Teclado" and leave 2 px before its control; the regular one runs 48.
void rotuloDaLinha( Pintor &p, const Caixa16 &c, const char *texto ) {
  textoCentrado( p, texto, c.x + PAD, c.y, c.h, tipo::apoio, p.t( ).tinta2, 'l' );
}

/* ------------------------------------------------------------------ */
/* Data e hora                                                          */
/* ------------------------------------------------------------------ */

// Day, month, year, hour, minute. The year's column is wider for its four digits.
constexpr int16_t W_COL = 44, W_ANO = 64;
constexpr int16_t LARGURAS[RELOG_COLUNAS] = { W_COL, W_COL, W_ANO, W_COL, W_COL };
// The gap after each column but the last: the "/" and ":" stand in the first,
// second and fourth; the third is the break between the date and the time.
constexpr int16_t G_SEP = ESP3, G_DATA_HORA = ESP5;
constexpr int16_t GAPS[RELOG_COLUNAS - 1] = { G_SEP, G_SEP, G_DATA_HORA, G_SEP };
// The rows, top down: the arrows that add, the value's band, the arrows that subtract.
constexpr int16_t Y_CIMA = Y0 + ESP1, H_VALOR = ESP6;
constexpr int16_t Y_VALOR = Y_CIMA + L_BOTAO + ESP1, Y_BAIXO = Y_VALOR + H_VALOR + ESP1;
// The boot's hint, under the arrows.
constexpr int16_t Y_DICA = Y_BAIXO + L_BOTAO + ESP2;

struct LayoutRelogio {
  Caixa16 cima[RELOG_COLUNAS], baixo[RELOG_COLUNAS];
  int16_t xSep[3];                  // the centres of the "/", "/" and ":"
  CaixasRodape rodape;
};

LayoutRelogio layoutRelogio( const VmRelogio &vm ) {
  LayoutRelogio l{ };
  int16_t total = 0;
  for ( int i = 0; i < RELOG_COLUNAS; i++ ) total += LARGURAS[i] + ( i < RELOG_COLUNAS - 1 ? GAPS[i] : 0 );
  int16_t x = rd2( L_TELA - total );
  int s = 0;
  for ( int i = 0; i < RELOG_COLUNAS; i++ ) {
    l.cima[i] = Caixa16{ x, Y_CIMA, LARGURAS[i], L_BOTAO };
    l.baixo[i] = Caixa16{ x, Y_BAIXO, LARGURAS[i], L_BOTAO };
    if ( i < RELOG_COLUNAS - 1 && i != 2 ) l.xSep[s++] = x + LARGURAS[i] + rd2( GAPS[i] );
    x += LARGURAS[i] + ( i < RELOG_COLUNAS - 1 ? GAPS[i] : 0 );
  }
  l.rodape = caixasRodape( "Salvar hora", vm.boot ? "Pular" : "Voltar" );
  return l;
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Segurança do PIN                                                     */
/* ------------------------------------------------------------------ */

void desenharPoliticaPin( Pintor &p, const VmPoliticaPin &vm ) {
  const Tema &T = p.t( );
  const LayoutPolitica l = layoutPolitica( vm );
  barraTituloVoltar( p, "Segurança do PIN" );
  for ( int i = 0; i < 3; i++ ) superficie( p, l.linha[i].x, l.linha[i].y, l.linha[i].w, l.linha[i].h );

  rotuloDaLinha( p, l.linha[0], "Tamanho mínimo" );
  botaoIcone( p, l.menos, IconeNome::MINUS, l.menosAtivo );
  char minimo[8];
  snprintf( minimo, sizeof minimo, "%u", (unsigned)vm.minimo );
  textoCentrado( p, minimo, l.xValor, l.linha[0].y, l.linha[0].h, tipo::titulo, T.tinta, 'c' );
  botaoIcone( p, l.mais, IconeNome::PLUS, l.maisAtivo );

  // The keypad's segments: 1 ordered, 2 six cards, 3 four cards (the old values).
  // The label of 2 and 3 follows the alphabet, which has more cards to the keypad.
  rotuloDaLinha( p, l.linha[1], "Teclado" );
  segmentadoLeve( p, l.grupoTeclado.x, l.grupoTeclado.y, l.grupoTeclado.w, l.grupoTeclado.h, vm.alfabeto ? kTecladoAz : kTeclado09, 3,
                  vm.teclado >= 1 && vm.teclado <= 3 ? vm.teclado - 1 : -1 );
  rotuloDaLinha( p, l.linha[2], "Caracteres" );
  segmentadoLeve( p, l.grupoCaracteres.x, l.grupoCaracteres.y, l.grupoCaracteres.w, l.grupoCaracteres.h, kCaracteres, 2,
                  vm.alfabeto ? 1 : 0 );

  char frase[64];
  textoAjuda( vm, frase, sizeof frase );
  ajuda( p, ESP2, l.yAjuda, ESP4, frase );
  desenharRodape( p, l.rodape, "Salvar", Botao::PRIMARIO, false, "Cancelar" );
}

int alvosPoliticaPin( const VmPoliticaPin &vm, Alvo *out ) {
  const LayoutPolitica l = layoutPolitica( vm );
  int n = addAlvo( out, 0, caixaVoltar( ), POLPIN_VOLTAR );
  if ( l.menosAtivo ) n = addAlvo( out, n, l.menos, POLPIN_MENOS );
  if ( l.maisAtivo ) n = addAlvo( out, n, l.mais, POLPIN_MAIS );
  for ( int i = 0; i < 3; i++ ) n = addAlvo( out, n, l.teclado[i], POLPIN_TECLADO, i + 1 );
  for ( int i = 0; i < 2; i++ ) n = addAlvo( out, n, l.caracteres[i], POLPIN_CARACTERES, i );
  n = addAlvo( out, n, l.rodape.secundaria, POLPIN_CANCELAR );
  return addAlvo( out, n, l.rodape.primaria, POLPIN_SALVAR );
}

/* ------------------------------------------------------------------ */
/* Data e hora                                                          */
/* ------------------------------------------------------------------ */

void desenharRelogio( Pintor &p, const VmRelogio &vm ) {
  const Tema &T = p.t( );
  const LayoutRelogio l = layoutRelogio( vm );
  // No "Voltar" in the bar: the footer's left button is it (or "Pular" at boot),
  // as the old screen had it.
  barraTitulo( p, "Data e hora", nullptr, 0 );
  char v[RELOG_COLUNAS][8];
  snprintf( v[0], sizeof v[0], "%02u", (unsigned)vm.dia );
  snprintf( v[1], sizeof v[1], "%02u", (unsigned)vm.mes );
  snprintf( v[2], sizeof v[2], "%04u", (unsigned)vm.ano );
  snprintf( v[3], sizeof v[3], "%02u", (unsigned)vm.hora );
  snprintf( v[4], sizeof v[4], "%02u", (unsigned)vm.minuto );
  for ( int i = 0; i < RELOG_COLUNAS; i++ ) {
    botaoIcone( p, l.cima[i], IconeNome::CHEVRON_UP, true );
    botaoIcone( p, l.baixo[i], IconeNome::CHEVRON_DOWN, true );
    textoCentrado( p, v[i], l.cima[i].x + rd2( l.cima[i].w ), Y_VALOR, H_VALOR, tipo::dadoGrande, T.tinta, 'c' );
  }
  static const char *const separador[3] = { "/", "/", ":" };
  for ( int s = 0; s < 3; s++ ) textoCentrado( p, separador[s], l.xSep[s], Y_VALOR, H_VALOR, tipo::dadoGrande, T.tinta2, 'c' );
  if ( vm.boot ) textoCentrado( p, "Sem rede: os registros usam esta hora.", X_MEIO, Y_DICA, ESP4, tipo::apoio, T.alerta, 'c' );
  desenharRodape( p, l.rodape, "Salvar hora", Botao::PRIMARIO, false, vm.boot ? "Pular" : "Voltar" );
}

int alvosRelogio( const VmRelogio &vm, Alvo *out ) {
  const LayoutRelogio l = layoutRelogio( vm );
  int n = 0;
  for ( int i = 0; i < RELOG_COLUNAS; i++ ) {
    n = addAlvo( out, n, l.cima[i], RELOG_MAIS, i );
    n = addAlvo( out, n, l.baixo[i], RELOG_MENOS, i );
  }
  n = addAlvo( out, n, l.rodape.secundaria, vm.boot ? RELOG_PULAR : RELOG_VOLTAR );
  return addAlvo( out, n, l.rodape.primaria, RELOG_SALVAR );
}

}  // namespace tela
}  // namespace ua
