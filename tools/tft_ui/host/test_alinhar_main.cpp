// test_alinhar_main.cpp -- the display alignment's preview and the glass it leaves, RUN on the host (2026-10-31). Built and run by tools/tft_ui/test_alinhar.py.
//
// The defect (v2.13.0 CHANGELOG, Known): the alignment screen puts its preview on the glass at once (actOffsetPreview( )), and only Voltar (actOffsetBack( )) puts the saved offset back.
// Every other exit (the 30 s idle return, the CLI's `screen *`, a theme change, an update) left the preview on the glass, and the next entry took the glass for the "saved" offset.
// DisplayManager cannot be linked here as a file (two cores, a panel, a touch controller, the flash). What decides the glass can be run, so the python side cuts the pieces out of the
// sources, verbatim and without comments, and this file compiles them against a stand-in (the manner of test_confirma_ap_main.cpp):
//   entrada.inc    showSettingsDisplayOffset( ), the alignment screen's entry (CalibrationCore.cpp)        carga.inc    loadDisplayOffset( ), the boot's load (CalibrationCore.cpp)
//   tique.inc      tickDisplayOffset( ), Core 1's check of the glass (CalibrationCore.cpp)                  acoes.inc    actOffsetUp( ) .. actOffsetApply( ) (DisplayManager_Actions.h)
//   ocioso.inc     the idle return, the no-touch branch of handleTouch( ) (DisplayManager_Touch.cpp)       forca.inc    forceDashboard( ) (DisplayManager.cpp)
//   menu.inc       showSettingsMain( ) (DisplayManager_SettingsCore.cpp)                                    ota.inc      otaTick( ), the update's exit (DisplayManager_OtaCore.cpp)
//   tema.inc       refreshTheme( ) (DisplayManager.cpp)                                                     tema_ang.inc the Angulo's uiThemeChanged( ) (DisplayManager_UiA.cpp)
//   evento.inc     the EVT_APPLY_DISPLAY_OFFSET branch, Core 0's Apply (AppManager_Events.cpp)
//   toque_orig.inc the original's touch branch of the alignment screen (UiTouchChain.h)                     toque_ang.inc the Angulo's case of the same screen (DisplayManager_UiA_Ajustes.cpp)
//   alinh.inc      the enum of the Angulo's targets, cut from AjustesTela.h
// The real headers display/SettingsSteps.h (offsetStep( )) and OtaScreen.h (the phases) are included as they are. What is the stand-in's: the panel (TftWithOffset's offset, its trail and
// whether Core 0 painted margins, which would put two cores on the SPI bus), the mutex, the touch gates, the event queue and the screens' draw chain (uiPass( ): what each UI paints from the
// repaint flags, read from UiOriginal.h and DisplayManager_UiA.cpp; the python side checks the original's table against the file).
// Two cores are modelled as a call order: core1Pass( ) is one pass of loopCore1( ) between the touch and the screen chain, and Core 0's code can be interleaved with it at the "points" it
// passes (the panel's store, the configuration's save, the calibration's entry): the case runs once with no pass in between and once with a pass at each point.
//
// Output: one line `CASE <id> <ui> PASS|FAIL <first failed check>` per case and UI, NOTE lines, and `RESULT <checks> checks, <failed> failed`. The exit status is 0: which failures are
// expected (XFAIL) is the python side's.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#define constrain( amt, low, high ) ( ( amt ) < ( low ) ? ( low ) : ( ( amt ) > ( high ) ? ( high ) : ( amt ) ) )
#define LOG_CODE( ... ) ( ( void )0 )
#define C1_PHASE( x ) ( ( void )0 )

#include "display/SettingsSteps.h"   // offsetStep( ): the real one
#include "OtaScreen.h"               // otaPhaseOf( ), otaHoldMs( ), otaPack( ): the real ones

#include "alinh.inc"

// ---- the stand-in: the names the pieces use ----
enum UiMode : uint8_t {
  MODE_DASHBOARD, MODE_GRAPH_LOADING, MODE_GRAPH_VIEW, MODE_GRAPH_DETAIL, MODE_SETTINGS_MAIN, MODE_SETTINGS_THEMES, MODE_SETTINGS_TOUCH_CAL, MODE_SETTINGS_TOUCH_SENS,
  MODE_SETTINGS_DISPLAY_OFFSET, MODE_CALENDAR, MODE_OTA_UPDATE
};
enum Ui { ORIGINAL, ANGULO };
enum class UiChange : uint8_t { DISPLAY_OFFSET_ENTER };
enum class UaToque : uint8_t { NAO_E_MEU, TRATADO, TRATADO_SEM_QUADRO };
struct Alvo { uint8_t tipo; };
struct UiEvent {
  enum EventType { EVT_NONE, EVT_APPLY_DISPLAY_OFFSET };
  EventType type = EVT_NONE;
  int id = 0;
  int param = 0;
};
struct __attribute__( ( packed ) ) DisplayOffsetData { uint8_t magic; int8_t offsetX; int8_t offsetY; uint8_t reserved; };
struct TouchCalData { uint8_t magic; uint8_t resto[15]; };
struct SoundConfigData { uint8_t b[8]; };
struct SystemConfig { uint8_t reserved[64] = { }; };
enum { SND_CONFIRM = 1 };
struct mutex_t { int fundo = 0; };
static void mutex_enter_blocking( mutex_t *m ) { m->fundo++; }
static void mutex_exit( mutex_t *m ) { m->fundo--; }

