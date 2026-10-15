#pragma once
// The two screens every other one leans on: the result of an action
// (MODE_PANEL_MESSAGE, the touch calibration's verdict) and the confirmation
// of something that cannot be undone from the panel (MODE_CONFIRM_MUTE_ALL,
// MODE_SETTINGS_USER_CONFIRM_DEL). No plan drew them: they are the Ângulo's
// dialog, in family A's chrome. MODE_CONFIRM_AP has no frame here: the wired
// Ângulo image cannot reach the mode (SettingsMenu.h). A Wi-Fi Ângulo image
// will need a VmDialogo case in uaMontarBase( ) and a touch handler that calls
// showSettingsMain( ) and actStartAp( ) (S6_PLANO.md section 5, item 7).
#include "Telas.h"

namespace ua {
namespace tela {

/* ------------------------------------------------------------------ */
/* Result of an action                                                  */
/* ------------------------------------------------------------------ */

struct VmMensagem {
  bool ok;               // positive (check) or the failure (x)
  char texto[96];        // what happened, one or two lines
  char detalhe[128];     // how to go on, up to three lines ("" none)
  char botao[24];        // "Entendi"
};
enum AlvoMensagem : uint8_t { MSG_NADA, MSG_BOTAO };
void desenharMensagem( Pintor &p, const VmMensagem &vm );
int alvosMensagem( const VmMensagem &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Confirmation                                                         */
/* ------------------------------------------------------------------ */

struct VmDialogo {
  char titulo[48];       // the question, sentence case
  char texto[192];       // what will happen, up to four lines
  char nao[24];          // the way back ("Voltar", "Cancelar")
  char sim[32];          // verb + object ("Excluir a conta")
  bool destrutivo;       // the confirming button in the danger colour
};
enum AlvoDialogo : uint8_t { DLG_NADA, DLG_NAO, DLG_SIM };
void desenharDialogo( Pintor &p, const VmDialogo &vm );
int alvosDialogo( const VmDialogo &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
