// The alarm's action screen. See PainelAlarme.h.
#include "PainelAlarme.h"

#include <stdio.h>

#include "PainelComum.h"
#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

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
  const char *fim = UA_TXT( "in alarm" );
  char b1[16], b2[16];
  l.detalhe[0] = 0;
  switch ( vm.causa ) {
    case Causa::ACIMA:
      fim = UA_TXT( "above the limit" );
      snprintf( l.detalhe, sizeof l.detalhe, "%s °C %s · %s %s °C", n1( vm.temp, b1 ), UA_TXT( "now" ), UA_TXT( "max" ), n1( vm.limite, b2 ) );
      break;
    case Causa::ABAIXO:
      fim = UA_TXT( "below the limit" );
      snprintf( l.detalhe, sizeof l.detalhe, "%s °C %s · %s %s °C", n1( vm.temp, b1 ), UA_TXT( "now" ), UA_TXT( "min" ), n1( vm.limite, b2 ) );
      break;
    case Causa::ERRO:
      fim = UA_TXT( "with a sensor error" );
      copiar( l.detalhe, UA_TXT( "No reading. Check the cable." ), (int)sizeof l.detalhe );
      break;
    case Causa::FORA:
      copiar( l.detalhe, UA_TXT( "A value went past the limit." ), (int)sizeof l.detalhe );
      break;
  }
  painel::nomeComFim( l.titulo, sizeof l.titulo, vm.nome, fim, tipo::subtitulo, X_BLOCO + W_BLOCO - PAD - l.xTexto );
  snprintf( l.rotuloSilenciar, sizeof l.rotuloSilenciar, "%s %u s", UA_TXT( "Silence for" ), (unsigned)vm.silencioSeg );
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
  botao( p, l.minmax.x, l.minmax.y, l.minmax.w, l.minmax.h, UA_TXT( "View minimum and maximum" ), Botao::SECUNDARIO, false,
         IconeNome::HISTORY );
  // The label says what the button will ask for; the PIN keypad is next.
  botao( p, l.desativar.x, l.desativar.y, l.desativar.w, l.desativar.h, UA_TXT( "Deactivate alarms (asks for PIN)" ),
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
