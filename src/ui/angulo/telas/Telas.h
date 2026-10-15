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
// screen's own enum (0 is never a target); `indice` a row, key or column
// (-1 when the type says it all).
struct Alvo {
  int16_t x, y, w, h;
  uint8_t tipo;
  int16_t indice;
};

// More than any screen has (the PIN keypad's 18 cards and its chrome).
constexpr int MAX_ALVOS = 48;

// Appends a target; returns the new count (n unchanged when out is full).
int addAlvo( Alvo *out, int n, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t tipo, int16_t indice = -1 );
inline int addAlvo( Alvo *out, int n, const Caixa16 &c, uint8_t tipo, int16_t indice = -1 ) {
  return addAlvo( out, n, c.x, c.y, c.w, c.h, tipo, indice );
}

// The LAST target containing (x, y) — a later target is drawn over an earlier
// one (a popup over its keypad) — or nullptr.
const Alvo *alvoEm( const Alvo *alvos, int n, int16_t x, int16_t y );

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
// the middle of the free room. When its words are wider than the room the account's name is cut with "…" (never
// the words around it), and with no room for even one letter of it the badge says "Entrou pela web"; a countdown
// that does not fit says "Silenciado" (a number cut in the middle would be a wrong number). When not even that
// fits, the clock stays.
void desenharStatus( Pintor &p, const VmStatus &s );

}  // namespace ua