static Ui g_ui = ORIGINAL;
static int g_core = 0;              // who is running: 0 or 1
static uint32_t g_now = 0;          // millis( )
static std::function<void( const char * )> g_hook;
static int g_pontos = 0;            // the points Core 0 has passed in this run
static bool timeSince( uint32_t desde, uint32_t ms ) { return ( uint32_t )( g_now - desde ) >= ms; }
static int settingsMenuBuild( uint32_t, int *itens ) { itens[0] = 0; return 3; }
static void ponto( const char *nome ) {   // Core 0 passes a point: the case may run a Core 1 pass here
  if ( g_core != 0 ) return;
  g_pontos++;
  if ( g_hook ) g_hook( nome );
}
// A member Core 0 stores to: each store is a point where a Core 1 pass may fall, after the store (the entry writes the preview and the mode one after the other). The constructor is explicit so that
// `shown ? _offsetPreviewX : _offsetSavedX` has one conversion and not two.
template <class T> struct Obs {
  T v;
  explicit Obs( T x ) : v( x ) { }
  Obs &operator=( T x ) { v = x; ponto( "escrita" ); return *this; }
  Obs &operator=( const Obs &o ) { v = o.v; ponto( "escrita" ); return *this; }
  operator T &( ) { return v; }
  operator const T &( ) const { return v; }
};

struct Tft {
  int8_t ox = 0, oy = 0;
  std::vector<std::pair<int, int>> trilha;   // every value the glass was given, in order
  int violacoes = 0;                         // Core 0 painted the margins: two cores on one SPI bus
  int sets1 = 0, margens1 = 0;               // Core 1's moves of the glass, and how many of them painted the margins (a restore must: the old image's edge is still on the glass)
  void setDisplayOffset( int8_t x, int8_t y, bool pintaMargens = true ) {
    ponto( "tft:antes" );
    if ( g_core == 0 && pintaMargens ) violacoes++;
    if ( g_core == 1 ) { sets1++; if ( pintaMargens ) margens1++; }
    ox = constrain( x, -4, 4 );
    oy = constrain( y, -4, 4 );
    trilha.push_back( { ox, oy } );
    ponto( "tft:depois" );
  }
  int8_t getOffsetX( ) const { return ox; }
  int8_t getOffsetY( ) const { return oy; }
};

struct Pintura { UiMode modo = MODE_DASHBOARD; int ox = 99, oy = 99; bool cheia = false; int n = 0; };
struct Pedidos {
  bool fs = false, rs = false, rg = false, rc = false, rl = false, oa = false, sujo = false, cheio = false;
  bool operator==( const Pedidos &o ) const { return fs == o.fs && rs == o.rs && rg == o.rg && rc == o.rc && rl == o.rl && oa == o.oa && sujo == o.sujo && cheio == o.cheio; }
  bool nenhum( ) const { return !( fs || rs || rg || rc || rl || oa || sujo || cheio ); }
  std::string texto( ) const {
    std::string s;
    s += fs ? "forceSettingsRedraw " : ""; s += rs ? "repaintSettings " : ""; s += rg ? "repaintGraph " : ""; s += rc ? "repaintCalendar " : ""; s += rl ? "repaintLoading " : "";
    s += oa ? "otaRedrawAll " : ""; s += sujo ? "isDirty " : ""; s += cheio ? "forceFullRedraw" : "";
    return s.empty( ) ? "none" : s;
  }
};

class DisplayManager {
 public:
  struct { Tft *tft = nullptr; } _driver;
  mutex_t _stateMutex;
  struct { bool isBooting = false; } _sharedState;
  Obs<UiMode> _uiMode{ MODE_DASHBOARD };
  Obs<int8_t> _offsetPreviewX{ 0 }, _offsetPreviewY{ 0 };
  int8_t _offsetSavedX = 0, _offsetSavedY = 0;
  bool _isDirty = false, _forceFullRedraw = false, _forceSettingsRedraw = false, _repaintSettings = false, _repaintGraph = false, _repaintCalendar = false, _repaintLoading = false;
  bool _otaRedrawAll = false, _themeChanged = false, _uaTudo = false, _uaSujo = false;
  uint32_t _lastTouchTime = 0, _otaState = 0, _otaShownMs = 0;
  int8_t _lastOffsetDrawX = 0, _lastOffsetDrawY = 0;   // the original's sentinels (UiOriginal.h)
  int _menuSelection = 0, _mainMenuPage = 0, _lastMainMenuPage = 0, _menuCount = 0, _menuItems[8] = { };
  uint32_t _panelPerms = 0;
  std::vector<UiEvent> fila;
  Pintura ultima, ultimaCheia;   // the last paint, and the last one that repainted the whole screen
  int pinturas = 0;
  bool temQuadro = false;
  UiMode quadroModo = MODE_DASHBOARD;
  int quadroOx = 99, quadroOy = 99;

  // ---- the pieces ----
  void showSettingsDisplayOffset( );
  void loadDisplayOffset( const DisplayOffsetData *data );
  void tickDisplayOffset( );
  inline __attribute__( ( always_inline ) ) bool actOffsetUp( );
  inline __attribute__( ( always_inline ) ) bool actOffsetDown( );
  inline __attribute__( ( always_inline ) ) bool actOffsetLeft( );
  inline __attribute__( ( always_inline ) ) bool actOffsetRight( );
  inline __attribute__( ( always_inline ) ) bool actOffsetCentre( );
  inline __attribute__( ( always_inline ) ) void actOffsetPreview( );
  inline __attribute__( ( always_inline ) ) void actOffsetBack( );
  inline __attribute__( ( always_inline ) ) void actOffsetApply( );
  void forceDashboard( );
  void showSettingsMain( );
  void otaTick( );
  void refreshTheme( );
  void uiThemeChangedAngulo( );

