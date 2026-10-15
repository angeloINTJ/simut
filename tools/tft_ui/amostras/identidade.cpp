// Samples of the identity group's screens (src/ui_angulo/telas/Identidade*.h):
// who is at the panel, the PIN keypad in its six layouts and every state of the
// lockout ladder, the text keyboard, the list of accounts and one account. Each
// sample fills a view model the way the device would in one state. See Amostras.h.
#include <stdint.h>
#include <string.h>

#include "Amostras.h"
#include "IdentidadeContas.h"
#include "IdentidadePin.h"
#include "IdentidadeTexto.h"

using namespace ua;
using namespace ua::tela;

namespace {

/* ------------------------------------------------------------------ */
/* Who is at the panel, the list of accounts, one account               */
/* ------------------------------------------------------------------ */

struct ContaAm {
  const char *nome;
  bool travada;
  uint8_t perms;
  bool temPin;
};

const uint8_t LAM = CTA_PERM_LIMITES | CTA_PERM_ALARMES | CTA_PERM_MANUT;
const uint8_t LA = CTA_PERM_LIMITES | CTA_PERM_ALARMES;

// Accounts of the shared bench; the admin is first, as the picker lists it (slot order).
const ContaAm kContas[] = {
  { "admin", false, LAM, true },        { "maria", false, LAM, true },       { "joao.silva", false, LA, true },
  { "ana.souza", false, CTA_PERM_LIMITES, true }, { "carlos", false, 0, true },  { "pedro.lima", false, CTA_PERM_MANUT, true },
  { "lucia", false, LA, true },         { "marcos.t", false, 0, true },       { "fernanda", false, LAM, true },
};

VmQuem quem( int total, int pagina, const ContaAm *contas, int n, int travada = -1 ) {
  VmQuem vm;
  memset( &vm, 0, sizeof vm );
  vm.total = (uint8_t)total;
  vm.pagina = (uint8_t)pagina;
  vm.nLinhas = (uint8_t)n;
  for ( int i = 0; i < n; i++ ) {
    copiar( vm.linhas[i].nome, contas[i].nome, sizeof vm.linhas[i].nome );
    vm.linhas[i].travada = i == travada;
  }
  return vm;
}

VmUsuarios usuarios( int total, int pagina, const ContaAm *contas, int n, bool podeCriar ) {
  VmUsuarios vm;
  memset( &vm, 0, sizeof vm );
  vm.total = (uint8_t)total;
  vm.pagina = (uint8_t)pagina;
  vm.nLinhas = (uint8_t)n;
  vm.podeCriar = podeCriar;
  for ( int i = 0; i < n; i++ ) {
    copiar( vm.linhas[i].nome, contas[i].nome, sizeof vm.linhas[i].nome );
    vm.linhas[i].perms = contas[i].perms;
    vm.linhas[i].temPin = contas[i].temPin;
  }
  return vm;
}

VmConta conta( const char *nome, bool nova, uint8_t perms, bool temPin ) {
  VmConta vm;
  memset( &vm, 0, sizeof vm );
  copiar( vm.nome, nome, sizeof vm.nome );
  vm.nova = nova;
  vm.perms = perms;
  vm.temPin = temPin;
  return vm;
}

// The longest name an account can have (15 bytes, UserAccount::username[16]) and one
// that cannot exist on the device but can come from outside (31): the cut must hold for both.
const char *const NOME_15 = "joao.silva.2026";
const char *const NOME_31 = "administradora.laboratorio.sul1";

const ContaAm kContasLongas[] = {
  { NOME_15, false, LAM, true }, { NOME_31, false, LA, false }, { NOME_31, true, 0, true }, { "maria", false, LAM, true },
};
const ContaAm kContasSemPin[] = {
  { "maria", false, LAM, true }, { "joao.silva", false, LA, true }, { "ana.souza", false, 0, false },
};
const ContaAm kContasCriadas[] = {   // the 7 accounts the admin created (the admin is not listed)
  { "maria", false, LAM, true }, { "joao.silva", false, LA, true }, { "ana.souza", false, CTA_PERM_LIMITES, false },
  { "carlos", false, 0, true },  { "pedro.lima", false, CTA_PERM_MANUT, true }, { "lucia", false, LA, true },
  { "marcos.t", false, 0, false },
};

const VmQuem kQuem3 = quem( 3, 0, kContas, 3 );
const VmQuem kQuemTravada = quem( 3, 0, kContas, 3, 1 );
const VmQuem kQuem9p1 = quem( 9, 0, kContas, 4 );
const VmQuem kQuem9p2 = quem( 9, 1, kContas + 4, 4 );
const VmQuem kQuem9p3 = quem( 9, 2, kContas + 8, 1 );
const VmQuem kQuemVazio = quem( 0, 0, kContas, 0 );
const VmQuem kQuemLongo = quem( 4, 0, kContasLongas, 4, 2 );

const VmUsuarios kUsr3 = usuarios( 3, 0, kContasSemPin, 3, true );
const VmUsuarios kUsr7p1 = usuarios( 7, 0, kContasCriadas, 4, true );
const VmUsuarios kUsr7p2 = usuarios( 7, 1, kContasCriadas + 4, 3, true );
const VmUsuarios kUsrVazio = usuarios( 0, 0, kContasCriadas, 0, true );
const VmUsuarios kUsrVazioSemPermissao = usuarios( 0, 0, kContasCriadas, 0, false );
const VmUsuarios kUsrSemPermissao = usuarios( 3, 0, kContasSemPin, 3, false );
const VmUsuarios kUsrLongo = usuarios( 4, 0, kContasLongas, 4, true );

const VmConta kContaExistente = conta( "maria", false, LA, true );
const VmConta kContaSemPin = conta( "ana.souza", false, CTA_PERM_LIMITES, false );
const VmConta kContaNova = conta( "carlos", true, 0, false );
const VmConta kContaLonga = conta( NOME_31, false, LAM, true );

/* ------------------------------------------------------------------ */
/* The PIN keypad                                                       */
/* ------------------------------------------------------------------ */

// scramblePinKeys( ), the way it deals: the alphabet and as many distinct decoys
// as the grid has room for, shuffled over keys x glyphs (Fisher-Yates, xorshift32).
uint32_t s_estado = 1;
uint32_t sorte( ) {
  s_estado ^= s_estado << 13;
  s_estado ^= s_estado >> 17;
  s_estado ^= s_estado << 5;
  return s_estado;
}

void sortear( VmPin &vm, int teclas, int glifos, bool alnum, uint32_t semente ) {
  static const char ALNUM[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static const char ENCHIMENTOS[] = "!@#$%&*+-=?~";                 // PinKb::DECOY_POOL
  const int nChars = alnum ? 36 : 10;
  const int total = teclas * glifos;
  char todos[PIN_CARTOES_MAX * PIN_GLIFOS_MAX];
  for ( int i = 0; i < nChars; i++ ) todos[i] = ALNUM[i];
  s_estado = semente;
  for ( int d = 0; d < total - nChars; d++ ) {
    char c;
    bool repetido;
    do {
      c = ENCHIMENTOS[sorte( ) % ( sizeof ENCHIMENTOS - 1 )];
      repetido = false;
      for ( int q = 0; q < d; q++ ) if ( todos[nChars + q] == c ) repetido = true;
    } while ( repetido );
    todos[nChars + d] = c;
  }
  for ( int i = total - 1; i > 0; i-- ) {
    const int j = (int)( sorte( ) % (uint32_t)( i + 1 ) );
    const char t = todos[i];
    todos[i] = todos[j];
    todos[j] = t;
  }
  for ( int k = 0; k < teclas; k++ ) {
    for ( int s = 0; s < glifos; s++ ) vm.cartoes[k][s] = todos[k * glifos + s];
    vm.cartoes[k][glifos] = 0;
  }
}

// A keypad as the device opens it: proving the PIN of "maria", nothing typed.
VmPin pin( PinTeclado t, uint32_t semente = 7 ) {
  VmPin vm;
  memset( &vm, 0, sizeof vm );
  vm.teclado = t;
  vm.fase = PinFase::IDENTIFICAR;
  vm.estado = PinEstado::LIVRE;
  vm.minimo = 4;                       // PinKb::entryMinLen( ): 4 when proving
  vm.pressionada = -1;
  vm.popup = -1;
  copiar( vm.conta, "maria", sizeof vm.conta );
  switch ( t ) {
    case PinTeclado::CARTOES4: vm.teto = 8; sortear( vm, 4, 3, false, semente ); break;
    case PinTeclado::CARTOES6: vm.teto = 12; sortear( vm, 6, 2, false, semente ); break;
    case PinTeclado::NUMERICO: vm.teto = 16; break;
    case PinTeclado::CARTOES12: vm.teto = 8; vm.alfanumerico = true; sortear( vm, 12, 3, true, semente ); break;
    case PinTeclado::CARTOES18: vm.teto = 12; vm.alfanumerico = true; sortear( vm, 18, 2, true, semente ); break;
    case PinTeclado::GRUPOS: vm.teto = 16; vm.alfanumerico = true; break;
  }
  return vm;
}

VmPin digitando( VmPin vm, int n, int pressionada = -1 ) {
  vm.digitados = (uint8_t)n;
  vm.pressionada = (int8_t)pressionada;
  return vm;
}
VmPin comEstado( VmPin vm, PinEstado e, PinErro erro = PinErro::INCORRETO ) {
  vm.estado = e;
  vm.erro = erro;
  return vm;
}
// Choosing a PIN: always an ordered layout, the policy's minimum, no deal.
VmPin definir( PinTeclado t, PinFase fase, int minimo, int teto, const char *dono ) {
  VmPin vm = pin( t );
  vm.fase = fase;
  vm.minimo = (uint8_t)minimo;
  vm.teto = (uint8_t)teto;
  copiar( vm.conta, dono, sizeof vm.conta );
  return vm;
}

const VmPin kC4Vazio = pin( PinTeclado::CARTOES4, 7 );
const VmPin kC4Digitando = digitando( pin( PinTeclado::CARTOES4, 11 ), 3, 1 );
const VmPin kC6Vazio = pin( PinTeclado::CARTOES6, 5 );
const VmPin kC6Digitando = digitando( pin( PinTeclado::CARTOES6, 9 ), 5, 4 );
const VmPin kNumVazio = pin( PinTeclado::NUMERICO );
const VmPin kNumDigitando = digitando( pin( PinTeclado::NUMERICO ), 4, 4 );
const VmPin kNum16 = digitando( pin( PinTeclado::NUMERICO ), 16 );       // the ceiling: sixteen places, all full
const VmPin kC12Vazio = pin( PinTeclado::CARTOES12, 3 );
const VmPin kC12Digitando = digitando( pin( PinTeclado::CARTOES12, 13 ), 5, 6 );
const VmPin kC18Vazio = pin( PinTeclado::CARTOES18, 21 );
const VmPin kC18Digitando = digitando( pin( PinTeclado::CARTOES18, 17 ), 6, 10 );
const VmPin kGruposVazio = pin( PinTeclado::GRUPOS );
const VmPin kGruposDigitando = digitando( pin( PinTeclado::GRUPOS ), 5, 4 );

// The popup: the second tap of an ordered alphanumeric keyboard.
VmPin comPopup( VmPin vm, int grupo ) { vm.popup = (int8_t)grupo; return vm; }
const VmPin kPopupPqrs = comPopup( digitando( pin( PinTeclado::GRUPOS ), 2 ), 6 );
const VmPin kPopupDigitos = comPopup( digitando( pin( PinTeclado::GRUPOS ), 4 ), 0 );
const VmPin kPopupAbc = comPopup( digitando( pin( PinTeclado::GRUPOS ), 0 ), 1 );

// Refusals: "PIN incorreto" at the cards (a fresh deal after it), "PIN curto"
// and "Os dois PINs não batem" while choosing one.
const VmPin kErroIncorreto = comEstado( pin( PinTeclado::CARTOES4, 19 ), PinEstado::ERRO, PinErro::INCORRETO );
const VmPin kErroCurto = comEstado( digitando( definir( PinTeclado::NUMERICO, PinFase::DEFINIR, 6, 8, "" ), 3 ), PinEstado::ERRO, PinErro::CURTO );
const VmPin kErroDiferentes = comEstado( definir( PinTeclado::NUMERICO, PinFase::DEFINIR, 6, 8, "" ), PinEstado::ERRO, PinErro::DIFERENTES );

VmPin comEspera( VmPin vm, int s ) { vm.esperaS = (uint16_t)s; return vm; }
const VmPin kAguardando = comEstado( digitando( pin( PinTeclado::CARTOES4, 23 ), 5 ), PinEstado::AGUARDANDO );
const VmPin kEspera = comEspera( comEstado( pin( PinTeclado::CARTOES6, 29 ), PinEstado::ESPERA ), 15 );
const VmPin kBloqueioPainel = comEstado( pin( PinTeclado::CARTOES4, 31 ), PinEstado::BLOQUEIO_PAINEL );
const VmPin kBloqueioConta = comEstado( pin( PinTeclado::NUMERICO ), PinEstado::BLOQUEIO_CONTA );

// Choosing a PIN: the first pass, the confirmation, another account's, and an alphanumeric one.
const VmPin kDefinirFase1 = digitando( definir( PinTeclado::NUMERICO, PinFase::DEFINIR, 6, 8, "" ), 4, 4 );
const VmPin kDefinirFase2 = digitando( definir( PinTeclado::NUMERICO, PinFase::CONFIRMAR, 6, 8, "" ), 2, 8 );
const VmPin kDefinirOutra = digitando( definir( PinTeclado::NUMERICO, PinFase::DEFINIR, 4, 8, "joao.silva" ), 0 );
const VmPin kDefinirGrupos = digitando( definir( PinTeclado::GRUPOS, PinFase::DEFINIR, 6, 12, "joao.silva" ), 5 );

VmPin comConta( VmPin vm, const char *nome ) { copiar( vm.conta, nome, sizeof vm.conta ); return vm; }
const VmPin kPinNomeLongo = comConta( pin( PinTeclado::CARTOES12, 37 ), NOME_31 );
const VmPin kPinNome15 = comConta( pin( PinTeclado::CARTOES4, 41 ), NOME_15 );

/* ------------------------------------------------------------------ */
/* The text keyboard                                                    */
/* ------------------------------------------------------------------ */

// The name of a new account, as the device opens the keyboard: 15 characters at most.
VmTexto texto( const char *txt ) {
  VmTexto vm;
  memset( &vm, 0, sizeof vm );
  vm.camada = TextoCamada::MIN;
  vm.shift = TextoShift::OFF;
  vm.maximo = 15;
  vm.pressionada = -1;
  copiar( vm.rotulo, "Seguir", sizeof vm.rotulo );
  copiar( vm.titulo, "Nome da conta", sizeof vm.titulo );
  copiar( vm.texto, txt, sizeof vm.texto );
  copiar( vm.ajuda, "Aspas e barra invertida não valem.", sizeof vm.ajuda );
  return vm;
}
VmTexto comCamada( VmTexto vm, TextoCamada c, TextoShift s = TextoShift::OFF ) { vm.camada = c; vm.shift = s; return vm; }
VmTexto comTecla( VmTexto vm, int pressionada ) { vm.pressionada = (int8_t)pressionada; return vm; }
VmTexto comErro( VmTexto vm, const char *erro ) { copiar( vm.erro, erro, sizeof vm.erro ); return vm; }
// The legacy PIN of `screen pwd`: dots and the eye, 7 at most.
VmTexto senhaLegada( const char *txt, bool ver ) {
  VmTexto vm = texto( txt );
  vm.secreto = true;
  vm.ver = ver;
  vm.maximo = 7;
  copiar( vm.titulo, "Nova senha", sizeof vm.titulo );
  copiar( vm.rotulo, "Salvar", sizeof vm.rotulo );
  copiar( vm.ajuda, "De 4 a 7 caracteres.", sizeof vm.ajuda );
  return vm;
}

const VmTexto kTxtVazio = texto( "" );
const VmTexto kTxtDigitando = comTecla( texto( "maria.s" ), 10 );        // the A of the second row
const VmTexto kTxtShift = comCamada( texto( "ana" ), TextoCamada::MIN, TextoShift::ON );
const VmTexto kTxtCaps = comCamada( texto( "ANA" ), TextoCamada::MIN, TextoShift::LOCK );
const VmTexto kTxt123 = comTecla( comCamada( texto( "ana2" ), TextoCamada::NUM ), 3 );
const VmTexto kTxtSimbolos = comCamada( texto( "ana-" ), TextoCamada::SIMB );
const VmTexto kTxtCheio = texto( NOME_15 );                              // 15 of 15
const VmTexto kTxtErro = comErro( texto( "maria" ), TXT_ERRO_REPETIDO );
const VmTexto kTxtErroReservado = comErro( texto( "Admin" ), TXT_ERRO_RESERVADO );
const VmTexto kTxtErroCaracteres = comErro( texto( "maria\"" ), TXT_ERRO_CARACTERES );
const VmTexto kTxtErroEspacos = comErro( texto( "   " ), TXT_ERRO_SO_ESPACOS );
const VmTexto kTxtEspacoFinal = texto( "ana maria " );
const VmTexto kTxtSenha = senhaLegada( "1234", false );
const VmTexto kTxtSenhaVer = senhaLegada( "1234", true );

/* ------------------------------------------------------------------ */
/* The table                                                            */
/* ------------------------------------------------------------------ */

#define AM( nome, tela, vm ) \
  { nome, []( Pintor &p ) { desenhar##tela( p, vm ); }, []( Alvo *o ) { return alvos##tela( vm, o ); } }

}  // namespace

namespace amostra {

const Amostra kAmostras[] = {
  // Who is at the panel
  AM( "quem_3_contas", Quem, kQuem3 ),
  AM( "quem_1_travada", Quem, kQuemTravada ),
  AM( "quem_9_pag1", Quem, kQuem9p1 ),
  AM( "quem_9_pag2", Quem, kQuem9p2 ),
  AM( "quem_9_pag3", Quem, kQuem9p3 ),
  AM( "quem_vazio", Quem, kQuemVazio ),
  AM( "quem_nomes_longos", Quem, kQuemLongo ),
  // The PIN: each layout empty and typing, then the rest of the states
  AM( "pin_cartoes4_vazio", Pin, kC4Vazio ),
  AM( "pin_cartoes4_digitando", Pin, kC4Digitando ),
  AM( "pin_cartoes6_vazio", Pin, kC6Vazio ),
  AM( "pin_cartoes6_digitando", Pin, kC6Digitando ),
  AM( "pin_numerico_vazio", Pin, kNumVazio ),
  AM( "pin_numerico_digitando", Pin, kNumDigitando ),
  AM( "pin_numerico_16", Pin, kNum16 ),
  AM( "pin_cartoes12_vazio", Pin, kC12Vazio ),
  AM( "pin_cartoes12_digitando", Pin, kC12Digitando ),
  AM( "pin_cartoes18_vazio", Pin, kC18Vazio ),
  AM( "pin_cartoes18_digitando", Pin, kC18Digitando ),
  AM( "pin_grupos_vazio", Pin, kGruposVazio ),
  AM( "pin_grupos_digitando", Pin, kGruposDigitando ),
  AM( "pin_popup_pqrs", Pin, kPopupPqrs ),
  AM( "pin_popup_digitos", Pin, kPopupDigitos ),
  AM( "pin_popup_abc", Pin, kPopupAbc ),
  AM( "pin_erro_incorreto", Pin, kErroIncorreto ),
  AM( "pin_erro_curto", Pin, kErroCurto ),
  AM( "pin_erro_diferentes", Pin, kErroDiferentes ),
  AM( "pin_aguardando", Pin, kAguardando ),
  AM( "pin_espera", Pin, kEspera ),
  AM( "pin_bloqueio_painel", Pin, kBloqueioPainel ),
  AM( "pin_bloqueio_conta", Pin, kBloqueioConta ),
  AM( "pin_definir_fase1", Pin, kDefinirFase1 ),
  AM( "pin_definir_fase2", Pin, kDefinirFase2 ),
  AM( "pin_definir_outra_conta", Pin, kDefinirOutra ),
  AM( "pin_definir_grupos", Pin, kDefinirGrupos ),
  AM( "pin_nome_15", Pin, kPinNome15 ),
  AM( "pin_nome_31", Pin, kPinNomeLongo ),
  // The text keyboard
  AM( "texto_vazio", Texto, kTxtVazio ),
  AM( "texto_digitando", Texto, kTxtDigitando ),
  AM( "texto_shift", Texto, kTxtShift ),
  AM( "texto_caps", Texto, kTxtCaps ),
  AM( "texto_123", Texto, kTxt123 ),
  AM( "texto_simbolos", Texto, kTxtSimbolos ),
  AM( "texto_cheio_15_de_15", Texto, kTxtCheio ),
  AM( "texto_erro", Texto, kTxtErro ),
  AM( "texto_erro_reservado", Texto, kTxtErroReservado ),
  AM( "texto_erro_caracteres", Texto, kTxtErroCaracteres ),
  AM( "texto_erro_so_espacos", Texto, kTxtErroEspacos ),
  AM( "texto_espaco_final", Texto, kTxtEspacoFinal ),
  AM( "texto_senha_legada", Texto, kTxtSenha ),
  AM( "texto_senha_legada_ver", Texto, kTxtSenhaVer ),
  // The list of accounts
  AM( "usuarios_3_contas", Usuarios, kUsr3 ),
  AM( "usuarios_7_pag1", Usuarios, kUsr7p1 ),
  AM( "usuarios_7_pag2", Usuarios, kUsr7p2 ),
  AM( "usuarios_vazio", Usuarios, kUsrVazio ),
  AM( "usuarios_vazio_sem_permissao", Usuarios, kUsrVazioSemPermissao ),
  AM( "usuarios_sem_permissao", Usuarios, kUsrSemPermissao ),
  AM( "usuarios_nomes_longos", Usuarios, kUsrLongo ),
  // One account
  AM( "conta_existente", Conta, kContaExistente ),
  AM( "conta_sem_pin", Conta, kContaSemPin ),
  AM( "conta_nova", Conta, kContaNova ),
  AM( "conta_nome_31", Conta, kContaLonga ),
};
const int kAmostrasN = sizeof kAmostras / sizeof kAmostras[0];

}  // namespace amostra
