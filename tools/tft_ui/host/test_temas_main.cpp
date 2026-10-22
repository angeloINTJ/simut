// test_temas_main.cpp -- the Angulo's mode and accent on the glass (S10c, A'2), built and run by tools/tft_ui/test_temas.py.
//
// The kit (src/ui/angulo/Angulo.cpp) is linked as it is. What it must give comes from the generated header, read here and not
// through the kit: the decode over all 256 int8_t values, ua::tema( ) and ua::desab( ) against the generated tables (17 roles
// x every palette entry x 2 modes), and the frame identity the glue hashes (two frames that differ only in the accent differ).

#include <Adafruit_GFX.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Angulo.h"
#include "telas/MenusConfig.h"   // the Tema screen's fixed texts and the boxes they are drawn in (S10c, A'3)

namespace gerado {
#include "kit/angulo_tokens.h"
}  // namespace gerado

namespace {

constexpr int PAPEIS = 17;
static_assert( sizeof( gerado::Tema ) == PAPEIS * sizeof( uint16_t ), "the generated Tema is the 17 roles and nothing else" );

int checks = 0, falhas = 0;

#define CONFERE( cond, ... ) do { checks++; if ( !( cond ) ) { falhas++; printf( "FAIL " ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

// Role r of a table, read from its bytes (the 17 roles come first, in the header's order).
uint16_t papel( const void *t, int r ) {
  uint16_t w;
  memcpy( &w, static_cast<const char *>( t ) + r * sizeof w, sizeof w );
  return w;
}

// FNV-1a, 32 bits: the hash DisplayManager_UiA.cpp runs over the view model and then over the frame's identity.
uint32_t fnv1a( const void *dados, size_t n, uint32_t h = 2166136261u ) {
  const uint8_t *p = static_cast<const uint8_t *>( dados );
  for ( size_t i = 0; i < n; i++ ) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

const gerado::Tema *const *const kGerado[2] = { gerado::TEMAS_CLARO, gerado::TEMAS_ESCURO };
const gerado::Tema *const *const kGeradoDesab[2] = { gerado::TEMAS_CLARO_DESAB, gerado::TEMAS_ESCURO_DESAB };

uint32_t hashDoQuadro( const ua::IdentidadeDoQuadro &id ) {
  static const uint8_t vm[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };   // the same view model in every frame
  const uint32_t h = fnv1a( vm, sizeof vm );
  return fnv1a( &id, sizeof id, h );
}

}  // namespace

int main( ) {
  const int N = gerado::ACENTOS_N;

  // 1. The decode, over all 256 int8_t values. Index m + 3 k is mode m with accent k; anything else means ( 0, 0 ). The expectation
  //    is built by listing the pairs, not by the kit's arithmetic.
  CONFERE( ua::MODO_CLARO == 0 && ua::MODO_ESCURO == 1 && ua::MODO_AUTOMATICO == 2, "the three modes are 0 light, 1 dark, 2 automatic" );
  for ( int v = -128; v <= 127; v++ ) {
    int modo = 0, acento = 0;
    for ( int k = 0; k < N; k++ )
      for ( int m = 0; m < 3; m++ )
        if ( v == m + 3 * k ) { modo = m; acento = k; }
    const ua::Escolha e = ua::escolhaDoIndice( (int8_t)v );
    CONFERE( e.modo == modo && e.acento == acento, "decode( %d ) = ( %d, %d ), expected ( %d, %d )", v, e.modo, e.acento, modo, acento );
  }

  // 2. The encode: every pair of the palette goes to its index and back; a pair outside the palette goes to 0 (the stock).
  for ( int k = 0; k < N; k++ )
    for ( int m = 0; m < 3; m++ ) {
      const int8_t i = ua::indiceDaEscolha( (uint8_t)m, (uint8_t)k );
      const ua::Escolha e = ua::escolhaDoIndice( i );
      CONFERE( i == m + 3 * k && e.modo == m && e.acento == k, "encode( %d, %d ) = %d, and back ( %d, %d )", m, k, i, e.modo, e.acento );
    }
  CONFERE( ua::indiceDaEscolha( 3, 0 ) == 0 && ua::indiceDaEscolha( 0, (uint8_t)N ) == 0 && ua::indiceDaEscolha( 255, 255 ) == 0 &&
               ua::indiceDaEscolha( 0, 255 ) == 0,
           "a pair outside the palette encodes to 0" );

  // 3. ua::tema( ) and ua::desab( ), against the generated tables: every role of every entry in both modes.
  for ( int escuro = 0; escuro < 2; escuro++ )
    for ( int k = 0; k < N; k++ ) {
      const ua::Tema &t = ua::tema( escuro != 0, (uint8_t)k );
      const gerado::Tema &g = *kGerado[escuro][k];
      const gerado::Tema &d = *kGeradoDesab[escuro][k];
      CONFERE( t.escuro == ( escuro != 0 ) && t.entrada == k, "tema( %d, %d ) says it is ( %d, %d )", escuro, k, t.escuro, t.entrada );
      uint16_t naoPapel = 0;
      for ( uint32_t w = 0; w < 65536; w++ ) {
        bool ehPapel = false;
        for ( int r = 0; r < PAPEIS; r++ ) ehPapel = ehPapel || papel( &g, r ) == w;
        if ( !ehPapel ) { naoPapel = (uint16_t)w; break; }
      }
      for ( int r = 0; r < PAPEIS; r++ ) {
        CONFERE( papel( &t, r ) == papel( &g, r ), "tema( %d, %d ) role %d is 0x%04X, the header has 0x%04X", escuro, k, r, papel( &t, r ), papel( &g, r ) );
        CONFERE( ua::desab( t, papel( &g, r ) ) == papel( &d, r ), "desab( tema( %d, %d ), role %d ) = 0x%04X, the header's twin is 0x%04X", escuro, k, r,
                 ua::desab( t, papel( &g, r ) ), papel( &d, r ) );
      }
      CONFERE( ua::desab( t, naoPapel ) == naoPapel, "desab( tema( %d, %d ), 0x%04X ), not a role, must come back unchanged", escuro, k, naoPapel );
    }
  CONFERE( &ua::tema( false, 0 ) == &ua::temaClaro( ) && &ua::tema( true, 0 ) == &ua::temaEscuro( ), "temaClaro( ) and temaEscuro( ) are the stock entry" );
  CONFERE( &ua::tema( true, 255 ) == &ua::tema( true, 0 ) && &ua::tema( false, (uint8_t)N ) == &ua::tema( false, 0 ),
           "an accent outside the palette is the stock one" );

  // 4. The frame's identity: two frames that differ only in the accent hash differently, and ask for a full repaint.
  CONFERE( sizeof( ua::IdentidadeDoQuadro ) == 5, "the identity is five bytes, no padding" );
  for ( int escuro = 0; escuro < 2; escuro++ )
    for ( int a = 0; a < N; a++ )
      for ( int b = 0; b < N; b++ ) {
        const ua::IdentidadeDoQuadro ia = ua::identidadeDoQuadro( 7, ua::tema( escuro != 0, (uint8_t)a ), 1, -2 );
        const ua::IdentidadeDoQuadro ib = ua::identidadeDoQuadro( 7, ua::tema( escuro != 0, (uint8_t)b ), 1, -2 );
        if ( a == b )
          CONFERE( memcmp( &ia, &ib, sizeof ia ) == 0 && hashDoQuadro( ia ) == hashDoQuadro( ib ) && !ib.repintaTudo( ia ), "the same theme ( %d, %d ) is the same frame", escuro, a );
        else
          CONFERE( memcmp( &ia, &ib, sizeof ia ) != 0 && hashDoQuadro( ia ) != hashDoQuadro( ib ) && ib.repintaTudo( ia ),
                   "accents %d and %d (mode %d) differ only in the accent: the hash must differ and a full repaint must follow", a, b, escuro );
      }
  const ua::IdentidadeDoQuadro base = ua::identidadeDoQuadro( 7, ua::tema( false, 0 ), 1, -2 );
  const ua::IdentidadeDoQuadro escuro = ua::identidadeDoQuadro( 7, ua::tema( true, 0 ), 1, -2 );
  const ua::IdentidadeDoQuadro ox = ua::identidadeDoQuadro( 7, ua::tema( false, 0 ), 2, -2 );
  const ua::IdentidadeDoQuadro oy = ua::identidadeDoQuadro( 7, ua::tema( false, 0 ), 1, -1 );
  const ua::IdentidadeDoQuadro tela = ua::identidadeDoQuadro( 8, ua::tema( false, 0 ), 1, -2 );
  CONFERE( escuro.repintaTudo( base ) && ox.repintaTudo( base ) && oy.repintaTudo( base ), "a change of mode, of ox or of oy asks for a full repaint" );
  CONFERE( !tela.repintaTudo( base ) && hashDoQuadro( tela ) != hashDoQuadro( base ), "a change of screen alone changes the hash and asks for no full repaint" );
  CONFERE( hashDoQuadro( escuro ) != hashDoQuadro( base ) && hashDoQuadro( ox ) != hashDoQuadro( base ) && hashDoQuadro( oy ) != hashDoQuadro( base ),
           "a change of mode, of ox or of oy changes the hash" );

  // 5. The Tema screen's fixed texts fit the room they are drawn in: the very strings and boxes of telas/MenusConfig.h (S10c, A'3). MEDIDA lines are the
  //    measures, echoed by test_temas.py.
  {
    using namespace ua::tela;
    for ( int m = 0; m < TM_N; m++ ) {
      const int w = ua::larguraTinta( TEMA_MODOS[m], TEMA_FONTE_SEGMENTO );
      printf( "MEDIDA segment label \"%s\": %d px in %d\n", TEMA_MODOS[m], w, TEMA_W_ROTULO );
      CONFERE( w <= TEMA_W_ROTULO, "the segment label \"%s\" is %d px and its room %d", TEMA_MODOS[m], w, TEMA_W_ROTULO );
    }
    const int linha = W_TELA - 2 * ua::ESP2;
    for ( int m = 0; m < TM_N; m++ ) {
      const int w = ua::larguraTinta( TEMA_FRASES[m], ua::tipo::corpo );
      printf( "MEDIDA words line \"%s\": %d px in %d\n", TEMA_FRASES[m], w, linha );
      CONFERE( w <= linha, "the line \"%s\" is %d px and its room %d", TEMA_FRASES[m], w, linha );
    }
    int pior = 0;
    for ( int k = 0; k < TEMA_CORES_N; k++ ) {
      char buf[64];
      snprintf( buf, sizeof buf, TEMA_FMT_COR, TEMA_CORES[k] );
      const int w = ua::larguraTinta( buf, ua::tipo::corpo );
      if ( w > pior ) pior = w;
      CONFERE( w <= linha, "the line \"%s\" is %d px and its room %d", buf, w, linha );
    }
    printf( "MEDIDA accent line, the widest of the %d: %d px in %d\n", TEMA_CORES_N, pior, linha );
    const int bw = ua::larguraTinta( TEMA_APLICAR_ROTULO, ua::tipo::botao ), bsala = linha - 2 * ua::PAD;
    printf( "MEDIDA button label \"%s\": %d px in %d\n", TEMA_APLICAR_ROTULO, bw, bsala );
    CONFERE( bw <= bsala, "the button label is %d px and its room %d", bw, bsala );
    // The control of the measure: a label that is too wide is refused by the same comparison.
    CONFERE( ua::larguraTinta( "Automático Automático", TEMA_FONTE_SEGMENTO ) > TEMA_W_ROTULO, "control: a label of two words must not fit a segment" );
  }

  printf( "%d checks, %d failed\n", checks, falhas );
  return falhas ? 1 : 0;
}
