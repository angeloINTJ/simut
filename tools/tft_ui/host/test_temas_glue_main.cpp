// test_temas_glue_main.cpp -- the glue's theme and paint path, RUN on the host (S10c, A'2c and A'2d). Built and run by tools/tft_ui/test_temas.py.
//
// DisplayManager_UiA.cpp cannot be linked here as a file: it is a DisplayManager, with two displays, a touch panel, the saved configuration and two cores behind it.
// Its theme and paint path can. Three reviews in a row (A'2, A'3, A'2b) found mutants of the glue that passed every check of the step, each outside what the test had cut out or
// asserted: the accent never reaching ua::tema( ), the dark mode inverted, the accent cut out of a hash, the preview kept outside the Tema screen, the Pintor handed the stock accent
// at the paint site, the strips cleared to the light background, the screen's mode on the glass fixed at light. Cutting a few lines out and reading the rest as text always left a line
// that nothing ran. So test_temas.py now cuts out whole functions, verbatim, and this file runs them:
//   estado.inc      the file-level state of the glue (the strip constants, the frame on the glass, the sequence counter) and fnv( ), the hash it runs;
//   uatema.inc      DisplayManager::uaTema( UiMode ), the theme of a frame;
//   uarelogio.inc   DisplayManager::uaRelogio( ), the hour of the automatic mode;
//   uapintar.inc    DisplayManager::uaPintar( ), the paint: the strips, their theme, their clear, the margin, the push, what it records of the frame on the glass;
//   uaestado.inc    DisplayManager::uaEstado( ), what GET /api/ui reports of that frame (the screen's mode on the glass, its hash);
//   uaquadro.inc    DisplayManager::uaQuadro( ), the pass that hashes the frame, decides what to repaint and drops the preview;
//   uipaintota.inc  DisplayManager::uiPaintOta( ), the update screen's paint.
// They are compiled against a stand-in for the members of DisplayManager they touch, the kit (Angulo.cpp, Faixa.h) as it is, and Adafruit GFX's canvas. The two things the hardware does
// are the stand-in's: the push of a strip to the panel (here: a record of the strip, copied to an array, the glass, when the push is waited for, as the DMA is; S10c, A'2d) and the clock.
// What the glass must show comes from the generated header and from the rules written out below, never from the kit: a screen of 17 swatches, one per role of the theme, a marker and a
// frame of four bars along the edges of the panel that cross the margin, drawn through the Pintor the glue hands it; the image it must make on the glass is computed here, pixel by pixel,
// from the role values of the generated table of the expected mode and accent, the margin black.
// The pieces are cut from the text without their comments, by brace matching, so that a reformatted signature is still found and a comment that names a function is not taken for it.
//
// What runs, through the glue's own functions:
//   1. uaQuadro( ): every saved index (the palette's 18, out-of-range values, no configuration), every clock line (none, malformed, every hour, provisional), three screens on show:
//      the glass, the screen's mode on it (uaEstado( )), the hash it reports, the strips pushed and the margin pass;
//   2. the clock the automatic mode reads when the state mutex is busy;
//   3. the preview: on the Tema screen's frame alone, dropped by uaQuadro( ) on any other screen, dropped before the line that returns when no look is due;
//   4. uiPaintOta( ): the update screen is painted, and hashed, with the SAVED theme whatever the screen on show and the preview say; its counters (the sequence and the percentage drawn) and its
//      early return within 400 ms (nothing painted or pushed, the sequence still said to be on the glass; S10c, A'2d);
//   5. the repaint decisions: nothing for an unchanged frame, everything for a change of accent alone, of mode alone, of alignment alone, a few strips for a change of the view model,
//      and strips pushed with others skipped between them (a half of the canvas reused while its push is still in flight);
//   6. (S10c, A'2d) the counters Core 0 reads off a pass of uaQuadro( ): _bootPaintSeq (booting, on the dashboard, a painted frame) and the heartbeat (Core 1, a painted frame);
//   7. uaPintar( ) called on its own: it returns with no push in flight, whatever it pushed;
//   and in 1 to 7 the push protocol on every paint (no push in flight when uaPintar( ) returns, no strip written while its push was in flight), uaRelogio( ) at the bounds of the hour and
//   of the tail, and uaEstado( ) with the screen on show not the one on the glass.

#include <Adafruit_GFX.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Angulo.h"
#include "Faixa.h"
#include "UiA.h"
#include "telas/Telas.h"

namespace gerado {
#include "kit/angulo_tokens.h"
}  // namespace gerado

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The stand-in. The firmware's UiMode has more members; these are the names the pieces use.
enum UiMode : uint8_t { MODE_DASHBOARD, MODE_SETTINGS_THEMES, MODE_OTA_UPDATE, MODE_GRAPH_VIEW, MODE_GRAPH_DETAIL, MODE_OUTRO };

static uint32_t g_agora = 1000;   // the clock of millis( ), moved by the checks
static bool g_mutexLivre = true;   // whether the state mutex is taken inside the 500 us the glue allows itself
static int g_nucleo = 0;
uint32_t millis( ) { return g_agora; }
uint32_t micros( ) { return g_agora * 1000u; }
void delayMicroseconds( unsigned ) {}
int get_core_num( ) { return g_nucleo; }
inline void __dmb( ) {}
struct mutex_t { int livre; };
bool mutex_enter_timeout_us( mutex_t *, uint32_t ) { return g_mutexLivre; }
void mutex_exit( mutex_t * ) {}
volatile int8_t g_uaNav[5] = { UA_NAV_NADA, UA_NAV_NADA, UA_NAV_NADA, UA_NAV_NADA, UA_NAV_NADA };

constexpr uint32_t OTA_PH_NONE = 0;   // the update's state word: the phase in the top byte, the sequence below it
inline uint32_t otaPhaseOf( uint32_t st ) { return st >> 24; }
inline uint32_t otaSeqOf( uint32_t st ) { return st & 0xFFFFFFu; }

extern int g_violacoes;
bool barramentoOcupado( );

class TftFalso {
 public:
  int8_t ox = 0, oy = 0;   // the display alignment the identity hashes
  int margensPretas = 0;   // how many times the margin band was blackened (a full repaint does it)
  int8_t getOffsetX( ) const { return ox; }
  int8_t getOffsetY( ) const { return oy; }
  void fillMarginsBlack( ) { margensPretas++; if ( barramentoOcupado( ) ) g_violacoes++; }
};

struct DisplayDriver {
  static constexpr int W = 320, H = 240;
  GFXcanvas16 *canvas = nullptr;
  TftFalso *tft = nullptr;
};

struct SystemState {
  char timeString[32];
  int8_t selectedSlotIdx, topSlotIdx;
  bool isBooting;
};
struct PainelFalso { bool fixed; int8_t fixedIdx; bool showMinMax; };
struct GraficoFalso { uint32_t tsCutoff, tsEnd; };
struct ConfigFalsa { int8_t themeIndex; };   // the one field of the saved configuration the pieces read

class DisplayManager {
 public:
  // the glue's own functions, cut out of DisplayManager_UiA.cpp
  const ua::Tema &uaTema( UiMode modo );
  bool uaRelogio( char hora[6], int &h, bool &provisoria );
  bool uaPintar( const UaQuadro &q, UiMode modo, uint32_t hash );
  int8_t uaEstado( UaEstado &e ) const;
  void uaQuadro( );
  void uiPaintOta( );

