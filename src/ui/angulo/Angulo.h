#pragma once
// The Ângulo kit for the TFT: tokens, text, icons, components and the chrome
// of the screens, ported from the TFT plan's renderer (docs/ui_tft_angulo,
// renderizador/src/angulo.js and telas.js, family A) line for line, so that a
// screen drawn here with the plan's scenario is pixel for pixel the plan's
// reference (tools/tft_ui/host_ui.py proves it on the host).
//
// Every coordinate rule of the renderer is kept: Math.round( ) is rd2( )/rdiv( )
// (half up, as JavaScript rounds), text is aligned by its ink, and every
// primitive is normalized the way the renderer's Surface normalizes it before
// Adafruit_GFX draws it (Pintor).
//
// Built only by the functional study build (pico_wiretft_angulo) and the host
// harness; no product image compiles src/ui/angulo/.

#include <stdint.h>

#include "AnguloMeta.h"

class Adafruit_GFX;

namespace ua {

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

// The 17 roles of a theme, in RGB565 (angulo_tokens.h).
struct Tema {
  uint16_t fundo, superficie, superficie2, tinta, tinta2, linha, linhaForte,
      acento, acentoForte, acentoTinta, positivo, positivoSuave, alerta,
      alertaSuave, perigo, perigoSuave, perigoTinta;
  bool escuro;
};
const Tema &temaClaro( );
const Tema &temaEscuro( );

// "Disabled": the role pre-mixed at 45 % over the theme's background
// (the delivered *_DESAB tables). Any colour that is not a role of the theme
// comes back unchanged.
uint16_t desab( const Tema &t, uint16_t cor );

enum : int16_t {
  ESP1 = 4, ESP2 = 8, ESP3 = 12, ESP4 = 16, ESP5 = 24, ESP6 = 32, ESP7 = 48,
  RAIO_CONTROLE = 6, RAIO_CARTAO = 12, RAIO_TOTAL = 999,
  PAD = ESP3,          // text inset in a card or row
  H_STATUS = 28,       // status bar
  Y0 = 36,             // first content row of family A
};

// Math.round(v / 2) and Math.round(n / d) for integers (d > 0), half up.
int16_t rd2( int32_t v );
int16_t rdiv( int32_t n, int32_t d );

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

enum class Fonte : uint8_t { SANS7, SANSB7, SANS9, SANSB9, MONO9, MONO12, DISP9, DISP12, DISP18, DISP24, DISP32, MARCA };

namespace tipo {
constexpr Fonte titulo = Fonte::DISP12;
constexpr Fonte subtitulo = Fonte::DISP9;
constexpr Fonte corpo = Fonte::SANS9;
constexpr Fonte botao = Fonte::SANSB9;
constexpr Fonte rotulo = Fonte::SANSB7;
constexpr Fonte apoio = Fonte::SANS7;
constexpr Fonte dado = Fonte::MONO9;
constexpr Fonte dadoGrande = Fonte::MONO12;
}  // namespace tipo

// Ink extent of drawn text.
struct Tinta { int16_t x0, x1; };

// Ink box of a UTF-8 string relative to the cursor and baseline.
struct Caixa { int16_t inkL, inkR, inkT, inkB; bool vazio; };
Caixa medir( const char *utf8, Fonte f );
int16_t larguraTinta( const char *utf8, Fonte f );   // inkR - inkL (the renderer's textW)
int16_t alturaCaixaAlta( Fonte f );
int16_t alturaX( Fonte f );

// Characters a font cannot draw are skipped (no advance) and counted here,
// so the host harness can fail on them.
uint32_t glifosFaltando( );

#ifdef UA_HOST
// Host harness only (tools/tft_ui/host_telas.py): the ink box of every string
// drawn since the last hostLimparTextos( ), to hold each screen's text inside
// the glass. Not compiled into the firmware.
struct TextoDesenhado { int16_t x0, x1, y0, y1; char texto[64]; };
void hostLimparTextos( );
int hostTextos( const TextoDesenhado **lista );
#endif

/* ------------------------------------------------------------------ */
/* Pintor: the renderer's Surface over Adafruit_GFX                    */
/* ------------------------------------------------------------------ */

class Pintor {
public:
  Pintor( Adafruit_GFX &g, const Tema &t ) : g_( g ), t_( &t ) {}

