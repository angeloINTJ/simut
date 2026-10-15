// Samples of the result and confirmation screens (src/ui_angulo/telas/Base.h),
// with the texts the device would show. See Amostras.h.
#include <string.h>

#include "Amostras.h"
#include "Base.h"

using namespace ua;
using namespace ua::tela;

namespace {

VmMensagem mensagem( bool ok, const char *texto, const char *detalhe ) {
  VmMensagem vm;
  memset( &vm, 0, sizeof vm );
  vm.ok = ok;
  copiar( vm.texto, texto, sizeof vm.texto );
  copiar( vm.detalhe, detalhe, sizeof vm.detalhe );
  copiar( vm.botao, "Entendi", sizeof vm.botao );
  return vm;
}

const VmMensagem kMsgOk = mensagem( true, "PIN salvo.", "" );
const VmMensagem kMsgErro = mensagem( false, "Sem permissão.", "Peça a quem administra o aparelho para liberar esta função na sua conta." );
const VmMensagem kMsgLonga = mensagem( true, "Calibração concluída. O toque já usa os novos valores.", "" );

VmDialogo dialogo( const char *titulo, const char *texto, const char *nao, const char *sim, bool destrutivo ) {
  VmDialogo vm;
  memset( &vm, 0, sizeof vm );
  copiar( vm.titulo, titulo, sizeof vm.titulo );
  copiar( vm.texto, texto, sizeof vm.texto );
  copiar( vm.nao, nao, sizeof vm.nao );
  copiar( vm.sim, sim, sizeof vm.sim );
  vm.destrutivo = destrutivo;
  return vm;
}

const VmDialogo kMudo = dialogo( "Silenciar todos os sons?",
                                 "Nenhum som toca, nem o do alarme, até alguém ligar os sons de novo. O mudo só vale depois de Salvar na tela de sons.",
                                 "Voltar", "Silenciar tudo", true );
const VmDialogo kExcluir = dialogo( "Excluir a conta maria?",
                                    "A conta perde o acesso pelo painel e pela web. O PIN e a senha dela são apagados.",
                                    "Cancelar", "Excluir a conta", true );

}  // namespace

namespace amostra {

const Amostra kAmostras[] = {
  { "mensagem_ok", []( Pintor &p ) { desenharMensagem( p, kMsgOk ); }, []( Alvo *o ) { return alvosMensagem( kMsgOk, o ); } },
  { "mensagem_erro", []( Pintor &p ) { desenharMensagem( p, kMsgErro ); }, []( Alvo *o ) { return alvosMensagem( kMsgErro, o ); } },
  { "mensagem_longa", []( Pintor &p ) { desenharMensagem( p, kMsgLonga ); }, []( Alvo *o ) { return alvosMensagem( kMsgLonga, o ); } },
  { "dialogo_mudo", []( Pintor &p ) { desenharDialogo( p, kMudo ); }, []( Alvo *o ) { return alvosDialogo( kMudo, o ); } },
  { "dialogo_excluir", []( Pintor &p ) { desenharDialogo( p, kExcluir ); }, []( Alvo *o ) { return alvosDialogo( kExcluir, o ); } },
};
const int kAmostrasN = sizeof kAmostras / sizeof kAmostras[0];

}  // namespace amostra
