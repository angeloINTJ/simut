// The touch hit test of the Ângulo screens (ua::alvoEm( ), src/ui/angulo/telas/Telas.cpp), checked on the host.
// Run by tools/tft_ui/test_alvos.py, which also builds mutants of the hit test and requires each one to fail
// at least one case below. The numbers are literals on purpose: they pin the design values (a slop of 12 px,
// counted in pixels from the touch to the nearest pixel of the box, so the first pixel outside is 1 away), and
// a change of the slop has to change them here and in tools/tft_ui/ESPEC_UI_A.md on purpose.
#include <initializer_list>
#include <stdio.h>

#include "Telas.h"

namespace {

int casos = 0, falhas = 0;

struct Toque { int16_t x, y; int esperado; };   // esperado: the tipo of the target that must answer, 0 for none

ua::Alvo caixa( int16_t x, int16_t y, int16_t w, int16_t h, uint8_t tipo ) { return ua::Alvo{ x, y, w, h, tipo, -1 }; }

// One case: every touch of the table must be answered by the target it names (0: by nobody). A case is one line
// when it holds and one line per touch that does not.
void caso( const char *nome, const char *o_que, const ua::Alvo *alvos, int n, std::initializer_list<Toque> toques ) {
  casos++;
  int erros = 0;
  for ( const Toque &t : toques ) {
    const ua::Alvo *a = ua::alvoEm( alvos, n, t.x, t.y );
    const int deu = a ? a->tipo : 0;
    if ( deu == t.esperado ) continue;
    printf( "FAIL %s: (%d,%d) was answered by %d, expected %d\n", nome, t.x, t.y, deu, t.esperado );
    erros++;
  }
  if ( erros ) falhas++;
  else printf( "ok   %s: %s (%d touch(es))\n", nome, o_que, (int)toques.size( ) );
}

}  // namespace

