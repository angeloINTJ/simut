// The alarm's action screen. See PainelAlarme.h.
#include "PainelAlarme.h"

#include <stdio.h>

#include "PainelComum.h"

namespace ua {
namespace tela {

namespace {

using painel::caixa;
using Causa = VmAcaoAlarme::Causa;

/* The plan's stack (alarme_acao_acoes): the danger strip under the status bar,
 * then three buttons 8 px apart. The one that gives the alarm up is the
 * shortest, so it is the hardest to hit by mistake. */
constexpr int16_t X_BLOCO = ESP2, W_BLOCO = 320 - 2 * ESP2;
constexpr int16_t H_FAIXA = 56, H_BOTAO = 40, H_DESATIVAR = 32;
constexpr int16_t Y_SILENCIAR = Y0 + H_FAIXA + ESP2;
constexpr int16_t Y_MINMAX = Y_SILENCIAR + H_BOTAO + ESP2;
constexpr int16_t Y_DESATIVAR = Y_MINMAX + H_BOTAO + ESP2;

struct LayoutAcao {
  Caixa16 faixa, silenciar, minmax, desativar;
  int16_t xTexto;          // the strip's text, after the icon
  char titulo[80];
  char detalhe[80];
  char rotuloSilenciar[32];
};

LayoutAcao layoutAcao( const VmAcaoAlarme &vm ) {
  LayoutAcao l{ };
  l.faixa = caixa( X_BLOCO, Y0, W_BLOCO, H_FAIXA );
  l.silenciar = caixa( X_BLOCO, Y_SILENCIAR, W_BLOCO, H_BOTAO );
  l.minmax = caixa( X_BLOCO, Y_MINMAX, W_BLOCO, H_BOTAO );
  l.desativar = caixa( X_BLOCO, Y_DESATIVAR, W_BLOCO, H_DESATIVAR );
  l.xTexto = X_BLOCO + PAD + 24 + ESP2;

  // What happened, in words, and the numbers that say it. The name gives way
  // to the rest of the title, so that the cause is never the part that is cut.
  const char *fim = " em alarme";
  char b1[16], b2[16];
  l.detalhe[0] = 0;
  switch ( vm.causa ) {
    case Causa::ACIMA:
      fim = " acima do limite";
      snprintf( l.detalhe, sizeof l.detalhe, "%s °C agora · máx %s °C", n1( vm.temp, b1 ), n1( vm.limite, b2 ) );
      break;
    case Causa::ABAIXO:
      fim = " abaixo do limite";
      snprintf( l.detalhe, sizeof l.detalhe, "%s °C agora · mín %s °C", n1( vm.temp, b1 ), n1( vm.limite, b2 ) );
      break;
    case Causa::ERRO:
      fim = " com erro no sensor";
      snprintf( l.detalhe, sizeof l.detalhe, "Sem leitura. Confira o cabo." );
      break;
    case Causa::FORA:
      snprintf( l.detalhe, sizeof l.detalhe, "Um valor passou do limite." );
      break;
  }
  painel::nomeComFim( l.titulo, sizeof l.titulo, vm.nome, fim, tipo::subtitulo, X_BLOCO + W_BLOCO - PAD - l.xTexto );
  snprintf( l.rotuloSilenciar, sizeof l.rotuloSilenciar, "Silenciar por %u s", (unsigned)vm.silencioSeg );
  return l;
}

}  // namespace

void desenharAcaoAlarme( Pintor &p, const VmAcaoAlarme &vm ) {
  const Tema &T = p.t( );
  const LayoutAcao l = layoutAcao( vm );
  desenharStatus( p, vm.status );
  p.fillRoundRect( l.faixa.x, l.faixa.y, l.faixa.w, l.faixa.h, RAIO_CARTAO, T.perigoSuave );
  p.icone( IconeNome::TRIANGLE_ALERT, 24, X_BLOCO + PAD, l.faixa.y + ESP4, T.perigo );
  textoCentrado( p, l.titulo, l.xTexto, l.faixa.y + ESP2, 20, tipo::subtitulo, T.perigo, 'l' );
  textoCentrado( p, l.detalhe, l.xTexto, l.faixa.y + ESP2 + 24, 16, tipo::apoio, T.perigo, 'l' );
  botao( p, l.silenciar.x, l.silenciar.y, l.silenciar.w, l.silenciar.h, l.rotuloSilenciar, Botao::PRIMARIO, false,
         IconeNome::BELL_OFF );
  botao( p, l.minmax.x, l.minmax.y, l.minmax.w, l.minmax.h, "Ver mínimo e máximo", Botao::SECUNDARIO, false,
         IconeNome::HISTORY );
  // The label says what the button will ask for; the PIN keypad is next.
  botao( p, l.desativar.x, l.desativar.y, l.desativar.w, l.desativar.h, "Desativar os alarmes (pede o PIN)",
         Botao::DESTRUTIVO );
}

int alvosAcaoAlarme( const VmAcaoAlarme &vm, Alvo *out ) {
  const LayoutAcao l = layoutAcao( vm );
  int n = addAlvo( out, 0, l.silenciar, ACA_SILENCIAR );
  n = addAlvo( out, n, l.minmax, ACA_MINMAX );
  return addAlvo( out, n, l.desativar, ACA_DESATIVAR );
}

}  // namespace tela
}  // namespace ua
