// test_confirma_ap_main.cpp -- the Ângulo glue's setup-network confirmation, RUN on the host (S10c, D7). Built and run by tools/tft_ui/test_confirma_ap.py.
//
// DisplayManager_UiA.cpp cannot be linked here as a file (it is a DisplayManager, with two displays, a touch panel and two cores behind it). What D7 added to it can be run, so
// the python side cuts it out, verbatim and without comments, and this file compiles it against a stand-in (the manner of test_temas_glue_main.cpp):
//   montar_caso.inc  the MODE_CONFIRM_AP case of uaMontarBase( ), the dispatcher the frame goes through;
//   toque_caso.inc   the MODE_CONFIRM_AP case of uaToqueBase( ), the dispatcher the touch goes through;
//   membros.inc      the one #if SIMUT_NET_WIFI block, with its #else: uaMontarConfirmaAp( ) and uaToqueConfirmaAp( ), the real bodies and the wired image's stubs;
//   decls.inc        the two declarations cut out of Members.h;
//   acao.inc         actStartAp( ) cut out of DisplayManager_Actions.h: the action Confirm runs, which pushes the event;
//   esperado.inc     the texts the frame must hold (the python side's constants, so that there is one source).
// The kit (Telas.cpp, Base.cpp, Angulo.cpp, Textos.cpp) is the real one. What the hardware does is the stand-in's: the touch gate (acceptTouch( ) answers what the check says and records the
// zone it was asked for), showSettingsMain( ), forceDashboard( ) and the event queue (pushUiEvent( )). -DSIMUT_NET_WIFI=1 builds a Wi-Fi image's branch, =0 the Wire's.
//
// What runs: the frame (the dialog's view model with the decided texts, the destructive weight, two targets, the body in at most four lines and none cut) and the touch (Cancel: showSettingsMain( )
// once behind the gate 0xC2 and no event; Confirm: EVT_START_AP once behind the gate 0xC3 and no mode change; nothing for no target; nothing when the gate refuses; any other target is Cancel); on the
// wired branch, the frame answers false and the touch NAO_E_MEU with no effect.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Telas.h"
#include "Base.h"
#include "UiA.h"
#include "Textos.h"   // UA_TXT( ): after the screens' headers, because its pragmas hold to the end of the unit

#ifndef SIMUT_NET_WIFI
#error "test_confirma_ap.py passes -DSIMUT_NET_WIFI=1 (a Wi-Fi image) or =0 (the wired one)"
#endif

alignas( 8 ) uint8_t g_uaVm[UA_VM_MAX];

#include "esperado.inc"

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The stand-in. The firmware's UiMode and UiEvent have more members; these are the names the pieces use.
enum UiMode : uint8_t { MODE_DASHBOARD, MODE_SETTINGS_MAIN, MODE_CONFIRM_AP, MODE_CONFIRM_MUTE_ALL };

struct UiEvent {
  enum Type : uint8_t { EVT_NONE, EVT_START_AP, EVT_USER_DEL };
  Type type;
  int id;
  int param;
};

class DisplayManager {
 public:
  UiMode _uiMode = MODE_CONFIRM_AP;
  bool porta = true;           // what acceptTouch( ) answers
  uint8_t zonas[8] = { };      // the zones it was asked for, in order
  int nZonas = 0;
  int nSettingsMain = 0, nForceDashboard = 0;
  UiEvent eventos[8] = { };
  int nEventos = 0;

  bool acceptTouch( uint8_t zona ) {
    if ( nZonas < 8 ) zonas[nZonas++] = zona;
    return porta;
  }
  void showSettingsMain( ) {
    nSettingsMain++;
    _uiMode = MODE_SETTINGS_MAIN;
  }
  void forceDashboard( ) {
    nForceDashboard++;
    _uiMode = MODE_DASHBOARD;
  }
  void pushUiEvent( const UiEvent &e ) {
    if ( nEventos < 8 ) eventos[nEventos++] = e;
  }
  void actStartAp( );

  bool uaMontarBase( UaQuadro &q ) {
    using namespace ua::tela;
    switch ( _uiMode ) {
      case MODE_CONFIRM_AP:   // the label is the glue's own; the cut holds the statements after it, none on the base
#include "montar_caso.inc"
      default:
        return false;
    }
  }
  UaToque uaToqueBase( const ua::Alvo *a, int16_t &, int16_t & ) {
    using namespace ua::tela;
    switch ( _uiMode ) {
      case MODE_CONFIRM_AP:   // the label is the glue's own; the cut holds the statements after it, none on the base
#include "toque_caso.inc"
      default:
        return UaToque::NAO_E_MEU;
    }
  }
#include "decls.inc"
};

#include "acao.inc"
#include "membros.inc"

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static int g_n = 0, g_falhas = 0;
#define CHECK( cond, ... ) do { g_n++; if ( !( cond ) ) { g_falhas++; printf( "FAIL (line %d): ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

