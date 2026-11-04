// The loading screen of the history chart. See Graficos.h.
#include "Graficos.h"
#include "GraficosComum.h"

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

using namespace graficos;

namespace {

// The plan's bar (60, 112, 200, 8) with the message under it. The moving
// segment is 56 px wide and the glue steps it by ESP5 (24 px) either way of the
// middle: the 144 px it can travel are three steps each side.
constexpr int16_t X_BARRA = 60, Y_BARRA = 112, W_BARRA = 200, H_BARRA_PROG = 8;
constexpr int16_t W_TRECHO = 56;
constexpr int16_t Y_MENSAGEM = 128;

// Steps from the middle for each phase: out to the right, back, out to the left, back.
constexpr int8_t kPasso[FASES_CARREGANDO] = { 0, 1, 2, 3, 2, 1, 0, -1, -2, -3, -2, -1 };
static_assert( W_BARRA - W_TRECHO == 6 * ESP5, "the segment travels three steps each side of the middle" );

}  // namespace

void desenharCarregando( Pintor &p, const VmCarregando &vm ) {
  const Tema &T = p.t( );
  char titulo[64];
  tituloComPeriodo( titulo, sizeof titulo, vm.sensor, vm.periodo, nullptr, 0, false );
  barraTitulo( p, titulo, nullptr, 0 );

  // The kit's progress bar (track and border) with a segment in the place of
  // the fill: nobody knows how far the read is, so nothing may look like a fraction.
  p.fillRoundRect( X_BARRA, Y_BARRA, W_BARRA, H_BARRA_PROG, RAIO_TOTAL, T.superficie2 );
  p.roundRect( X_BARRA, Y_BARRA, W_BARRA, H_BARRA_PROG, RAIO_TOTAL, T.linha );
  const int fase = vm.fase % FASES_CARREGANDO;
  const int16_t x = (int16_t)( X_BARRA + 3 * ESP5 + kPasso[fase] * ESP5 );
  p.fillRoundRect( x, Y_BARRA, W_TRECHO, H_BARRA_PROG, RAIO_TOTAL, T.acento );

  textoCentrado( p, UA_TXT( "Reading the history…" ), X_CENTRO, Y_MENSAGEM, H_CORPO, tipo::corpo, T.tinta2, 'c' );
}

int alvosCarregando( const VmCarregando &, Alvo * ) {
  // No branch of the old touch handler runs in this mode (MODE_GRAPH_LOADING
  // is not in handleTouch), so nothing here answers one either.
  return 0;
}

}  // namespace tela
}  // namespace ua
