// The text keyboard. See IdentidadeTexto.h. The geometry, the key order and the
// field are the plan's T1 (tools/tft_ui/plano/PlanoTeclados.cpp, desenharT1( )):
// keys of 26 x 28 four px apart, an action row of 32 px under them, the field
// at y 36. What the plan's T1 does not say is here: the counter, the layer keys
// that lead back, the full and the empty states.
#include "IdentidadeTexto.h"

#include <stdio.h>
#include <string.h>

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

namespace {

using namespace ident;

// The bar's only action: the kit's "Cancel" in every language. A function and not a constant: UA_TXT( ) answers from the pack the boot loads, so a constant built at start-up would hold the
// English for good (Textos.h); the bar keeps the action it was measured from (Barra::acoes), so the label that sizes the box is the label that is drawn.
AcaoBarra acaoCancelar( ) { return AcaoBarra{ UA_TXT( "Cancel" ), Botao::DISCRETO, IconeNome::NENHUM, false }; }

constexpr int16_t M = ESP3, W_UTIL = 320 - 2 * M;
constexpr int16_t Y_CAMPO = 36, H_CAMPO = 32, Y_AJUDA = 72, H_AJUDA = 16;
constexpr int16_t KW = 26, KH = 28, PASSO = KW + ESP1, Y_TECLAS = 96;      // the plan's keys, step 30
constexpr int16_t Y_ACOES = 196, H_ACOES = 32;
constexpr int16_t W_FUNC = 36, W_SALVAR = 64;                               // shift, layer and delete; Salvar at its least
constexpr int16_t MEIO_VAO = ESP1 / 2;                                      // a target reaches half the gap
constexpr int16_t W_OLHO = 20;                                              // the "see" icon

// The layers, three rows of up to ten keys (the keys' faces are data and not text: the letters of the alphabet, tools/ui_texts_allow.json). All ASCII, and no character the
// old keyboard could not type: the "#+=" layer is its 28 symbols in the order of
// PwdKb::SYMBOLS (PasswordKeyboard.h:30), ten, ten and eight to a row. abc and
// ABC keep the plan's @ . - _ and # ! ? ; at the ends of the rows.
const char *const kCamadas[4][3] = {
  { "qwertyuiop", "asdfghjkl@", "zxcvbnm.-_" },      // abc
  { "QWERTYUIOP", "ASDFGHJKL#", "ZXCVBNM!?;" },      // ABC
  { "1234567890", "@#$%&*-+=~", "()[]{}/\\^_" },     // 123
  { ".,!?:;\"'@#", "$%&*-+=~()", "[]{}/\\^_" },      // #+=
};

// camadaAtiva( ): the layer on screen (shift over abc gives ABC).
const char *const *camadaAtiva( TextoCamada c, TextoShift s ) {
  if ( c == TextoCamada::MIN && s != TextoShift::OFF ) return kCamadas[(int)TextoCamada::MAI];
  const int i = (int)c;
  return kCamadas[i >= 0 && i < 4 ? i : 0];
}

bool letras( TextoCamada c ) { return c == TextoCamada::MIN || c == TextoCamada::MAI; }

int tamanho( const VmTexto &vm ) { return (int)strnlen( vm.texto, TEXTO_MAX ); }
int maximoDe( const VmTexto &vm ) { return vm.maximo ? vm.maximo : TEXTO_MAX - 1; }

struct LayoutTexto {
  Barra barra;
  Caixa16 campo, ver;
  int nFila[3];
  Caixa16 teclas[3][10];
  Caixa16 shift, camada, espaco, apagar, salvar;
  bool cheio;                       // no more characters: the characters and the space are dimmed
  bool vazio;                       // nothing to delete or to save
  const char *rotuloSalvar;
};

LayoutTexto layoutTexto( const VmTexto &vm ) {
  LayoutTexto l{ };
  const AcaoBarra cancelar = acaoCancelar( );
  l.barra = layoutBarra( &cancelar, 1 );
  l.campo = Caixa16{ M, Y_CAMPO, W_UTIL, H_CAMPO };
  if ( vm.secreto ) {
    // The eye and the margin around it, up to the field's edge: 40 px wide.
    const int16_t x = (int16_t)( M + W_UTIL - ESP3 - W_OLHO - ESP2 );
    l.ver = Caixa16{ x, Y_CAMPO, (int16_t)( M + W_UTIL - x ), H_CAMPO };
  }
  const char *const *filas = camadaAtiva( vm.camada, vm.shift );
  for ( int r = 0; r < 3; r++ ) {
    l.nFila[r] = (int)strlen( filas[r] );
    for ( int c = 0; c < l.nFila[r]; c++ )
      l.teclas[r][c] = Caixa16{ (int16_t)( M + c * PASSO ), (int16_t)( Y_TECLAS + r * ( KH + ESP1 ) ), KW, KH };
  }
  l.cheio = tamanho( vm ) >= maximoDe( vm );
  l.vazio = tamanho( vm ) == 0;
  l.rotuloSalvar = vm.rotulo[0] ? vm.rotulo : UA_TXT( "Save" );   // looked up once: the width below and the draw read this one pointer

  // Actions, as the plan's: shift | layer | space | delete | Salvar, 296 px edge to edge.
  // Salvar grows (to the 4 px grid) only when its label does not fit 64 px.
  int16_t wS = W_SALVAR;
  const int16_t preciso = (int16_t)( larguraTinta( l.rotuloSalvar, tipo::botao ) + 2 * ESP1 );
  if ( preciso > wS ) wS = (int16_t)( ( preciso + ESP1 - 1 ) / ESP1 * ESP1 );
  int16_t x = M;
  l.shift = Caixa16{ x, Y_ACOES, W_FUNC, H_ACOES };
  x += W_FUNC + ESP1;
  l.camada = Caixa16{ x, Y_ACOES, W_FUNC, H_ACOES };
  x += W_FUNC + ESP1;
  l.salvar = Caixa16{ (int16_t)( 320 - M - wS ), Y_ACOES, wS, H_ACOES };
  l.apagar = Caixa16{ (int16_t)( l.salvar.x - ESP1 - W_FUNC ), Y_ACOES, W_FUNC, H_ACOES };
  l.espaco = Caixa16{ x, Y_ACOES, (int16_t)( l.apagar.x - ESP1 - x ), H_ACOES };
  return l;
}

Caixa16 crescer( const Caixa16 &c ) {
  return Caixa16{ (int16_t)( c.x - MEIO_VAO ), (int16_t)( c.y - MEIO_VAO ), (int16_t)( c.w + 2 * MEIO_VAO ), (int16_t)( c.h + 2 * MEIO_VAO ) };
}

// What the left key does: Shift over abc and ABC, the way to "#+=" over 123 and
// back to 123 from it. The second key leads to the numbers from the letters and to
// the letters from the rest.
TextoCamada destinoEsquerda( TextoCamada c ) { return c == TextoCamada::NUM ? TextoCamada::SIMB : TextoCamada::NUM; }
TextoCamada destinoSegunda( TextoCamada c ) { return letras( c ) ? TextoCamada::NUM : TextoCamada::MIN; }

// The width of one space in font f: the ink of "x x" less that of "xx". The kit
// measures ink, and a space has none.
int16_t avancoEspaco( Fonte f ) { return (int16_t)( larguraTinta( "x x", f ) - larguraTinta( "xx", f ) ); }

// The field's content, in the box campo( ) leaves (x + 12, y, w - 24, h): the
// text in the clear with a cursor, or dots; the counter; the eye of a secret.
void conteudoCampo( Pintor &p, const VmTexto &vm, const LayoutTexto &l ) {
  const Tema &T = p.t( );
  const int16_t xi = (int16_t)( l.campo.x + ESP3 ), wi = (int16_t)( l.campo.w - 2 * ESP3 );
  const int len = tamanho( vm );

  // The counter, against the right edge (left of the eye when there is one).
  char cont[16];
  snprintf( cont, sizeof cont, "%d %s %d", len, UA_TXT( "of" ), maximoDe( vm ) );
  const int16_t xr = (int16_t)( xi + wi - ( vm.secreto ? W_OLHO + ESP2 : 0 ) );
  const Tinta tc = textoCentrado( p, cont, xr, l.campo.y, l.campo.h, tipo::apoio, l.cheio ? T.alerta : T.tinta2, 'r' );
  if ( vm.secreto )
    p.icone( vm.ver ? IconeNome::EYE_OFF : IconeNome::EYE, W_OLHO, xi + wi - W_OLHO, l.campo.y + rd2( l.campo.h - W_OLHO ), T.tinta2 );
  const int16_t limite = (int16_t)( tc.x0 - ESP2 );

  if ( vm.secreto && !vm.ver ) {
    pontos( p, xi, l.campo.y, l.campo.h, (int16_t)len, PontosOpts{ } );
    return;
  }
  // In the clear. Spaces have no ink and the kit aligns by ink, so the ones at
  // the ends are measured apart; a text wider than the room shows its tail, to
  // keep the cursor in sight.
  const int16_t esp = avancoEspaco( tipo::corpo );
  int ini = 0, fim = len;
  while ( ini < fim && vm.texto[ini] == ' ' ) ini++;
  while ( fim > ini && vm.texto[fim - 1] == ' ' ) fim--;
  const int lead = ini, trail = len - fim;
  char corrida[TEXTO_MAX];
  memcpy( corrida, vm.texto + ini, (size_t)( fim - ini ) );
  corrida[fim - ini] = 0;
  int pula = 0;
  while ( corrida[pula] && xi + lead * esp + larguraTinta( corrida + pula, tipo::corpo ) + trail * esp + ESP1 + 2 > limite ) pula++;
  const int16_t x0 = (int16_t)( xi + lead * esp );
  int16_t xc = x0;
  if ( corrida[pula] ) {
    const Tinta t = textoCentrado( p, corrida + pula, x0, l.campo.y, l.campo.h, tipo::corpo, T.tinta, 'l' );
    xc = t.x1;
  }
  xc = (int16_t)( xc + trail * esp + ( len ? ESP1 : 0 ) );
  p.fillRect( xc, l.campo.y + rd2( l.campo.h ) - 8, 2, 16, T.acento );
}

}  // namespace