  // ---- the stand-in's members ----
  bool acceptTouch( uint8_t ) { return true; }
  bool acceptHoldTouch( uint8_t ) { return true; }
  void pushUiEvent( const UiEvent &e ) { fila.push_back( e ); }
  void uiChanged( UiChange c, int ) { if ( c == UiChange::DISPLAY_OFFSET_ENTER && g_ui == ORIGINAL ) { _lastOffsetDrawX = 99; _lastOffsetDrawY = 99; } }
  void uaSujo( ) { _uaSujo = true; }
  bool otaEnd( uint32_t visto ) {   // dismisses `visto`, unless Core 0 published since
    if ( _otaState != visto ) return false;
    _otaState = otaPack( ( uint16_t )( otaSeqOf( visto ) + 1 ), OTA_PH_NONE, OTA_WHY_NONE );
    return true;
  }
  void pintar( UiMode m, bool cheia ) { ultima.modo = m; ultima.ox = _driver.tft->ox; ultima.oy = _driver.tft->oy; ultima.cheia = cheia; ultima.n = ++pinturas; if ( cheia ) ultimaCheia = ultima; }
  void uiPaintOta( ) { pintar( MODE_OTA_UPDATE, _otaRedrawAll ); _otaRedrawAll = false; }   // both UIs: _otaRedrawAll is a full repaint (the Angulo: _uaTudo, DisplayManager_UiA.cpp)
  void showTouchCalibration( ) {   // the calibration's entry, reduced to what the Apply's order depends on: the mode changes
    ponto( "showTouchCalibration:antes" );
    mutex_enter_blocking( &_stateMutex );
    _uiMode = MODE_SETTINGS_TOUCH_SENS; _forceSettingsRedraw = true; _repaintSettings = true;
    mutex_exit( &_stateMutex );
    ponto( "showTouchCalibration:depois" );
  }
  void resetTouchCalibration( ) { ponto( "resetTouchCalibration" ); }

  void handleTouchIdle( );
  void handleTouchOriginal( int16_t x, int16_t y );
  UaToque handleTouchAngulo( const Alvo *a );
  void uiThemeChanged( ) {
    if ( g_ui == ANGULO ) { uiThemeChangedAngulo( ); return; }
    // the original's hook draws "applying theme", then the dashboard in full with the new palette, and ends in the dashboard (UiOriginal.h): a stand-in, the drawing is not modelled
    if ( !_sharedState.isBooting ) { pintar( MODE_DASHBOARD, true ); mutex_enter_blocking( &_stateMutex ); _isDirty = false; mutex_exit( &_stateMutex ); _uiMode = MODE_DASHBOARD; }
  }
  // The screen chain: what each UI paints from the flags. The original's (UiOriginal.h): the dashboard from _isDirty (render( ) paints in full on _forceFullRedraw), the graph screens on
  // _repaintGraph, the calendar on _repaintCalendar, the update screen from otaTick( ), every other screen on _repaintSettings (in full on _forceSettingsRedraw).
  // The Angulo's (DisplayManager_UiA.cpp, uiPass( ) and uaQuadro( )): the requests all become uaSujo( ); a frame repaints in full when _uaTudo is up or its identity (the screen and
  // the glass's alignment) is not the last painted frame's.
  void uiPass( ) {
    if ( g_ui == ORIGINAL ) {
      if ( _uiMode == MODE_DASHBOARD ) {
        if ( _isDirty ) { const bool cheia = _forceFullRedraw; _isDirty = false; if ( cheia ) _forceFullRedraw = false; pintar( MODE_DASHBOARD, cheia ); }
      } else if ( _uiMode == MODE_GRAPH_VIEW || _uiMode == MODE_GRAPH_DETAIL ) {
        if ( _repaintGraph ) { _repaintGraph = false; pintar( _uiMode, true ); }
      } else if ( _uiMode == MODE_CALENDAR ) {
        if ( _repaintCalendar ) { _repaintCalendar = false; pintar( _uiMode, true ); }
      } else if ( _uiMode == MODE_GRAPH_LOADING ) {
        if ( _repaintLoading ) { _repaintLoading = false; pintar( _uiMode, true ); }
      } else if ( _uiMode != MODE_OTA_UPDATE ) {
        if ( _repaintSettings ) { _repaintSettings = false; const bool cheia = _forceSettingsRedraw; _forceSettingsRedraw = false; pintar( _uiMode, cheia ); }
      }
      return;
    }
    if ( _uiMode == MODE_DASHBOARD && _isDirty ) { _isDirty = false; uaSujo( ); }
    bool pedido = false;
    if ( _repaintSettings ) { _repaintSettings = false; pedido = true; }
    if ( _repaintGraph ) { _repaintGraph = false; pedido = true; }
    if ( _repaintLoading ) { _repaintLoading = false; pedido = true; }
    if ( _repaintCalendar ) { _repaintCalendar = false; pedido = true; }
    if ( _forceSettingsRedraw ) { _forceSettingsRedraw = false; pedido = true; }
    if ( pedido ) uaSujo( );
    if ( _uiMode == MODE_OTA_UPDATE ) return;
    if ( !_uaSujo && !_uaTudo ) return;
    _uaSujo = false;
    const bool identidade = !temQuadro || quadroModo != _uiMode || quadroOx != _driver.tft->ox || quadroOy != _driver.tft->oy;
    const bool cheia = _uaTudo || identidade;
    _uaTudo = false;
    pintar( _uiMode, cheia );
    temQuadro = true; quadroModo = _uiMode; quadroOx = _driver.tft->ox; quadroOy = _driver.tft->oy;
  }
};

