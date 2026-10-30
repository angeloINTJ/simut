// The PIN keypad. See IdentidadePin.h. The header (title, field, help line) is
// the plan's cabecalhoPin( ) from tools/tft_ui/plano/PlanoTeclados.cpp; the keys
// follow P1, P2 and P3 and add what the plan has not drawn: Apagar and Entrar,
// the alphanumeric layouts and the lockout states.
#include "IdentidadePin.h"

#include <stdio.h>
#include <string.h>

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

namespace {

using namespace ident;

// The bar's only action, never dimmed, even when the keys are. A function and not a constant: UA_TXT( ) answers from the pack the boot loads, so a constant built at start-up would hold the
// English for good (Textos.h). The kit's "Cancel" in every language; the bar keeps the action it was measured from (Barra::acoes), so the label that sizes the box is the label that is drawn.
AcaoBarra acaoCancelar( ) { return AcaoBarra{ UA_TXT( "Cancel" ), Botao::DISCRETO, IconeNome::NENHUM, false }; }

constexpr int16_t M = ESP3, W_UTIL = 320 - 2 * M;                 // the margins of the plan's keypads
constexpr int16_t Y_CAMPO = 36, H_CAMPO = 32, Y_AJUDA = 72, H_AJUDA = 16;
constexpr int16_t Y_TECLAS = 96;                                   // under the help line, as P1 to P3
constexpr int16_t Y_RODAPE = 196, H_RODAPE = 32;                   // Apagar and Entrar: T1's action row
constexpr int16_t PASSO_LUGAR = ESP4, D_PONTO = 6;                 // the plan's places: 16 px apart, dots of 6
constexpr int LUGARES_MAX = 16;                                    // the longest PIN of any keypad (KB_PLAIN)

// Today's layouts, in the plan's drawing: the card width follows from the columns
// and the gap (the keys fill the 296 px between the margins). Every grid but the
// numeric pad ends at y 188 (two rows of 44 or three of 28, 4 px apart), 8 px
// above the footer row (196); the numeric pad has no footer, its Entrar and
// Apagar are cells.
struct Grade { int8_t cols, rows; int16_t h, gx, gy; };
const Grade kGrades[6] = {
  { 2, 2, 44, ESP1, ESP1 },     // CARTOES4:  P1's 2x2, 146 x 44
  { 3, 2, 44, ESP1, ESP1 },     // CARTOES6:  P3's 3x2, 96 x 44
  { 3, 4, 28, ESP1, ESP1 },     // NUMERICO:  P2's 3x4, 96 x 28
  { 4, 3, 28, ESP1, ESP1 },     // CARTOES12: 4x3, 71 x 28
  { 6, 3, 28, ESP1, ESP1 },     // CARTOES18: 6x3, 46 x 28
  { 3, 3, 28, ESP1, ESP1 },     // GRUPOS:    3x3, 96 x 28
};

// The keys' faces are data and not text: the alphabet's letters in their groups (tools/ui_texts_allow.json), the same in every language.
const char *const kGrupos[9] = { "0123456789", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ" };
const char *const kRotulosGrupo[9] = { "0-9", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ" };   // the digits group is too wide for its key

const Grade &gradeDe( PinTeclado t ) {
  const int i = (int)t;
  return kGrades[i >= 0 && i < 6 ? i : 0];
}

int16_t larguraTecla( const Grade &g ) { return (int16_t)( ( W_UTIL - ( g.cols - 1 ) * g.gx ) / g.cols ); }

// The popup of a group, over the whole key area (y 92 to 192): one row of up to
// five keys, or the ten digits as 5 + 5, every row centred (PinKb::popupRows).
constexpr int16_t Y_POPUP = 92, H_POPUP = 100, W_TECLA_POPUP = 48, H_UMA_FILA = 52, H_DUAS_FILAS = 36;
constexpr int POPUP_MAX = 10;

struct LayoutPin {
  Barra barra;
  bool bloqueio;                    // BLOQUEIO_*: the danger card, no field and no keys
  bool teclasAtivas;                // the keys and the footer answer touch
  int lugares;                      // places in the field
  int nTeclas;
  Caixa16 teclas[PIN_CARTOES_MAX];
  Fonte fonte;                      // the face of a card or a key
  bool rodape;                      // the Apagar | Entrar row under the keys
  Caixa16 apagar, entrar;
  bool popup;
  Caixa16 cartao;
  int nPopup;
  Caixa16 pteclas[POPUP_MAX];
  char pchars[POPUP_MAX + 1];
};

LayoutPin layoutPin( const VmPin &vm ) {
  LayoutPin l{ };
  const AcaoBarra cancelar = acaoCancelar( );
  l.barra = layoutBarra( &cancelar, 1 );
  l.bloqueio = vm.estado == PinEstado::BLOQUEIO_CONTA || vm.estado == PinEstado::BLOQUEIO_PAINEL;
  l.teclasAtivas = vm.estado == PinEstado::LIVRE || vm.estado == PinEstado::ERRO;
  // The places: as many as the shortest entry or what has been typed past it,
  // up to the keypad's ceiling (the old row: n = max( typed, minimum )).
  int teto = vm.teto ? vm.teto : LUGARES_MAX;
  if ( teto > LUGARES_MAX ) teto = LUGARES_MAX;
  int lugares = vm.digitados > vm.minimo ? vm.digitados : vm.minimo;
  if ( lugares > teto ) lugares = teto;
  if ( lugares < 1 ) lugares = 1;
  l.lugares = lugares;
  if ( l.bloqueio ) return l;

  const Grade &g = gradeDe( vm.teclado );
  const int16_t w = larguraTecla( g );
  l.nTeclas = g.cols * g.rows;
  for ( int k = 0; k < l.nTeclas; k++ )
    l.teclas[k] = Caixa16{ (int16_t)( M + ( k % g.cols ) * ( w + g.gx ) ), (int16_t)( Y_TECLAS + ( k / g.cols ) * ( g.h + g.gy ) ), w, g.h };
  switch ( vm.teclado ) {
    case PinTeclado::CARTOES4:
    case PinTeclado::CARTOES6: l.fonte = tipo::dadoGrande; break;      // the old rule: 12 pt when a glyph has 50 px
    case PinTeclado::CARTOES12:
    case PinTeclado::CARTOES18: l.fonte = tipo::dado; break;           // the dense grids step down to 9 pt
    default: l.fonte = tipo::botao; break;
  }
  if ( vm.teclado == PinTeclado::NUMERICO ) {
    l.entrar = l.teclas[9];                                            // the old pad's empty cells
    l.apagar = l.teclas[11];
  } else {
    l.rodape = true;
    const int16_t wb = (int16_t)( ( W_UTIL - ESP2 ) / 2 );
    l.apagar = Caixa16{ M, Y_RODAPE, wb, H_RODAPE };
    l.entrar = Caixa16{ (int16_t)( M + wb + ESP2 ), Y_RODAPE, wb, H_RODAPE };
  }

  // The popup exists only over the group keys, and only while they are alive.
  if ( vm.teclado == PinTeclado::GRUPOS && vm.popup >= 0 && vm.popup < 9 && l.teclasAtivas ) {
    l.popup = true;
    l.cartao = Caixa16{ M, Y_POPUP, W_UTIL, H_POPUP };
    copiar( l.pchars, kGrupos[vm.popup], sizeof l.pchars );
    l.nPopup = (int)strlen( l.pchars );
    const int duas = l.nPopup > 5;
    const int primeira = duas ? ( l.nPopup + 1 ) / 2 : l.nPopup;
    const int16_t h = duas ? H_DUAS_FILAS : H_UMA_FILA;
    for ( int i = 0; i < l.nPopup; i++ ) {
      const int fila = i < primeira ? 0 : 1;
      const int nFila = fila == 0 ? primeira : l.nPopup - primeira;
      const int col = fila == 0 ? i : i - primeira;
      const int16_t largura = (int16_t)( nFila * W_TECLA_POPUP + ( nFila - 1 ) * ESP2 );
      const int16_t x0 = (int16_t)( M + ( W_UTIL - largura ) / 2 );
      const int16_t y0 = duas ? (int16_t)( Y_POPUP + PAD + fila * ( H_DUAS_FILAS + ESP1 ) )
                              : (int16_t)( Y_POPUP + ( H_POPUP - H_UMA_FILA ) / 2 );
      l.pteclas[i] = Caixa16{ (int16_t)( x0 + col * ( W_TECLA_POPUP + ESP2 ) ), y0, W_TECLA_POPUP, h };
    }
  }
  return l;
}

/* ------------------------------------------------------------------ */
/* Texts                                                                */
/* ------------------------------------------------------------------ */

void montarTitulo( const VmPin &vm, char *out, int cap ) {
  switch ( vm.fase ) {
    case PinFase::DEFINIR: copiar( out, UA_TXT( "New PIN" ), cap ); break;
    case PinFase::CONFIRMAR: copiar( out, UA_TXT( "Confirm the new PIN" ), cap ); break;
    case PinFase::IDENTIFICAR:
    default:
      if ( vm.conta[0] ) snprintf( out, (size_t)cap, "%s %s", UA_TXT( "PIN for" ), vm.conta );   // "PIN de maria"
      else copiar( out, UA_TXT( "Access PIN" ), cap );
      break;
  }
}

const char *rotuloEntrar( const VmPin &vm ) {
  switch ( vm.fase ) {
    case PinFase::DEFINIR: return UA_TXT( "Continue" );      // the next pass confirms it
    case PinFase::CONFIRMAR: return UA_TXT( "Save PIN" );
    default: return UA_TXT( "Sign in" );
  }
}

// "<account><rest>" for another account's PIN, the name cut to the room the rest leaves in the
// line (W_UTIL). The name goes first: the line has 296 px and one row of 9 pt text, there is no
// room under the field for a second (the keys start 28 px below it), and "Escolha de 6 a 12
// caracteres para joao.silva.2026." was 385 px. Measured on the ink of the whole line, not of
// its parts: the joint of the two shares the neighbours' side bearings, so the parts' sum is a
// pixel or two short of the whole. A 15-character name (the device's longest) is whole up to
// about 139 px of ink ("joao.silva.2026" is 115) beside the longest rest; the rest never is cut.
void linhaDaConta( const char *conta, const char *resto, char *out, int cap ) {
  char nome[ident::NOME + 4];
  int16_t largura = (int16_t)( W_UTIL - larguraTinta( resto, tipo::apoio ) );
  for ( ;; ) {
    cortar( conta, tipo::apoio, largura, nome, sizeof nome );
    snprintf( out, (size_t)cap, "%s%s", nome, resto );
    if ( larguraTinta( out, tipo::apoio ) <= W_UTIL || largura <= 0 ) break;
    largura = (int16_t)( largura - 2 );
  }
}

// What a PIN is counted in, plural: digits, or characters with the alphanumeric keyboard (the words are looked up where they are needed, UA_TXT( ) cannot initialise a table).
const char *unidadeDoPin( const VmPin &vm ) { return vm.alfanumerico ? UA_TXT( "characters" ) : UA_TXT( "digits" ); }

// The line under the field: the refusal, the wait, or the help of this layout
// and this pass. The device talks of digits, or of characters with 0-9 A-Z. Every text here is
// one line of 296 px at 9 pt: the 7 pt copy ("Toque no cartão que tem cada dígito do PIN.",
// 337 px) was cut ten times at the floor (2026-10-15), and so was the lengths' message below
// with "caracteres" (300 to 310 px, in a layout the samples did not draw).
void montarLinha( const VmPin &vm, const LayoutPin &l, char *out, int cap, Tom *tom ) {
  *tom = Tom::NEUTRO;
  out[0] = 0;
  if ( vm.estado == PinEstado::ERRO ) {
    *tom = Tom::PERIGO;
    switch ( vm.erro ) {
      case PinErro::CURTO:
        snprintf( out, (size_t)cap, "%s %d %s %s", UA_TXT( "Too short: use" ), (int)vm.minimo, unidadeDoPin( vm ), UA_TXT( "or more." ) );
        break;
      case PinErro::DIFERENTES: copiar( out, UA_TXT( "The PINs do not match. Start over." ), cap ); break;
      case PinErro::INCORRETO:
      default: copiar( out, UA_TXT( "Wrong PIN. Try again." ), cap ); break;
    }
    return;
  }
  if ( vm.estado == PinEstado::AGUARDANDO ) { copiar( out, UA_TXT( "Checking…" ), cap ); return; }
  if ( vm.estado == PinEstado::ESPERA ) {
    *tom = Tom::ALERTA;
    snprintf( out, (size_t)cap, "%s %u s.", UA_TXT( "Too many attempts. Wait" ), (unsigned)vm.esperaS );
    return;
  }
  if ( l.popup ) { snprintf( out, (size_t)cap, "%s %s.", UA_TXT( "Tap the character in group" ), kRotulosGrupo[vm.popup] ); return; }
  switch ( vm.fase ) {
    case PinFase::DEFINIR: {
      const int teto = vm.teto ? vm.teto : LUGARES_MAX;
      char regra[40];
      if ( teto > vm.minimo ) snprintf( regra, sizeof regra, "%d %s %d %s.", (int)vm.minimo, UA_TXT( "to" ), teto, unidadeDoPin( vm ) );
      else snprintf( regra, sizeof regra, "%d %s.", teto, unidadeDoPin( vm ) );
      if ( vm.conta[0] ) {                                // "joao.silva: 6 a 12 caracteres."
        char resto[48];
        snprintf( resto, sizeof resto, ": %s", regra );
        linhaDaConta( vm.conta, resto, out, cap );
      } else if ( teto > vm.minimo ) {
        snprintf( out, (size_t)cap, "%s %s %s", UA_TXT( "Choose" ), UA_TXT( "from" ), regra );   // "Escolha de 6 a 12 caracteres."
      } else {
        snprintf( out, (size_t)cap, "%s %s", UA_TXT( "Choose" ), regra );                        // "Escolha 8 dígitos."
      }
      break;
    }
    case PinFase::CONFIRMAR:
      if ( vm.conta[0] ) {                                  // "joao.silva: digite o mesmo PIN."
        char resto[48];
        snprintf( resto, sizeof resto, ": %s", UA_TXT( "type the same PIN." ) );
        linhaDaConta( vm.conta, resto, out, cap );
      } else {
        copiar( out, UA_TXT( "Type the same PIN again." ), cap );
      }
      break;
    case PinFase::IDENTIFICAR:
    default:
      switch ( vm.teclado ) {
        case PinTeclado::NUMERICO: copiar( out, UA_TXT( "Tap the digits of the PIN." ), cap ); break;
        case PinTeclado::GRUPOS: copiar( out, UA_TXT( "Tap the group, then the character." ), cap ); break;
        default:
          snprintf( out, (size_t)cap, "%s %s.", UA_TXT( "Tap the card with each" ), vm.alfanumerico ? UA_TXT( "character" ) : UA_TXT( "digit" ) );
          break;
      }
      break;
  }
}

// "790" -> "7 9 0": the plan's group key, the glyphs spaced and in one ink.
void juntarGlifos( const char *glifos, char *out ) {
  int j = 0;
  for ( int i = 0; i < PIN_GLIFOS_MAX && glifos[i]; i++ ) {
    if ( i ) out[j++] = ' ';
    out[j++] = glifos[i];
  }
  out[j] = 0;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                              */
/* ------------------------------------------------------------------ */

void desenharTecla( Pintor &p, const VmPin &vm, const LayoutPin &l, int k ) {
  const Caixa16 &c = l.teclas[k];
  TeclaOpts o;
  o.estado = !l.teclasAtivas ? TeclaEstado::DESAB : vm.pressionada == k ? TeclaEstado::PRESSIONADA : TeclaEstado::REPOUSO;
  char legenda[2 * PIN_GLIFOS_MAX + 1];
  switch ( vm.teclado ) {
    case PinTeclado::NUMERICO:
      if ( k == 9 ) {
        o.tipo = TeclaTipo::PRIMARIA;
        o.legenda = rotuloEntrar( vm );
        o.fonte = tipo::botao;
      } else if ( k == 11 ) {
        o.tipo = TeclaTipo::FUNCAO;
        o.icone = IconeNome::DELETE;
        o.tam = 20;
      } else {
        legenda[0] = k == 10 ? '0' : (char)( '1' + k );
        legenda[1] = 0;
        o.legenda = legenda;
        o.fonte = tipo::botao;
      }
      break;
    case PinTeclado::GRUPOS:
      o.legenda = kRotulosGrupo[k];
      o.fonte = tipo::botao;
      break;
    default:
      juntarGlifos( vm.cartoes[k], legenda );
      o.legenda = legenda;
      o.fonte = l.fonte;
      break;
  }
  tecla( p, c.x, c.y, c.w, c.h, o );
}

void desenharPopup( Pintor &p, const LayoutPin &l ) {
  const Tema &T = p.t( );
  // A floating card: a strong border and no shadow (the TFT has none; the plan's
  // busy-web card does the same).
  p.fillRoundRect( l.cartao.x, l.cartao.y, l.cartao.w, l.cartao.h, RAIO_CARTAO, T.superficie );
  p.roundRect( l.cartao.x, l.cartao.y, l.cartao.w, l.cartao.h, RAIO_CARTAO, T.linhaForte );
  for ( int i = 0; i < l.nPopup; i++ ) {
    const Caixa16 &c = l.pteclas[i];
    const char s[2] = { l.pchars[i], 0 };
    TeclaOpts o;
    o.tipo = TeclaTipo::FUNCAO;                 // a tone off the card, so each key reads as a key
    o.legenda = s;
    o.fonte = tipo::dadoGrande;
    tecla( p, c.x, c.y, c.w, c.h, o );
  }
}

// The lockouts: the whole screen in the danger language of the plan's alarm
// action (a card in perigoSuave, the alert icon, the text in perigo), with
// nothing to type on and only Cancelar in the bar.
void desenharBloqueio( Pintor &p, const VmPin &vm ) {
  const Tema &T = p.t( );
  const int16_t w = W_UTIL, h = 104, y = 60;
  p.fillRoundRect( M, y, w, h, RAIO_CARTAO, T.perigoSuave );
  p.roundRect( M, y, w, h, RAIO_CARTAO, T.perigo );
  p.icone( IconeNome::TRIANGLE_ALERT, 24, 160 - 12, y + ESP4, T.perigo );
  textoCentrado( p, vm.estado == PinEstado::BLOQUEIO_CONTA ? UA_TXT( "Account locked." ) : UA_TXT( "Panel locked." ), 160, y + 44, 24,
                 tipo::titulo, T.perigo, 'c' );
  textoCentrado( p, UA_TXT( "Restart the device to unlock." ), 160, y + 72, 20, tipo::corpo, T.tinta, 'c' );
}

}  // namespace

void desenharPin( Pintor &p, const VmPin &vm ) {
  const Tema &T = p.t( );
  const LayoutPin l = layoutPin( vm );
  char titulo[64];
  montarTitulo( vm, titulo, sizeof titulo );
  desenharBarra( p, l.barra, titulo );
  // Under the popup the bar is still drawn, but a touch there only closes the popup, as any point outside its keys: not pressable.
  UA_CONTROLE( l.barra.caixas[0].x, l.barra.caixas[0].y, l.barra.caixas[0].w, l.barra.caixas[0].h, !l.popup, "botao" );
  if ( l.bloqueio ) { desenharBloqueio( p, vm ); return; }

  // The field with the places of the PIN (the plan's cabecalhoPin( )): centred, no
  // "see" button (a PIN is never shown), no cursor (the empty places say where
  // the next one goes). Dimmed while the keys are.
  campo( p, M, Y_CAMPO, W_UTIL, H_CAMPO,
         vm.estado == PinEstado::ERRO ? CampoEstado::ERRO : l.teclasAtivas ? CampoEstado::FOCO : CampoEstado::DESAB );
  {
    const int16_t xi = M + ESP3, wi = W_UTIL - 2 * ESP3;
    const int16_t largura = (int16_t)( l.lugares * PASSO_LUGAR - ( PASSO_LUGAR - D_PONTO ) );
    PontosOpts o;
    o.cursor = false;
    o.max = (int16_t)l.lugares;
    o.passo = PASSO_LUGAR;
    if ( !l.teclasAtivas ) { o.temCor = true; o.cor = desab( T, T.tinta ); }
    const int n = vm.digitados < l.lugares ? vm.digitados : l.lugares;
    pontos( p, xi + rd2( wi - largura ), Y_CAMPO, H_CAMPO, (int16_t)n, o );
  }
  char linha[96], cortada[104];
  Tom tom;
  montarLinha( vm, l, linha, sizeof linha, &tom );
  cortar( linha, tipo::apoio, W_UTIL, cortada, sizeof cortada );
  ajuda( p, M, Y_AJUDA, H_AJUDA, cortada, tom );

  // Under a popup the keys are covered by its card: they are not drawn at all,
  // so no hidden legend lies under the popup's.
  if ( l.popup ) desenharPopup( p, l );
  else for ( int k = 0; k < l.nTeclas; k++ ) desenharTecla( p, vm, l, k );

  if ( l.rodape ) {
    const bool desabilitado = !l.teclasAtivas || l.popup;
    botao( p, l.apagar.x, l.apagar.y, l.apagar.w, l.apagar.h, UA_TXT( "Delete" ), Botao::SECUNDARIO, desabilitado, IconeNome::DELETE );
    botao( p, l.entrar.x, l.entrar.y, l.entrar.w, l.entrar.h, rotuloEntrar( vm ), Botao::PRIMARIO, desabilitado );
  }
}

int alvosPin( const VmPin &vm, Alvo *out ) {
  const LayoutPin l = layoutPin( vm );
  int n = 0;
  const uint8_t tipos[1] = { PIN_CANCELAR };
  // Under the popup the bar's action and the footer are drawn but dead (a touch there closes the popup, as any point outside its
  // keys does), and while the keys wait out a lockout or a reply they are drawn disabled: inert targets, with the boxes they have
  // alive. The popup's keys are listed last, so that they answer where a target meets one.
  const bool ativa[1] = { !l.popup };
  n = addAlvosBarra( out, n, l.barra, tipos, ativa );
  if ( l.bloqueio ) return n;
  const bool vivas = l.teclasAtivas && !l.popup;
  auto rodape = [&]( ) {
    if ( !l.rodape ) return;
    n = addAlvo( out, n, (int16_t)( l.apagar.x - ESP1 ), l.apagar.y, (int16_t)( l.apagar.w + 2 * ESP1 ), l.apagar.h, vivas ? (uint8_t)PIN_APAGAR : ALVO_INERTE );
    n = addAlvo( out, n, (int16_t)( l.entrar.x - ESP1 ), l.entrar.y, (int16_t)( l.entrar.w + 2 * ESP1 ), l.entrar.h, vivas ? (uint8_t)PIN_ENTRAR : ALVO_INERTE );
  };
  if ( l.popup ) {
    rodape( );
    // Same rule as the keys: each popup key reaches half the gap to its neighbours.
    const int16_t dx = ESP2 / 2, dy = l.nPopup > 5 ? ESP1 / 2 : 0;
    for ( int i = 0; i < l.nPopup; i++ ) {
      const Caixa16 &c = l.pteclas[i];
      n = addAlvo( out, n, (int16_t)( c.x - dx ), (int16_t)( c.y - dy ), (int16_t)( c.w + 2 * dx ), (int16_t)( c.h + 2 * dy ), PIN_POPUP, (int16_t)i );
    }
    return n;
  }

  // A key's target reaches half the gap to its neighbours, so a finger that lands between two
  // keys still presses one.
  const Grade &g = gradeDe( vm.teclado );
  const int16_t dx = g.gx / 2, dy = g.gy / 2;
  for ( int k = 0; k < l.nTeclas; k++ ) {
    const Caixa16 &c = l.teclas[k];
    uint8_t t = PIN_TECLA;
    int16_t indice = (int16_t)k;
    if ( vm.teclado == PinTeclado::NUMERICO && k == 9 ) { t = PIN_ENTRAR; indice = -1; }
    if ( vm.teclado == PinTeclado::NUMERICO && k == 11 ) { t = PIN_APAGAR; indice = -1; }
    n = addAlvo( out, n, (int16_t)( c.x - dx ), (int16_t)( c.y - dy ), (int16_t)( c.w + 2 * dx ), (int16_t)( c.h + 2 * dy ),
                 vivas ? t : ALVO_INERTE, vivas ? indice : (int16_t)-1 );
  }
  rodape( );
  return n;
}

}  // namespace tela
}  // namespace ua
