#pragma once
// The Ângulo's text lookup (S10c, B2a). A text the panel draws is written once, in English, in the code, as UA_TXT( "Cancel" ); the pack of the
// language the device runs (/lang/language_<code>.lng) gives the text that stands for that English, or, when it has none, the English itself
// comes back. The pack carries them in a section of its own, so that the original panel's dictionary is left as it is:
//
//   @ANGULO
//   <8 hex digits: FNV-1a 32 of the English><one space><the text, to the end of the line>
//   ...
//
// Why a section of its own. The TRL macro is not a mechanism: DisplayManager::trlLookup( ) answers "absent" since 2026-08-29, so a text under it
// is the English on the device (src/DisplayManager_LangParser.cpp). The original panel reads @DICT, a positional dictionary the products load whole into
// the heap (LANG_DICT_MAX, 6,144 B), and the Ângulo's texts would not fit there and would cost every product that heap (S10c B1 counted 408
// distinct literals; once every screen was converted, 2026-10-23, the packs hold 373 texts, 5,611 B of pt-BR UTF-8, beside @DICT's 3,292 B). The
// loader of the products does not know @ANGULO: LangPackScanner (src/LangPackIndex.h) takes an unknown directive as the end of the body before it
// and reads on, so the products' heap does not grow. @ANGULO has to come before @WEBDICT, which has to be the file's suffix.
//
// The hash is of the English's bytes up to the NUL, the function of DisplayManager::fnv1a32 (tools/tft_ui/test_textos.py cuts that one out of the
// device's source and compares the two on 50 strings). It is computed when the text is asked for and not at compile time, because a constant at
// each call site costs flash at every site and saves one pass over the English: measured on 2026-10-22 with the project's flags (-Os), 100 sites
// are 4,146 B with the hash at run time and 4,750 B with a constant per site on the Cortex-M0+ (4,142 and 4,738 B on the Cortex-M33), 6.0 B a site,
// which is 3.1 KB over the 523 sites B1 counted. When no pack is loaded txt( ) returns before it hashes anything.
//
// WHERE IT MAY BE CALLED. Where the text is drawn, never in an initialiser that runs before main( ): a table of UA_TXT( ) built at start-up
// holds the English for good, because the pack is not loaded yet.
//
// A LOOKED-UP TEXT IS NEVER A FORMAT. The text is a file anyone with PERM_FILE_UPLOAD can put on the device (37 of the 408 texts hold a
// conversion, 2026-10-22), the loader keeps only hashes and so cannot compare a translation's conversions with the English's, and the build has no
// -Wformat-nonliteral: snprintf( b, 16, UA_TXT( "%d min" ), v ) compiles under -Wall -Wextra -Werror, and a translation with another conversion,
// or a %n, would read the stack or write where nobody pointed. So the words go in as arguments of a format that is a literal:
// snprintf( b, 16, "%d %s", v, UA_TXT( "min" ) ). If a language's word order ever forces a conversion into a translation, a call-site helper must
// compare the translation's conversion sequence with the English literal's (same count and types, no %n, no *) and fall back to the English on
// any difference; none exists yet. What holds today: tools/check_ui_texts.py refuses UA_TXT( ) as the format argument of a printf-family call (and
// of a kit function declared with a const char * before ... or a va_list). A text that reaches printf through a variable is for
// -Wformat-nonliteral, which is on from the end of this header (S10c B2b, the two pragmas below): measured on 2026-10-22 on the Ângulo's units of both
// Ângulo images it names 0 sites in src/ui/angulo. The pragma holds to the end of the translation unit, so a unit includes this header AFTER the project's
// own headers: -Wformat-nonliteral names src/SystemDefs_Records.h:962, a lambda of a header the panel's units include, and that one is not the panel's to fix.
//
// Built only by the Ângulo UI driver and the host harness. textosCarregar( ) is called once, on Core 0, from DisplayManager::uiLangLoaded( )
// (DisplayManager_UiA_Textos.cpp, S10c B2b), with the body of the pack's @ANGULO section, which the loader found by byte range and uiLangLoaded( )
// read into one temporary buffer (refused above UA_TEXTOS_MAX before anything is allocated) and freed. Core 1 may already be drawing: it sees
// no table or the whole one.

#include <stddef.h>
#include <stdint.h>

