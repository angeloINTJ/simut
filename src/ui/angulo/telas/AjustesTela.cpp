// The display alignment, the touch sensitivity and the touch calibration. See AjustesTela.h.
#include "AjustesTela.h"

#include <stdio.h>
#include <string.h>

#include "AjustesComum.h"
#include "Base.h"

namespace ua {
namespace tela {

namespace {

using namespace ajustes;

/* ------------------------------------------------------------------ */
/* Alinhar a tela                                                       */
/* ------------------------------------------------------------------ */

// The reference frame stands on the edge of the safe area, x 4..315 and y 4..235
// (DisplayManager.h; tools/screen_safe_area.py): the alignment moves the image
// by up to 4 px, so the frame is what a user lines up with the glass's edge,
// and anything the panel draws inside it is always on the glass.
constexpr int16_t FRAME_X = 4, FRAME_Y = 4, FRAME_W = 312, FRAME_H = 232;

// The footer 4 px higher than the kit's row (y 196): the kit's buttons would end
// on the frame's own last row (y 235) and the two lines would run together.
constexpr int16_t Y_RODAPE_ALINHAR = FRAME_Y + FRAME_H - ESP1 - H_RODAPE;

// The plan's cross of 40 px arrows around the middle, one button and 8 px apart.
constexpr int16_t PASSO_SETA = L_BOTAO + ESP2;
constexpr int16_t X_SETA_MEIO = X_MEIO - L_BOTAO / 2;
constexpr int16_t BRACO_CRUZ = 10;        // the crosshair's arms: lines of 20 px
constexpr int16_t L_VALOR_EIXO = 44;      // the axis letter, a space and a sign with a digit, in the mono face
// The columns of text beside the arrows: the left one starts 8 px inside the frame, where
// the title's inset (PAD) is, so it lines up with the title. At 9 pt (2026-10-15) each
// column is 120 px wide: the left one from here to 8 px short of the up arrow (x 140),
// the right one from 8 px past the lower arrow (x 180) to 8 px inside the frame.
constexpr int16_t X_TEXTO = FRAME_X + ESP2;

struct LayoutAlinhar {
  Caixa16 cima, baixo, esquerda, direita;
  bool cimaAtiva, baixoAtiva, esquerdaAtiva, direitaAtiva;
  CaixasRodape rodape;
};

LayoutAlinhar layoutAlinhar( const VmAlinhar &vm ) {
  LayoutAlinhar l{ };
  l.cima = Caixa16{ X_SETA_MEIO, Y0, L_BOTAO, L_BOTAO };
  l.esquerda = Caixa16{ (int16_t)( X_SETA_MEIO - PASSO_SETA ), (int16_t)( Y0 + PASSO_SETA ), L_BOTAO, L_BOTAO };
  l.direita = Caixa16{ (int16_t)( X_SETA_MEIO + PASSO_SETA ), l.esquerda.y, L_BOTAO, L_BOTAO };
  l.baixo = Caixa16{ X_SETA_MEIO, (int16_t)( Y0 + 2 * PASSO_SETA ), L_BOTAO, L_BOTAO };
  // The offset stops at the limit the panel can shift by.
  l.cimaAtiva = vm.y > -(int)vm.limite;
  l.baixoAtiva = vm.y < (int)vm.limite;
  l.esquerdaAtiva = vm.x > -(int)vm.limite;
  l.direitaAtiva = vm.x < (int)vm.limite;
  l.rodape = caixasRodape( "Salvar", "Redefinir", Y_RODAPE_ALINHAR );
  return l;
}

// "+1", "0", "−2": the sign is always said, and the minus is the true one.
void comSinal( int v, char *dst, int cap ) {
  snprintf( dst, (size_t)cap, "%s%d", v > 0 ? "+" : v < 0 ? "\xE2\x88\x92" : "", v < 0 ? -v : v );
}

/* ------------------------------------------------------------------ */
/* Sensibilidade do toque                                               */
/* ------------------------------------------------------------------ */

// The stability bar: 24 wide, 12 from the right edge, as tall as the content
// (the plan's bar). It fills from the bottom and turns positive from 85 %, where
// the old screen turned it green (drawTouchSensitivity( ) in DisplayManager_Calibration.cpp).
constexpr int16_t L_ESTAB = 24, X_ESTAB = L_TELA - ESP3 - L_ESTAB, H_ESTAB = Y_RODAPE - ESP2 - Y0;
constexpr int PCT_MAX = 100, ESTAVEL_PCT = 85;

// The text and the figures live to the bar's left; the text lines are the base's
// dialog pitch, 20 px.
constexpr int16_t L_TEXTO = X_ESTAB - ESP2 - PAD;
constexpr int16_t H_LINHA_TEXTO = 20;
constexpr int16_t H_LINHA_DADO = 28;                     // a row of the figures' card
constexpr int N_DADOS = 3;
// The card of figures starts 12 px under the two lines of text.
constexpr int16_t Y_DADOS = Y0 + ESP1 + 2 * H_LINHA_TEXTO + ESP3;

struct LayoutSensibilidade {
  Caixa16 cancelar;
  Caixa16 dados;                    // the card of figures
  Caixa16 barra;
};

LayoutSensibilidade layoutSensibilidade( ) {
  LayoutSensibilidade l{ };
  // The old CANCEL answers a touch at y >= 196 and x <= 124 (DisplayManager_Touch.cpp:
  // `sy > 195 && sx < 125`): the button, on the footer's row and at the left (its
  // label lined up with the 12 px the rest of the page is inset by), is wholly
  // inside that zone: x 4..95, y 196..235.
  const int16_t w = larguraTinta( "Cancelar", tipo::botao ) + 2 * ESP2;
  l.cancelar = Caixa16{ PAD - ESP2, Y_RODAPE, w, H_RODAPE };
  l.dados = Caixa16{ ESP2, Y_DADOS, (int16_t)( X_ESTAB - 2 * ESP2 ), (int16_t)( N_DADOS * H_LINHA_DADO ) };
  l.barra = Caixa16{ X_ESTAB, Y0, L_ESTAB, H_ESTAB };
  return l;
}

/* ------------------------------------------------------------------ */
/* Calibrar o toque                                                     */
/* ------------------------------------------------------------------ */

// The plan's sight: two circles, 16 px arms (so the one at a corner ends on the
// safe area's edge), and a dot.
constexpr int16_t R_MIRA = 10, BRACO_MIRA = 16, R_PONTO = 2;
// The plan's two lines in the middle of the screen (calibrar_p1): what to do, then where we are.
constexpr int16_t Y_INSTRUCAO = 104, Y_POSICAO = Y_INSTRUCAO + ESP4 + ESP1;
constexpr int PONTOS = 4, VOLTAS = 2;      // the four points, taken twice

void mira( Pintor &p, int16_t x, int16_t y, uint16_t cor ) {
  p.circle( x, y, R_MIRA, cor );
  p.circle( x, y, R_MIRA + 1, cor );
  p.hline( x - BRACO_MIRA, y, 2 * BRACO_MIRA, cor );
  p.vline( x, y - BRACO_MIRA, 2 * BRACO_MIRA, cor );
  p.fillCircle( x, y, R_PONTO, cor );
}

// The verdict is the base's message screen.
VmMensagem veredito( uint8_t resultado ) {
  VmMensagem m;
  memset( &m, 0, sizeof m );
  if ( resultado == CALRES_CONCLUIDA ) {
    m.ok = true;
    copiar( m.texto, "Calibração concluída.", sizeof m.texto );
    copiar( m.botao, "Entendi", sizeof m.botao );
  } else {
    // The old button, on a refusal, starts the eight captures again.
    m.ok = false;
    copiar( m.texto, "Toques imprecisos.", sizeof m.texto );
    copiar( m.detalhe, "Toque de novo, devagar, no centro de cada mira.", sizeof m.detalhe );
    copiar( m.botao, "Refazer a calibração", sizeof m.botao );
  }
  return m;
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Alinhar a tela                                                       */
/* ------------------------------------------------------------------ */

void desenharAlinhar( Pintor &p, const VmAlinhar &vm ) {
  const Tema &T = p.t( );
  const LayoutAlinhar l = layoutAlinhar( vm );
  barraTituloVoltar( p, "Alinhar a tela" );
  botaoIcone( p, l.cima, IconeNome::CHEVRON_UP, l.cimaAtiva );
  botaoIcone( p, l.esquerda, IconeNome::CHEVRON_LEFT, l.esquerdaAtiva );
  botaoIcone( p, l.direita, IconeNome::CHEVRON_RIGHT, l.direitaAtiva );
  botaoIcone( p, l.baixo, IconeNome::CHEVRON_DOWN, l.baixoAtiva );
  // The plan's crosshair in the middle of the arrows.
  const int16_t cx = X_SETA_MEIO + L_BOTAO / 2, cy = l.esquerda.y + L_BOTAO / 2;
  p.hline( cx - BRACO_CRUZ, cy, 2 * BRACO_CRUZ, T.tinta2 );
  p.vline( cx, cy - BRACO_CRUZ, 2 * BRACO_CRUZ, T.tinta2 );

  // The instruction at the top left, the offset at the bottom left (by "Redefinir", which
  // zeroes it) and the warning at the bottom right (by "Salvar", which it is about).
  // "A moldura deve / encostar na borda." ran to x 162 at 9 pt, 22 px into the up arrow's
  // box (its second line is 142 px); "tocar a borda." is 106 px, and the first line 119.
  textoCentrado( p, "A moldura deve", X_TEXTO, Y0 + ESP2, ESP4, tipo::apoio, T.tinta2, 'l' );
  textoCentrado( p, "tocar a borda.", X_TEXTO, Y0 + ESP2 + PASSO_APOIO, ESP4, tipo::apoio, T.tinta2, 'l' );
  // The two axes one under the other, centred on the lower arrow's row: on one line "x −4
  // · y +4" is 119 px of the mono face, 1 px under the 120 the column has.
  char valores[2][8];
  comSinal( vm.x, valores[0], sizeof valores[0] );
  comSinal( vm.y, valores[1], sizeof valores[1] );
  for ( int i = 0; i < 2; i++ ) {
    const int16_t y = l.baixo.y + rd2( L_BOTAO - ( PASSO_APOIO + ESP4 ) ) + i * PASSO_APOIO;
    textoCentrado( p, i ? "y" : "x", X_TEXTO, y, ESP4, tipo::dado, T.tinta2, 'l' );
    textoCentrado( p, valores[i], X_TEXTO + L_VALOR_EIXO, y, ESP4, tipo::dado, T.tinta, 'r' );
  }
  // The warning: three lines of the column's 120 px ("Salvar refaz a / calibração do /
  // toque."); the two lines it had ended in "calibração do…" at 9 pt. It starts at the
  // lower arrow's top: its 52 px are taller than the arrow's 40, and centred on the arrow
  // the first line would start 2 px under the right arrow's box.
  char aviso[3][48];
  const int16_t xAviso = l.baixo.x + l.baixo.w + ESP2;
  const int n = quebrar( "Salvar refaz a calibração do toque.", tipo::apoio, FRAME_X + FRAME_W - ESP2 - xAviso, &aviso[0][0],
                         sizeof aviso[0], 3 );
  for ( int i = 0; i < n; i++ )
    textoCentrado( p, aviso[i], xAviso, l.baixo.y + i * PASSO_APOIO, ESP4, tipo::apoio, T.alerta, 'l' );

  desenharRodape( p, l.rodape, "Salvar", Botao::PRIMARIO, false, "Redefinir" );
  // Last, so that nothing paints over it.
  p.rect( FRAME_X, FRAME_Y, FRAME_W, FRAME_H, T.acento );
}

int alvosAlinhar( const VmAlinhar &vm, Alvo *out ) {
  const LayoutAlinhar l = layoutAlinhar( vm );
  int n = addAlvoVoltar( out, 0, caixaVoltar( ), ALINH_VOLTAR );
  n = addAlvoSe( out, n, l.cimaAtiva, l.cima, ALINH_CIMA );
  n = addAlvoSe( out, n, l.esquerdaAtiva, l.esquerda, ALINH_ESQUERDA );
  n = addAlvoSe( out, n, l.direitaAtiva, l.direita, ALINH_DIREITA );
  n = addAlvoSe( out, n, l.baixoAtiva, l.baixo, ALINH_BAIXO );
  n = addAlvo( out, n, l.rodape.secundaria, ALINH_REDEFINIR );
  return addAlvo( out, n, l.rodape.primaria, ALINH_SALVAR );
}

/* ------------------------------------------------------------------ */
/* Sensibilidade do toque                                               */
/* ------------------------------------------------------------------ */

void desenharSensibilidade( Pintor &p, const VmSensibilidade &vm ) {
  const Tema &T = p.t( );
  const LayoutSensibilidade l = layoutSensibilidade( );
  // No "Voltar" in the bar: only the old CANCEL zone leaves this screen, every
  // other touch is a sample, so a button there would count as one.
  barraTitulo( p, "Sensibilidade do toque", nullptr, 0 );

  if ( vm.concluido ) {
    textoCentrado( p, "Pronto.", PAD, Y0 + ESP1, H_LINHA_TEXTO, tipo::subtitulo, T.positivo, 'l' );
    textoCentrado( p, "Agora a posição do toque.", PAD, Y0 + ESP1 + H_LINHA_TEXTO, H_LINHA_TEXTO, tipo::corpo, T.tinta, 'l' );
  } else {
    char linhas[2][64];
    const int n = quebrar( "Toque na tela várias vezes, como faria no dia a dia.", tipo::corpo, L_TEXTO, &linhas[0][0], sizeof linhas[0], 2 );
    for ( int i = 0; i < n; i++ )
      textoCentrado( p, linhas[i], PAD, Y0 + ESP1 + H_LINHA_TEXTO * i, H_LINHA_TEXTO, tipo::corpo, T.tinta, 'l' );
  }

  // The figures: the readings counted, how steady the touch is, the threshold so far.
  superficie( p, l.dados.x, l.dados.y, l.dados.w, l.dados.h );
  const char *const rotulos[N_DADOS] = { "Leituras", "Estabilidade", "Limiar" };
  char valores[N_DADOS][16];
  snprintf( valores[0], sizeof valores[0], "%u", (unsigned)vm.leituras );
  snprintf( valores[1], sizeof valores[1], "%u %%", (unsigned)vm.estabilidade );
  if ( vm.limiar ) snprintf( valores[2], sizeof valores[2], "%u", (unsigned)vm.limiar );
  else snprintf( valores[2], sizeof valores[2], "--" );
  for ( int i = 0; i < N_DADOS; i++ ) {
    const int16_t y = l.dados.y + i * H_LINHA_DADO;
    if ( i > 0 ) p.hline( l.dados.x + PAD, y, l.dados.w - 2 * PAD, T.linha );
    textoCentrado( p, rotulos[i], l.dados.x + PAD, y, H_LINHA_DADO, tipo::apoio, T.tinta2, 'l' );
    textoCentrado( p, valores[i], l.dados.x + l.dados.w - PAD, y, H_LINHA_DADO, tipo::dado, T.tinta, 'r' );
  }

  // The bar: the track, and the fill inside it from the bottom up with 4 px of margin.
  p.fillRoundRect( l.barra.x, l.barra.y, l.barra.w, l.barra.h, RAIO_CONTROLE, T.superficie2 );
  p.roundRect( l.barra.x, l.barra.y, l.barra.w, l.barra.h, RAIO_CONTROLE, T.linha );
  const int pct = vm.estabilidade < PCT_MAX ? vm.estabilidade : PCT_MAX;
  const int16_t alturaInterna = l.barra.h - 2 * ESP1;
  const int16_t cheio = rdiv( (int32_t)alturaInterna * pct, PCT_MAX );
  if ( cheio > 0 )
    p.fillRoundRect( l.barra.x + ESP1, l.barra.y + ESP1 + alturaInterna - cheio, l.barra.w - 2 * ESP1, cheio, RAIO_CONTROLE,
                     pct >= ESTAVEL_PCT ? T.positivo : T.acento );

  botao( p, l.cancelar.x, l.cancelar.y, l.cancelar.w, l.cancelar.h, "Cancelar", Botao::DISCRETO );
}

int alvosSensibilidade( const VmSensibilidade &, Alvo *out ) {
  const LayoutSensibilidade l = layoutSensibilidade( );
  return addAlvo( out, 0, l.cancelar, SENSIB_CANCELAR );
}

/* ------------------------------------------------------------------ */
/* Calibrar o toque                                                     */
/* ------------------------------------------------------------------ */

void desenharCalibrar( Pintor &p, const VmCalibrar &vm ) {
  const Tema &T = p.t( );
  if ( vm.resultado != CALRES_MEDINDO ) {
    desenharMensagem( p, veredito( vm.resultado ) );
    return;
  }
  const int passo = vm.passo < PONTOS ? vm.passo : 0;
  mira( p, MIRA_X[passo], MIRA_Y[passo], vm.segurando ? T.positivo : T.acento );
  if ( vm.segurando )
    textoCentrado( p, "Pode soltar.", X_MEIO, Y_INSTRUCAO, ESP4, tipo::corpo, T.positivo, 'c' );
  else
    textoCentrado( p, "Toque no centro da mira e segure.", X_MEIO, Y_INSTRUCAO, ESP4, tipo::corpo, T.tinta, 'c' );
  char posicao[40];
  snprintf( posicao, sizeof posicao, "Ponto %d de %d · volta %d de %d", passo + 1, PONTOS, vm.volta < VOLTAS ? 1 : VOLTAS, VOLTAS );
  textoCentrado( p, posicao, X_MEIO, Y_POSICAO, ESP4, tipo::apoio, T.tinta2, 'c' );
}

int alvosCalibrar( const VmCalibrar &vm, Alvo *out ) {
  if ( vm.resultado == CALRES_MEDINDO ) return 0;
  const int n = alvosMensagem( veredito( vm.resultado ), out );
  for ( int i = 0; i < n; i++ ) out[i].tipo = CALIB_BOTAO;
  return n;
}

}  // namespace tela
}  // namespace ua