  // what they call: the frame the screen on show builds, the panel's push, the update screen's frame
  bool uaMontar( UaQuadro &q );
  bool uaMontarOta( UaQuadro &q );
  bool uaEmpurrar( const uint16_t *buf, int x, int y, int w, int h );
  void uaEmpurrarEsperar( );

  // the state they read
  mutex_t _stateMutex = {};
  SystemState _lastRenderedState = {}, _sharedState = {};
  UiMode _uiMode = MODE_DASHBOARD;
  int8_t _uaPrevia = -1;
  const ConfigFalsa *_sysConfigPtr = nullptr;
  DisplayDriver _driver;
  bool _uaTudo = false, _uaSujo = false;
  uint32_t _uaVisto = 0, _bootPaintSeq = 0, _lastHeartbeat = 0;
  PainelFalso _topPanel = {}, _bottomPanel = {};
  GraficoFalso _graphData = {};
  uint32_t _otaState = 0, _otaDrawnSeq = 0, _otaPctDrawn = 0, _otaPct = 0;
  bool _otaRedrawAll = false;
  int esperas = 0;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The glass, and the screen the checks put on it.
constexpr int PAPEIS = 17;
static_assert( sizeof( gerado::Tema ) == PAPEIS * sizeof( uint16_t ), "the generated Tema is the 17 roles and nothing else" );
constexpr int LV = 320, AV = 240, ALT_FAIXA = 20, N_FAIXAS = AV / ALT_FAIXA;
constexpr uint16_t SENTINELA = 0x0001;   // what the glass holds where nothing was pushed: no colour of any table (checked below)
uint16_t g_vidro[AV][LV];
constexpr int MARGEM_VIDRO = 4;                 // the glass's margin, written out here and not read from the kit: the 4 px around the panel are black in every theme (ua::margemPreta( ))
constexpr int BARRA = MARGEM_VIDRO + 2;         // the bars the screen draws along the four edges: wider than the margin, so that they cross it
// The push in flight (S10c, A'2d), at most one: the hardware has one channel and one bus. See DisplayManager::uaEmpurrar( ) below.
struct EmVoo {
  const uint16_t *buf = nullptr;                // the strip the burst reads
  int x = 0, y = 0, w = 0, h = 0;               // and where it goes
  uint16_t copia[LV * ALT_FAIXA];               // the strip as it was when the burst started
};
EmVoo g_emVoo;
int g_empurradas = 0, g_terminadas = 0, g_violacoes = 0;
bool barramentoOcupado( ) { return g_emVoo.buf != nullptr; }
// The overlap of compose and push (S10c, A'2e, the review of A'2d and A'6): the waits the glue asks for itself while a push is in flight. uaEmpurrar( ) waits
// too (one channel), and that wait is the hardware's rule, not the glue's, so it is not counted. A paint that pushes every strip has one such wait, the last; a strip
// skipped between two pushed ones adds the wait before its half of the canvas is composed again. A glue that waits after every push, or before every strip,
// paints the same pixels with compose and push one after the other: only this count sees it.
bool g_noEmpurrar = false;
int g_esperasDaCola = 0;

// Role r of a table, read from its bytes (the 17 roles come first, in the header's order).
uint16_t papel( const void *t, int r ) {
  uint16_t w;
  memcpy( &w, static_cast<const char *>( t ) + r * sizeof w, sizeof w );
  return w;
}

// Swatch r (the role r, 40 x 16 px) sits in column r % 6 and in the strip r / 6 + 1; the marker (6 x 6, in the ink role) in the strip 4 + n, n the view model's byte. So a change of accent
// alone changes the strips 1 to 3, and a change of n alone two strips: the repaint decisions have strips that do not change to tell them apart.
constexpr int SW_W = 40, SW_H = 16, MK_X = 6, MK_L = 6;
int swX( int r ) { return 20 + 48 * ( r % 6 ); }
int swY( int r ) { return ALT_FAIXA * ( r / 6 + 1 ) + 2; }
int mkY( int n ) { return ALT_FAIXA * ( 4 + n ) + 6; }

struct VmFalso { uint8_t n; uint8_t reservado[3]; };   // hashed byte for byte, the padding zero
VmFalso g_vm, g_vmOta;                                  // the frame of the screen on show, and the update screen's

void desenharFalso( ua::Pintor &p, const void *vm ) {
  const ua::Tema &t = p.t( );   // the theme the glue handed the Pintor, and nothing else
  // A frame of four bars along the edges, BARRA px thick, in a role that is no black in any theme (checked in 0): the screen draws over the margin the glue blackens AFTER it
  // (ua::margemPreta( ), DisplayManager_UiA.cpp), so a margin blackened before the screen, or not in every strip, shows as a bar's colour on the glass (S10c, A'2d, the review's P1).
  p.fillRect( 0, 0, LV, BARRA, t.linhaForte );
  p.fillRect( 0, (int16_t)( AV - BARRA ), LV, BARRA, t.linhaForte );
  p.fillRect( 0, 0, BARRA, AV, t.linhaForte );
  p.fillRect( (int16_t)( LV - BARRA ), 0, BARRA, AV, t.linhaForte );
  for ( int r = 0; r < PAPEIS; r++ ) p.fillRect( (int16_t)swX( r ), (int16_t)swY( r ), SW_W, SW_H, papel( &t, r ) );
  p.fillRect( MK_X, (int16_t)mkY( static_cast<const VmFalso *>( vm )->n ), MK_L, MK_L, t.tinta );
}
int alvosFalso( const void *, ua::Alvo * ) { return 0; }
UaQuadro quadroDe( const VmFalso &v ) { return UaQuadro{ &v, sizeof v, desenharFalso, alvosFalso }; }

bool DisplayManager::uaMontar( UaQuadro &q ) { q = quadroDe( g_vm ); return true; }
bool DisplayManager::uaMontarOta( UaQuadro &q ) { q = quadroDe( g_vmOta ); return true; }

// The push the hardware does by DMA, DEFERRED as it is (S10c, A'2d). Read from DisplayManager_UiA_Empurrar.cpp: uaEmpurrar( ) starts by calling uaEmpurrarEsperar( ) (one channel, one bus: a second burst cannot
// begin before the first has ended), starts the burst and returns; the burst reads the strip while the caller goes on composing the next one in the canvas's other half, and uaEmpurrarEsperar( ) -- called by the
// caller, or by the next push -- is the only thing that ends it. Both rules are modelled, because the real function has both. So a push here is a record (the strip's pointer and place, and a copy of the strip as it
// was when the burst started) and the strip reaches the glass when the push is waited for, copied from the strip AS IT IS THEN: a caller that writes the strip before waiting for its push puts the wrong strip on the
// glass (and is counted in g_violacoes, which compares the strip with its copy), and a push never waited for never reaches the glass. A real burst reads a little all the time between the start and the end, so a
// caller that waits before it writes gets the right strip whenever the burst reads, and one that does not gets it right or wrong by the luck of the timing: the model makes the wrong one certain.
bool DisplayManager::uaEmpurrar( const uint16_t *buf, int x, int y, int w, int h ) {
  g_noEmpurrar = true;
  uaEmpurrarEsperar( );
  g_noEmpurrar = false;
  if ( (size_t)w * (size_t)h > sizeof g_emVoo.copia / sizeof( uint16_t ) ) {
    printf( "FAIL stand-in: a push of %d x %d px does not fit its record\n", w, h );
    exit( 1 );
  }
  g_empurradas++;
  g_emVoo.buf = buf;
  g_emVoo.x = x;
  g_emVoo.y = y;
  g_emVoo.w = w;
  g_emVoo.h = h;
  memcpy( g_emVoo.copia, buf, (size_t)w * (size_t)h * sizeof( uint16_t ) );
  return true;
}

void DisplayManager::uaEmpurrarEsperar( ) {
  esperas++;
  if ( !g_noEmpurrar && g_emVoo.buf ) g_esperasDaCola++;
  if ( !g_emVoo.buf ) return;   // as the real one: nothing in flight, nothing to wait for
  const EmVoo &v = g_emVoo;
  if ( memcmp( v.copia, v.buf, (size_t)v.w * (size_t)v.h * sizeof( uint16_t ) ) != 0 ) g_violacoes++;   // the strip was written while the burst was reading it
  for ( int r = 0; r < v.h; r++ ) memcpy( &g_vidro[v.y + r][v.x], v.buf + (size_t)r * v.w, (size_t)v.w * sizeof( uint16_t ) );
#ifdef PROVA_PIXEL
  g_vidro[v.y][v.x + 100] ^= 1;   // the control of the instrument: one pixel of every strip wrong, which the glass check must see
#endif
  g_emVoo.buf = nullptr;
  g_terminadas++;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The glue's functions, verbatim.
namespace {
#include "estado.inc"
}  // namespace

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"   // a mutant may stop reading the frame's mode: it must then be caught by what it gives, not by the compiler
#include "uatema.inc"
#include "uarelogio.inc"
#include "uapintar.inc"
#include "uaestado.inc"
#include "uaquadro.inc"
#include "uipaintota.inc"
#pragma GCC diagnostic pop

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
namespace {

int checks = 0, falhas = 0;

// The first failures are printed and the sixth ends the run: a mutant of the glue can fail every one of the thousands of frames below.
#define CONFERE( cond, ... ) do { checks++; if ( !( cond ) ) { falhas++; printf( "FAIL " ); printf( __VA_ARGS__ ); printf( "\n" ); \
                                    if ( falhas >= 6 ) { printf( "%d checks, %d failed (stopped at the sixth)\n", checks, falhas ); exit( 1 ); } } } while ( 0 )

// FNV-1a, 32 bits, written out again here: the model the glue's hash must equal.
uint32_t fnv1a( const void *dados, size_t n, uint32_t h = 2166136261u ) {
  const uint8_t *p = static_cast<const uint8_t *>( dados );
  for ( size_t i = 0; i < n; i++ ) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

// What the glue's hash of a frame must be: the view model, then the five bytes of the identity (screen, dark, accent, alignment).
uint32_t modeloHash( const VmFalso &v, int tela, bool noite, int acento, int ox, int oy ) {
  const uint32_t h = fnv1a( &v, sizeof v );
  const ua::IdentidadeDoQuadro id{ (uint8_t)tela, (uint8_t)noite, (uint8_t)acento, (int8_t)ox, (int8_t)oy };
  return fnv1a( &id, sizeof id, h );
}

const gerado::Tema *const *const kGerado[2] = { gerado::TEMAS_CLARO, gerado::TEMAS_ESCURO };

struct Par { uint8_t modo, acento; };

// The decode, listed and not computed: index m + 3 k is mode m with accent k; anything else means light with the stock accent.
Par decodificado( int t ) {
  for ( int k = 0; k < gerado::ACENTOS_N; k++ )
    for ( int m = 0; m < 3; m++ )
      if ( t == m + 3 * k ) return Par{ (uint8_t)m, (uint8_t)k };
  return Par{ 0, 0 };
}

// Dark from 19:00 to 6:59 in the automatic mode; light with no valid clock.
bool noiteEsperada( int modo, int hora ) { return modo == 1 || ( modo == 2 && hora >= 0 && ( hora >= 19 || hora <= 6 ) ); }

// The image the glass must show for a theme and a marker: the 4 px margin black in every theme, the swatches, the marker, the background everywhere else. Built once per case.
const uint16_t *imagem( bool noite, int acento, int n ) {
  static uint16_t img[2][gerado::ACENTOS_N][8][AV][LV];
  static bool feita[2][gerado::ACENTOS_N][8];
  uint16_t( &m )[AV][LV] = img[noite ? 1 : 0][acento][n];
  if ( !feita[noite ? 1 : 0][acento][n] ) {
    const gerado::Tema *g = kGerado[noite ? 1 : 0][acento];
    for ( int y = 0; y < AV; y++ )
      for ( int x = 0; x < LV; x++ ) {
        uint16_t c = g->fundo;
        if ( x < BARRA || x >= LV - BARRA || y < BARRA || y >= AV - BARRA ) c = g->linhaForte;
        if ( x >= MK_X && x < MK_X + MK_L && y >= mkY( n ) && y < mkY( n ) + MK_L ) c = g->tinta;
        for ( int r = 0; r < PAPEIS; r++ )
          if ( x >= swX( r ) && x < swX( r ) + SW_W && y >= swY( r ) && y < swY( r ) + SW_H ) c = papel( g, r );
        if ( x < MARGEM_VIDRO || x >= LV - MARGEM_VIDRO || y < MARGEM_VIDRO || y >= AV - MARGEM_VIDRO ) c = 0;   // the margin is black over everything the screen drew
        m[y][x] = c;
      }
    feita[noite ? 1 : 0][acento][n] = true;
  }
  return &m[0][0];
}

void sentinelaNoVidro( ) {
  for ( auto &linha : g_vidro )
    for ( auto &px : linha ) px = SENTINELA;
}

// The push protocol (DisplayManager_UiA_Empurrar.cpp), checked after every paint (S10c, A'2d): uaPintar( ) returns with no push in flight (the touch read, the GRAM readback and the update screen rely on a free
// bus), and no strip was written while its push was in flight.
void confereProtocolo( const char *quando ) {
  const int v = g_violacoes;
  g_violacoes = 0;
  CONFERE( g_emVoo.buf == nullptr, "%s: a push is still in flight when uaPintar( ) returned", quando );
  CONFERE( v == 0, "%s: %d strip(s) were written while their push was in flight (a half of the canvas was reused before the push reading it was waited for)", quando, v );
}

// The glass against the image of (theme, marker): every pixel.
void confereVidro( const char *quando, bool noite, int acento, int n ) {
  confereProtocolo( quando );
  const uint16_t *esp = imagem( noite, acento, n );
  checks++;
  if ( memcmp( g_vidro, esp, sizeof g_vidro ) == 0 ) return;
  int dif = 0, fx = 0, fy = 0;
  for ( int y = 0; y < AV; y++ )
    for ( int x = 0; x < LV; x++ )
      if ( g_vidro[y][x] != esp[y * LV + x] ) {
        if ( !dif ) { fx = x; fy = y; }
        dif++;
      }
  CONFERE( false, "%s: the glass differs from the %s theme, accent %d, marker %d: %d pixels, the first at (%d,%d) is %04x and should be %04x", quando, noite ? "dark" : "light", acento, n, dif,
           fx, fy, g_vidro[fy][fx], esp[fy * LV + fx] );
}

uint32_t pinturas( const DisplayManager &dm ) {
  UaEstado e;
  dm.uaEstado( e );
  return e.pinturas;
}

struct Relogio { char texto[32]; int hora; };   // hora: the hour the line says, -1 when there is no valid clock
Relogio g_relogios[48];
int g_nRelogios = 0;

void poeRelogio( const char *t, int hora ) {
  snprintf( g_relogios[g_nRelogios].texto, sizeof g_relogios[0].texto, "%s", t );
  g_relogios[g_nRelogios++].hora = hora;
}

void montaRelogios( ) {
  char b[32];
  poeRelogio( "", -1 );                      // no clock line yet
  poeRelogio( "garbage", -1 );
  poeRelogio( "22/10/26 - 1", -1 );          // too short after the separator
  poeRelogio( "22/10/26 - 99:99:99", -1 );   // an hour out of range
  poeRelogio( "22/10/26 - ab:cd:ef", -1 );
  for ( int h = 0; h < 24; h++ ) {
    snprintf( b, sizeof b, "22/10/26 - %02d:%02d:07", h, ( h * 7 ) % 60 );
    poeRelogio( b, h );
  }
  for ( const int h : { 3, 12, 22 } ) {   // a provisional clock still gives an hour
    snprintf( b, sizeof b, "22/10/26 ? %02d:30:00", h );
    poeRelogio( b, h );
  }
  // The bounds of the hour and of the tail (S10c, A'2d, the review's P4 and P5): 23 is the last hour and 24 and 29 are refused; a tail of 5 characters after the separator is the shortest accepted and 4 is refused;
  // the provisional separator at both. A refused line is no clock: light in the automatic mode.
  poeRelogio( "22/10/26 - 24:00:00", -1 );
  poeRelogio( "22/10/26 - 29:59:59", -1 );
  poeRelogio( "22/10/26 - /9:00:00", -1 );   // an hour of -1
  poeRelogio( "22/10/26 - 23:59", 23 );      // a tail of exactly 5 characters
  poeRelogio( "22/10/26 - 23:5", -1 );       // a tail of 4
  poeRelogio( "22/10/26 ? 23:59", 23 );
  poeRelogio( "22/10/26 ? 23:5", -1 );
  poeRelogio( "22/10/26 ? 24:00:00", -1 );
}

void escreveRelogio( DisplayManager &dm, const char *t ) {
  snprintf( dm._sharedState.timeString, sizeof dm._sharedState.timeString, "%s", t );
  snprintf( dm._lastRenderedState.timeString, sizeof dm._lastRenderedState.timeString, "%s", t );
}

constexpr int SEM_CONFIG = 1000;   // an index that stands for no saved configuration at all

}  // namespace

int main( ) {
  static GFXcanvas16 canvas( 320, 45 );   // the firmware's: the panel's width by the 45 rows of the old screens' canvas
  static TftFalso tft;
  static DisplayManager dm;
  dm._driver.canvas = &canvas;
  dm._driver.tft = &tft;
  montaRelogios( );

  // 0. The instrument: the sentinel is no colour of any table, the 24 swatch-and-marker images tell every theme apart, and a hash model that did not tell the themes apart would pass anything.
  int sentinelaLivre = 1, imagensIguais = 0;
  for ( int m = 0; m < 2; m++ )
    for ( int k = 0; k < gerado::ACENTOS_N; k++ ) {
      for ( int r = 0; r < PAPEIS; r++ ) sentinelaLivre &= papel( kGerado[m][k], r ) != SENTINELA;
      for ( int m2 = 0; m2 < 2; m2++ )
        for ( int k2 = 0; k2 < gerado::ACENTOS_N; k2++ )
          if ( ( m2 > m || ( m2 == m && k2 > k ) ) && memcmp( imagem( m, k, 0 ), imagem( m2, k2, 0 ), AV * LV * sizeof( uint16_t ) ) == 0 ) imagensIguais++;
    }
  CONFERE( sentinelaLivre, "the glass's sentinel %04x is a colour of a theme", SENTINELA );
  int barraVisivel = 1;   // the bars the screen draws over the margin must be seen there: neither black (the margin's) nor the background's
  for ( int m = 0; m < 2; m++ )
    for ( int k = 0; k < gerado::ACENTOS_N; k++ ) barraVisivel &= kGerado[m][k]->linhaForte != 0 && kGerado[m][k]->linhaForte != kGerado[m][k]->fundo;
  CONFERE( barraVisivel, "the colour of the bars is black or the background in a theme: a margin blackened wrongly could not be seen" );
  CONFERE( imagensIguais == 0, "%d pairs of themes give the same image on the glass: the check could not tell them apart", imagensIguais );
  uint32_t modelos[2 * gerado::ACENTOS_N];
  for ( int i = 0; i < 2 * gerado::ACENTOS_N; i++ ) {
    modelos[i] = modeloHash( g_vm, MODE_DASHBOARD, i / gerado::ACENTOS_N == 1, i % gerado::ACENTOS_N, 3, -2 );
    for ( int j = 0; j < i; j++ ) CONFERE( modelos[i] != modelos[j], "the model gives the themes %d and %d the same hash %08x", i, j, modelos[i] );
  }
  CONFERE( modeloHash( g_vm, MODE_DASHBOARD, false, 0, 3, -2 ) != modeloHash( g_vm, MODE_DASHBOARD, false, 0, -2, 3 ), "the model does not tell ox and oy apart" );
  CONFERE( modeloHash( g_vm, MODE_DASHBOARD, false, 0, 3, -2 ) != modeloHash( g_vm, MODE_SETTINGS_THEMES, false, 0, 3, -2 ), "the model does not tell the screens apart" );

  // 1. uaQuadro( ): every saved index and clock, three screens on show. A full repaint is asked for each time, as uiThemeChanged( ) does, and the glass starts as the sentinel: a strip not pushed shows.
  g_vm = VmFalso{ 2, { 0, 0, 0 } };
  const int indices[] = { -128, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 127, SEM_CONFIG };
  const UiMode telas[3] = { MODE_DASHBOARD, MODE_SETTINGS_THEMES, MODE_OUTRO };
  int pintadas = 0;
  for ( const int idx : indices ) {
    const ConfigFalsa conf{ (int8_t)( idx == SEM_CONFIG ? 0 : idx ) };
    dm._sysConfigPtr = idx == SEM_CONFIG ? nullptr : &conf;
    const Par p = decodificado( idx == SEM_CONFIG ? 0 : idx );
    tft.ox = idx >= 0 && idx < 3 ? 0 : 3;   // the alignment the identity hashes, zero for some
    tft.oy = idx >= 0 && idx < 3 ? 0 : -2;
    for ( int ri = 0; ri < g_nRelogios; ri++ ) {
      escreveRelogio( dm, g_relogios[ri].texto );
      const bool noite = noiteEsperada( p.modo, g_relogios[ri].hora );
      for ( const UiMode tela : telas ) {
        dm._uiMode = tela;
        dm._uaPrevia = -1;
        sentinelaNoVidro( );
        dm._uaTudo = true;
        g_agora += 300;
        const uint32_t antes = pinturas( dm );
        const int margens = tft.margensPretas;
        dm.uaQuadro( );
        UaEstado e;
        const int8_t r = dm.uaEstado( e );
        char quando[96];
        snprintf( quando, sizeof quando, "index %d, clock '%s', screen %d", idx, g_relogios[ri].texto, (int)tela );
        CONFERE( r == 1 && e.pinturas == antes + 1, "%s: the frame was not painted once (uaEstado says %d, %u paints after %u)", quando, (int)r, (unsigned)e.pinturas, (unsigned)antes );
        confereVidro( quando, noite, p.acento, 2 );
        CONFERE( e.escuro == noite, "%s: the mode on the glass says dark %d, expected %d", quando, (int)e.escuro, (int)noite );
        CONFERE( e.modo == tela && e.modoAtual == tela, "%s: the frame on the glass is for screen %d (now %d), expected %d", quando, (int)e.modo, (int)e.modoAtual, (int)tela );
        CONFERE( e.hash == modeloHash( g_vm, tela, noite, p.acento, tft.ox, tft.oy ), "%s: the hash is %08x, the model says %08x", quando, (unsigned)e.hash,
                 (unsigned)modeloHash( g_vm, tela, noite, p.acento, tft.ox, tft.oy ) );
        CONFERE( e.faixas == N_FAIXAS, "%s: %d strips pushed on a full repaint, expected %d", quando, (int)e.faixas, N_FAIXAS );
        CONFERE( !dm._uaTudo && tft.margensPretas == margens + 1, "%s: the full repaint was not taken (flag %d) or the margin not blackened (%d times)", quando, (int)dm._uaTudo,
                 tft.margensPretas - margens );
        pintadas++;
      }
    }
  }

  // 1b. What the web reads of the frame (GET /api/ui, uaEstado( )): e.modo is the screen of the frame ON THE GLASS and e.modoAtual the screen on show now. They are the same right after a pass and differ from
  //     the moment the mode changes until the next pass paints it (S10c, A'2d, the review's P10).
  int estados = 0;
  {
    const ConfigFalsa conf{ 0 };
    dm._sysConfigPtr = &conf;
    escreveRelogio( dm, "" );
    dm._uaPrevia = -1;
    tft.ox = 0;
    tft.oy = 0;
    const UiMode vistas[] = { MODE_SETTINGS_THEMES, MODE_GRAPH_VIEW, MODE_OTA_UPDATE, MODE_OUTRO, MODE_DASHBOARD };
    for ( const UiMode noVidro : { MODE_DASHBOARD, MODE_SETTINGS_THEMES } ) {
      dm._uiMode = noVidro;
      dm._uaTudo = true;
      g_agora += 300;
      dm.uaQuadro( );   // the glass holds this screen's frame
      for ( const UiMode vista : vistas ) {
        dm._uiMode = vista;   // the screen on show changes, nothing is painted
        UaEstado e;
        const int8_t r = dm.uaEstado( e );
        CONFERE( r == 1 && e.modo == noVidro && e.modoAtual == vista, "the frame on the glass is for screen %d and the screen on show is %d: uaEstado( ) says %d and %d (read %d)", (int)noVidro, (int)vista,
                 (int)e.modo, (int)e.modoAtual, (int)r );
        estados++;
      }
    }
  }

  // 2. The clock the automatic mode reads: the shared line while the state mutex is free, the last rendered one when it is not.
  {
    const ConfigFalsa conf{ 2 };   // automatic, stock accent
    dm._sysConfigPtr = &conf;
    dm._uiMode = MODE_DASHBOARD;
    dm._uaPrevia = -1;
    tft.ox = 0;
    tft.oy = 0;
    for ( const bool livre : { true, false } )
      for ( const bool diaNoShared : { true, false } ) {
        g_mutexLivre = livre;
        snprintf( dm._sharedState.timeString, sizeof dm._sharedState.timeString, diaNoShared ? "22/10/26 - 12:00:00" : "22/10/26 - 22:00:00" );
        snprintf( dm._lastRenderedState.timeString, sizeof dm._lastRenderedState.timeString, diaNoShared ? "22/10/26 - 22:00:00" : "22/10/26 - 12:00:00" );
        const bool noite = livre ? !diaNoShared : diaNoShared;   // the line it must read: the shared one when free, the rendered one when busy
        sentinelaNoVidro( );
        dm._uaTudo = true;
        g_agora += 300;
        dm.uaQuadro( );
        UaEstado e;
        dm.uaEstado( e );
        char quando[64];
        snprintf( quando, sizeof quando, "mutex %s, the shared line says %s", livre ? "free" : "busy", diaNoShared ? "noon" : "ten at night" );
        confereVidro( quando, noite, 0, 2 );
        CONFERE( e.escuro == noite, "%s: the mode on the glass says dark %d, expected %d", quando, (int)e.escuro, (int)noite );
        pintadas++;
      }
    g_mutexLivre = true;
  }

  // 2b. uaRelogio( ) itself, at the bounds of the hour and of the tail (S10c, A'2d, the review's P4 and P5): what it returns, the hour and the "HH:MM" it gives, and the separator it saw. 23 is the last hour; 24 and 29 are
  //     refused but the hour and the text are still given; a tail of exactly 5 characters after the separator is the shortest accepted and a tail of 4 is refused with no hour and no text; the provisional separator
  //     " ? " is seen at both. Each line is read from the buffer the function must read (the shared one while the state mutex is free, the last rendered one when it is not) with a valid line of another hour in the other.
  int relogios = 0;
  {
    struct CasoRelogio { const char *linha; bool ok; int h; const char *hora; bool provisoria; };
    static const CasoRelogio casos[] = {
      { "22/10/26 - 23:59:59", true, 23, "23:59", false },
      { "22/10/26 - 24:00:00", false, 24, "24:00", false },
      { "22/10/26 - 29:59:59", false, 29, "29:59", false },
      { "22/10/26 - 00:00:00", true, 0, "00:00", false },
      { "22/10/26 - /9:00:00", false, -1, "/9:00", false },   // an hour of -1: refused
      { "22/10/26 - 12:34", true, 12, "12:34", false },        // a tail of exactly 5 characters
      { "22/10/26 - 12:3", false, -1, "", false },              // a tail of 4
      { "22/10/26 ? 12:34", true, 12, "12:34", true },
      { "22/10/26 ? 12:3", false, -1, "", true },
      { "22/10/26 ? 23:59:59", true, 23, "23:59", true },
      { "22/10/26 ? 24:00:00", false, 24, "24:00", true },
      { "garbage", false, -1, "", false },
      { "", false, -1, "", false },
    };
    for ( const bool livre : { true, false } )
      for ( const CasoRelogio &c : casos ) {
        g_mutexLivre = livre;
        char *lida = livre ? dm._sharedState.timeString : dm._lastRenderedState.timeString;
        char *outra = livre ? dm._lastRenderedState.timeString : dm._sharedState.timeString;
        snprintf( lida, sizeof dm._sharedState.timeString, "%s", c.linha );
        snprintf( outra, sizeof dm._sharedState.timeString, "01/01/01 - 05:55:55" );
        char hora[6] = { 'x', 'x', 'x', 'x', 'x', 0 };   // poisoned: a function that does not write them is seen
        int h = 12345;
        bool prov = !c.provisoria;
        const bool ok = dm.uaRelogio( hora, h, prov );
        CONFERE( ok == c.ok && h == c.h && strcmp( hora, c.hora ) == 0 && prov == c.provisoria,
                 "uaRelogio( ) on '%s' (mutex %s) gave %d, hour %d, text '%s', provisional %d; expected %d, %d, '%s', %d", c.linha, livre ? "free" : "busy", (int)ok, h, hora, (int)prov, (int)c.ok, c.h,
                 c.hora, (int)c.provisoria );
        relogios++;
      }
    g_mutexLivre = true;
  }

  // 3. The preview: only the Tema screen's frame takes it; uaQuadro( ) drops it on any other screen, and does so before the line that returns while no look is due.
  const ConfigFalsa salvo{ 17 };   // automatic, the last accent: with no clock, light
  dm._sysConfigPtr = &salvo;
  escreveRelogio( dm, "" );
  tft.ox = 3;
  tft.oy = -2;
  int previas = 0;
  const int previasTeste[] = { -128, -1, 0, 1, 2, 3, 4, 5, 8, 16, 17, 18, 100, 127 };
  for ( const UiMode tela : telas )
    for ( const int pv : previasTeste ) {
      const bool vale = tela == MODE_SETTINGS_THEMES && pv >= 0;   // the preview reaches the glass on this frame
      const Par ptema = decodificado( vale ? pv : 17 );
      const bool noite = noiteEsperada( ptema.modo, -1 );
      char quando[96];
      snprintf( quando, sizeof quando, "screen %d, preview %d", (int)tela, pv );
      // (a) a pass that paints
      dm._uiMode = tela;
      dm._uaPrevia = (int8_t)pv;
      sentinelaNoVidro( );
      dm._uaTudo = true;
      g_agora += 300;
      dm.uaQuadro( );
      UaEstado e;
      dm.uaEstado( e );
      confereVidro( quando, noite, ptema.acento, 2 );
      CONFERE( e.escuro == noite && e.modo == tela, "%s: the mode on the glass says dark %d for screen %d, expected %d for %d", quando, (int)e.escuro, (int)e.modo, (int)noite, (int)tela );
      CONFERE( e.hash == modeloHash( g_vm, tela, noite, ptema.acento, 3, -2 ), "%s: the hash is %08x, the model says %08x", quando, (unsigned)e.hash,
               (unsigned)modeloHash( g_vm, tela, noite, ptema.acento, 3, -2 ) );
      CONFERE( dm._uaPrevia == ( tela == MODE_SETTINGS_THEMES ? pv : -1 ), "%s: the pass leaves the preview at %d", quando, (int)dm._uaPrevia );
      // (b) a pass that returns at the 250 ms gate (the previous pass looked just now): the preview must be dropped all the same
      dm._uiMode = tela;
      dm._uaPrevia = (int8_t)pv;
      dm._uaTudo = false;
      dm._uaSujo = false;
      const uint32_t antes = pinturas( dm );
      dm.uaQuadro( );
      CONFERE( dm._uaPrevia == ( tela == MODE_SETTINGS_THEMES ? pv : -1 ), "%s: a pass that returns at the gate leaves the preview at %d", quando, (int)dm._uaPrevia );
      CONFERE( pinturas( dm ) == antes, "%s: a pass at the gate painted", quando );
      previas++;
    }

  // 4. The update screen: painted, and hashed, with the SAVED theme, whatever the screen on show says and whatever the Tema screen is previewing. Core 0 paints it while the screen on show
  //    may still be Tema. The alignment is not in its identity.
  g_vmOta = VmFalso{ 3, { 0, 0, 0 } };
  uint32_t seqOta = 0;
  int otas = 0;
  const int previasOta[] = { -1, 0, 5, 16 };
  const UiMode telasOta[3] = { MODE_DASHBOARD, MODE_SETTINGS_THEMES, MODE_OTA_UPDATE };
  for ( int s = 0; s < 3 * gerado::ACENTOS_N; s++ ) {
    const ConfigFalsa conf{ (int8_t)s };
    dm._sysConfigPtr = &conf;
    const Par ps = decodificado( s );
    const bool noite = noiteEsperada( ps.modo, -1 );   // no clock
    for ( const int pv : previasOta )
      for ( const UiMode tela : telasOta ) {
        dm._uiMode = tela;
        dm._uaPrevia = (int8_t)pv;
        dm._otaState = ( 1u << 24 ) | ++seqOta;
        dm._otaRedrawAll = true;
        dm._otaPct = 1 + seqOta % 99;   // a percentage that is not 0, and different on every pass (the review's P23)
        dm._otaPctDrawn = 0;
        dm._otaDrawnSeq = 0;
        sentinelaNoVidro( );
        g_agora += 500;
        const uint32_t antes = pinturas( dm );
        dm.uiPaintOta( );
        UaEstado e;
        const int8_t r = dm.uaEstado( e );
        char quando[96];
        snprintf( quando, sizeof quando, "update screen, saved %d, preview %d, screen on show %d", s, pv, (int)tela );
        CONFERE( r == 1 && e.pinturas == antes + 1, "%s: not painted once", quando );
        confereVidro( quando, noite, ps.acento, 3 );
        CONFERE( e.escuro == noite, "%s: the mode on the glass says dark %d, expected the saved theme's %d", quando, (int)e.escuro, (int)noite );
        CONFERE( e.modo == MODE_OTA_UPDATE && e.modoAtual == tela, "%s: the frame on the glass is for screen %d (expected the update screen) and the screen on show reads %d (expected %d)", quando, (int)e.modo,
                 (int)e.modoAtual, (int)tela );
        CONFERE( e.hash == modeloHash( g_vmOta, MODE_OTA_UPDATE, noite, ps.acento, 0, 0 ), "%s: the hash is %08x, the model says %08x", quando, (unsigned)e.hash,
                 (unsigned)modeloHash( g_vmOta, MODE_OTA_UPDATE, noite, ps.acento, 0, 0 ) );
        CONFERE( dm._otaDrawnSeq == seqOta && !dm._otaRedrawAll && dm._otaPct != 0 && dm._otaPctDrawn == dm._otaPct, "%s: the counters Core 0 waits on were not left as it expects (drawn sequence %u of %u, percentage drawn %u of %u)",
                 quando, (unsigned)dm._otaDrawnSeq, (unsigned)seqOta, (unsigned)dm._otaPctDrawn, (unsigned)dm._otaPct );
        otas++;
      }
  }
  {
    // The preview alone does not repaint the update screen (its hash is the only trigger and it does not hold the preview); a change of the saved accent alone does, with a few strips at most.
    const ConfigFalsa a{ 5 }, b{ 8 };   // automatic stock+accent 1, then accent 2: both light with no clock
    dm._sysConfigPtr = &a;
    dm._uiMode = MODE_SETTINGS_THEMES;
    dm._uaPrevia = -1;
    dm._otaState = ( 1u << 24 ) | ++seqOta;
    dm._otaRedrawAll = true;
    g_agora += 500;
    dm.uiPaintOta( );
    const uint32_t antes = pinturas( dm );
    dm._uaPrevia = 4;   // dark, accent 1
    dm._otaRedrawAll = false;
    dm._uaTudo = false;
    g_agora += 500;
    dm.uiPaintOta( );
    CONFERE( pinturas( dm ) == antes, "the update screen was repainted for a change of the Tema screen's preview alone" );
    confereVidro( "update screen after a preview alone", false, 1, 3 );
    dm._sysConfigPtr = &b;
    g_agora += 500;
    dm.uiPaintOta( );
    CONFERE( pinturas( dm ) == antes + 1, "the update screen was not repainted for a change of the saved accent" );
    confereVidro( "update screen after a change of the saved accent", false, 2, 3 );
    otas += 3;
  }
  {
    // The early return (S10c, A'2d, the review's P22): the same phase and sequence asked again within 400 ms, with nothing to redraw, paints and pushes nothing, does not read the percentage again (what is drawn is
    // what is on the glass), and says all the same that the sequence is on the glass: Core 0 waits on _otaDrawnSeq before the steps that freeze the panel.
    const ConfigFalsa conf{ 0 };
    dm._sysConfigPtr = &conf;
    dm._uiMode = MODE_DASHBOARD;
    dm._uaPrevia = -1;
    dm._otaState = ( 1u << 24 ) | ++seqOta;
    dm._otaRedrawAll = true;
    dm._otaPct = 40;
    g_agora += 500;
    dm.uiPaintOta( );   // paints: the glass holds the update screen
    const uint32_t antes = pinturas( dm );
    const int empurradas = g_empurradas;
    dm._otaRedrawAll = false;
    dm._uaTudo = false;
    dm._otaDrawnSeq = 0;
    dm._otaPct = 55;
    g_agora += 100;
    dm.uiPaintOta( );
    CONFERE( dm._otaDrawnSeq == seqOta, "the early return of uiPaintOta( ) left _otaDrawnSeq at %u, expected %u", (unsigned)dm._otaDrawnSeq, (unsigned)seqOta );
    CONFERE( pinturas( dm ) == antes && g_empurradas == empurradas, "the early return of uiPaintOta( ) painted or pushed" );
    CONFERE( dm._otaPctDrawn == 40, "the early return read the percentage again: drawn %u, expected 40 (the one on the glass)", (unsigned)dm._otaPctDrawn );
    dm._otaRedrawAll = true;   // asked to redraw all, the same call does not return early
    dm._otaDrawnSeq = 0;
    g_agora += 100;
    dm.uiPaintOta( );
    CONFERE( pinturas( dm ) == antes + 1 && dm._otaDrawnSeq == seqOta && !dm._otaRedrawAll && dm._otaPctDrawn == 55, "a redraw of all asked for within 400 ms was not painted (%u paints after %u, drawn sequence %u, percentage %u)",
             (unsigned)pinturas( dm ), (unsigned)antes, (unsigned)dm._otaDrawnSeq, (unsigned)dm._otaPctDrawn );
    confereVidro( "update screen after the early return and a redraw of all", false, 0, 3 );
    otas += 3;
  }

  // 5. The repaint decisions of uaQuadro( ), with no full repaint asked for: what changed decides how much is pushed.
  int passos = 0;
  {
    ConfigFalsa conf{ 0 };   // light, stock accent
    dm._sysConfigPtr = &conf;
    escreveRelogio( dm, "" );
    dm._uiMode = MODE_DASHBOARD;
    dm._uaPrevia = -1;
    tft.ox = 3;
    tft.oy = -2;
    g_vm.n = 0;
    sentinelaNoVidro( );
    dm._uaTudo = true;
    g_agora += 300;
    dm.uaQuadro( );
    confereVidro( "first frame", false, 0, 0 );
    // quando, the frame is repainted, strips it pushes, margin passes it makes, dark, accent, marker
    auto passo = [&]( const char *quando, bool repinta, int faixas, int margens, bool noite, int acento, int n ) {
      g_agora += 300;
      const uint32_t antes = pinturas( dm );
      const int m0 = tft.margensPretas;
      dm.uaQuadro( );
      UaEstado e;
      dm.uaEstado( e );
      CONFERE( ( e.pinturas == antes + 1 ) == repinta, "%s: %s", quando, repinta ? "not repainted" : "repainted though nothing changed" );
      if ( repinta ) {
        CONFERE( e.faixas == faixas, "%s: %d strips pushed, expected %d", quando, (int)e.faixas, faixas );
        CONFERE( e.hash == modeloHash( g_vm, MODE_DASHBOARD, noite, acento, tft.ox, tft.oy ), "%s: the hash is %08x, the model says %08x", quando, (unsigned)e.hash,
                 (unsigned)modeloHash( g_vm, MODE_DASHBOARD, noite, acento, tft.ox, tft.oy ) );
      }
      CONFERE( tft.margensPretas - m0 == margens, "%s: the margin was blackened %d times, expected %d", quando, tft.margensPretas - m0, margens );
      CONFERE( e.escuro == noite, "%s: the mode on the glass says dark %d, expected %d", quando, (int)e.escuro, (int)noite );
      confereVidro( quando, noite, acento, n );
      passos++;
    };
    passo( "nothing changed", false, 0, 0, false, 0, 0 );
    dm._uaTudo = true;
    passo( "a full repaint asked for, nothing else changed", true, N_FAIXAS, 1, false, 0, 0 );
    conf.themeIndex = 3;   // light, accent 1: the accent alone
    passo( "the accent alone changed", true, N_FAIXAS, 1, false, 1, 0 );
    conf.themeIndex = 4;   // dark, accent 1: the mode alone
    passo( "the mode alone changed", true, N_FAIXAS, 1, true, 1, 0 );
    tft.ox = -3;           // the alignment alone
    passo( "the alignment alone changed", true, N_FAIXAS, 1, true, 1, 0 );
    tft.oy = 4;
    passo( "the vertical alignment alone changed", true, N_FAIXAS, 1, true, 1, 0 );
    g_vm.n = 1;            // the view model alone: the marker moves from strip 4 to strip 5
    passo( "the view model alone changed", true, 2, 0, true, 1, 1 );
    // Strips a paint skips (S10c, A'2d, the review's P2): strip k is pushed, k + 1 is not (its pixels did not change), and k + 2 is, composed in the half of the canvas that strip k's push may still be reading. The push of
    // k must be waited for before k + 2 is composed (uaPintar( ) does it at `voando == buf`): a paint that did not would put the pixels of k + 2 at the place of k on the glass.
    g_vm.n = 3;            // the marker from strip 5 to strip 7: strips 5 and 7 change, 6 does not
    passo( "the marker two strips on (one strip skipped between the two pushed)", true, 2, 0, true, 1, 3 );
    g_vm.n = 0;            // from strip 7 to strip 4: 4 and 7 change, 5 and 6 do not
    passo( "the marker three strips back (two strips skipped between the two pushed)", true, 2, 0, true, 1, 0 );
    g_vm.n = 1;            // and to strip 5, where the next steps expect it
    passo( "the marker one strip on", true, 2, 0, true, 1, 1 );
    conf.themeIndex = 0;
    passo( "back to light, stock accent", true, N_FAIXAS, 1, false, 0, 1 );
    // the automatic mode follows the clock: light by day, dark from 19:00 to 6:59
    conf.themeIndex = 2;
    passo( "automatic with no clock is the light theme just shown", false, 0, 0, false, 0, 1 );
    escreveRelogio( dm, "22/10/26 - 18:59:30" );
    passo( "automatic at 18:59", false, 0, 0, false, 0, 1 );
    escreveRelogio( dm, "22/10/26 - 19:00:00" );
    passo( "automatic at 19:00 goes dark", true, N_FAIXAS, 1, true, 0, 1 );
    escreveRelogio( dm, "22/10/26 - 06:59:59" );
    passo( "automatic at 06:59 is still dark", false, 0, 0, true, 0, 1 );
    escreveRelogio( dm, "22/10/26 - 07:00:00" );
    passo( "automatic at 07:00 goes light", true, N_FAIXAS, 1, false, 0, 1 );
  }

  // 6. What Core 0 reads off a pass of Core 1 (S10c, A'2d, the review's P17 and P18): _bootPaintSeq, which the boot's AP-by-touch window waits on and which must count the frames of the dashboard that are on the glass
  //    while the device boots, and nothing else; and the heartbeat, which the health check on Core 0 restarts a Core 1 for when it is 10 s old: Core 1 feeds it after a pass that painted, Core 0 does not.
  int contadores = 0;
  {
    const ConfigFalsa conf{ 0 };
    dm._sysConfigPtr = &conf;
    escreveRelogio( dm, "" );
    dm._uaPrevia = -1;
    tft.ox = 0;
    tft.oy = 0;
    constexpr uint32_t SEM_BATIDA = 0xA5A5A5A5u;
    for ( const int nucleo : { 1, 0 } )
      for ( const bool bota : { true, false } )
        for ( const UiMode tela : telas )
          for ( const bool pinta : { true, false } ) {
            dm._uiMode = tela;
            dm._lastRenderedState.isBooting = false;
            g_nucleo = 1;
            dm._uaTudo = true;   // the glass holds this screen's frame, so that a pass that asks for no repaint finds nothing to paint
            g_agora += 300;
            dm.uaQuadro( );
            dm._lastRenderedState.isBooting = bota;
            g_nucleo = nucleo;
            dm._bootPaintSeq = 100;
            dm._lastHeartbeat = SEM_BATIDA;
            dm._uaTudo = pinta;
            dm._uaSujo = true;   // a look is due, as when a hook said that the state changed
            g_agora += 300;
            const uint32_t antes = pinturas( dm );
            dm.uaQuadro( );
            char quando[96];
            snprintf( quando, sizeof quando, "core %d, booting %d, screen %d, %s", nucleo, (int)bota, (int)tela, pinta ? "a repaint asked for" : "nothing changed" );
            CONFERE( ( pinturas( dm ) == antes + 1 ) == pinta, "%s: the pass %s", quando, pinta ? "did not paint" : "painted" );
            const uint32_t espera = 100u + ( pinta && bota && tela == MODE_DASHBOARD ? 1u : 0u );
            CONFERE( dm._bootPaintSeq == espera, "%s: _bootPaintSeq is %u after the pass, expected %u (it counts a painted frame of the dashboard while booting, nothing else)", quando, (unsigned)dm._bootPaintSeq,
                     (unsigned)espera );
            if ( pinta )
              CONFERE( dm._lastHeartbeat == ( nucleo == 1 ? g_agora : SEM_BATIDA ), "%s: the heartbeat is %u after a pass that painted, expected %u (fed on Core 1 only)", quando, (unsigned)dm._lastHeartbeat,
                       (unsigned)( nucleo == 1 ? g_agora : SEM_BATIDA ) );
            contadores++;
          }
    dm._lastRenderedState.isBooting = false;
    g_nucleo = 0;
  }

  // 7. uaPintar( ) on its own (S10c, A'2d): whatever strips it pushes, it returns with no push in flight, and no strip was written while its push was (the touch read, the GRAM readback and the update screen rely on a
  //    free bus the moment it returns). Called here, not through uaQuadro( ): every strip, then two strips with one between them, then none.
  int protocolos = 0;
  {
    const ConfigFalsa conf{ 0 };
    dm._sysConfigPtr = &conf;
    escreveRelogio( dm, "" );
    dm._uiMode = MODE_DASHBOARD;
    dm._uaPrevia = -1;
    // esperasCola: the glue's own waits with a push in flight (A'2e). Every strip: the last wait only. Strips 4 and 6 with 5 skipped: before strip 6 reuses strip 4's half,
    // and before strip 8 reuses strip 6's (nothing was pushed in between), and the last wait finds the bus free. None: no wait finds a push.
    struct Passo7 { const char *quando; int n; bool tudo; int faixas; int esperasCola; };
    const Passo7 passos7[] = { { "uaPintar( ) pushing every strip", 0, true, N_FAIXAS, 1 }, { "uaPintar( ) pushing strips 4 and 6, 5 skipped", 2, false, 2, 2 }, { "uaPintar( ) pushing none", 2, false, 0, 0 } };
    sentinelaNoVidro( );
    for ( const Passo7 &c : passos7 ) {
      g_vm.n = (uint8_t)c.n;
      UaQuadro q;
      dm.uaMontar( q );
      dm._uaTudo = c.tudo;
      const int ec0 = g_esperasDaCola;
      const bool pintou = dm.uaPintar( q, MODE_DASHBOARD, 1 );
      const bool livre = g_emVoo.buf == nullptr;   // read before anything else runs
      UaEstado e;
      dm.uaEstado( e );
      CONFERE( pintou && livre && e.faixas == c.faixas, "%s: it returned %d with %s and %d strips pushed, expected a free bus and %d", c.quando, (int)pintou, livre ? "no push in flight" : "a push in flight", (int)e.faixas, c.faixas );
      CONFERE( g_esperasDaCola - ec0 == c.esperasCola, "%s: the glue waited %d times with a push in flight, expected %d (compose and push overlap: it waits only before reusing the half a push still reads, and at the end)",
               c.quando, g_esperasDaCola - ec0, c.esperasCola );
      confereVidro( c.quando, false, 0, c.n );
      protocolos++;
    }
  }
  CONFERE( g_empurradas == g_terminadas, "%d pushes started and %d waited for", g_empurradas, g_terminadas );

  printf( "MEDIDA glue: uaQuadro( ) ran %d full frames and %d repaint decisions, the preview on %d screen-and-preview cases, uiPaintOta( ) on %d cases, uaEstado( ) on %d screens-on-show, uaRelogio( ) on %d bound cases, "
          "%d counter passes and %d direct uaPintar( ) calls; %d strips pushed and %d waited for (%d calls of the wait), every one of the glass's pixels compared with the image computed from the generated tables\n",
          pintadas, passos, previas, otas, estados, relogios, contadores, protocolos, g_empurradas, g_terminadas, dm.esperas );
  printf( "%d checks, %d failed\n", checks, falhas );
  return falhas ? 1 : 0;
}
