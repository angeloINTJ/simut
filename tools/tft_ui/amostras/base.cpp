// Samples of the result and confirmation screens (src/ui/angulo/telas/Base.h),
// with the texts the device would show. See Amostras.h.
//
// A sample builds its view model when it is drawn, not at start-up (S10c, B3): the texts that the device's glue (DisplayManager_UiA.cpp) asks of UA_TXT( )
// come from the language pack that the harness loads in main( ) before the first sample draws, and a constant built earlier would hold the English for good.
// Each English below is the glue's own, because the glue cannot be built on the host; tools/tft_ui/host_telas.py holds every UA_TXT( ) English of a sample file to an
// English of src/ui/angulo. A text with no UA_TXT( ) is one the glue does not say: the long message is the calibration's (Ajustes, converted in B4) and stays in Portuguese, as its frame was drawn.
// "Reboot required" is the one text the glue does say without UA_TXT( ): Core 0 sends the key and the glue
// takes tr( ) of it, in English because the unit that shows it has loaded no pack (msgReiniciar( ) below).
#include <stdio.h>
#include <string.h>

#include "Amostras.h"
#include "Base.h"
#include "Textos.h"   // UA_TXT( ): after the screens' headers, because its pragmas hold to the end of the unit

using namespace ua;
using namespace ua::tela;

namespace {

VmMensagem mensagem( bool ok, const char *texto, const char *detalhe ) {
  VmMensagem vm;
  memset( &vm, 0, sizeof vm );
  vm.ok = ok;
  copiar( vm.texto, texto, sizeof vm.texto );
  copiar( vm.detalhe, detalhe, sizeof vm.detalhe );
  copiar( vm.botao, UA_TXT( "Got it" ), sizeof vm.botao );
  return vm;
}

// The success text of the glue: textoDaMensagem( ), DisplayManager_UiA.cpp, answers TR_PIN_SAVED with UA_TXT( "Change saved." ), the neutral words because the same key also answers a saved PIN policy. It is
// looked up in the pack as the glue looks it up: the manual's figure of a success message (S10c, C6) is drawn from this sample, and it used to say "PIN salvo.", which the device no longer says.
VmMensagem msgOk( ) { return mensagem( true, UA_TXT( "Change saved." ), "" ); }
VmMensagem msgErro( ) {
  return mensagem( false, UA_TXT( "No permission." ), UA_TXT( "Ask whoever administers the device to allow this function on your account." ) );
}
// TR_USERS_FULL, which EVT_USER_ADD raises through showPanelMessage( false, TR_USERS_FULL, ... ) when it finds no free slot (AppManager_Panel.cpp): the glue's textoDaMensagem( ) answers it with
// these two Englishes. No sample drew it until S10c C6b, which found that its second line named the wrong number of accounts.
VmMensagem msgContasCheias( ) {
  return mensagem( false, UA_TXT( "No room for another account." ),
                   UA_TXT( "Delete an account to create another." ) );
}
VmMensagem msgLonga( ) { return mensagem( true, "Calibração concluída. O toque já usa os novos valores.", "" ); }

// Core 0's "Reboot required" (AppManager_Events.cpp, EVT_APPLY_LANG), said when the Idioma screen's choice is a pack the unit has only found in /lang and has not loaded, which is an English boot
// (display/LangChoice.h, langAppliesAtBoot( )). Nothing is translated yet, so the glue (uaMontarBase( ), MODE_PANEL_MESSAGE) takes the text from tr( TR_REBOOT_REQ ), the English of
// DisplayManager_i18n.cpp:25, and the button, UA_TXT( "Got it" ), answers in English too, because no @ANGULO is in RAM. Both are literals here, English in every language of the harness:
// a frame of this message in Portuguese is one the device cannot draw, since a pack that is loaded applies at once and says nothing. It is the manual's figure of the message (S10c, C6).
VmMensagem msgReiniciar( ) {
  VmMensagem vm = mensagem( true, "Reboot required", "" );
  copiar( vm.botao, "Got it", sizeof vm.botao );
  return vm;
}

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

VmDialogo mudo( ) {
  return dialogo( UA_TXT( "Mute all sounds?" ),
                  UA_TXT( "No sound plays, not even the alarm's, until someone turns the sounds back on. Mute only takes effect "
                          "after you press Save on the sounds screen." ),
                  UA_TXT( "Back" ), UA_TXT( "Mute all" ), true );
}
VmDialogo excluir( ) {
  char titulo[48];
  snprintf( titulo, sizeof titulo, "%s %s?", UA_TXT( "Delete the account" ), "maria" );
  return dialogo( titulo, UA_TXT( "The account loses access from the panel and the web. Its PIN and password are erased." ), UA_TXT( "Cancel" ),
                  UA_TXT( "Delete account" ), true );
}

// The setup network's confirmation (MODE_CONFIRM_AP, a Wi-Fi image's; S10c, D7): the glue's uaMontarConfirmaAp( ) fills the same dialog with these Englishes.
VmDialogo redeAp( ) {
  return dialogo( UA_TXT( "Open the setup network?" ),
                  UA_TXT( "The device leaves its Wi-Fi network and opens its own setup network. Telemetry stops until it rejoins, "
                          "15 minutes later." ),
                  UA_TXT( "Cancel" ), UA_TXT( "Start setup" ), true );
}

}  // namespace

namespace amostra {

const Amostra kAmostras[] = {
  { "mensagem_ok", []( Pintor &p ) { desenharMensagem( p, msgOk( ) ); }, []( Alvo *o ) { return alvosMensagem( msgOk( ), o ); } },
  { "mensagem_erro", []( Pintor &p ) { desenharMensagem( p, msgErro( ) ); }, []( Alvo *o ) { return alvosMensagem( msgErro( ), o ); } },
  { "mensagem_contas_cheias", []( Pintor &p ) { desenharMensagem( p, msgContasCheias( ) ); }, []( Alvo *o ) { return alvosMensagem( msgContasCheias( ), o ); } },
  { "mensagem_longa", []( Pintor &p ) { desenharMensagem( p, msgLonga( ) ); }, []( Alvo *o ) { return alvosMensagem( msgLonga( ), o ); } },
  { "mensagem_reiniciar", []( Pintor &p ) { desenharMensagem( p, msgReiniciar( ) ); }, []( Alvo *o ) { return alvosMensagem( msgReiniciar( ), o ); } },
  { "dialogo_mudo", []( Pintor &p ) { desenharDialogo( p, mudo( ) ); }, []( Alvo *o ) { return alvosDialogo( mudo( ), o ); } },
  { "dialogo_excluir", []( Pintor &p ) { desenharDialogo( p, excluir( ) ); }, []( Alvo *o ) { return alvosDialogo( excluir( ), o ); } },
  { "dialogo_ap", []( Pintor &p ) { desenharDialogo( p, redeAp( ) ); }, []( Alvo *o ) { return alvosDialogo( redeAp( ), o ); } },
};
const int kAmostrasN = sizeof kAmostras / sizeof kAmostras[0];

}  // namespace amostra
