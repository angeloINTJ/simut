/**
 * @file DisplayManager_UiA_Textos.cpp
 * @brief The Ângulo build's glue for the texts the panel draws (S10c, B2b): at boot it loads the active language pack's @ANGULO section into
 *        the table UA_TXT( ) answers from (Textos.h). pico_wiretft_angulo and pico2_w_angulo only.
 *
 * The chain, end to end. LangPackScanner (src/LangPackIndex.h) finds the section while loadLangFile( ) streams the pack, and the loader keeps
 * its byte range in _activeLang (anguloOffset, anguloLen: DisplayManager_LangParser.cpp), the way it keeps @HELP's and @WEBDICT's. AppManager::setup( )
 * then calls uiLangLoaded( ), once, on Core 0, right after the block that loads the pack (AppManager_Boot.cpp). This function reads those bytes
 * from LittleFS into one temporary buffer of exactly their length, hands it to ua::textosCarregar( ), which copies what it keeps into the one
 * allocation the table is, and frees the buffer. A board in English loads no pack, so there is no range and nothing happens: every text shows its English.
 *
 * What it costs at the boot, at the most: the buffer plus the table, both alive while the table is built. The section is refused above
 * UA_TEXTOS_MAX (16,384 B) before anything is opened or allocated, so the worst case is 32,774 B (the review of B2b, measured: 2 * 16,384 + 6 when the section is the file's last and has no final newline; 32,773 with it), and the
 * section B2a projected for pt-BR (10,413 B, 408 texts) peaks at 10,413 + 9,197 = 19,610 B; what stays afterwards is the table alone, 9,197 B (16,390 B at the most). Both
 * numbers are measured by tools/tft_ui/test_textos.py on the host, not on a board (B7 measures the heap there).
 *
 * What is logged, and by whom. Only a refusal, and not here: this function returns 0, or the ua::TextosResultado of the refusal, and the core's call site
 * logs it (AppManager_Boot.cpp, where the core logs everything). It does not log itself because LOG_CODE( ) instantiates LogManager::instance( ), whose
 * function-local singleton (516 B and a 4 B guard) tools/check_ui_ram.py then counts as the Ângulo's static RAM, though the image has it from LogManager.cpp
 * all the same (measured on 2026-10-22: 9,695 B against the budget's 9,171 B, +524 B for 4 B of real RAM). A pack with no @ANGULO section, and a section that
 * loaded, leave no record of their own (the pack loader's I18N_PACK_LOADED already says the pack loaded, and a pack without the section shows the English,
 * as an older pack does). A refusal is I18N_PACK_FAILED, the code the loader uses for a pack that did not load, at LOG_WARN with the context 100 + the result:
 * 101 empty, 102 over UA_TEXTOS_MAX, 103 a malformed line, 104 a repeated hash, 105 no memory, 106 a table already published, 107 the pack could not be
 * read. The loader's own record of that code is LOG_ERROR with context 0 (DisplayManager_LangParser.cpp, the only other use), so neither can be taken for
 * the other. A refused section keeps nothing (all or nothing), so the panel shows its English. No code of its own: a name in translateCodeEn( ) would be
 * in the nine images (S10c, B2b).
 *
 * Core 1 may already be drawing when this runs (startCore1( ) is earlier in setup( )): it sees no table or the whole one, because the table is
 * published with one release store (Textos.cpp). tools/tft_ui/test_textos.py compiles this function, cut out of this file by brace matching, with a
 * LittleFS and a LOG_CODE of its own and the real scanner and kit, and runs planted packs through the whole chain; keep the function in one piece, with
 * no preprocessor line inside it.
 */

#include "../../DisplayManager.h"

#include <LittleFS.h>
#include <stdlib.h>

#include "Textos.h"

int DisplayManager::uiLangLoaded( ) {
  // No pack (the board runs in English, or the pack was refused whole) or a pack without @ANGULO: nothing was asked, so nothing is reported.
  if ( !_activeLangLoaded || _activeLang.anguloLen == 0 ) return 0;
  ua::TextosResultado r = ua::TEXTOS_LEITURA;
  const uint32_t len = _activeLang.anguloLen;
  if ( len > ua::UA_TEXTOS_MAX ) {
    r = ua::TEXTOS_GRANDE;   // refused before the file is opened or a byte is allocated
  } else {
    File f = LittleFS.open( _activeLang.path, "r" );
    if ( f ) {
      char *secao = static_cast<char *>( malloc( len ) );
      if ( !secao ) {
        r = ua::TEXTOS_SEM_MEMORIA;
      } else {
        if ( f.seek( _activeLang.anguloOffset ) && f.readBytes( secao, len ) == len ) r = ua::textosCarregar( secao, len );
        free( secao );
      }
      f.close( );
    }
  }
  return (int)r;   // 0 is ua::TEXTOS_OK: the table is published
}
