#pragma once
// The PIN keypad's layout, as pure arithmetic over PinKeypad.h: which of the six
// layouts of telas/IdentidadePin.h today's grid is. The keypad's touch no longer
// moves a point to the old layout (ua::pinPontoAntigo( ) went in S9 C2, 2026-10-21):
// each target now runs the core's keypad action (DisplayManager_Actions.h).
#include "../../PinKeypad.h"
#include "telas/IdentidadePin.h"

namespace ua {

// The new keypad of today's grid: the dealt layouts by their card count (PinKb::GRIDS),
// the ordered ones by the alphabet (PinKb::NUM_GRID, GROUP_GRID).
inline tela::PinTeclado pinTecladoDe( bool ordenado, bool alfanumerico, const PinKb::Grid &g ) {
  using tela::PinTeclado;
  if ( ordenado ) return alfanumerico ? PinTeclado::GRUPOS : PinTeclado::NUMERICO;
  switch ( g.keys ) {
    case 6: return PinTeclado::CARTOES6;
    case 12: return PinTeclado::CARTOES12;
    case 18: return PinTeclado::CARTOES18;
    default: return PinTeclado::CARTOES4;
  }
}

}  // namespace ua
