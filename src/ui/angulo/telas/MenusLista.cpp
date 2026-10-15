// What the "menus" screens share: the pager, "Voltar", the paged list's boxes
// and the text helpers. See MenusLista.h.
#include "MenusLista.h"

#include <stdio.h>
#include <string.h>

namespace ua {
namespace tela {

namespace {

// "…" and its terminator.
const char kReticencias[] = "\xE2\x80\xA6";

// An arrow of the pager: a square secondary button with a chevron. With no
// page to go to the button and the chevron are drawn disabled.
void seta( Pintor &p, const Caixa16 &c, IconeNome icone, bool ativa ) {
  const Tema &T = p.t( );
  botao( p, c.x, c.y, c.w, c.h, "", Botao::SECUNDARIO, !ativa );
  p.icone( icone, ICONE_CONTROLE, c.x + rd2( c.w - ICONE_CONTROLE ), c.y + rd2( c.h - ICONE_CONTROLE ),
           ativa ? T.tinta : desab( T, T.tinta ) );
}

// "n de m", 1-based: the page the user is on.
void rotuloPaginas( const Paginas &g, char *out, int cap ) { snprintf( out, (size_t)cap, "%d de %d", g.pagina + 1, g.total ); }

}  // namespace

/* ------------------------------------------------------------------ */
/* The pager                                                            */
/* ------------------------------------------------------------------ */

Paginas paginasDe( int pagina, int total ) {
  Paginas g;
  g.total = total < 1 ? 1 : total;
  g.pagina = pagina < 0 ? 0 : pagina >= g.total ? g.total - 1 : pagina;
  g.visivel = g.total > 1;
  g.anteriorAtiva = g.pagina > 0;
  g.proximaAtiva = g.pagina < g.total - 1;
  return g;
}

void desenharPaginador( Pintor &p, const Paginas &g ) {
  if ( !g.visivel ) return;
  const Tema &T = p.t( );
  const Caixa16 e = paginadorEsquerda( ), d = paginadorDireita( );
  seta( p, e, IconeNome::CHEVRON_LEFT, g.anteriorAtiva );
  seta( p, d, IconeNome::CHEVRON_RIGHT, g.proximaAtiva );
  char rotulo[24];
  rotuloPaginas( g, rotulo, sizeof rotulo );
  // Where the kit's paginador( ) puts it: ESP3 after the right arrow.
  textoCentrado( p, rotulo, d.x + d.w + ESP3, e.y, e.h, tipo::apoio, T.tinta2, 'l' );
}

int16_t fimDoPaginador( const Paginas &g ) {
  if ( !g.visivel ) return PAD;
  char rotulo[24];
  rotuloPaginas( g, rotulo, sizeof rotulo );
  const Caixa16 d = paginadorDireita( );
  return (int16_t)( d.x + d.w + ESP3 + larguraTinta( rotulo, tipo::apoio ) );
}

int alvosPaginador( const Paginas &g, Alvo *out, int n ) {
  if ( !g.visivel ) return n;
  if ( g.anteriorAtiva ) n = addAlvo( out, n, paginadorEsquerda( ), LST_PAG_ANTERIOR );
  if ( g.proximaAtiva ) n = addAlvo( out, n, paginadorDireita( ), LST_PAG_PROXIMA );
  return n;
}

/* ------------------------------------------------------------------ */
/* Title bar                                                            */
/* ------------------------------------------------------------------ */

Caixa16 alvoVoltar( ) {
  // The action barraTituloVoltar( ) draws: the width depends only on its label
  // and icon. The box starts ESP2 before the button, goes on to the edge of the
  // glass, and covers the bar and the 8 px under its line (down to Y0, where the
  // content starts), the 20 px button being small for a finger.
  Acao a;
  a.rotulo = "Voltar";
  a.icone = IconeNome::ARROW_LEFT;
  const int16_t x = W_TELA - PAD - larguraAcao( a ) - ESP2;
  return Caixa16{ x, 0, (int16_t)( W_TELA - x ), Y0 };
}

int16_t larguraTitulo( ) { return alvoVoltar( ).x - PAD; }

void tituloCortado( const char *nome, char *out, int cap ) { cortar( nome, tipo::subtitulo, larguraTitulo( ), out, cap ); }

/* ------------------------------------------------------------------ */
/* A list in a card                                                     */
/* ------------------------------------------------------------------ */

LayoutLista layoutLista( int total, int pagina, int filas, int16_t hFila ) {
  if ( filas > LISTA_FILAS ) filas = LISTA_FILAS;      // LayoutLista holds no more than that
  if ( filas < 1 ) filas = 1;
  LayoutLista l{ };
  l.total = total < 0 ? 0 : total;
  l.pag = paginasDe( pagina, ( l.total + filas - 1 ) / filas );
  l.primeira = l.pag.pagina * filas;
  l.n = l.total - l.primeira;
  if ( l.n > filas ) l.n = filas;
  if ( l.n < 0 ) l.n = 0;
  l.voltar = alvoVoltar( );
  for ( int i = 0; i < l.n; i++ )
    l.linhas[i] = Caixa16{ ESP2, (int16_t)( Y0 + i * hFila ), (int16_t)( W_TELA - 2 * ESP2 ), hFila };
  return l;
}

int alvosLista( const LayoutLista &l, Alvo *out, bool linhasSaoAlvos ) {
  int n = addAlvo( out, 0, l.voltar, LST_VOLTAR );
  if ( linhasSaoAlvos )
    for ( int i = 0; i < l.n; i++ ) n = addAlvo( out, n, l.linhas[i], LST_LINHA, (int16_t)( l.primeira + i ) );
  return alvosPaginador( l.pag, out, n );
}

/* ------------------------------------------------------------------ */
/* Text and notes                                                       */
/* ------------------------------------------------------------------ */

void cortar( const char *s, Fonte f, int16_t largura, char *out, int cap ) {
  copiar( out, s, cap );
  if ( larguraTinta( out, f ) <= largura ) return;
  int n = (int)strlen( out );
  while ( n > 0 ) {
    // One character less each time, never inside a UTF-8 sequence, and no
    // space left in front of the ellipsis. Only bytes from n up are
    // overwritten, so the prefix stays the original for the next try.
    do n--; while ( n > 0 && ( (uint8_t)out[n] & 0xC0 ) == 0x80 );
    while ( n > 0 && out[n - 1] == ' ' ) n--;
    if ( n + (int)sizeof kReticencias > cap ) continue;
    memcpy( out + n, kReticencias, sizeof kReticencias );
    if ( larguraTinta( out, f ) <= largura ) return;
  }
  // Not even "…" fits: an empty string never leaves its box.
  out[0] = 0;
}

void desenharNota( Pintor &p, int16_t y, const char *texto ) {
  textoCentrado( p, texto, ESP2, y, ESP4, tipo::apoio, p.t( ).tinta2, 'l' );
}

void desenharDuasLinhas( Pintor &p, const Caixa16 &c, int16_t x, const char *linha1, uint16_t cor1,
                         const char *linha2, Fonte fonte2, uint16_t cor2 ) {
  // Two boxes of 20 px, 4 px under the row's top and the first's bottom: the caps of the first
  // line then start 7 px under the row's edge and the baseline of the second is 7 above its foot.
  constexpr int16_t H_CAIXA = 20, Y_CAIXA_1 = ESP1;
  textoCentrado( p, linha1, x, c.y + Y_CAIXA_1, H_CAIXA, tipo::corpo, cor1, 'l' );
  textoCentrado( p, linha2, x, c.y + Y_CAIXA_1 + H_CAIXA, H_CAIXA, fonte2, cor2, 'l' );
}

int16_t desenharVazio( Pintor &p, const char *texto ) {
  const Tema &T = p.t( );
  // Sentence lines, at the 20 px pitch of the result screen's text.
  constexpr int16_t PASSO = 20;
  constexpr int MAX = 3;
  char linhas[MAX][96];
  const int16_t largura = W_TELA - 2 * ESP2 - 2 * PAD;
  const int n = quebrar( texto, tipo::corpo, largura, &linhas[0][0], sizeof linhas[0], MAX );
  const int16_t h = 2 * PAD + n * PASSO;
  superficie( p, ESP2, Y0, W_TELA - 2 * ESP2, h );
  for ( int i = 0; i < n; i++ )
    textoCentrado( p, linhas[i], ESP2 + PAD, Y0 + PAD + i * PASSO, PASSO, tipo::corpo, T.tinta, 'l' );
  return Y0 + h;
}

}  // namespace tela
}  // namespace ua
