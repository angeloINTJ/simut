// test_rede_main.cpp -- the Ângulo glue's network glyph (DisplayManager::uaVmStatus( )), RUN on the host (S10c, the Wi-Fi status bar). Built and run by tools/tft_ui/test_rede.py.
//
// DisplayManager_UiA.cpp cannot be linked here as a file (it is a DisplayManager, with two displays, a touch panel and two cores behind it), so the python side cuts the one member out of it,
// verbatim and without comments (uaVmStatus.inc: its head and its body, with the `if constexpr ( SIMUT_NET_WIFI )` it holds), and this file compiles that against a stand-in (the manner of
// test_confirma_ap_main.cpp). The kit (Telas.cpp, Base.cpp, Angulo.cpp, Textos.cpp) is the real one. -DSIMUT_NET_WIFI=1 builds a Wi-Fi image's branch, =0 the cable's.
//
// What runs, against the OLD top bar's ladder (DisplayManager::drawTopBar( ), DisplayManager_Dashboard.cpp), whose thresholds the python side read out of that file (esperado.inc):
//   1. every level from -200 to +20 dBm and the extremes of int: the glue's Rede equals the model (Wi-Fi: no bar WIFI_SEM, then WIFI_1 to WIFI_4 as the old bar's one to four bars; cable: above -100
//      CABO, else CABO_SEM_LINK), and is never NENHUMA nor a state of the other network;
//   2. the named points of the brief (strong, each threshold and the step beside it, weak, disconnected), written out in this file and not derived from the model;
//   3. the fields the function does not own (pendentes, envio);
//   4. the frames: the status bar drawn by the real kit (ua::desenharStatus( )) from the view model the glue made, in both themes. What the network icon adds to the bar (the bar against the same bar with
//      Rede::NENHUMA) must be the pixels of the expected Lucide icon drawn alone by the kit, all in the expected ink: signal, signal-high, signal-medium and signal-low for four to one bars (the one-bar icon in the alert
//      colour), wifi-off for no signal in the bar's neutral ink, and for the cable ethernet-port with a link and unplug without, both in the same neutral ink (no state draws in the danger colour: semVermelho( ) holds it for every state). No icon missing, equal states draw equal bars and different states different, the cable's two included
//      ones. The frames are written to the folder in argv[1] (RGB565, 320 x 40, and _ordem.txt: name, level, state) for test_rede.py to picture.

#include <Adafruit_GFX.h>
#include <algorithm>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <utility>
#include <vector>

#include "Telas.h"
#include "Base.h"
#include "UiA.h"
#include "Textos.h"   // UA_TXT( ): after the screens' headers, because its pragmas hold to the end of the unit

#ifndef SIMUT_NET_WIFI
#error "test_rede.py passes -DSIMUT_NET_WIFI=1 (a Wi-Fi image) or =0 (the cable's)"
#endif

alignas( 8 ) uint8_t g_uaVm[UA_VM_MAX];

#include "esperado.inc"   // ANTIGO_SEM_LINK, ANTIGO_4_BARRAS, ANTIGO_3_BARRAS, ANTIGO_2_BARRAS: read from DisplayManager_Dashboard.cpp

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The stand-in: the members and the free functions uaVmStatus( ) uses, with the firmware's types (DisplayManager.h:295, :910-912, :1345-1353; SystemDefs_Time.h:110, :184).
constexpr uint32_t WEB_NOTIFY_DURATION_MS = 5000;
static uint32_t millis( ) { return 0; }
static bool timeSince( uint32_t, uint32_t ) { return true; }   // never reached: _webNotifyStartMs stays 0

class DisplayManager {
 public:
  struct { int wifiRssi; uint16_t pendingPkts; } _lastRenderedState = { };
  volatile bool _alarmSilenced = false;
  volatile uint32_t _alarmSilenceEnd = 0;
  char _webNotifyUser[16] = { 0 };
  uint32_t _webNotifyStartMs = 0;
  volatile uint8_t _pktArrowState = 0;

