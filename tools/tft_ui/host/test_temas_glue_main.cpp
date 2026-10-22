// test_temas_glue_main.cpp -- the glue's own statements about the theme, linked on the host (S10c, A'2b), built and run by tools/tft_ui/test_temas.py.
//
// DisplayManager_UiA.cpp itself cannot be linked here: it is a DisplayManager, with two displays, a touch panel, the saved configuration and two cores behind it.
// Its text for the theme can. The review of A'2 found three mutants of that text which every check of A'2 passed (the accent never reaching ua::tema( ), the dark mode
// inverted, the accent cut out of the hash by a length of 2), because the checks read the glue as text and nothing ran it. test_temas.py now cuts four pieces
// out of the file, verbatim, into the .inc files included below:
//   fnv.inc          fnv( ), the hash the glue runs;
//   uatema.inc       DisplayManager::uaTema( UiMode ), the theme of a frame;
//   hash_pintar.inc  the run of statements that hashes the view model and then the identity of the Angulo's frame;
//   hash_ota.inc     the same run for the update screen.
// They are compiled against a stand-in for the few members of DisplayManager they touch and the kit (Angulo.cpp) as it is. What they must give comes from the
// generated header and from the rules written out below, not from the kit.

#include <Adafruit_GFX.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Angulo.h"

namespace gerado {
#include "kit/angulo_tokens.h"
}  // namespace gerado

// The stand-in. The firmware's UiMode has more members; these are the names the pieces use.
enum UiMode { MODE_DASHBOARD, MODE_SETTINGS_THEMES, MODE_OTA_UPDATE, MODE_OUTRO };

struct UaQuadro {   // what the two runs of statements read of the real one (src/ui/angulo/UiA.h)
  const void *vm = nullptr;
  uint16_t tam = 0;
};

struct ConfigFalsa {   // the one field of the saved configuration that uaTema( ) reads
  int8_t themeIndex;
};

class DisplayManager {
 public:
  const ua::Tema &uaTema( UiMode modo );
  bool uaRelogio( char hora[6], int &h, bool &provisoria );

  int8_t _uaPrevia = -1;
  const ConfigFalsa *_sysConfigPtr = nullptr;
  bool relogioTem = false;   // the stand-in clock: whether there is a line yet, and the hour it says
  int relogioHora = 0;
};

// Like the real one: with no clock line it says so and leaves h as the caller set it.
bool DisplayManager::uaRelogio( char hora[6], int &h, bool &provisoria ) {
  provisoria = false;
  if ( !relogioTem ) return false;
  memcpy( hora, "12:00", 6 );
  h = relogioHora;
  return true;
}

namespace {
#include "fnv.inc"
}  // namespace

#include "uatema.inc"

uint32_t hashPintar( const UaQuadro &q, UiMode _uiMode, const ua::Tema &tema, const int8_t ox, const int8_t oy ) {
  (void)q; (void)_uiMode; (void)tema; (void)ox; (void)oy;   // a mutant may drop one of them
#include "hash_pintar.inc"
  return h;
}

uint32_t hashOta( const UaQuadro &q, const ua::Tema &tema ) {
  (void)q; (void)tema;
#include "hash_ota.inc"
  return h;
}

