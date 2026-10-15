// The Settings menu, the theme list and the language list. See MenusConfig.h.
#include "MenusConfig.h"

#include <stdio.h>

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

/* Tema: three rows, no pager. */

namespace {
constexpr int TEMAS_N = 3;
}

void desenharTemas( Pintor &p, const VmTemas &vm ) {
  barraTituloVoltar( p, "Tema" );
  Item itens[TEMAS_N];
  itens[TM_CLARO].texto = "Claro";
  itens[TM_CLARO].icone = IconeNome::SUN;
  itens[TM_ESCURO].texto = "Escuro";
  itens[TM_ESCURO].icone = IconeNome::MOON;
  // The rule is a value, not a parenthesis in the label: with the mark on this
  // row a label of 246 px would run under the check mark (the row has 232 px
  // between the icon and the mark).
  itens[TM_AUTOMATICO].texto = "Automático";
  itens[TM_AUTOMATICO].valor = "escuro das 19 às 7";
  itens[TM_AUTOMATICO].valorFonte = tipo::rotulo;
  itens[TM_AUTOMATICO].icone = IconeNome::SUN_MOON;
  for ( int i = 0; i < TEMAS_N; i++ ) itens[i].marcado = vm.atual == i;
  const int16_t fim = lista( p, Y0, itens, TEMAS_N, LISTA_H_FILA );
  desenharNota( p, fim + ESP2, "A cor de destaque é uma só: muda com o tema." );
}

int alvosTemas( const VmTemas &, Alvo *out ) { return alvosLista( layoutLista( TEMAS_N, 0 ), out, true ); }

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
