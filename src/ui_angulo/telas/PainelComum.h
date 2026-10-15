#pragma once
// What the panel group's three screens share: a box, and the two ways a text
// that may not fit is cut. Header-only and in its own namespace, so that the
// glue can include every group's headers together without a clash.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Telas.h"

namespace ua {
namespace tela {
namespace painel {

inline Caixa16 caixa( int x, int y, int w, int h ) {
  return Caixa16{ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
}

// The UTF-8 text cut to `larg` px of ink: whole when it fits, else its longest
// prefix that fits with an ellipsis after it, at a character and not at a word
// (a sensor's name or a boot line is told apart by its tail as often as by its
// head). The text is at most 79 bytes; `cap` holds the cut text, at most 3
// bytes (the ellipsis) beyond the text it replaces: 36 for a name, 80 for a boot
// line.
inline void cortar( char *dst, int cap, const char *texto, Fonte f, int16_t larg ) {
  char orig[80];
  copiar( orig, texto, (int)sizeof orig );
  if ( larguraTinta( orig, f ) <= larg ) {
    copiar( dst, orig, cap );
    return;
  }
  int n = (int)strlen( orig );
  dst[0] = 0;
  while ( n > 0 ) {
    do n--; while ( n > 0 && ( (uint8_t)orig[n] & 0xC0 ) == 0x80 );
    int m = n;
    while ( m > 0 && orig[m - 1] == ' ' ) m--;
    if ( m + 4 > cap ) continue;
    memcpy( dst, orig, (size_t)m );
    memcpy( dst + m, "\xE2\x80\xA6", 4 );
    if ( larguraTinta( dst, f ) <= larg ) return;
  }
}

// "<nome><fim>" in `dst`, the name cut so that the whole sentence fits in
// `larg` px: the ending says what happened, the name is what gives way. The
// ending is measured together with the name, because a leading space has no
// ink: a width taken from the ending alone would come out short by its advance.
inline void nomeComFim( char *dst, int cap, const char *nome, const char *fim, Fonte f, int16_t larg ) {
  char cortado[40];
  int16_t orcamento = larg - larguraTinta( fim, f );
  for ( ;; ) {
    cortar( cortado, (int)sizeof cortado, nome, f, orcamento );
    snprintf( dst, (size_t)cap, "%s%s", cortado, fim );
    if ( larguraTinta( dst, f ) <= larg || orcamento <= 0 ) return;
    orcamento--;
  }
}

}  // namespace painel
}  // namespace tela
}  // namespace ua
