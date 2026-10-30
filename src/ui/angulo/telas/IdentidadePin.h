#pragma once
// The PIN keypad of the identity group (MODE_AUTH, ESPEC_UI_A.md §6.4): the
// plan's P1, P2 and P3 dressed with today's model (inventario/pin_usuarios_alarmes.md
// §3): six layouts, a popup for the alphanumeric groups, and the states of the
// lockout ladder. Key k of this screen is key k of today's layout (PinKb::keyAt( )'s
// order: left to right, top to bottom), and since S9 C2 (2026-10-21) each target runs
// the core's keypad action (DisplayManager_Actions.h) under the original's zone: the
// shuffle after each tap and the lockout stay the core's.
#include "IdentidadeComum.h"

namespace ua {
namespace tela {

// The six layouts (PinKb::GRIDS, NUM_GRID and GROUP_GRID):
//   CARTOES4   0-9, 4 cards of 3 glyphs (2x2), 2 decoys   the factory keypad
//   CARTOES6   0-9, 6 cards of 2 glyphs (3x2), 2 decoys
//   NUMERICO   0-9 ordered, 3x4: 1..9, then Entrar, 0 and Apagar (the old pad's two empty cells)
//   CARTOES12  0-9 A-Z, 12 cards of 3 glyphs (4x3), no decoys
//   CARTOES18  0-9 A-Z, 18 cards of 2 glyphs (6x3), no decoys
//   GRUPOS     0-9 A-Z ordered: 9 group keys "0-9", "ABC" .. "WXYZ" and a popup with the group's characters
enum class PinTeclado : uint8_t { CARTOES4, CARTOES6, NUMERICO, CARTOES12, CARTOES18, GRUPOS };

// What the PIN is for: proving it (the title names the account), or choosing it
// (always an ordered layout) in two passes.
enum class PinFase : uint8_t { IDENTIFICAR, DEFINIR, CONFIRMAR };

// LIVRE            keys alive
// ERRO             a refusal, said in the danger colour; the keys stay alive
// AGUARDANDO       the PIN went to Core 0: keys and footer dimmed, Cancelar alive
// ESPERA           the lockout ladder's wait (esperaS seconds): keys dimmed, Cancelar alive
// BLOQUEIO_CONTA   the account is out of tries until the device restarts: only Cancelar
// BLOQUEIO_PAINEL  the panel is out until the device restarts: only Cancelar
enum class PinEstado : uint8_t { LIVRE, ERRO, AGUARDANDO, ESPERA, BLOQUEIO_CONTA, BLOQUEIO_PAINEL };
enum class PinErro : uint8_t { INCORRETO, CURTO, DIFERENTES };

constexpr int PIN_CARTOES_MAX = 18;     // PinKb::KEYS_MAX
constexpr int PIN_GLIFOS_MAX = 3;       // PinKb::SLOTS_MAX

struct VmPin {
  PinTeclado teclado;
  PinFase fase;
  PinEstado estado;
  PinErro erro;                // only with estado == ERRO
  bool alfanumerico;           // 0-9 A-Z: the texts say "caracteres", not "dígitos"
  uint8_t digitados;           // characters entered so far
  uint8_t minimo;              // places always shown: PinKb::entryMinLen( ) (4 proving, the policy's choosing)
  uint8_t teto;                // the keypad's length ceiling (PinKb::maxLenFor: 8, 12 or 16)
  int8_t pressionada;          // key under the finger (the index of TECLA), -1 none
  int8_t popup;                // GRUPOS: the open group 0..8, -1 none
  uint16_t esperaS;            // ESPERA: seconds left
  char conta[ident::NOME];     // whose PIN: the title proving it, "para <conta>" choosing another's; "" when it is the session's own
  // What the device dealt on each card, exactly the glyphs of _pinKeyChars[k] with their decoys
  // ("790", "!2#"): the screen draws every glyph alike, same size and same ink, because a card
  // must not say how many real characters it holds. Used by the CARTOES* layouts only.
  char cartoes[PIN_CARTOES_MAX][PIN_GLIFOS_MAX + 1];
};

// TECLA: indice = the key's number in today's layout. For NUMERICO the cell 9
// is ENTRAR and the cell 11 is APAGAR (empty in the old pad), so TECLA covers
// cells 0..8 and 10 (the 0). POPUP: indice = the character's place in the
// group, rows in reading order, as PinKb::popupKeyAt( ). With a popup open the
// popup's keys are the only targets: a tap anywhere else closes it. Under
// AGUARDANDO, ESPERA and the lockouts only CANCELAR is a target.
enum AlvoPin : uint8_t { PIN_NADA, PIN_TECLA, PIN_POPUP, PIN_APAGAR, PIN_ENTRAR, PIN_CANCELAR };
void desenharPin( Pintor &p, const VmPin &vm );
int alvosPin( const VmPin &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
