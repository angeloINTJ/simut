/**
 * @file DisplayManager_UiA_Painel.cpp
 * @brief The Ângulo build's glue for the "painel" screens (tools/tft_ui/ESPEC_UI_A.md
 *        §6.1, telas/Painel*.h): the boot screen, the alarm's action screen and the
 *        dashboard, as view models filled from the DisplayManager's state, and their
 *        touches. pico_wiretft_angulo only.
 *
 * No touch here is a translation (ESPEC §7) any more: each target runs the core's action
 * for its zone (DisplayManager_Actions.h), the code handleTouch( ) runs for the same zone,
 * so the 3 s hold, the lift and the slide across the row work as they did when a touch
 * was moved into the old layout.
 */

#include "../../DisplayManager.h"
#include "../../simut_config.h"   /* SIMUT_NET_LAN is tested below: included here, not left to another header */


#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../net/WiredLink.h"
#include "../../sensors/SensorChannels.h"
#include "Angulo.h"
#include "UiA.h"
#include "telas/PainelAlarme.h"
#include "telas/PainelBoot.h"
#include "telas/PainelPrincipal.h"
#include "telas/Telas.h"
#include "../../DisplayManager_Actions.h"   /* the core's touch actions, which the alarm screen's buttons run; last */

namespace {

/* A float to tenths, the way the view models carry it; `ou` when it is not a number. */
int16_t decimos( float v, int16_t ou ) {
  if ( !isfinite( v ) ) return ou;
  const long d = lroundf( v * 10.0f );
  return (int16_t)( d > 3276 ? 3276 : d < -3276 ? -3276 : d );
}

/* What the sensor slot's alarm action asks for: actAlarmSilence( ) in DisplayManager_Actions.h (EVT_ALARM_SILENCE,
 * param 120). */
constexpr uint16_t SILENCIO_SEG = 120;

/* One card's data as the old drawSlotPanel( ) takes it (render( ), in DisplayManager_Render.cpp). */
struct Dados {
  float t, h, p;
  bool valido;
  SensorType tipo;
  int idx;
  char nome[32];
};

Dados dadosDe( float t, float h, float p, bool valido, SensorType tipo, int idx, const char *nome ) {
  Dados d{ t, h, p, valido, tipo, idx, "" };
  ua::copiar( d.nome, nome, sizeof d.nome );
  return d;
}

/* The drivers' dispatch (SensorPanelDispatch.h): a type nobody drew falls to the DS18B20's row. */
ua::tela::VmPainel::Tipo tipoDe( SensorType t ) {
  using Tipo = ua::tela::VmPainel::Tipo;
  switch ( t ) {
    case TYPE_DHT22: return Tipo::DHT22;
    case TYPE_BME280: return Tipo::BME280;
    case TYPE_BMP280: return Tipo::BMP280;
    default: return Tipo::DS18B20;
  }
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontarPainel( UaQuadro &q ) {
  using namespace ua::tela;

  /* The boot screen stands in for the dashboard only: render( ), whose boot
   * branch keeps _lastRenderedState and asks for a frame (DisplayManager.cpp),
   * runs in MODE_DASHBOARD alone, and a boot can open other screens over it
   * (the touch calibration of a first boot, handleTouch( )'s calibration branch;
   * the clock asked at boot, its clock-entry screen), which must not stay
   * hidden behind the log (review of 2026-10-14). */
  if ( _lastRenderedState.isBooting && _uiMode == MODE_DASHBOARD ) {
    VmBoot &vm = uaVm<VmBoot>( );
    /* The ring, oldest first: setBootStatusKey( ) shifts it toward 0 and writes
     * bootLogs[4] (DisplayManager.cpp). An entry nobody wrote still holds
     * the constructor's key == TR_KEYS_COUNT and an empty suffix
     * (the constructor, in DisplayManager.cpp): it has no text and is left out. The text is
     * composed as the old box did (render( ), in DisplayManager_Render.cpp), in Latin-1
     * from tr( ), and goes to the kit in UTF-8. */
    int n = 0;
    for ( int i = 0; i < 5; i++ ) {
      const BootLogEntry &e = _lastRenderedState.bootLogs[i];
      char cru[96];
      if ( e.key >= 0 && e.key < (int16_t)TR_KEYS_COUNT ) snprintf( cru, sizeof cru, "%s%s", tr( (LangKey)e.key ), e.suffix );
      else snprintf( cru, sizeof cru, "%s", e.suffix );
      if ( !cru[0] ) continue;
      uaLatin1ParaUtf8( cru, vm.passos[n++], sizeof vm.passos[0] );
    }
    vm.nPassos = (uint8_t)n;
    /* The version the old screen printed under the brand (render( ), in DisplayManager_Render.cpp). */
    ua::copiar( vm.versao, SIMUT_VERSION, sizeof vm.versao );
#if SIMUT_NET_LAN
    /* The old screen has no MAC. The one the web and the telemetry report
     * (NetworkManager_Wired.cpp getMacAddress( )): the board's, the same before
     * the module starts. */
    {
      uint8_t m[6];
      WiredLink::macAddress( m );
      snprintf( vm.mac, sizeof vm.mac, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5] );
    }
#endif
    /* -1 is "no bar" (SystemState's own value); the constructor's -2 before the first render is below 0 too. */
    vm.apProgressPct = (int8_t)( _lastRenderedState.apProgressPct > 100 ? 100 : _lastRenderedState.apProgressPct );
    vm.showSkipButton = _lastRenderedState.showSkipButton;
    uaUsar<VmBoot, desenharBoot, alvosBoot>( q, vm );
    return true;
  }

  switch ( _uiMode ) {
    case MODE_ALARM_ACTION: {
      VmAcaoAlarme &vm = uaVm<VmAcaoAlarme>( );
      uaVmStatus( vm.status );
      const int slot = _alarmActionSlot;
      const SensorRecord *rec = ( _sysConfigPtr && slot >= 0 && slot < MAX_SENSORS ) ? &_sysConfigPtr->sensors[slot] : nullptr;
      /* The name of the slot the buttons act on (id = _alarmActionSlot, which actAlarmSilence( ) and actAlarmDeactivate( ) send), from
       * the configuration. The old header printed the LOWER card's name whatever the slot
       * (DisplayManager_Alarm.cpp:76-84): opened from the upper card it named the wrong sensor. */
      if ( rec && rec->friendlyName[0] ) ua::copiar( vm.nome, rec->friendlyName, sizeof vm.nome );
      else snprintf( vm.nome, sizeof vm.nome, "Sensor %d", slot );   // the cards' fallback (drawSlotPanel( ))

      /* The reading: only the two cards' slots have a live one in the display (setSlotsSnapshot( ) is
       * the Alpha build's). The state is read as Core 0 keeps it, not as the last render saw it:
       * render( ) does not run in this mode. */
      bool achou = false, valido = false;
      float t = NAN;
      auto ler = [&]( const SystemState &s ) {
        if ( slot == s.selectedSlotIdx ) { achou = true; t = s.slotTemp; valido = s.slotValid; }
        else if ( slot == s.topSlotIdx ) { achou = true; t = s.topSlotTemp; valido = s.topSlotValid; }
      };
      if ( mutex_enter_timeout_us( &_stateMutex, 500 ) ) {
        ler( _sharedState );
        mutex_exit( &_stateMutex );
      } else {
        ler( _lastRenderedState );
      }

      /* What put the slot in alarm, compared in floats as the alarm itself does
       * (AppManager_HistoryAlarm.cpp: a channel outside chMin..chMax). Without the slot's reading
       * (neither card shows it any more) only the error mask can say: the rest is FORA. */
      vm.causa = VmAcaoAlarme::Causa::FORA;
      if ( isSlotErrAlarming( slot ) || ( achou && !valido ) ) {
        vm.causa = VmAcaoAlarme::Causa::ERRO;
      } else if ( achou && rec && isfinite( t ) ) {
        const float lo = rec->chMin[CH_TEMP], hi = rec->chMax[CH_TEMP];
        if ( t > hi ) { vm.causa = VmAcaoAlarme::Causa::ACIMA; vm.limite = decimos( hi, 0 ); }
        else if ( t < lo ) { vm.causa = VmAcaoAlarme::Causa::ABAIXO; vm.limite = decimos( lo, 0 ); }
      }
      vm.temp = achou ? decimos( t, 0 ) : 0;
      vm.silencioSeg = SILENCIO_SEG;
      uaUsar<VmAcaoAlarme, desenharAcaoAlarme, alvosAcaoAlarme>( q, vm );
      return true;
    }
    case MODE_DASHBOARD: {
      VmPainel &vm = uaVm<VmPainel>( );
      uaVmStatus( vm.status );
      const SystemState &st = _lastRenderedState;

      /* The two cards' data as render( ) hands it to drawSlotPanel( ): render( ) works on a copy of
       * the snapshot with two mirror rules and stores the snapshot itself as _lastRenderedState, so
       * the rules are applied here to the same snapshot, in the same order. The pressure goes along
       * with the rest (the old rules copy temperature, humidity, type, validity and name only, and
       * the mirrored card would print the pressure of whichever slot last set it). */
      Dados baixo = dadosDe( st.slotTemp, st.slotHum, st.slotPres, st.slotValid, st.slotType, st.selectedSlotIdx, st.slotName );
      Dados cima = dadosDe( st.topSlotTemp, st.topSlotHum, st.topSlotPres, st.topSlotValid, st.topSlotType, st.topSlotIdx, st.topSlotName );
      /* 1. A card that follows the selection and has no valid data shows the lower one
       *    (render( ), DisplayManager_Render.cpp); a PINNED card keeps its own, invalid or not. */
      if ( !_topPanel.fixed && !cima.valido ) cima = baixo;
      /* 2. Both on the same sensor: the same data (render( ), DisplayManager_Render.cpp). */
      if ( cima.idx == baixo.idx ) cima = baixo;

      /* One card. A slot with no running sensor comes as TYPE_NONE and "Empty / Inactive"
       * (AppManager_HistoryAlarm.cpp:71, 93): when the slot is not active in the configuration there
       * is no sensor to show (the screen says so); when it is, the sensor is missing and the card is
       * an error on the configured type and name, which says more than the old "Empty / Inactive". */
      auto preencher = [&]( VmPainel::Sensor &s, const Dados &d, const DashPanel &painel ) {
        const SensorRecord *rec = ( _sysConfigPtr && d.idx >= 0 && d.idx < MAX_SENSORS ) ? &_sysConfigPtr->sensors[d.idx] : nullptr;
        SensorType tipo = d.tipo;
        bool erro = !d.valido;
        char nome[32];
        ua::copiar( nome, d.nome, sizeof nome );
        if ( tipo == TYPE_NONE ) {
          if ( !rec || !rec->active ) { s.tipo = VmPainel::Tipo::NENHUM; return; }
          tipo = (SensorType)rec->sensorType;
          erro = true;
          nome[0] = 0;
          if ( rec->friendlyName[0] ) ua::copiar( nome, rec->friendlyName, sizeof nome );
        }
        if ( !nome[0] ) snprintf( nome, sizeof nome, "Sensor %d", d.idx );   // the old fallback (drawSlotPanel( ))
        ua::copiar( s.nome, nome, sizeof s.nome );
        s.tipo = tipoDe( tipo );
        s.erro = erro || !isfinite( d.t );
        s.temp = decimos( d.t, 0 );
        s.umid = decimos( d.h, VmPainel::SEM_VALOR );
        /* Whole hPa, truncated as the BME280 driver prints it (BME280Driver.h:393, "%d" of (int)p). */
        s.pres = ( isfinite( d.p ) && d.p >= 1.0f ) ? (uint16_t)( d.p > 65535.0f ? 65535.0f : d.p ) : 0;
        /* The day's extremes the panel keeps (setSlotMinMax( ), DisplayManager.cpp); NAN is no record yet. */
        s.tMin = decimos( painel.minTemp, VmPainel::SEM_VALOR );
        s.tMax = decimos( painel.maxTemp, VmPainel::SEM_VALOR );
        s.uMin = decimos( painel.minHum, VmPainel::SEM_VALOR );
        s.uMax = decimos( painel.maxHum, VmPainel::SEM_VALOR );
        /* The temperature channel's alarm limits (SensorRecord::chMin/chMax, indexed by channel). */
        if ( rec ) {
          s.lo = decimos( rec->chMin[CH_TEMP], 0 );
          s.hi = decimos( rec->chMax[CH_TEMP], 0 );
        }
        s.alarme = isSlotAlarming( d.idx ) || isSlotErrAlarming( d.idx );   // what makes the old card flash (drawSlotPanel( ))
        s.minmax = painel.showMinMax;
      };
      preencher( vm.cima, cima, _topPanel );
      preencher( vm.baixo, baixo, _bottomPanel );
      /* The old card shows no sign of a pin (§16.8 of the survey): the icon is for a card PINNED to a sensor. */
      vm.cimaFixada = _topPanel.fixed && _topPanel.fixedIdx >= 0;

      /* The alarm's flash and the 120 s silence (slotAlarmBg( ); _alarmSilenced). */
      vm.faseAcesa = _alarmFlashPhase;
      vm.silenciado = _alarmSilenced;

      /* The bottom row, as drawBottomButtons( ) builds it (DisplayManager_Dashboard.cpp): buildDashLayout( )
       * says what each of the five positions is, and the page button turns when an active slot in alarm
       * has no button on this page (the pinned slot, which has none on any, counts). */
      DashBtn btns[5];
      int totalPages = 1;
      bool paging = false;
      const int nBtns = buildDashLayout( btns, &totalPages, &paging );
      bool alarmeOutraPagina = false;
      if ( paging && _sysConfigPtr && ( _alarmSlotMask != 0 || _alarmErrMask != 0 ) ) {
        for ( int sl = 0; sl < MAX_SENSORS && !alarmeOutraPagina; sl++ ) {
          if ( !_sysConfigPtr->sensors[sl].active ) continue;
          if ( !isSlotAlarming( sl ) && !isSlotErrAlarming( sl ) ) continue;
          bool nestaPagina = false;
          for ( int i = 0; i < nBtns; i++ )
            if ( btns[i].kind == 0 && btns[i].slotId == sl ) { nestaPagina = true; break; }
          if ( !nestaPagina ) alarmeOutraPagina = true;
        }
      }
      for ( int i = 0; i < 5; i++ ) {
        VmPainel::Nav &nv = vm.nav[i];
        int8_t nav = UA_NAV_NADA;
        nv.tipo = VmPainel::NavTipo::NADA;
        if ( btns[i].kind == 0 ) {
          nv.tipo = VmPainel::NavTipo::SLOT;
          nv.slot = (uint8_t)btns[i].slotId;
          nv.selecionado = btns[i].slotId == st.selectedSlotIdx;   // drawBottomButtons( state.selectedSlotIdx )
          nv.alarme = isSlotAlarming( btns[i].slotId ) || isSlotErrAlarming( btns[i].slotId );
          nav = btns[i].slotId;
        } else if ( btns[i].kind == 1 ) {
          nv.tipo = VmPainel::NavTipo::CFG;
          nav = UA_NAV_CFG;
        } else if ( btns[i].kind == 2 ) {
          nv.tipo = VmPainel::NavTipo::PAGINA;
          nv.alarme = alarmeOutraPagina;
          nav = UA_NAV_PAGINA;
        }
        g_uaNav[i] = nav;   // GET /api/ui tells the bench which slot each button is
      }
      vm.pagina = (uint8_t)( _currentPage + 1 );
      vm.paginas = (uint8_t)totalPages;

      /* The web is saving: touch stays refused and the notice says who (the sticky flag, as
       * handleTouch( ) and tickDashboard( ) read it: the web-busy refusal in handleTouch( )). The name
       * is only meaningful while busy: setWebBusy( ) writes it, nothing clears it. */
      vm.webOcupada = _lastWebBusy;
      if ( vm.webOcupada ) ua::copiar( vm.contaWeb, _webBusyUser, sizeof vm.contaWeb );
      uaUsar<VmPainel, desenharPainel, alvosPainel>( q, vm );
      return true;
    }
    default:
      return false;
  }
}

UaToque DisplayManager::uaToquePainel( const ua::Alvo *a, int16_t &, int16_t & ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_DASHBOARD:
      /* A boot frame still on the glass: this touch came in the pass where endBoot( ) cleared
       * _sharedState.isBooting (handleTouch( ) answers the boot's own touch first, in its isBooting branch)
       * and render( ) has not run yet. Nothing in that frame is for the actions below (BOOT_PULAR
       * would read as the upper card). */
      if ( _lastRenderedState.isBooting || !a ) return UaToque::TRATADO;
      /* Every target runs the core's action behind the gate its old zone held (DisplayManager_Actions.h): the upper card's is
       * inside its action (the lock tail and the first touch), the lower card's is acceptTouch( 1 ), the row's are inside its
       * action. Each sample of a press reaches the action, so the 3 s hold keeps its clock, the lift is answered at the top of
       * handleTouch( ), and a slide across the row selects slot after slot, as they did when the point was moved. */
      switch ( a->tipo ) {
        case PNL_CARTAO_CIMA: actTopCardPress( false ); return UaToque::TRATADO;
        case PNL_GRAFICO_CIMA: actTopCardPress( true ); return UaToque::TRATADO;
        case PNL_CARTAO_BAIXO:
          if ( acceptTouch( 1 ) ) actBottomCardTap( false );
          return UaToque::TRATADO;
        case PNL_GRAFICO_BAIXO:
          if ( acceptTouch( 1 ) ) actBottomCardTap( true );
          return UaToque::TRATADO;
        case PNL_NAV:
          actNavButton( a->indice );
          return UaToque::TRATADO;
        default: return UaToque::TRATADO;
      }
    case MODE_ALARM_ACTION:
      /* The three buttons run the core's actions behind the gates the old zones held (silence acceptTouch( 0 ), deactivate
       * ( 1 ), min/max ( 2 ): Touch.cpp), not the old branch with a point moved into its zone. */
      if ( !a ) return UaToque::TRATADO;
      switch ( a->tipo ) {
        case ACA_SILENCIAR:
          if ( acceptTouch( 0 ) ) actAlarmSilence( );
          break;
        case ACA_DESATIVAR:
          if ( acceptTouch( 1 ) ) actAlarmDeactivate( );
          break;
        case ACA_MINMAX:
          if ( acceptTouch( 2 ) ) actAlarmShowMinMax( );
          break;
        default: break;
      }
      return UaToque::TRATADO;
    default:
      return UaToque::NAO_E_MEU;
  }
}

