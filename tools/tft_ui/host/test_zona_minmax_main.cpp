/**
 * @file test_zona_minmax_main.cpp
 * @brief The host side of tools/tft_ui/test_zona_minmax.py: the original dashboard's min/max graph button, painted and touched.
 *
 * The three .inc files are written by the script, cut out of the sources verbatim and without comments, and are not retyped here:
 *   pecas_desenho.inc  src/sensors/SensorDrawing.h: MINMAX_BTN_ANCHOR / _W / _Y / _H and minMaxBtnX( ), what the three drivers paint the button with
 *   pecas_cartao.inc   src/DisplayManager.h: CARD_X and CARD_W, the rectangle both cards are painted on
 *   pecas_toque.inc    src/ui/original/UiTouchChain.h: the dashboard branch of the original's touch chain, the one `if (_uiMode == MODE_DASHBOARD)`
 * What is modelled and not cut: the core's actions behind the zones (they only record that they were called, and with which `onGraphBtn`), the touch
 * gate, and the nav row's action. The card actions are the same code for both cards in the sources (DisplayManager_Actions.h); what differs between
 * the two cards is the zone the chain hands them, and that is what this runs.
 *
 * The oracle is the painted rectangle: x from CARD_X + minMaxBtnX( CARD_W ) for MINMAX_BTN_W columns, the same numbers every driver passes to
 * drawMinMaxGraphBtn( ). A touch on one of those columns, anywhere inside the card's rows, must reach the card's action with onGraphBtn true;
 * a touch on any other column must reach it with false; the action runs exactly once, and only inside the card's rows.
 *
 * Exit 0 when every check holds, 1 when one does not. Prints one line per card.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @license MIT License
 */
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "pecas_desenho.inc"

enum UiMode { MODE_DASHBOARD, MODE_OUTRO };

struct Painel {
#include "pecas_cartao.inc"
  UiMode _uiMode = MODE_DASHBOARD;
  int chamadasCima = 0, chamadasBaixo = 0, chamadasNav = 0;
  bool botaoCima = false, botaoBaixo = false;
  bool acceptTouch(uint8_t) { return true; }
  void actTopCardPress(bool noBotao) { chamadasCima++; botaoCima = noBotao; }
  void actBottomCardTap(bool noBotao) { chamadasBaixo++; botaoBaixo = noBotao; }
  void actNavButton(int) { chamadasNav++; }
  void toque(int16_t x, int16_t y) {
#include "pecas_toque.inc"
  }
};

static int g_conta = 0, g_falhas = 0;

static void confere(bool ok, const char *msg, int a, int b) {
  g_conta++;
  if (!ok) { g_falhas++; printf("FAIL %s (%d, %d)\n", msg, a, b); }
}

int main() {
  // What is painted: the button's rectangle, from the numbers the drivers draw with (drawMinMaxGraphBtn( cv, minMaxBtnX( cardW ), ..., MINMAX_BTN_W, ... ),
  // blitted at CARD_X).
  const int X0 = Painel::CARD_X + minMaxBtnX(Painel::CARD_W);
  const int X1 = X0 + MINMAX_BTN_W;   // exclusive
  printf("painted button: x %d..%d (CARD_X %d, CARD_W %d, %d columns)\n", X0, X1 - 1, Painel::CARD_X, Painel::CARD_W, MINMAX_BTN_W);
  const struct { const char *nome; int yDentro; int yPrimeira, yUltima; } cartoes[2] = {
    {"upper card", 60, 36, 109},    // the chain's rows: y > 35 && y < 110
    {"lower card", 150, 116, 189},  // y > 115 && y < 190
  };
  for (int c = 0; c < 2; c++) {
    int naZona = 0, faltam = 0, sobram = 0, primeira = -1, ultima = -1, chamadasErradas = 0;
    for (int x = 0; x < 320; x++) {
      Painel p;
      p.toque((int16_t)x, (int16_t)cartoes[c].yDentro);
      const int n = c == 0 ? p.chamadasCima : p.chamadasBaixo;
      const bool botao = c == 0 ? p.botaoCima : p.botaoBaixo;
      if (n != 1 || (c == 0 ? p.chamadasBaixo : p.chamadasCima) != 0) { chamadasErradas++; continue; }
      const bool pintado = x >= X0 && x < X1;
      if (botao) { naZona++; if (primeira < 0) primeira = x; ultima = x; }
      if (pintado && !botao) faltam++;      // painted, and a touch there does not open the graph
      if (!pintado && botao) sobram++;      // not painted, and a touch there opens the graph
    }
    const bool ok = chamadasErradas == 0 && faltam == 0 && sobram == 0;
    printf("%s %s: the zone is x %d..%d (%d columns); %d painted columns do not open the graph, %d unpainted ones do; %d columns reached the wrong action or none\n",
           ok ? "ok  " : "FAIL", cartoes[c].nome, primeira, ultima, naZona, faltam, sobram, chamadasErradas);
    confere(chamadasErradas == 0, "a touch inside the card's rows did not reach the card's action exactly once", c, chamadasErradas);
    confere(faltam == 0, "painted columns that do not open the graph", c, faltam);
    confere(sobram == 0, "unpainted columns that open the graph", c, sobram);
    // The rows: the first and the last row of the card reach its action, the row above and the row below do not (the fix moves no row).
    for (int d = -1; d <= 1; d += 2) {
      const int y = d < 0 ? cartoes[c].yPrimeira - 1 : cartoes[c].yUltima + 1;
      Painel p;
      p.toque((int16_t)(X0 + 5), (int16_t)y);
      confere((c == 0 ? p.chamadasCima : p.chamadasBaixo) == 0, "a touch just outside the card's rows reached its action", c, y);
    }
    for (int y : {cartoes[c].yPrimeira, cartoes[c].yUltima}) {
      Painel p;
      p.toque((int16_t)(X0 + 5), (int16_t)y);
      confere((c == 0 ? p.chamadasCima : p.chamadasBaixo) == 1, "a touch on the card's first or last row did not reach its action", c, y);
    }
  }
  printf("%d checks, %d failed\n", g_conta, g_falhas);
  return g_falhas ? 1 : 0;
}
