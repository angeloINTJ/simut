// The licence text's index. See MenusLicenca.h.
#include "MenusLicenca.h"

#include <string.h>

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

namespace {

// quebrar( ) wraps a NUL-terminated string, and a paragraph of the sources ends
// at a newline inside a longer one: it is copied here first. The longest of the
// real text is 460 bytes (HelpLicenseEN.h, measured 2026-10-14); a longer
// paragraph is wrapped in pieces cut at a space.
constexpr uint32_t PAR_BYTES = 512;
// Lines asked of quebrar( ) at a time. Its last line is the one that ends in
// "…" when the text goes on, so only the first LINHAS_PEDIDAS - 1 can be kept.
constexpr int LINHAS_PEDIDAS = 4;

// A line of a source into the view model's line. The kit's fonts have no dash
// (U+2013, U+2014) and the text has one ("original — that is the version"): it
// is drawn as a hyphen, as the old screen's ASCII fold drew it.
void copiarLinha( char *dst, const char *src, uint32_t tam ) {
  uint32_t o = 0;
  for ( uint32_t i = 0; i < tam; ) {
    const bool traco = i + 2 < tam && (uint8_t)src[i] == 0xE2 && (uint8_t)src[i + 1] == 0x80 &&
                       ( (uint8_t)src[i + 2] == 0x93 || (uint8_t)src[i + 2] == 0x94 );
    if ( traco ) { dst[o++] = '-'; i += 3; }
    else dst[o++] = src[i++];
  }
  dst[o] = 0;
}

void anexar( IndiceLicenca &ix, int fonte, uint32_t inicio, uint32_t tam ) {
  if ( ix.n >= LICENCA_INDICE_MAX || inicio > 0xFFFFu || tam > 0xFFu ) return;
  IndiceLicenca::Linha &l = ix.linhas[ix.n++];
  l.inicio = (uint16_t)inicio;
  l.fonte = (uint8_t)fonte;
  l.tam = (uint8_t)tam;
}

// One paragraph: [off, off + len) of source `fonte`.
void paragrafo( IndiceLicenca &ix, int fonte, uint32_t off, uint32_t len ) {
  static char par[PAR_BYTES];
  static char lin[LINHAS_PEDIDAS][LICENCA_BYTES];
  if ( len == 0 ) { anexar( ix, fonte, 0, 0 ); return; }   // a blank line
  const char *p = ix.fontes[fonte] + off;
  uint32_t pos = 0;
  while ( pos < len && ix.n < LICENCA_INDICE_MAX ) {
    // The rest of the paragraph, or a window of it when it does not fit the buffer.
    uint32_t w = len - pos;
    bool cortada = false;
    if ( w > PAR_BYTES - 1 ) {
      cortada = true;
      w = PAR_BYTES - 1;
      uint32_t e = w;
      while ( e > 0 && p[pos + e] != ' ' ) e--;
      if ( e > 0 ) w = e;
      else while ( w > 0 && ( (uint8_t)p[pos + w] & 0xC0 ) == 0x80 ) w--;   // no space: not inside a UTF-8 sequence
    }
    memcpy( par, p + pos, w );
    par[w] = 0;
    const int usadas = quebrar( par, tipo::apoio, LICENCA_LARGURA, &lin[0][0], LICENCA_BYTES, LINHAS_PEDIDAS );
    if ( usadas == 0 ) break;                   // nothing but spaces
    // Every line when the window reaches the paragraph's end and quebrar( ) had
    // a line to spare; else all but the last, which is wrapped again from its
    // start with the text that follows.
    const bool acabou = !cortada && usadas < LINHAS_PEDIDAS;
    const int validas = acabou ? usadas : ( usadas > LINHAS_PEDIDAS - 1 ? LINHAS_PEDIDAS - 1 : usadas );
    // Where each line starts: quebrar( ) skips the spaces between lines and
    // trims the ones before a break, so the text of line i is found by walking
    // the window past its spaces.
    uint32_t c = 0;
    for ( int i = 0; i < validas; i++ ) {
      while ( c < w && par[c] == ' ' ) c++;
      const uint32_t tam = (uint32_t)strlen( lin[i] );
      anexar( ix, fonte, off + pos + c, tam );
      c += tam;
    }
    if ( acabou ) break;
    pos += c;
  }
}

}  // namespace

