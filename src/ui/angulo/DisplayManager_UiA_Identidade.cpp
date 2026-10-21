/**
 * @file DisplayManager_UiA_Identidade.cpp
 * @brief The Ângulo build's glue for the "identidade" screens (tools/tft_ui/ESPEC_UI_A.md §6.4, §7):
 *        who is at the panel (MODE_AUTH_USER), the PIN keypad (MODE_AUTH), the text
 *        keyboard (MODE_SETTINGS_PASSWORD), the list of accounts (MODE_SETTINGS_USERS)
 *        and one account (MODE_SETTINGS_USER_EDIT). pico_wiretft_angulo only.
 *
 * The view models come from the members the old draw functions read, and every touch
 * does what the old branch did for the same request. Each action cites the old line it
 * reproduces (file:line of 2026-10-14, DisplayManager_Users.cpp unless said otherwise).
 *
 * Touch, per mode (ESPEC §4):
 *  - MODE_AUTH runs the core's keypad actions (DisplayManager_Actions.h, S9 C2): the
 *    predicate that holds every control but Cancelar under a lockout, the popup's drop, the
 *    backspace and the card with their re-deal, Entrar, the popup and the ordered keys.
 *    The lockout ladder, the deal and Core 0's comparison are the core's and do not move.
 *  - The other four are Controllers: the same functions, fields and permission checks. The text
 *    keyboard's result button, X, OK and boxes run the core's password actions since S9 C5
 *    (actPwdResult( ), actPwdLeave( ), actPwdNameAccept( ), actPwdLegacyOk( ), actPwdToggleShow( )),
 *    and its keys, space and backspace since S9 C6 (actPwdAppend( ), actPwdBackspace( )); its layers,
 *    Shift, error line and the new name's check (S9_PLANO.md D2) stay here.
 */

#include "../../DisplayManager.h"


#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "../../PinKeypad.h"
#include "../../StorageManager.h"
#include "../../SystemDefs_Validate.h"
#include "Angulo.h"
#include "IdentidadeToque.h"
#include "UiA.h"
#include "telas/Base.h"
#include "telas/IdentidadeContas.h"
#include "telas/IdentidadePin.h"
#include "telas/IdentidadeTexto.h"
#include "../../DisplayManager_Actions.h"   /* the core's touch actions, which the PIN keypad runs; last, as in the other glue */

