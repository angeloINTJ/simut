// The Settings menu, the theme list and the language list. See MenusConfig.h.
#include "MenusConfig.h"

#include <stdio.h>

#include <Arduino.h>   // the tokens header includes it: a system header first included inside the namespace below would land in it
namespace entrega {
#include "../kit/angulo_tokens.h"   // the palette's size, for the static_asserts below
}  // namespace entrega

namespace ua {
namespace tela {

namespace {

/* Configurações: the label and the icon of each item, by ItemMenu. */
struct Entrada { const char *texto; IconeNome icone; };
const Entrada kEntradas[IM_N] = {
  /* IM_TEMA */ { "Tema", IconeNome::PALETTE },
  /* IM_LIMITES */ { "Limites de alarme", IconeNome::BELL },
  /* IM_SONS */ { "Sons", IconeNome::VOLUME_2 },
  /* IM_IDIOMA */ { "Idioma", IconeNome::LANGUAGES },
  /* IM_PIN_PROPRIO */ { "Alterar meu PIN", IconeNome::KEY_ROUND },
  /* IM_CALIBRAR */ { "Calibrar o toque", IconeNome::CROSSHAIR },
  /* IM_LICENCA */ { "Licença", IconeNome::FILE_TEXT },
  /* IM_STATUS */ { "Status do sistema", IconeNome::ACTIVITY },
  /* IM_ALINHAR */ { "Alinhar a tela", IconeNome::MOVE },
  /* IM_USUARIOS */ { "Usuários", IconeNome::USERS },
  /* IM_SEGURANCA_PIN */ { "Segurança do PIN", IconeNome::SHIELD },
  /* IM_MODO_CONFIG */ { "Modo de configuração", IconeNome::ROUTER },
  /* IM_DATA_HORA */ { "Data e hora", IconeNome::CLOCK },
};

// An id the table does not have (a glue bug) is a row without text or icon,
// not a read past the table.
const Entrada kSemEntrada = { "", IconeNome::NENHUM };
const Entrada &entrada( uint8_t id ) { return id < IM_N ? kEntradas[id] : kSemEntrada; }

struct LayoutMenu {
  LayoutLista lista;
  char conta[48];      // "Conta: maria", cut to its room; "" when there is no account
};

LayoutMenu layoutMenu( const VmMenu &vm ) {
  LayoutMenu l{ };
  l.lista = layoutLista( vm.n < MENU_ITENS_MAX ? vm.n : MENU_ITENS_MAX, vm.pagina );
  if ( vm.conta[0] ) {
    char completo[48];
    snprintf( completo, sizeof completo, "Conta: %s", vm.conta );
    // The footer row is shared: the pager on the left, the account on the right
    // with what the pager leaves; ESP4 apart from "n de m".
    const int16_t fim = fimDoPaginador( l.lista.pag );
    const int16_t largura = (int16_t)( W_TELA - ESP2 - fim - ( l.lista.pag.visivel ? ESP4 : 0 ) );
    cortar( completo, tipo::apoio, largura, l.conta, sizeof l.conta );
  }
  return l;
}

}  // namespace

void desenharMenu( Pintor &p, const VmMenu &vm ) {
  const Tema &T = p.t( );
  const LayoutMenu l = layoutMenu( vm );
  barraTituloVoltar( p, "Configurações" );
  Item itens[LISTA_FILAS];
  for ( int i = 0; i < l.lista.n; i++ ) {
    const Entrada &e = entrada( vm.itens[l.lista.primeira + i] );
    itens[i].texto = e.texto;
    itens[i].icone = e.icone;
    itens[i].chevron = true;
  }
  if ( l.lista.n ) lista( p, Y0, itens, (int16_t)l.lista.n, LISTA_H_FILA );
  desenharPaginador( p, l.lista.pag );
  // In the pager's row, at the right edge of the list's card.
  if ( l.conta[0] ) {
    const Caixa16 e = paginadorEsquerda( );
    textoCentrado( p, l.conta, W_TELA - ESP2, e.y, e.h, tipo::apoio, T.tinta2, 'r' );
  }
}

int alvosMenu( const VmMenu &vm, Alvo *out ) { return alvosLista( layoutMenu( vm ).lista, out, true ); }

/* Tema: the mode, the accent, the choice in words and one primary button (S10c, A'3; S10C_A_PLANO.md section 3). Bands on the 4 px grid: the title
 * with Back (to y 35), three 96 x 44 segments (44), the mode in words (92), six 44 px swatches (120), "Cor de destaque: <nome>" (168), the button (192). */

namespace {

static_assert( ESP2 + TM_N * TEMA_W_SEGMENTO + ( TM_N - 1 ) * ESP2 + ESP2 == W_TELA, "the segments fill the glass between its margins" );
static_assert( TEMA_CORES_N == entrega::ACENTOS_N, "the screen names every entry of the palette, and only those" );
static_assert( entrega::ACENTOS_N <= 6, "the swatches are one row of 44 px targets, 8 px apart" );
static_assert( ESP2 + TEMA_CORES_N * TEMA_ALVO + ( TEMA_CORES_N - 1 ) * ESP2 <= W_TELA - ESP2, "the swatches fit the glass in one row" );
static_assert( ESP1 + ICONE_CONTROLE + ESP1 + TEMA_H_ROTULO + ESP1 == TEMA_ALVO, "a segment holds its icon over its label" );
static_assert( static_cast<int>( TT_NADA ) == static_cast<int>( ALVO_INERTE ), "a disabled button is listed as the inert target" );

Caixa16 caixaModo( int m ) {
  return Caixa16{ (int16_t)( ESP2 + m * ( TEMA_W_SEGMENTO + ESP2 ) ), TEMA_Y_SEGMENTOS, TEMA_W_SEGMENTO, TEMA_ALVO };
}

Caixa16 caixaCor( int k ) {
  return Caixa16{ (int16_t)( ESP2 + k * ( TEMA_ALVO + ESP2 ) ), TEMA_Y_AMOSTRAS, TEMA_ALVO, TEMA_ALVO };
}

Caixa16 caixaAplicar( ) { return Caixa16{ ESP2, TEMA_Y_APLICAR, (int16_t)( W_TELA - 2 * ESP2 ), TEMA_H_APLICAR }; }

// "Aplicar tema" has something to apply when the choice on show is not the saved one.
bool temAplicar( const VmTemas &vm ) { return indiceDaEscolha( vm.modo, vm.acento ) != vm.salvo; }

bool premido( const VmTemas &vm, uint8_t alvo, int indice ) { return vm.premidoTipo == alvo && vm.premidoIndice == indice; }

// A segment: an icon over its label. The selected one is the accent's fill with acentoTinta content; the others are superficie with a linhaForte
// edge. Pressed: the selected one turns acentoForte, another superficie2.
void desenharModo( Pintor &p, const VmTemas &vm, int m ) {
  const Tema &T = p.t( );
  const Caixa16 c = caixaModo( m );
  const bool atual = vm.modo == m;
  uint16_t fundo = atual ? T.acento : T.superficie;
  const uint16_t tinta = atual ? T.acentoTinta : T.tinta;
  if ( premido( vm, TT_MODO, m ) ) fundo = atual ? T.acentoForte : T.superficie2;
  UA_CONTROLE( c.x, c.y, c.w, c.h, true, "segmento" );
  p.fillRoundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, fundo );
  if ( !atual ) p.roundRect( c.x, c.y, c.w, c.h, RAIO_CONTROLE, T.linhaForte );
  p.icone( TEMA_ICONES[m], ICONE_CONTROLE, (int16_t)( c.x + rd2( c.w - ICONE_CONTROLE ) ), (int16_t)( c.y + ESP1 ), tinta );
  textoCentrado( p, TEMA_MODOS[m], (int16_t)( c.x + rd2( c.w ) ), (int16_t)( c.y + ESP1 + ICONE_CONTROLE + ESP1 ), TEMA_H_ROTULO, TEMA_FONTE_SEGMENTO, tinta, 'c' );
}

// A swatch: a 36 px disc in the entry's own accent for the mode on the glass. Never colour alone: the selected one also has the check mark and a
// 2 px ring of tinta with a 2 px gap that fills the 44 px box. Pressed: the disc turns the entry's acentoForte.
void desenharCor( Pintor &p, const VmTemas &vm, int k ) {
  const Tema &T = p.t( );
  const Caixa16 c = caixaCor( k );
  const Tema &E = ua::tema( T.escuro, (uint8_t)k );
  const bool atual = vm.acento == k;
  UA_CONTROLE( c.x, c.y, c.w, c.h, true, "amostra" );
  if ( atual ) {
    p.fillRoundRect( c.x, c.y, c.w, c.h, RAIO_TOTAL, T.tinta );
    p.fillRoundRect( (int16_t)( c.x + TEMA_ANEL ), (int16_t)( c.y + TEMA_ANEL ), (int16_t)( c.w - 2 * TEMA_ANEL ), (int16_t)( c.h - 2 * TEMA_ANEL ), RAIO_TOTAL, T.fundo );
  }
  p.fillRoundRect( (int16_t)( c.x + ESP1 ), (int16_t)( c.y + ESP1 ), (int16_t)( c.w - 2 * ESP1 ), (int16_t)( c.h - 2 * ESP1 ), RAIO_TOTAL,
                   premido( vm, TT_COR, k ) ? E.acentoForte : E.acento );
  if ( atual )
    p.icone( IconeNome::CHECK, ICONE_CONTROLE, (int16_t)( c.x + rd2( c.w - ICONE_CONTROLE ) ), (int16_t)( c.y + rd2( c.h - ICONE_CONTROLE ) ), E.acentoTinta );
}

}  // namespace

void desenharTemas( Pintor &p, const VmTemas &vm ) {
  const Tema &T = p.t( );
  barraTituloVoltar( p, TEMA_TITULO );
  for ( int m = 0; m < TM_N; m++ ) desenharModo( p, vm, m );
  // The choice in words: the selected state never rests on a colour alone.
  textoCentrado( p, TEMA_FRASES[vm.modo < TM_N ? vm.modo : static_cast<int>( TM_CLARO )], ESP2, TEMA_Y_FRASE, TEMA_H_LINHA, tipo::corpo, T.tinta, 'l' );
  for ( int k = 0; k < TEMA_CORES_N; k++ ) desenharCor( p, vm, k );
  char linha[48];
  snprintf( linha, sizeof linha, TEMA_FMT_COR, TEMA_CORES[vm.acento < TEMA_CORES_N ? vm.acento : 0] );
  textoCentrado( p, linha, ESP2, TEMA_Y_NOME, TEMA_H_LINHA, tipo::corpo, T.tinta, 'l' );
  // The one primary button. Disabled (its 45 % twin) while there is nothing to apply; pressed, it turns acentoForte.
  const Caixa16 b = caixaAplicar( );
  if ( premido( vm, TT_APLICAR, -1 ) && temAplicar( vm ) ) {
    UA_CONTROLE( b.x, b.y, b.w, b.h, true, "botao" );
    p.fillRoundRect( b.x, b.y, b.w, b.h, RAIO_CONTROLE, T.acentoForte );
    textoCentrado( p, TEMA_APLICAR_ROTULO, (int16_t)( b.x + rd2( b.w ) ), b.y, b.h, tipo::botao, T.acentoTinta, 'c' );
  } else {
    botao( p, b.x, b.y, b.w, b.h, TEMA_APLICAR_ROTULO, Botao::PRIMARIO, !temAplicar( vm ) );
  }
}

int alvosTemas( const VmTemas &vm, Alvo *out ) {
  int n = addAlvoVoltar( out, 0, alvoVoltar( ), LST_VOLTAR );
  for ( int m = 0; m < TM_N; m++ ) n = addAlvo( out, n, caixaModo( m ), TT_MODO, (int16_t)m );
  for ( int k = 0; k < TEMA_CORES_N; k++ ) n = addAlvo( out, n, caixaCor( k ), TT_COR, (int16_t)k );
  // A disabled button is listed with the box it would have enabled, as the inert target: a tap on it does nothing and no other target answers it.
  return addAlvo( out, n, caixaAplicar( ), (uint8_t)( temAplicar( vm ) ? TT_APLICAR : TT_NADA ) );
}

/* Idioma: English and the loaded pack, no pager. */

namespace {

int idiomasN( const VmIdioma &vm ) { return vm.pacote[0] ? 2 : 1; }

}  // namespace

void desenharIdioma( Pintor &p, const VmIdioma &vm ) {
  barraTituloVoltar( p, "Idioma" );
  const int n = idiomasN( vm );
  Item itens[2];
  itens[ID_INGLES].texto = "English";
  itens[ID_INGLES].marcado = vm.atual == ID_INGLES;
  // A pack's name is its own, so it is cut to the room left of the mark.
  char nome[sizeof vm.pacote];
  const int16_t sobra = (int16_t)( W_TELA - 2 * ESP2 - 2 * PAD - ICONE_CONTROLE - ESP2 );
  cortar( vm.pacote, tipo::corpo, sobra, nome, sizeof nome );
  itens[ID_PACOTE].texto = nome;
  itens[ID_PACOTE].marcado = vm.atual == ID_PACOTE;
  const int16_t fim = lista( p, Y0, itens, (int16_t)n, LISTA_H_FILA );
  // A deviation on the record: the new screens' texts are fixed in pt-BR, so
  // the choice (cfg.displayLang) still reaches the CLI and the log labels
  // (AppManager_Events.cpp, AppManager_Commands.cpp) but not this panel.
  desenharNota( p, fim + ESP2, "Esta build mostra o painel em português." );
}

int alvosIdioma( const VmIdioma &vm, Alvo *out ) { return alvosLista( layoutLista( idiomasN( vm ), 0 ), out, true ); }

}  // namespace tela
}  // namespace ua
