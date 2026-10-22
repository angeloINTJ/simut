// Host test of the Ângulo's text lookup, src/ui/angulo/Textos.h and Textos.cpp (S10c, B2a), driven by tools/tft_ui/test_textos.py.
//
//   test_textos_main run             every case: one line "ok   <name>" or "FAIL <name>: <the check>", then "UA_TEXTOS_MAX <n>"; exit 1 when one failed
//   test_textos_main hashes <file>   <file> holds one string per line, as hex bytes (an empty line is the empty string); for each, prints
//                                    "<ua::fnv1a( )> <the device's fnv1a32( )>" in hex. The device's function is DisplayManager::fnv1a32 of
//                                    src/DisplayManager_LangParser.cpp, cut out by the test into device_fnv.inc and renamed
//
// A section is handed over in a block of exactly its size, so that a read past `n` is the sanitizer's (the test is built with
// -fsanitize=address,undefined, which also finds a leak), and the block is freed right after the call, so that a table which kept a pointer into
// it is found too. malloc( ) is wrapped (-Wl,--wrap=malloc) so that one case can make it fail.
#include "Textos.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "device_fnv.inc"   // uint32_t fnv1a32_dispositivo( const char *s )

extern "C" void *__real_malloc( size_t );
static bool g_malloc_falha = false;
extern "C" void *__wrap_malloc( size_t n ) { return g_malloc_falha ? nullptr : __real_malloc( n ); }

using ua::TextosResultado;

// The published vectors of FNV-1a 32 (isthe.com/chongo/tech/comp/fnv), checked by the compiler before any run.
static_assert( ua::fnv1a( "" ) == 0x811c9dc5u );
static_assert( ua::fnv1a( "a" ) == 0xe40c292cu );
static_assert( ua::fnv1a( "foobar" ) == 0xbf9cf968u );
static_assert( ua::fnv1a( nullptr ) == 0x811c9dc5u );   // as the device's: a null pointer is the offset basis

static int g_ok = 0, g_fail = 0, g_ruins = 0;
static const char *g_nome = "";
static char g_porque[300];

