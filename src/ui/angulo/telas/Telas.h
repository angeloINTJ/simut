#pragma once
// The functional build's screens: family A of the TFT plan (docs/ui_tft_angulo,
// the delivery of 2026-10), drawn by the kit (../Angulo.h) from a view model.
//
// Each screen is a pure function of its view model, so the host draws every
// one of them with sample data in both themes (tools/tft_ui/host_telas.py) and
// the firmware draws the very same code from the DisplayManager's state
// (DisplayManager_UiA*.cpp, built only by pico_wiretft_angulo). What each screen
// keeps:
//
//  - Its view model (Vm*) is a POD of fixed-size members and no pointers. The
//    firmware hashes its bytes to repaint only when something on the glass
//    would change, so the builder zeroes it (memset) before filling it, and
//    nothing in it may point at memory that changes under it.
//  - One layout computation gives every box the screen paints AND touches:
//    desenhar*( ) paints from it and alvos*( ) lists the touch targets from it,
//    so a target cannot drift away from what is drawn.
//  - Its texts are pt-BR, UTF-8 (the kit draws UTF-8; the device's tr( )
//    returns Latin-1 for the old fonts and is not used by these screens).
#include <stdint.h>

#include "../Angulo.h"

namespace ua {

// A touch target: a box and what it means to its screen. `tipo` is the
// screen's own enum, whose 0 is ALVO_INERTE; `indice` a row, key or column
// (-1 when the type says it all).
struct Alvo {
  int16_t x, y, w, h;
  uint8_t tipo;
  int16_t indice;
};

// The type of a control that is drawn but cannot be pressed now: a disabled button, the arrow of a pager that has
// nowhere to go. Every screen's enum starts with XXX_NADA = 0, so the type needs no new number, and the screen lists
// the control with the box it would have enabled. Without it a touch on the greyed control was answered by the
// nearest enabled one within FOLGA_TOQUE: on the chart at the present the left part of the greyed next arrow
// (276,196 36x40) answered the "7 d" chip beside it (228,196 40x40), and on the detail's first page the greyed
// previous arrow answered the next one, the opposite direction (tools/tft_ui/test_alvos.py reproduces both).
// alvoEm( ) treats an inert target like any other; what the glue does with its answer is nothing (alvoDoToque( )).
// tools/tft_ui/host_telas.py fails a sample that draws a control with no target, or an enabled one held only by
// inert targets, and reads this number from here.
constexpr uint8_t ALVO_INERTE = 0;

// More than any screen has: the largest list of any sample is 38 (the text keyboard, texto_senha_legada) and the calendar's is 34
// (calendario_bissexto), measured on 2026-10-15; tools/tft_ui/host_telas.py fails a sample whose list fills this or drops a target.
constexpr int MAX_ALVOS = 48;

// Appends a target; returns the new count (n unchanged when out is full).
int addAlvo( Alvo *out, int n, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t tipo, int16_t indice = -1 );
inline int addAlvo( Alvo *out, int n, const Caixa16 &c, uint8_t tipo, int16_t indice = -1 ) {
  return addAlvo( out, n, c.x, c.y, c.w, c.h, tipo, indice );
}
#ifdef UA_HOST
// Host harness only (tools/tft_ui/host_telas.py): the targets addAlvo( ) dropped because the list was full, since the
// last hostLimparAlvosPerdidos( ). A screen that loses a target loses a control without a word: the harness fails a
// sample that drops one, or whose list fills MAX_ALVOS. Not compiled into the firmware.
int hostAlvosPerdidos( );
void hostLimparAlvosPerdidos( );
#endif

// The title bar's area left of its first action's target, as an inert target. The title does nothing (the original's header tap
// showed the sensor's name, and the Angulo always shows it), but the nearest-target rule did not know: with the 12 px slop the
// lower part of a title's ink (the cedilla of "Configurações" reaches y 28) answered the first row of the list under the bar, and the
// end of a long title answered the action beside it ("Voltar", "Nova conta"): 50 of the 261 samples that have a title, measured on
// 67c6521 on 2026-10-15 (stages/nav_fix2/titles_before_67c6521.txt), 20 of them on the chart and the detail, where the band under the
// bar opened the detail or turned its page. It ends where the action's target begins, so no
// touch between the two is left to that rule, and it is 29 px tall by default: the bar and its hairline, none of the row below.
inline Caixa16 areaDoTitulo( int16_t xPrimeiraAcao, int16_t altura = H_STATUS + 1 ) { return Caixa16{ 0, 0, xPrimeiraAcao, altura }; }
// The "Voltar" of a title bar, and the inert title beside it.
inline int addAlvoVoltar( Alvo *out, int n, const Caixa16 &voltar, uint8_t tipo ) {
  n = addAlvo( out, n, areaDoTitulo( voltar.x ), ALVO_INERTE );
  return addAlvo( out, n, voltar, tipo );
}

// A control that is live (type `tipo`, with its `indice`) or drawn disabled: the same box either way, and the disabled one is
// listed inert (ALVO_INERTE, no index), so that a touch on it does nothing and does not fall to a neighbour.
inline int addAlvoSe( Alvo *out, int n, bool viva, const Caixa16 &c, uint8_t tipo, int16_t indice = -1 ) {
  return addAlvo( out, n, c, viva ? tipo : ALVO_INERTE, viva ? indice : (int16_t)-1 );
}

// How far outside every target a touch still answers the nearest one, in pixels. A design value, not a
// measurement: a finger covers about 9 mm (about 40 px on this 3.5-inch 320 x 240 panel) and the resistive
// panel's calibration is off by a few pixels, more near the edges, so a touch that lands in the gap between two
// controls or just outside a small one is a touch that meant one of them. Small enough that two controls 8 px
// apart still split the gap between them, and that a touch in a blank area (more than 12 px from every target)
// answers nothing. The bench is what checks it (tools/tft_ui/ESPEC_UI_A.md); tools/tft_ui/test_alvos.py pins the number.
constexpr int16_t FOLGA_TOQUE = 12;

// The target that answers a touch at (x, y), or nullptr.
//  - Inside one or more targets: the LAST of them listed — a later target is drawn over an earlier one (a popup
//    over its keypad).
//  - Outside every target: the one whose box is NEAREST to the point (Euclidean distance in pixels, from the
//    point to the box's nearest pixel), if that is at most FOLGA_TOQUE; a tie goes to the first listed.
// A control drawn disabled is in the list as an inert target (ALVO_INERTE, with the box it would have enabled) and
// takes part like any other: a touch on it is answered by it, so it does not fall to an enabled neighbour within the
// slop. A control that is not drawn at all is not in the list. What an inert answer means is alvoDoToque( )'s. A box with no
// pixel (w or h not above 0) is never an answer: it cannot be hit inside, so it cannot be the nearest either.
const Alvo *alvoEm( const Alvo *alvos, int n, int16_t x, int16_t y );

// The target a touch means to the glue (DisplayManager::uaToque( )): alvoEm( )'s answer, except that an inert target
// gives nullptr, which is how every handler already reads a touch outside every target: nothing happens, no gate is
// called (no touch sound, as the original's dead zones), no busy line is raised. One place for all the screens, so a
// handler cannot forget the inert type. tools/tft_ui/test_alvos.py tests it and bites it.
const Alvo *alvoDoToque( const Alvo *alvos, int n, int16_t x, int16_t y );

// Copies at most cap - 1 bytes of a UTF-8 string, never cutting a sequence,
// and terminates it. For the view models' fixed arrays.
void copiar( char *dst, const char *src, int cap );

// Greedy word wrap of a UTF-8 string into at most `max` lines of at most
// `largura` px of ink in font f, each copied into linhas[i] (cap bytes). A
// word wider than a line is cut at a character. Returns the lines used; when
// the text needs more than `max`, the last line ends in "…".
int quebrar( const char *texto, Fonte f, int16_t largura, char *linhas, int cap, int max );

// The status bar every panel screen shares (dashboard, alarm action).
enum class Rede : uint8_t { NENHUMA, CABO, CABO_SEM_LINK, WIFI, WIFI_FRACO, WIFI_SEM };
// The badge that takes the clock's place for a few seconds. The view model says what it is about and the bar
// writes the words (desenharStatus( )): the text has to be cut to the room the brand and the icons leave, and
// only the bar knows how much that is.
enum class Aviso : uint8_t { NENHUM, SILENCIADO, WEB };
struct VmStatus {
  char hora[6];          // "14:32"; "" when the clock is not set
  bool horaProvisoria;   // the clock has not been confirmed (NTP or manual)
  Aviso aviso;           // the badge in place of the clock
  uint16_t avisoSeg;     // SILENCIADO: the seconds left, as the countdown says them
  char avisoNome[32];    // WEB: the account that logged in (the device keeps 15 bytes of it), UTF-8
  Rede rede;
  uint16_t pendentes;    // telemetry records waiting (0: nothing shown)
  uint8_t envio;         // 0 idle, 1 last batch sent, 2 last batch failed
};
// Draws it with the kit's badge and the network and pending icons. The badge sits in what is free between the
// brand's ink and the icons on the right, ESP2 clear of both: at the panel's centre when it fits there, else in
// the middle of the free room. When its words are wider than the room the account's name is cut with "…" ("Web: "
// stays whole), and with no room for even one letter of it the badge says "Web"; a countdown that does not fit
// says "Silenciado" (a number cut in the middle would be a wrong number). When not even that fits, the clock stays.
void desenharStatus( Pintor &p, const VmStatus &s );

}  // namespace ua