namespace ua {

// The most bytes an @ANGULO section may hold; a section over it is refused and every text shows its English. The gate (tools/check_lang_packs.py)
// reads this line, and holds both packs to it. A line is its text plus 10 B (8 hex digits, a space, the newline); the table textosCarregar( ) builds from the section takes an 8 B header,
// 6 B an entry and each text with its NUL. B2a projected on 2026-10-22 (check_ui_texts.py on 549f4253) 408 distinct texts, 6,333 B of pt-BR UTF-8: a section of 10,413 B and a table of 9,197 B, so 16,384 B was 1.57 times the section:
// room for the texts that are still to be written (es-ES is longer than pt-BR); the gate warns at 95 %, while a hostile pack cannot make the device hold more than this plus the 8 B header.
// Offsets into the text area are 16 bits, so the bound stays under 65,536. Measured on 2026-10-23, every screen converted (S10c B5b-b): 373 texts, the sections 9,341 B (pt-BR) and 9,590 B (es-ES),
// the tables 8,230 and 8,479 B, 16,384 B 1.71 times the larger. Measured on 2026-10-28 (tools/check_lang_packs.py for the sections; python3 tools/tft_ui/host_telas.py --idioma pt menus, or es, for the tables in RAM):
// 376 texts, the sections 9,536 B (pt-BR) and 9,797 B (es-ES), the tables 8,416 and 8,677 B; 16,384 B is 1.67 times the larger.
constexpr size_t UA_TEXTOS_MAX = 16384;
static_assert( UA_TEXTOS_MAX < 0x10000 );

// FNV-1a, 32 bits, over the bytes of `s` up to the NUL. A null pointer gives the offset basis, as DisplayManager::fnv1a32 does. The bytes are
// taken as unsigned: with a signed char the high bytes of a UTF-8 sequence would sign-extend and the hash would differ from the pack's.
constexpr uint32_t fnv1a( const char *s ) {
  uint32_t h = 0x811c9dc5u;
  if ( !s ) return h;
  while ( *s ) {
    h ^= (uint8_t)*s++;
    h *= 0x01000193u;
  }
  return h;
}

// What textosCarregar( ) made of a section. Only TEXTOS_OK keeps anything; after every other result the table is as it was before the call.
enum TextosResultado : uint8_t {
  TEXTOS_OK = 0,         // loaded: textosContagem( ) texts, every text not in the pack shows its English
  TEXTOS_VAZIO,          // no line (no section, no byte, or blank lines only): nothing to keep
  TEXTOS_GRANDE,         // more than UA_TEXTOS_MAX bytes
  TEXTOS_MALFORMADA,     // a line that is not "<8 hex digits> <text>" (the text must show a character and hold no NUL)
  TEXTOS_DUPLICADA,      // two lines with the same hash
  TEXTOS_SEM_MEMORIA,    // malloc( ) gave nothing
  TEXTOS_JA_CARREGADO,   // a table is already published: it stays
  TEXTOS_LEITURA,        // never returned by textosCarregar( ): the caller's own, when the pack did not open or gave fewer bytes than the section is long
};

// Builds the table from the body of an @ANGULO section: `n` bytes at `secao`, lines ended by '\n' with a '\r' before it allowed, blank lines
// skipped, the last line needing no end. One allocation: an 8 B header, the hashes in ascending order, the offsets of the texts and the texts
// (UTF-8, byte for byte), copied, so the caller may free `secao` at once. All or nothing: a malformed line, a repeated hash or a section over
// UA_TEXTOS_MAX refuses the whole section and keeps nothing, so every text shows its English. Core 0, once, at boot right after the pack loads,
// when Core 1 may already be drawing (it sees no table or the whole one, and the first boot frames are English, as the original's are): a table
// is never replaced or freed while the device runs, because Core 1 may hold a text of it (a second call returns TEXTOS_JA_CARREGADO).
TextosResultado textosCarregar( const char *secao, size_t n );

#ifdef UA_HOST
// Gives the table back. For the host tests only, and compiled only under UA_HOST (the define the host harness passes): on the device Core 1 may
// still be drawing a text of the table, so no device build can call it. tools/tft_ui/test_textos.py checks the object of a build without UA_HOST.
void textosLimpar( );
#endif

size_t textosContagem( );   // texts in the published table, 0 when there is none
size_t textosBytes( );      // bytes of its allocation, 0 when there is none

// The pack's text for `en`, or `en` itself. A binary search over the hashes, in RAM, no allocation: any core, any time.
const char *txt( const char *en );

}  // namespace ua

// The argument has to be a string literal: the `""` in front makes anything else (a variable, a number) a compile error, and joins adjacent
// literals as the compiler does, so UA_TXT( "a " "b" ) is one English. tools/check_ui_texts.py and tools/check_lang_packs.py read the literals
// of the sources the same way.
#define UA_TXT( en ) ( ::ua::txt( "" en ) )

// A looked-up text is never a format, on the compiler (S10c, B2b). With the project's -Wall -Wextra -Werror, from here to the end of the translation unit a
// call to a function declared with the format attribute (the printf family of the C library) whose format is not a literal is an error: -Wformat-nonliteral when it has arguments (snprintf( b, n, UA_TXT( "x %d" ), 1 )) and
// -Wformat-security when it has none (printf( UA_TXT( "x" ) )). The words go in as arguments of a literal format: snprintf( b, n, "%d %s", v, UA_TXT( "min" ) ).
// tools/tft_ui/test_textos.py plants both calls on g++ and on the cross-compiler and holds that they fail, and that they compile once these two lines are out.
// A formatter without the attribute (Serial.printf, a variadic of the kit's own) is not seen here: tools/check_ui_texts.py refuses UA_TXT as its format.
#pragma GCC diagnostic error "-Wformat-nonliteral"
#pragma GCC diagnostic error "-Wformat-security"
