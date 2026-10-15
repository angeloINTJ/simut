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

// Lines the index holds; the rest of a longer text is left out. The real text is 115 lines in the
// wired build and 125 in the radio one (indexed on the host, 2026-10-14): 192 leaves half again.
constexpr int LICENCA_INDICE_MAX = 192;
constexpr int LICENCA_FONTES_MAX = 8;

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

}  // namespace tela
}  // namespace ua