int indexarLicenca( IndiceLicenca &ix, const FonteLicenca *fontes, int n ) {
  ix.n = 0;
  ix.nFontes = n < LICENCA_FONTES_MAX ? n : LICENCA_FONTES_MAX;
  for ( int f = 0; f < ix.nFontes; f++ ) ix.fontes[f] = fontes[f].texto;
  for ( int f = 0; f < ix.nFontes && ix.n < LICENCA_INDICE_MAX; f++ ) {
    const char *s = fontes[f].texto;
    uint32_t pos = 0;
    while ( s[pos] && ix.n < LICENCA_INDICE_MAX ) {
      uint32_t fim = pos;
      while ( s[fim] && s[fim] != '\n' ) fim++;
      paragrafo( ix, f, pos, fim - pos );
      pos = s[fim] ? fim + 1 : fim;
    }
    for ( int b = 0; b < fontes[f].brancas; b++ ) anexar( ix, f, 0, 0 );
  }
  return ix.n;
}

void paginaDaLicenca( const IndiceLicenca &ix, int pagina, VmLicenca &vm ) {
  const int paginas = ix.n ? ( ix.n + LICENCA_LINHAS - 1 ) / LICENCA_LINHAS : 1;
  if ( pagina >= paginas ) pagina = paginas - 1;
  if ( pagina < 0 ) pagina = 0;
  vm.pagina = (uint16_t)pagina;
  vm.paginas = (uint16_t)paginas;
  const int primeira = pagina * LICENCA_LINHAS;
  int n = ix.n - primeira;
  if ( n > LICENCA_LINHAS ) n = LICENCA_LINHAS;
  if ( n < 0 ) n = 0;
  for ( int i = 0; i < n; i++ ) {
    const IndiceLicenca::Linha &l = ix.linhas[primeira + i];
    copiarLinha( vm.linhas[i], ix.fontes[l.fonte] + l.inicio, l.tam );
  }
  vm.n = (uint8_t)n;
}

// This panel's own fonts and icons, after the third-party list (S10c C4, 2026-10-30). tools/third_party.toml has them as the rows marked
// `ui = "angulo"`, and `tools/gen_notices.py --check` holds this text to them: the name, and the holder and licence as "<holder>, <licence>.",
// must read here as the table has them. The words around the proper names are the device's language; the names, holders and licences are not
// translated. The English comes back for a text the pack lacks.
int anexarFontesDoAngulo( FonteLicenca *fontes, int n, int max ) {
  const FonteLicenca extra[] = {
    { UA_TXT( "--- Fonts and icons of this panel ---" ), 1 },
    { UA_TXT( "Bricolage Grotesque, in the titles: The Bricolage Grotesque Project Authors, SIL Open Font License 1.1." ), 0 },
    { UA_TXT( "GNU FreeFont (FreeSans, FreeMono Bold), in the text and the figures: GNU FreeFont contributors, GPL-3.0-or-later with the font exception." ), 0 },
    { UA_TXT( "Lucide, in the icons: Lucide Contributors, ISC." ), 0 },
  };
  constexpr int N = sizeof extra / sizeof extra[0];
  static_assert( N == LICENCA_FONTES_ANGULO );   // the sources above and the count MenusLicenca.h states (and checks against the maximum) agree
  if ( n + N > max ) return n;   // all or none, never a list cut in the middle; not reached while the static_assert of the header holds for the caller's array
  for ( int i = 0; i < N; i++ ) fontes[n++] = extra[i];
  return n;
}

}  // namespace tela
}  // namespace ua