#define CHECK( c )                                                                                                  \
  do {                                                                                                              \
    if ( !( c ) ) {                                                                                                 \
      if ( !g_ruins ) snprintf( g_porque, sizeof g_porque, "%s (line %d)", #c, __LINE__ );                          \
      g_ruins++;                                                                                                    \
    }                                                                                                               \
  } while ( 0 )

// "<hash of the English> <text>" and its line end.
static std::string linha( const std::string &ingles, const std::string &texto, const char *fim = "\n" ) {
  char h[16];
  snprintf( h, sizeof h, "%08x ", (unsigned)ua::fnv1a( ingles.c_str( ) ) );
  return std::string( h ) + texto + fim;
}

static TextosResultado carregar( const std::string &s, bool sem_memoria = false ) {
  char *b = (char *)malloc( s.empty( ) ? 1 : s.size( ) );
  if ( !s.empty( ) ) memcpy( b, s.data( ), s.size( ) );
  g_malloc_falha = sem_memoria;   // only the loader's own malloc( ) is made to fail, not the block above
  const TextosResultado r = ua::textosCarregar( b, s.size( ) );
  g_malloc_falha = false;
  free( b );
  return r;
}

static bool e_ingles( const char *en ) { return ua::txt( en ) == en; }   // the same pointer: the English itself, not a copy
static bool e_texto( const char *en, const std::string &esperado ) {
  const char *r = ua::txt( en );
  return r != en && strlen( r ) == esperado.size( ) && memcmp( r, esperado.data( ), esperado.size( ) ) == 0;
}
static bool nada_guardado( ) { return ua::textosContagem( ) == 0 && ua::textosBytes( ) == 0; }

// A section of exactly `alvo` bytes of good lines (distinct hashes), the last one longer than the others.
static std::string secao_de_tamanho( size_t alvo ) {
  std::string s;
  const size_t k = alvo / 40 - 1;
  for ( size_t i = 1; i <= k; i++ ) {
    char b[64];
    snprintf( b, sizeof b, "%08x %s\n", (unsigned)i, "enchimento 0123456789 abcdefgh" );   // 8 + 1 + 30 + 1 = 40 B
    s += b;
  }
  const size_t resto = alvo - s.size( );   // 40..79
  char h[16];
  snprintf( h, sizeof h, "%08x ", (unsigned)( k + 1 ) );
  s += h;
  s += std::string( resto - 10, 'x' ) + "\n";
  return s;
}

static uint32_t g_rng = 2463534242u;
static uint32_t sorteio( ) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

// ---------------------------------------------------------------------------------------------------------------------------------------

static void c_sem_secao( ) {
  CHECK( nada_guardado( ) );
  CHECK( e_ingles( "Cancel" ) );
  CHECK( e_ingles( "" ) );
  CHECK( ua::txt( nullptr ) == nullptr );   // never dereferenced: it comes back as it went in
}

static void c_acerto( ) {
  // three lines, in no order of hash: the table sorts them
  const std::string s = linha( "Save", "Salvar" ) + linha( "Cancel", "Cancelar" ) + linha( "Settings", "Configurações" );
  CHECK( carregar( s ) == ua::TEXTOS_OK );
  CHECK( ua::textosContagem( ) == 3 );
  CHECK( e_texto( "Save", "Salvar" ) );
  CHECK( e_texto( "Cancel", "Cancelar" ) );
  CHECK( e_texto( "Settings", "Configurações" ) );
  // by content, not by address: the same English in another array hits
  char copia[] = "Cancel";
  CHECK( e_texto( copia, "Cancelar" ) );
}

static void c_falta( ) {
  CHECK( carregar( linha( "Save", "Salvar" ) + linha( "Cancel", "Cancelar" ) ) == ua::TEXTOS_OK );
  CHECK( e_ingles( "Delete" ) );   // a miss gives the English itself
  CHECK( e_ingles( "save" ) );     // the case is part of the English
  CHECK( e_ingles( "Save " ) );
  CHECK( e_ingles( "" ) );
  CHECK( e_texto( "Save", "Salvar" ) );   // and the hits are still there
}

static void c_utf8( ) {
  // 2-byte (accents, degree), 3-byte (ellipsis, a euro sign) and 4-byte (an emoji, outside any font but still bytes) sequences
  const std::string a = "Configurações do alarme", b = "Carregando…", c = "10 °C — 5 €", d = "ok \xF0\x9F\x98\x80", e = "\xC3\xA9\xC3\xA7\xC3\xB1";
  CHECK( carregar( linha( "A", a ) + linha( "B", b ) + linha( "C", c ) + linha( "D", d ) + linha( "E", e ) ) == ua::TEXTOS_OK );
  CHECK( e_texto( "A", a ) );
  CHECK( e_texto( "B", b ) );
  CHECK( e_texto( "C", c ) );
  CHECK( e_texto( "D", d ) );
  CHECK( e_texto( "E", e ) );
}

static void c_vazia( ) {
  CHECK( ua::textosCarregar( nullptr, 0 ) == ua::TEXTOS_VAZIO );
  CHECK( carregar( "" ) == ua::TEXTOS_VAZIO );
  CHECK( carregar( "\n" ) == ua::TEXTOS_VAZIO );
  CHECK( carregar( "\n\n\r\n\r\n" ) == ua::TEXTOS_VAZIO );
  CHECK( ua::textosCarregar( nullptr, 5 ) == ua::TEXTOS_VAZIO );   // no section to read from
  CHECK( nada_guardado( ) );
  CHECK( e_ingles( "Save" ) );
  // the control: the same blank lines around a line that is there are only skipped
  CHECK( carregar( "\n\r\n" + linha( "Save", "Salvar" ) + "\n\n" ) == ua::TEXTOS_OK );
  CHECK( ua::textosContagem( ) == 1 && e_texto( "Save", "Salvar" ) );
}

struct Par {
  const char *quem;
  std::string mau, bom;   // the same section with the defect, and without it
};

static void c_malformada( ) {
  const std::string antes = linha( "Save", "Salvar" ), depois = linha( "Cancel", "Cancelar" );
  const std::string nul( "te\0xt", 5 );
  const std::vector<Par> pares = {
      { "7 hex digits", "1234567 texto\n", "12345678 texto\n" },
      { "9 hex digits", "123456789 texto\n", "12345678 texto\n" },
      { "a letter that is not hex", "1234567g texto\n", "1234567f texto\n" },
      { "no space after the hash", "12345678texto\n", "12345678 texto\n" },
      { "a tab after the hash", "12345678\ttexto\n", "12345678 texto\n" },
      { "no text", "12345678 \n", "12345678 t\n" },
      { "no text and no space", "12345678\n", "12345678 t\n" },
      { "no text, at the end of the section", "12345678", "12345678 t" },
      { "a text of blanks", "12345678 \t \n", "12345678 \tt\n" },
      { "a space before the hash", " 2345678 texto\n", "12345678 texto\n" },
      { "a comment line", "# aviso\n", "\n" },
      { "a line of blanks", "   \n", "\n" },
      { "a NUL in the text", "12345678 " + nul + "\n", "12345678 text\n" },
      { "0x before the hash", "0x345678 texto\n", "00345678 texto\n" },
      { "a minus sign", "-2345678 texto\n", "12345678 texto\n" },
  };
  for ( const Par &p : pares ) {
    const int antes_dos_ruins = g_ruins;
    // the defect alone, then between two good lines (at the end of the section when it has no line end): all or nothing, so the good ones go too
    const bool no_fim = p.mau.back( ) != '\n';
    const std::string entre_mau = antes + p.mau + ( no_fim ? "" : depois ), entre_bom = antes + p.bom + ( no_fim ? "" : depois );
    CHECK( carregar( p.mau ) == ua::TEXTOS_MALFORMADA );
    CHECK( nada_guardado( ) );
    CHECK( carregar( entre_mau ) == ua::TEXTOS_MALFORMADA );
    CHECK( nada_guardado( ) );
    CHECK( e_ingles( "Save" ) && e_ingles( "Cancel" ) );
    // the control: without the defect the same shapes load
    CHECK( carregar( entre_bom ) == ua::TEXTOS_OK );
    CHECK( e_texto( "Save", "Salvar" ) && ( no_fim || e_texto( "Cancel", "Cancelar" ) ) );
    ua::textosLimpar( );
    if ( !antes_dos_ruins && g_ruins ) {   // the first failure is the one kept: say which shape it was
      const std::string com_nome = std::string( "[" ) + p.quem + "] " + g_porque;
      snprintf( g_porque, sizeof g_porque, "%.250s", com_nome.c_str( ) );
    }
  }
}

static void c_duplicada( ) {
  const std::string a = linha( "Save", "Salvar" ), b = linha( "Cancel", "Cancelar" ), c = linha( "Settings", "Configurações" );
  const std::string outra = linha( "Save", "Guardar" );   // the same hash, another text
  CHECK( carregar( a + outra ) == ua::TEXTOS_DUPLICADA );
  CHECK( nada_guardado( ) && e_ingles( "Save" ) );
  CHECK( carregar( a + b + c + outra ) == ua::TEXTOS_DUPLICADA );   // apart in the file
  CHECK( nada_guardado( ) && e_ingles( "Cancel" ) );
  CHECK( carregar( a + a ) == ua::TEXTOS_DUPLICADA );   // the same text twice is a repeat all the same
  CHECK( carregar( "ABCDEF01 x\nabcdef01 y\n" ) == ua::TEXTOS_DUPLICADA );   // hex digits in either case are the same number
  CHECK( nada_guardado( ) );
  // the control: three distinct hashes load
  CHECK( carregar( a + b + c ) == ua::TEXTOS_OK );
  CHECK( ua::textosContagem( ) == 3 );
}

static void c_tamanho( ) {
  const size_t max = ua::UA_TEXTOS_MAX;
  std::string no_limite = secao_de_tamanho( max ), abaixo = secao_de_tamanho( max - 1 ), acima = secao_de_tamanho( max + 1 );
  CHECK( no_limite.size( ) == max && abaixo.size( ) == max - 1 && acima.size( ) == max + 1 );
  CHECK( carregar( acima ) == ua::TEXTOS_GRANDE );   // good lines all of them: the size alone refuses it
  CHECK( nada_guardado( ) && e_ingles( "Save" ) );
  CHECK( carregar( no_limite ) == ua::TEXTOS_OK );   // the bound is inclusive
  CHECK( ua::textosContagem( ) > 100 );
  ua::textosLimpar( );
  CHECK( carregar( abaixo ) == ua::TEXTOS_OK );
  ua::textosLimpar( );
  // a hostile size with a null pointer is refused before anything is read
  CHECK( ua::textosCarregar( nullptr, (size_t)-1 ) == ua::TEXTOS_GRANDE );
  CHECK( nada_guardado( ) );
}

static void c_nova_tentativa( ) {
  CHECK( carregar( "lixo\n" ) == ua::TEXTOS_MALFORMADA );
  CHECK( carregar( "" ) == ua::TEXTOS_VAZIO );
  CHECK( carregar( linha( "Save", "Salvar" ) + linha( "Save", "Guardar" ) ) == ua::TEXTOS_DUPLICADA );
  CHECK( carregar( secao_de_tamanho( ua::UA_TEXTOS_MAX + 1 ) ) == ua::TEXTOS_GRANDE );
  CHECK( nada_guardado( ) );
  CHECK( carregar( linha( "Save", "Salvar" ) ) == ua::TEXTOS_OK );   // no refusal leaves a state that blocks the next try
  CHECK( e_texto( "Save", "Salvar" ) );
}

static void c_segunda_carga( ) {
  CHECK( carregar( linha( "Save", "Salvar" ) ) == ua::TEXTOS_OK );
  const size_t bytes = ua::textosBytes( );
  // Core 1 may hold a text of the table: a second load must not free it, replace it or touch it
  CHECK( carregar( linha( "Save", "Guardar" ) + linha( "Cancel", "Cancelar" ) ) == ua::TEXTOS_JA_CARREGADO );
  CHECK( carregar( "lixo\n" ) == ua::TEXTOS_JA_CARREGADO );
  CHECK( carregar( "" ) == ua::TEXTOS_JA_CARREGADO );
  CHECK( e_texto( "Save", "Salvar" ) && e_ingles( "Cancel" ) );
  CHECK( ua::textosContagem( ) == 1 && ua::textosBytes( ) == bytes );
}

static void c_limpar( ) {
  ua::textosLimpar( );   // with nothing loaded
  CHECK( nada_guardado( ) );
  CHECK( carregar( linha( "Save", "Salvar" ) ) == ua::TEXTOS_OK );
  ua::textosLimpar( );
  ua::textosLimpar( );   // twice
  CHECK( nada_guardado( ) && e_ingles( "Save" ) );
  CHECK( carregar( linha( "Save", "Guardar" ) ) == ua::TEXTOS_OK );   // and a new load is possible
  CHECK( e_texto( "Save", "Guardar" ) );
}

static void c_fim_de_linha( ) {
  // the last line needs no newline, a CR before the newline is not part of the text, a CR inside the text is
  const std::string s = linha( "Save", "Salvar", "\r\n" ) + linha( "Cancel", "Cancelar", "\n" ) + linha( "Settings", "Con\rfig", "\r\n" ) +
                        linha( "Exit", "Sair", "" );
  CHECK( carregar( s ) == ua::TEXTOS_OK );
  CHECK( ua::textosContagem( ) == 4 );
  CHECK( e_texto( "Save", "Salvar" ) );
  CHECK( e_texto( "Cancel", "Cancelar" ) );
  CHECK( e_texto( "Settings", "Con\rfig" ) );
  CHECK( e_texto( "Exit", "Sair" ) );
  // trailing blanks are the text's (only the CR of a line end is not)
  ua::textosLimpar( );
  CHECK( carregar( linha( "Save", "Salvar  " ) ) == ua::TEXTOS_OK );
  CHECK( e_texto( "Save", "Salvar  " ) );
  // ONE CR before the newline is the line end and no more: "abc\r\r\n" is the text "abc\r" (a loader that strips every trailing CR reads "abc"), and a
  // CR that ends the section with no newline after it is a line end too
  ua::textosLimpar( );
  CHECK( carregar( linha( "Save", "abc", "\r\r\n" ) + linha( "Cancel", "x", "\r" ) ) == ua::TEXTOS_OK );
  CHECK( e_texto( "Save", "abc\r" ) );
  CHECK( e_texto( "Cancel", "x" ) );
}

static void c_busca( ) {
  // 500 entries in a shuffled order, and the two extremes of the hash, found by the binary search; 500 others miss
  std::vector<std::string> ing, trad;
  for ( int i = 0; i < 500; i++ ) {
    char b[48];
    snprintf( b, sizeof b, "Entry number %d of the lot", i );
    ing.push_back( b );
    snprintf( b, sizeof b, "Entrada %d, pt-BR ç", i );
    trad.push_back( b );
  }
  std::vector<int> ordem( 500 );
  for ( int i = 0; i < 500; i++ ) ordem[i] = i;
  for ( int i = 499; i > 0; i-- ) std::swap( ordem[i], ordem[sorteio( ) % ( i + 1 )] );
  std::string s = "00000000 baixo\nffffffff alto\n";
  for ( int i : ordem ) s += linha( ing[i], trad[i] );
  CHECK( carregar( s ) == ua::TEXTOS_OK );
  CHECK( ua::textosContagem( ) == 502 );
  int achados = 0, faltas = 0;
  for ( int i = 0; i < 500; i++ ) {
    achados += e_texto( ing[i].c_str( ), trad[i] );
    char b[48];
    snprintf( b, sizeof b, "Another entry %d", i );
    faltas += e_ingles( b );
  }
  CHECK( achados == 500 );
  CHECK( faltas == 500 );
  // the size of the one allocation: the header, 6 B an entry, and each text with its NUL
  size_t texto = 0;
  for ( const std::string &t : trad ) texto += t.size( ) + 1;
  texto += strlen( "baixo" ) + 1 + strlen( "alto" ) + 1;
  CHECK( ua::textosBytes( ) == 8 + 6 * 502 + texto );
}

static void c_bordas( ) {
  // The edges of the binary search: the hundred strings "k0".."k99" sorted by hash, the table holds the middle eighty. The ten smallest
  // hashes miss below the first entry, the ten largest above the last, and the first and the last entries hit.
  std::vector<std::pair<uint32_t, std::string>> todas;
  for ( int i = 0; i < 100; i++ ) {
    char b[16];
    snprintf( b, sizeof b, "k%d", i );
    todas.push_back( { ua::fnv1a( b ), b } );
  }
  std::sort( todas.begin( ), todas.end( ) );
  std::string s;
  for ( int i = 80 - 1; i >= 0; i-- ) s += linha( todas[10 + i].second, "tr " + todas[10 + i].second );
  CHECK( carregar( s ) == ua::TEXTOS_OK );
  CHECK( ua::textosContagem( ) == 80 );
  int abaixo = 0, acima = 0;
  for ( int i = 0; i < 10; i++ ) abaixo += e_ingles( todas[i].second.c_str( ) );
  for ( int i = 90; i < 100; i++ ) acima += e_ingles( todas[i].second.c_str( ) );
  CHECK( abaixo == 10 && acima == 10 );
  CHECK( e_texto( todas[10].second.c_str( ), "tr " + todas[10].second ) );
  CHECK( e_texto( todas[89].second.c_str( ), "tr " + todas[89].second ) );
  int dentro = 0;
  for ( int i = 10; i < 90; i++ ) dentro += e_texto( todas[i].second.c_str( ), "tr " + todas[i].second );
  CHECK( dentro == 80 );
}

static void c_macro( ) {
  CHECK( carregar( linha( "Cancel", "Cancelar" ) ) == ua::TEXTOS_OK );
  CHECK( !strcmp( UA_TXT( "Cancel" ), "Cancelar" ) );
  CHECK( !strcmp( UA_TXT( "Can" "cel" ), "Cancelar" ) );   // adjacent literals are one English, as the compiler joins them
  const char *outra = UA_TXT( "Never translated" );
  CHECK( !strcmp( outra, "Never translated" ) );
  CHECK( !strcmp( UA_TXT( "Cancel" ), ua::txt( "Cancel" ) ) );
}

static void c_memoria( ) {
  CHECK( carregar( linha( "Save", "Salvar" ), true ) == ua::TEXTOS_SEM_MEMORIA );
  CHECK( nada_guardado( ) && e_ingles( "Save" ) );
  CHECK( carregar( linha( "Save", "Salvar" ) ) == ua::TEXTOS_OK );   // and the next try, with memory, loads
  // a refusal after the allocation gives it back (the leak check at exit is the sanitizer's)
  ua::textosLimpar( );
  CHECK( carregar( linha( "Save", "Salvar" ) + linha( "Save", "Guardar" ) ) == ua::TEXTOS_DUPLICADA );
  CHECK( nada_guardado( ) );
}

static void c_fuzz( ) {
  // Random sections, and valid ones with a few bytes broken: nothing crashes (the sanitizer is the judge), a refusal keeps nothing, an
  // acceptance keeps a table no bigger than the section, and a second load is always refused while one is published.
  static const char alfabeto[] = "0123456789abcdefABCDEF \t\r\n\0@#-xX\xC3\xA9\xE2\x80\xA6";
  const size_t na = sizeof alfabeto - 1;
  int aceitas = 0, recusadas = 0;
  for ( int it = 0; it < 40000; it++ ) {
    std::string s;
    if ( it % 2 == 0 ) {
      const size_t n = sorteio( ) % 200;
      for ( size_t i = 0; i < n; i++ ) s += alfabeto[sorteio( ) % na];
    } else {
      for ( int i = 0; i < 6; i++ ) s += linha( std::string( "T" ) + (char)( 'a' + i ), std::string( "texto ç " ) + (char)( 'a' + i ), i % 2 ? "\r\n" : "\n" );
      for ( int m = (int)( sorteio( ) % 4 ); m > 0 && !s.empty( ); m-- ) {
        const size_t p = sorteio( ) % s.size( );
        switch ( sorteio( ) % 3 ) {
        case 0: s[p] = alfabeto[sorteio( ) % na]; break;
        case 1: s.erase( p, 1 ); break;
        default: s.insert( p, 1, alfabeto[sorteio( ) % na] ); break;
        }
      }
    }
    const TextosResultado r = carregar( s );
    if ( r == ua::TEXTOS_OK ) {
      aceitas++;
      CHECK( ua::textosContagem( ) >= 1 && ua::textosBytes( ) <= s.size( ) + 8 );
      CHECK( carregar( s ) == ua::TEXTOS_JA_CARREGADO );
    } else {
      recusadas++;
      CHECK( nada_guardado( ) );
    }
    ua::textosLimpar( );
    if ( g_ruins ) break;
  }
  CHECK( aceitas > 1000 && recusadas > 1000 );   // the generator reaches both sides
}

static const struct {
  const char *nome;
  void ( *f )( );
} CASOS[] = {
    { "no section loaded: every text is its English, the same pointer", c_sem_secao },
    { "a hit gives the pack's text, whatever the order of the lines", c_acerto },
    { "a miss gives the English itself (the same pointer); the case and the blanks count", c_falta },
    { "UTF-8 is kept byte for byte (2, 3 and 4-byte sequences)", c_utf8 },
    { "an empty section (no bytes, blank lines, no pointer) keeps nothing", c_vazia },
    { "a malformed line is refused with nothing kept, 15 shapes, each with its good twin", c_malformada },
    { "a duplicate hash is refused with nothing kept (adjacent, apart, in either case)", c_duplicada },
    { "a section over UA_TEXTOS_MAX is refused, one of exactly that size loads", c_tamanho },
    { "after every kind of refusal the next good section loads", c_nova_tentativa },
    { "a second load while one is published is refused and the first stays", c_segunda_carga },
    { "textosLimpar( ) gives back English, twice, and a new load follows", c_limpar },
    { "line ends: no newline on the last line, CRLF, a CR inside a text, a second CR kept, trailing blanks", c_fim_de_linha },
    { "500 entries shuffled and both ends of the hash: all found, 500 misses give the English", c_busca },
    { "the edges of the binary search: a miss below the first entry and above the last, a hit on each", c_bordas },
    { "UA_TXT( ) gives ua::txt( ) of its English, adjacent literals joined", c_macro },
    { "malloc( ) failing keeps nothing; a refusal after the allocation gives it back", c_memoria },
    { "40000 random and broken sections: no crash, refusal keeps nothing, acceptance is bounded", c_fuzz },
};

static std::string de_hex( const std::string &h ) {
  std::string out;
  for ( size_t i = 0; i + 1 < h.size( ); i += 2 ) out += (char)strtol( h.substr( i, 2 ).c_str( ), nullptr, 16 );
  return out;
}

int main( int argc, char **argv ) {
  if ( argc >= 3 && !strcmp( argv[1], "hashes" ) ) {
    FILE *f = fopen( argv[2], "rb" );
    if ( !f ) { perror( argv[2] ); return 2; }
    char l[1024];
    while ( fgets( l, sizeof l, f ) ) {
      std::string h( l );
      while ( !h.empty( ) && ( h.back( ) == '\n' || h.back( ) == '\r' ) ) h.pop_back( );
      const std::string s = de_hex( h );
      printf( "%08x %08x\n", (unsigned)ua::fnv1a( s.c_str( ) ), (unsigned)fnv1a32_dispositivo( s.c_str( ) ) );
    }
    fclose( f );
    return 0;
  }
  if ( argc < 2 || strcmp( argv[1], "run" ) ) { fprintf( stderr, "usage: %s run | hashes <file>\n", argv[0] ); return 2; }
  for ( const auto &c : CASOS ) {
    g_nome = c.nome;
    g_ruins = 0;
    ua::textosLimpar( );
    c.f( );
    ua::textosLimpar( );
    if ( g_ruins ) { g_fail++; printf( "FAIL %s: %s\n", g_nome, g_porque ); }
    else { g_ok++; printf( "ok   %s\n", g_nome ); }
    fflush( stdout );
  }
  printf( "UA_TEXTOS_MAX %zu\n", (size_t)ua::UA_TEXTOS_MAX );
  printf( "%d cases, %d failed\n", g_ok + g_fail, g_fail );
  return g_fail ? 1 : 0;
}