#include "carga.inc"
#include "entrada.inc"
#if SEM_TIQUE
void DisplayManager::tickDisplayOffset( ) { }   // the piece is not in the source yet (the first commit of the test): the tree's behaviour, nothing checks the glass
#else
#include "tique.inc"
#endif
#include "acoes.inc"
#include "forca.inc"
#include "menu.inc"
#include "ota.inc"
#include "tema.inc"
#include "tema_ang.inc"

void DisplayManager::handleTouchIdle( ) {
#include "ocioso.inc"
}
void DisplayManager::handleTouchOriginal( int16_t x, int16_t y ) {
  if ( false ) { }
#include "toque_orig.inc"
}
UaToque DisplayManager::handleTouchAngulo( const Alvo *a ) {
  switch ( _uiMode ) {
#include "toque_ang.inc"
    default: break;
  }
  return UaToque::NAO_E_MEU;
}

struct Storage {
  SystemConfig cfg;
  SystemConfig &getConfig( ) { return cfg; }
  void saveConfiguration( ) { ponto( "saveConfiguration" ); }
};
struct Sound { void play( int ) { ponto( "play" ); } };
struct App {
  Storage *_storageMgr = nullptr;
  DisplayManager *_displayMgr = nullptr;
  Sound *_soundMgr = nullptr;
  void apply( UiEvent &uiEv ) {
    if ( false ) { }
#include "evento.inc"
  }
};

// ---- the harness ----
struct World {
  Tft tft;
  DisplayManager dm;
  Storage st;
  Sound snd;
  App app;
  bool houveTique = false;   // a tick changed the glass since zeraTique( )
  Pedidos pedidos;           // the requests standing right after that tick
  World( ) {
    dm._driver.tft = &tft;
    app._storageMgr = &st; app._displayMgr = &dm; app._soundMgr = &snd;
    g_core = 0; g_now = 1000000; g_hook = nullptr; g_pontos = 0;
    dm._lastTouchTime = g_now; dm._otaShownMs = g_now;
  }
  void zeraTique( ) { houveTique = false; }
};

static const char *g_caso = "";
static bool g_ok = true;
static int g_checks = 0, g_fails = 0;
static std::string g_detalhe;
#define CK( cond, ... ) do { g_checks++; if ( !( cond ) ) { g_fails++; if ( g_ok ) { char b_[240]; snprintf( b_, sizeof b_, __VA_ARGS__ ); g_detalhe = b_; } g_ok = false; } } while ( 0 )
static void abre( const char *id ) { g_caso = id; g_ok = true; g_detalhe.clear( ); }
static void fecha( ) { printf( "CASE %s %s %s %s\n", g_caso, g_ui == ORIGINAL ? "original" : "angulo", g_ok ? "PASS" : "FAIL", g_detalhe.c_str( ) ); }

// What each screen takes of the repaint requests (the original's uiPass( ) and otaTick( ), which the python side reads from UiOriginal.h; the Angulo takes all of them): the set the tick must raise
// for the screen that is up, and nothing else. A request nobody takes stays up, and repaintPending in loopCore1( ) stays true with it.
static Pedidos esperado( UiMode m ) {
  Pedidos p;
  switch ( m ) {
    case MODE_DASHBOARD: p.sujo = true; p.cheio = true; break;
    case MODE_GRAPH_LOADING: p.rl = true; break;
    case MODE_GRAPH_VIEW: case MODE_GRAPH_DETAIL: p.rg = true; break;
    case MODE_CALENDAR: p.rc = true; break;
    case MODE_OTA_UPDATE: p.oa = true; break;
    default: p.fs = true; p.rs = true; break;
  }
  return p;
}
static Pedidos pedidosDe( const DisplayManager &d ) { return Pedidos{ d._forceSettingsRedraw, d._repaintSettings, d._repaintGraph, d._repaintCalendar, d._repaintLoading, d._otaRedrawAll, d._isDirty, d._forceFullRedraw }; }
// The requests still standing after a pass. The Angulo never takes _forceFullRedraw (only the original's render( ) does) and it is in no pace test, so it is left out there.
static Pedidos dePe( const DisplayManager &d ) { Pedidos p = pedidosDe( d ); if ( g_ui == ANGULO ) p.cheio = false; return p; }

