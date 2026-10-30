// The publication of the Ângulo's text table under ThreadSanitizer (S10c, B2a-b), driven by tools/tft_ui/test_textos.py.
//
// Core 0 builds the table (ua::textosCarregar) and publishes it with one release store; Core 1 reads it (ua::txt) with acquire loads. The host has
// one view of memory, so the order of the stores cannot be seen by running; ThreadSanitizer instead models the happens-before the atomics give. Three
// reader threads call txt( ) in a loop while this thread loads a section of 380 texts (the Ângulo has 408): with the release store and the acquire
// loads every read of the table happens after its fill and the run is silent; with a relaxed store, or a store before the fill, the readers read the
// table with nothing ordering them after the writes and ThreadSanitizer reports a data race.
//
// The readers must see the table (hits > 0): a silent run of readers that only ever saw "no table" proves nothing. Prints
// "tsan run: load=<TextosResultado> hits=<n> bad=<n>"; exit 0 when the load was TEXTOS_OK, no reader saw a wrong text and the readers together got
// at least 3,000 hits (the loop waits for them, up to 5 s). The Python side reads ThreadSanitizer's own report from stderr.
#include "Textos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

static std::string linha( const std::string &ingles, const std::string &texto ) {
  char h[16];
  snprintf( h, sizeof h, "%08x ", (unsigned)ua::fnv1a( ingles.c_str( ) ) );
  return std::string( h ) + texto + "\n";
}

int main( ) {
  const int N = 380, LEITORES = 3, PASSO = 37;
  std::string secao;
  std::vector<std::string> ingles, esperado;
  for ( int i = 0; i < N; i++ ) {
    char en[32];
    snprintf( en, sizeof en, "English %d", i );
    secao += linha( en, std::string( "Traduzido " ) + en );
    if ( i % PASSO == 0 ) {
      ingles.push_back( en );
      esperado.push_back( std::string( "Traduzido " ) + en );
    }
  }
  std::atomic<bool> pronto{ false }, parar{ false };
  std::atomic<long> acertos{ 0 }, ruins{ 0 };
  std::vector<std::thread> leitores;
  for ( int t = 0; t < LEITORES; t++ )
    leitores.emplace_back( [&] {
      while ( !pronto.load( ) ) {}
      while ( !parar.load( ) ) {
        for ( size_t k = 0; k < ingles.size( ); k++ ) {
          const char *r = ua::txt( ingles[k].c_str( ) );
          if ( r != ingles[k].c_str( ) ) {   // not the English itself: the table answered
            acertos++;
            if ( strcmp( r, esperado[k].c_str( ) ) != 0 ) ruins++;
          }
        }
      }
    } );
  pronto = true;
  std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );   // the readers run against "no table" first
  char *b = (char *)malloc( secao.size( ) );   // a block of exactly the section's size, freed at once
  memcpy( b, secao.data( ), secao.size( ) );
  const ua::TextosResultado r = ua::textosCarregar( b, secao.size( ) );
  free( b );
  for ( int ms = 0; ms < 5000 && acertos.load( ) < 3000; ms += 5 ) std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );   // until they have seen it
  parar = true;
  for ( auto &t : leitores ) t.join( );
  printf( "tsan run: load=%d hits=%ld bad=%ld\n", (int)r, acertos.load( ), ruins.load( ) );
  ua::textosLimpar( );
  return r == ua::TEXTOS_OK && ruins.load( ) == 0 && acertos.load( ) >= 3000 ? 0 : 1;
}
