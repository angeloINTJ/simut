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

// The screen is one: the mode (three segments), the accent (a row of swatches), the choice in words and one primary button. A tap on a
// segment or a swatch previews the choice on the glass (Core 1 keeps it and nothing is saved), "Aplicar tema" saves it and Back drops it
// (S10c, A'3; S10C_A_PLANO.md section 3).
enum EscolhaTema : uint8_t { TM_CLARO, TM_ESCURO, TM_AUTOMATICO, TM_N };
static_assert( static_cast<int>( TM_CLARO ) == static_cast<int>( MODO_CLARO ) && static_cast<int>( TM_ESCURO ) == static_cast<int>( MODO_ESCURO ) &&
                   static_cast<int>( TM_AUTOMATICO ) == static_cast<int>( MODO_AUTOMATICO ) && static_cast<int>( TM_N ) == MODO_AUTOMATICO + 1,
               "the segments are the kit's modes, in the kit's order" );

// The targets: AlvoLista's Back, then these. TT_MODO's indice is the EscolhaTema, TT_COR's the palette entry. A disabled "Aplicar tema" is
// listed as ALVO_INERTE (TT_NADA), so a tap on it is answered by no live target.
enum AlvoTema : uint8_t { TT_NADA = LST_NADA, TT_MODO = LST_PAG_PROXIMA + 1, TT_COR, TT_APLICAR };

struct VmTemas {
  uint8_t modo;            // EscolhaTema: the mode on show (the preview, or the saved one until the first tap)
  uint8_t acento;          // the palette entry on show
  int8_t salvo;            // the saved choice as ua::indiceDaEscolha( ) writes it: "Aplicar tema" has something to apply when it differs from ( modo, acento )
  uint8_t premidoTipo;     // the target under the finger: TT_NADA, TT_MODO, TT_COR or TT_APLICAR. The panel acts when the touch lands and holds no press, so the glue leaves
                           // it at TT_NADA (as the keypad's pressed key); the state is drawn all the same and the host draws it
  int8_t premidoIndice;    // its indice (-1 for TT_NADA and TT_APLICAR)
};

// The geometry, on the 4 px grid (S10C_A_PLANO.md section 3), and the fixed texts, here so that tools/tft_ui/test_temas.py measures the very strings and
// boxes the screen draws. The texts are pt-BR; Stage B translates them (the commit of A'3 lists each one's English).
constexpr int16_t TEMA_ALVO = 44;           // a finger's target: the segments' height, the swatches' box
constexpr int16_t TEMA_W_SEGMENTO = 96;
constexpr int16_t TEMA_H_LINHA = 20;        // the two lines of words, each in a box of this height
constexpr int16_t TEMA_H_APLICAR = 40;
constexpr int16_t TEMA_Y_SEGMENTOS = 44;
constexpr int16_t TEMA_Y_FRASE = 92;
constexpr int16_t TEMA_Y_AMOSTRAS = 120;
constexpr int16_t TEMA_Y_NOME = 168;
constexpr int16_t TEMA_Y_APLICAR = 192;
constexpr int16_t TEMA_H_ROTULO = 12;       // the label's box under the icon: a segment is ESP1, icon, ESP1, label, ESP1
constexpr int16_t TEMA_ANEL = 2;            // the selected swatch's ring, and the gap between the ring and the disc
// 9 pt regular, the size and weight the plan measured: "Automático" is 86 px in the 88 px of a segment (tools/tft_ui/test_temas.py, 2026-10-22). The bold of the kit's
// segmented control (tipo::rotulo) makes it 97 px, which does not fit.
constexpr Fonte TEMA_FONTE_SEGMENTO = tipo::corpo;
constexpr int16_t TEMA_W_ROTULO = TEMA_W_SEGMENTO - 2 * ESP1;   // the room of a segment's label

constexpr const char *TEMA_TITULO = "Tema";
constexpr const char *TEMA_MODOS[TM_N] = { "Claro", "Escuro", "Automático" };
constexpr IconeNome TEMA_ICONES[TM_N] = { IconeNome::SUN, IconeNome::MOON, IconeNome::SUN_MOON };
constexpr const char *TEMA_FRASES[TM_N] = { "Modo claro", "Modo escuro", "Modo automático: escuro das 19 às 7" };
// The palette's accents in its order (entry 0 is the stock one): the names the screen and the web list say. In lower case: a name follows a colon on the words line ("Cor de destaque: azul")
// and a comma in the catalogue ("Claro, azul"), and the Ângulo's sentence case (ANGULO.md section 7) writes it so (S10c, A'3b); tools/tft_ui/test_temas.py holds it.
constexpr const char *TEMA_CORES[] = { "verde-azulado", "petróleo", "azul", "índigo", "violeta", "magenta" };
constexpr int TEMA_CORES_N = sizeof TEMA_CORES / sizeof TEMA_CORES[0];
constexpr const char *TEMA_FMT_COR = "Cor de destaque: %s";
constexpr const char *TEMA_APLICAR_ROTULO = "Aplicar tema";

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
