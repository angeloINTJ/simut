#pragma once
// What the identity screens share (Identidade*.cpp): a title bar whose actions
// can be dimmed and whose title is cut to fit, the cut of a long name with "…",
// the pager with its arrows dimmed at the ends, the empty state of a list, and
// the footer's boxes. Header-only: three translation units use it, and the
// boxes it computes are the ones both desenhar*( ) and alvos*( ) read.
//
// Why the bar is not the kit's barraTitulo( ): that one has no disabled action
// ("Nova conta" without the permission to manage accounts) and does not cut a
// title that is too wide even in the smaller type (an account name of 15
// letters beside "Voltar"). For a title that fits it draws the same pixels:
// compared on 2026-10-14 with barraTituloVoltar( ) for four titles and with
// barraTitulo( ) for "Usuários" and "Nova conta" + "Voltar", in both themes,
// 0 differing pixels of 36 rows; the footer against rodapeAcoes( ), also 0.
// A title that does not fit beside two actions is cut with "…" here, where the
// kit's would be drawn under the buttons.
#include <string.h>

#include "Telas.h"

namespace ua {
namespace tela {
namespace ident {

// Bytes of an account name in a view model. The device keeps 15 + NUL
// (UserAccount::username[16], SystemDefs_Records.h:170); the rest is room to
// spare, nothing on the device keeps more than 15 for an account.
constexpr int NOME = 32;

// Rows of the lists of accounts of one line: 4 of 40 px from Y0 (36) to 196, the pager's row below.
constexpr int LINHAS = 4;
constexpr int16_t H_LINHA = 40;

// The list of accounts (Usuários) is the exception: its rows hold two lines, the name over what the
// account may do, in 48 px and three to a page (y 36..180), as the alarm list's rows (MenusLista.h,
// LISTA_FILAS_2). At the 9 pt text floor (2026-10-15) the 40 px row left 4 px between the two lines
// and the name's descenders touched the summary under it; in 48 px the two 20 px boxes leave 7 px
// above and below, and 6 between the name's baseline and the summary's caps.
constexpr int LINHAS_2 = 3;
constexpr int16_t H_LINHA_2 = 48;

inline int paginasDe( int total, int porPagina = LINHAS ) { return total > 0 ? ( total + porPagina - 1 ) / porPagina : 1; }

/* ------------------------------------------------------------------ */
/* Text                                                                 */
/* ------------------------------------------------------------------ */

// Copies src into dst (cap bytes), cut with "…" so that its ink is at most
// `largura` px in font f. Never cuts a UTF-8 sequence. dst must hold the
// source plus the 2 bytes the ellipsis can add.
inline void cortar( const char *src, Fonte f, int16_t largura, char *dst, int cap ) {
  copiar( dst, src, cap );
  if ( larguraTinta( dst, f ) <= largura ) return;
  static const char reticencias[] = "\xE2\x80\xA6";
  int n = (int)strlen( dst );
  while ( n > 0 ) {
    do n--; while ( n > 0 && ( (uint8_t)dst[n] & 0xC0 ) == 0x80 );
    if ( n + 4 > cap ) continue;                    // the ellipsis would not fit the buffer
    char prova[96];
    if ( n + 4 > (int)sizeof prova ) continue;
    memcpy( prova, dst, (size_t)n );
    memcpy( prova + n, reticencias, 4 );
    if ( larguraTinta( prova, f ) <= largura ) { memcpy( dst, prova, (size_t)n + 4 ); return; }
  }
  dst[0] = 0;
}

// Appends s to dst (cap bytes), whole or not at all.
inline void anexar( char *dst, int cap, const char *s ) {
  const int n = (int)strlen( dst ), m = (int)strlen( s );
  if ( n + m + 1 > cap ) return;
  memcpy( dst + n, s, (size_t)m + 1 );
}

/* ------------------------------------------------------------------ */
/* Title bar                                                            */
/* ------------------------------------------------------------------ */

struct AcaoBarra { const char *rotulo; Botao tipo; IconeNome icone; bool desab; };
constexpr int MAX_ACOES = 2;

// The bar keeps the actions its boxes were measured from, labels included, and desenharBarra( ) draws those and no others: a label is looked up once, where
// the layout is made, and a box cannot be measured from one text while another is drawn (B3-b and B3-c found that defect for the kit's "Voltar" and the
// pager's count; B4b closed it for the Ajustes' footer, B5b closes it for these bars). The label pointers are the caller's: a UA_TXT( ) answer lives as long
// as the pack's table, which is never replaced or freed while the device runs (Textos.h).
struct Barra {
  Caixa16 caixas[MAX_ACOES];   // the actions, left to right (20 px tall, as the kit's)
  AcaoBarra acoes[MAX_ACOES];  // the actions the boxes were measured from
  int n;
  int16_t ocupado;             // the kit's measure: every action's width and a gap
  int16_t cabe;                // ink room for the title, up to 8 px short of the first action
};

inline Barra layoutBarra( const AcaoBarra *acoes, int n ) {
  Barra b{ };
  b.n = n;
  int16_t x = 320 - PAD;
  for ( int i = n - 1; i >= 0; i-- ) {
    Acao a;
    a.rotulo = acoes[i].rotulo;
    a.tipo = acoes[i].tipo;
    a.icone = acoes[i].icone;
    b.acoes[i] = acoes[i];
    const int16_t w = larguraAcao( a );
    x -= w;
    b.caixas[i] = Caixa16{ x, 4, w, 20 };
    b.ocupado += (int16_t)( w + ESP2 );
    x -= ESP2;
  }
  // x is 8 px left of the first action (or the right margin without actions).
  b.cabe = (int16_t)( x - PAD );
  return b;
}

// The title in the type the kit's barraTitulo( ) would pick (display type,
// the smaller one when it does not fit beside the actions) and cut with "…"
// when even that is too wide. Returns the type.
inline Fonte tituloDaBarra( const Barra &b, const char *titulo, char *saida, int cap ) {
  Fonte f = tipo::titulo;
  if ( larguraTinta( titulo, f ) + PAD + ESP2 + b.ocupado > 320 - PAD ) f = tipo::subtitulo;
  cortar( titulo, f, b.cabe, saida, cap );
  return f;
}

inline void desenharBarra( Pintor &p, const Barra &b, const char *titulo ) {
  const Tema &T = p.t( );
  char t[64];
  const Fonte f = tituloDaBarra( b, titulo, t, sizeof t );
  textoCentrado( p, t, PAD, 0, H_STATUS, f, T.tinta, 'l' );
  for ( int i = 0; i < b.n; i++ ) {
    const Caixa16 &c = b.caixas[i];
    botao( p, c.x, c.y, c.w, c.h, b.acoes[i].rotulo, b.acoes[i].tipo, b.acoes[i].desab, b.acoes[i].icone );
  }
  p.hline( 0, H_STATUS, 320, T.linha );
}

// A bar button is drawn 20 px tall, too small for a finger: its target is the
// whole bar (y 0 to Y0), reaching half the gap to its neighbour, and the last
// one reaches the edge of the glass. `tipos[i]` is the action's target type;
// an action that is not `ativa` is drawn disabled and listed inert, with the box it would have. Returns the new count.
inline int addAlvosBarra( Alvo *out, int n, const Barra &b, const uint8_t *tipos, const bool *ativa ) {
  // The title, inert, up to where the first action's target begins (areaDoTitulo( ) in Telas.h).
  if ( b.n > 0 ) n = addAlvo( out, n, areaDoTitulo( (int16_t)( b.caixas[0].x - ESP2 ) ), ALVO_INERTE );
  for ( int i = 0; i < b.n; i++ ) {
    const int16_t x0 = i == 0 ? (int16_t)( b.caixas[i].x - ESP2 ) : (int16_t)( b.caixas[i].x - ESP1 );
    const int16_t x1 = i == b.n - 1 ? 320 : (int16_t)( b.caixas[i + 1].x - ESP1 );
    n = addAlvo( out, n, x0, 0, (int16_t)( x1 - x0 ), Y0, ativa[i] ? tipos[i] : ALVO_INERTE );
  }
  return n;
}

/* ------------------------------------------------------------------ */
/* Pager, footer, empty state                                           */
/* ------------------------------------------------------------------ */

// The kit's pager always draws both arrows alive. At the first and the last
// page the arrow that goes nowhere is drawn dimmed over it (the kit has no
// disabled state per arrow), and it is an inert target.
inline void paginadorEstado( Pintor &p, int16_t pagina, int16_t total ) {
  const Tema &T = p.t( );
  paginador( p, (int16_t)( pagina + 1 ), total );
  if ( pagina <= 0 ) {
    const Caixa16 c = paginadorEsquerda( );
    botao( p, c.x, c.y, c.w, c.h, "", Botao::SECUNDARIO, true );
    p.icone( IconeNome::CHEVRON_LEFT, 20, c.x + 14, c.y + 10, desab( T, T.tinta ) );
  }
  if ( pagina + 1 >= total ) {
    const Caixa16 c = paginadorDireita( );
    botao( p, c.x, c.y, c.w, c.h, "", Botao::SECUNDARIO, true );
    p.icone( IconeNome::CHEVRON_RIGHT, 20, c.x + 14, c.y + 10, desab( T, T.tinta ) );
  }
}

// The pager's targets: an arrow that goes nowhere is listed inert, where it is drawn dimmed.
inline int addAlvosPaginador( Alvo *out, int n, int pagina, int paginas, uint8_t anterior, uint8_t proxima ) {
  n = addAlvoSe( out, n, pagina > 0, paginadorEsquerda( ), anterior );
  return addAlvoSe( out, n, pagina + 1 < paginas, paginadorDireita( ), proxima );
}

// The footer of the editors: the primary action at the right, the discreet
// one beside it (rodapeAcoes( )'s boxes, 40 px tall at y 196). Drawn here from
// the boxes so that what is drawn is what is touched. The struct keeps the very labels its
// boxes were measured from, and desenharRodape( ) draws those and no others (the Ajustes'
// CaixasRodape, B4b): a label is looked up once, where the layout is made.
struct Rodape { Caixa16 primaria, secundaria; const char *rotuloPrimaria, *rotuloSecundaria; };

inline Rodape layoutRodape( const char *primaria, const char *secundaria ) {
  const int16_t y = 196, h = 40;
  const int16_t wP = larguraTinta( primaria, tipo::botao ) + 2 * ESP4;
  const int16_t wS = larguraTinta( secundaria, tipo::botao ) + 2 * ESP2;
  Rodape r;
  r.primaria = Caixa16{ (int16_t)( 320 - ESP2 - wP ), y, wP, h };
  r.secundaria = Caixa16{ (int16_t)( 320 - ESP2 - wP - ESP2 - wS ), y, wS, h };
  r.rotuloPrimaria = primaria;
  r.rotuloSecundaria = secundaria;
  return r;
}

inline void desenharRodape( Pintor &p, const Rodape &r ) {
  botao( p, r.primaria.x, r.primaria.y, r.primaria.w, r.primaria.h, r.rotuloPrimaria, Botao::PRIMARIO );
  botao( p, r.secundaria.x, r.secundaria.y, r.secundaria.w, r.secundaria.h, r.rotuloSecundaria, Botao::DISCRETO );
}

// A list with nothing in it: an icon, what is missing, and what to do, centred
// in the space the rows would take (y 36 to 196).
inline void estadoVazio( Pintor &p, const char *titulo, const char *detalhe ) {
  const Tema &T = p.t( );
  const int16_t y = 80;
  p.icone( IconeNome::USERS, 20, 160 - 10, y, T.tinta2 );
  textoCentrado( p, titulo, 160, y + 20 + ESP2, 20, tipo::subtitulo, T.tinta, 'c' );
  textoCentrado( p, detalhe, 160, y + 20 + ESP2 + 20 + ESP1, 20, tipo::corpo, T.tinta2, 'c' );
}

}  // namespace ident
}  // namespace tela
}  // namespace ua
