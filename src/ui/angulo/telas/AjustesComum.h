#pragma once
// What the ajustes group's screens share (ESPEC_UI_A.md §6.3): the geometry of
// the footer and of the title bar's way out, the plan's stepper card, and the
// square icon button.
//
// The kit draws the footer and the title bar but has no box-only form of
// either, and alvos*( ) has no Pintor to draw with. So the boxes are computed
// here, once, with the kit's own arithmetic (rodapeAcoes( ), larguraAcao( )),
// and desenhar*( ) paints from the very same boxes with botao( ), the way
// Base.cpp does: a target cannot drift away from the button it belongs to.
//
// Everything lives in ua::tela::ajustes so that no helper here can collide at
// link time with another group's helper of the same name.
#include "Telas.h"

namespace ua {
namespace tela {
namespace ajustes {

// The panel's width and its middle column.
constexpr int16_t L_TELA = 320, X_MEIO = L_TELA / 2;

// The rows the kit fixes: rodapeAcoes( ) and paginador( ) stand at y 196, 40 px
// tall; barraTitulo( ) is 28 px tall by default.
constexpr int16_t Y_RODAPE = 196, H_RODAPE = 40, H_BARRA = 28;

// The kit's icon size in buttons and list rows, and its switch (interruptor( )
// draws 40 x 24 and has no constants for it).
constexpr int16_t L_ICONE = 20, L_CHAVE = 40, H_CHAVE = 24;

// The plan's square buttons (the stepper's minus and plus, the alignment arrows).
constexpr int16_t L_BOTAO = 40;

// Between the baselines of 9 pt text, the panel's smallest (tipo::apoio, tipo::rotulo,
// since 2026-10-15): the capitals are 14 px, so the lines keep 4 px apart (the 7 pt
// layout's 16 px kept 5 for its 11 px capitals).
constexpr int16_t PASSO_APOIO = ESP4 + 2;

// The plan's stepper card: 8 above the button, the button, 8 below.
constexpr int16_t H_CARTAO = ESP2 + L_BOTAO + ESP2;

/* ------------------------------------------------------------------ */
/* Title bar                                                            */
/* ------------------------------------------------------------------ */

// The hit box of the bar's "Voltar": the drawn button (20 px tall) widened by
// 4 px each side and stretched to the whole bar, so a thumb finds it.
Caixa16 caixaVoltar( );

// "<antes><nome><depois>" with the name cut by "…" so that the title still
// clears the bar's "Voltar" in the kit's smaller face (barraTitulo( ) falls to
// it when the title does not fit in the larger one). A sensor name has up to 31
// characters, which the bar does not cut by itself.
void montarTitulo( char *dst, int cap, const char *antes, const char *nome, const char *depois );

/* ------------------------------------------------------------------ */
/* Footer                                                               */
/* ------------------------------------------------------------------ */

// The footer's two actions: the primary at the right and the discreet one to
// its left (secundaria == nullptr: the primary alone, whose box then stands
// alone and `secundaria` is all zeros: not a target).
struct CaixasRodape { Caixa16 primaria, secundaria; };
CaixasRodape caixasRodape( const char *primaria, const char *secundaria, int16_t y = Y_RODAPE );
void desenharRodape( Pintor &p, const CaixasRodape &c, const char *primaria, Botao tipo, bool primariaDesabilitada,
                     const char *secundaria );

/* ------------------------------------------------------------------ */
/* Buttons and cards                                                    */
/* ------------------------------------------------------------------ */

// A secondary button with a 20 px icon in its middle; `ativo` false draws it
// disabled (the stepper at the end of its range, the pager without a page to go to).
void botaoIcone( Pintor &p, const Caixa16 &c, IconeNome ic, bool ativo );

// The segment `i` of a control of `n` segments over `w` px, 4 px between, `h` tall.
// The kit's segmentado( ) floors each segment's width and leaves the remainder
// blank after the last; here the first segments take it, a pixel each, so that a
// control ends where its row's other controls do. With `w` 280 and n = 3 the
// segments are 91, 91 and 90 px, and the alphabet control of the PIN policy (185 px,
// n = 2) stands exactly under the keypad's last two (2026-10-15).
Caixa16 segmento( int16_t x, int16_t y, int16_t w, int16_t h, int n, int i );

// The kit's segmentado( ) in the regular small face. Its bold one is 87 px of ink
// for "18 cartões" at 9 pt, which in the 90 px segments the PIN policy has room for
// leaves 1.5 px of air against the active segment's line; the regular face is 78 (6
// px). Same colours; the boxes are segmento( )'s.
void segmentadoLeve( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const char *const *opcoes, int16_t n,
                     int16_t ativa );

// The plan's stepper card (telas.js tela_alarme_editar): the label, the value
// in display type with its unit, and the minus and plus buttons at the right.
struct CaixasStepper { Caixa16 cartao, menos, mais; };
CaixasStepper caixasStepper( int16_t y );
// `foco` puts the accent on the card's line: the card touched last.
void desenharStepper( Pintor &p, const CaixasStepper &c, const char *rotulo, const char *valor, const char *unidade,
                      bool foco, bool menosAtivo, bool maisAtivo );

}  // namespace ajustes
}  // namespace tela
}  // namespace ua
