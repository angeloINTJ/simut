// What every screen of the functional build shares: targets, the bounded
// copy for the view models' arrays, and the status bar. See Telas.h.
#include "Telas.h"

#include <stdio.h>
#include <string.h>

namespace ua {

#ifdef UA_HOST
namespace {
int s_perdidos = 0;
}
int hostAlvosPerdidos( ) { return s_perdidos; }
void hostLimparAlvosPerdidos( ) { s_perdidos = 0; }
#endif

int addAlvo( Alvo *out, int n, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t tipo, int16_t indice ) {
  if ( n >= MAX_ALVOS ) {
#ifdef UA_HOST
    s_perdidos++;
#endif
    return n;
  }
  out[n] = Alvo{ x, y, w, h, tipo, indice };
  return n + 1;
}

namespace {

// Pixels from p to the span [a, a + w): 0 inside it, 1 for the first pixel outside, as the box's own test counts.
int32_t distancia( int32_t p, int32_t a, int32_t w ) { return p < a ? a - p : p >= a + w ? p - ( a + w - 1 ) : 0; }

}  // namespace

const Alvo *alvoEm( const Alvo *alvos, int n, int16_t x, int16_t y ) {
  // Inside a box the answer is the one it always was: the last listed, the one drawn on top.
  for ( int i = n - 1; i >= 0; i-- ) {
    const Alvo &a = alvos[i];
    if ( x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h ) return &a;
  }
  // Outside every box, the nearest one, if the finger can have meant it (FOLGA_TOQUE). A tie goes to the first
  // listed. A disabled control is listed too (inert), so a touch next to it finds it and not an enabled neighbour.
  const Alvo *perto = nullptr;
  int32_t melhor = 0;
  for ( int i = 0; i < n; i++ ) {
    const Alvo &a = alvos[i];
    if ( a.w <= 0 || a.h <= 0 ) continue;
    const int32_t dx = distancia( x, a.x, a.w ), dy = distancia( y, a.y, a.h );
    // Out of the slop on one axis already; also keeps the squares below far from wrapping around.
    if ( dx > FOLGA_TOQUE || dy > FOLGA_TOQUE ) continue;
    const int32_t d2 = dx * dx + dy * dy;
    if ( d2 > FOLGA_TOQUE * FOLGA_TOQUE ) continue;
    if ( !perto || d2 < melhor ) { perto = &a; melhor = d2; }
  }
  return perto;
}

const Alvo *alvoDoToque( const Alvo *alvos, int n, int16_t x, int16_t y ) {
  const Alvo *a = alvoEm( alvos, n, x, y );
  return a && a->tipo == ALVO_INERTE ? nullptr : a;
}

void copiar( char *dst, const char *src, int cap ) {
  if ( cap <= 0 ) return;
  if ( !src ) { dst[0] = 0; return; }
  int n = (int)strnlen( src, (size_t)cap );
  if ( n > cap - 1 ) {
    n = cap - 1;
    // Back off to the first byte of the sequence the cut fell into.
    while ( n > 0 && ( (uint8_t)src[n] & 0xC0 ) == 0x80 ) n--;
  }
  memcpy( dst, src, (size_t)n );
  dst[n] = 0;
}

namespace {

const char kMarca[] = "SIMUT";
const char kPrefixoWeb[] = "Web: ";

// The words of the notice, in the widest of its forms whose ink is at most `largura` px in the badge's font; false
// when none fits. A countdown goes whole or loses its number: "Silenciado · 12…" would say a wrong one. The web
// notice reads "Web: <name>", the form of the original top bar: "Web: " stays whole and the name gives way, cut at
// a character with "…" and never inside a UTF-8 sequence; the words are measured whole, because a space has no
// ink and the prefix alone would measure short by its advance. With no name, or no room for one letter of it, the
// word alone.
// Measured on the host kit, 2026-10-15 (9 pt, SANSB9): the badge's dot and padding take 26 px, so the 210 px that
// the cable icon alone leaves hold 184 px of words, the 166 px with "3k" waiting hold 140 and the 156 px with a
// three-character count hold 130. "Web: maria.aparecida" (15 characters, 183) fits the first whole;
// with "3k" a 15-character name keeps 8 letters (132), and a 31-character name keeps 13 beside the cable
// icon and 7 beside a three-character count; "Web: admin" (99) fits all three whole.
bool textoDoAviso( const VmStatus &s, int16_t largura, char *out, int cap ) {
  if ( s.aviso == Aviso::SILENCIADO ) {
    snprintf( out, (size_t)cap, "Silenciado \xC2\xB7 %u s", (unsigned)s.avisoSeg );
    if ( larguraTinta( out, tipo::rotulo ) <= largura ) return true;
    snprintf( out, (size_t)cap, "Silenciado" );
    return larguraTinta( out, tipo::rotulo ) <= largura;
  }
  char nome[sizeof s.avisoNome];
  copiar( nome, s.avisoNome, (int)sizeof nome );
  snprintf( out, (size_t)cap, "%s%s", kPrefixoWeb, nome );
  if ( nome[0] && larguraTinta( out, tipo::rotulo ) <= largura ) return true;
  int n = (int)strlen( nome );
  while ( n > 1 ) {
    do n--; while ( n > 0 && ( (uint8_t)nome[n] & 0xC0 ) == 0x80 );
    int m = n;
    while ( m > 0 && nome[m - 1] == ' ' ) m--;
    if ( m == 0 ) break;
    snprintf( out, (size_t)cap, "%s%.*s\xE2\x80\xA6", kPrefixoWeb, m, nome );
    if ( larguraTinta( out, tipo::rotulo ) <= largura ) return true;
  }
  snprintf( out, (size_t)cap, "Web" );
  return larguraTinta( out, tipo::rotulo ) <= largura;
}

}  // namespace

void desenharStatus( Pintor &p, const VmStatus &s ) {
  const Tema &T = p.t( );
  // The brand stays when a badge takes the clock's place: the plan's renderer
  // paints the badge's background over it (telas.js barraStatus), which the
  // kit keeps for the references; on the device the brand is the anchor, and
  // the badge is placed in the room it leaves.
  textoCentrado( p, kMarca, PAD, 0, H_STATUS, tipo::subtitulo, T.tinta, 'l' );
  // The icons on the right, right to left: the network, then what telemetry
  // still has to send. Laid out before the badge, which has to stay clear of
  // them: `esq` is where their ink starts.
  int16_t x = 320 - PAD - 16;
  IconeNome rede = IconeNome::NENHUM;
  uint16_t corRede = T.tinta2;
  switch ( s.rede ) {
    case Rede::CABO: rede = IconeNome::ETHERNET_PORT; break;
    case Rede::CABO_SEM_LINK: rede = IconeNome::ETHERNET_PORT; corRede = T.perigo; break;
    case Rede::WIFI: rede = IconeNome::WIFI; break;
    case Rede::WIFI_FRACO: rede = IconeNome::WIFI; corRede = T.alerta; break;
    case Rede::WIFI_SEM: rede = IconeNome::WIFI_OFF; corRede = T.perigo; break;
    case Rede::NENHUMA: break;
  }
  const int16_t xRede = x;
  const bool temRede = rede != IconeNome::NENHUM;
  int16_t esq = temRede ? xRede : 320 - PAD;
  if ( temRede ) x -= ESP1;
  char n[8] = "";
  if ( s.pendentes ) {
    if ( s.pendentes < 1000 ) {
      int v = s.pendentes, i = 0;
      char tmp[8];
      do { tmp[i++] = (char)( '0' + v % 10 ); v /= 10; } while ( v );
      for ( int k = 0; k < i; k++ ) n[k] = tmp[i - 1 - k];
      n[i] = 0;
    } else {
      // pendingLabel( )'s rule: above 999 the count is said in thousands.
      int v = s.pendentes / 1000, i = 0;
      char tmp[8];
      do { tmp[i++] = (char)( '0' + v % 10 ); v /= 10; } while ( v );
      for ( int k = 0; k < i; k++ ) n[k] = tmp[i - 1 - k];
      n[i++] = 'k';
      n[i] = 0;
    }
    x -= 16;
    esq = x - ESP1 - larguraTinta( n, tipo::dado );
  }
  bool aviso = false;
  if ( s.aviso != Aviso::NENHUM ) {
    // Free room: from ESP2 after the brand's ink to ESP2 before the icons' (the panel's right margin when there
    // are none).
    const int16_t livreE = PAD + larguraTinta( kMarca, tipo::subtitulo ) + ESP2;
    const int16_t livreD = temRede || s.pendentes ? esq - ESP2 : esq;
    char texto[64];
    if ( textoDoAviso( s, (int16_t)( livreD - livreE - larguraSelo( "" ) ), texto, sizeof texto ) ) {
      const int16_t w = larguraSelo( texto );
      int16_t bx = rd2( 320 - w );
      if ( bx < livreE || bx + w > livreD ) bx = (int16_t)( livreE + rd2( livreD - livreE - w ) );
      selo( p, bx, 4, 20, texto, s.aviso == Aviso::SILENCIADO ? Tom::ALERTA : Tom::NEUTRO );
      aviso = true;
    }
  }
  if ( !aviso && s.hora[0] ) {
    // A clock nobody confirmed is said in the alert colour, as the old top
    // bar did with its " ? " (the records carry that time).
    textoCentrado( p, s.hora, 160, 0, H_STATUS, tipo::botao, s.horaProvisoria ? T.alerta : T.tinta, 'c' );
  }
  if ( temRede ) p.icone( rede, 16, xRede, 6, corRede );
  if ( s.pendentes ) {
    const uint16_t cor = s.envio == 2 ? T.perigo : T.tinta2;
    p.icone( IconeNome::UPLOAD, 16, x, 6, cor );
    textoCentrado( p, n, x - ESP1, 0, H_STATUS, tipo::dado, cor, 'r' );
  }
  p.hline( 0, H_STATUS, 320, T.linha );
}

}  // namespace ua

