// The Ângulo kit (see Angulo.h). Every function here is a port of the TFT
// plan's renderer, named after it in the comment; tools/tft_ui/host_ui.py held
// the screens drawn with it to the plan's references, pixel for pixel, until the
// approved look of 2026-10-15 departed from the plan (Angulo.h says how).

#include "Angulo.h"

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#ifdef UA_HOST
#include <stdlib.h>   // abort( ): the control recorder stops the harness when a sample has more than it holds
#endif

// The delivery's headers define their fonts, icons and tokens as namespace-
// scope consts (internal linkage): this translation unit is the only one that
// uses them; two others include angulo_tokens.h to read the palette's size.
namespace entrega {
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
#include "Textos.h"   // UA_TXT( ): after the unit's own headers, because its pragmas hold to the end of the unit (S10c, B3)

namespace ua {

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

namespace {

constexpr Tema deEntrega( const entrega::Tema &e, bool escuro, uint8_t entrada ) {
  return Tema{ e.fundo, e.superficie, e.superficie2, e.tinta, e.tinta2, e.linha, e.linhaForte, e.acento,
               e.acentoForte, e.acentoTinta, e.positivo, e.positivoSuave, e.alerta, e.alertaSuave, e.perigo,
               e.perigoSuave, e.perigoTinta, escuro, entrada };
}

// How many entries the palette holds: the extent of the generated index array, so that no second copy of the count lives here.
constexpr size_t kAcentos = sizeof entrega::TEMAS_CLARO / sizeof entrega::TEMAS_CLARO[0];

// The palette's tables, one per mode and entry, built by the compiler (S10c, A'2b): the generated tables are constexpr, so these are too, and they sit in flash
// beside them. As const objects with a constructor they sat in .bss (0x1B0 B) and a static initialiser filled them at every boot; tools/check_ui_ram.py measured
// the static RAM at 9,603 B then and 9,171 B now (2026-10-22). The 45 % twins stay in the generated tables: desab( ) reads them in place.
struct Paleta {
  Tema t[2][kAcentos];
  constexpr Paleta( ) : t{} {
    for ( size_t k = 0; k < kAcentos; k++ ) {
      t[0][k] = deEntrega( *entrega::TEMAS_CLARO[k], false, (uint8_t)k );
      t[1][k] = deEntrega( *entrega::TEMAS_ESCURO[k], true, (uint8_t)k );
    }
  }
};

constexpr Paleta kPaleta;

}  // namespace

const Tema &temaClaro( ) { return kPaleta.t[0][0]; }
const Tema &temaEscuro( ) { return kPaleta.t[1][0]; }

const Tema &tema( bool escuro, uint8_t acento ) {
  return kPaleta.t[escuro ? 1 : 0][acento < kAcentos ? acento : 0];
}

uint16_t desab( const Tema &t, uint16_t cor ) {
  // The 17 roles are distinct within a theme (angulo_tokens.h, 2026-10-14), so
  // the colour names its role. They are read from the object's bytes, role by
  // role: reading them as an array from &t.fundo indexes past one object, which is
  // undefined behaviour (cppcheck objectIndex, the CI's static analysis,
  // 2026-10-15); a table of member pointers would cost 72 B more.
  static_assert( offsetof( Tema, fundo ) == 0 && offsetof( Tema, escuro ) == 17 * sizeof( uint16_t ),
                 "Tema must start with its 17 colour roles and nothing else before escuro" );
  static_assert( sizeof( entrega::Tema ) == 17 * sizeof( uint16_t ), "the delivered Tema is the 17 roles and nothing else" );
  // The twin is the one of the same palette entry and mode as t (S10c, A'2), read in place from the delivered tables.
  const entrega::Tema &d = *( t.escuro ? entrega::TEMAS_ESCURO_DESAB : entrega::TEMAS_CLARO_DESAB )[t.entrada < kAcentos ? t.entrada : 0];
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

// The two encoding functions: the only code that knows how many modes there are, and the only hand-written code that names the generated count of the palette (ACENTOS_N) outside a static_assert. Other files tie a size to it in
// static_assert statements (Temas.cpp: the catalogue; MenusConfig.cpp: the Tema screen). tema( ) and desab( ) read the palette's size too, through kAcentos above: the extent of the generated index array, so the count has one source and no second copy.
Escolha escolhaDoIndice( int8_t t ) {
  static_assert( 3 * entrega::ACENTOS_N <= 127, "themeIndex is an int8_t: every index of the palette must fit" );
  if ( t < 0 || t >= 3 * entrega::ACENTOS_N ) return Escolha{ MODO_CLARO, 0 };
  return Escolha{ (uint8_t)( t % 3 ), (uint8_t)( t / 3 ) };
}

int8_t indiceDaEscolha( uint8_t modo, uint8_t acento ) {
  if ( modo >= 3 || acento >= entrega::ACENTOS_N ) return 0;
  return (int8_t)( modo + 3 * acento );
}

IdentidadeDoQuadro identidadeDoQuadro( uint8_t tela, const Tema &t, int8_t ox, int8_t oy ) {
  return IdentidadeDoQuadro{ tela, (uint8_t)t.escuro, t.entrada, ox, oy };
}

bool IdentidadeDoQuadro::repintaTudo( const IdentidadeDoQuadro &antes ) const {
  return ox != antes.ox || oy != antes.oy || escuro != antes.escuro || acento != antes.acento;
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
    &entrega::SANS9, &entrega::SANSB9, &entrega::MONO9, &entrega::MONO12,
    &entrega::DISP9, &entrega::DISP12, &entrega::DISP18, &entrega::DISP24, &entrega::DISP32, &entrega::DISP24_MARCA,
};
// Both tables are indexed by ua::Fonte: an entry added to or taken from the enum must be taken here
// and in tools/tft_ui/gerar_meta.js's FONTES too (the 7 pt fonts left all three on 2026-10-15).
static_assert( sizeof kFontes / sizeof kFontes[0] == (size_t)Fonte::MARCA + 1, "kFontes follows ua::Fonte" );
static_assert( sizeof meta::kCapX / sizeof meta::kCapX[0] == (size_t)Fonte::MARCA, "kCapX follows ua::Fonte" );

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

namespace {
// The recording of a frame (Pintor::gravar( )): one record per primitive call, 14 bytes each, in the list the caller lends (Pintor::lista( )). The
// records fill the list from its start; the bytes they point to (a text, the function and argument of a repetivel( ) call) fill it from its end.
enum : uint8_t { OP_CLEAR, OP_PIXEL, OP_FILL_RECT, OP_RECT, OP_LINE, OP_CIRCLE, OP_FILL_CIRCLE, OP_ROUND_RECT, OP_FILL_ROUND_RECT, OP_ICONE, OP_TEXTO,
                 OP_REPETIVEL };
struct Op {
  uint8_t tipo, aux;
  int16_t a, b, c, d, e;
  uint16_t cor;
};
static_assert( sizeof( Op ) == 14 && alignof( Op ) == 2, "a record is 14 bytes: the list is sized in them" );
// What an OP_REPETIVEL record points to. It sits at any byte of the list, so it is copied in and out with memcpy.
struct Chamada {
  Pintor::Repetivel fn;
  const void *arg;
};
uint8_t *s_lista = nullptr;
int s_bytesLista = 0;   // at most 32,767: a record's offset into the list is an int16_t
int s_nRegOps = 0;
int s_topo = 0;         // the offset the bytes filling the list from its end have reached; s_bytesLista with none
Op *regOps( ) { return reinterpret_cast<Op *>( s_lista ); }

// The blends remembered (mist( ), below), one entry for each coverage: declared here so that Pintor::gravar( ) can clear them.
struct Lembrada {
  uint16_t fg, bg, res;
};
Lembrada s_lembradas[16];
}  // namespace

void Pintor::lista( void *mem, size_t bytes ) {
  if ( !mem || bytes > 32767 || ( reinterpret_cast<uintptr_t>( mem ) & ( alignof( Op ) - 1 ) ) ) {
    s_lista = nullptr;
    s_bytesLista = 0;
    return;
  }
  s_lista = static_cast<uint8_t *>( mem );
  s_bytesLista = (int)bytes;
}

void Pintor::gravar( ) {
  // An entry of the blends' memo is three stores, and a restart of Core 1 does not clear .bss: a recording starts from none, so that no frame trusts what an
  // interrupted one left (96 B, a few stores a paint).
  memset( s_lembradas, 0, sizeof s_lembradas );
  s_nRegOps = 0;
  s_topo = s_bytesLista;
  pronto_ = false;
  estouro_ = !s_lista;   // no list: nothing is recorded, as for a frame that does not fit
  gravando_ = !estouro_;
}

void Pintor::parar( ) {
  if ( gravando_ || estouro_ ) pronto_ = !estouro_;
  gravando_ = false;
}

int Pintor::gravadas( ) const { return s_nRegOps; }
int Pintor::bytesGravados( ) const { return s_bytesLista - s_topo; }
int Pintor::usadosDaLista( ) const { return s_nRegOps * (int)sizeof( Op ) + s_bytesLista - s_topo; }

void Pintor::gravarOp( uint8_t tipo, int16_t a, int16_t b, int16_t c, int16_t d, int16_t e, uint16_t cor, uint8_t aux ) {
  if ( (size_t)( s_nRegOps + 1 ) * sizeof( Op ) > (size_t)s_topo ) {
    estouro_ = true;
    gravando_ = false;
    return;
  }
  regOps( )[s_nRegOps++] = Op{ tipo, aux, a, b, c, d, e, cor };
}

// n bytes from the list's end for what a record points to, with the record's own 14 bytes kept free; their offset, or -1 when the frame does not fit.
int Pintor::tomarBytes( const void *src, int n ) {
  if ( (size_t)s_topo < (size_t)( s_nRegOps + 1 ) * sizeof( Op ) + (size_t)n ) {
    estouro_ = true;
    gravando_ = false;
    return -1;
  }
  s_topo -= n;
  memcpy( s_lista + s_topo, src, (size_t)n );
  return s_topo;
}

void Pintor::gravarTexto( const char *s, int16_t ox, int16_t y, Fonte f, uint16_t cor, int16_t inkT, int16_t inkB ) {
  const int e = tomarBytes( s, (int)strlen( s ) + 1 );
  if ( e >= 0 ) gravarOp( OP_TEXTO, ox, y, inkT, inkB, (int16_t)e, cor, (uint8_t)f );
}

void Pintor::gravarRepetivel( Repetivel fn, const void *arg ) {
  const Chamada c{ fn, arg };
  const int e = tomarBytes( &c, (int)sizeof c );
  if ( e >= 0 ) gravarOp( OP_REPETIVEL, (int16_t)e, 0, 0, 0, 0, 0 );
}

void Pintor::repetivel( Repetivel fn, const void *arg ) {
  if ( gravando_ ) gravarRepetivel( fn, arg );
  Silencio mudo( *this );   // built after the record: a frame that did not fit there stays unrecorded
  fn( *this, arg );
}

// Runs the recorded calls again, against the window the Pintor has now. A text is not measured again: its pen position and its ink rows were
// worked out by the first strip.
void Pintor::repetir( ) {
  if ( !pronto_ ) return;
  const bool antes = gravando_;
  gravando_ = false;
  for ( int i = 0; i < s_nRegOps; i++ ) {
    const Op &o = regOps( )[i];
    switch ( o.tipo ) {
      case OP_CLEAR: clear( o.cor ); break;
      case OP_PIXEL: pixel( o.a, o.b, o.cor ); break;
      case OP_FILL_RECT: fillRect( o.a, o.b, o.c, o.d, o.cor ); break;
      case OP_RECT: rect( o.a, o.b, o.c, o.d, o.cor ); break;
      case OP_LINE: line( o.a, o.b, o.c, o.d, o.cor ); break;
      case OP_CIRCLE: circle( o.a, o.b, o.c, o.cor ); break;
      case OP_FILL_CIRCLE: fillCircle( o.a, o.b, o.c, o.cor ); break;
      case OP_ROUND_RECT: roundRect( o.a, o.b, o.c, o.d, o.e, o.cor ); break;
      case OP_FILL_ROUND_RECT: fillRoundRect( o.a, o.b, o.c, o.d, o.e, o.cor ); break;
      case OP_ICONE: icone( (IconeNome)o.e, o.c, o.a, o.b, o.cor ); break;
      case OP_TEXTO:
        if ( !fora( (int16_t)( o.b + o.c ), (int16_t)( o.d - o.c ) ) )
          desenharTexto( reinterpret_cast<const char *>( s_lista ) + o.e, o.a, o.b, (Fonte)o.aux, o.cor );
        break;
      case OP_REPETIVEL: {
        Chamada c;
        memcpy( &c, s_lista + o.a, sizeof c );
        c.fn( *this, c.arg );
        break;
      }
      default: break;
    }
  }
  gravando_ = antes;
}

void Pintor::clear( uint16_t c ) {
  if ( gravando_ ) gravarOp( OP_CLEAR, 0, 0, 0, 0, 0, c );
  // fillRect, not fillScreen: TftWithOffset::fillScreen( ) also paints the
  // alignment offset's margins black. On a canvas the two are equal.
  g_.fillRect( 0, 0, g_.width( ), g_.height( ), c );
}

void Pintor::pixel( int16_t x, int16_t y, uint16_t c ) {
  if ( gravando_ ) gravarOp( OP_PIXEL, x, y, 0, 0, 0, c );
  if ( x < 0 || y < 0 || x >= g_.width( ) || y >= g_.height( ) ) return;
  g_.drawPixel( x, y, c );
}

void Pintor::fillRect( int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c ) {
  if ( gravando_ ) gravarOp( OP_FILL_RECT, x, y, w, h, 0, c );
  // Wholly outside the window the strip keeps none of it, and Faixa::fillRect( ) would only clip it away: the chart's band is ~250 one-pixel columns, and each
  // strip that runs the plot ran this for every one of them (the board, 2026-10-16: a 24 h chart with full data composed in 58 ms, with little data in 37).
  if ( fora( y, h ) ) return;
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
  if ( gravando_ ) gravarOp( OP_RECT, x, y, w, h, 0, c );
  if ( w <= 0 || h <= 0 || fora( y, h ) ) return;
  g_.drawRect( x, y, w, h, c );
}

void Pintor::line( int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c ) {
  if ( gravando_ ) gravarOp( OP_LINE, x0, y0, x1, y1, 0, c );
  // A line stays within the rows of its two ends, so one wholly outside the window leaves no pixel in it. Without this, each of the plot's ~600 short lines
  // walked its pixels through a virtual drawPixel( ) in every strip that runs the plot.
  if ( ( y0 < y1 ? y1 : y0 ) < jy0_ || ( y0 < y1 ? y0 : y1 ) >= jy1_ ) return;
  if ( bruto_ ) {
    linhaBruta( x0, y0, x1, y1, c );
    return;
  }
  g_.drawLine( x0, y0, x1, y1, c );
}

// Adafruit_GFX::drawLine( ) written into the raw buffer (bruto( )): a vertical or a horizontal one as a run, any other with writeLine( )'s Bresenham, each pixel
// kept where the surface would keep it (inside the buffer's columns and rows). Adafruit's goes through two virtual calls and the surface's clip for every pixel
// (~90 instructions each from flash); the chart's second curve and its mean are ~1,000 pixels a frame, 7.8 of its 60.3 ms on the board (2026-10-16).
void Pintor::linhaBruta( int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c ) {
  const int32_t colHi = bLarg_ < g_.width( ) ? bLarg_ : g_.width( );
  const int32_t linhaLo = bTopo_ < 0 ? 0 : bTopo_;
  const int32_t linhaHi = bTopo_ + bAlt_ > g_.height( ) ? g_.height( ) : bTopo_ + bAlt_;
  auto ponto = [&]( int32_t px, int32_t py ) {
    if ( px >= 0 && px < colHi && py >= linhaLo && py < linhaHi ) bruto_[(int32_t)( py - bTopo_ ) * bLarg_ + px] = c;
  };
  if ( x0 == x1 ) {
    if ( y0 > y1 ) { const int16_t t = y0; y0 = y1; y1 = t; }
    for ( int32_t py = y0; py <= y1; py++ ) ponto( x0, py );
    return;
  }
  if ( y0 == y1 ) {
    if ( x0 > x1 ) { const int16_t t = x0; x0 = x1; x1 = t; }
    for ( int32_t px = x0; px <= x1; px++ ) ponto( px, y0 );
    return;
  }
  const bool inclinada = ( y1 > y0 ? y1 - y0 : y0 - y1 ) > ( x1 > x0 ? x1 - x0 : x0 - x1 );
  if ( inclinada ) {
    int16_t t = x0; x0 = y0; y0 = t;
    t = x1; x1 = y1; y1 = t;
  }
  if ( x0 > x1 ) {
    int16_t t = x0; x0 = x1; x1 = t;
    t = y0; y0 = y1; y1 = t;
  }
  const int16_t dx = x1 - x0, dy = y1 > y0 ? y1 - y0 : y0 - y1;
  int16_t erro = dx / 2;
  const int16_t passo = y0 < y1 ? 1 : -1;
  for ( ; x0 <= x1; x0++ ) {
    if ( inclinada ) ponto( y0, x0 ); else ponto( x0, y0 );
    erro = (int16_t)( erro - dy );
    if ( erro < 0 ) {
      y0 = (int16_t)( y0 + passo );
      erro = (int16_t)( erro + dx );
    }
  }
}

void margemPreta( Pintor &p ) {
  // The top and bottom bands are as wide as the surface and the side ones run between them, so no pixel is painted twice. The size is
  // the surface's (the firmware's strip reports the panel's 320 x 240 and keeps only its own rows), not a literal.
  const int16_t w = p.gfx( ).width( ), h = p.gfx( ).height( );
  p.fillRect( 0, 0, w, MARGEM, PRETO );
  p.fillRect( 0, (int16_t)( h - MARGEM ), w, MARGEM, PRETO );
  p.fillRect( 0, MARGEM, MARGEM, (int16_t)( h - 2 * MARGEM ), PRETO );
  p.fillRect( (int16_t)( w - MARGEM ), MARGEM, MARGEM, (int16_t)( h - 2 * MARGEM ), PRETO );
}

static_assert( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "hashFaixa reads two pixels as one little-endian word" );

namespace {
inline uint32_t entraPalavra( uint32_t h, uint32_t w ) {
  h = ( h ^ w ) * 16777619u;
  return ( h << 15 ) | ( h >> 17 );
}
}  // namespace

// Runs from RAM (P12a), like Faixa::fillRect( ) (Faixa.h has the numbers): on the board on 2026-10-16 the margin and the hash of a chart frame took 8.2 ms against 3.5 to 4.1
// on the dashboard for the same 6,400 pixels a strip, the chart screens' XIP cache missing 6.4 to 8.0% of the time against 1.0 to 1.6%; in SRAM, 2.8 to 2.9 ms on both.
// __no_inline_not_in_flash_func and not __not_in_flash_func: the section attribute only places an out-of-line copy, and GCC inlines a function so marked into a caller in
// flash (DisplayManager.cpp, core1AlarmIsr). Pinned by tools/check_ui_ram.py. The host has no flash to leave.
#ifndef __no_inline_not_in_flash_func
#define __no_inline_not_in_flash_func( f ) f
#endif
uint32_t __no_inline_not_in_flash_func( hashFaixa )( const uint16_t *px, size_t n ) {
  uint32_t h = 2166136261u;
  size_t i = 0;
  if ( ( reinterpret_cast<uintptr_t>( px ) & 3 ) == 0 ) {
    const Palavra *w = static_cast<const Palavra *>( __builtin_assume_aligned( px, 4 ) );
    const size_t pares = n / 2;
    size_t k = 0;
    for ( ; k + 4 <= pares; k += 4 ) {
      h = entraPalavra( h, w[k] );
      h = entraPalavra( h, w[k + 1] );
      h = entraPalavra( h, w[k + 2] );
      h = entraPalavra( h, w[k + 3] );
    }
    for ( ; k < pares; k++ ) h = entraPalavra( h, w[k] );
    i = pares * 2;
  } else {
    for ( ; i + 2 <= n; i += 2 ) h = entraPalavra( h, (uint32_t)px[i] | (uint32_t)px[i + 1] << 16 );
  }
  if ( i < n ) h = entraPalavra( h, px[i] );
  return h;
}

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

// misturar16( ) remembered, one entry for each coverage a (1 to 15). The corners of a screen's boxes mix the same few colours over the same background
// again and again (the detail's six cards and their outlines are ~1,500 blends a frame), so the arithmetic runs once for each (colour, background,
// coverage) and the rest cost a compare and a load: a hit returns what misturar16( ) returns, for it is a function of the three. All zero is a valid
// entry (black over black is black), and it is what Pintor::gravar( ) leaves at the start of each recording. 96 B of RAM, held by tools/ui_ram_budget.json; the
// boxes took 20.9 of the detail's 53.0 ms on the board (2026-10-16).
inline uint16_t mist( uint16_t fg, uint16_t bg, uint8_t a ) {
  Lembrada &m = s_lembradas[a & 15];
  if ( m.fg != fg || m.bg != bg ) {
    m.fg = fg;
    m.bg = bg;
    m.res = misturar16( fg, bg, a );
  }
  return m.res;
}

inline void espalhar( uint16_t *p, int n, uint16_t c ) {
  while ( n-- > 0 ) *p++ = c;
}

// Sixteenths of a pixel inside the disc of squared radius out2 and outside the
// one of squared radius in2 (in2 < 0: a whole disc), everything in eighths of a
// pixel so that it stays integer: (dx8, dy8) is the pixel's corner nearest the
// centre's opposite side, and the 4 x 4 samples sit at its (2k + 1) / 8 points.
constexpr uint8_t cobertura( int32_t dx8, int32_t dy8, int32_t out2, int32_t in2 ) {
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

// The coverage of a pixel of a rounded corner depends on nothing but its place (i, j), the radius and the shape, a fill or a 1 px outline, so for
// the radii the screens draw it is a table, built by the compiler out of the very function above (the 16 samples a pixel cost on the board:
// cantos took 9.6 ms of the dashboard's 73, measured on 2026-10-15). A corner of radius r is r x r bytes, row j from the top and column i from the
// outer edge: r = 1..12 (RAIO_CONTROLE 6, RAIO_CARTAO 12, the pills up to 24 px high), 650 B for each shape. A disc of radius r is the same coverage
// mirrored four ways, so (r + 1) x (r + 1) bytes cover it from its centre: r = 0..8 (the knob of a switch is the biggest), 285 B for each shape.
// Both are in flash (constexpr): no RAM. A bigger radius, or a shape that is neither, is computed as before.
constexpr int R_TAB = 12, R_DISCO = 8;
constexpr int tamCantos( int r ) { return r * ( r + 1 ) * ( 2 * r + 1 ) / 6; }
constexpr int tamDisco( int r ) { return ( r + 1 ) * ( r + 2 ) * ( 2 * r + 3 ) / 6; }

struct TabCantos {
  uint8_t d[2][tamCantos( R_TAB )];
  constexpr TabCantos( ) : d{} {
    for ( int r = 1; r <= R_TAB; r++ ) {
      const int off = tamCantos( r - 1 );
      for ( int k = 0; k < 2; k++ ) {
        const int32_t out = 8 * r, in = 8 * ( r - 1 );
        const int32_t in2 = k == 0 || in <= 0 ? -1 : in * in;
        for ( int j = 0; j < r; j++ )
          for ( int i = 0; i < r; i++ ) d[k][off + j * r + i] = cobertura( 8 * ( i - r ), 8 * ( j - r ), out * out, in2 );
      }
    }
  }
};
constexpr TabCantos kCantos;

struct TabDisco {
  uint8_t d[2][tamDisco( R_DISCO )];
  constexpr TabDisco( ) : d{} {
    for ( int r = 0; r <= R_DISCO; r++ ) {
      const int off = tamDisco( r - 1 );
      for ( int k = 0; k < 2; k++ ) {
        const int32_t out = 8 * r + 4, in = 8 * r - 4;
        const int32_t in2 = k == 0 || in <= 0 ? -1 : in * in;
        for ( int j = 0; j <= r; j++ )
          for ( int i = 0; i <= r; i++ ) d[k][off + j * ( r + 1 ) + i] = cobertura( 8 * i - 4, 8 * j - 4, out * out, in2 );
      }
    }
  }
};
constexpr TabDisco kDisco;

}  // namespace

void Pintor::misturar( int16_t x, int16_t y, uint16_t c, uint8_t a ) {
  if ( a == 0 || x < 0 || y < 0 || x >= g_.width( ) || y >= g_.height( ) ) return;
  if ( bruto_ ) {
    // The surface is a raw buffer (bruto( )): the pixel is read and written in place, the same arithmetic without the two virtual calls (ler( ), drawPixel( ))
    // that were most of the cost of a corner; a row the buffer does not hold is not kept by the surface either.
    const int32_t linha = (int32_t)y - bTopo_;
    if ( linha < 0 || linha >= bAlt_ || x >= bLarg_ ) return;
    uint16_t *px = bruto_ + linha * bLarg_ + x;
    *px = a >= 16 ? c : mist( c, *px, a );
    return;
  }
  g_.drawPixel( x, y, a >= 16 ? c : mist( c, l_.ler( x, y ), a ) );
}

// The four r x r corner squares of a box at [x, x + w) by [y, y + h): each pixel
// mixed against the quarter disc whose centre is r in from both outer edges, and
// outside the disc of squared radius in2 (an outline; in2 < 0 for a fill). Rows
// outside the strip being drawn (janela) are skipped: they are not kept.
void Pintor::cantos( int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c, int32_t in2 ) {
  const int32_t out = 8 * r, in = 8 * ( r - 1 );
  // The table, when the radius is one it holds and the shape is the fill or the 1 px outline roundRect( ) asks for.
  const uint8_t *tab = nullptr;
  if ( r >= 1 && r <= R_TAB && ( in2 < 0 || in2 == ( in > 0 ? in * in : -1 ) ) ) tab = &kCantos.d[in2 < 0 ? 0 : 1][tamCantos( r - 1 )];
  // The strip's own case, which is every box the firmware draws: the table, a raw buffer whose rows are the window's, the box inside the surface's columns.
  // Each row of a corner is then walked once in the table, the spans and the blends go straight into the buffer (a span was a Pintor::fillRect( ) call, with
  // its window and surface tests, and a Faixa::fillRect( ) behind it; a blend, misturar( )'s tests and the arithmetic): on the board the boxes took 11.9 ms of the
  // chart's 60.3 and 20.9 of the detail's 53.0, for 204 spans and 764 blends on the chart and 324 and 1,452 on the detail (counted on the host, in this function):
  // ~12 us an operation, nearly all of it instructions fetched from flash (2026-10-16). The same pixels in the same cells: host_perf --caixas holds them to a
  // per-pixel reference, and every frame to the one Adafruit GFX draws whole.
  if ( tab && bruto_ && x >= 0 && (int32_t)x + w <= bLarg_ && (int32_t)x + w <= g_.width( ) && jy0_ >= bTopo_ && jy1_ <= bTopo_ + bAlt_ && jy0_ >= 0 &&
       jy1_ <= g_.height( ) ) {
    for ( int16_t j = 0; j < r; j++ ) {
      const int16_t ya = y + j, yb = y + h - 1 - j;
      uint16_t *const cima = ya >= jy0_ && ya < jy1_ ? bruto_ + (int32_t)( ya - bTopo_ ) * bLarg_ + x : nullptr;
      uint16_t *const baixo = yb >= jy0_ && yb < jy1_ ? bruto_ + (int32_t)( yb - bTopo_ ) * bLarg_ + x : nullptr;
      if ( !cima && !baixo ) continue;
      const uint8_t *const linha = tab + j * r;
      for ( int16_t i = 0; i < r; ) {
        const uint8_t a = linha[i];
        if ( a == 0 ) {
          i++;
          continue;
        }
        int16_t k = i + 1;
        if ( a >= 16 )
          while ( k < r && linha[k] >= 16 ) k++;
        if ( k - i >= 2 ) {   // a run of whole cells: one span a side
          if ( cima ) {
            espalhar( cima + i, k - i, c );
            espalhar( cima + w - k, k - i, c );
          }
          if ( baixo ) {
            espalhar( baixo + i, k - i, c );
            espalhar( baixo + w - k, k - i, c );
          }
        } else if ( a >= 16 ) {
          if ( cima ) cima[i] = cima[w - 1 - i] = c;
          if ( baixo ) baixo[i] = baixo[w - 1 - i] = c;
        } else {
          if ( cima ) {
            cima[i] = mist( c, cima[i], a );
            cima[w - 1 - i] = mist( c, cima[w - 1 - i], a );
          }
          if ( baixo ) {
            baixo[i] = mist( c, baixo[i], a );
            baixo[w - 1 - i] = mist( c, baixo[w - 1 - i], a );
          }
        }
        i = k;
      }
    }
    return;
  }
  auto cel = [&]( int16_t i, int16_t j ) -> uint8_t { return tab ? tab[j * r + i] : cobertura( 8 * ( i - r ), 8 * ( j - r ), out * out, in2 ); };
  for ( int16_t j = 0; j < r; j++ ) {
    const int16_t ya = y + j, yb = y + h - 1 - j;
    const bool cima = ya >= jy0_ && ya < jy1_, baixo = yb >= jy0_ && yb < jy1_;
    if ( !cima && !baixo ) continue;
    for ( int16_t i = 0; i < r; ) {
      const uint8_t a = cel( i, j );
      if ( a == 0 ) {   // misturar( ) would return at once, four times
        i++;
        continue;
      }
      // A run of cells the shape covers whole (the inner end of a row of a filled corner) is one span a side, not a pixel at a time; the boxes were 14.3 ms of a
      // chart frame's 58.7 on the board (2026-10-16). The left corner's cells i to k - 1 are the columns x + i to x + k - 1, the right corner's x + w - k to
      // x + w - 1 - i. host_perf --caixas holds the corners, the spans and the body to a per-pixel reference.
      int16_t k = i + 1;
      if ( a >= 16 )
        while ( k < r && cel( k, j ) >= 16 ) k++;
      if ( k - i >= 2 ) {
        if ( cima ) { fillRect( x + i, ya, k - i, 1, c ); fillRect( x + w - k, ya, k - i, 1, c ); }
        if ( baixo ) { fillRect( x + i, yb, k - i, 1, c ); fillRect( x + w - k, yb, k - i, 1, c ); }
      } else {
        if ( cima ) { misturar( x + i, ya, c, a ); misturar( x + w - 1 - i, ya, c, a ); }
        if ( baixo ) { misturar( x + i, yb, c, a ); misturar( x + w - 1 - i, yb, c, a ); }
      }
      i = k;
    }
  }
}

// A circle as Adafruit GFX places it (centre pixel (x, y), radius r: the pixels
// from x - r to x + r), drawn as the disc of radius r + 0.5 around that pixel's
// centre; the outline is the ring between r - 0.5 and r + 0.5 (in2 >= 0).
void Pintor::disco( int16_t x, int16_t y, int16_t r, uint16_t c, int32_t in2 ) {
  if ( r < 0 || fora( y - r, 2 * r + 1 ) ) return;
  const int32_t out = 8 * r + 4, in = 8 * r - 4;
  const uint8_t *tab = nullptr;
  if ( r <= R_DISCO && ( in2 < 0 || in2 == ( in > 0 ? in * in : -1 ) ) ) tab = &kDisco.d[in2 < 0 ? 0 : 1][tamDisco( r - 1 )];
  for ( int16_t j = -r; j <= r; j++ ) {
    if ( y + j < jy0_ || y + j >= jy1_ ) continue;
    const int aj = j < 0 ? -j : j;
    for ( int16_t i = -r; i <= r; i++ )
      misturar( x + i, y + j, c, tab ? tab[aj * ( r + 1 ) + ( i < 0 ? -i : i )] : cobertura( 8 * i - 4, 8 * j - 4, out * out, in2 ) );
  }
}

void Pintor::circle( int16_t x, int16_t y, int16_t r, uint16_t c ) {
  if ( gravando_ ) gravarOp( OP_CIRCLE, x, y, r, 0, 0, c );
  const int32_t in = 8 * r - 4;
  disco( x, y, r, c, in > 0 ? in * in : -1 );
}

void Pintor::fillCircle( int16_t x, int16_t y, int16_t r, uint16_t c ) {
  if ( gravando_ ) gravarOp( OP_FILL_CIRCLE, x, y, r, 0, 0, c );
  disco( x, y, r, c, -1 );
}

// A rounded box as Adafruit GFX places it ([x, x + w) by [y, y + h), corners of
// radius r, at most half the shorter side): the straight parts as before, the
// corners smoothed (cantos). The outline is 1 px: the ring between the corner's
// disc and the one of radius r - 1 around the same centre, the box inset by 1.
void Pintor::roundRect( int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c ) {
  if ( gravando_ ) gravarOp( OP_ROUND_RECT, x, y, w, h, r, c );
#ifdef UA_HOST
  hostSuperficie( x, y, w, h );
#endif
  const Silencio parte( *this );
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
  if ( gravando_ ) gravarOp( OP_FILL_ROUND_RECT, x, y, w, h, r, c );
#ifdef UA_HOST
  hostSuperficie( x, y, w, h );
#endif
  const Silencio parte( *this );
  if ( w <= 0 || h <= 0 || fora( y, h ) ) return;
  const int16_t m = ( w < h ? w : h ) / 2;
  if ( r > m ) r = m;
  // The body in three pieces that meet the corners: the rows between them whole, in one run a row (a strip that holds none of the corners' rows, which is most of
  // them, used to fill the middle column and the two sides separately), and the top and the bottom between the corners.
  fillRect( x, y + r, w, h - 2 * r, c );
  fillRect( x + r, y, w - 2 * r, r, c );
  fillRect( x + r, y + h - r, w - 2 * r, r, c );
  cantos( x, y, w, h, r, c, -1 );
}

bool temIcone( IconeNome n, int16_t tam ) {
  for ( const icones::Reg &r : icones::kIcones )
    if ( r.nome == n && r.tam == tam ) return true;
  return false;
}

void Pintor::icone( IconeNome n, int16_t tam, int16_t x, int16_t y, uint16_t c ) {
  if ( gravando_ ) gravarOp( OP_ICONE, x, y, tam, 0, (int16_t)n, c );
  for ( const icones::Reg &r : icones::kIcones ) {
    if ( r.nome == n && r.tam == tam ) {
      if ( !fora( y, tam ) ) {
        // Adafruit's drawBitmap( ) tests every pixel of the icon and makes a virtual call for each one that is set, in every strip the icon touches (2.3 to 2.5 ms of
        // the chart's and the detail's frames on the board, 2026-10-16): one byte at a time, the rows of the window only, when the icon lies inside the surface's columns.
        if ( bruto_ && x >= 0 && (int32_t)x + tam <= bLarg_ && (int32_t)x + tam <= g_.width( ) ) bitmapBruto( x, y, r.bits, tam, c );
        else g_.drawBitmap( x, y, r.bits, tam, tam, c );
      }
      return;
    }
  }
  s_faltando++;
}

void Pintor::bitmapBruto( int16_t x, int16_t y, const uint8_t *bits, int16_t tam, uint16_t c ) {
  const int32_t linhaLo = bTopo_ < 0 ? 0 : bTopo_;
  const int32_t linhaHi = bTopo_ + bAlt_ > g_.height( ) ? g_.height( ) : bTopo_ + bAlt_;
  const int32_t j0 = y < linhaLo ? linhaLo - y : 0, j1 = y + tam > linhaHi ? linhaHi - y : tam;
  const int32_t porLinha = ( tam + 7 ) / 8;
  for ( int32_t j = j0; j < j1; j++ ) {
    uint16_t *d = bruto_ + (int32_t)( y + j - bTopo_ ) * bLarg_ + x;
    const uint8_t *b = bits + j * porLinha;
    for ( int32_t resto = tam; resto > 0; resto -= 8, d += 8 ) {
      uint32_t m = *b++;
      if ( resto < 8 ) m &= ( 0xFFu << ( 8 - resto ) ) & 0xFFu;
      if ( !m ) continue;
      if ( m & 0x80 ) d[0] = c;
      if ( m & 0x40 ) d[1] = c;
      if ( m & 0x20 ) d[2] = c;
      if ( m & 0x10 ) d[3] = c;
      if ( m & 0x08 ) d[4] = c;
      if ( m & 0x04 ) d[5] = c;
      if ( m & 0x02 ) d[6] = c;
      if ( m & 0x01 ) d[7] = c;
    }
  }
}

#ifdef UA_HOST
namespace {
constexpr int MAX_TEXTOS = 256;   // 42 in the fullest sample (2026-10-22); a sample over this stops the harness, it is never cut
TextoDesenhado s_textos[MAX_TEXTOS];
int s_nTextos = 0;
void hostAnotarTexto( const char *s, int16_t x0, int16_t x1, int16_t y0, int16_t y1, bool selo = false ) {
  if ( s_nTextos >= MAX_TEXTOS ) {
    fprintf( stderr, "more than %d texts in one sample\n", MAX_TEXTOS );
    abort( );
  }
  TextoDesenhado &t = s_textos[s_nTextos++];
  t.x0 = x0; t.x1 = x1; t.y0 = y0; t.y1 = y1; t.selo = selo;
  int i = 0;
  for ( ; i < (int)sizeof t.texto - 1 && s[i]; i++ ) t.texto[i] = s[i];
  // A cut that falls inside a character backs off to its first byte, as ua::copiar( ) does: host_telas.py reads the record as UTF-8, and a
  // translation of 84 bytes with "í" at bytes 62 and 63 ended it in a UnicodeDecodeError that named no sample (the review of S10c B3).
  // The longest text drawn is 43 bytes (2026-10-22, six groups in pt-BR and es-ES); host_telas.py refuses a record within 3 bytes of the field,
  // which every cut record is.
  if ( s[i] )
    while ( i > 0 && ( (uint8_t)s[i] & 0xC0 ) == 0x80 ) i--;
  t.texto[i] = 0;
}
}  // namespace
void hostLimparTextos( ) { s_nTextos = 0; }
int hostTextos( const TextoDesenhado **lista ) { *lista = s_textos; return s_nTextos; }

namespace {
constexpr int MAX_SUPERFICIES = 2048;   // the text keyboard has about 45 keys; a sample over this stops the harness, it is never cut
SuperficieDesenhada s_superficies[MAX_SUPERFICIES];
int s_nSuperficies = 0;
}  // namespace
void hostLimparSuperficies( ) { s_nSuperficies = 0; }
int hostSuperficies( const SuperficieDesenhada **lista ) { *lista = s_superficies; return s_nSuperficies; }
void hostSuperficie( int16_t x, int16_t y, int16_t w, int16_t h ) {
  if ( s_nSuperficies >= MAX_SUPERFICIES ) {
    fprintf( stderr, "more than %d surfaces in one sample\n", MAX_SUPERFICIES );
    abort( );
  }
  s_superficies[s_nSuperficies++] = SuperficieDesenhada{ x, y, w, h };
}

namespace {
constexpr int MAX_CONTROLES = 128;   // the text keyboard has about 45; a sample over this stops the harness, it is never cut
ControleDesenhado s_controles[MAX_CONTROLES];
int s_nControles = 0;
char s_naoControle[sizeof s_controles[0].excluido] = "";
char s_sobAlvo[sizeof s_controles[0].sob] = "";
void copiarNome( char *dst, int cap, const char *src ) {
  int i = 0;
  for ( ; i < cap - 1 && src[i]; i++ ) dst[i] = src[i];
  dst[i] = 0;
}
}  // namespace
void hostLimparControles( ) { s_nControles = 0; s_naoControle[0] = 0; s_sobAlvo[0] = 0; }
int hostControles( const ControleDesenhado **lista ) { *lista = s_controles; return s_nControles; }
void hostNaoEControle( const char *motivo ) { copiarNome( s_naoControle, (int)sizeof s_naoControle, motivo ); }
void hostControleSobAlvo( const char *motivo ) { copiarNome( s_sobAlvo, (int)sizeof s_sobAlvo, motivo ); }
void hostControle( int16_t x, int16_t y, int16_t w, int16_t h, bool habilitado, const char *tipo ) {
  ControleDesenhado *c = nullptr;
  for ( int i = 0; i < s_nControles && !c; i++ )
    if ( s_controles[i].x == x && s_controles[i].y == y && s_controles[i].w == w && s_controles[i].h == h ) c = &s_controles[i];
  if ( !c ) {
    if ( s_nControles >= MAX_CONTROLES ) { fprintf( stderr, "more than %d controls in one sample\n", MAX_CONTROLES ); abort( ); }
    c = &s_controles[s_nControles++];
  }
  c->x = x; c->y = y; c->w = w; c->h = h; c->habilitado = habilitado;
  copiarNome( c->tipo, (int)sizeof c->tipo, tipo );
  copiarNome( c->excluido, (int)sizeof c->excluido, s_naoControle );
  s_naoControle[0] = 0;
  copiarNome( c->sob, (int)sizeof c->sob, s_sobAlvo );
  s_sobAlvo[0] = 0;
}
#endif

// The glyphs of s written straight into the surface's buffer (bruto( )): what Adafruit_GFX::drawChar( ) does for a custom font at size 1 (a pixel
// of the colour for each set bit of the glyph's bitmap, the rows one after the other with no padding at the end of a row, the pen moving on by
// xAdvance), without the virtual drawPixel( ) it makes for each of them, and without walking the rows and columns the buffer does not hold.
void Pintor::desenharTextoBruto( const char *s, int16_t ox, int16_t y, Fonte f, uint16_t cor ) {
  const int32_t colHi = bLarg_ < g_.width( ) ? bLarg_ : g_.width( );
  const int32_t linhaLo = bTopo_ < 0 ? 0 : bTopo_;
  const int32_t linhaHi = bTopo_ + bAlt_ > g_.height( ) ? g_.height( ) : bTopo_ + bAlt_;
  int32_t cursor = ox;
  while ( *s ) {
    const uint32_t cp = proximo( s );
    const uint8_t c = codigo( cp );
    const GFXfont *gf = c ? fonteDe( f, c ) : nullptr;
    if ( !gf ) { s_faltando++; continue; }
    const GFXglyph &gl = gf->glyph[c - gf->first];
    if ( gl.width > 0 && gl.height > 0 ) {
      const int32_t gx = cursor + gl.xOffset, gy = y + gl.yOffset;   // the bitmap's top left
      const int32_t xa = gx < 0 ? 0 : gx, xb = gx + gl.width > colHi ? colHi : gx + gl.width;
      const int32_t ya = gy < linhaLo ? linhaLo : gy, yb = gy + gl.height > linhaHi ? linhaHi : gy + gl.height;
      if ( xa < xb && ya < yb ) {
        const uint8_t *mapa = gf->bitmap + gl.bitmapOffset;
        const int32_t n = xb - xa;
        for ( int32_t py = ya; py < yb; py++ ) {
          uint16_t *dst = bruto_ + (int32_t)( py - bTopo_ ) * bLarg_ + xa;
          const uint32_t bit = (uint32_t)( py - gy ) * gl.width + (uint32_t)( xa - gx );   // the first bit of this row that is kept
          const uint8_t *b = mapa + ( bit >> 3 );
          // A byte of the bitmap at a time, not a pixel: a byte of blank is a test, and a set bit is one store with no shift, count or reload round it (the
          // pixel loop was ~10 instructions a pixel from flash, and the texts 10.7 of the chart's 60.3 ms and 12.0 of the detail's 53.0 on the board, 2026-10-16).
          // The kept bits of the first byte sit at its top; k of the byte's bits belong to this row, the rest to the next glyph row or to the clipped columns.
          uint32_t bits = (uint32_t)( *b++ << ( bit & 7 ) ) & 0xFFu;
          int32_t tem = 8 - (int32_t)( bit & 7 );
          for ( int32_t resto = n;; ) {
            const int32_t k = tem < resto ? tem : resto;
            const uint32_t m = k < 8 ? bits & ( 0xFFu << ( 8 - k ) ) & 0xFFu : bits;
            if ( m ) {
              if ( m & 0x80 ) dst[0] = cor;
              if ( m & 0x40 ) dst[1] = cor;
              if ( m & 0x20 ) dst[2] = cor;
              if ( m & 0x10 ) dst[3] = cor;
              if ( m & 0x08 ) dst[4] = cor;
              if ( m & 0x04 ) dst[5] = cor;
              if ( m & 0x02 ) dst[6] = cor;
              if ( m & 0x01 ) dst[7] = cor;
            }
            resto -= k;
            if ( resto <= 0 ) break;
            dst += k;
            bits = *b++;
            tem = 8;
          }
        }
      }
    }
    cursor += gl.xAdvance;
  }
}

// The glyphs of s, drawn through Adafruit GFX from the pen at ox, baseline y. Adafruit_GFX::write( ) draws each glyph at the cursor and advances
// it by xAdvance; a glyph that would cross the right edge is clipped, not wrapped.
void Pintor::desenharTexto( const char *s, int16_t ox, int16_t y, Fonte f, uint16_t cor ) {
  if ( bruto_ ) {
    desenharTextoBruto( s, ox, y, f, cor );
    return;
  }
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
}

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
  if ( gravando_ ) gravarTexto( s, ox, y, f, cor, cx.inkT, cx.inkB );
  if ( fora( (int16_t)( y + cx.inkT ), (int16_t)( cx.inkB - cx.inkT ) ) ) {
#ifdef UA_HOST
    hostAnotarTexto( s0, (int16_t)( ox + cx.inkL ), (int16_t)( ox + cx.inkR ), (int16_t)( y + cx.inkT ),
                     (int16_t)( y + cx.inkB ) );
#endif
    return Tinta{ (int16_t)( ox + cx.inkL ), (int16_t)( ox + cx.inkR ) };
  }
  desenharTexto( s, ox, y, f, cor );
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
  UA_CONTROLE( x, y, w, h, !des, "botao" );
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
  UA_CONTROLE( x, y, w, h, o.estado != TeclaEstado::DESAB, "tecla" );
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
    UA_CONTROLE( sx, y, sw, h, !des, "segmento" );
    p.fillRoundRect( sx, y, sw, h, RAIO_CONTROLE, cor( on ? T.superficie : T.superficie2 ) );
    p.roundRect( sx, y, sw, h, RAIO_CONTROLE, cor( on ? T.acento : T.linha ) );
    textoCentrado( p, opcoes[i], sx + rd2( sw ), y, h, tipo::rotulo, cor( on ? T.acento : T.tinta2 ), 'c' );
  }
}

int16_t larguraChip( const char *rotulo ) { return larguraTinta( rotulo, tipo::rotulo ) + 2 * ESP2; }

int16_t chip( Pintor &p, int16_t x, int16_t y, int16_t h, const char *rotulo, bool ativo ) {
  const Tema &T = p.t( );
  const int16_t w = larguraChip( rotulo );
  UA_CONTROLE( x, y, w, h, true, "chip" );
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
#ifdef UA_HOST
  // The whole box, not the label's ink: on the bench (2026-10-15) the 9 pt status-bar badge covered the "T" of
  // "SIMUT" and was drawn over by the pending count, and no two inks overlapped, so host_telas.py saw nothing.
  hostAnotarTexto( rotulo, x, (int16_t)( x + w ), y, (int16_t)( y + h ), true );
#endif
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

Acao acaoVoltar( ) {
  Acao a;
  a.rotulo = UA_TXT( "Back" );
  a.tipo = Botao::DISCRETO;
  a.icone = IconeNome::ARROW_LEFT;
  return a;
}

void barraTituloVoltar( Pintor &p, const char *titulo, Caixa16 *voltar ) {
  const Acao a = acaoVoltar( );
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
  UA_CONTROLE( x, y, 40, 24, !des, "interruptor" );
  const uint16_t trilho = des ? desab( T, ligado ? T.acento : T.superficie2 ) : ligado ? T.acento : T.superficie2;
  p.fillRoundRect( x, y, 40, 24, RAIO_TOTAL, trilho );
  if ( !ligado ) p.roundRect( x, y, 40, 24, RAIO_TOTAL, des ? desab( T, T.linhaForte ) : T.linhaForte );
  const int16_t cx = ligado ? x + 40 - 12 : x + 12;
  p.fillCircle( cx, y + 12, 8, ligado ? T.acentoTinta : T.linhaForte );
}

Caixa16 paginadorEsquerda( ) { return Caixa16{ ESP2, 196, 48, 40 }; }
Caixa16 paginadorDireita( ) { return Caixa16{ ESP2 + 48 + ESP2, 196, 48, 40 }; }
Caixa16 paginadorAcao( const Acao &a ) { return Caixa16{ (int16_t)( 320 - ESP2 - a.w ), 196, a.w, 40 }; }

void contagemDoPaginador( char *buf, int cap, int16_t pagina, int16_t total ) {
  snprintf( buf, (size_t)cap, "%d %s %d", (int)pagina, UA_TXT( "of" ), (int)total );
}

void paginador( Pintor &p, int16_t pagina, int16_t total, const Acao *acao ) {
  const Tema &T = p.t( );
  const int16_t y = 196, h = 40;
  botao( p, ESP2, y, 48, h, "", Botao::SECUNDARIO );
  p.icone( IconeNome::CHEVRON_LEFT, 20, ESP2 + 14, y + 10, T.tinta );
  botao( p, ESP2 + 48 + ESP2, y, 48, h, "", Botao::SECUNDARIO );
  p.icone( IconeNome::CHEVRON_RIGHT, 20, ESP2 + 48 + ESP2 + 14, y + 10, T.tinta );
  char buf[24];
  contagemDoPaginador( buf, (int)sizeof buf, pagina, total );
  textoCentrado( p, buf, ESP2 + 104 + ESP3, y, h, tipo::apoio, T.tinta2, 'l' );
  if ( acao ) botao( p, 320 - ESP2 - acao->w, y, acao->w, h, acao->rotulo, acao->tipo );
}

void rodapeAcoes( Pintor &p, const char *primaria, Botao primariaTipo, const char *secundaria, Caixa16 *cxP,
                  Caixa16 *cxS ) {
  const int16_t y = 196, h = 40;
  if ( !secundaria ) secundaria = UA_TXT( "Cancel" );   // the default (Angulo.h): looked up once, measured and drawn from this one pointer below
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

const char *separadorDecimal( ) {
  // The key is words and the English separator is what an unknown key gives back: tools/check_ui_texts.py and tools/check_lang_packs.py read only the literals that
  // hold a letter, so a UA_TXT( "." ) would be none of their texts and the pack's line for it would match nothing (measured 2026-10-22, B5a). With no pack, or a
  // pack with no line for it, txt( ) returns the 17 bytes of the key itself. A pack gives "," or "."; anything else is the point: the review of B5a planted "5",
  // which a guard by length took, and 13,8 drew as 1358.
  const char *s = UA_TXT( "Decimal separator" );
  return ( s[0] == ',' || s[0] == '.' ) && s[1] == 0 ? s : ".";
}

char *n1( int32_t d, char *buf ) {
  // toFixed(1) of a value already rounded to tenths; -0 prints as 0,0 (0.0 in English).
  const bool neg = d < 0;
  const int32_t a = neg ? -d : d;
  snprintf( buf, 16, "%s%ld%s%ld", neg ? "-" : "", (long)( a / 10 ), separadorDecimal( ), (long)( a % 10 ) );
  return buf;
}

}  // namespace ua
