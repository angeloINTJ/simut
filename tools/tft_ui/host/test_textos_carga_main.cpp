// test_textos_carga_main.cpp -- the chain of S10c B2b on the host, driven by tools/tft_ui/test_textos.py.
//
//   a pack's bytes -> LangPackScanner (src/LangPackIndex.h, the real one, SIMUT_UI_ANGULO from the command line)
//   -> the loader's store of the section's range (cut from src/DisplayManager_LangParser.cpp, carregador.inc)
//   -> DisplayManager::uiLangLoaded( ) (cut from src/ui/angulo/DisplayManager_UiA_Textos.cpp, glue.inc), which returns 0 or why it kept no table
//   -> the core's call site and its log record (cut from src/AppManager_Boot.cpp, chamada.inc)
//   -> ua::textosCarregar( ) and UA_TXT( ) (src/ui/angulo/Textos.cpp, the real one).
//
// What is stubbed is what the device gives and the host does not: LittleFS (a File over a host file, with a read that can come short and an
// open that can fail), LOG_CODE (it records), and the class DisplayManager reduced to the members the cuts touch. The cuts are the device's
// text, byte for byte; the test refuses to run if the members the stub declares are not the ones DisplayManager.h declares.
//
// Output, one line per fact (the test reads them):
//   INDEX sizeof=<n> tail=<0|1> [<name>=<present>:<start>:<end> ...]
//   KEEP loaded=<0|1> offset=<n> len=<n>
//   CALL opens=<n> mallocs=<n> peak=<bytes live at the most> live=<bytes still live after the call>
//   LOG <level> <tag> <code> <ctx> <text>
//   TABLE count=<n> bytes=<n>
//   TXT <hex of the English> <hex of what ua::txt( ) gave>
//   LIT <n> <hex of what UA_TXT( literal n ) gave>
//
// Usage: test_textos_carga [--texts FILE] [--short N] [--noopen] [--nomem K] [--inactive] [--twice] PACK
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "LangPackIndex.h"
#include "Textos.h"

#if SIMUT_UI_ANGULO   // the original build of this program only prints the scanner's index (the nine's scanner, before and after B2b)
// ---- malloc( ) and free( ), wrapped at link time (-Wl,--wrap=malloc,--wrap=free): the count, the bytes alive and the most alive, and one that fails
extern "C" {
void *__real_malloc( size_t n );
void __real_free( void *p );
}
namespace {
struct Bloco { void *p; size_t n; };
Bloco gBlocos[64];
unsigned gChamadas = 0, gFalhaNa = 0;
size_t gVivo = 0, gPico = 0;
void zerarContas( ) { gChamadas = 0; gVivo = 0; gPico = 0; for ( Bloco &b : gBlocos ) b = Bloco{ nullptr, 0 }; }
}  // namespace
extern "C" void *__wrap_malloc( size_t n ) {
  gChamadas++;
  if ( gFalhaNa && gChamadas == gFalhaNa ) return nullptr;
  void *p = __real_malloc( n );
  for ( Bloco &b : gBlocos )
    if ( !b.p ) { b = Bloco{ p, n }; gVivo += n; if ( gVivo > gPico ) gPico = gVivo; break; }
  return p;
}
extern "C" void __wrap_free( void *p ) {
  for ( Bloco &b : gBlocos )
    if ( b.p == p && p ) { gVivo -= b.n; b = Bloco{ nullptr, 0 }; break; }
  __real_free( p );
}

