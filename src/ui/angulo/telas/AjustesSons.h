#pragma once
// The sound settings (MODE_SETTINGS_SOUNDS) and the choice of a melody, which
// the old screen kept inside the same mode (_inMelodySelect) and here is a
// screen of its own (ESPEC_UI_A.md §6.3). Both edit a copy: the melody screen
// hands its choice back to the sounds screen, and nothing is saved until
// "Salvar sons".
#include "Telas.h"

namespace ua {
namespace tela {

/* ------------------------------------------------------------------ */
/* Sons (MODE_SETTINGS_SOUNDS)                                          */
/* ------------------------------------------------------------------ */

// The switches of the list, in the order the screen shows them (the old list
// had the two volumes first and the sounds from index 2: the glue maps).
// Page 1 holds SOM_TOQUE..SOM_ALARME, page 2 SOM_ATENCAO..SOM_MUDO, page 3 the
// two volumes.
enum LinhaSom : uint8_t { SOM_TOQUE, SOM_CONFIRMACAO, SOM_ERRO, SOM_ALARME, SOM_ATENCAO, SOM_WEB, SOM_MUDO, SOM_N };
constexpr int SOM_COM_MELODIA = 5;     // SOM_TOQUE..SOM_ATENCAO have a melody to choose (the old _melSelectType 0..4)
constexpr int MELODIAS_POR_SOM = 6;    // the old selector's six variants of each
constexpr int SONS_PAGINAS = 3;

struct VmSons {
  uint8_t pagina;                      // 0-based: 0..SONS_PAGINAS - 1
  uint8_t foco;                        // page 3: the slider touched last, 0 system, 1 alarm
  bool ligado[SOM_N];                  // each switch, by LinhaSom; SOM_MUDO is "Silenciar tudo"
  uint8_t melodia[SOM_COM_MELODIA];    // the melody of each sound that has one, 0..MELODIAS_POR_SOM - 1; shown while it is on
  uint8_t volumeSistema;               // 0..100, in steps of 10
  uint8_t volumeAlarme;                // 0..100, in steps of 10
};

// SONS_INTERRUPTOR and SONS_LINHA carry the LinhaSom as `indice`; SONS_MENOS and
// SONS_MAIS the slider (0 system, 1 alarm).
//  - A sound that is on and has a melody shows the melody's name and a chevron:
//    SONS_LINHA is the row up to the switch (open the choice of the melody),
//    SONS_INTERRUPTOR the switch and the margin around it.
//  - Any other row (a sound that is off, web access, mute all) is one
//    SONS_INTERRUPTOR over the whole row: tapping anywhere flips it. Turning
//    SOM_MUDO on is what asks for the confirmation (the glue opens it).
//  - A volume button at the end of its range is drawn disabled and is not a
//    target; so is a pager arrow with no page to go to.
//  - SONS_VOLTAR leaves without saving (the old SAIR). There is no Cancelar
//    button: its place in the footer is the pager's, since a page of four rows
//    leaves no room for a pager row above the footer.
enum AlvoSons : uint8_t {
  SONS_NADA,
  SONS_VOLTAR,
  SONS_INTERRUPTOR,
  SONS_LINHA,
  SONS_MENOS,
  SONS_MAIS,
  SONS_PAG_ANTERIOR,
  SONS_PAG_PROXIMA,
  SONS_SALVAR
};
void desenharSons( Pintor &p, const VmSons &vm );
int alvosSons( const VmSons &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Melodia (the old _inMelodySelect)                                    */
/* ------------------------------------------------------------------ */

struct VmMelodia {
  uint8_t som;                         // LinhaSom, SOM_TOQUE..SOM_ATENCAO: whose melody this is
  uint8_t escolhida;                   // 0..MELODIAS_POR_SOM - 1: the one with the mark
};

// The six melodies are laid out two by two, three rows: six rows of 40 px do not
// fit above the footer, and six of 24 px are too thin for a finger, or a page
// would hide half of what there is to compare. MELOD_ITEM's `indice` is the
// melody (0..5, row by row); tapping it chooses it and the glue plays the
// preview. MELOD_VOLTAR and MELOD_CANCELAR both go back to the sounds without
// the choice (the old SAIR); MELOD_USAR takes it (the old SALVAR).
enum AlvoMelodia : uint8_t { MELOD_NADA, MELOD_VOLTAR, MELOD_ITEM, MELOD_USAR, MELOD_CANCELAR };
void desenharMelodia( Pintor &p, const VmMelodia &vm );
int alvosMelodia( const VmMelodia &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
