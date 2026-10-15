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
constexpr int16_t X_ESQ = X_CARTAO + PAD;                 // where a row's label starts: 12 px inside the card
constexpr int16_t X_DIR = X_CARTAO + W_CARTAO - PAD;      // where a row's controls end: 12 px inside the card
constexpr int16_t H_SEG = 28;                             // a segmented control
constexpr int16_t H_LINHA_PASSO = L_BOTAO + 2 * ESP1;     // the stepper's row: 40 px buttons with 4 px clear above and below
constexpr int16_t H_LINHA_SEG = H_SEG + 2 * ESP1;         // a control beside its label: 4 px clear above and below, likewise
// The keypad's row is two lines, the label above its control: at 9 pt "Teclado" (62 px
// of ink) and three segments that hold "18 cartões" (78 px of ink, 4 px of air each
// side) take 336 px of the 280 the row has (2026-10-15), so the control takes the
// row's whole width.
constexpr int16_t Y_CONTROLE_TECLADO = ESP1 + ESP4 + ESP1;                    // under the label, 4 px clear
constexpr int16_t H_LINHA_TECLADO = Y_CONTROLE_TECLADO + H_SEG + ESP1;       // 56
constexpr int16_t W_VALOR = 40;                           // the room the stepper's number has between its buttons
// The keypad control's three segments split the row's 280 px (91, 91 and 90 px, 6 px
// of air each side of "18 cartões" in the regular face); the alphabet control sits
// under the last two, whose room is what "Caracteres" (85 px of ink) leaves.
constexpr int16_t W_TECLADO = X_DIR - X_ESQ;

const char *const kTeclado09[3] = { "Ordenado", "6 cartões", "4 cartões" };
const char *const kTecladoAz[3] = { "Ordenado", "18 cartões", "12 cartões" };
const char *const kCaracteres[2] = { "0-9", "0-9 e A-Z" };

struct LayoutPolitica {
  Caixa16 linha[3];                 // the three rows' cards
  Caixa16 menos, mais;              // the stepper's buttons
  int16_t xValor;                   // the centre of the stepper's number
  bool menosAtivo, maisAtivo;
  Caixa16 teclado[3], caracteres[2];   // the segments, stretched over the whole row below the label for the finger
  Caixa16 grupoTeclado, grupoCaracteres;   // the controls as segmentadoLeve( ) draws them
  CaixasRodape rodape;
};

// The segments' boxes: x and width from segmento( ), the same arithmetic that draws
// them, over the rows `y` and `h` (a finger's zone is taller than the control).
void segmentos( const Caixa16 &grupo, int n, int16_t y, int16_t h, Caixa16 *out ) {
  for ( int i = 0; i < n; i++ ) out[i] = segmento( grupo.x, y, grupo.w, h, n, i );
}

LayoutPolitica layoutPolitica( const VmPoliticaPin &vm ) {
  LayoutPolitica l{ };
  const int16_t y1 = Y0 + H_LINHA_PASSO + ESP2, y2 = y1 + H_LINHA_TECLADO + ESP2;
  l.linha[0] = Caixa16{ X_CARTAO, Y0, W_CARTAO, H_LINHA_PASSO };
  l.linha[1] = Caixa16{ X_CARTAO, y1, W_CARTAO, H_LINHA_TECLADO };
  l.linha[2] = Caixa16{ X_CARTAO, y2, W_CARTAO, H_LINHA_SEG };
  l.mais = Caixa16{ (int16_t)( X_DIR - L_BOTAO ), (int16_t)( Y0 + ESP1 ), L_BOTAO, L_BOTAO };
  l.menos = Caixa16{ (int16_t)( l.mais.x - ESP1 - W_VALOR - ESP1 - L_BOTAO ), l.mais.y, L_BOTAO, L_BOTAO };
  l.xValor = l.menos.x + L_BOTAO + ESP1 + rd2( W_VALOR );
  l.menosAtivo = vm.minimo > vm.piso;
  l.maisAtivo = vm.minimo < vm.teto;
  l.grupoTeclado = Caixa16{ X_ESQ, (int16_t)( y1 + Y_CONTROLE_TECLADO ), W_TECLADO, H_SEG };
  const Caixa16 sob = segmento( X_ESQ, 0, W_TECLADO, 0, 3, 1 );   // the keypad's second segment
  l.grupoCaracteres = Caixa16{ sob.x, (int16_t)( y2 + ESP1 ), (int16_t)( X_DIR - sob.x ), H_SEG };
  const int16_t yDedo = y1 + Y_CONTROLE_TECLADO - ESP1;
  segmentos( l.grupoTeclado, 3, yDedo, y1 + H_LINHA_TECLADO - yDedo, l.teclado );
  segmentos( l.grupoCaracteres, 2, y2, H_LINHA_SEG, l.caracteres );
  l.rodape = caixasRodape( "Salvar", "Cancelar" );
  return l;
}