static void core1Pass( World &w ) {   // one pass of loopCore1( ): handleTouch( ), the tick, otaTick( ), the theme, the screen chain (the python side holds the real file to this order)
  const int antes = g_core;
  g_core = 1;
  DisplayManager &d = w.dm;
  d.handleTouchIdle( );
  const int ax = w.tft.ox, ay = w.tft.oy;
  d.tickDisplayOffset( );
  if ( w.tft.ox != ax || w.tft.oy != ay ) {
    w.houveTique = true;
    w.pedidos = pedidosDe( d );
  }
  d.otaTick( );
  if ( d._themeChanged ) { d.uiThemeChanged( ); d._themeChanged = false; }
  d.uiPass( );
  g_core = antes;
}
static void passe( World &w, int n = 1 ) { while ( n-- > 0 ) core1Pass( w ); }
static void carrega( World &w, int8_t x, int8_t y ) { const DisplayOffsetData d = { 0xD0, x, y, 0 }; w.dm.loadDisplayOffset( &d ); }
static void toca( World &w, char qual ) {   // a touch on Right ('R'), Down ('D'), Voltar ('V') or Salvar ('S'), on Core 1, in the UI's own handler
  const int antes = g_core;
  g_core = 1;
  w.dm._lastTouchTime = g_now;
  if ( g_ui == ORIGINAL ) {
    int16_t x = 250, y = 220;   // Salvar
    if ( qual == 'R' ) { x = 210; y = 120; } else if ( qual == 'D' ) { x = 160; y = 165; } else if ( qual == 'V' ) { x = 60; y = 220; }
    w.dm.handleTouchOriginal( x, y );
  } else {
    const Alvo a = { ( uint8_t )( qual == 'R' ? ALINH_DIREITA : qual == 'D' ? ALINH_BAIXO : qual == 'V' ? ALINH_VOLTAR : ALINH_SALVAR ) };
    w.dm.handleTouchAngulo( &a );
  }
  g_core = antes;
}
// The gestures of the preview: Right (+1,0), Down (0,+1) and both (+1,+1), the diagonal. Down alone moves Y with X still: the tick has to compare both axes and keep both of the preview's.
struct Gesto { const char *nome; const char *toques; int px, py; };
static const Gesto GESTOS[] = { { "right", "R", 1, 0 }, { "down", "D", 0, 1 }, { "diagonal", "RD", 1, 1 } };
static void faz( World &w, const Gesto &g ) { for ( const char *c = g.toques; *c; c++ ) toca( w, *c ); }
static bool vidro( const World &w, int x, int y ) { return w.tft.ox == x && w.tft.oy == y; }
static bool margensOk( const World &w ) { return w.tft.sets1 == w.tft.margens1; }   // every move of the glass on Core 1 painted the margins
static bool repintado( const World &w ) {   // the screen on the glass was repainted in full at the glass's own offset (the update screen goes on painting its bar after that, in part)
  const Pintura &p = w.dm.ultimaCheia;
  return p.modo == w.dm._uiMode && p.ox == w.tft.ox && p.oy == w.tft.oy;
}
static std::string pintura( const World &w ) {
  char b[120];
  snprintf( b, sizeof b, "mode %d, last full paint was mode %d at %d,%d, glass %d,%d", ( int )w.dm._uiMode, ( int )w.dm.ultimaCheia.modo, w.dm.ultimaCheia.ox, w.dm.ultimaCheia.oy, w.tft.ox, w.tft.oy );
  return b;
}
static void abreAlinhamento( World &w ) { w.dm.showSettingsDisplayOffset( ); w.dm._lastTouchTime = g_now; passe( w ); }   // the CLI's `screen` ends in resetTouchIdle( )
static void deixaPrevia( World &w ) {   // saved 0,0; the alignment screen; Right; the 30 s idle return (the first pass after it)
  carrega( w, 0, 0 );
  abreAlinhamento( w );
  toca( w, 'R' );
  passe( w, 2 );
  g_now += 31000;
  passe( w );
}

// C1: the preview (Right, Down, or both), then the idle return, then ONE pass: the glass is at the saved offset, the repaint was asked for and the restore painted the margins. Before that, the preview
// stays on the glass while the screen is shown.
static void c1( ) {
  abre( "C1" );
  for ( const Gesto &g : GESTOS ) {
    World w;
    carrega( w, 0, 0 );
    abreAlinhamento( w );
    faz( w, g );
    CK( vidro( w, g.px, g.py ), "%s: the glass reads %d,%d, wanted the preview %d,%d at once", g.nome, w.tft.ox, w.tft.oy, g.px, g.py );
    passe( w, 3 );
    CK( vidro( w, g.px, g.py ), "%s: the screen is shown and a pass moved the glass to %d,%d: the preview %d,%d must stay", g.nome, w.tft.ox, w.tft.oy, g.px, g.py );
    g_now += 31000;
    w.zeraTique( );
    passe( w );
    CK( w.dm._uiMode == MODE_DASHBOARD, "%s: the idle return did not reach the dashboard (mode %d)", g.nome, ( int )w.dm._uiMode );
    CK( vidro( w, 0, 0 ), "%s: the idle return and one pass left the glass at %d,%d, wanted the saved 0,0", g.nome, w.tft.ox, w.tft.oy );
    CK( w.houveTique && w.pedidos == esperado( MODE_DASHBOARD ), "%s: the pass that put the glass back raised [%s] (tick changed the glass: %d), wanted exactly the dashboard's [%s]", g.nome, w.pedidos.texto( ).c_str( ), w.houveTique, esperado( MODE_DASHBOARD ).texto( ).c_str( ) );
    CK( repintado( w ), "%s: the dashboard was not repainted in full at the glass's offset (%s)", g.nome, pintura( w ).c_str( ) );
    CK( dePe( w.dm ).nenhum( ), "%s: requests of another screen left standing after the pass: [%s]", g.nome, dePe( w.dm ).texto( ).c_str( ) );
    CK( margensOk( w ), "%s: Core 1 moved the glass %d time(s) and painted the margins %d time(s): a restore without them leaves the old image's edge on the glass", g.nome, w.tft.sets1, w.tft.margens1 );
    CK( w.tft.violacoes == 0, "%s: Core 0 painted the margins", g.nome );
  }
  fecha( );
}

