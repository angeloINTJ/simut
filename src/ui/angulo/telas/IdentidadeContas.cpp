// Who is at the panel, the list of accounts and one account. See IdentidadeContas.h.
#include "IdentidadeContas.h"

#include <stdio.h>
#include <string.h>

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

namespace {

using namespace ident;

// The bar's way out is the kit's own action (acaoVoltar( )), so that the identity bar says the kit's "Back" in every language; its type and its icon are the ones this bar always had.
// A function and not a constant: UA_TXT( ) answers from the pack the boot loads, so a constant built at start-up would hold the English for good (Textos.h). The bar keeps the action
// it was measured from (Barra::acoes), so the label that sizes the box is the label that is drawn.
AcaoBarra acaoVoltarDaBarra( ) {
  const Acao a = acaoVoltar( );
  return AcaoBarra{ a.rotulo, a.tipo, a.icone, false };
}

// The badges, one function each that the measure of the name's room (layoutQuem( ), layoutUsuarios( )) and the draw both call: a badge measured from one text and drawn
// with another would leave the name too much or too little room in every language but the one of the literal.
const char *seloBloqueada( ) { return UA_TXT( "Locked" ); }
const char *seloSemPin( ) { return UA_TXT( "No PIN" ); }

// Where the row's content sits, as the kit's lista( ) lays it (x 8, 304 wide,
// PAD inside): the name from the left, the chevron against the right.
constexpr int16_t X_CARTAO = ESP2, W_CARTAO = 320 - 2 * ESP2;
constexpr int16_t W_CHEVRON = 20 + ESP2;     // the icon and the gap before what sits left of it

/* ------------------------------------------------------------------ */
/* Who is at the panel                                                  */
/* ------------------------------------------------------------------ */

struct LayoutQuem {
  Barra barra;
  int pagina, paginas;
  bool pager;
  int nLinhas;
  Caixa16 linhas[LINHAS];
  char nomes[LINHAS][NOME + 4];    // cut to the room the row leaves beside the chevron or the badge
};

LayoutQuem layoutQuem( const VmQuem &vm ) {
  LayoutQuem l{ };
  const AcaoBarra voltar = acaoVoltarDaBarra( );
  l.barra = layoutBarra( &voltar, 1 );
  l.paginas = paginasDe( vm.total );
  l.pagina = vm.pagina < l.paginas ? vm.pagina : l.paginas - 1;
  l.pager = vm.total > LINHAS;
  l.nLinhas = vm.nLinhas < LINHAS ? vm.nLinhas : LINHAS;
  for ( int i = 0; i < l.nLinhas; i++ ) {
    l.linhas[i] = Caixa16{ X_CARTAO, (int16_t)( Y0 + i * H_LINHA ), W_CARTAO, H_LINHA };
    // A row that is out of tries has the badge and no chevron: it opens nothing.
    const int16_t reserva = vm.linhas[i].travada ? larguraSelo( seloBloqueada( ) ) : W_CHEVRON;
    cortar( vm.linhas[i].nome, tipo::corpo, (int16_t)( W_CARTAO - 2 * PAD - reserva - ESP2 ), l.nomes[i],
            sizeof l.nomes[i] );
  }
  return l;
}

/* ------------------------------------------------------------------ */
/* The list of accounts                                                 */
/* ------------------------------------------------------------------ */

struct LayoutUsuarios {
  Barra barra;
  int pagina, paginas;
  bool pager;
  int nLinhas;
  Caixa16 linhas[LINHAS_2];
  char nomes[LINHAS_2][NOME + 4];
  char resumos[LINHAS_2][40];
  int16_t yNota;                   // where the note under the list goes (-1: no note, or a pager takes the row)
};

// The two lines of a row, each in a box of 20 px (the pitch of `corpo`) 4 px under the row's top and
// under the first box: the name's caps start 7 px under the row's edge and the summary's baseline
// is 7 px above its foot, as MenusLista's desenharDuasLinhas( ).
constexpr int16_t H_CAIXA = 20, Y_CAIXA = ESP1;

// "Limites · Alarmes · Manut." for the bits an account has; "Só leitura" for none. The names are looked up as they are needed (UA_TXT( ) cannot initialise a table).
void resumoPermissoes( uint8_t perms, char *out, int cap ) {
  out[0] = 0;
  for ( int b = 0; b < 3; b++ ) {
    if ( !( perms & ( 1u << b ) ) ) continue;
    if ( out[0] ) anexar( out, cap, " · " );
    anexar( out, cap, b == 0 ? UA_TXT( "Limits" ) : b == 1 ? UA_TXT( "Alarms" ) : UA_TXT( "Maint." ) );
  }
  if ( !out[0] ) copiar( out, UA_TXT( "Read only" ), cap );
}

void acoesUsuarios( const VmUsuarios &vm, AcaoBarra *a ) {
  // "Nova conta" has no icon: the kit's USER_PLUS exists only at 20 px, and
  // the bar's buttons are 20 px tall (they take 16 px icons). With it the
  // bar would not hold the title, "Nova conta" and "Voltar" either.
  a[0] = AcaoBarra{ UA_TXT( "Add account" ), Botao::DISCRETO, IconeNome::NENHUM, !vm.podeCriar };
  a[1] = acaoVoltarDaBarra( );
}

LayoutUsuarios layoutUsuarios( const VmUsuarios &vm ) {
  LayoutUsuarios l{ };
  AcaoBarra acoes[MAX_ACOES];
  acoesUsuarios( vm, acoes );
  l.barra = layoutBarra( acoes, 2 );
  l.paginas = paginasDe( vm.total, LINHAS_2 );
  l.pagina = vm.pagina < l.paginas ? vm.pagina : l.paginas - 1;
  l.pager = vm.total > LINHAS_2;
  l.nLinhas = vm.nLinhas < LINHAS_2 ? vm.nLinhas : LINHAS_2;
  for ( int i = 0; i < l.nLinhas; i++ ) {
    l.linhas[i] = Caixa16{ X_CARTAO, (int16_t)( Y0 + i * H_LINHA_2 ), W_CARTAO, H_LINHA_2 };
    // The badge sits on the name's line, between the name and the chevron.
    const int16_t reserva = W_CHEVRON + ( vm.linhas[i].temPin ? 0 : larguraSelo( seloSemPin( ) ) + ESP2 );
    cortar( vm.linhas[i].nome, tipo::corpo, (int16_t)( W_CARTAO - 2 * PAD - reserva - ESP2 ), l.nomes[i],
            sizeof l.nomes[i] );
    resumoPermissoes( vm.linhas[i].perms, l.resumos[i], sizeof l.resumos[i] );
  }
  l.yNota = -1;
  if ( !vm.podeCriar && vm.total > 0 && !l.pager )          // a pager would take the row under the list
    l.yNota = (int16_t)( Y0 + l.nLinhas * H_LINHA_2 + ESP2 );
  return l;
}

/* ------------------------------------------------------------------ */
/* One account                                                          */
/* ------------------------------------------------------------------ */

// Compact rows, as the plan's own one-row cards (28 px; the switch is 24): five
// rows of 40 would not leave the footer its place.
constexpr int16_t H_CTA = 28;
constexpr int16_t Y_CARTAO2 = Y0 + 3 * H_CTA + ESP2;       // 128: a gap after the three switches

struct LayoutConta {
  Barra barra;
  Caixa16 interruptores[3];
  Caixa16 definirPin, excluir;
  int16_t yNota;                   // the note of a new account (-1: none)
  Rodape rodape;                   // Salvar | Cancelar, with the labels the boxes were measured from
};

LayoutConta layoutConta( const VmConta &vm ) {
  LayoutConta l{ };
  const AcaoBarra voltar = acaoVoltarDaBarra( );
  l.barra = layoutBarra( &voltar, 1 );
  for ( int i = 0; i < 3; i++ )
    l.interruptores[i] = Caixa16{ X_CARTAO, (int16_t)( Y0 + i * H_CTA ), W_CARTAO, H_CTA };
  l.yNota = -1;
  if ( vm.nova ) {
    l.yNota = Y_CARTAO2;
  } else {
    l.definirPin = Caixa16{ X_CARTAO, Y_CARTAO2, W_CARTAO, H_CTA };
    l.excluir = Caixa16{ X_CARTAO, (int16_t)( Y_CARTAO2 + H_CTA ), W_CARTAO, H_CTA };
  }
  l.rodape = layoutRodape( vm.nova ? UA_TXT( "Continue" ) : UA_TXT( "Save" ), UA_TXT( "Cancel" ) );   // looked up once: the draw takes the labels from these boxes
  return l;
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Who is at the panel                                                  */
/* ------------------------------------------------------------------ */

void desenharQuem( Pintor &p, const VmQuem &vm ) {
  const Tema &T = p.t( );
  const LayoutQuem l = layoutQuem( vm );
  desenharBarra( p, l.barra, UA_TXT( "Who is at the panel?" ) );
  if ( l.nLinhas == 0 ) {
    estadoVazio( p, UA_TXT( "No account has a PIN." ), UA_TXT( "Set a PIN on the web." ) );
  } else {
    Item itens[LINHAS];
    for ( int i = 0; i < l.nLinhas; i++ ) {
      itens[i].texto = l.nomes[i];
      if ( vm.linhas[i].travada ) {
        // Dimmed name, and the reason in the danger colour: "this account is
        // done until the panel restarts", said BEFORE the PIN, not after.
        itens[i].temTom = true;
        itens[i].tom = desab( T, T.tinta );
        itens[i].selo = seloBloqueada( );
        itens[i].seloTom = Tom::PERIGO;
      } else {
        itens[i].chevron = true;
      }
    }
    lista( p, Y0, itens, (int16_t)l.nLinhas, H_LINHA );
  }
  if ( l.pager ) paginadorEstado( p, (int16_t)l.pagina, (int16_t)l.paginas );
}

int alvosQuem( const VmQuem &vm, Alvo *out ) {
  const LayoutQuem l = layoutQuem( vm );
  const uint8_t tipos[1] = { QUEM_VOLTAR };
  const bool ativa[1] = { true };
  int n = addAlvosBarra( out, 0, l.barra, tipos, ativa );
  for ( int i = 0; i < l.nLinhas; i++ ) {
    // A dimmed row is an inert target: its top and bottom 12 px would otherwise select the account beside it.
    n = addAlvoSe( out, n, !vm.linhas[i].travada, l.linhas[i], QUEM_LINHA, (int16_t)( l.pagina * LINHAS + i ) );
  }
  if ( l.pager ) n = addAlvosPaginador( out, n, l.pagina, l.paginas, QUEM_PAG_ANTERIOR, QUEM_PAG_PROXIMA );
  return n;
}

/* ------------------------------------------------------------------ */
/* The list of accounts                                                 */
/* ------------------------------------------------------------------ */

void desenharUsuarios( Pintor &p, const VmUsuarios &vm ) {
  const Tema &T = p.t( );
  const LayoutUsuarios l = layoutUsuarios( vm );
  desenharBarra( p, l.barra, UA_TXT( "Users" ) );
  if ( l.nLinhas == 0 ) {
    estadoVazio( p, UA_TXT( "Only the admin exists." ),
                 vm.podeCriar ? UA_TXT( "Tap Add account to create another." ) : UA_TXT( "Your account cannot create accounts." ) );
  } else {
    // The kit's lista( ) has one line per row, and a name, the permissions,
    // the badge and the chevron do not fit one (the permissions alone are
    // 194 px of 280 at 9 pt). Same card, same dividers and chevron, the
    // permissions on a second line in the support type. The badge shares the
    // name's line, which leaves the permissions the whole width under it:
    // beside it (it starts at x 175) the widest summary, "Limites · Alarmes ·
    // Manut." (194 px from x 20), would end under the badge.
    const int16_t total = (int16_t)( l.nLinhas * H_LINHA_2 );
    p.fillRoundRect( X_CARTAO, Y0, W_CARTAO, total, RAIO_CARTAO, T.superficie );
    p.roundRect( X_CARTAO, Y0, W_CARTAO, total, RAIO_CARTAO, T.linha );
    for ( int i = 0; i < l.nLinhas; i++ ) {
      const Caixa16 &c = l.linhas[i];
      if ( i > 0 ) p.hline( c.x + PAD, c.y, c.w - 2 * PAD, T.linha );
      const int16_t tx = c.x + PAD;
      textoCentrado( p, l.nomes[i], tx, c.y + Y_CAIXA, H_CAIXA, tipo::corpo, T.tinta, 'l' );
      textoCentrado( p, l.resumos[i], tx, c.y + Y_CAIXA + H_CAIXA, H_CAIXA, tipo::apoio, T.tinta2, 'l' );
      int16_t rx = c.x + c.w - PAD;
      p.icone( IconeNome::CHEVRON_RIGHT, 20, rx - 20, c.y + rd2( c.h - 20 ), T.tinta2 );
      rx -= W_CHEVRON;
      if ( !vm.linhas[i].temPin ) {
        const char *const semPin = seloSemPin( );                // looked up once: the width and the badge read this one pointer
        const int16_t sw = larguraSelo( semPin );
        selo( p, rx - sw, c.y + Y_CAIXA, H_CAIXA, semPin, Tom::ALERTA );
      }
    }
    if ( l.yNota >= 0 ) textoCentrado( p, UA_TXT( "Your account cannot create accounts." ), PAD, l.yNota, 16, tipo::apoio, T.tinta2, 'l' );
  }
  if ( l.pager ) paginadorEstado( p, (int16_t)l.pagina, (int16_t)l.paginas );
}

int alvosUsuarios( const VmUsuarios &vm, Alvo *out ) {
  const LayoutUsuarios l = layoutUsuarios( vm );
  const uint8_t tipos[2] = { USR_NOVA, USR_VOLTAR };
  const bool ativa[2] = { vm.podeCriar, true };
  int n = addAlvosBarra( out, 0, l.barra, tipos, ativa );
  for ( int i = 0; i < l.nLinhas; i++ ) n = addAlvo( out, n, l.linhas[i], USR_LINHA, (int16_t)( l.pagina * LINHAS_2 + i ) );
  if ( l.pager ) n = addAlvosPaginador( out, n, l.pagina, l.paginas, USR_PAG_ANTERIOR, USR_PAG_PROXIMA );
  return n;
}

/* ------------------------------------------------------------------ */
/* One account                                                          */
/* ------------------------------------------------------------------ */

void desenharConta( Pintor &p, const VmConta &vm ) {
  const Tema &T = p.t( );
  const LayoutConta l = layoutConta( vm );
  desenharBarra( p, l.barra, vm.nome[0] ? vm.nome : UA_TXT( "New account" ) );

  // Looked up here: UA_TXT( ) cannot initialise a static table. "Maintenance mode" and not "Maintenance": one English has one
  // translation, and the shared key's es-ES is "Mantenim." for the alarm list's badge, where "Mantenimiento" cut the sensor's
  // name (S10c B4a-b). This row has the width for the whole word, which the original panel's es-ES row shows (review of B5b).
  const char *const rotulos[3] = { UA_TXT( "Edit limits" ), UA_TXT( "Block alarms" ), UA_TXT( "Maintenance mode" ) };
  static const uint8_t bits[3] = { CTA_PERM_LIMITES, CTA_PERM_ALARMES, CTA_PERM_MANUT };
  Item perms[3];
  for ( int i = 0; i < 3; i++ ) {
    perms[i].texto = rotulos[i];
    perms[i].interruptor = ( vm.perms & bits[i] ) ? 1 : 0;
  }
  lista( p, Y0, perms, 3, H_CTA );

  if ( vm.nova ) {
    // What comes after Seguir, said before the tap. Two lines, 18 px apart: in one the sentence
    // is 345 px and left the glass at 9 pt (2026-10-15), and 68 px are free under the switches.
    textoCentrado( p, UA_TXT( "After Continue, you set" ), PAD, l.yNota, 18, tipo::apoio, T.tinta2, 'l' );
    textoCentrado( p, UA_TXT( "the account's PIN." ), PAD, l.yNota + 18, 18, tipo::apoio, T.tinta2, 'l' );
  } else {
    Item acoes[2];
    acoes[0].texto = UA_TXT( "Set PIN" );
    acoes[0].icone = IconeNome::KEY_ROUND;
    acoes[0].chevron = true;
    if ( vm.temPin ) acoes[0].valor = UA_TXT( "Set" );
    else { acoes[0].selo = seloSemPin( ); acoes[0].seloTom = Tom::ALERTA; }
    acoes[0].valorFonte = tipo::rotulo;
    acoes[1].texto = UA_TXT( "Delete account" );
    acoes[1].icone = IconeNome::TRASH_2;
    acoes[1].temTom = true;               // the danger colour on the icon and the text
    acoes[1].tom = T.perigo;
    lista( p, Y_CARTAO2, acoes, 2, H_CTA );
  }
  desenharRodape( p, l.rodape );
}

int alvosConta( const VmConta &vm, Alvo *out ) {
  const LayoutConta l = layoutConta( vm );
  const uint8_t tipos[1] = { CTA_VOLTAR };
  const bool ativa[1] = { true };
  int n = addAlvosBarra( out, 0, l.barra, tipos, ativa );
  for ( int i = 0; i < 3; i++ ) n = addAlvo( out, n, l.interruptores[i], CTA_INTERRUPTOR, (int16_t)i );
  if ( !vm.nova ) {
    n = addAlvo( out, n, l.definirPin, CTA_DEFINIR_PIN );
    n = addAlvo( out, n, l.excluir, CTA_EXCLUIR );
  }
  n = addAlvo( out, n, l.rodape.primaria, CTA_SALVAR );
  return addAlvo( out, n, l.rodape.secundaria, CTA_CANCELAR );
}

}  // namespace tela
}  // namespace ua