// ---- LittleFS, as far as the glue uses it
namespace {
size_t gFalta = 0;       // --short N: every read gives N bytes fewer than asked
bool gNaoAbre = false;   // --noopen
unsigned gAbertos = 0;
}  // namespace
class File {
 public:
  File( ) {}
  explicit File( FILE *p ) : fp( p ) {}
  File( File &&o ) : fp( o.fp ) { o.fp = nullptr; }
  File( const File & ) = delete;
  ~File( ) { close( ); }
  explicit operator bool( ) const { return fp != nullptr; }
  bool seek( uint32_t pos ) { return fp && fseek( fp, (long)pos, SEEK_SET ) == 0; }
  size_t readBytes( char *dst, size_t n ) {
    const size_t got = fp ? fread( dst, 1, n, fp ) : 0;
    return got > gFalta ? got - gFalta : 0;
  }
  void close( ) {
    if ( fp ) fclose( fp );
    fp = nullptr;
  }
 private:
  FILE *fp = nullptr;
};
struct FS {
  File open( const char *caminho, const char *modo ) {
    (void)modo;
    if ( gNaoAbre ) return File( );
    gAbertos++;
    return File( fopen( caminho, "rb" ) );
  }
} LittleFS;

// ---- LOG_CODE( ), as far as the glue uses it: it records
enum LogLevel { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };
enum LogCode { I18N_PACK_FAILED = I18N_PACK_FAILED_VALUE };
struct Registro { int nivel; std::string tag; int codigo; int ctx; std::string texto; };
static std::vector<Registro> gRegistros;
#define LOG_CODE( lvl, tag, code, ctx, msg ) gRegistros.push_back( Registro{ (int)( lvl ), ( tag ), (int)( code ), (int)( ctx ), ( msg ) } )

// ---- DisplayManager, reduced to the members the two cuts touch (the test checks them against src/DisplayManager.h)
class DisplayManager {
 public:
  struct ActiveLang { char path[64]; uint32_t anguloOffset; uint32_t anguloLen; };
  static ActiveLang _activeLang;
  static bool _activeLangLoaded;
  int uiLangLoaded( );
  static void guardarIntervalo( const LangPackIndex &ix );
};
DisplayManager::ActiveLang DisplayManager::_activeLang = {};
bool DisplayManager::_activeLangLoaded = false;

#include "glue.inc"          // void DisplayManager::uiLangLoaded( ) { ... }, as in src/ui/angulo/DisplayManager_UiA_Textos.cpp
void DisplayManager::guardarIntervalo( const LangPackIndex &ix ) {
#include "carregador.inc"    // the statement of loadLangFile( ) that keeps the section's range, as in src/DisplayManager_LangParser.cpp
}
#endif   // SIMUT_UI_ANGULO

#if SIMUT_UI_ANGULO
namespace {
std::string deHex( const std::string &h ) {
  std::string s;
  for ( size_t i = 0; i + 1 < h.size( ); i += 2 ) s += (char)strtol( h.substr( i, 2 ).c_str( ), nullptr, 16 );
  return s;
}
std::string emHex( const char *s ) {
  std::string h;
  char b[3];
  for ( ; *s; s++ ) { snprintf( b, sizeof b, "%02x", (unsigned char)*s ); h += b; }
  return h;
}
}  // namespace
#endif