  bool uaRelogio( char hora[6], int &h, bool &provisoria ) {
    memcpy( hora, "14:32", 6 );
    h = 14;
    provisoria = false;
    return true;
  }
  void uaVmStatus( ua::VmStatus &s );
};

#include "uaVmStatus.inc"   // the glue's member, as cut

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static int g_confere = 0, g_falhas = 0;
#define CONFERE( cond, ... ) \
  do { \
    g_confere++; \
    if ( !( cond ) ) { \
      g_falhas++; \
      if ( g_falhas <= 40 ) { printf( "FAIL: " ); printf( __VA_ARGS__ ); printf( "\n" ); } \
    } \
  } while ( 0 )

using R = ua::Rede;

static const char *nome( R r ) {
  switch ( r ) {
    case R::NENHUMA: return "NENHUMA";
    case R::CABO: return "CABO";
    case R::CABO_SEM_LINK: return "CABO_SEM_LINK";
    case R::WIFI_SEM: return "WIFI_SEM";
    case R::WIFI_1: return "WIFI_1";
    case R::WIFI_2: return "WIFI_2";
    case R::WIFI_3: return "WIFI_3";
    case R::WIFI_4: return "WIFI_4";
  }
  return "?";
}

static bool ehWifi( R r ) { return r == R::WIFI_SEM || r == R::WIFI_1 || r == R::WIFI_2 || r == R::WIFI_3 || r == R::WIFI_4; }
static bool ehCabo( R r ) { return r == R::CABO || r == R::CABO_SEM_LINK; }

static ua::VmStatus vmDoNivel( int rssi, uint16_t pendentes = 0, uint8_t seta = 0 ) {
  DisplayManager d;
  d._lastRenderedState.wifiRssi = rssi;
  d._lastRenderedState.pendingPkts = pendentes;
  d._pktArrowState = seta;
  ua::VmStatus s = { };   // rede starts as NENHUMA: a glue that forgets to set it answers NENHUMA, which no case below accepts
  d.uaVmStatus( s );
  return s;
}

// The old top bar's ladder, as the python side read it: the bars drawn for a level (DisplayManager::drawTopBar( ), the Wi-Fi image's `barras`).
static int barras( int rssi ) {
  if ( !( rssi > ANTIGO_SEM_LINK ) ) return 0;
  if ( rssi > ANTIGO_4_BARRAS ) return 4;
  if ( rssi > ANTIGO_3_BARRAS ) return 3;
  if ( rssi > ANTIGO_2_BARRAS ) return 2;
  return 1;
}

#if SIMUT_NET_WIFI
static R esperado( int rssi ) {
  switch ( barras( rssi ) ) {
    case 0: return R::WIFI_SEM;
    case 1: return R::WIFI_1;
    case 2: return R::WIFI_2;
    case 3: return R::WIFI_3;
    default: return R::WIFI_4;
  }
}
#else
// The old bar's cable glyph: green with the link up (`> -100`), grey without.
static R esperado( int rssi ) { return rssi > ANTIGO_SEM_LINK ? R::CABO : R::CABO_SEM_LINK; }
#endif