// What the keypad and the alphabet allow, under the stepper's label: the range the
// minimum can take, which is also how long a PIN can be. It stands for the sentence
// that was under the rows ("No teclado ordenado, o PIN vai até 16 caracteres.": 378 px
// at 9 pt, on a glass that leaves 312, and no line of its own below the taller keypad
// row, 2026-10-15), which also named the keypad, and the segment that is lit does.
void textoFaixa( const VmPoliticaPin &vm, char *dst, int cap ) {
  snprintf( dst, (size_t)cap, "%u a %u %s", (unsigned)vm.piso, (unsigned)vm.teto, vm.alfabeto ? "caracteres" : "dígitos" );
}

// A row's label: the row's main text, in the body type.
void rotuloDaLinha( Pintor &p, int16_t y, int16_t h, const char *texto ) {
  textoCentrado( p, texto, X_ESQ, y, h, tipo::corpo, p.t( ).tinta, 'l' );
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

  // The stepper's row: the label, and under it the range the minimum can take.
  const Caixa16 &passo = l.linha[0];
  const int16_t yRotulo = passo.y + rd2( passo.h - ( PASSO_APOIO + ESP4 ) );
  rotuloDaLinha( p, yRotulo, ESP4, "Tamanho mínimo" );
  char faixa[24];
  textoFaixa( vm, faixa, sizeof faixa );
  ajuda( p, X_ESQ, yRotulo + PASSO_APOIO, ESP4, faixa );
  botaoIcone( p, l.menos, IconeNome::MINUS, l.menosAtivo );
  char minimo[8];
  snprintf( minimo, sizeof minimo, "%u", (unsigned)vm.minimo );
  textoCentrado( p, minimo, l.xValor, passo.y, passo.h, tipo::titulo, T.tinta, 'c' );
  botaoIcone( p, l.mais, IconeNome::PLUS, l.maisAtivo );

  // The keypad's segments: 1 ordered, 2 six cards, 3 four cards (the old values).
  // The label of 2 and 3 follows the alphabet, which has more cards to the keypad.
  rotuloDaLinha( p, l.linha[1].y + ESP1, ESP4, "Teclado" );
  segmentadoLeve( p, l.grupoTeclado.x, l.grupoTeclado.y, l.grupoTeclado.w, l.grupoTeclado.h, vm.alfabeto ? kTecladoAz : kTeclado09, 3,
                  vm.teclado >= 1 && vm.teclado <= 3 ? vm.teclado - 1 : -1 );
  rotuloDaLinha( p, l.linha[2].y, l.linha[2].h, "Caracteres" );
  segmentadoLeve( p, l.grupoCaracteres.x, l.grupoCaracteres.y, l.grupoCaracteres.w, l.grupoCaracteres.h, kCaracteres, 2,
                  vm.alfabeto ? 1 : 0 );

  desenharRodape( p, l.rodape, "Salvar", Botao::PRIMARIO, false, "Cancelar" );
}

int alvosPoliticaPin( const VmPoliticaPin &vm, Alvo *out ) {
  const LayoutPolitica l = layoutPolitica( vm );
  int n = addAlvoVoltar( out, 0, caixaVoltar( ), POLPIN_VOLTAR );
  n = addAlvoSe( out, n, l.menosAtivo, l.menos, POLPIN_MENOS );
  n = addAlvoSe( out, n, l.maisAtivo, l.mais, POLPIN_MAIS );
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