// The refusals of a new account's name (see the header).
const char *erroSoEspacos( ) { return UA_TXT( "Type a name, not only spaces." ); }
const char *erroCaracteres( ) { return UA_TXT( "Remove the quotes and the backslash." ); }
const char *erroReservado( ) { return UA_TXT( "Admin is reserved. Use another name." ); }
const char *erroRepetido( ) { return UA_TXT( "That name already exists. Use another." ); }

char teclaCaractere( TextoCamada camada, TextoShift shift, int indice ) {
  if ( indice < 0 ) return 0;
  const int r = indice / 10, c = indice % 10;
  if ( r > 2 ) return 0;
  const char *fila = camadaAtiva( camada, shift )[r];
  return c < (int)strlen( fila ) ? fila[c] : 0;
}

void desenharTexto( Pintor &p, const VmTexto &vm ) {
  const LayoutTexto l = layoutTexto( vm );
  desenharBarra( p, l.barra, vm.titulo );
  campo( p, l.campo.x, l.campo.y, l.campo.w, l.campo.h, vm.erro[0] ? CampoEstado::ERRO : CampoEstado::FOCO );
  conteudoCampo( p, vm, l );
  char linha[104];
  if ( vm.erro[0] ) {
    cortar( vm.erro, tipo::apoio, W_UTIL, linha, sizeof linha );
    ajuda( p, M, Y_AJUDA, H_AJUDA, linha, Tom::PERIGO );
  } else if ( vm.ajuda[0] ) {
    cortar( vm.ajuda, tipo::apoio, W_UTIL, linha, sizeof linha );
    ajuda( p, M, Y_AJUDA, H_AJUDA, linha, Tom::NEUTRO );
  }

  // The characters: one baseline to a row; a row of lowercase letters centres
  // the x height (the ascenders rise), any other the cap height.
  const char *const *filas = camadaAtiva( vm.camada, vm.shift );
  for ( int r = 0; r < 3; r++ ) {
    bool minusculas = false;
    for ( const char *c = filas[r]; *c; c++ )
      if ( *c >= 'a' && *c <= 'z' ) minusculas = true;
    for ( int c = 0; c < l.nFila[r]; c++ ) {
      const char legenda[2] = { filas[r][c], 0 };
      TeclaOpts o;
      o.legenda = legenda;
      o.estado = l.cheio ? TeclaEstado::DESAB : vm.pressionada == r * 10 + c ? TeclaEstado::PRESSIONADA : TeclaEstado::REPOUSO;
      o.alturaDeX = minusculas;
      o.fonte = tipo::corpo;
      tecla( p, l.teclas[r][c].x, l.teclas[r][c].y, KW, KH, o );
    }
  }

  // The left key: Shift or Caps over the letters, the way to and from "#+=" otherwise.
  TeclaOpts sh;
  sh.tipo = TeclaTipo::FUNCAO;
  if ( letras( vm.camada ) ) {
    sh.estado = vm.shift == TextoShift::LOCK ? TeclaEstado::TRAVADA : vm.shift == TextoShift::ON ? TeclaEstado::ATIVA : TeclaEstado::REPOUSO;
    sh.icone = vm.shift == TextoShift::LOCK ? IconeNome::ARROW_BIG_UP_DASH : IconeNome::ARROW_BIG_UP;
    sh.tam = 20;
  } else {
    sh.legenda = vm.camada == TextoCamada::NUM ? "#+=" : "123";
    sh.fonte = tipo::rotulo;
  }
  tecla( p, l.shift.x, l.shift.y, l.shift.w, l.shift.h, sh );

  TeclaOpts ly;
  ly.tipo = TeclaTipo::FUNCAO;
  ly.legenda = letras( vm.camada ) ? "123" : UA_TXT( "abc" );
  ly.fonte = tipo::rotulo;
  tecla( p, l.camada.x, l.camada.y, l.camada.w, l.camada.h, ly );

  TeclaOpts sp;
  sp.tipo = TeclaTipo::ESPACO;
  sp.legenda = UA_TXT( "space" );
  sp.fonte = tipo::apoio;
  sp.alturaDeX = true;
  sp.estado = l.cheio ? TeclaEstado::DESAB : TeclaEstado::REPOUSO;
  tecla( p, l.espaco.x, l.espaco.y, l.espaco.w, l.espaco.h, sp );

  TeclaOpts ap;
  ap.tipo = TeclaTipo::FUNCAO;
  ap.icone = IconeNome::DELETE;
  ap.tam = 20;
  ap.estado = l.vazio ? TeclaEstado::DESAB : TeclaEstado::REPOUSO;
  tecla( p, l.apagar.x, l.apagar.y, l.apagar.w, l.apagar.h, ap );

  TeclaOpts sv;
  sv.tipo = TeclaTipo::PRIMARIA;
  sv.fonte = tipo::botao;
  sv.legenda = l.rotuloSalvar;
  sv.estado = l.vazio ? TeclaEstado::DESAB : TeclaEstado::REPOUSO;
  tecla( p, l.salvar.x, l.salvar.y, l.salvar.w, l.salvar.h, sv );
}

