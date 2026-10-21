#pragma once
// The Ângulo build's glue between the DisplayManager and the screens of
// src/ui/angulo/telas/ (pico_wiretft_angulo only; src/ui/UiDriver.h includes this
// under SIMUT_UI_ANGULO, before class DisplayManager, whose members use its types).
//
// Each pass of Core 1 paints in DisplayManager::uaQuadro( ), which uiPass( ) calls
// after the timers and the repaint requests: the screen of the
// current mode fills its view model from the state into the one shared buffer,
// the bytes are hashed, and only a frame that would look different is painted,
// strip by strip, through the canvas the old screens used. The touch targets
// of the painted frame are kept, so a touch lands on what is on the glass.
#include <stdint.h>
#include <string.h>

namespace ua {
struct Tema;
class Pintor;
struct Alvo;
struct VmStatus;
}  // namespace ua

// One frame of a screen: its view model and how to paint and touch it.
struct UaQuadro {
  const void *vm = nullptr;
  uint16_t tam = 0;
  void ( *desenhar )( ua::Pintor &p, const void *vm ) = nullptr;
  int ( *alvos )( const void *vm, ua::Alvo *out ) = nullptr;
};

// What a group's touch handler did with a touch.
enum class UaToque : uint8_t {
  NAO_E_MEU,   // the mode is not this group's
  TRATADO,             // done here, and the frame may have changed: the pass repaints
  TRATADO_SEM_QUADRO,  // done here, the frame is unchanged: nothing to repaint (the update screen's touch)
};

// The view model of the frame being built. The largest one is the chart's
// (200 buckets); every builder asserts that its type fits.
constexpr uint16_t UA_VM_MAX = 4096;
extern uint8_t g_uaVm[UA_VM_MAX];

// The shared buffer as a zeroed T (a view model is hashed byte for byte, so
// padding and unused tails must be zero).
template <class T> T &uaVm( ) {
  static_assert( sizeof( T ) <= UA_VM_MAX, "view model larger than UA_VM_MAX" );
  T *v = reinterpret_cast<T *>( g_uaVm );
  memset( static_cast<void *>( v ), 0, sizeof( T ) );
  return *v;
}

// Points q at view model vm of type T and at its screen's functions.
template <class T, void ( *D )( ua::Pintor &, const T & ), int ( *A )( const T &, ua::Alvo * )>
void uaUsar( UaQuadro &q, const T &vm ) {
  q.vm = &vm;
  q.tam = sizeof( T );
  q.desenhar = []( ua::Pintor &p, const void *v ) { D( p, *static_cast<const T *>( v ) ); };
  q.alvos = []( const void *v, ua::Alvo *o ) { return A( *static_cast<const T *>( v ), o ); };
}

// What GET /api/ui reports (src/ui/angulo/WebManager_UiA.cpp): the frame on
// the glass and its touch targets, for the bench to touch what is drawn.
struct UaAlvoBruto { int16_t x, y, w, h; uint8_t tipo; int16_t indice; };
constexpr int UA_MAX_ALVOS = 48;   // ua::MAX_ALVOS (asserted in DisplayManager_UiA.cpp)
struct UaEstado {
  uint8_t modo;         // the mode whose frame is on the glass
  uint8_t modoAtual;    // _uiMode now
  bool escuro;          // the palette of that frame
  uint32_t hash;        // its view model's hash
  uint32_t pinturas;    // frames painted since boot
  uint32_t ms, msMax;   // the last paint's duration and the longest one
  uint8_t faixas;       // strips the last paint pushed (0..12)
  // The dashboard's own state, which the panel saves (reserved[52..53]): the
  // bench puts back what a walk changes. nav[i] is the slot of button i, or
  // UA_NAV_CFG / UA_NAV_PAGINA / UA_NAV_NADA.
  int8_t selecionado, cima;
  bool fixado, minmaxCima, minmaxBaixo;
  int8_t nav[5];
  // The chart's window (GraphDataPackage::tsCutoff, tsEnd) while the frame is
  // the chart or its detail, 0 otherwise: a chart opened anew must end now.
  uint32_t janelaInicio, janelaFim;
  uint8_t nAlvos;
  UaAlvoBruto alvos[UA_MAX_ALVOS];
};
constexpr int8_t UA_NAV_NADA = -1, UA_NAV_CFG = -2, UA_NAV_PAGINA = -3;
// Written by the dashboard's glue each time it builds its frame (Core 1).
extern volatile int8_t g_uaNav[5];

// Latin-1 (what tr( ) and the old fonts use) to UTF-8 (what the kit draws).
void uaLatin1ParaUtf8( const char *latin1, char *out, int cap );