namespace {

/* The zones of acceptTouch( ) / acceptHoldTouch( ): one per action, in a range no old
 * handler uses (grep of 2026-10-14: the identity modes use 0..54 and 0x5F..0x7B, the
 * other modes 0x80..0x93 and 0xC0 up). They only matter within one press (a held arrow
 * repeats on the zone it started on). */
enum : uint8_t {
  Z_QUEM_LINHA = 0xA0, Z_QUEM_VOLTAR, Z_QUEM_ANTERIOR, Z_QUEM_PROXIMA,
  Z_USR_LINHA = 0xA4, Z_USR_NOVA, Z_USR_VOLTAR, Z_USR_ANTERIOR, Z_USR_PROXIMA,
  Z_CTA_INTERRUPTOR = 0xA9, Z_CTA_PIN, Z_CTA_EXCLUIR, Z_CTA_SALVAR, Z_CTA_CANCELAR, Z_CTA_VOLTAR,
  Z_TXT_TECLA = 0xB0, Z_TXT_ESPACO, Z_TXT_APAGAR, Z_TXT_SHIFT, Z_TXT_CAMADA, Z_TXT_VER, Z_TXT_SALVAR,
  Z_TXT_CANCELAR, Z_TXT_RESULTADO,
};

/* What only the new text keyboard has: the layer on screen, the shift, and the refusal
 * on its line. The old keyboard had a popup per group instead. Reset when the mode is
 * entered (see uaMontarIdentidade( )) and when it is left by an action of ours. */
ua::tela::TextoCamada s_camada = ua::tela::TextoCamada::MIN;
ua::tela::TextoShift s_shift = ua::tela::TextoShift::OFF;
char s_erro[96] = "";
UiMode s_modoAnterior = MODE_DASHBOARD;
uint32_t s_ultimaMontagemTexto = 0;

void zerarTeclado( ) {
  s_camada = ua::tela::TextoCamada::MIN;
  s_shift = ua::tela::TextoShift::OFF;
  s_erro[0] = 0;
}

/* UserAccount::username is char[16] and nothing promises its NUL: read at most 16. */
void copiarNome( char *dst, int cap, const char *username ) {
  char tmp[17];
  const size_t n = strnlen( username, 16 );
  memcpy( tmp, username, n );
  tmp[n] = 0;
  ua::copiar( dst, tmp, cap );
}

/* The three panel bits as the screens number them (IdentidadeContas.h). */
uint8_t permsDe( uint16_t p ) {
  using namespace ua::tela;
  return (uint8_t)( ( ( p & PERM_ALARM_LIMITS ) ? CTA_PERM_LIMITES : 0 ) | ( ( p & PERM_ALARM_BLOCK ) ? CTA_PERM_ALARMES : 0 ) |
                    ( ( p & PERM_MAINT ) ? CTA_PERM_MANUT : 0 ) );
}

/* The name as Core 0 will read it: trailing spaces are the keyboard's, not the name's
 * (AppManager_Panel.cpp:399-400). */
void aparar( const char *src, char *dst, int cap ) {
  ua::copiar( dst, src, cap );
  for ( int n = (int)strlen( dst ) - 1; n >= 0 && dst[n] == ' '; n-- ) dst[n] = 0;
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Frames                                                              */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontarIdentidade( UaQuadro &q ) {
  using namespace ua::tela;
  const uint32_t agora = millis( );
  /* The text keyboard starts on abc with no refusal. The previous mode and a gap in the
   * calls say it was entered anew: while it is current this runs at least every 250 ms
   * (uaQuadro), so a gap above a second means the mode was left, also through modes
   * that another group's builder answers before this one is asked. */
  if ( _uiMode == MODE_SETTINGS_PASSWORD ) {
    if ( s_modoAnterior != MODE_SETTINGS_PASSWORD || agora - s_ultimaMontagemTexto > 1000 ) zerarTeclado( );
    s_ultimaMontagemTexto = agora;
  }
  s_modoAnterior = _uiMode;

  switch ( _uiMode ) {
    case MODE_AUTH_USER:
    case MODE_SETTINGS_USERS: {
      if ( !_sysConfigPtr ) return false;        // drawUserList( ) does the same (:933)
      /* The page the old draw clamped (:936-938). Rows to a page: "who is at the panel" keeps the old
       * four; the list of accounts has rows of two lines, three to a page (ident::LINHAS_2). */
      const int porPagina = _uiMode == MODE_AUTH_USER ? ident::LINHAS : ident::LINHAS_2;
      int paginas = ( _usersCount + porPagina - 1 ) / porPagina;
      if ( paginas == 0 ) paginas = 1;
      if ( _usersPage >= paginas ) _usersPage = paginas - 1;
      if ( _usersPage < 0 ) _usersPage = 0;
      const int ini = _usersPage * porPagina;
      if ( _uiMode == MODE_AUTH_USER ) {
        VmQuem &vm = uaVm<VmQuem>( );
        vm.total = (uint8_t)_usersCount;
        vm.pagina = (uint8_t)_usersPage;
        for ( int i = 0; i < ident::LINHAS && ini + i < _usersCount; i++ ) {
          const int slot = _usersMap[ini + i];
          copiarNome( vm.linhas[i].nome, sizeof vm.linhas[i].nome, _sysConfigPtr->users[slot].username );
          vm.linhas[i].travada = ( _slotLocked & ( 1u << (unsigned)slot ) ) != 0;   // drawUserRowInto( ) :894
          vm.nLinhas++;
        }
        uaUsar<VmQuem, desenharQuem, alvosQuem>( q, vm );
      } else {
        VmUsuarios &vm = uaVm<VmUsuarios>( );
        vm.total = (uint8_t)_usersCount;
        vm.pagina = (uint8_t)_usersPage;
        vm.podeCriar = ( _panelPerms & PERM_USER_MGR ) != 0;                       // the old NOVO's check (:1551)
        for ( int i = 0; i < ident::LINHAS_2 && ini + i < _usersCount; i++ ) {
          const UserAccount &u = _sysConfigPtr->users[_usersMap[ini + i]];
          copiarNome( vm.linhas[i].nome, sizeof vm.linhas[i].nome, u.username );
          vm.linhas[i].perms = permsDe( u.permissions );                           // the L, B, M letters (:860-874)
          vm.linhas[i].temPin = StorageManager::userHasPin( u );                   // the dot after the name (:920)
          vm.nLinhas++;
        }
        uaUsar<VmUsuarios, desenharUsuarios, alvosUsuarios>( q, vm );
      }
      return true;
    }

    case MODE_SETTINGS_USER_EDIT: {
      if ( !_sysConfigPtr ) return false;        // drawUserEdit( ) does the same (:1022)
      VmConta &vm = uaVm<VmConta>( );
      vm.nova = _userEditNew;
      vm.perms = permsDe( _userEditPerms );      // the copy being edited, not the stored bits (:1005)
      if ( _userEditNew ) {
        copiarNome( vm.nome, sizeof vm.nome, _newUserName );                       // the title (:1030-1032)
      } else if ( _userEditSlot >= 0 && _userEditSlot < MAX_USERS ) {
        const UserAccount &u = _sysConfigPtr->users[_userEditSlot];
        copiarNome( vm.nome, sizeof vm.nome, u.username );
        vm.temPin = StorageManager::userHasPin( u );
      }
      uaUsar<VmConta, desenharConta, alvosConta>( q, vm );
      return true;
    }

    case MODE_AUTH: {
      VmPin &vm = uaVm<VmPin>( );
      uint8_t minLen, kb, alfa;
      pinPolicy( minLen, kb, alfa );
      const bool alnum = alfa == PinKb::ALPHA_ALNUM;
      const bool ordenado = pinOrderedNow( );
      const PinKb::Grid g = pinGrid( );
      vm.alfanumerico = alnum;
      /* The layout on the glass: the old draw picks it the same way (:565-572). */
      vm.teclado = ua::pinTecladoDe( ordenado, alnum, g );
      if ( !ordenado ) {
        // The deal, glyph for glyph (drawPinCardInto( ) :378-405); the screen draws decoys and digits alike.
        for ( int k = 0; k < (int)g.keys && k < PIN_CARTOES_MAX; k++ )
          for ( int s = 0; s < (int)g.slots && s < PIN_GLIFOS_MAX; s++ ) vm.cartoes[k][s] = _pinKeyChars[k][s];
      }
      vm.fase = _pinPurpose == PIN_FOR_AUTH ? PinFase::IDENTIFICAR : ( _pinPhase == 0 ? PinFase::DEFINIR : PinFase::CONFIRMAR );   // :545-546
      vm.digitados = _pinLen;
      vm.minimo = pinEntryMinLen( );             // the floor of the places (:512)
      vm.teto = (uint8_t)( PinKb::maxLenFor( kb ) < PIN_MAX_LEN ? PinKb::maxLenFor( kb ) : PIN_MAX_LEN );   // pinAppendChar( ) :118
      vm.pressionada = -1;                       // the old keypad shows no pressed key
      vm.popup = ( ordenado && alnum ) ? _pinPopup : (int8_t)-1;
      // Whose PIN: the account the picker chose, the one being edited, or the new name.
      if ( _pinPurpose == PIN_FOR_NEW_ACCOUNT ) copiarNome( vm.conta, sizeof vm.conta, _newUserName );
      else if ( _pinPurpose != PIN_FOR_OWN && _pinTarget >= 0 && _pinTarget < MAX_USERS && _sysConfigPtr )
        copiarNome( vm.conta, sizeof vm.conta, _sysConfigPtr->users[_pinTarget].username );

      /* The state, in the order the old screen gives them weight: a panel out of tries until
       * the restart, an account out of tries (the picker refuses it, :1210), the wait, Core 0
       * answering (:489-493), a refusal (:489-500). */
      const bool acontaTravada = _pinPurpose == PIN_FOR_AUTH && _pinTarget >= 0 && _pinTarget < MAX_USERS &&
                                 ( _slotLocked & ( 1u << (unsigned)_pinTarget ) ) != 0;
      const bool espera = _lockoutUntil > 0 && !timeReached( _lockoutUntil );     // :539
      if ( _permanentLockout ) {
        vm.estado = PinEstado::BLOQUEIO_PAINEL;
      } else if ( acontaTravada ) {
        vm.estado = PinEstado::BLOQUEIO_CONTA;
      } else if ( espera ) {
        vm.estado = PinEstado::ESPERA;
        vm.esperaS = (uint16_t)( timeRemaining( _lockoutUntil ) / 1000 + 1 );   // :604
      } else if ( _pinWaiting ) {
        vm.estado = PinEstado::AGUARDANDO;
      } else if ( _pinMsg != TR_KEYS_COUNT ) {
        vm.estado = PinEstado::ERRO;
        vm.erro = _pinMsg == TR_PIN_TOO_SHORT ? PinErro::CURTO : _pinMsg == TR_PIN_MISMATCH ? PinErro::DIFERENTES : PinErro::INCORRETO;
      } else {
        vm.estado = PinEstado::LIVRE;
      }
      uaUsar<VmPin, desenharPin, alvosPin>( q, vm );
      return true;
    }

    case MODE_SETTINGS_PASSWORD: {
      if ( _kbPhase >= 2 ) {
        /* The verdict of the legacy PIN (only `screen pwd` reaches it; drawPasswordMessage( )). */
        VmMensagem &vm = uaVm<VmMensagem>( );
        vm.ok = _kbPhase == 3;
        if ( _kbMsgKey < TR_KEYS_COUNT ) uaLatin1ParaUtf8( tr( _kbMsgKey ), vm.texto, sizeof vm.texto );
        ua::copiar( vm.botao, "Entendi", sizeof vm.botao );
        uaUsar<VmMensagem, desenharMensagem, alvosMensagem>( q, vm );
        return true;
      }
      VmTexto &vm = uaVm<VmTexto>( );
      const bool nome = _kbPurpose == 1;
      vm.camada = s_camada;
      vm.shift = s_shift;
      vm.secreto = !nome;                        // a name is shown, the legacy PIN is dots (Settings.cpp:641-680)
      vm.ver = !nome && _kbShowRaw;
      vm.maximo = (uint8_t)kbMaxLen( );
      vm.pressionada = -1;
      const char *ativo = _kbPhase == 0 ? _kbBuffer : _kbConfirmBuf;
      const size_t n = strnlen( ativo, sizeof _kbBuffer );
      memcpy( vm.texto, ativo, n < sizeof vm.texto - 1 ? n : sizeof vm.texto - 1 );
      if ( nome ) {
        ua::copiar( vm.titulo, "Nome da conta", sizeof vm.titulo );
        ua::copiar( vm.rotulo, "Seguir", sizeof vm.rotulo );
        ua::copiar( vm.ajuda, "Aspas e barra invertida não valem.", sizeof vm.ajuda );
      } else {
        ua::copiar( vm.titulo, _kbPhase == 0 ? "Nova senha" : "Confirmar senha", sizeof vm.titulo );
        ua::copiar( vm.rotulo, _kbPhase == 0 ? "Seguir" : "Salvar", sizeof vm.rotulo );
        ua::copiar( vm.ajuda, "De 4 a 7 caracteres.", sizeof vm.ajuda );
      }
      ua::copiar( vm.erro, s_erro, sizeof vm.erro );
      uaUsar<VmTexto, desenharTexto, alvosTexto>( q, vm );
      return true;
    }

    default:
      return false;
  }
}

/* ------------------------------------------------------------------ */
/* Touch                                                               */
/* ------------------------------------------------------------------ */

UaToque DisplayManager::uaToqueIdentidade( const ua::Alvo *a, int16_t &x, int16_t &y ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    /* ---- the PIN keypad: Controller (S9 C2). Each target runs the core's keypad action under the original's zone (0, 1, 2, 10 + k,
     * 20 + k, 40 + the popup key + 1), in the original's order: Cancelar answers under a lockout, every other control waits for
     * pinInputBlocked( ), Apagar and Entrar close the popup before acting. ---- */
    case MODE_AUTH: {
      /* With the alphanumeric keyboard's group popup up, the popup owns every touch: one of its keys types its character, anything else, a
       * key of the keyboard behind it or no target at all, only closes it (the original's rule). */
      const bool popupAberto = _pinPopup >= 0 && pinOrderedNow( ) && pinAlphabet( ) == PinKb::ALPHA_ALNUM;
      const char *mem = popupAberto ? PinKb::GROUP_CHARS[_pinPopup] : "";
      const int n = (int)strlen( mem );
      if ( !a ) {
        if ( popupAberto && !pinInputBlocked( ) && acceptTouch( 40 ) ) actPinPopupKey( mem, n, -1 );
        return UaToque::TRATADO;
      }
      switch ( a->tipo ) {
        /* Cancelar: the way out of the screen, and the only control that has to work under a lockout. */
        case PIN_CANCELAR:
          if ( !acceptTouch( 0 ) ) break;
          pinCancel( );
          break;
        case PIN_APAGAR:
          if ( pinInputBlocked( ) ) break;
          actPinPopupDrop( );
          if ( !acceptTouch( 1 ) ) break;
          actPinBackspace( );
          break;
        case PIN_ENTRAR:
          if ( pinInputBlocked( ) ) break;
          actPinPopupDrop( );
          if ( !acceptTouch( 2 ) ) break;
          actPinOk( );
          break;
        case PIN_POPUP:
        case PIN_TECLA: {
          if ( pinInputBlocked( ) ) break;
          if ( popupAberto ) {
            const int j = ( a->tipo == PIN_POPUP && a->indice >= 0 && a->indice < n ) ? a->indice : -1;
            if ( !acceptTouch( (uint8_t)( 40 + ( j < 0 ? 0 : j + 1 ) ) ) ) break;
            actPinPopupKey( mem, n, j );
            break;
          }
          if ( a->tipo != PIN_TECLA ) break;
          const PinKb::Grid g = pinGrid( );
          const int k = a->indice;
          /* An index the grid does not have is no key, as the old translation said; on the ordered keyboard neither is an empty face, one
           * of the numeric pad's two blank cells. */
          if ( pinOrderedNow( ) ) {
            if ( k >= g.keys ) break;
            if ( k < 0 || !_pinKeyChars[k][0] ) break;
            if ( !acceptTouch( (uint8_t)( 20 + k ) ) ) break;
            actPinOrderedKey( k, pinAlphabet( ) );
          } else {
            if ( k < 0 || k >= g.keys ) break;
            if ( !acceptTouch( (uint8_t)( 10 + k ) ) ) break;
            actPinCard( g, k );
          }
          break;
        }
        default:
          break;
      }
      return UaToque::TRATADO;
    }

    /* ---- who is at the panel: Controller ---- */
    case MODE_AUTH_USER: {
      if ( !a ) return UaToque::TRATADO;
      switch ( a->tipo ) {
        case QUEM_LINHA: {
          const int idx = a->indice;
          if ( idx < 0 || idx >= _usersCount ) break;
          if ( !acceptTouch( Z_QUEM_LINHA ) ) break;
          _usersSel = idx; _usersPage = idx / 4;
          actUserOpen( true );                   // the account's PIN entry; a locked account only beeps
          break;
        }
        case QUEM_VOLTAR:                        // the old SAIR (:1541-1543)
          if ( !acceptTouch( Z_QUEM_VOLTAR ) ) break;
          forceDashboard( );
          break;
        case QUEM_PAG_ANTERIOR:                  // the old up arrow, held (:1531-1535), by page and without wrapping
          if ( !acceptHoldTouch( Z_QUEM_ANTERIOR ) ) break;
          if ( _usersPage > 0 ) { _usersPage--; _usersSel = _usersPage * 4; }
          _repaintSettings = true;
          break;
        case QUEM_PAG_PROXIMA:                   // the old down arrow (:1536-1540)
          if ( !acceptHoldTouch( Z_QUEM_PROXIMA ) ) break;
          if ( ( _usersPage + 1 ) * 4 < _usersCount ) { _usersPage++; _usersSel = _usersPage * 4; }
          _repaintSettings = true;
          break;
        default: break;
      }
      return UaToque::TRATADO;
    }

    /* ---- the list of accounts: Controller ---- */
    case MODE_SETTINGS_USERS: {
      if ( !a ) return UaToque::TRATADO;
      switch ( a->tipo ) {
        case USR_LINHA: {
          const int idx = a->indice;
          if ( idx < 0 || idx >= _usersCount ) break;
          if ( !acceptTouch( Z_USR_LINHA ) ) break;
          _usersSel = idx; _usersPage = idx / ident::LINHAS_2;       /* three rows to a page, not the old four */
          actUserOpen( false );                  // the account's editor
          break;
        }
        case USR_NOVA:                           // the old NOVO (:1546-1556)
          if ( !acceptTouch( Z_USR_NOVA ) ) break;
          actUserNew( );                         // behind PERM_USER_MGR, name first on the text keyboard
          break;
        case USR_VOLTAR:                         // the old SAIR (:1541-1544)
          if ( !acceptTouch( Z_USR_VOLTAR ) ) break;
          showSettingsMain( );
          break;
        case USR_PAG_ANTERIOR:                   // :1531-1535, three rows to a page
          if ( !acceptHoldTouch( Z_USR_ANTERIOR ) ) break;
          if ( _usersPage > 0 ) { _usersPage--; _usersSel = _usersPage * ident::LINHAS_2; }
          _repaintSettings = true;
          break;
        case USR_PAG_PROXIMA:                    // :1536-1540, three rows to a page
          if ( !acceptHoldTouch( Z_USR_PROXIMA ) ) break;
          if ( ( _usersPage + 1 ) * ident::LINHAS_2 < _usersCount ) { _usersPage++; _usersSel = _usersPage * ident::LINHAS_2; }
          _repaintSettings = true;
          break;
        default: break;
      }
      return UaToque::TRATADO;
    }

    /* ---- one account: Controller ---- */
    case MODE_SETTINGS_USER_EDIT: {
      if ( !a ) return UaToque::TRATADO;
      const bool lockedBits = ( !_userEditNew && _userEditSlot == 0 );          // the original's: the admin's bits are locked
      switch ( a->tipo ) {
        case CTA_INTERRUPTOR: {                  // rows 0-2: a bit of the copy (refused on the admin's account)
          const int idx = a->indice;
          if ( idx < 0 || idx > 2 ) break;
          if ( !acceptTouch( Z_CTA_INTERRUPTOR ) ) break;
          actUserEditActivate( idx, lockedBits );
          break;
        }
        case CTA_DEFINIR_PIN:                    // row 3: the account's PIN entry
          if ( !acceptTouch( Z_CTA_PIN ) ) break;
          actUserEditActivate( 3, lockedBits );
          break;
        case CTA_EXCLUIR:                        // row 4: confirm first
          if ( !acceptTouch( Z_CTA_EXCLUIR ) ) break;
          /* The original's editor has no delete row for the admin (userEditRowCount( ) gives 4); the Angulo lists Excluir on every account
           * that is not new, so its own guard keeps slot 0 out (S9_PLANO.md D3). */
          if ( lockedBits ) { _errorSoundPending = true; break; }
          actUserEditActivate( 4, lockedBits );
          break;
        case CTA_SALVAR:                         // the original's Salvar / Avancar
          if ( !acceptTouch( Z_CTA_SALVAR ) ) break;
          actUserEditSave( lockedBits );
          break;
        case CTA_CANCELAR:                       // the old BACK: back to the list, the bits dropped (:1666-1668)
        case CTA_VOLTAR:
          if ( !acceptTouch( a->tipo == CTA_VOLTAR ? Z_CTA_VOLTAR : Z_CTA_CANCELAR ) ) break;
          showSettingsUsers( );
          break;
        default: break;
      }
      return UaToque::TRATADO;
    }

    /* ---- the text keyboard: Controller ---- */
    case MODE_SETTINGS_PASSWORD: {
      /* The verdict of the legacy PIN: its one button runs actPwdResult( ) (a saved password goes back to the menu, a refusal starts again). */
      if ( _kbPhase >= 2 ) {
        if ( a && a->tipo == MSG_BOTAO && acceptTouch( Z_TXT_RESULTADO ) ) actPwdResult( );
        return UaToque::TRATADO;
      }
      if ( !a ) return UaToque::TRATADO;
      char *ativo = _kbPhase == 0 ? _kbBuffer : _kbConfirmBuf;
      /* One character into the buffer: the room test is this keyboard's own and the store is actPwdAppend( ), the original's popup key and space; the limit
       * is in BYTES, which is what the 15 of a name counts (every key types one ASCII byte). */
      auto digitar = [&]( char c ) {
        if ( !c || _kbCursor >= kbMaxLen( ) ) return;
        actPwdAppend( ativo, c );
        s_erro[0] = 0;
        if ( s_shift == TextoShift::ON ) s_shift = TextoShift::OFF;       // Shift types one capital
        _repaintSettings = true;
      };
      switch ( a->tipo ) {
        case TXT_TECLA:
          if ( !acceptTouch( Z_TXT_TECLA ) ) break;
          digitar( teclaCaractere( s_camada, s_shift, a->indice ) );
          break;
        case TXT_ESPACO:                         // the space, typed as a key
          if ( !acceptTouch( Z_TXT_ESPACO ) ) break;
          digitar( ' ' );
          break;
        case TXT_APAGAR:                         // the backspace, held to repeat
          if ( !acceptHoldTouch( Z_TXT_APAGAR ) ) break;
          if ( _kbCursor > 0 ) { actPwdBackspace( ativo ); s_erro[0] = 0; }
          _repaintSettings = true;
          break;
        case TXT_SHIFT:                          // OFF -> ON -> LOCK -> OFF
          if ( !acceptTouch( Z_TXT_SHIFT ) ) break;
          s_shift = s_shift == TextoShift::OFF ? TextoShift::ON : s_shift == TextoShift::ON ? TextoShift::LOCK : TextoShift::OFF;
          break;
        case TXT_CAMADA:
          if ( !acceptTouch( Z_TXT_CAMADA ) ) break;
          if ( a->indice >= 0 && a->indice <= (int)TextoCamada::SIMB ) s_camada = (TextoCamada)a->indice;
          break;
        case TXT_VER:                            // the boxes toggle mask/plain
          if ( !acceptTouch( Z_TXT_VER ) ) break;
          actPwdToggleShow( );
          break;
        case TXT_CANCELAR:                       // the X: back to the accounts (a new name) or to the menu
          if ( !acceptTouch( Z_TXT_CANCELAR ) ) break;
          zerarTeclado( );
          actPwdLeave( );
          break;
        case TXT_SALVAR:                         // the OK: a new account's name, or the legacy PIN's two passes
          if ( !acceptTouch( Z_TXT_SALVAR ) ) break;
          if ( _kbPurpose == 1 ) {
            /* The name of a new account. The old OK took one character and left Core 0 to refuse
             * the name after both passes of the PIN (TR_NAME_INVALID); here the same rules
             * (AppManager_Panel.cpp:399-404) answer on the keyboard, with the way out. */
            char nome[16];
            aparar( _kbBuffer, nome, sizeof nome );
            const char *erro = nullptr;
            if ( !nome[0] ) erro = TXT_ERRO_SO_ESPACOS;
            else if ( !isValidName( nome, 15 ) ) erro = TXT_ERRO_CARACTERES;
            else if ( strcasecmp( nome, "admin" ) == 0 ) erro = TXT_ERRO_RESERVADO;
            else if ( _sysConfigPtr ) {
              for ( int i = 0; i < MAX_USERS; i++ )
                if ( _sysConfigPtr->users[i].active && strcasecmp( nome, _sysConfigPtr->users[i].username ) == 0 )
                  erro = TXT_ERRO_REPETIDO;
            }
            if ( erro ) { ua::copiar( s_erro, erro, sizeof s_erro ); _errorSoundPending = true; break; }
            zerarTeclado( );
            actPwdNameAccept( nome );             // the name goes on to the editor of the new account
            break;
          }
          /* The legacy PIN (`screen pwd`, not in this build): the old two passes, as they were. */
          actPwdLegacyOk( );
          break;
        default: break;
      }
      return UaToque::TRATADO;
    }

    default:
      return UaToque::NAO_E_MEU;
  }
}

