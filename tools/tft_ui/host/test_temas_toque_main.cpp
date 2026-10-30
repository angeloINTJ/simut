// test_temas_toque_main.cpp -- the Tema screen's touch, RUN on the host (S10c, A'2d). Built and run by tools/tft_ui/test_temas.py.
//
// The review of A'3b and A'2c found three mutants of DisplayManager_UiA_Menus.cpp that passed test_temas.py, because the Tema screen's touch sat outside the six functions the glue program cuts out of
// DisplayManager_UiA.cpp: a tap on a mode that resets the accent (MM1), a tap on an accent that resets the mode (MM2), and escolhaNaTela( ) ignoring a preview of 0 (MM3). The kit's glue_check.py refused two of
// them, outside the repository, where CI never runs it. So test_temas.py now cuts out of the menus glue, verbatim and without comments, by brace matching, the three pieces that decide what a tap on the Tema
// screen does, and this file runs them:
//   escolhanatela.inc   escolhaNaTela( ), the choice the screen shows and its touch reads: the preview while there is one, else the saved one;
//   toque_prologo.inc   the two statements uaToqueMenus( ) declares before its switch and the Tema case reads (voltar, linha, i);
//   toque_tema.inc      the body of `case MODE_SETTINGS_THEMES: {` of uaToqueMenus( ): a tap on a mode or on an accent previews it, "Aplicar tema" posts the pair on show, Back drops the preview.
// They are compiled against a stand-in for what they call (the gate acceptTouch( ), the core's action actApplyTheme( ), showSettingsMain( )) and the kit (Angulo.cpp) as it is. The stand-in records what the case
// does; what it must do is written out below from the decode LISTED (index m + 3 k is mode m with accent k), never from the kit's arithmetic.
//
// What runs:
//   1. escolhaNaTela( ) for every saved themeIndex (all 256 int8_t values, and no configuration at all) against every preview (all 256 int8_t values): it gives ua::escolhaDoIndice( preview >= 0 ? preview : saved );
//   2. the case, for every saved index and no configuration, a preview of none, of every index of the palette and of a few values outside it, and every kind of target (Back, every mode, every accent, "Aplicar tema",
//      the inert target and the list's other kinds), the gate granting the touch and refusing it: the zone the gate is asked for, the preview left behind, what was posted to the core and whether the menu was
//      shown again.

#include <Adafruit_GFX.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Angulo.h"
#include "UiA.h"
#include "telas/MenusConfig.h"   // the Tema screen's targets (TT_*) and the lists' (LST_*)

namespace gerado {
#include "kit/angulo_tokens.h"
}  // namespace gerado

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The stand-in. The firmware's UiMode has more members; these are the ones the pieces name.
enum UiMode : uint8_t { MODE_SETTINGS_MAIN, MODE_SETTINGS_THEMES, MODE_SETTINGS_LANG };
struct SystemConfig { int8_t themeIndex; };   // the one field of the saved configuration the pieces read
constexpr uint8_t ZONA_VOLTAR = 0xA0, ZONA_LINHA = 0xA1;   // the glue's names; the values only have to differ

class DisplayManager {
 public:
  void tocaTema( const ua::Alvo *a );
  bool montaTema( UaQuadro &q );   // the Tema case of uaToqueMenus( ), in the function's own frame

  // what the case calls, and records
  bool acceptTouch( uint8_t zona ) {
    if ( nZonas < 8 ) zonas[nZonas] = zona;
    nZonas++;
    return aceita;
  }
  void actApplyTheme( int idx ) {
    if ( nAplicados < 8 ) aplicados[nAplicados] = idx;
    nAplicados++;
  }
  void showSettingsMain( ) { voltou++; }

  // the state it reads and moves
  UiMode _uiMode = MODE_SETTINGS_THEMES;
  int8_t _uaPrevia = -1;
  const SystemConfig *_sysConfigPtr = nullptr;

