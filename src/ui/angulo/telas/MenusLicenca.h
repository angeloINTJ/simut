#pragma once
// The licence's text for the licence screen (MenusStatus.h: VmLicenca): the
// sources of the old screen's text (the opening, then the licence and the
// third-party list), laid out in tipo::apoio at LICENCA_LARGURA once, and kept
// as where each line starts, so a page is copied out of the sources and no
// wrapped text is stored (about 125 lines of 4 bytes, not 125 of 96).
//
// Pure, like the screens: the host harness runs this very code on the real
// MIT text (tools/tft_ui/amostras/menus.cpp), and the glue only hands it the
// sources the firmware has.
#include "MenusStatus.h"

namespace ua {
namespace tela {

// Lines the index holds; the rest of a longer text is left out. The real text is 134 lines in the
// wired build and 145 in the radio one (indexed on the host with the 9 pt text, 2026-10-15; 115 and
// 125 at 7 pt, the day before): 192 leaves a third more.
constexpr int LICENCA_INDICE_MAX = 192;
// The sources of the licence screen, worst case (the glue, DisplayManager_UiA_Menus.cpp): the five strings of the English opening, used when the pack has none; LICENSE_TEXT_EN, which
// is ONE source (the MIT text and the third-party list in a single string); and this panel's own fonts and icons (anexarFontesDoAngulo( ), S10c C4): 5 + 1 + 4 = 10. 12 leaves two.
// The glue and the function state their own counts against these constants (a static_assert each), so a source added to either cannot pass the maximum unseen.
constexpr int LICENCA_FONTES_ABERTURA = 5;
constexpr int LICENCA_FONTES_TEXTO = 1;
constexpr int LICENCA_FONTES_ANGULO = 4;
constexpr int LICENCA_FONTES_MAX = 12;
static_assert( LICENCA_FONTES_ABERTURA + LICENCA_FONTES_TEXTO + LICENCA_FONTES_ANGULO <= LICENCA_FONTES_MAX );   // fails when the worst case passes the maximum: raise it

// One source of the text and how many blank lines follow it.
struct FonteLicenca {
  const char *texto;                       // UTF-8, '\n' ends a paragraph, "" between two is a blank line
  uint8_t brancas;
};

struct IndiceLicenca {
  struct Linha {
    uint16_t inicio;                       // byte offset of the line in its source
    uint8_t fonte;                         // which source
    uint8_t tam;                           // bytes; 0 is a blank line
  };
  Linha linhas[LICENCA_INDICE_MAX];
  int n;                                   // lines indexed
  const char *fontes[LICENCA_FONTES_MAX];  // the sources, which must outlive the index
  int nFontes;
};

// Wraps the sources, in order, and fills the index; returns the lines. Each
// source is split at its newlines (a newline ends a paragraph: a trailing one
// does not make a blank line) and each paragraph wrapped with quebrar( ).
int indexarLicenca( IndiceLicenca &ix, const FonteLicenca *fontes, int n );

// The page `pagina` (held to the pages that exist) into vm: the lines, their
// count, and the page count and number for the pager.
void paginaDaLicenca( const IndiceLicenca &ix, int pagina, VmLicenca &vm );

// This panel's own fonts and icons (Bricolage Grotesque, GNU FreeFont, Lucide), appended to `fontes` after the licence text and the third-party list: a heading and
// one paragraph each, in the device's language (UA_TXT( ), a line each in the @ANGULO section of both packs). Returns the new count: all LICENCA_FONTES_ANGULO sources or, when they do not fit `max`, none. Call it where the
// text is indexed, not from an initialiser: the pack is loaded by then. tools/gen_notices.py --check holds its text to the rows of tools/third_party.toml.
int anexarFontesDoAngulo( FonteLicenca *fontes, int n, int max );

}  // namespace tela
}  // namespace ua
