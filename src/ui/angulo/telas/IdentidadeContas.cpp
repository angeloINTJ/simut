// Who is at the panel, the list of accounts and one account. See IdentidadeContas.h.
#include "IdentidadeContas.h"

#include <stdio.h>
#include <string.h>

namespace ua {
namespace tela {

namespace {

using namespace ident;

const AcaoBarra kVoltar = { "Voltar", Botao::DISCRETO, IconeNome::ARROW_LEFT, false };

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
  l.barra = layoutBarra( &kVoltar, 1 );
  l.paginas = paginasDe( vm.total );
  l.pagina = vm.pagina < l.paginas ? vm.pagina : l.paginas - 1;
  l.pager = vm.total > LINHAS;
  l.nLinhas = vm.nLinhas < LINHAS ? vm.nLinhas : LINHAS;
  for ( int i = 0; i < l.nLinhas; i++ ) {
    l.linhas[i] = Caixa16{ X_CARTAO, (int16_t)( Y0 + i * H_LINHA ), W_CARTAO, H_LINHA };
    // A row that is out of tries has the badge and no chevron: it opens nothing.
    const int16_t reserva = vm.linhas[i].travada ? larguraSelo( "Bloqueada" ) : W_CHEVRON;
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

// "Limites · Alarmes · Manut." for the bits an account has; "Só leitura" for none.
void resumoPermissoes( uint8_t perms, char *out, int cap ) {
  static const char *const nomes[3] = { "Limites", "Alarmes", "Manut." };
  out[0] = 0;
  for ( int b = 0; b < 3; b++ ) {
    if ( !( perms & ( 1u << b ) ) ) continue;
    if ( out[0] ) anexar( out, cap, " · " );
    anexar( out, cap, nomes[b] );
  }
  if ( !out[0] ) copiar( out, "Só leitura", cap );
}

void acoesUsuarios( const VmUsuarios &vm, AcaoBarra *a ) {
  // "Nova conta" has no icon: the kit's USER_PLUS exists only at 20 px, and
  // the bar's buttons are 20 px tall (they take 16 px icons). With it the
  // bar would not hold the title, "Nova conta" and "Voltar" either.
  a[0] = AcaoBarra{ "Nova conta", Botao::DISCRETO, IconeNome::NENHUM, !vm.podeCriar };
  a[1] = kVoltar;
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
    const int16_t reserva = W_CHEVRON + ( vm.linhas[i].temPin ? 0 : larguraSelo( "Sem PIN" ) + ESP2 );
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
  Caixa16 salvar, cancelar;
  const char *primaria;
};

LayoutConta layoutConta( const VmConta &vm ) {
  LayoutConta l{ };
  l.barra = layoutBarra( &kVoltar, 1 );
  for ( int i = 0; i < 3; i++ )
    l.interruptores[i] = Caixa16{ X_CARTAO, (int16_t)( Y0 + i * H_CTA ), W_CARTAO, H_CTA };
  l.yNota = -1;
  if ( vm.nova ) {
    l.yNota = Y_CARTAO2;
  } else {
    l.definirPin = Caixa16{ X_CARTAO, Y_CARTAO2, W_CARTAO, H_CTA };
    l.excluir = Caixa16{ X_CARTAO, (int16_t)( Y_CARTAO2 + H_CTA ), W_CARTAO, H_CTA };
  }
  l.primaria = vm.nova ? "Seguir" : "Salvar";
  layoutRodape( l.primaria, "Cancelar", &l.salvar, &l.cancelar );
  return l;
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Who is at the panel                                                  */
/* ------------------------------------------------------------------ */

void desenharQuem( Pintor &p, const VmQuem &vm ) {
  const Tema &T = p.t( );
  const LayoutQuem l = layoutQuem( vm );
  desenharBarra( p, l.barra, &kVoltar, "Quem está no painel?" );
  if ( l.nLinhas == 0 ) {
    estadoVazio( p, "Nenhuma conta tem PIN.", "Defina um PIN pela web." );
  } else {
    Item itens[LINHAS];
    for ( int i = 0; i < l.nLinhas; i++ ) {
      itens[i].texto = l.nomes[i];
      if ( vm.linhas[i].travada ) {
        // Dimmed name, and the reason in the danger colour: "this account is
        // done until the panel restarts", said BEFORE the PIN, not after.
        itens[i].temTom = true;
        itens[i].tom = desab( T, T.tinta );
        itens[i].selo = "Bloqueada";
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
  AcaoBarra acoes[MAX_ACOES];
  acoesUsuarios( vm, acoes );
  desenharBarra( p, l.barra, acoes, "Usuários" );
  if ( l.nLinhas == 0 ) {
    estadoVazio( p, "Só o admin existe.", vm.podeCriar ? "Toque em Nova conta para criar outra." : "Sua conta não pode criar contas." );
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
        const int16_t sw = larguraSelo( "Sem PIN" );
        selo( p, rx - sw, c.y + Y_CAIXA, H_CAIXA, "Sem PIN", Tom::ALERTA );
      }
    }
    if ( l.yNota >= 0 ) textoCentrado( p, "Sua conta não pode criar contas.", PAD, l.yNota, 16, tipo::apoio, T.tinta2, 'l' );
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
  desenharBarra( p, l.barra, &kVoltar, vm.nome[0] ? vm.nome : "Nova conta" );

  static const char *const rotulos[3] = { "Editar limites", "Bloquear alarmes", "Manutenção" };
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
    textoCentrado( p, "Depois de Seguir, você define", PAD, l.yNota, 18, tipo::apoio, T.tinta2, 'l' );
    textoCentrado( p, "o PIN da conta.", PAD, l.yNota + 18, 18, tipo::apoio, T.tinta2, 'l' );
  } else {
    Item acoes[2];
    acoes[0].texto = "Definir PIN";
    acoes[0].icone = IconeNome::KEY_ROUND;
    acoes[0].chevron = true;
    if ( vm.temPin ) acoes[0].valor = "Definido";
    else { acoes[0].selo = "Sem PIN"; acoes[0].seloTom = Tom::ALERTA; }
    acoes[0].valorFonte = tipo::rotulo;
    acoes[1].texto = "Excluir a conta";
    acoes[1].icone = IconeNome::TRASH_2;
    acoes[1].temTom = true;               // the danger colour on the icon and the text
    acoes[1].tom = T.perigo;
    lista( p, Y_CARTAO2, acoes, 2, H_CTA );
  }
  desenharRodape( p, l.primaria, "Cancelar", l.salvar, l.cancelar );
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
  n = addAlvo( out, n, l.salvar, CTA_SALVAR );
  return addAlvo( out, n, l.cancelar, CTA_CANCELAR );
}

}  // namespace tela
}  // namespace ua
