#pragma once
// The PIN keypad's glue, as pure arithmetic over PinKeypad.h so that the host can
// hold it to the old hit tests (DisplayManager_Users.cpp handleTouchPanelV24( )):
// which of the six layouts a grid is, and, for a target of the new keypad
// (telas/IdentidadePin.h), the point of the OLD layout that means the same thing.
// MODE_AUTH is a Translation (ESPEC_UI_A.md §4): the glue writes that point into
// the touch and the old branch runs on it, unchanged.
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

// A point of the old layout that no key and no footer button owns: with a group
// popup open the old handler closes it on any point that is not one of its keys
// (popupKeyAt( ) answers -1 there), and nothing else happens. y 50 is above the
// footer and above every key (the first row starts at y 70).
constexpr int16_t PIN_NEUTRO_X = 160, PIN_NEUTRO_Y = 50;

// The centre of the old element target `a` stands for, in x and y; false when it
// stands for none (an index the old layout does not have). `popup` is the open
// group (_pinPopup), -1 for none. Cancelar answers at the old SAIR (FOOT_EXIT),
// Apagar at the old backspace (FOOT_BACK), Entrar at the old ENTRAR (FOOT_OK): all
// three zones are decided by x alone once y is in the footer.
inline bool pinPontoAntigo( const PinKb::Grid &g, int popup, const Alvo &a, int16_t &x, int16_t &y ) {
  using namespace PinKb;
  switch ( a.tipo ) {
    case tela::PIN_CANCELAR:
      x = FOOT_EXIT_X + FOOT_EXIT_W / 2; y = FOOT_Y + FOOT_H / 2;
      return true;
    case tela::PIN_APAGAR:
      x = FOOT_BACK_X + FOOT_BACK_W / 2; y = FOOT_Y + FOOT_H / 2;
      return true;
    case tela::PIN_ENTRAR:
      x = FOOT_OK_X + FOOT_OK_W / 2; y = FOOT_Y + FOOT_H / 2;
      return true;
    case tela::PIN_TECLA: {
      const int k = a.indice;
      if ( k < 0 || k >= (int)g.keys ) return false;
      x = (int16_t)( keyX( g, k ) + g.w / 2 ); y = (int16_t)( keyY( g, k ) + g.h / 2 );
      return true;
    }
    case tela::PIN_POPUP: {
      // popupKeyAt( )'s arithmetic, reversed: row r holds popupRowKeys( n, r ) keys, centred.
      if ( popup < 0 || popup >= GROUP_COUNT ) return false;
      const int n = (int)strlen( GROUP_CHARS[popup] );
      const int j = a.indice;
      int base = 0;
      for ( int r = 0; r < popupRows( n ); r++ ) {
        const int m = popupRowKeys( n, r );
        if ( j >= base && j < base + m ) {
          x = (int16_t)( popupRowX0( m ) + ( j - base ) * ( POP_KEY_W + POP_GAP ) + POP_KEY_W / 2 );
          y = (int16_t)( popupRowY( n, r ) + POP_KEY_H / 2 );
          return true;
        }
        base += m;
      }
      return false;
    }
    default:
      return false;
  }
}

}  // namespace ua