  // the record, and the gate's answer
  bool aceita = true;
  int nZonas = 0, nAplicados = 0, voltou = 0;
  uint8_t zonas[8] = {};
  int aplicados[8] = {};
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The glue's pieces, verbatim.
namespace {
#include "escolhanatela.inc"
}  // namespace

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"   // the prologue declares `linha` and `i`, which this case does not read; another case does
void DisplayManager::tocaTema( const ua::Alvo *a ) {
  using namespace ua;   // the inert target's name, ua::ALVO_INERTE: the real function reaches it through the file's own scope; a mutant may name it
  using namespace ua::tela;
#include "toque_prologo.inc"
  switch ( _uiMode ) {
#include "toque_tema.inc"
    default:
      break;
  }
}
#pragma GCC diagnostic pop

uint8_t g_uaVm[UA_VM_MAX];
namespace ua { namespace tela {
void desenharTemas( Pintor &, const VmTemas & ) {}
int alvosTemas( const VmTemas &, Alvo * ) { return 0; }
} }
bool DisplayManager::montaTema( UaQuadro &q ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
#include "montar_tema.inc"
    default:
      break;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
namespace {

int checks = 0, falhas = 0;

// The first failures are printed and the sixth ends the run: a mutant can fail every one of the thousands of cases below.
#define CONFERE( cond, ... ) do { checks++; if ( !( cond ) ) { falhas++; printf( "FAIL " ); printf( __VA_ARGS__ ); printf( "\n" ); \
                                    if ( falhas >= 6 ) { printf( "%d checks, %d failed (stopped at the sixth)\n", checks, falhas ); exit( 1 ); } } } while ( 0 )

struct Par { uint8_t modo, acento; };

// The decode, listed and not computed: index m + 3 k is mode m with accent k; anything else means light with the stock accent.
Par decodificado( int t ) {
  for ( int k = 0; k < gerado::ACENTOS_N; k++ )
    for ( int m = 0; m < 3; m++ )
      if ( t == m + 3 * k ) return Par{ (uint8_t)m, (uint8_t)k };
  return Par{ 0, 0 };
}

struct Esperado {
  int nZonas;
  uint8_t zona;
  int previa;
  int nAplicados;
  int aplicado;
  int voltou;
};

// What a tap on a target of `tipo` and `indice` must do, from the choice on show (the preview while there is one, else the saved index, else light with the stock accent).
Esperado esperado( bool semConfig, int salvo, int previa, bool aceita, int tipo, int indice ) {
  using namespace ua::tela;
  const int mostrado = previa >= 0 ? previa : ( semConfig ? 0 : salvo );
  const Par p = decodificado( mostrado );
  Esperado e{ 0, 0, previa, 0, 0, 0 };
  if ( tipo == LST_VOLTAR ) {
    e.nZonas = 1;
    e.zona = ZONA_VOLTAR;
    if ( aceita ) { e.previa = -1; e.voltou = 1; }
  } else if ( tipo == TT_MODO ) {
    e.nZonas = 1;
    e.zona = ZONA_LINHA;
    if ( aceita ) e.previa = indice + 3 * p.acento;   // that mode, the accent on show
  } else if ( tipo == TT_COR ) {
    e.nZonas = 1;
    e.zona = ZONA_LINHA;
    if ( aceita ) e.previa = p.modo + 3 * indice;     // that accent, the mode on show
  } else if ( tipo == TT_APLICAR ) {
    e.nZonas = 1;
    e.zona = ZONA_LINHA;
    if ( aceita ) { e.nAplicados = 1; e.aplicado = p.modo + 3 * p.acento; }   // the pair on show, once
  }
  return e;   // any other target: nothing is asked and nothing changes
}

}  // namespace