// C2: after that, the next entry starts at the saved offset.
static void c2( ) {
  abre( "C2" );
  World w;
  deixaPrevia( w );
  w.dm.showSettingsDisplayOffset( );
  CK( w.dm._offsetPreviewX == 0 && w.dm._offsetPreviewY == 0, "the next entry starts at %d,%d, wanted 0,0", ( int )w.dm._offsetPreviewX, ( int )w.dm._offsetPreviewY );
  CK( w.dm._offsetSavedX == 0 && w.dm._offsetSavedY == 0, "the next entry holds %d,%d as the saved offset, wanted 0,0", w.dm._offsetSavedX, w.dm._offsetSavedY );
  passe( w, 2 );
  CK( vidro( w, 0, 0 ), "the glass reads %d,%d after the next entry, wanted 0,0", w.tft.ox, w.tft.oy );
  fecha( );
}

// C3: Core 0's exits (the CLI's `screen dash` and `screen set`, an update, a theme change) leave the glass at the saved offset and the screen repainted at it; so does a preview left in any mode.
static void c3( ) {
  abre( "C3" );
  const char *saidas[] = { "dash", "menu", "update", "theme" };
  for ( const char *s : saidas ) {
    World w;
    carrega( w, 0, 0 );
    abreAlinhamento( w );
    toca( w, 'R' );
    passe( w );
    CK( vidro( w, 1, 0 ), "%s: the preview is not on the glass (%d,%d)", s, w.tft.ox, w.tft.oy );
    if ( !strcmp( s, "dash" ) ) w.dm.forceDashboard( );
    else if ( !strcmp( s, "menu" ) ) w.dm.showSettingsMain( );
    else if ( !strcmp( s, "update" ) ) w.dm._otaState = otaPack( 1, OTA_PH_RECEIVING, OTA_WHY_NONE );
    else w.dm.refreshTheme( );
    passe( w, 3 );
    CK( vidro( w, 0, 0 ), "%s: the glass reads %d,%d after the exit and three passes, wanted the saved 0,0", s, w.tft.ox, w.tft.oy );
    CK( repintado( w ), "%s: the screen was not repainted in full at the glass's offset (%s)", s, pintura( w ).c_str( ) );
    CK( dePe( w.dm ).nenhum( ), "%s: requests left standing after the exit and three passes: [%s]", s, dePe( w.dm ).texto( ).c_str( ) );
    CK( margensOk( w ), "%s: Core 1 moved the glass %d time(s) and painted the margins %d time(s)", s, w.tft.sets1, w.tft.margens1 );
    if ( !strcmp( s, "update" ) ) {
      CK( w.dm._uiMode == MODE_OTA_UPDATE, "update: the update screen is not up (mode %d)", ( int )w.dm._uiMode );
      w.dm._otaState = otaPack( 2, OTA_PH_NONE, OTA_WHY_NONE );
      passe( w, 2 );
      CK( w.dm._uiMode == MODE_DASHBOARD, "update: the end of the update did not reach the dashboard (mode %d)", ( int )w.dm._uiMode );
      CK( vidro( w, 0, 0 ) && repintado( w ), "update: after the end the glass reads %d,%d (%s)", w.tft.ox, w.tft.oy, pintura( w ).c_str( ) );
    }
    CK( w.tft.violacoes == 0, "%s: Core 0 painted the margins", s );
  }
  // a preview left on the glass, whatever the screen: the tick alone raises exactly what that screen takes, the pass repaints it in full at the saved offset, and nothing is left standing
  const UiMode modos[] = { MODE_DASHBOARD, MODE_GRAPH_LOADING, MODE_GRAPH_VIEW, MODE_GRAPH_DETAIL, MODE_CALENDAR, MODE_SETTINGS_MAIN, MODE_SETTINGS_THEMES, MODE_SETTINGS_TOUCH_CAL,
                           MODE_SETTINGS_TOUCH_SENS, MODE_SETTINGS_DISPLAY_OFFSET, MODE_OTA_UPDATE };
  for ( UiMode m : modos ) {
    World w;
    carrega( w, 0, 0 );
    w.tft.ox = 1;
    w.dm._uiMode = m;
    if ( m == MODE_OTA_UPDATE ) w.dm._otaState = otaPack( 1, OTA_PH_RECEIVING, OTA_WHY_NONE );
    g_core = 1;
    w.dm.tickDisplayOffset( );
    g_core = 0;
    CK( vidro( w, 0, 0 ), "mode %d: a stale glass is at %d,%d after the tick, wanted 0,0", ( int )m, w.tft.ox, w.tft.oy );
    CK( w.tft.sets1 == 1 && w.tft.margens1 == 1, "mode %d: the tick moved the glass %d time(s) and painted the margins %d time(s), wanted once and with them", ( int )m, w.tft.sets1, w.tft.margens1 );
    CK( pedidosDe( w.dm ) == esperado( m ), "mode %d: the tick raised [%s], wanted exactly what the screen takes: [%s]", ( int )m, pedidosDe( w.dm ).texto( ).c_str( ), esperado( m ).texto( ).c_str( ) );
    passe( w, 2 );
    CK( repintado( w ), "mode %d: not repainted in full at the glass's offset (%s)", ( int )m, pintura( w ).c_str( ) );
    CK( dePe( w.dm ).nenhum( ), "mode %d: requests left standing after the repaint: [%s]", ( int )m, dePe( w.dm ).texto( ).c_str( ) );
  }
  fecha( );
}

