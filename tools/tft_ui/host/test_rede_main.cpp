// test_rede_main.cpp -- the Ângulo glue's network glyph (DisplayManager::uaVmStatus( )), RUN on the host (S10c, the Wi-Fi status bar). Built and run by tools/tft_ui/test_rede.py.
//
// DisplayManager_UiA.cpp cannot be linked here as a file (it is a DisplayManager, with two displays, a touch panel and two cores behind it), so the python side cuts the one member out of it,
// verbatim and without comments (uaVmStatus.inc: its head and its body, with the #if SIMUT_NET_WIFI it holds), and this file compiles that against a stand-in (the manner of test_confirma_ap_main.cpp).
// The kit (Telas.cpp, Base.cpp, Angulo.cpp, Textos.cpp) is the real one. -DSIMUT_NET_WIFI=1 builds a Wi-Fi image's branch, =0 the cable's.
//
// What runs, against the OLD top bar's ladder (DisplayManager::drawTopBar( ), DisplayManager_Dashboard.cpp), whose thresholds the python side read out of that file (esperado.inc):
//   1. every level from -200 to +20 dBm and the extremes of int: the glue's Rede equals the model (Wi-Fi: no bar -> WIFI_SEM, the lowest level, one bar -> WIFI_FRACO, two to four bars -> WIFI;
//      cable: above -100 CABO, else CABO_SEM_LINK), and is never NENHUMA nor a state of the other network;
//   2. the named points of the brief (strong, each threshold and the step beside it, weak, disconnected), written out in this file and not derived from the model;
//   3. the fields the function does not own (pendentes, envio);
//   4. the frames: the status bar drawn by the real kit (ua::desenharStatus( )) from the view model the glue made, in both themes: no icon missing, the alert colour only in the weak frame, the
//      danger colour only in the disconnected one, the frames of different states different. They are written to the folder in argv[1] (RGB565, 320 x 40) for test_rede.py to picture.

#include <Adafruit_GFX.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static const char *nome( ua::Rede r ) {
  switch ( r ) {
    case ua::Rede::NENHUMA: return "NENHUMA";
    case ua::Rede::CABO: return "CABO";
    case ua::Rede::CABO_SEM_LINK: return "CABO_SEM_LINK";
    case ua::Rede::WIFI: return "WIFI";
    case ua::Rede::WIFI_FRACO: return "WIFI_FRACO";
    case ua::Rede::WIFI_SEM: return "WIFI_SEM";
  }
  return "?";
}

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
static ua::Rede esperado( int rssi ) {
  const int b = barras( rssi );
  return b == 0 ? ua::Rede::WIFI_SEM : b == 1 ? ua::Rede::WIFI_FRACO : ua::Rede::WIFI;
}
#else
// The old bar's cable glyph: green with the link up (`> -100`), grey without.
static ua::Rede esperado( int rssi ) { return rssi > ANTIGO_SEM_LINK ? ua::Rede::CABO : ua::Rede::CABO_SEM_LINK; }
#endif

