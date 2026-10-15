// The Ângulo kit (see Angulo.h). Every function here is a port of the TFT
// plan's renderer, named after it in the comment; tools/tft_ui/host_ui.py
// holds the screens drawn with it to the plan's references, pixel for pixel.

#include "Angulo.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// The delivery's headers define their fonts, icons and tokens as namespace-
// scope consts (internal linkage): this translation unit is the only one that
// includes them.
namespace entrega {
#include "kit/fontes/SANS7.h"
#include "kit/fontes/SANSB7.h"
#include "kit/fontes/SANS9.h"
#include "kit/fontes/SANSB9.h"
#include "kit/fontes/MONO9.h"
#include "kit/fontes/MONO12.h"
#include "kit/fontes/DISP9.h"
#include "kit/fontes/DISP12.h"
#include "kit/fontes/DISP18.h"
#include "kit/fontes/DISP24.h"
#include "kit/fontes/DISP32.h"
#include "kit/fontes/DISP24_MARCA.h"
#include "kit/icones/icones.h"
#include "kit/angulo_tokens.h"
}  // namespace entrega

#include "AnguloIcones.h"

namespace ua {

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

namespace {

Tema deEntrega( const entrega::Tema &e, bool escuro ) {
  return Tema{ e.fundo, e.superficie, e.superficie2, e.tinta, e.tinta2, e.linha, e.linhaForte, e.acento,
               e.acentoForte, e.acentoTinta, e.positivo, e.positivoSuave, e.alerta, e.alertaSuave, e.perigo,
               e.perigoSuave, e.perigoTinta, escuro };
}

const Tema kClaro = deEntrega( entrega::TEMA_CLARO, false );
const Tema kEscuro = deEntrega( entrega::TEMA_ESCURO, true );
const Tema kClaroDesab = deEntrega( entrega::TEMA_CLARO_DESAB, false );
const Tema kEscuroDesab = deEntrega( entrega::TEMA_ESCURO_DESAB, true );

}  // namespace

const Tema &temaClaro( ) { return kClaro; }
const Tema &temaEscuro( ) { return kEscuro; }

uint16_t desab( const Tema &t, uint16_t cor ) {
  // The 17 roles are distinct within a theme (angulo_tokens.h, 2026-10-14), so
  // the colour names its role. They are read from the object's bytes, role by
  // role: reading them as an array from &t.fundo indexes past one object, which is
  // undefined behaviour (cppcheck objectIndex, the CI's static analysis,
  // 2026-10-15); a table of member pointers would cost 72 B more.
  static_assert( offsetof( Tema, fundo ) == 0 && offsetof( Tema, escuro ) == 17 * sizeof( uint16_t ),
                 "Tema must start with its 17 colour roles and nothing else before escuro" );
  const Tema &d = t.escuro ? kEscuroDesab : kClaroDesab;
  const unsigned char *papeis = reinterpret_cast<const unsigned char *>( &t );
  const unsigned char *mist = reinterpret_cast<const unsigned char *>( &d );
  for ( size_t o = 0; o < offsetof( Tema, escuro ); o += sizeof( uint16_t ) ) {
    uint16_t v;
    memcpy( &v, papeis + o, sizeof v );
    if ( v == cor ) {
      memcpy( &v, mist + o, sizeof v );
      return v;
    }
  }
  return cor;
}

int16_t rd2( int32_t v ) {
  // floor((v + 1) / 2), with floor division: Math.round(v / 2).
  const int32_t n = v + 1;
  return (int16_t)( n >= 0 ? n / 2 : -( ( -n + 1 ) / 2 ) );
}

int16_t rdiv( int32_t n, int32_t d ) {
  // Math.round(n / d) = floor((2n + d) / (2d)), floor division.
  const int32_t a = 2 * n + d, b = 2 * d;
  return (int16_t)( a >= 0 ? a / b : -( ( -a + b - 1 ) / b ) );
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

namespace {

const GFXfont *const kFontes[] = {
    &entrega::SANS7, &entrega::SANSB7, &entrega::SANS9, &entrega::SANSB9, &entrega::MONO9, &entrega::MONO12,
    &entrega::DISP9, &entrega::DISP12, &entrega::DISP18, &entrega::DISP24, &entrega::DISP32, &entrega::DISP24_MARCA,
};

uint32_t s_faltando = 0;

// Next code point of a UTF-8 string; advances *s. Malformed bytes come back as
// U+FFFD, which no font holds.
uint32_t proximo( const char *&s ) {
  const uint8_t c = (uint8_t)*s++;
  if ( c < 0x80 ) return c;
  int extra = ( c >= 0xF0 ) ? 3 : ( c >= 0xE0 ) ? 2 : ( c >= 0xC0 ) ? 1 : -1;
  if ( extra < 0 ) return 0xFFFD;
  uint32_t cp = c & ( 0x3F >> extra );
  for ( int k = 0; k < extra; k++ ) {
    const uint8_t b = (uint8_t)*s;
    if ( ( b & 0xC0 ) != 0x80 ) return 0xFFFD;
    cp = ( cp << 6 ) | ( b & 0x3F );
    s++;
  }
  return cp;
}

// GFX code of a code point: ASCII as is, the EXTRA table above it; 0 if none.
uint8_t codigo( uint32_t cp ) {
  if ( cp >= 32 && cp <= 126 ) return (uint8_t)cp;
  for ( uint8_t i = 0; i < sizeof( meta::kExtra ) / sizeof( meta::kExtra[0] ); i++ )
    if ( meta::kExtra[i] == cp ) return (uint8_t)( 0x7F + i );
  return 0;
}

// The GFX font that draws code c for scene font f: its own, or for DISP24 the
// brand's (the renderer's fallback: DISP24 holds only the digits).
const GFXfont *fonteDe( Fonte f, uint8_t c ) {
  const GFXfont *g = kFontes[(int)f];
  if ( c >= g->first && c <= g->last ) return g;
  if ( f == Fonte::DISP24 ) {
    const GFXfont *m = kFontes[(int)Fonte::MARCA];
    if ( c >= m->first && c <= m->last ) return m;
  }
  return nullptr;
}

}  // namespace

uint32_t glifosFaltando( ) { return s_faltando; }

Caixa medir( const char *s, Fonte f ) {
  int32_t pen = 0, l = INT32_MAX, r = INT32_MIN, t = INT32_MAX, b = INT32_MIN;
  while ( *s ) {
    const uint8_t c = codigo( proximo( s ) );
    const GFXfont *g = c ? fonteDe( f, c ) : nullptr;
    if ( !g ) continue;
    const GFXglyph &gl = g->glyph[c - g->first];
    if ( gl.width > 0 && gl.height > 0 ) {
      if ( pen + gl.xOffset < l ) l = pen + gl.xOffset;
      if ( pen + gl.xOffset + gl.width > r ) r = pen + gl.xOffset + gl.width;
      if ( gl.yOffset < t ) t = gl.yOffset;
      if ( gl.yOffset + gl.height > b ) b = gl.yOffset + gl.height;
    }
    pen += gl.xAdvance;
  }
  if ( r == INT32_MIN ) return Caixa{ 0, 0, 0, 0, true };
  return Caixa{ (int16_t)l, (int16_t)r, (int16_t)t, (int16_t)b, false };
}

int16_t larguraTinta( const char *s, Fonte f ) {
  const Caixa c = medir( s, f );
  return c.inkR - c.inkL;
}

int16_t alturaCaixaAlta( Fonte f ) { return meta::kCapX[f == Fonte::MARCA ? (int)Fonte::DISP24 : (int)f][0]; }
int16_t alturaX( Fonte f ) { return meta::kCapX[f == Fonte::MARCA ? (int)Fonte::DISP24 : (int)f][1]; }

/* ------------------------------------------------------------------ */
/* Pintor                                                              */
/* ------------------------------------------------------------------ */

void Pintor::clear( uint16_t c ) {
  // fillRect, not fillScreen: TftWithOffset::fillScreen( ) also paints the
  // alignment offset's margins black. On a canvas the two are equal.
  g_.fillRect( 0, 0, g_.width( ), g_.height( ), c );
}

void Pintor::pixel( int16_t x, int16_t y, uint16_t c ) {
  if ( x < 0 || y < 0 || x >= g_.width( ) || y >= g_.height( ) ) return;
  g_.drawPixel( x, y, c );
}

void Pintor::fillRect( int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c ) {
  // Surface.fillRect: clipped, and nothing for a width or height <= 0
  // (Adafruit_SPITFT::fillRect would flip a negative one instead).
  int16_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int32_t x1 = (int32_t)x + w, y1 = (int32_t)y + h;
  if ( x1 > g_.width( ) ) x1 = g_.width( );
  if ( y1 > g_.height( ) ) y1 = g_.height( );
  if ( x1 <= x0 || y1 <= y0 ) return;
  g_.fillRect( x0, y0, (int16_t)( x1 - x0 ), (int16_t)( y1 - y0 ), c );
}

void Pintor::rect( int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c ) {
  if ( w <= 0 || h <= 0 ) return;
  g_.drawRect( x, y, w, h, c );
}

void Pintor::line( int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c ) { g_.drawLine( x0, y0, x1, y1, c ); }

namespace {

// fg over bg, a sixteenths of fg: each channel widened to 8 bits (high bits
// replicated, as the panel shows it), mixed, rounded, narrowed back.
uint16_t misturar16( uint16_t fg, uint16_t bg, uint8_t a ) {
  if ( a >= 16 ) return fg;
  if ( a == 0 ) return bg;
  auto ex5 = []( uint16_t v ) { return (uint16_t)( ( v << 3 ) | ( v >> 2 ) ); };
  auto ex6 = []( uint16_t v ) { return (uint16_t)( ( v << 2 ) | ( v >> 4 ) ); };
  const uint16_t r = (uint16_t)( ( ex5( fg >> 11 ) * a + ex5( bg >> 11 ) * ( 16 - a ) + 8 ) / 16 );
  const uint16_t g = (uint16_t)( ( ex6( ( fg >> 5 ) & 63 ) * a + ex6( ( bg >> 5 ) & 63 ) * ( 16 - a ) + 8 ) / 16 );
  const uint16_t b = (uint16_t)( ( ex5( fg & 31 ) * a + ex5( bg & 31 ) * ( 16 - a ) + 8 ) / 16 );
  return (uint16_t)( ( ( r & 0xF8 ) << 8 ) | ( ( g & 0xFC ) << 3 ) | ( b >> 3 ) );
}

// Sixteenths of a pixel inside the disc of squared radius out2 and outside the
// one of squared radius in2 (in2 < 0: a whole disc), everything in eighths of a
// pixel so that it stays integer: (dx8, dy8) is the pixel's corner nearest the
// centre's opposite side, and the 4 x 4 samples sit at its (2k + 1) / 8 points.
uint8_t cobertura( int32_t dx8, int32_t dy8, int32_t out2, int32_t in2 ) {
  uint8_t n = 0;
  for ( int32_t l = 0; l < 4; l++ ) {
    const int32_t dy = dy8 + 2 * l + 1;
    for ( int32_t k = 0; k < 4; k++ ) {
      const int32_t dx = dx8 + 2 * k + 1;
      const int32_t d2 = dx * dx + dy * dy;
      if ( d2 <= out2 && d2 > in2 ) n++;
    }
  }
  return n;
}

}  // namespace

void Pintor::misturar( int16_t x, int16_t y, uint16_t c, uint8_t a ) {
  if ( a == 0 || x < 0 || y < 0 || x >= g_.width( ) || y >= g_.height( ) ) return;
  g_.drawPixel( x, y, a >= 16 ? c : misturar16( c, l_.ler( x, y ), a ) );
}

// The four r x r corner squares of a box at [x, x + w) by [y, y + h): each pixel
// mixed against the quarter disc whose centre is r in from both outer edges, and
// outside the disc of squared radius in2 (an outline; in2 < 0 for a fill). Rows
// outside the strip being drawn (janela) are skipped: they are not kept.
void Pintor::cantos( int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c, int32_t in2 ) {
  const int32_t out = 8 * r;
  for ( int16_t j = 0; j < r; j++ ) {
    const int16_t ya = y + j, yb = y + h - 1 - j;
    const bool cima = ya >= jy0_ && ya < jy1_, baixo = yb >= jy0_ && yb < jy1_;
    if ( !cima && !baixo ) continue;
    for ( int16_t i = 0; i < r; i++ ) {
      const uint8_t a = cobertura( 8 * ( i - r ), 8 * ( j - r ), out * out, in2 );
      if ( cima ) { misturar( x + i, ya, c, a ); misturar( x + w - 1 - i, ya, c, a ); }
      if ( baixo ) { misturar( x + i, yb, c, a ); misturar( x + w - 1 - i, yb, c, a ); }
    }
  }
}

// A circle as Adafruit GFX places it (centre pixel (x, y), radius r: the pixels
// from x - r to x + r), drawn as the disc of radius r + 0.5 around that pixel's
// centre; the outline is the ring between r - 0.5 and r + 0.5 (in2 >= 0).
void Pintor::disco( int16_t x, int16_t y, int16_t r, uint16_t c, int32_t in2 ) {
  if ( r < 0 || fora( y - r, 2 * r + 1 ) ) return;
  const int32_t out = 8 * r + 4;
  for ( int16_t j = -r; j <= r; j++ ) {
    if ( y + j < jy0_ || y + j >= jy1_ ) continue;
    for ( int16_t i = -r; i <= r; i++ ) misturar( x + i, y + j, c, cobertura( 8 * i - 4, 8 * j - 4, out * out, in2 ) );
  }
}

void Pintor::circle( int16_t x, int16_t y, int16_t r, uint16_t c ) {
  const int32_t in = 8 * r - 4;
  disco( x, y, r, c, in > 0 ? in * in : -1 );
}

void Pintor::fillCircle( int16_t x, int16_t y, int16_t r, uint16_t c ) { disco( x, y, r, c, -1 ); }

// A rounded box as Adafruit GFX places it ([x, x + w) by [y, y + h), corners of
// radius r, at most half the shorter side): the straight parts as before, the
// corners smoothed (cantos). The outline is 1 px: the ring between the corner's
// disc and the one of radius r - 1 around the same centre, the box inset by 1.
void Pintor::roundRect( int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c ) {
  if ( w <= 0 || h <= 0 || fora( y, h ) ) return;
  const int16_t m = ( w < h ? w : h ) / 2;
  if ( r > m ) r = m;
  hline( x + r, y, w - 2 * r, c );
  hline( x + r, y + h - 1, w - 2 * r, c );
  vline( x, y + r, h - 2 * r, c );
  vline( x + w - 1, y + r, h - 2 * r, c );
  const int32_t in = 8 * ( r - 1 );
  cantos( x, y, w, h, r, c, in > 0 ? in * in : -1 );
}

void Pintor::fillRoundRect( int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c ) {
  if ( w <= 0 || h <= 0 || fora( y, h ) ) return;
  const int16_t m = ( w < h ? w : h ) / 2;
  if ( r > m ) r = m;
  fillRect( x + r, y, w - 2 * r, h, c );
  fillRect( x, y + r, r, h - 2 * r, c );
  fillRect( x + w - r, y + r, r, h - 2 * r, c );
  cantos( x, y, w, h, r, c, -1 );
}

bool temIcone( IconeNome n, int16_t tam ) {
  for ( const icones::Reg &r : icones::kIcones )
    if ( r.nome == n && r.tam == tam ) return true;
  return false;
}

void Pintor::icone( IconeNome n, int16_t tam, int16_t x, int16_t y, uint16_t c ) {
  for ( const icones::Reg &r : icones::kIcones ) {
    if ( r.nome == n && r.tam == tam ) {
      if ( !fora( y, tam ) ) g_.drawBitmap( x, y, r.bits, tam, tam, c );
      return;
    }
  }
  s_faltando++;
}

#ifdef UA_HOST
namespace {
TextoDesenhado s_textos[256];
int s_nTextos = 0;
void hostAnotarTexto( const char *s, int16_t x0, int16_t x1, int16_t y0, int16_t y1 ) {
  if ( s_nTextos >= (int)( sizeof s_textos / sizeof s_textos[0] ) ) return;
  TextoDesenhado &t = s_textos[s_nTextos++];
  t.x0 = x0; t.x1 = x1; t.y0 = y0; t.y1 = y1;
  int i = 0;
  for ( ; i < (int)sizeof t.texto - 1 && s[i]; i++ ) t.texto[i] = s[i];
  t.texto[i] = 0;
}
}  // namespace
void hostLimparTextos( ) { s_nTextos = 0; }
int hostTextos( const TextoDesenhado **lista ) { *lista = s_textos; return s_nTextos; }
#endif

Tinta Pintor::texto( const char *s, int16_t x, int16_t y, Fonte f, uint16_t cor, char alinha ) {
  const Caixa cx = medir( s, f );
#ifdef UA_HOST
  const char *const s0 = s;
#endif
  if ( cx.vazio ) return Tinta{ x, x };
  int16_t ox;
  if ( alinha == 'c' ) ox = x - rd2( cx.inkL + cx.inkR );
  else if ( alinha == 'r' ) ox = x - cx.inkR;
  else ox = x - cx.inkL;
  if ( fora( (int16_t)( y + cx.inkT ), (int16_t)( cx.inkB - cx.inkT ) ) ) {
#ifdef UA_HOST
    hostAnotarTexto( s0, (int16_t)( ox + cx.inkL ), (int16_t)( ox + cx.inkR ), (int16_t)( y + cx.inkT ),
                     (int16_t)( y + cx.inkB ) );
#endif
    return Tinta{ (int16_t)( ox + cx.inkL ), (int16_t)( ox + cx.inkR ) };
  }
  // Adafruit_GFX::write( ) draws each glyph at the cursor and advances it by
  // xAdvance; a glyph that would cross the right edge is clipped, not wrapped.
  g_.setTextWrap( false );
  g_.setTextSize( 1 );
  g_.setTextColor( cor );
  const GFXfont *atual = nullptr;
  int16_t cursor = ox;
  while ( *s ) {
    const uint32_t cp = proximo( s );
    const uint8_t c = codigo( cp );
    const GFXfont *gf = c ? fonteDe( f, c ) : nullptr;
    if ( !gf ) { s_faltando++; continue; }
    if ( gf != atual ) { g_.setFont( gf ); atual = gf; }
    g_.setCursor( cursor, y );
    g_.write( c );
    cursor += gf->glyph[c - gf->first].xAdvance;
  }
#ifdef UA_HOST
  hostAnotarTexto( s0, (int16_t)( ox + cx.inkL ), (int16_t)( ox + cx.inkR ), (int16_t)( y + cx.inkT ),
                   (int16_t)( y + cx.inkB ) );
#endif
  return Tinta{ (int16_t)( ox + cx.inkL ), (int16_t)( ox + cx.inkR ) };
}

Tinta textoCentrado( Pintor &p, const char *s, int16_t x, int16_t y, int16_t h, Fonte f, uint16_t cor, char alinha,
                     bool alturaDeX ) {
  const int16_t alt = alturaDeX ? alturaX( f ) : alturaCaixaAlta( f );
  const int16_t base = y + rd2( h + alt );
  return p.texto( s, x, base, f, cor, alinha );
}

/* ------------------------------------------------------------------ */
/* Components                                                          */
/* ------------------------------------------------------------------ */

void botao( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const char *rotulo, Botao tipo, bool des,
            IconeNome icone ) {
  const Tema &T = p.t( );
  bool temFundo = false, temBorda = false;
  uint16_t fundo = 0, borda = 0, texto = T.tinta;
  switch ( tipo ) {
    case Botao::PRIMARIO: temFundo = true; fundo = T.acento; texto = T.acentoTinta; break;
    case Botao::SECUNDARIO: temFundo = true; fundo = T.superficie; temBorda = true; borda = T.linhaForte; texto = T.tinta; break;
    case Botao::DISCRETO: texto = T.acento; break;
    case Botao::DESTRUTIVO: temFundo = true; fundo = T.perigo; texto = T.perigoTinta; break;
  }
  if ( des ) { fundo = desab( T, fundo ); borda = desab( T, borda ); texto = desab( T, texto ); }
  if ( temFundo ) p.fillRoundRect( x, y, w, h, RAIO_CONTROLE, fundo );
  if ( temBorda ) p.roundRect( x, y, w, h, RAIO_CONTROLE, borda );
  if ( icone != IconeNome::NENHUM ) {
    // 20 px icon in a tall button, 16 in the compact ones (title bar).
    const int16_t iw = h >= 36 ? 20 : 16;
    const int16_t tw = larguraTinta( rotulo, tipo::botao );
    const int16_t total = iw + ESP1 + tw;
    const int16_t ix = x + rd2( w - total );
    p.icone( icone, iw, ix, y + rd2( h - iw ), texto );
    textoCentrado( p, rotulo, ix + iw + ESP1, y, h, tipo::botao, texto, 'l' );
  } else {
    textoCentrado( p, rotulo, x + rd2( w ), y, h, tipo::botao, texto, 'c' );
  }
}

void tecla( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const TeclaOpts &o ) {
  const Tema &T = p.t( );
  uint16_t fundo = T.superficie, borda = T.linha, cor = T.tinta;
  bool temBorda = true;
  if ( o.tipo == TeclaTipo::FUNCAO ) fundo = T.superficie2;
  if ( o.tipo == TeclaTipo::ESPACO ) cor = T.tinta2;
  if ( o.tipo == TeclaTipo::PRIMARIA ) { fundo = T.acento; temBorda = false; cor = T.acentoTinta; }
  if ( o.estado == TeclaEstado::PRESSIONADA ) {
    fundo = T.superficie2; temBorda = true; borda = T.linhaForte;
    if ( o.tipo == TeclaTipo::PRIMARIA ) fundo = T.acentoForte;
  }
  if ( o.estado == TeclaEstado::ATIVA ) { temBorda = true; borda = T.acento; cor = T.acento; }
  if ( o.estado == TeclaEstado::TRAVADA ) { fundo = T.acento; temBorda = false; cor = T.acentoTinta; }
  if ( o.estado == TeclaEstado::DESAB ) { fundo = desab( T, fundo ); if ( temBorda ) borda = desab( T, borda ); cor = desab( T, cor ); }
  if ( o.temCorLegenda ) cor = o.corLegenda;
  if ( o.grade ) {
    // A grid cell: the fill leaves the first row and column to the grid line,
    // drawn before; only the states that change the border draw it on top.
    p.fillRect( x + 1, y + 1, w - 1, h - 1, fundo );
    if ( temBorda && borda != T.linha ) p.rect( x + 1, y + 1, w - 1, h - 1, borda );
  } else {
    p.fillRoundRect( x, y, w, h, RAIO_CONTROLE, fundo );
    if ( temBorda ) p.roundRect( x, y, w, h, RAIO_CONTROLE, borda );
  }
  if ( o.icone != IconeNome::NENHUM ) {
    p.icone( o.icone, o.tam, x + rd2( w - o.tam ), y + rd2( h - o.tam ), cor );
  } else if ( o.legenda && *o.legenda ) {
    textoCentrado( p, o.legenda, x + rd2( w ), y, h, o.fonte, cor, 'c', o.alturaDeX );
  }
}

void campo( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, CampoEstado e ) {
  const Tema &T = p.t( );
  const uint16_t borda = e == CampoEstado::FOCO ? T.acento : e == CampoEstado::ERRO ? T.perigo
                         : e == CampoEstado::DESAB ? T.linha : T.linhaForte;
  p.fillRoundRect( x, y, w, h, RAIO_CONTROLE, e == CampoEstado::DESAB ? T.superficie2 : T.superficie );
  p.roundRect( x, y, w, h, RAIO_CONTROLE, borda );
}

void pontos( Pintor &p, int16_t x, int16_t y, int16_t h, int16_t n, const PontosOpts &o ) {
  const Tema &T = p.t( );
  const int16_t cy = y + rd2( h );
  const uint16_t tinta = o.temCor ? o.cor : T.tinta;
  int16_t cx = x + 3;
  for ( int16_t i = 0; i < n; i++ ) {
    if ( o.ultimo && i == n - 1 ) {
      textoCentrado( p, o.ultimo, cx - 3, y, h, tipo::corpo, tinta, 'l' );
      cx += larguraTinta( o.ultimo, tipo::corpo ) + ESP1;
    } else {
      p.fillCircle( cx, cy, 3, tinta );
      cx += o.passo;
    }
  }
  if ( o.cursor ) p.fillRect( n ? cx - 3 : x, cy - 8, 2, 16, T.acento );
  // empty places up to max (a PIN of fixed length)
  for ( int16_t i = n; i < o.max; i++ ) { p.circle( cx, cy, 3, T.linhaForte ); cx += o.passo; }
}

namespace {
uint16_t corDoTom( const Tema &T, Tom tom ) {
  switch ( tom ) {
    case Tom::PERIGO: return T.perigo;
    case Tom::ALERTA: return T.alerta;
    case Tom::POSITIVO:
    case Tom::OK: return T.positivo;
    default: return T.tinta2;
  }
}
}  // namespace

void ajuda( Pintor &p, int16_t x, int16_t y, int16_t h, const char *texto, Tom tom ) {
  textoCentrado( p, texto, x, y, h, tipo::apoio, corDoTom( p.t( ), tom ), 'l' );
}

void segmentado( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const char *const *opcoes, int16_t n,
                 int16_t ativa, bool des ) {
  const Tema &T = p.t( );
  // Math.floor((w - (n - 1) * 4) / n), positive here
  const int16_t sw = ( w - ( n - 1 ) * ESP1 ) / n;
  auto cor = [&]( uint16_t c ) { return des ? desab( T, c ) : c; };
  for ( int16_t i = 0; i < n; i++ ) {
    const int16_t sx = x + i * ( sw + ESP1 );
    const bool on = i == ativa;
    p.fillRoundRect( sx, y, sw, h, RAIO_CONTROLE, cor( on ? T.superficie : T.superficie2 ) );
    p.roundRect( sx, y, sw, h, RAIO_CONTROLE, cor( on ? T.acento : T.linha ) );
    textoCentrado( p, opcoes[i], sx + rd2( sw ), y, h, tipo::rotulo, cor( on ? T.acento : T.tinta2 ), 'c' );
  }
}

int16_t larguraChip( const char *rotulo ) { return larguraTinta( rotulo, tipo::rotulo ) + 2 * ESP2; }

int16_t chip( Pintor &p, int16_t x, int16_t y, int16_t h, const char *rotulo, bool ativo ) {
  const Tema &T = p.t( );
  const int16_t w = larguraChip( rotulo );
  p.fillRoundRect( x, y, w, h, RAIO_CONTROLE, ativo ? T.superficie : T.superficie2 );
  if ( ativo ) p.roundRect( x, y, w, h, RAIO_CONTROLE, T.acento );
  textoCentrado( p, rotulo, x + rd2( w ), y, h, tipo::rotulo, ativo ? T.acento : T.tinta, 'c' );
  return w;
}

int16_t larguraSelo( const char *rotulo ) { return 6 + ESP1 + larguraTinta( rotulo, tipo::rotulo ) + 2 * ESP2; }

int16_t selo( Pintor &p, int16_t x, int16_t y, int16_t h, const char *rotulo, Tom tom ) {
  const Tema &T = p.t( );
  uint16_t a, b;
  switch ( tom ) {
    case Tom::POSITIVO: case Tom::OK: a = T.positivoSuave; b = T.positivo; break;
    case Tom::ALERTA: a = T.alertaSuave; b = T.alerta; break;
    case Tom::PERIGO: a = T.perigoSuave; b = T.perigo; break;
    default: a = T.superficie2; b = T.tinta2; break;
  }
  const int16_t w = larguraSelo( rotulo );
  p.fillRoundRect( x, y, w, h, RAIO_TOTAL, a );
  p.fillCircle( x + ESP2 + 3, y + rd2( h ), 3, b );
  textoCentrado( p, rotulo, x + ESP2 + 6 + ESP1, y, h, tipo::rotulo, b, 'l' );
  return w;
}

/* ------------------------------------------------------------------ */
/* Chrome                                                              */
/* ------------------------------------------------------------------ */

void barraStatus( Pintor &p, const Status &s ) {
  const Tema &T = p.t( );
  textoCentrado( p, "SIMUT", PAD, 0, H_STATUS, tipo::subtitulo, T.tinta, 'l' );
  if ( s.aviso ) {
    // telas.js draws the badge at x 0 to learn its width, paints the
    // background over it and draws it centred. The paint also covers the
    // brand drawn above (x 12..), so in the plan's references a status bar
    // with a badge has no "SIMUT"; kept, for the references to hold.
    const int16_t w = selo( p, 0, 4, 20, s.aviso, s.avisoTom );
    p.fillRect( 0, 4, w, 20, T.fundo );
    selo( p, rd2( 320 - w ), 4, 20, s.aviso, s.avisoTom );
  } else {
    textoCentrado( p, s.hora, 160, 0, H_STATUS, tipo::botao, T.tinta, 'c' );
  }
  int16_t x = 320 - PAD - 16;
  for ( IconeNome ic : s.icones ) {
    if ( ic == IconeNome::NENHUM ) continue;
    p.icone( ic, 16, x, 6, T.tinta2 );
    x -= 16 + ESP1;
  }
  p.hline( 0, H_STATUS, 320, T.linha );
}

int16_t larguraAcao( const Acao &a ) {
  if ( a.w ) return a.w;
  return larguraTinta( a.rotulo, tipo::botao ) + 2 * ESP2 + ( a.icone != IconeNome::NENHUM ? 16 + ESP1 : 0 );
}

void barraTitulo( Pintor &p, const char *titulo, const Acao *acoes, int16_t n, Caixa16 *caixas, int16_t h ) {
  const Tema &T = p.t( );
  int16_t ocupado = 0;
  for ( int16_t i = 0; i < n; i++ ) ocupado += larguraAcao( acoes[i] ) + ESP2;
  // The title uses `titulo`; when it does not fit beside the actions it
  // falls to `subtitulo` (one title per screen still).
  const Fonte fonte = larguraTinta( titulo, tipo::titulo ) + PAD + ESP2 + ocupado > 320 - PAD ? tipo::subtitulo
                                                                                              : tipo::titulo;
  textoCentrado( p, titulo, PAD, 0, h, fonte, T.tinta, 'l' );
  int16_t x = 320 - PAD;
  for ( int16_t i = n - 1; i >= 0; i-- ) {
    const int16_t w = larguraAcao( acoes[i] );
    x -= w;
    botao( p, x, 4, w, 20, acoes[i].rotulo, acoes[i].tipo, false, acoes[i].icone );
    if ( caixas ) caixas[i] = Caixa16{ x, 4, w, 20 };
    x -= ESP2;
  }
  p.hline( 0, h, 320, T.linha );
}

void barraTituloVoltar( Pintor &p, const char *titulo, Caixa16 *voltar ) {
  Acao a;
  a.rotulo = "Voltar";
  a.tipo = Botao::DISCRETO;
  a.icone = IconeNome::ARROW_LEFT;
  barraTitulo( p, titulo, &a, 1, voltar );
}

void superficie( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, bool temFundo, uint16_t fundo, bool temBorda,
                 uint16_t borda ) {
  const Tema &T = p.t( );
  p.fillRoundRect( x, y, w, h, RAIO_CARTAO, temFundo ? fundo : T.superficie );
  p.roundRect( x, y, w, h, RAIO_CARTAO, temBorda ? borda : T.linha );
}

int16_t lista( Pintor &p, int16_t y, const Item *itens, int16_t n, int16_t hFila, int16_t selecionado ) {
  const Tema &T = p.t( );
  const int16_t x = ESP2, w = 320 - 2 * ESP2;
  const int16_t total = n * hFila;
  p.fillRoundRect( x, y, w, total, RAIO_CARTAO, T.superficie );
  p.roundRect( x, y, w, total, RAIO_CARTAO, T.linha );
  for ( int16_t i = 0; i < n; i++ ) {
    const Item &it = itens[i];
    const int16_t fy = y + i * hFila;
    if ( i == selecionado )
      p.fillRect( x + 1, fy + ( i == 0 ? 1 : 0 ), w - 2, hFila - ( i == 0 || i == n - 1 ? 1 : 0 ), T.superficie2 );
    if ( i > 0 ) p.hline( x + PAD, fy, w - 2 * PAD, T.linha );
    int16_t tx = x + PAD;
    const uint16_t corIt = it.temTom ? it.tom : T.tinta;
    if ( it.icone != IconeNome::NENHUM ) {
      p.icone( it.icone, 20, tx, fy + rd2( hFila - 20 ), it.temTom ? it.tom : T.tinta2 );
      tx += 20 + ESP2;
    }
    textoCentrado( p, it.texto, tx, fy, hFila, it.negrito ? tipo::botao : tipo::corpo, corIt, 'l' );
    int16_t rx = x + w - PAD;
    if ( it.chevron ) { p.icone( IconeNome::CHEVRON_RIGHT, 20, rx - 20, fy + rd2( hFila - 20 ), T.tinta2 ); rx -= 20 + ESP2; }
    if ( it.marcado ) { p.icone( IconeNome::CHECK, 20, rx - 20, fy + rd2( hFila - 20 ), T.acento ); rx -= 20 + ESP2; }
    if ( it.interruptor >= 0 ) { interruptor( p, rx - 40, fy + rd2( hFila - 24 ), it.interruptor == 1, false ); rx -= 40 + ESP2; }
    if ( it.selo ) {
      const int16_t sw = larguraSelo( it.selo );
      selo( p, rx - sw, fy + rd2( hFila - 20 ), 20, it.selo, it.seloTom );
      rx -= sw + ESP2;
    }
    if ( it.valor )
      textoCentrado( p, it.valor, rx, fy, hFila, it.valorFonte, it.temValorCor ? it.valorCor : T.tinta2, 'r' );
  }
  return y + total;
}

void interruptor( Pintor &p, int16_t x, int16_t y, bool ligado, bool des ) {
  const Tema &T = p.t( );
  const uint16_t trilho = des ? desab( T, ligado ? T.acento : T.superficie2 ) : ligado ? T.acento : T.superficie2;
  p.fillRoundRect( x, y, 40, 24, RAIO_TOTAL, trilho );
  if ( !ligado ) p.roundRect( x, y, 40, 24, RAIO_TOTAL, des ? desab( T, T.linhaForte ) : T.linhaForte );
  const int16_t cx = ligado ? x + 40 - 12 : x + 12;
  p.fillCircle( cx, y + 12, 8, ligado ? T.acentoTinta : T.linhaForte );
}

Caixa16 paginadorEsquerda( ) { return Caixa16{ ESP2, 196, 48, 40 }; }
Caixa16 paginadorDireita( ) { return Caixa16{ ESP2 + 48 + ESP2, 196, 48, 40 }; }
Caixa16 paginadorAcao( const Acao &a ) { return Caixa16{ (int16_t)( 320 - ESP2 - a.w ), 196, a.w, 40 }; }

void paginador( Pintor &p, int16_t pagina, int16_t total, const Acao *acao ) {
  const Tema &T = p.t( );
  const int16_t y = 196, h = 40;
  botao( p, ESP2, y, 48, h, "", Botao::SECUNDARIO );
  p.icone( IconeNome::CHEVRON_LEFT, 20, ESP2 + 14, y + 10, T.tinta );
  botao( p, ESP2 + 48 + ESP2, y, 48, h, "", Botao::SECUNDARIO );
  p.icone( IconeNome::CHEVRON_RIGHT, 20, ESP2 + 48 + ESP2 + 14, y + 10, T.tinta );
  char buf[24];
  snprintf( buf, sizeof buf, "%d de %d", (int)pagina, (int)total );
  textoCentrado( p, buf, ESP2 + 104 + ESP3, y, h, tipo::apoio, T.tinta2, 'l' );
  if ( acao ) botao( p, 320 - ESP2 - acao->w, y, acao->w, h, acao->rotulo, acao->tipo );
}

void rodapeAcoes( Pintor &p, const char *primaria, Botao primariaTipo, const char *secundaria, Caixa16 *cxP,
                  Caixa16 *cxS ) {
  const int16_t y = 196, h = 40;
  const int16_t wP = larguraTinta( primaria, tipo::botao ) + 2 * ESP4;
  botao( p, 320 - ESP2 - wP, y, wP, h, primaria, primariaTipo );
  if ( cxP ) *cxP = Caixa16{ (int16_t)( 320 - ESP2 - wP ), y, wP, h };
  const int16_t wS = larguraTinta( secundaria, tipo::botao ) + 2 * ESP2;
  botao( p, 320 - ESP2 - wP - ESP2 - wS, y, wS, h, secundaria, Botao::DISCRETO );
  if ( cxS ) *cxS = Caixa16{ (int16_t)( 320 - ESP2 - wP - ESP2 - wS ), y, wS, h };
}

int16_t numero( Pintor &p, int16_t x, int16_t y, const char *valor, const char *unidade, Fonte fonteNum, uint16_t cor,
                char alinha, Fonte fonteUn, bool temCorUn, uint16_t corUn ) {
  const int16_t w = larguraTinta( valor, fonteNum );
  const int16_t wu = ( unidade && *unidade ) ? larguraTinta( unidade, fonteUn ) + ESP1 : 0;
  const int16_t x0 = alinha == 'c' ? x - rd2( w + wu ) : alinha == 'r' ? x - w - wu : x;
  const Tinta r = p.texto( valor, x0, y, fonteNum, cor, 'l' );
  if ( unidade && *unidade ) p.texto( unidade, r.x1 + ESP1, y, fonteUn, temCorUn ? corUn : cor, 'l' );
  return x0 + w + wu;
}

void progresso( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, int32_t mil ) {
  const Tema &T = p.t( );
  p.fillRoundRect( x, y, w, h, RAIO_TOTAL, T.superficie2 );
  p.roundRect( x, y, w, h, RAIO_TOTAL, T.linha );
  // Math.max(h, Math.round(w * frac))
  int16_t pw = rdiv( (int32_t)w * mil, 1000 );
  if ( pw < h ) pw = h;
  p.fillRoundRect( x, y, pw, h, RAIO_TOTAL, T.acento );
}

void tracejada( Pintor &p, int16_t x0, int16_t x1, int16_t y, uint16_t cor ) {
  for ( int16_t x = x0; x < x1; x += 8 ) p.hline( x, y, ( x1 - x ) < 4 ? ( x1 - x ) : 4, cor );
}

char *n1( int32_t d, char *buf ) {
  // toFixed(1) of a value already rounded to tenths; -0 prints as 0,0.
  const bool neg = d < 0;
  const int32_t a = neg ? -d : d;
  snprintf( buf, 16, "%s%ld,%ld", neg ? "-" : "", (long)( a / 10 ), (long)( a % 10 ) );
  return buf;
}

}  // namespace ua