// C4: `screen offset` while a preview is on the glass (Right, Down, or both), then Voltar: the saved offset is back (the preview is not taken for the saved one).
static void c4( ) {
  abre( "C4" );
  for ( const Gesto &g : GESTOS ) {
    World w;
    carrega( w, 0, 0 );
    abreAlinhamento( w );
    faz( w, g );
    passe( w );
    CK( vidro( w, g.px, g.py ), "%s: the preview is not on the glass (%d,%d)", g.nome, w.tft.ox, w.tft.oy );
    w.dm.showSettingsDisplayOffset( );   // Core 0, the CLI's `screen offset`
    passe( w, 2 );
    CK( w.dm._offsetSavedX == 0 && w.dm._offsetSavedY == 0, "%s: the entry took the glass's preview for the saved offset: saved %d,%d", g.nome, w.dm._offsetSavedX, w.dm._offsetSavedY );
    CK( vidro( w, 0, 0 ), "%s: after the second entry the glass reads %d,%d, wanted the entry's 0,0", g.nome, w.tft.ox, w.tft.oy );
    toca( w, 'V' );
    passe( w, 2 );
    CK( w.dm._uiMode == MODE_SETTINGS_MAIN, "%s: Voltar did not reach the menu (mode %d)", g.nome, ( int )w.dm._uiMode );
    CK( vidro( w, 0, 0 ), "%s: after Voltar the glass reads %d,%d, wanted the saved 0,0", g.nome, w.tft.ox, w.tft.oy );
    CK( margensOk( w ), "%s: Core 1 moved the glass %d time(s) and painted the margins %d time(s)", g.nome, w.tft.sets1, w.tft.margens1 );
  }
  fecha( );
}

// C5: Voltar, the control: the saved offset goes back at once and the tick has nothing to correct.
static void c5( ) {
  abre( "C5" );
  World w;
  carrega( w, 0, 0 );
  abreAlinhamento( w );
  toca( w, 'R' );
  passe( w );
  toca( w, 'V' );
  CK( vidro( w, 0, 0 ), "Voltar left the glass at %d,%d", w.tft.ox, w.tft.oy );
  CK( w.dm._uiMode == MODE_SETTINGS_MAIN, "Voltar did not reach the menu (mode %d)", ( int )w.dm._uiMode );
  CK( w.dm._offsetPreviewX == 0 && w.dm._offsetPreviewY == 0, "the preview was not discarded: %d,%d", ( int )w.dm._offsetPreviewX, ( int )w.dm._offsetPreviewY );
  w.zeraTique( );
  passe( w, 3 );
  CK( !w.houveTique && vidro( w, 0, 0 ), "after Voltar the tick still had to move the glass (changed %d, glass %d,%d)", w.houveTique, w.tft.ox, w.tft.oy );
  CK( margensOk( w ), "Core 1 moved the glass %d time(s) and painted the margins %d time(s)", w.tft.sets1, w.tft.margens1 );
  fecha( );
}

// One Salvar, Core 0's Apply interleaved with a Core 1 pass at point k (-1: none). Returns the points the Apply passed.
static int salva( World &w, int k, int *marca ) {
  carrega( w, 0, 0 );
  abreAlinhamento( w );
  toca( w, 'R' );
  passe( w );
  *marca = ( int )w.tft.trilha.size( );
  toca( w, 'S' );
  UiEvent ev = w.dm.fila.empty( ) ? UiEvent( ) : w.dm.fila.back( );
  g_pontos = 0;
  if ( k >= 0 ) g_hook = [&w, k]( const char * ) { if ( g_pontos - 1 == k ) core1Pass( w ); };
  w.app.apply( ev );
  g_hook = nullptr;
  const int pontos = g_pontos;
  passe( w, 3 );
  return pontos;
}

// C6: Salvar: the glass keeps the preview through the whole Apply, whatever Core 1 does in between, and the next entry starts at it.
static void c6( ) {
  abre( "C6" );
  int marca = 0, pontos = 0;
  { World w; pontos = salva( w, -1, &marca ); }
  for ( int k = -1; k < pontos; k++ ) {
    World w;
    salva( w, k, &marca );
    CK( !w.dm.fila.empty( ) && w.dm.fila.back( ).id == 1 && w.dm.fila.back( ).param == 0, "the Apply event does not carry 1,0" );
    bool saiu = false;
    for ( size_t i = ( size_t )marca; i < w.tft.trilha.size( ); i++ ) if ( w.tft.trilha[i].first != 1 || w.tft.trilha[i].second != 0 ) saiu = true;
    CK( !saiu, "pass at point %d of the Apply: the glass left the new offset 1,0 for another value", k );
    CK( vidro( w, 1, 0 ), "pass at point %d: after the Apply the glass reads %d,%d, wanted 1,0", k, w.tft.ox, w.tft.oy );
    CK( w.dm._uiMode == MODE_SETTINGS_TOUCH_SENS, "pass at point %d: the calibration is not up (mode %d)", k, ( int )w.dm._uiMode );
    w.dm.showSettingsMain( );
    passe( w );
    w.dm.showSettingsDisplayOffset( );
    passe( w, 2 );
    CK( w.dm._offsetPreviewX == 1 && w.dm._offsetPreviewY == 0, "pass at point %d: the next entry starts at %d,%d, wanted 1,0", k, ( int )w.dm._offsetPreviewX, ( int )w.dm._offsetPreviewY );
    CK( vidro( w, 1, 0 ), "pass at point %d: the next entry shows the glass at %d,%d, wanted 1,0", k, w.tft.ox, w.tft.oy );
    CK( w.tft.violacoes == 0, "pass at point %d: Core 0 painted the margins", k );
  }
  fecha( );
}