int alvosTexto( const VmTexto &vm, Alvo *out ) {
  const LayoutTexto l = layoutTexto( vm );
  const uint8_t tipos[1] = { TXT_CANCELAR };
  const bool ativa[1] = { true };
  int n = addAlvosBarra( out, 0, l.barra, tipos, ativa );
  if ( vm.secreto ) n = addAlvo( out, n, l.ver, TXT_VER );
  // A full field draws its keys disabled and an empty one its Apagar and Salvar: listed inert, where they are drawn.
  for ( int r = 0; r < 3; r++ )
    for ( int c = 0; c < l.nFila[r]; c++ ) n = addAlvoSe( out, n, !l.cheio, crescer( l.teclas[r][c] ), TXT_TECLA, (int16_t)( r * 10 + c ) );
  if ( letras( vm.camada ) ) n = addAlvo( out, n, crescer( l.shift ), TXT_SHIFT );
  else n = addAlvo( out, n, crescer( l.shift ), TXT_CAMADA, (int16_t)destinoEsquerda( vm.camada ) );
  n = addAlvo( out, n, crescer( l.camada ), TXT_CAMADA, (int16_t)destinoSegunda( vm.camada ) );
  n = addAlvoSe( out, n, !l.cheio, crescer( l.espaco ), TXT_ESPACO );
  n = addAlvoSe( out, n, !l.vazio, crescer( l.apagar ), TXT_APAGAR );
  return addAlvoSe( out, n, !l.vazio, crescer( l.salvar ), TXT_SALVAR );
}

}  // namespace tela
}  // namespace ua