static void testarQuadro( ) {
  using namespace ua;
  using namespace ua::tela;
  DisplayManager d;
  d._uiMode = MODE_CONFIRM_AP;
  UaQuadro q;
  const bool montou = d.uaMontarBase( q );
#if SIMUT_NET_WIFI
  CHECK( montou, "uaMontarBase( ) answered false for MODE_CONFIRM_AP: uaMontar( ) goes on to uaMontarPendente( ) and the glass says 'This screen (mode %d)...', not the title '%s'", (int)MODE_CONFIRM_AP, TITULO );
  if ( !montou ) return;
  const VmDialogo *vm = static_cast<const VmDialogo *>( q.vm );
  CHECK( q.vm == g_uaVm, "the frame does not point at the shared view-model buffer" );
  CHECK( q.tam == sizeof( VmDialogo ), "the frame's view model is %u bytes, a VmDialogo is %u: the dialog's screen was not used", (unsigned)q.tam, (unsigned)sizeof( VmDialogo ) );
  CHECK( q.desenhar != nullptr && q.alvos != nullptr, "the frame has no draw function or no targets function" );
  if ( q.vm != g_uaVm || q.tam != sizeof( VmDialogo ) || !q.alvos ) return;
  CHECK( !strcmp( vm->titulo, TITULO ), "title '%s', wanted '%s'", vm->titulo, TITULO );
  CHECK( !strcmp( vm->texto, CORPO ), "body '%s', wanted '%s'", vm->texto, CORPO );
  CHECK( !strcmp( vm->nao, CANCELA ), "Cancel label '%s', wanted '%s' (the existing key)", vm->nao, CANCELA );
  CHECK( !strcmp( vm->sim, CONFIRMA ), "Confirm label '%s', wanted '%s'", vm->sim, CONFIRMA );
  CHECK( vm->destrutivo, "the confirming button is not in the destructive weight (the mute confirmation's, DisplayManager_UiA.cpp)" );
  Alvo alvos[MAX_ALVOS];
  const int n = q.alvos( q.vm, alvos );
  CHECK( n == 2 && alvos[0].tipo == DLG_NAO && alvos[1].tipo == DLG_SIM, "targets: %d, wanted Cancel then Confirm (DLG_NAO, DLG_SIM)", n );
  // the fit, in the English the source holds (the three languages are host_telas.py's, from the packs): the body breaks into at most four lines and nothing is cut
  char linhas[4][96];
  const int nl = quebrar( vm->texto, tipo::corpo, 320 - 2 * PAD, &linhas[0][0], sizeof linhas[0], 4 );
  CHECK( nl >= 1 && nl <= 4, "the body breaks into %d lines, the dialog holds four", nl );
  char junto[256];
  size_t k = 0;
  junto[0] = 0;
  for ( int i = 0; i < nl && k < sizeof junto - 1; i++ ) k += (size_t)snprintf( junto + k, sizeof junto - k, "%s%s", i ? " " : "", linhas[i] );
  CHECK( !strcmp( junto, CORPO ), "the body, broken in %d lines, reads '%s': cut or changed", nl, junto );
#else
  CHECK( !montou, "the wired image answers true for MODE_CONFIRM_AP: it would claim a mode it cannot reach and draw a frame the Wire does not carry" );
  CHECK( q.vm == nullptr, "the wired image's stub touched the frame" );
#endif
}

static UaToque tocar( DisplayManager &d, bool nulo, uint8_t alvoTipo, bool porta ) {
  d._uiMode = MODE_CONFIRM_AP;
  d.porta = porta;
  ua::Alvo alvo;
  memset( &alvo, 0, sizeof alvo );
  alvo.tipo = alvoTipo;
  int16_t x = 0, y = 0;
  return d.uaToqueBase( nulo ? nullptr : &alvo, x, y );
}