int main( ) {
  // 1. Nothing listed, nothing answers: a null list and an empty one, at the corners, the middle and past the glass.
  {
    casos++;
    const ua::Alvo vazio[1] = { };
    bool ok = true;
    const int16_t pontos[5][2] = { { 0, 0 }, { 160, 120 }, { 319, 239 }, { -5, -5 }, { 400, 300 } };
    for ( const auto &p : pontos ) ok = ok && !ua::alvoEm( nullptr, 0, p[0], p[1] ) && !ua::alvoEm( vazio, 0, p[0], p[1] );
    if ( ok ) printf( "ok   empty list: nothing answers, null list or empty (5 touch(es))\n" );
    else { printf( "FAIL empty list: something answered\n" ); falhas++; }
  }

  // The box used by most cases: 40 x 40 at (100,100), pixels 100..139 on each axis.
  const ua::Alvo um[1] = { caixa( 100, 100, 40, 40, 1 ) };

  // 2. Inside a box: as before, edge pixels included.
  caso( "inside", "the box's four corner pixels and its middle answer", um, 1,
        { { 100, 100, 1 }, { 139, 100, 1 }, { 100, 139, 1 }, { 139, 139, 1 }, { 120, 120, 1 } } );

  // 3. Two boxes overlap: the last one listed answers where they meet (it is drawn on top), the first where only it is.
  {
    const ua::Alvo ab[2] = { caixa( 100, 100, 60, 60, 1 ), caixa( 130, 130, 60, 60, 2 ) };
    const ua::Alvo ba[2] = { ab[1], ab[0] };
    caso( "overlap", "the last listed answers where two boxes meet, the first where only it reaches", ab, 2,
          { { 140, 140, 2 }, { 110, 110, 1 }, { 180, 180, 2 }, { 159, 159, 2 } } );
    caso( "overlap, reversed list", "the last listed answers again after the list is reversed", ba, 2,
          { { 140, 140, 1 }, { 110, 110, 1 }, { 180, 180, 2 } } );
  }

  // 4. Inside a big box beats a nearer small one outside it: the card (y 60..189) answers its own last row, and
  //    below it the card is the nearer until the button (y 196..) is.
  {
    const ua::Alvo cartao_e_botao[2] = { caixa( 8, 60, 304, 130, 1 ), caixa( 8, 196, 36, 40, 2 ) };
    caso( "inside beats nearer", "a touch inside a box answers it even 7 px from another box", cartao_e_botao, 2,
          { { 20, 189, 1 }, { 20, 190, 1 }, { 20, 192, 1 }, { 20, 193, 2 }, { 20, 195, 2 } } );
  }

  // 5. Outside an edge: 12 px away is the target, 13 px is nothing. All four edges.
  caso( "edge, right", "12 px past the last pixel answers, 13 px does not", um, 1,
        { { 140, 120, 1 }, { 151, 120, 1 }, { 152, 120, 0 }, { 153, 120, 0 } } );
  caso( "edge, left", "12 px before the first pixel answers, 13 px does not", um, 1,
        { { 99, 120, 1 }, { 88, 120, 1 }, { 87, 120, 0 }, { 86, 120, 0 } } );
  caso( "edge, top", "12 px above the first row answers, 13 px does not", um, 1,
        { { 120, 99, 1 }, { 120, 88, 1 }, { 120, 87, 0 }, { 120, 86, 0 } } );
  caso( "edge, bottom", "12 px under the last row answers, 13 px does not", um, 1,
        { { 120, 140, 1 }, { 120, 151, 1 }, { 120, 152, 0 }, { 120, 153, 0 } } );

  // 6. The dead band between two targets (8 px, pixels 50..57): each half goes to the nearer, whichever is listed first.
  {
    const ua::Alvo ab[2] = { caixa( 10, 100, 40, 40, 1 ), caixa( 58, 100, 40, 40, 2 ) };
    const ua::Alvo ba[2] = { ab[1], ab[0] };
    const std::initializer_list<Toque> faixa = { { 50, 120, 1 }, { 52, 120, 1 }, { 53, 120, 1 },
                                                 { 54, 120, 2 }, { 55, 120, 2 }, { 57, 120, 2 } };
    caso( "dead band", "each half of the gap goes to the nearer target", ab, 2, faixa );
    caso( "dead band, reversed list", "the same answers when the list is reversed", ba, 2, faixa );
  }

  // 7. A gap wider than twice the slop leaves a middle nobody answers.
  {
    const ua::Alvo ab[2] = { caixa( 0, 100, 40, 40, 1 ), caixa( 100, 100, 40, 40, 2 ) };
    caso( "wide gap", "12 px from either answers, 13 px from both does not", ab, 2,
          { { 51, 120, 1 }, { 52, 120, 0 }, { 70, 120, 0 }, { 87, 120, 0 }, { 88, 120, 2 } } );
  }

  // 8. A corner is Euclidean: a point 9 px out on both axes is 12.7 px away, not 9. The circle of 12 px includes
  //    (12,0) and (8,8) and leaves out (12,1), (9,8) and (9,9).
  caso( "corner, bottom right", "the slop is a distance, not a margin on each axis", um, 1,
        { { 151, 139, 1 }, { 151, 140, 0 }, { 147, 147, 1 }, { 148, 147, 0 }, { 147, 148, 0 }, { 148, 148, 0 },
          { 146, 147, 1 } } );
  caso( "corner, top left", "the same at the opposite corner", um, 1,
        { { 88, 100, 1 }, { 88, 99, 0 }, { 92, 92, 1 }, { 91, 91, 0 }, { 91, 92, 0 }, { 92, 91, 0 } } );
  caso( "corner, other two", "and at the two others", um, 1,
        { { 147, 92, 1 }, { 148, 91, 0 }, { 92, 147, 1 }, { 91, 148, 0 } } );

  // 9. The nearest by distance, not the first within the slop and not the nearer on one axis: A is 8 px out on both
  //    axes (distance 11.3, 8 on each axis), B is 10 px out on one (distance 10, 10 on that axis).
  {
    const ua::Alvo ab[2] = { caixa( 60, 60, 33, 33, 1 ), caixa( 80, 110, 40, 40, 2 ) };
    const ua::Alvo ba[2] = { ab[1], ab[0] };
    caso( "nearest", "B at 10 px beats A at 11.3 px, A being listed first", ab, 2, { { 100, 100, 2 } } );
    caso( "nearest, reversed list", "and with B listed first", ba, 2, { { 100, 100, 2 } } );
  }

  // 10. A tie goes to the first listed target (gap of 9 px, pixels 50..58: 54 is 5 px from each).
  {
    const ua::Alvo ab[2] = { caixa( 10, 100, 40, 40, 1 ), caixa( 59, 100, 40, 40, 2 ) };
    const ua::Alvo ba[2] = { ab[1], ab[0] };
    caso( "tie", "the middle of the gap goes to the first listed", ab, 2, { { 54, 120, 1 } } );
    caso( "tie, reversed list", "and to the other one when the list is reversed", ba, 2, { { 54, 120, 2 } } );
  }

  // 11. A target left out of the list (a disabled control) answers nothing: the graph's footer without its next
  //     arrow, whose place (x 148..183) starts 9 px from the last chip's edge (139) and has its middle 27 px away.
  {
    const ua::Alvo rodape[2] = { caixa( 8, 196, 36, 40, 1 ), caixa( 100, 196, 40, 40, 2 ) };
    caso( "left out", "a box that is not listed answers nothing; beyond the slop of the others", rodape, 2,
          { { 170, 216, 0 }, { 183, 235, 0 }, { 8, 216, 1 } } );
  }

  // 12. The touch has no edge at the glass: a calibration that overshoots by a few pixels still reaches the corner box.
  {
    const ua::Alvo cantos[2] = { caixa( 0, 0, 40, 28, 1 ), caixa( 280, 0, 40, 28, 2 ) };
    caso( "beyond the glass", "-3 and 322 are 3 px from the boxes at the edges", cantos, 2,
          { { -3, 5, 1 }, { 322, 5, 2 }, { -12, -12, 0 }, { 332, 5, 0 } } );
  }

  // 13. Far apart, with extreme coordinates: the squares must not wrap into a false hit.
  {
    const ua::Alvo longe[1] = { caixa( 32700, 32700, 67, 67, 1 ) };
    caso( "extreme", "a touch at -32768 is not turned into a hit by the arithmetic", longe, 1,
          { { -32768, -32768, 0 }, { -32768, 32700, 0 }, { 32700, -32768, 0 }, { 32760, 32760, 1 } } );
  }

  printf( "%s: %d case(s), %d failure(s)\n", falhas ? "FAIL" : "OK", casos, falhas );
  return falhas ? 1 : 0;
}