int main( ) {
  using namespace ua::tela;
  static_assert( TT_NADA == ua::ALVO_INERTE, "a disabled button is listed as the inert target" );
  CONFERE( ZONA_VOLTAR != ZONA_LINHA, "the stand-in's two zones must differ for the gate to tell Back from the other taps" );

  // 1. escolhaNaTela( ): every saved index and every preview; and no configuration.
  int escolhas = 0;
  for ( int s = -128; s <= 127; s++ )
    for ( int p = -128; p <= 127; p++ ) {
      const SystemConfig c{ (int8_t)s };
      const ua::Escolha got = escolhaNaTela( &c, (int8_t)p ), want = ua::escolhaDoIndice( (int8_t)( p >= 0 ? p : s ) );
      CONFERE( got.modo == want.modo && got.acento == want.acento, "escolhaNaTela( saved %d, preview %d ) = ( %d, %d ), expected ( %d, %d )", s, p, got.modo, got.acento, want.modo, want.acento );
      escolhas++;
    }
  for ( int p = -128; p <= 127; p++ ) {
    const ua::Escolha got = escolhaNaTela( nullptr, (int8_t)p ), want = ua::escolhaDoIndice( (int8_t)( p >= 0 ? p : 0 ) );
    CONFERE( got.modo == want.modo && got.acento == want.acento, "escolhaNaTela( no configuration, preview %d ) = ( %d, %d ), expected ( %d, %d )", p, got.modo, got.acento, want.modo, want.acento );
    escolhas++;
  }

  // 2. The case, through the stand-in.
  struct Alvo1 { uint8_t tipo; int16_t indice; };
  Alvo1 alvos[32];
  int nAlvos = 0;
  alvos[nAlvos++] = { LST_VOLTAR, -1 };
  for ( int m = 0; m < 3; m++ ) alvos[nAlvos++] = { TT_MODO, (int16_t)m };
  for ( int k = 0; k < gerado::ACENTOS_N; k++ ) alvos[nAlvos++] = { TT_COR, (int16_t)k };
  alvos[nAlvos++] = { TT_APLICAR, -1 };
  alvos[nAlvos++] = { TT_NADA, -1 };            // the inert target: no arm answers it
  alvos[nAlvos++] = { LST_LINHA, 0 };           // the list's other kinds, which the Tema screen does not have
  alvos[nAlvos++] = { LST_PAG_ANTERIOR, -1 };
  alvos[nAlvos++] = { LST_PAG_PROXIMA, -1 };
  const int previas[] = { -128, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 100, 127 };
  int toques = 0;
  static DisplayManager dm;
  for ( int s = -129; s <= 127; s++ ) {   // -129: no saved configuration at all
    const bool semConfig = s == -129;
    const SystemConfig c{ (int8_t)( semConfig ? 0 : s ) };
    for ( const int pv : previas )
      for ( const bool aceita : { true, false } )
        for ( int a = 0; a < nAlvos; a++ ) {
          dm._uiMode = MODE_SETTINGS_THEMES;
          dm._sysConfigPtr = semConfig ? nullptr : &c;
          dm._uaPrevia = (int8_t)pv;
          dm.aceita = aceita;
          dm.nZonas = dm.nAplicados = dm.voltou = 0;
          const ua::Alvo alvo{ 10, 10, 40, 40, alvos[a].tipo, alvos[a].indice };
          dm.tocaTema( &alvo );
          const Esperado e = esperado( semConfig, s, pv, aceita, alvos[a].tipo, alvos[a].indice );
          const bool ok = dm.nZonas == e.nZonas && ( e.nZonas == 0 || dm.zonas[0] == e.zona ) && dm._uaPrevia == e.previa && dm.nAplicados == e.nAplicados &&
                          ( e.nAplicados == 0 || dm.aplicados[0] == e.aplicado ) && dm.voltou == e.voltou;
          CONFERE( ok, "saved %s%d, preview %d, target %d/%d, gate %s: asked the gate %d time(s) (zone %02x), preview left at %d, posted %d (%d), menu shown %d time(s); expected %d time(s) (zone %02x), %d, posted %d (%d), %d",
                   semConfig ? "none " : "", s, pv, (int)alvos[a].tipo, (int)alvos[a].indice, aceita ? "grants" : "refuses", dm.nZonas, (unsigned)dm.zonas[0], (int)dm._uaPrevia, dm.nAplicados, dm.aplicados[0],
                   dm.voltou, e.nZonas, (unsigned)e.zona, e.previa, e.nAplicados, e.aplicado, e.voltou );
          toques++;
        }
  }


  // 3. The Tema screen's builder (the first case MODE_SETTINGS_THEMES of uaMontarMenus( )): the view model it fills.
  int montagens = 0;
  for ( int s = -129; s <= 127; s++ ) {
    const bool semConfig = s == -129;
    const SystemConfig c{ (int8_t)( semConfig ? 0 : s ) };
    for ( const int pv : previas ) {
      dm._uiMode = MODE_SETTINGS_THEMES;
      dm._sysConfigPtr = semConfig ? nullptr : &c;
      dm._uaPrevia = (int8_t)pv;
      UaQuadro q = {};
      const bool ok = dm.montaTema( q );
      const VmTemas *v = static_cast<const VmTemas *>( q.vm );
      const Par shown = decodificado( pv >= 0 ? pv : ( semConfig ? 0 : s ) ), saved = decodificado( semConfig ? 0 : s );
      CONFERE( ok && v && q.tam == sizeof( VmTemas ) && v->modo == shown.modo && v->acento == shown.acento && v->salvo == saved.modo + 3 * saved.acento && v->premidoTipo == TT_NADA && v->premidoIndice == -1,
               "builder: saved %d, preview %d: ok %d, shown (%d,%d) expected (%d,%d), salvo %d expected %d", s, pv, (int)ok, v ? (int)v->modo : -1, v ? (int)v->acento : -1, shown.modo, shown.acento, v ? (int)v->salvo : -99, saved.modo + 3 * saved.acento );
      montagens++;
    }
  }
  printf( "MEDIDA builder: the Tema screen's view model on %d saved-and-preview cases\n", montagens );
  printf( "MEDIDA toque: escolhaNaTela( ) on %d saved-and-preview pairs; the Tema case on %d taps (every saved index and none x %d previews x %d targets x the gate granting and refusing)\n", escolhas, toques,
          (int)( sizeof previas / sizeof previas[0] ), nAlvos );
  printf( "%d checks, %d failed\n", checks, falhas );
  return falhas ? 1 : 0;
}