namespace ua {

namespace {

// Bytes of the UTF-8 sequence that starts at s (1 for a stray byte).
int bytesDe( const char *s ) {
  const uint8_t b = (uint8_t)*s;
  const int n = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
  int k = 1;
  while ( k < n && s[k] && ( (uint8_t)s[k] & 0xC0 ) == 0x80 ) k++;
  return k;
}

// Ink width of the first `n` bytes of s.
int16_t larguraDe( const char *s, int n, Fonte f, char *tmp, int cap ) {
  if ( n > cap - 1 ) n = cap - 1;
  memcpy( tmp, s, (size_t)n );
  tmp[n] = 0;
  return larguraTinta( tmp, f );
}

}  // namespace

int quebrar( const char *texto, Fonte f, int16_t largura, char *linhas, int cap, int max ) {
  if ( !texto || max <= 0 || cap < 2 ) return 0;
  char tmp[256];
  int usadas = 0;
  const char *s = texto;
  while ( *s == ' ' ) s++;
  while ( *s && usadas < max ) {
    // The longest prefix that fits, ending at a space when one does.
    int fim = 0, ultimoEspaco = -1;
    int k = 0;
    while ( s[k] ) {
      const int b = s[k] == ' ' ? 1 : bytesDe( s + k );
      if ( larguraDe( s, k + b, f, tmp, sizeof tmp ) > largura && k > 0 ) break;
      if ( s[k] == ' ' ) ultimoEspaco = k;
      k += b;
      fim = k;
    }
    int corte = fim;
    if ( s[fim] && ultimoEspaco > 0 ) corte = ultimoEspaco;
    char *linha = linhas + usadas * cap;
    int n = corte < cap - 1 ? corte : cap - 1;
    if ( n < 0 ) n = 0;
    memcpy( linha, s, (size_t)n );
    linha[n] = 0;
    while ( n > 0 && linha[n - 1] == ' ' ) linha[--n] = 0;
    usadas++;
    s += corte;
    while ( *s == ' ' ) s++;
    if ( *s && usadas == max ) {
      // Out of lines: the last one says that the text goes on.
      static const char reticencias[] = "\xE2\x80\xA6";
      while ( n > 0 ) {
        char prova[256];
        snprintf( prova, sizeof prova, "%s%s", linha, reticencias );
        if ( larguraTinta( prova, f ) <= largura ) break;
        do n--; while ( n > 0 && ( (uint8_t)linha[n] & 0xC0 ) == 0x80 );
        linha[n] = 0;
      }
      if ( n + 3 < cap ) memcpy( linha + n, reticencias, 4 );
    }
  }
  return usadas;
}

}  // namespace ua