int main( int argc, char **argv ) {
  const char *pacote = nullptr, *textos = nullptr;
  bool inativo = false, duas = false;
  for ( int i = 1; i < argc; i++ ) {
    const std::string a = argv[i];
    if ( a == "--texts" && i + 1 < argc ) textos = argv[++i];
#if SIMUT_UI_ANGULO
    else if ( a == "--short" && i + 1 < argc ) gFalta = (size_t)atol( argv[++i] );
    else if ( a == "--noopen" ) gNaoAbre = true;
    else if ( a == "--nomem" && i + 1 < argc ) gFalhaNa = (unsigned)atoi( argv[++i] );
#endif
    else if ( a == "--inactive" ) inativo = true;
    else if ( a == "--twice" ) duas = true;
    else pacote = argv[i];
  }
  if ( !pacote ) { fprintf( stderr, "no pack\n" ); return 2; }

  // loadLangFile( )'s first pass: the whole file through a 256 B chunk into the scanner
  FILE *f = fopen( pacote, "rb" );
  if ( !f ) { fprintf( stderr, "cannot open %s\n", pacote ); return 2; }
  LangPackScanner scan;
  char chunk[256];
  for ( size_t got; ( got = fread( chunk, 1, sizeof chunk, f ) ) > 0; ) scan.feed( chunk, got );
  fclose( f );
  LangPackIndex ix;
  scan.finish( ix );
  static const char *const nomes[] = { "NAME", "CODE", "DICT", "HELP", "LOGCODES", "TRL", "WEBDICT"
#if SIMUT_UI_ANGULO
                                       , "ANGULO"
#endif
  };
  printf( "INDEX sizeof=%zu tail=%d", sizeof( LangPackIndex ), (int)ix.tailAfterWebDict );
  static_assert( sizeof( nomes ) / sizeof( nomes[0] ) == LANG_SEC_COUNT, "a section of the scanner has no name here" );
  for ( int s = 0; s < LANG_SEC_COUNT; s++ ) printf( " %s=%d:%u:%u", nomes[s], (int)ix.present[s], ix.start[s], ix.end[s] );
  printf( "\n" );

#if SIMUT_UI_ANGULO
  // the loader refuses a pack with a directive after @WEBDICT's marker; every pack planted here has a @DICT, so that is the one rule emulated
  DisplayManager::_activeLangLoaded = !ix.tailAfterWebDict && !inativo;
  strncpy( DisplayManager::_activeLang.path, pacote, sizeof( DisplayManager::_activeLang.path ) - 1 );
  if ( !ix.tailAfterWebDict ) DisplayManager::guardarIntervalo( ix );   // loadLangFile( ) stores the ranges before it marks the pack loaded
  printf( "KEEP loaded=%d offset=%u len=%u\n", (int)DisplayManager::_activeLangLoaded, DisplayManager::_activeLang.anguloOffset,
          DisplayManager::_activeLang.anguloLen );

  zerarContas( );
  DisplayManager dm;
  DisplayManager *_displayMgr = &dm;   // the name AppManager::setup( ) calls the hook through
  for ( int vez = 0; vez < ( duas ? 2 : 1 ); vez++ ) {
#include "chamada.inc"       // the core's call site in AppManager::setup( ): the hook, and the log of what it returns, as in src/AppManager_Boot.cpp
  }
  printf( "CALL opens=%u mallocs=%u peak=%zu live=%zu\n", gAbertos, gChamadas, gPico, gVivo );
  for ( const Registro &r : gRegistros ) printf( "LOG %d %s %d %d %s\n", r.nivel, r.tag.c_str( ), r.codigo, r.ctx, r.texto.c_str( ) );
  printf( "TABLE count=%zu bytes=%zu\n", ua::textosContagem( ), ua::textosBytes( ) );

  if ( textos ) {
    FILE *t = fopen( textos, "r" );
    char linha[1024];
    while ( t && fgets( linha, sizeof linha, t ) ) {
      std::string h = linha;
      while ( !h.empty( ) && ( h.back( ) == '\n' || h.back( ) == '\r' ) ) h.pop_back( );
      const std::string en = deHex( h );
      printf( "TXT %s %s\n", h.c_str( ), emHex( ua::txt( en.c_str( ) ) ).c_str( ) );
    }
    if ( t ) fclose( t );
  }
  // the macro, on literals: it hashes the compiler's bytes, which the pack's lines were keyed by
  printf( "LIT 0 %s\n", emHex( UA_TXT( "Cancel" ) ).c_str( ) );
  printf( "LIT 1 %s\n", emHex( UA_TXT( "Save changes" ) ).c_str( ) );
  printf( "LIT 2 %s\n", emHex( UA_TXT( "Na\xC3\xAFve caf\xC3\xA9" ) ).c_str( ) );
  ua::textosLimpar( );
#else
  (void)textos; (void)inativo; (void)duas;   // the original build only prints the scanner's index
#endif
  return 0;
}
