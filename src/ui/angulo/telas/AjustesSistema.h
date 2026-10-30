#pragma once
// The two settings of the device itself that the menu opens (ESPEC_UI_A.md
// §6.3): the PIN policy (MODE_SETTINGS_PIN_POLICY) and the clock
// (MODE_SET_CLOCK, also the question the boot asks when there is no network).
#include "Telas.h"

namespace ua {
namespace tela {

/* ------------------------------------------------------------------ */
/* Segurança do PIN (MODE_SETTINGS_PIN_POLICY)                          */
/* ------------------------------------------------------------------ */

// The three settings of the policy, as the old screen edited them on a copy.
struct VmPoliticaPin {
  uint8_t minimo;              // the minimum length being edited: piso..teto
  uint8_t piso;                // the lowest minimum (4)
  uint8_t teto;                // the longest PIN the chosen keypad allows (maxLenFor: 16, 12 or 8)
  uint8_t teclado;             // 1 ordered, 2 six cards (18 with A-Z), 3 four cards (12 with A-Z)
  uint8_t alfabeto;            // 0 "0-9", 1 "0-9 e A-Z"
};

// `indice` of POLPIN_TECLADO is the keypad the segment sets (1..3, the old values),
// of POLPIN_CARACTERES the alphabet (0 or 1). The segment already chosen is a
// target too (tapping it changes nothing). POLPIN_MENOS and POLPIN_MAIS stop at
// piso and teto and are then disabled and not targets. POLPIN_VOLTAR and
// POLPIN_CANCELAR both leave without saving (the old SAIR).
enum AlvoPoliticaPin : uint8_t {
  POLPIN_NADA,
  POLPIN_VOLTAR,
  POLPIN_MENOS,
  POLPIN_MAIS,
  POLPIN_TECLADO,
  POLPIN_CARACTERES,
  POLPIN_SALVAR,
  POLPIN_CANCELAR
};
void desenharPoliticaPin( Pintor &p, const VmPoliticaPin &vm );
int alvosPoliticaPin( const VmPoliticaPin &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Data e hora (MODE_SET_CLOCK)                                         */
/* ------------------------------------------------------------------ */

struct VmRelogio {
  bool boot;                   // asked at boot, with no network: "Pular" and the hint
  uint8_t dia, mes;
  uint16_t ano;
  uint8_t hora, minuto;
};

// The five columns, left to right: day, month, year, hour, minute: `indice` of
// RELOG_MAIS (the arrow above the value) and RELOG_MENOS (below) is the column
// 0..4. The left footer button is RELOG_PULAR at boot and RELOG_VOLTAR
// otherwise (the bar has no "Voltar": the footer has it); RELOG_SALVAR is the
// old SALVAR.
enum AlvoRelogio : uint8_t { RELOG_NADA, RELOG_MAIS, RELOG_MENOS, RELOG_VOLTAR, RELOG_PULAR, RELOG_SALVAR };
constexpr int RELOG_COLUNAS = 5;
void desenharRelogio( Pintor &p, const VmRelogio &vm );
int alvosRelogio( const VmRelogio &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