  const Tema &t( ) const { return *t_; }
  void tema( const Tema &t ) { t_ = &t; }
  Adafruit_GFX &gfx( ) { return g_; }

  // The rows [y0, y1) that will be kept: the firmware draws a screen once per
  // 40-row strip (DisplayManager_UiA.cpp), and text, icons and rounded boxes
  // wholly outside the strip are skipped instead of being clipped pixel by
  // pixel. Only a speed-up: what lands inside the window is the same.
  void janela( int16_t y0, int16_t y1 ) { jy0_ = y0; jy1_ = y1; }

  void clear( uint16_t c );
  void pixel( int16_t x, int16_t y, uint16_t c );
  void fillRect( int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c );
  void hline( int16_t x, int16_t y, int16_t w, uint16_t c ) { fillRect( x, y, w, 1, c ); }
  void vline( int16_t x, int16_t y, int16_t h, uint16_t c ) { fillRect( x, y, 1, h, c ); }
  void rect( int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c );
  void line( int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c );
  void circle( int16_t x, int16_t y, int16_t r, uint16_t c );
  void fillCircle( int16_t x, int16_t y, int16_t r, uint16_t c );
  void roundRect( int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c );
  void fillRoundRect( int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c );
  void icone( IconeNome n, int16_t tam, int16_t x, int16_t y, uint16_t c );