namespace {

constexpr int PAPEIS = 17;
static_assert( sizeof( gerado::Tema ) == PAPEIS * sizeof( uint16_t ), "the generated Tema is the 17 roles and nothing else" );

int checks = 0, falhas = 0;

// Only the first failures are printed: a mutant of the glue can fail every one of the thousands of frames below.
#define CONFERE( cond, ... ) do { checks++; if ( !( cond ) ) { falhas++; if ( falhas <= 6 ) { printf( "FAIL " ); printf( __VA_ARGS__ ); printf( "\n" ); } } } while ( 0 )

// Role r of a table, read from its bytes (the 17 roles come first, in the header's order).
uint16_t papel( const void *t, int r ) {
  uint16_t w;
  memcpy( &w, static_cast<const char *>( t ) + r * sizeof w, sizeof w );
  return w;
}

// FNV-1a, 32 bits, written out again here: the model the glue's hash must equal.
uint32_t fnv1a( const void *dados, size_t n, uint32_t h = 2166136261u ) {
  const uint8_t *p = static_cast<const uint8_t *>( dados );
  for ( size_t i = 0; i < n; i++ ) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
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

// The theme the frame of `modo` must be painted with when the index is `indice`: mode 0 light, 1 dark, 2 automatic (dark from 19:00 to 6:59 by the clock, light with no clock),
// the accent's table of that mode, its flag and its entry.
void confereQuadro( DisplayManager &dm, UiMode modo, int indice ) {
  const Par p = decodificado( indice );
  const bool noite = p.modo == 1 || ( p.modo == 2 && dm.relogioTem && ( dm.relogioHora >= 19 || dm.relogioHora <= 6 ) );
  const ua::Tema &t = dm.uaTema( modo );
  CONFERE( t.escuro == noite, "frame %d, preview %d, clock %d: index %d is mode %d accent %d, dark is %d, expected %d", (int)modo, (int)dm._uaPrevia,
           dm.relogioTem ? dm.relogioHora : -1, indice, (int)p.modo, (int)p.acento, (int)t.escuro, (int)noite );
  CONFERE( t.entrada == p.acento, "frame %d, preview %d, clock %d: index %d is accent %d, the theme says %d", (int)modo, (int)dm._uaPrevia,
           dm.relogioTem ? dm.relogioHora : -1, indice, (int)p.acento, (int)t.entrada );
  const gerado::Tema *g = kGerado[noite ? 1 : 0][p.acento];
  int diferentes = 0;
  for ( int r = 0; r < PAPEIS; r++ )
    if ( papel( &t, r ) != papel( g, r ) ) diferentes++;
  CONFERE( diferentes == 0, "frame %d, preview %d, clock %d: index %d, %d of the 17 roles differ from the generated table", (int)modo, (int)dm._uaPrevia,
           dm.relogioTem ? dm.relogioHora : -1, indice, diferentes );
}

// What the glue's hash must be: the view model, then the identity's bytes.
uint32_t modelo( const UaQuadro &q, uint8_t tela, const ua::Tema &t, int8_t ox, int8_t oy ) {
  const uint32_t h = fnv1a( q.vm, q.tam );
  const ua::IdentidadeDoQuadro id = ua::identidadeDoQuadro( tela, t, ox, oy );
  return fnv1a( &id, sizeof id, h );
}

}  // namespace

int main( ) {
  DisplayManager dm;
  const UiMode modos[3] = { MODE_DASHBOARD, MODE_SETTINGS_THEMES, MODE_OTA_UPDATE };
  int quadros = 0;

  // 1. The saved theme, over every int8_t value and over no configuration at all, with every hour of the clock and with none, on the three kinds of frame.
  for ( int cfg = -129; cfg <= 127; cfg++ ) {   // -129: no configuration yet, which means index 0
    const ConfigFalsa conf{ (int8_t)( cfg < -128 ? 0 : cfg ) };
    dm._sysConfigPtr = cfg < -128 ? nullptr : &conf;
    dm._uaPrevia = -1;
    for ( int relogio = -1; relogio < 24; relogio++ ) {
      dm.relogioTem = relogio >= 0;
      dm.relogioHora = relogio < 0 ? 0 : relogio;
      for ( const UiMode modo : modos ) {
        confereQuadro( dm, modo, cfg < -128 ? 0 : cfg );
        quadros++;
      }
    }
  }

  // 2. The preview: the frame of the Tema screen takes it when there is one (a value of 0 or more), every other frame keeps the saved theme (17: automatic, last accent).
  const ConfigFalsa salvo{ 17 };
  dm._sysConfigPtr = &salvo;
  const int relogios[4] = { -1, 3, 12, 22 };
  for ( int previa = -128; previa <= 127; previa++ ) {
    dm._uaPrevia = (int8_t)previa;
    for ( const int relogio : relogios ) {
      dm.relogioTem = relogio >= 0;
      dm.relogioHora = relogio < 0 ? 0 : relogio;
      for ( const UiMode modo : modos ) {
        confereQuadro( dm, modo, ( modo == MODE_SETTINGS_THEMES && previa >= 0 ) ? previa : 17 );
        quadros++;
      }
    }
  }

  // 3. The two hashes: each equals the model, and every theme, view model, screen and alignment gives its own value.
  static const uint8_t vmA[24] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 };
  uint8_t vmB[24];
  memcpy( vmB, vmA, sizeof vmA );
  vmB[23] ^= 0x40;   // one byte apart
  const UaQuadro qA{ vmA, sizeof vmA }, qB{ vmB, sizeof vmB };
  constexpr int NT = 2 * gerado::ACENTOS_N;
  uint32_t hp[NT], ho[NT];
  for ( int i = 0; i < NT; i++ ) {
    const ua::Tema &t = ua::tema( i / gerado::ACENTOS_N == 1, (uint8_t)( i % gerado::ACENTOS_N ) );
    hp[i] = hashPintar( qA, MODE_SETTINGS_THEMES, t, 3, -2 );
    ho[i] = hashOta( qA, t );
    const uint32_t mp = modelo( qA, (uint8_t)MODE_SETTINGS_THEMES, t, 3, -2 ), mo = modelo( qA, (uint8_t)MODE_OTA_UPDATE, t, 0, 0 );
    CONFERE( hp[i] == mp, "the Angulo's frame hash of theme %d is %08x, the model says %08x", i, hp[i], mp );
    CONFERE( ho[i] == mo, "the update screen's hash of theme %d is %08x, the model says %08x", i, ho[i], mo );
  }
  for ( int i = 0; i < NT; i++ )
    for ( int j = i + 1; j < NT; j++ ) {
      CONFERE( hp[i] != hp[j], "the Angulo's frame hashes themes %d and %d alike (%08x)", i, j, hp[i] );
      CONFERE( ho[i] != ho[j], "the update screen hashes themes %d and %d alike (%08x)", i, j, ho[i] );
    }
  const ua::Tema &t0 = ua::tema( false, 0 );
  const uint32_t base = hashPintar( qA, MODE_SETTINGS_THEMES, t0, 3, -2 );
  CONFERE( hashPintar( qB, MODE_SETTINGS_THEMES, t0, 3, -2 ) != base, "the view model is not in the Angulo's frame hash" );
  CONFERE( hashOta( qB, t0 ) != hashOta( qA, t0 ), "the view model is not in the update screen's hash" );
  CONFERE( hashPintar( qA, MODE_DASHBOARD, t0, 3, -2 ) != base, "the screen is not in the Angulo's frame hash" );
  CONFERE( hashPintar( qA, MODE_SETTINGS_THEMES, t0, -2, 3 ) != base, "ox and oy are interchangeable in the Angulo's frame hash" );
  CONFERE( hashPintar( qA, MODE_SETTINGS_THEMES, t0, 3, 0 ) != base, "oy is not in the Angulo's frame hash" );
  CONFERE( hashPintar( qA, MODE_SETTINGS_THEMES, t0, 0, -2 ) != base, "ox is not in the Angulo's frame hash" );

  printf( "MEDIDA glue: uaTema( ) on %d frames (every index, every hour, the preview), the two hashes on %d themes and %d pairs\n", quadros, NT, NT * ( NT - 1 ) / 2 );
  printf( "%d checks, %d failed\n", checks, falhas );
  return falhas ? 1 : 0;
}
