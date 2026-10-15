#pragma once
// What the graph group's screens share. Internal to Graficos*.cpp: the glue
// includes Graficos.h only.
#include "Graficos.h"

namespace ua {
namespace tela {
namespace graficos {

// The middle of the glass, the boxes the plan centres a line of body text and a
// line of support text in, and the three icon sizes the kit has.
constexpr int16_t X_CENTRO = 160;
constexpr int16_t H_CORPO = 20, H_APOIO = 16;
constexpr int16_t ICONE_P = 16, ICONE_M = 20, ICONE_G = 24;

// The title bar's height (the kit's default) and where it puts an action.
constexpr int16_t H_BARRA = 28;
constexpr int16_t Y_ACAO = 4, H_ACAO = 20;
constexpr int16_t H_OCUPADO = 3;   // the busy line's thickness: the old hint's 3 px

// "1 h", "6 h", "12 h", "24 h", "7 d" for 0..4 (anything else is the last).
const char *rotuloPeriodo( int periodo );

// "<nome> · <período>" in dst, cut with "…" so that it fits beside the n actions
// of the title bar in the smaller title font (the kit falls to it when the title
// does not fit). A screen that shows the period some other way (the chart's
// chips) lets it go first when the whole does not fit, so the name is cut last;
// the loading screen has no other way and cuts the name. cap >= 64.
void tituloComPeriodo( char *dst, int cap, const char *nome, int periodo, const Acao *acoes, int n, bool periodoPodeSair );

// The boxes barraTitulo( ) gives its n actions, left to right. barraTitulo( )
// only reports them while it draws, and a screen's targets are listed without
// drawing, so this mirrors its placement; conferirCaixas( ) holds the two equal.
void caixasDasAcoes( const Acao *acoes, int n, Caixa16 *out );

// On the host, stops the harness when the boxes the kit reported while it drew
// a title bar are not the ones caixasDasAcoes( ) gave the targets; on the
// device, nothing.
#ifdef UA_HOST
void conferirCaixas( const char *tela, const Caixa16 *kit, const Caixa16 *meu, int n );
#else
inline void conferirCaixas( const char *, const Caixa16 *, const Caixa16 *, int ) { }
#endif

// The band between the title bar's targets (they end at y 31) and the footer (y 196): the chart and the detail answer every
// pixel of it, as the original's central zone did (0,40 320x155, "Tap on central zone" in DisplayManager_Touch.cpp), not only the
// card or the cards. It overlaps no other target, so no touch in it can reach a control by the last-listed rule.
constexpr int16_t Y_BANDA = H_BARRA + ESP1, Y_RODAPE = 196;
inline Caixa16 bandaCentral( ) { return Caixa16{ 0, Y_BANDA, 320, (int16_t)( Y_RODAPE - Y_BANDA ) }; }

// A title bar action's touch target: the drawn box is 20 px tall, so the target
// takes the whole bar and 4 px under its line, and 4 px at each side, where the
// neighbours' targets meet it without overlapping; the action at the right margin
// reaches the edge of the glass. It stops at y 32 and not at Y0: the first text
// of the chart and of the calendar (the stats, the weekday row) has its ink from
// y 34, and a target that a text crosses fails host_telas.py.
Caixa16 alvoDaAcao( const Caixa16 &c );

// "14:40", "13/10" and "13/10 14:40".
char *horaMin( const Instante &t, char *buf, int cap );
char *diaMes( const Instante &t, char *buf, int cap );
char *diaMesHoraMin( const Instante &t, char *buf, int cap );

// The footer's pager, drawn as the kit's paginador( ) with the arrow that has
// nowhere to go disabled, and the page's name after "n de m" when `nome` is
// given. pagina is 1-based.
void paginadorComEstados( Pintor &p, int16_t pagina, int16_t total, const char *nome );

// The "busy" line: the Core 0 is reading what a tap asked for and the screen
// still shows the old one. A thin accent line right under the title bar, over
// its hairline, as the old panel put one on the header (drawGraphBusyHint( ),
// DisplayManager_Graph.cpp), which in this build only marks the frame dirty.
void linhaOcupado( Pintor &p );

// "Sem dados neste período." centred in `area`, a line of what to do under it.
void semDados( Pintor &p, const Caixa16 &area, const char *dica );

}  // namespace graficos
}  // namespace tela
}  // namespace ua
