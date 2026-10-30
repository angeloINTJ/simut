// The result and the confirmation screens. See Base.h.
#include "Base.h"

namespace ua {
namespace tela {

namespace {

// Both screens end in a 40 px row of buttons at the foot, inside the safe
// area: y 188..227, the old screens' button row (y 185..230) included.
constexpr int16_t Y_BOTOES = 188, H_BOTAO = 40;

/* Result: the icon, the text and the button, centred as one block. */
struct LayoutMensagem {
  char linhas[2][96];
  int nLinhas;
  char det[3][96];
  int nDet;
  int16_t yIcone, yTexto, yDetalhe;
  Caixa16 botao;
};

LayoutMensagem layoutMensagem( const VmMensagem &vm ) {
  LayoutMensagem l{ };
  const int16_t larg = 320 - 2 * ESP5;
  l.nLinhas = quebrar( vm.texto, tipo::subtitulo, larg, &l.linhas[0][0], sizeof l.linhas[0], 2 );
  l.nDet = vm.detalhe[0] ? quebrar( vm.detalhe, tipo::corpo, larg, &l.det[0][0], sizeof l.det[0], 3 ) : 0;
  // icon 24, 16 below it, text lines of 20, 8, detail lines of 20
  const int16_t alto = 24 + ESP4 + 20 * l.nLinhas + ( l.nDet ? ESP2 + 20 * l.nDet : 0 );
  const int16_t topo = rd2( Y_BOTOES - alto );
  l.yIcone = topo;
  l.yTexto = topo + 24 + ESP4;
  l.yDetalhe = l.yTexto + 20 * l.nLinhas + ESP2;
  l.botao = Caixa16{ PAD, Y_BOTOES, 320 - 2 * PAD, H_BOTAO };
  return l;
}

/* Confirmation: title bar, the text, two buttons side by side. */
struct LayoutDialogo {
  char linhas[4][96];
  int nLinhas;
  Caixa16 nao, sim;
};

LayoutDialogo layoutDialogo( const VmDialogo &vm ) {
  LayoutDialogo l{ };
  l.nLinhas = quebrar( vm.texto, tipo::corpo, 320 - 2 * PAD, &l.linhas[0][0], sizeof l.linhas[0], 4 );
  const int16_t w = rd2( 320 - 2 * PAD - ESP2 );
  l.nao = Caixa16{ PAD, Y_BOTOES, w, H_BOTAO };
  l.sim = Caixa16{ (int16_t)( 320 - PAD - w ), Y_BOTOES, w, H_BOTAO };
  return l;
}

}  // namespace

void desenharMensagem( Pintor &p, const VmMensagem &vm ) {
  const Tema &T = p.t( );
  const LayoutMensagem l = layoutMensagem( vm );
  p.icone( vm.ok ? IconeNome::CIRCLE_CHECK : IconeNome::CIRCLE_X, 24, 160 - 12, l.yIcone, vm.ok ? T.positivo : T.perigo );
  for ( int i = 0; i < l.nLinhas; i++ )
    textoCentrado( p, l.linhas[i], 160, l.yTexto + 20 * i, 20, tipo::subtitulo, T.tinta, 'c' );
  for ( int i = 0; i < l.nDet; i++ )
    textoCentrado( p, l.det[i], 160, l.yDetalhe + 20 * i, 20, tipo::corpo, T.tinta2, 'c' );
  botao( p, l.botao.x, l.botao.y, l.botao.w, l.botao.h, vm.botao, Botao::PRIMARIO );
}

int alvosMensagem( const VmMensagem &vm, Alvo *out ) {
  const LayoutMensagem l = layoutMensagem( vm );
  return addAlvo( out, 0, l.botao, MSG_BOTAO );
}

void desenharDialogo( Pintor &p, const VmDialogo &vm ) {
  const Tema &T = p.t( );
  const LayoutDialogo l = layoutDialogo( vm );
  barraTitulo( p, vm.titulo, nullptr, 0 );
  for ( int i = 0; i < l.nLinhas; i++ )
    textoCentrado( p, l.linhas[i], PAD, Y0 + ESP2 + 20 * i, 20, tipo::corpo, T.tinta, 'l' );
  botao( p, l.nao.x, l.nao.y, l.nao.w, l.nao.h, vm.nao, Botao::SECUNDARIO );
  botao( p, l.sim.x, l.sim.y, l.sim.w, l.sim.h, vm.sim, vm.destrutivo ? Botao::DESTRUTIVO : Botao::PRIMARIO );
}

int alvosDialogo( const VmDialogo &vm, Alvo *out ) {
  const LayoutDialogo l = layoutDialogo( vm );
  int n = addAlvo( out, 0, l.nao, DLG_NAO );
  return addAlvo( out, n, l.sim, DLG_SIM );
}

}  // namespace tela
}  // namespace ua
