#pragma once
// The "menus" group of the functional build's screens (tools/tft_ui/ESPEC_UI_A.md
// §6.2): the Settings menu, theme, language, alarm limits, one sensor's alarm
// menu, system status and the licence. Almost all of them are one screen with
// other rows: "Voltar" in the title bar, rows of 40 px in a card, four to a
// page, and the pager under the card. This header is what they share; each
// screen's view model is in MenusConfig.h, MenusAlarmes.h and MenusStatus.h.
//
// What the glue has to know about the pager: the arrows do not wrap. On the
// first page "previous" is drawn disabled and is an inert target; on the last one
// the same holds for "next". The old screens' up and down arrows wrapped
// around; a page that stops is what a pager with a visible "n de m" promises.
#include "Telas.h"

namespace ua {
namespace tela {

// The panel's width, which the kit spells 320 wherever it needs it.
constexpr int16_t W_TELA = 320;
// The title bar's height: barraTitulo( )'s default, and the line under it.
constexpr int16_t H_BARRA = 28;
// Icon size inside a 40 px control (a row, a pager arrow).
constexpr int16_t ICONE_CONTROLE = 20;
// A badge in a row: the kit's lista( ) draws it at this height.
constexpr int16_t H_SELO = 20;

constexpr int LISTA_FILAS = 4;          // rows to a page: y 36..195, the pager at 196
constexpr int16_t LISTA_H_FILA = 40;

// A row of two lines, a label over its value or its note: 48 px, three to a page (y 36..179). At
// 9 pt, two lines of 14 px caps in 40 px left 4 px above, between and below them, and the first
// line's descenders touched the second line (2026-10-15); in 48 px they are 7 above and below, and
// 6 between the first baseline and the next line's caps.
constexpr int LISTA_FILAS_2 = 3;
constexpr int16_t LISTA_H_FILA_2 = 48;

// The targets of every list screen of the group. LST_LINHA's indice is the
// row's position in the whole list, not on the page. Rows that only show
// something (the status table, the licence) are not targets.
enum AlvoLista : uint8_t { LST_NADA, LST_VOLTAR, LST_LINHA, LST_PAG_ANTERIOR, LST_PAG_PROXIMA };

/* ------------------------------------------------------------------ */
/* The pager                                                            */
/* ------------------------------------------------------------------ */

struct Paginas {
  int pagina;            // 0-based, held to the pages that exist
  int total;             // pages, at least 1
  bool visivel;          // more than one page: the pager is drawn
  bool anteriorAtiva;    // there is a page before this one
  bool proximaAtiva;     // there is a page after it
};
Paginas paginasDe( int pagina, int total );

// The two arrows (a disabled one when there is no page to go to, which the
// kit's paginador( ) cannot show) and "n de m". Draws nothing for one page.
void desenharPaginador( Pintor &p, const Paginas &g );
// Appends the arrows that work; returns the new count.
int alvosPaginador( const Paginas &g, Alvo *out, int n );
// Where the pager's drawing ends (the right ink edge of "n de m"), for what
// shares its row; PAD when the pager is not drawn.
int16_t fimDoPaginador( const Paginas &g );

/* ------------------------------------------------------------------ */
/* Title bar                                                            */
/* ------------------------------------------------------------------ */

// "Voltar"'s touch box: the drawn button plus a margin to its left, as tall as
// the bar. The button is 20 px tall; a fingertip needs the room.
Caixa16 alvoVoltar( );
// The widest a title can be in the bar without reaching "Voltar" (the kit
// does not cut a title).
int16_t larguraTitulo( );
// A title from a name the user typed, cut with "…" to fit the bar in the
// kit's smaller title font, the one barraTitulo( ) falls to.
void tituloCortado( const char *nome, char *out, int cap );

/* ------------------------------------------------------------------ */
/* A list in a card, four rows to a page                                */
/* ------------------------------------------------------------------ */

struct LayoutLista {
  Paginas pag;
  int total;                           // rows in the whole list
  int primeira;                        // position, in the whole list, of this page's first row
  int n;                               // rows on this page
  Caixa16 voltar;
  Caixa16 linhas[LISTA_FILAS];         // the rows on this page
};
// `filas` rows to a page of `hFila` px each: LISTA_FILAS of LISTA_H_FILA, or LISTA_FILAS_2 of
// LISTA_H_FILA_2 for the rows of two lines.
LayoutLista layoutLista( int total, int pagina, int filas = LISTA_FILAS, int16_t hFila = LISTA_H_FILA );
// Voltar, the rows (when they are targets: LST_LINHA, indice = position in the
// whole list) and the arrows that work.
int alvosLista( const LayoutLista &l, Alvo *out, bool linhasSaoAlvos );

/* ------------------------------------------------------------------ */
/* Text and notes                                                       */
/* ------------------------------------------------------------------ */

// Copies s into out (cap bytes), cut at a character and ended with "…" when
// its ink is wider than `largura` in font f. Never cuts a UTF-8 sequence.
void cortar( const char *s, Fonte f, int16_t largura, char *out, int cap );

// A line of small text under a card, aligned with the card's edge.
void desenharNota( Pintor &p, int16_t y, const char *texto );

// The two lines of a row of LISTA_H_FILA_2 whose box is c, from x: each in a box of 20 px (the
// pitch of `corpo`), the pair centred in the row. The first is the label (`corpo`), the second its
// value or note.
void desenharDuasLinhas( Pintor &p, const Caixa16 &c, int16_t x, const char *linha1, uint16_t cor1,
                         const char *linha2, Fonte fonte2, uint16_t cor2 );

// The empty list: the same card with a sentence in `corpo` that says what to
// do (Ângulo §4.4). Returns the y after the card.
int16_t desenharVazio( Pintor &p, const char *texto );

}  // namespace tela
}  // namespace ua