struct Ponto {
  int rssi;
  R wifi, cabo;
  const char *quando;
};
// Written out, not derived: the brief's strong, each threshold of the old ladder and the step beside it, weak, and disconnected.
static const Ponto kPontos[] = {
  { -30, R::WIFI_4, R::CABO, "strong" },
  { -54, R::WIFI_4, R::CABO, "just above the 4-bar threshold (-55): four bars" },
  { -55, R::WIFI_3, R::CABO, "at the 4-bar threshold: three bars" },
  { -60, R::WIFI_3, R::CABO, "three bars" },
  { -64, R::WIFI_3, R::CABO, "just above the 3-bar threshold (-65): three bars" },
  { -65, R::WIFI_2, R::CABO, "at the 3-bar threshold: two bars" },
  { -70, R::WIFI_2, R::CABO, "two bars" },
  { -74, R::WIFI_2, R::CABO, "just above the 2-bar threshold (-75): two bars" },
  { -75, R::WIFI_1, R::CABO, "at the 2-bar threshold: the lowest level, one bar" },
  { -76, R::WIFI_1, R::CABO, "one bar" },
  { -78, R::WIFI_1, R::CABO, "RSSI_MIN_THRESHOLD: from here down the device holds telemetry; one bar" },
  { -90, R::WIFI_1, R::CABO, "weak" },
  { -99, R::WIFI_1, R::CABO, "the weakest link: one bar, above the no-link threshold" },
  { -100, R::WIFI_SEM, R::CABO_SEM_LINK, "disconnected: getRssi( )'s answer for a link that is not up, and the shared state's first value" },
  { -101, R::WIFI_SEM, R::CABO_SEM_LINK, "below the no-link threshold" },
  { -127, R::WIFI_SEM, R::CABO_SEM_LINK, "far below" },
  { 0, R::WIFI_4, R::CABO, "0 dBm" },
};

static void niveis( ) {
  for ( int r = -200; r <= 20; r++ ) {
    const ua::VmStatus s = vmDoNivel( r );
    CONFERE( s.rede == esperado( r ), "level %d: the glue answers %s, the old bar's ladder says %s", r, nome( s.rede ), nome( esperado( r ) ) );
  }
  const int extremos[] = { INT_MIN, INT_MIN + 1, -32768, -1000, 1000, 32767, INT_MAX - 1, INT_MAX };
  for ( int r : extremos ) {
    const ua::VmStatus s = vmDoNivel( r );
    CONFERE( s.rede == esperado( r ), "level %d: the glue answers %s, the old bar's ladder says %s", r, nome( s.rede ), nome( esperado( r ) ) );
  }
  for ( const Ponto &p : kPontos ) {
    const R quer = SIMUT_NET_WIFI ? p.wifi : p.cabo;
    const ua::VmStatus s = vmDoNivel( p.rssi );
    CONFERE( s.rede == quer, "%d dBm (%s): the glue answers %s, expected %s", p.rssi, p.quando, nome( s.rede ), nome( quer ) );
    CONFERE( quer == esperado( p.rssi ), "%d dBm (%s): the named point and the model of the old ladder disagree (%s / %s)", p.rssi, p.quando, nome( quer ), nome( esperado( p.rssi ) ) );
  }
  // No level at all gives NENHUMA nor a state of the other network.
  for ( int r = -200; r <= 20; r++ ) {
    const R got = vmDoNivel( r ).rede;
    CONFERE( got != R::NENHUMA && ( SIMUT_NET_WIFI ? ( ehWifi( got ) && !ehCabo( got ) ) : ( ehCabo( got ) && !ehWifi( got ) ) ), "level %d: %s is not a state of this image's network", r, nome( got ) );
  }
}