struct Ponto {
  int rssi;
  ua::Rede wifi, cabo;
  const char *quando;
};
// Written out, not derived: the brief's strong, each threshold of the old ladder and the step beside it, weak, and disconnected.
static const Ponto kPontos[] = {
  { -30, ua::Rede::WIFI, ua::Rede::CABO, "strong" },
  { -54, ua::Rede::WIFI, ua::Rede::CABO, "just above the 4-bar threshold (-55): four bars" },
  { -55, ua::Rede::WIFI, ua::Rede::CABO, "at the 4-bar threshold: three bars" },
  { -64, ua::Rede::WIFI, ua::Rede::CABO, "just above the 3-bar threshold (-65): three bars" },
  { -65, ua::Rede::WIFI, ua::Rede::CABO, "at the 3-bar threshold: two bars" },
  { -74, ua::Rede::WIFI, ua::Rede::CABO, "just above the 2-bar threshold (-75): two bars" },
  { -75, ua::Rede::WIFI_FRACO, ua::Rede::CABO, "at the 2-bar threshold: the lowest level, one bar" },
  { -76, ua::Rede::WIFI_FRACO, ua::Rede::CABO, "one bar" },
  { -90, ua::Rede::WIFI_FRACO, ua::Rede::CABO, "weak" },
  { -99, ua::Rede::WIFI_FRACO, ua::Rede::CABO, "the weakest link: one bar, above the no-link threshold" },
  { -100, ua::Rede::WIFI_SEM, ua::Rede::CABO_SEM_LINK, "disconnected: getRssi( )'s answer for a link that is not up, and the shared state's first value" },
  { -101, ua::Rede::WIFI_SEM, ua::Rede::CABO_SEM_LINK, "below the no-link threshold" },
  { -127, ua::Rede::WIFI_SEM, ua::Rede::CABO_SEM_LINK, "far below" },
  { 0, ua::Rede::WIFI, ua::Rede::CABO, "0 dBm" },
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
    const ua::Rede quer = SIMUT_NET_WIFI ? p.wifi : p.cabo;
    const ua::VmStatus s = vmDoNivel( p.rssi );
    CONFERE( s.rede == quer, "%d dBm (%s): the glue answers %s, expected %s", p.rssi, p.quando, nome( s.rede ), nome( quer ) );
    CONFERE( quer == esperado( p.rssi ), "%d dBm (%s): the named point and the model of the old ladder disagree (%s / %s)", p.rssi, p.quando, nome( quer ), nome( esperado( p.rssi ) ) );
  }
  // No level at all gives NENHUMA nor a state of the other network.
  for ( int r = -200; r <= 20; r++ ) {
    const ua::Rede got = vmDoNivel( r ).rede;
    const bool wifi = got == ua::Rede::WIFI || got == ua::Rede::WIFI_FRACO || got == ua::Rede::WIFI_SEM;
    const bool cabo = got == ua::Rede::CABO || got == ua::Rede::CABO_SEM_LINK;
    CONFERE( got != ua::Rede::NENHUMA && ( SIMUT_NET_WIFI ? ( wifi && !cabo ) : ( cabo && !wifi ) ), "level %d: %s is not a state of this image's network", r, nome( got ) );
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

constexpr int LARG = 320, ALT = 40;   // the status bar's room: the two strips the bar spans (ua::LINHAS_DAS_FAIXAS)

static int contaCor( const GFXcanvas16 &cv, uint16_t cor ) {
  int n = 0;
  const uint16_t *b = const_cast<GFXcanvas16 &>( cv ).getBuffer( );
  for ( int i = 0; i < LARG * ALT; i++ )
    if ( b[i] == cor ) n++;
  return n;
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
  ua::Rede quer;
};
#if SIMUT_NET_WIFI
static const Quadro kQuadros[] = {
  { "forte", -40, ua::Rede::WIFI },
  { "limite_bom", -74, ua::Rede::WIFI },
  { "limite_fraco", -75, ua::Rede::WIFI_FRACO },
  { "fraco", -90, ua::Rede::WIFI_FRACO },
  { "sem", -100, ua::Rede::WIFI_SEM },
};
#else
static const Quadro kQuadros[] = {
  { "cabo", -40, ua::Rede::CABO },
  { "cabo_sem", -100, ua::Rede::CABO_SEM_LINK },
};
#endif
constexpr int N_QUADROS = (int)( sizeof kQuadros / sizeof kQuadros[0] );

static void quadros( const char *pasta ) {
  uint32_t hashes[2][N_QUADROS] = { };
  for ( int t = 0; t < 2; t++ ) {
    const ua::Tema &tema = ua::tema( t == 0, 0 );
    const char *nomeTema = t ? "claro" : "escuro";
    for ( int q = 0; q < N_QUADROS; q++ ) {
      const Quadro &Q = kQuadros[q];
      GFXcanvas16 canvas( LARG, 240 );
      LeitorCanvas leitor( canvas );
      canvas.fillScreen( tema.fundo );
      ua::Pintor p( canvas, tema, leitor );
      p.bruto( canvas.getBuffer( ), LARG, 0, 240 );
      const uint32_t faltavam = ua::glifosFaltando( );
      const ua::VmStatus vm = vmDoNivel( Q.rssi );
      CONFERE( vm.rede == Q.quer, "frame %s_%s: the glue answers %s, expected %s", Q.nome, nomeTema, nome( vm.rede ), nome( Q.quer ) );
      ua::desenharStatus( p, vm );
      ua::margemPreta( p );
      CONFERE( ua::glifosFaltando( ) == faltavam, "frame %s_%s: the kit could not draw a glyph or an icon", Q.nome, nomeTema );
      const int alerta = contaCor( canvas, tema.alerta ), perigo = contaCor( canvas, tema.perigo );
      const bool fraco = Q.quer == ua::Rede::WIFI_FRACO;
      const bool sem = Q.quer == ua::Rede::WIFI_SEM || Q.quer == ua::Rede::CABO_SEM_LINK;
      CONFERE( fraco ? alerta > 0 : alerta == 0, "frame %s_%s: %d alert-colour pixels (%s)", Q.nome, nomeTema, alerta, fraco ? "the weak icon is drawn in the alert colour" : "no alert ink expected" );
      CONFERE( sem ? perigo > 0 : perigo == 0, "frame %s_%s: %d danger-colour pixels (%s)", Q.nome, nomeTema, perigo, sem ? "the icon of no link is drawn in the danger colour" : "no danger ink expected" );
      hashes[t][q] = hash( canvas );
      if ( pasta ) {
        char path[512];
        snprintf( path, sizeof path, "%s/%s_%s.565", pasta, Q.nome, nomeTema );
        FILE *f = fopen( path, "wb" );
        if ( !f ) { perror( path ); g_falhas++; continue; }
        fwrite( canvas.getBuffer( ), 2, LARG * ALT, f );
        fclose( f );
      }
    }
    for ( int a = 0; a < N_QUADROS; a++ )
      for ( int b = a + 1; b < N_QUADROS; b++ ) {
        // -40 and -74 are both "good": the same icon on purpose; every other pair is a different state and must be a different picture.
        const bool igual = kQuadros[a].quer == kQuadros[b].quer;
        CONFERE( igual ? hashes[t][a] == hashes[t][b] : hashes[t][a] != hashes[t][b], "frames %s and %s (%s): %s", kQuadros[a].nome, kQuadros[b].nome, nomeTema,
                 igual ? "the same state must draw the same bar" : "two states drew the same bar" );
      }
  }
}

int main( int argc, char **argv ) {
  niveis( );
  campos( );
  quadros( argc > 1 ? argv[1] : nullptr );
  printf( "%s image: %d checks, %d failed\n", SIMUT_NET_WIFI ? "Wi-Fi" : "cable", g_confere, g_falhas );
  return g_falhas ? 1 : 0;
}