// C7: the boot's load interleaved with Core 1: the glass converges to the loaded offset, and once it shows it never shows another value.
static void c7( ) {
  abre( "C7" );
  const DisplayOffsetData dados[] = { { 0xD0, 2, -1, 0 }, { 0xD0, -4, 4, 0 }, { 0x00, 3, 3, 0 } };
  for ( const DisplayOffsetData &d : dados ) {
    const int ex = d.magic == 0xD0 ? d.offsetX : 0, ey = d.magic == 0xD0 ? d.offsetY : 0;
    int pontos = 0;
    for ( int k = -2; k < 2 || k < pontos; k++ ) {   // -2: the control with no Core 1 pass; -1 .. : a pass at each point, and at every point
      World w;
      w.dm._sharedState.isBooting = true;
      g_pontos = 0;
      if ( k == -1 ) g_hook = [&w]( const char * ) { core1Pass( w ); };
      else if ( k >= 0 ) g_hook = [&w, k]( const char * ) { if ( g_pontos - 1 == k ) core1Pass( w ); };
      w.dm.loadDisplayOffset( &d );
      g_hook = nullptr;
      if ( k == -2 ) pontos = g_pontos;
      passe( w, 4 );
      CK( vidro( w, ex, ey ), "data %d,%d magic %02x, pass at point %d: the glass reads %d,%d, wanted %d,%d", d.offsetX, d.offsetY, d.magic, k, w.tft.ox, w.tft.oy, ex, ey );
      CK( w.dm._offsetSavedX == ex && w.dm._offsetSavedY == ey, "pass at point %d: the saved offset is %d,%d, wanted %d,%d", k, w.dm._offsetSavedX, w.dm._offsetSavedY, ex, ey );
      bool visto = false, volta = false;
      for ( const auto &v : w.tft.trilha ) { if ( v.first == ex && v.second == ey ) visto = true; else if ( visto ) volta = true; }
      CK( !volta, "pass at point %d: once the glass showed %d,%d it showed another value again", k, ex, ey );
      CK( w.tft.violacoes == 0, "pass at point %d: Core 0 painted the margins", k );
    }
  }
  fecha( );
}

// C9: `screen offset` on Core 0 while a stale preview is still in the members (the last visit was left by `screen dash`), with a Core 1 pass falling after each store the entry makes: the glass must never
// show the old preview. The entry writes the preview and then the mode; a pass that sees the mode first takes the old preview for its target.
static void c9( ) {
  abre( "C9" );
  for ( const Gesto &g : GESTOS ) {
    int pontos = 0;
    for ( int k = -2; k < pontos; k++ ) {   // -2: no pass (it counts the points); -1: a pass after every store; k: after the k-th only
      World w;
      carrega( w, 0, 0 );
      abreAlinhamento( w );
      faz( w, g );
      passe( w );
      w.dm.forceDashboard( );   // Core 0, `screen dash`: the preview stays in the members and the tick puts the saved offset on the glass
      passe( w, 2 );
      CK( vidro( w, 0, 0 ) && w.dm._offsetPreviewX == g.px && w.dm._offsetPreviewY == g.py && w.dm._uiMode == MODE_DASHBOARD, "%s: the setup: glass %d,%d, stale preview %d,%d, mode %d", g.nome, w.tft.ox, w.tft.oy,
          ( int )w.dm._offsetPreviewX, ( int )w.dm._offsetPreviewY, ( int )w.dm._uiMode );
      const size_t marca = w.tft.trilha.size( );
      g_pontos = 0;
      if ( k == -1 ) g_hook = [&w]( const char * ) { core1Pass( w ); };
      else if ( k >= 0 ) g_hook = [&w, k]( const char * ) { if ( g_pontos - 1 == k ) core1Pass( w ); };
      w.dm.showSettingsDisplayOffset( );   // Core 0, the CLI's `screen offset`
      g_hook = nullptr;
      if ( k == -2 ) pontos = g_pontos;
      bool velha = false;
      for ( size_t i = marca; i < w.tft.trilha.size( ); i++ ) if ( w.tft.trilha[i].first != 0 || w.tft.trilha[i].second != 0 ) velha = true;
      CK( !velha, "%s, pass at point %d of the entry: the glass showed the old preview %d,%d", g.nome, k, g.px, g.py );
      passe( w, 2 );
      CK( vidro( w, 0, 0 ) && w.dm._uiMode == MODE_SETTINGS_DISPLAY_OFFSET && w.dm._offsetPreviewX == 0 && w.dm._offsetPreviewY == 0, "%s, pass at point %d: the entry ends with the glass at %d,%d, mode %d, preview %d,%d",
          g.nome, k, w.tft.ox, w.tft.oy, ( int )w.dm._uiMode, ( int )w.dm._offsetPreviewX, ( int )w.dm._offsetPreviewY );
      CK( margensOk( w ) && w.tft.violacoes == 0, "%s, pass at point %d: margins (Core 1 %d of %d, Core 0 %d)", g.nome, k, w.tft.margens1, w.tft.sets1, w.tft.violacoes );
    }
  }
  fecha( );
}

int main( ) {
  for ( Ui ui : { ORIGINAL, ANGULO } ) {
    g_ui = ui;
    c1( ); c2( ); c3( ); c4( ); c5( ); c6( ); c7( ); c9( );
  }
  printf( "RESULT: %d checks, %d failed\n", g_checks, g_fails );
  return 0;
}
