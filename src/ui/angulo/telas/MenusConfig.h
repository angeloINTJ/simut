#pragma once
// The Settings menu and the two lists that choose one thing from it: the
// theme and the language (ESPEC_UI_A.md §6.2). See MenusLista.h for the rows,
// the pager and the targets they share (LST_VOLTAR, LST_LINHA, LST_PAG_*).
#include "MenusLista.h"

namespace ua {
namespace tela {

/* ------------------------------------------------------------------ */
/* Configurações (MODE_SETTINGS_MAIN)                                   */
/* ------------------------------------------------------------------ */

// The menu's items. The values are SettingsMenuItem's (src/display/SettingsMenu.h)
// one for one, so the glue copies _menuItems[ ] into VmMenu::itens as it is
// (and can static_assert the match). The order they are shown in is the
// list's, not this enum's.
enum ItemMenu : uint8_t {
  IM_TEMA,             // MENU_THEMES
  IM_LIMITES,          // MENU_ALARMS
  IM_SONS,             // MENU_SOUNDS
  IM_IDIOMA,           // MENU_LANG
  IM_PIN_PROPRIO,      // MENU_OWN_PIN
  IM_CALIBRAR,         // MENU_TOUCH_CAL
  IM_LICENCA,          // MENU_LICENSE
  IM_STATUS,           // MENU_STATUS
  IM_ALINHAR,          // MENU_DISPLAY_OFFSET
  IM_USUARIOS,         // MENU_USERS
  IM_SEGURANCA_PIN,    // MENU_PIN_POLICY
  IM_MODO_CONFIG,      // MENU_SETUP_AP (not in the wired build)
  IM_DATA_HORA,        // MENU_CLOCK
  IM_N
};
constexpr int MENU_ITENS_MAX = IM_N;

struct VmMenu {
  uint8_t n;                          // items listed: already filtered by the account's bits
  uint8_t pagina;                     // 0-based; four items to a page
  uint8_t itens[MENU_ITENS_MAX];      // ItemMenu, in the order they are shown
  char conta[32];                     // the account at the panel ("" none), "Conta: maria" under the list
};
// Targets: AlvoLista. LST_LINHA's indice is the position in itens[ ].
void desenharMenu( Pintor &p, const VmMenu &vm );
int alvosMenu( const VmMenu &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Tema (MODE_SETTINGS_THEMES)                                          */
/* ------------------------------------------------------------------ */

enum EscolhaTema : uint8_t { TM_CLARO, TM_ESCURO, TM_AUTOMATICO };

struct VmTemas {
  uint8_t atual;                      // EscolhaTema: the one with the mark
};
// Targets: AlvoLista. LST_LINHA's indice is the EscolhaTema it applies.
void desenharTemas( Pintor &p, const VmTemas &vm );
int alvosTemas( const VmTemas &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Idioma (MODE_SETTINGS_LANG)                                          */
/* ------------------------------------------------------------------ */

enum EscolhaIdioma : uint8_t { ID_INGLES, ID_PACOTE };

struct VmIdioma {
  uint8_t atual;                      // EscolhaIdioma: the one with the mark
  char pacote[32];                    // the loaded pack's name as it reads ("Português (Brasil)"); "" when none
};
// Targets: AlvoLista. LST_LINHA's indice is the EscolhaIdioma it applies.
void desenharIdioma( Pintor &p, const VmIdioma &vm );
int alvosIdioma( const VmIdioma &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