static void testarToque( ) {
  using namespace ua::tela;
#if SIMUT_NET_WIFI
  {   // Cancel, the gate lets it through
    DisplayManager d;
    const UaToque r = tocar( d, false, DLG_NAO, true );
    CHECK( r == UaToque::TRATADO, "Cancel: the touch answered %d, wanted TRATADO", (int)r );
    CHECK( d.nZonas == 1 && d.zonas[0] == 0xC2, "Cancel: %d zone(s) asked, the first 0x%02X, wanted one: 0xC2", d.nZonas, d.zonas[0] );
    CHECK( d.nSettingsMain == 1, "Cancel: showSettingsMain( ) ran %d times, wanted once", d.nSettingsMain );
    CHECK( d.nEventos == 0, "Cancel: %d event(s) pushed, wanted none", d.nEventos );
    CHECK( d.nForceDashboard == 0, "Cancel: forceDashboard( ) ran %d times: Cancel goes back to Settings, not to the dashboard", d.nForceDashboard );
    CHECK( d._uiMode == MODE_SETTINGS_MAIN, "Cancel: the mode is %d, wanted MODE_SETTINGS_MAIN (%d)", (int)d._uiMode, (int)MODE_SETTINGS_MAIN );
  }
  {   // Confirm, the gate lets it through
    DisplayManager d;
    const UaToque r = tocar( d, false, DLG_SIM, true );
    CHECK( r == UaToque::TRATADO, "Confirm: the touch answered %d, wanted TRATADO", (int)r );
    CHECK( d.nZonas == 1 && d.zonas[0] == 0xC3, "Confirm: %d zone(s) asked, the first 0x%02X, wanted one: 0xC3", d.nZonas, d.zonas[0] );
    CHECK( d.nEventos == 1 && d.eventos[0].type == UiEvent::EVT_START_AP && d.eventos[0].id == 0, "Confirm: %d event(s), the first of type %d, wanted one EVT_START_AP", d.nEventos, (int)d.eventos[0].type );
    CHECK( d.nSettingsMain == 0 && d.nForceDashboard == 0, "Confirm: showSettingsMain( ) ran %d times and forceDashboard( ) %d, wanted neither", d.nSettingsMain, d.nForceDashboard );
    CHECK( d._uiMode == MODE_CONFIRM_AP, "Confirm: the panel changed the mode to %d itself: Core 0 answers the event with the boot terminal", (int)d._uiMode );
  }
  {   // no target
    DisplayManager d;
    const UaToque r = tocar( d, true, DLG_SIM, true );
    CHECK( r == UaToque::TRATADO, "no target: the touch answered %d, wanted TRATADO", (int)r );
    CHECK( d.nZonas == 0 && d.nEventos == 0 && d.nSettingsMain == 0 && d.nForceDashboard == 0 && d._uiMode == MODE_CONFIRM_AP, "no target: something ran (%d zones, %d events, %d, %d)", d.nZonas, d.nEventos, d.nSettingsMain, d.nForceDashboard );
  }
  {   // the gate refuses
    DisplayManager d;
    tocar( d, false, DLG_NAO, false );
    CHECK( d.nZonas == 1 && d.zonas[0] == 0xC2, "Cancel, gate refused: %d zone(s) asked, the first 0x%02X, wanted one: 0xC2", d.nZonas, d.zonas[0] );
    CHECK( d.nSettingsMain == 0 && d.nEventos == 0 && d.nForceDashboard == 0 && d._uiMode == MODE_CONFIRM_AP, "Cancel, gate refused: it acted anyway (%d, %d events, %d)", d.nSettingsMain, d.nEventos, d.nForceDashboard );
    DisplayManager e;
    tocar( e, false, DLG_SIM, false );
    CHECK( e.nZonas == 1 && e.zonas[0] == 0xC3, "Confirm, gate refused: %d zone(s) asked, the first 0x%02X, wanted one: 0xC3", e.nZonas, e.zonas[0] );
    CHECK( e.nEventos == 0 && e.nSettingsMain == 0 && e.nForceDashboard == 0 && e._uiMode == MODE_CONFIRM_AP, "Confirm, gate refused: it acted anyway (%d events, %d, %d)", e.nEventos, e.nSettingsMain, e.nForceDashboard );
  }
  {   // any other target is Cancel, as in the original's chain (UiTouchChain.h: a point outside Confirm is no Confirm)
    DisplayManager d;
    tocar( d, false, DLG_NADA, true );
    CHECK( d.nZonas == 1 && d.zonas[0] == 0xC2 && d.nSettingsMain == 1 && d.nEventos == 0, "another target: %d zone(s), the first 0x%02X, showSettingsMain( ) %d times, %d event(s): wanted Cancel", d.nZonas, d.zonas[0], d.nSettingsMain, d.nEventos );
  }
#else
  {   // the wired image: not its mode
    const bool alvos[3] = { false, false, true };   // nullptr, Cancel, Confirm
    for ( int i = 0; i < 3; i++ ) {
      DisplayManager d;
      const UaToque r = tocar( d, i == 0, i == 2 ? DLG_SIM : DLG_NAO, true );
      CHECK( r == UaToque::NAO_E_MEU, "wired, case %d: the touch answered %d, wanted NAO_E_MEU", i, (int)r );
      CHECK( d.nZonas == 0 && d.nEventos == 0 && d.nSettingsMain == 0 && d.nForceDashboard == 0, "wired, case %d: something ran (%d zones, %d events)", i, d.nZonas, d.nEventos );
    }
    (void)alvos;
  }
#endif
}

int main( ) {
  testarQuadro( );
  testarToque( );
  printf( "%s: %d checks, %d failed\n", SIMUT_NET_WIFI ? "wifi" : "wired", g_n, g_falhas );
  return g_falhas ? 1 : 0;
}