  // (x, y) = anchor and baseline; align 'l' left ink edge, 'c' ink centre,
  // 'r' right ink edge. Returns the ink extent.
  Tinta texto( const char *utf8, int16_t x, int16_t y, Fonte f, uint16_t cor, char alinha = 'l' );

private:
  bool fora( int16_t y, int16_t h ) const { return y + h <= jy0_ || y >= jy1_; }
  Adafruit_GFX &g_;
  const Tema *t_;
  int16_t jy0_ = -32768, jy1_ = 32767;
};

// Text centred vertically in [y, y + h): cap height (or x height).
Tinta textoCentrado( Pintor &p, const char *s, int16_t x, int16_t y, int16_t h, Fonte f, uint16_t cor,
                     char alinha = 'c', bool alturaDeX = false );

// Is (nome, tam) in the registry? (the host harness checks every one it uses)
bool temIcone( IconeNome n, int16_t tam );

/* ------------------------------------------------------------------ */
/* Components (angulo.js)                                              */
/* ------------------------------------------------------------------ */

enum class Botao : uint8_t { PRIMARIO, SECUNDARIO, DISCRETO, DESTRUTIVO };
void botao( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const char *rotulo,
            Botao tipo = Botao::PRIMARIO, bool desabilitado = false, IconeNome icone = IconeNome::NENHUM );

enum class TeclaTipo : uint8_t { LETRA, FUNCAO, ESPACO, PRIMARIA };
enum class TeclaEstado : uint8_t { REPOUSO, PRESSIONADA, ATIVA, TRAVADA, DESAB };
struct TeclaOpts {
  TeclaTipo tipo = TeclaTipo::LETRA;
  TeclaEstado estado = TeclaEstado::REPOUSO;
  const char *legenda = "";
  IconeNome icone = IconeNome::NENHUM;
  Fonte fonte = tipo::corpo;
  bool alturaDeX = false;
  bool grade = false;
  int16_t tam = 16;
  bool temCorLegenda = false;
  uint16_t corLegenda = 0;
};
void tecla( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const TeclaOpts &o );

enum class CampoEstado : uint8_t { REPOUSO, FOCO, ERRO, DESAB };
// Draws the field's box; the content goes in (x + 12, y, w - 24, h).
void campo( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, CampoEstado e = CampoEstado::REPOUSO );

struct PontosOpts {
  bool cursor = true;
  const char *ultimo = nullptr;
  int16_t max = 0;
  int16_t passo = ESP3;
  bool temCor = false;
  uint16_t cor = 0;
};
void pontos( Pintor &p, int16_t x, int16_t y, int16_t h, int16_t n, const PontosOpts &o );

enum class Tom : uint8_t { NEUTRO, POSITIVO, ALERTA, PERIGO, OK };
void ajuda( Pintor &p, int16_t x, int16_t y, int16_t h, const char *texto, Tom tom = Tom::NEUTRO );
void segmentado( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const char *const *opcoes, int16_t n,
                 int16_t ativa, bool desabilitado = false );
int16_t chip( Pintor &p, int16_t x, int16_t y, int16_t h, const char *rotulo, bool ativo = false );
int16_t larguraChip( const char *rotulo );
int16_t selo( Pintor &p, int16_t x, int16_t y, int16_t h, const char *rotulo, Tom tom = Tom::NEUTRO );
int16_t larguraSelo( const char *rotulo );

/* ------------------------------------------------------------------ */
/* Chrome (telas.js, family A)                                         */
/* ------------------------------------------------------------------ */

// Status bar: brand, the clock (or a badge in its place), icons on the right.
struct Status {
  const char *hora = "";
  const char *aviso = nullptr;      // badge text in place of the clock
  Tom avisoTom = Tom::NEUTRO;
  IconeNome icones[3] = { IconeNome::NENHUM, IconeNome::NENHUM, IconeNome::NENHUM };   // right to left
};
void barraStatus( Pintor &p, const Status &s );

// Title bar of the inner screens: title on the left (falls to subtitulo when
// it does not fit beside the actions), actions on the right, a line below.
struct Acao {
  const char *rotulo = "";
  Botao tipo = Botao::DISCRETO;
  IconeNome icone = IconeNome::NENHUM;
  int16_t w = 0;                    // 0: from the label
};
struct Caixa16 { int16_t x, y, w, h; };
// Draws the bar; caixas (if given) receives the actions' boxes, left to right.
void barraTitulo( Pintor &p, const char *titulo, const Acao *acoes, int16_t n, Caixa16 *caixas = nullptr,
                  int16_t h = 28 );
// The common case: the title and "Voltar".
void barraTituloVoltar( Pintor &p, const char *titulo, Caixa16 *voltar = nullptr );
int16_t larguraAcao( const Acao &a );

// Card (family A): fill + 1 px line, radius 12.
void superficie( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, bool temFundo = false, uint16_t fundo = 0,
                 bool temBorda = false, uint16_t borda = 0 );

struct Item {
  const char *texto = "";
  const char *valor = nullptr;
  Fonte valorFonte = tipo::dado;
  bool temValorCor = false;
  uint16_t valorCor = 0;
  IconeNome icone = IconeNome::NENHUM;
  bool temTom = false;
  uint16_t tom = 0;                 // colour of icon and text (a role)
  bool chevron = false;
  bool marcado = false;
  int8_t interruptor = -1;          // -1 none, 0 off, 1 on
  const char *selo = nullptr;
  Tom seloTom = Tom::NEUTRO;
  bool negrito = false;
};
// List in a card (family A); returns the y after it.
int16_t lista( Pintor &p, int16_t y, const Item *itens, int16_t n, int16_t hFila = 40, int16_t selecionado = -1 );

void interruptor( Pintor &p, int16_t x, int16_t y, bool ligado, bool desabilitado = false );

// Footer pager: two square arrow buttons and "n de m"; an optional action at the right.
void paginador( Pintor &p, int16_t pagina, int16_t total, const Acao *acao = nullptr );
// The pager's boxes (left arrow, right arrow, action).
Caixa16 paginadorEsquerda( );
Caixa16 paginadorDireita( );
Caixa16 paginadorAcao( const Acao &acao );

// Footer with the secondary (discreet) and the primary action.
void rodapeAcoes( Pintor &p, const char *primaria, Botao primariaTipo = Botao::PRIMARIO,
                  const char *secundaria = "Cancelar", Caixa16 *cxPrimaria = nullptr, Caixa16 *cxSecundaria = nullptr );

// Number with unit: digits in a display font, unit in botao, same baseline.
// Returns the x after the unit.
int16_t numero( Pintor &p, int16_t x, int16_t y, const char *valor, const char *unidade, Fonte fonteNum, uint16_t cor,
                char alinha = 'l', Fonte fonteUn = tipo::botao, bool temCorUn = false, uint16_t corUn = 0 );

void progresso( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, int32_t milesimos );   // frac in 1/1000
void tracejada( Pintor &p, int16_t x0, int16_t x1, int16_t y, uint16_t cor );

/* ------------------------------------------------------------------ */
/* Number formatting (pt-BR: comma, one decimal, sign kept)            */
/* ------------------------------------------------------------------ */

// n1: Math.round(v * 10) / 10 with toFixed(1), the comma for the point.
// v in tenths. Writes into buf (at least 16 bytes) and returns it.
char *n1( int32_t decimos, char *buf );

}  // namespace ua
