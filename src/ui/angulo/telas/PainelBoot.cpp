// The boot screen. See PainelBoot.h.
#include "PainelBoot.h"

#include "PainelComum.h"

namespace ua {
namespace tela {

namespace {

constexpr int16_t CENTRO = 160;

/* The plan's boot_wifi, lifted 36 px so that the log fits between the subtitle
 * and the foot: the brand and its subtitle, the log growing downwards from
 * Y_PASSOS, the foot. Four lines of it since the panel's text floor is 9 pt
 * (2026-10-15): five at 20 px would reach the foot. */
constexpr int16_t Y_MARCA = 20, H_MARCA = 40;
constexpr int16_t Y_SUBTITULO = Y_MARCA + H_MARCA + ESP1, H_SUBTITULO = 16;
constexpr int16_t Y_PASSOS = 96;
constexpr int16_t H_ANTERIOR = 20;       // an earlier step: apoio (9 pt), 20 px apart
constexpr int16_t H_ATUAL = 20;          // the step in progress: corpo
constexpr int16_t LARGURA_PASSO = 320 - 2 * PAD;

/* The hold-for-AP gesture takes the log's place: the old screen did the same,
 * it cleared itself for the bar. The plan's bar: 200 x 8, 60 px from the left. */
constexpr int16_t Y_ROTULO_AP = 120, H_ROTULO_AP = 20;
constexpr int16_t X_BARRA = 60, W_BARRA = 200, H_BARRA = 8, Y_BARRA = Y_ROTULO_AP + H_ROTULO_AP + ESP4;

/* The foot: the plan's two lines, or the button in their place. The button
 * stays inside the zone the old handler answers: x 81..239, y 191..239. */
constexpr int16_t Y_VERSAO = 196, Y_MAC = 212, H_LINHA = 16;
constexpr int16_t W_PULAR = 152, H_PULAR = 40, Y_PULAR = 192;

using painel::caixa;

struct LayoutBoot {
  Caixa16 pular;                       // w == 0: not shown
  int n;                               // log lines drawn
  char linhas[5][80];                  // each cut to the width, the last is the current step
  int16_t y[5];
  Fonte fonteAtual;                    // the step in progress: corpo, or apoio when corpo would cut it
};

LayoutBoot layoutBoot( const VmBoot &vm ) {
  LayoutBoot l{ };
  if ( vm.showSkipButton ) l.pular = caixa( CENTRO - W_PULAR / 2, Y_PULAR, W_PULAR, H_PULAR );
  // The newest four: the last of them is the step in progress.
  l.n = vm.nPassos > 4 ? 4 : vm.nPassos;
  const int de = vm.nPassos - l.n;
  l.fonteAtual = tipo::corpo;
  int16_t y = Y_PASSOS;
  for ( int i = 0; i < l.n; i++ ) {
    const bool atual = i == l.n - 1;
    // A line wider than the glass is cut, not wrapped: a step is one line. The
    // step in progress gives up its size before it gives up its text: the AP's
    // network and key come as one line, and the key is what someone at the
    // panel came to read.
    if ( atual && larguraTinta( vm.passos[de + i], tipo::corpo ) > LARGURA_PASSO ) l.fonteAtual = tipo::apoio;
    painel::cortar( l.linhas[i], sizeof l.linhas[i], vm.passos[de + i], atual ? l.fonteAtual : tipo::apoio, LARGURA_PASSO );
    // The step in progress sits a little apart from the ones that came before.
    if ( atual && i > 0 ) y += ESP1;
    l.y[i] = y;
    y += atual ? H_ATUAL : H_ANTERIOR;
  }
  return l;
}

}  // namespace

void desenharBoot( Pintor &p, const VmBoot &vm ) {
  const Tema &T = p.t( );
  const LayoutBoot l = layoutBoot( vm );
  textoCentrado( p, "SIMUT", CENTRO, Y_MARCA, H_MARCA, Fonte::DISP24, T.tinta, 'c' );
  textoCentrado( p, "Monitor de temperatura e umidade", CENTRO, Y_SUBTITULO, H_SUBTITULO, tipo::apoio, T.tinta2, 'c' );

  if ( vm.apProgressPct >= 0 ) {
    const int pct = vm.apProgressPct > 100 ? 100 : vm.apProgressPct;
    textoCentrado( p, "Segure para o modo de configuração", CENTRO, Y_ROTULO_AP, H_ROTULO_AP, tipo::corpo, T.tinta, 'c' );
    progresso( p, X_BARRA, Y_BARRA, W_BARRA, H_BARRA, pct * 10 );
  } else {
    for ( int i = 0; i < l.n; i++ ) {
      if ( i == l.n - 1 )
        textoCentrado( p, l.linhas[i], CENTRO, l.y[i], H_ATUAL, l.fonteAtual, T.tinta, 'c' );
      else
        textoCentrado( p, l.linhas[i], CENTRO, l.y[i], H_ANTERIOR, tipo::apoio, T.tinta2, 'c' );
    }
  }

  if ( l.pular.w ) {
    botao( p, l.pular.x, l.pular.y, l.pular.w, l.pular.h, "Pular a espera", Botao::SECUNDARIO );
  } else {
    textoCentrado( p, vm.versao, CENTRO, Y_VERSAO, H_LINHA, tipo::dado, T.tinta2, 'c' );
    textoCentrado( p, vm.mac, CENTRO, Y_MAC, H_LINHA, tipo::dado, T.tinta2, 'c' );
  }
}

int alvosBoot( const VmBoot &vm, Alvo *out ) {
  const LayoutBoot l = layoutBoot( vm );
  return l.pular.w ? addAlvo( out, 0, l.pular, BOOT_PULAR ) : 0;
}

}  // namespace tela
}  // namespace ua