static void campos( ) {
  const ua::VmStatus s = vmDoNivel( -60, 1234, 2 );
  CONFERE( s.pendentes == 1234, "pendentes %u, expected 1234", (unsigned)s.pendentes );
  CONFERE( s.envio == 2, "envio %u, expected 2", (unsigned)s.envio );
  CONFERE( strcmp( s.hora, "14:32" ) == 0, "hora '%s'", s.hora );
  CONFERE( s.aviso == ua::Aviso::NENHUM, "aviso %d, expected none", (int)s.aviso );
  const ua::VmStatus t = vmDoNivel( -60, 0, 1 );
  CONFERE( t.pendentes == 0 && t.envio == 1, "pendentes %u envio %u, expected 0 and 1", (unsigned)t.pendentes, (unsigned)t.envio );
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
class LeitorCanvas : public ua::Leitor {
 public:
  explicit LeitorCanvas( GFXcanvas16 &c ) : c_( c ) {}
  uint16_t ler( int16_t x, int16_t y ) override { return c_.getPixel( x, y ); }

 private:
  GFXcanvas16 &c_;
};

constexpr int LARG = 320, ALT = 40;     // what is kept of a frame: the two strips the status bar spans (ua::LINHAS_DAS_FAIXAS)
constexpr int X0_REDE = 320 - 48;       // the network icon is the only thing in the bar's last 48 columns (no badge, no pending count)
using Pontos = std::vector<std::pair<int, int>>;

// The pixels of a bar that the network icon adds: the bar drawn with this state against the same bar with Rede::NENHUMA.
static Pontos tintaDaRede( const GFXcanvas16 &com, const GFXcanvas16 &sem, std::vector<uint16_t> &cores ) {
  Pontos p;
  for ( int y = 0; y < ua::H_STATUS; y++ )
    for ( int x = X0_REDE; x < LARG; x++ ) {
      const uint16_t a = const_cast<GFXcanvas16 &>( com ).getPixel( x, y ), b = const_cast<GFXcanvas16 &>( sem ).getPixel( x, y );
      if ( a != b ) {
        p.emplace_back( x, y );
        cores.push_back( a );
      }
    }
  return p;
}

static Pontos normalizado( Pontos p ) {
  if ( p.empty( ) ) return p;
  int x0 = INT_MAX, y0 = INT_MAX;
  for ( const auto &q : p ) { x0 = std::min( x0, q.first ); y0 = std::min( y0, q.second ); }
  for ( auto &q : p ) { q.first -= x0; q.second -= y0; }
  std::sort( p.begin( ), p.end( ) );
  return p;
}

// The icon the kit draws alone, 16 px, on a blank canvas.
static Pontos tintaDoIcone( ua::IconeNome n, const ua::Tema &T ) {
  GFXcanvas16 cv( LARG, 240 );
  LeitorCanvas leitor( cv );
  cv.fillScreen( 0 );
  ua::Pintor p( cv, T, leitor );
  p.icone( n, 16, 0, 0, 0xFFFF );
  Pontos pts;
  for ( int y = 0; y < 16; y++ )
    for ( int x = 0; x < 16; x++ )
      if ( cv.getPixel( x, y ) == 0xFFFF ) pts.emplace_back( x, y );
  return pts;
}

// The icon and the ink a state is meant to draw. The neutral grey is the bar's own ink (tinta2): no signal is not an error, so it does not get the danger colour, on the radio or on the cable ("cinza nos dois", 2026-10-30); one bar keeps the alert colour. The cable without link has an icon of its own, unplug ("icone proprio, em cinza", 2026-10-30): the shape carries the state, as on Wi-Fi.
static bool desenhoDoEstado( R r, const ua::Tema &T, ua::IconeNome &ic, uint16_t &cor ) {
  switch ( r ) {
    case R::WIFI_4: ic = ua::IconeNome::SIGNAL; cor = T.tinta2; return true;
    case R::WIFI_3: ic = ua::IconeNome::SIGNAL_HIGH; cor = T.tinta2; return true;
    case R::WIFI_2: ic = ua::IconeNome::SIGNAL_MEDIUM; cor = T.tinta2; return true;
    case R::WIFI_1: ic = ua::IconeNome::SIGNAL_LOW; cor = T.alerta; return true;
    case R::WIFI_SEM: ic = ua::IconeNome::WIFI_OFF; cor = T.tinta2; return true;
    case R::CABO: ic = ua::IconeNome::ETHERNET_PORT; cor = T.tinta2; return true;
    case R::CABO_SEM_LINK: ic = ua::IconeNome::UNPLUG; cor = T.tinta2; return true;
    case R::NENHUMA: return false;
  }
  return false;
}

static uint32_t hash( const GFXcanvas16 &cv ) {
  uint32_t h = 2166136261u;
  const uint16_t *b = const_cast<GFXcanvas16 &>( cv ).getBuffer( );
  for ( int i = 0; i < LARG * ALT; i++ ) h = ( h ^ b[i] ) * 16777619u;
  return h;
}

struct Quadro {
  const char *nome;
  int rssi;
  R quer;
};
#if SIMUT_NET_WIFI
static const Quadro kQuadros[] = {
  { "forte", -40, R::WIFI_4 },      { "limite_4", -54, R::WIFI_4 },  { "limite_3", -55, R::WIFI_3 },  { "tres_meio", -60, R::WIFI_3 },
  { "tres_baixo", -64, R::WIFI_3 }, { "limite_2", -65, R::WIFI_2 },  { "duas_meio", -70, R::WIFI_2 }, { "duas_baixo", -74, R::WIFI_2 },
  { "limite_1", -75, R::WIFI_1 },   { "uma_fraco", -90, R::WIFI_1 }, { "uma_pior", -99, R::WIFI_1 },  { "sem", -100, R::WIFI_SEM },
};
#else
static const Quadro kQuadros[] = {
  { "cabo", -40, R::CABO },
  { "cabo_sem", -100, R::CABO_SEM_LINK },
};
#endif
constexpr int N_QUADROS = (int)( sizeof kQuadros / sizeof kQuadros[0] );

static void desenharBarra( GFXcanvas16 &canvas, const ua::Tema &tema, const ua::VmStatus &vm ) {
  LeitorCanvas leitor( canvas );
  canvas.fillScreen( tema.fundo );
  ua::Pintor p( canvas, tema, leitor );
  p.bruto( canvas.getBuffer( ), LARG, 0, 240 );
  ua::desenharStatus( p, vm );
  ua::margemPreta( p );
}

// No network state draws in the danger colour ("cinza nos dois", 2026-10-30): a link that is down is not an error, on a cable or on the radio. Every state of the renderer, whichever image this is, in both themes.
static void semVermelho( ) {
  const R todos[] = { R::CABO, R::CABO_SEM_LINK, R::WIFI_SEM, R::WIFI_1, R::WIFI_2, R::WIFI_3, R::WIFI_4 };
  for ( int t = 0; t < 2; t++ ) {
    const ua::Tema &tema = ua::tema( t == 0, 0 );
    for ( R r : todos ) {
      GFXcanvas16 canvas( LARG, 240 );
      ua::VmStatus vm = vmDoNivel( -60 );
      vm.rede = r;
      desenharBarra( canvas, tema, vm );
      int n = 0;
      for ( int y = 0; y < ua::H_STATUS; y++ )
        for ( int x = X0_REDE; x < LARG; x++ )
          if ( canvas.getPixel( x, y ) == tema.perigo ) n++;
      CONFERE( n == 0, "%s, %s theme: %d danger-colour pixels in the network icon's corner (no network state draws in the danger colour)", nome( r ), t ? "light" : "dark", n );
    }
  }
}

static void quadros( const char *pasta ) {
  uint32_t hashes[2][N_QUADROS] = { };
  FILE *ordem = pasta ? [&] { char path[512]; snprintf( path, sizeof path, "%s/_ordem.txt", pasta ); return fopen( path, "w" ); }( ) : nullptr;
  for ( int t = 0; t < 2; t++ ) {
    const ua::Tema &tema = ua::tema( t == 0, 0 );
    const char *nomeTema = t ? "claro" : "escuro";
    for ( int q = 0; q < N_QUADROS; q++ ) {
      const Quadro &Q = kQuadros[q];
      GFXcanvas16 canvas( LARG, 240 ), vazio( LARG, 240 );
      const uint32_t faltavam = ua::glifosFaltando( );
      const ua::VmStatus vm = vmDoNivel( Q.rssi );
      CONFERE( vm.rede == Q.quer, "frame %s_%s: the glue answers %s, expected %s", Q.nome, nomeTema, nome( vm.rede ), nome( Q.quer ) );
      desenharBarra( canvas, tema, vm );
      ua::VmStatus semRede = vm;
      semRede.rede = R::NENHUMA;
      desenharBarra( vazio, tema, semRede );
      ua::IconeNome ic = ua::IconeNome::NENHUM;
      uint16_t cor = 0;
      CONFERE( desenhoDoEstado( Q.quer, tema, ic, cor ), "frame %s_%s: no drawing is defined for %s", Q.nome, nomeTema, nome( Q.quer ) );
      std::vector<uint16_t> cores;
      const Pontos tinta = tintaDaRede( canvas, vazio, cores );
      const Pontos ref = tintaDoIcone( ic, tema );
      CONFERE( !ref.empty( ), "frame %s_%s: the kit draws nothing for the icon of %s", Q.nome, nomeTema, nome( Q.quer ) );
      CONFERE( normalizado( tinta ) == normalizado( ref ), "frame %s_%s: the network icon the bar adds (%zu px) is not the expected Lucide icon (%zu px) for %s", Q.nome, nomeTema, tinta.size( ), ref.size( ), nome( Q.quer ) );
      bool tudoNaCor = !cores.empty( );
      for ( uint16_t c : cores ) tudoNaCor = tudoNaCor && c == cor;
      CONFERE( tudoNaCor, "frame %s_%s: the network icon of %s is not all in the expected ink 0x%04x", Q.nome, nomeTema, nome( Q.quer ), cor );
      CONFERE( ua::glifosFaltando( ) == faltavam, "frame %s_%s: the kit could not draw a glyph or an icon", Q.nome, nomeTema );
      hashes[t][q] = hash( canvas );
      if ( pasta ) {
        char path[512];
        snprintf( path, sizeof path, "%s/%s_%s.565", pasta, Q.nome, nomeTema );
        FILE *f = fopen( path, "wb" );
        if ( !f ) { perror( path ); g_falhas++; continue; }
        fwrite( canvas.getBuffer( ), 2, LARG * ALT, f );
        fclose( f );
        if ( ordem && t == 0 ) fprintf( ordem, "%s %d %s\n", Q.nome, Q.rssi, nome( Q.quer ) );
      }
    }
    for ( int a = 0; a < N_QUADROS; a++ )
      for ( int b = a + 1; b < N_QUADROS; b++ ) {
        // Two levels of one state (-40 and -54, say) draw the same bar on purpose; every other pair is a different state, the cable's two included and must be a different picture.
        const bool igual = kQuadros[a].quer == kQuadros[b].quer;
        CONFERE( igual ? hashes[t][a] == hashes[t][b] : hashes[t][a] != hashes[t][b], "frames %s and %s (%s): %s", kQuadros[a].nome, kQuadros[b].nome, nomeTema,
                 igual ? "the same state must draw the same bar" : "two states drew the same bar" );
      }
  }
  if ( ordem ) fclose( ordem );
#if SIMUT_NET_WIFI
  // The five Wi-Fi icons are five different shapes (the kit's own registry, not the glue's).
  const ua::Tema &T0 = ua::tema( true, 0 );
  const ua::IconeNome nomes[] = { ua::IconeNome::SIGNAL, ua::IconeNome::SIGNAL_HIGH, ua::IconeNome::SIGNAL_MEDIUM, ua::IconeNome::SIGNAL_LOW, ua::IconeNome::WIFI_OFF };
  for ( int a = 0; a < 5; a++ )
    for ( int b = a + 1; b < 5; b++ )
      CONFERE( normalizado( tintaDoIcone( nomes[a], T0 ) ) != normalizado( tintaDoIcone( nomes[b], T0 ) ), "the Wi-Fi icons %d and %d are the same shape", a, b );
#else
  // The cable's two icons are two shapes of the kit's registry (the state is carried by the shape, as on Wi-Fi), both drawn in the same ink.
  {
    const ua::Tema &T0 = ua::tema( true, 0 );
    CONFERE( normalizado( tintaDoIcone( ua::IconeNome::ETHERNET_PORT, T0 ) ) != normalizado( tintaDoIcone( ua::IconeNome::UNPLUG, T0 ) ), "the cable's two icons, ethernet-port and unplug, are the same shape" );
  }
#endif
}

// The pieces a mask is made of: its 8-connected components (the dot and each bar of a Lucide signal icon are one piece each).
static int pecas( const Pontos &pts ) {
  bool m[16][16] = { };
  for ( const auto &q : pts ) m[q.second][q.first] = true;
  int n = 0;
  for ( int y = 0; y < 16; y++ )
    for ( int x = 0; x < 16; x++ ) {
      if ( !m[y][x] ) continue;
      n++;
      std::vector<std::pair<int, int>> pilha = { { x, y } };
      m[y][x] = false;
      while ( !pilha.empty( ) ) {
        const auto [cx, cy] = pilha.back( );
        pilha.pop_back( );
        for ( int dy = -1; dy <= 1; dy++ )
          for ( int dx = -1; dx <= 1; dx++ ) {
            const int nx = cx + dx, ny = cy + dy;
            if ( nx >= 0 && nx < 16 && ny >= 0 && ny < 16 && m[ny][nx] ) {
              m[ny][nx] = false;
              pilha.emplace_back( nx, ny );
            }
          }
      }
    }
  return n;
}

// The lit pixels and the pieces of each network icon at 16 px: what the bar weighs on the glass, for the record, and, for the signal icons, the registry's CONTENT. The frames cannot see it (they compare the
// bar with the kit's own drawing of the same icon, which goes through the same registry: two rows with each other's bitmap draw alike), so the four signal icons are held here to what they are: the lit pixels grow with
// the level, and each is made of as many pieces as Lucide 0.468.0's svg has elements, 2, 3, 4 and 5 (the dot and one bar more at each level; counted in the svg files on 2026-10-30).
static void contagens( ) {
  const ua::Tema &T0 = ua::tema( true, 0 );
  struct Linha { const char *nome; ua::IconeNome ic; };
#if SIMUT_NET_WIFI
  const Linha v[] = { { "signal-low", ua::IconeNome::SIGNAL_LOW }, { "signal-medium", ua::IconeNome::SIGNAL_MEDIUM }, { "signal-high", ua::IconeNome::SIGNAL_HIGH },
                      { "signal", ua::IconeNome::SIGNAL }, { "wifi-off", ua::IconeNome::WIFI_OFF } };
#else
  const Linha v[] = { { "ethernet-port", ua::IconeNome::ETHERNET_PORT }, { "unplug", ua::IconeNome::UNPLUG } };
#endif
  size_t px[8] = { };
  int pc[8] = { };
  int k = 0;
  for ( const Linha &e : v ) {
    const Pontos p = tintaDoIcone( e.ic, T0 );
    px[k] = p.size( );
    pc[k] = pecas( p );
    printf( "icon %-14s %3zu lit px at 16 px, %d piece(s)\n", e.nome, px[k], pc[k] );
    k++;
  }
#if SIMUT_NET_WIFI
  CONFERE( px[0] < px[1] && px[1] < px[2] && px[2] < px[3], "the lit pixels of signal-low, -medium, -high and signal (%zu, %zu, %zu, %zu) do not grow with the level: a registry row has another icon's bitmap", px[0], px[1], px[2], px[3] );
  for ( int i = 0; i < 4; i++ ) CONFERE( pc[i] == i + 2, "%s is made of %d piece(s), the Lucide svg has %d elements", v[i].nome, pc[i], i + 2 );
#endif
}

int main( int argc, char **argv ) {
  niveis( );
  campos( );
  semVermelho( );
  quadros( argc > 1 ? argv[1] : nullptr );
  contagens( );
  printf( "%s image: %d checks, %d failed\n", SIMUT_NET_WIFI ? "Wi-Fi" : "cable", g_confere, g_falhas );
  return g_falhas ? 1 : 0;
}
