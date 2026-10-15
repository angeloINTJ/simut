/**
 * @file DisplayManager_UiA_Graficos.cpp
 * @brief The Ângulo build's glue for the "graficos" screens (telas/Graficos.h,
 *        tools/tft_ui/ESPEC_UI_A.md): view models from the DisplayManager's
 *        state, and their touches. pico_wiretft_angulo only.
 *
 * Four modes are drawn here: MODE_GRAPH_LOADING, MODE_GRAPH_VIEW,
 * MODE_GRAPH_DETAIL and MODE_CALENDAR, plus the update screen (uaMontarOta( ),
 * which Core 0 also calls through uiPaintOta( ) whatever _uiMode says). The old
 * draw functions read the same state, under the same locks; uiPass( ) takes the
 * repaint requests that used to call them.
 */

#include "../../DisplayManager.h"


#include <stdio.h>
#include <string.h>

#include "Angulo.h"
#include "GraficosPreencher.h"
#include "UiA.h"
#include "telas/Graficos.h"
#include "../../display/GraphNav.h"   /* calDayHasHistory( ): the calendar's day test */
#include "../../DisplayManager_Actions.h"   /* the core's touch actions, which the graph screens' buttons run; last */

namespace {

/* The update screen's numbering is the model's: the glue converts with a cast,
 * so a renumbered phase or reason must stop the build, not the panel. */
static_assert( (int)ua::tela::FaseOta::NENHUMA == OTA_PH_NONE && (int)ua::tela::FaseOta::RECEBENDO == OTA_PH_RECEIVING &&
                   (int)ua::tela::FaseOta::CONFERINDO == OTA_PH_CHECKING && (int)ua::tela::FaseOta::CONFERIDA == OTA_PH_READY &&
                   (int)ua::tela::FaseOta::INSTALANDO == OTA_PH_INSTALLING && (int)ua::tela::FaseOta::RECUSADA == OTA_PH_REFUSED &&
                   (int)ua::tela::FaseOta::INTERROMPIDA == OTA_PH_CUT,
               "FaseOta is numbered as OtaPhase" );
static_assert( (int)ua::tela::MotivoOta::NENHUM == OTA_WHY_NONE && (int)ua::tela::MotivoOta::MODELO == OTA_WHY_MODEL &&
                   (int)ua::tela::MotivoOta::SEM_ASSINATURA == OTA_WHY_UNSIGNED && (int)ua::tela::MotivoOta::ASSINATURA == OTA_WHY_SIGNATURE &&
                   (int)ua::tela::MotivoOta::CHAVE_APOSENTADA == OTA_WHY_RETIRED_KEY &&
                   (int)ua::tela::MotivoOta::VERSAO_BLOQUEADA == OTA_WHY_BLOCKED_VERSION &&
                   (int)ua::tela::MotivoOta::CHAVE_DE_BANCADA == OTA_WHY_BENCH_KEY && (int)ua::tela::MotivoOta::DANIFICADA == OTA_WHY_DAMAGED,
               "MotivoOta is numbered as OtaWhy" );

/* The busy line. A tap that asks Core 0 for another window (a step in time,
 * another period, another month) leaves the old data on the glass until the
 * read lands, which takes from 0.2 s to seconds (the old panel painted a hint
 * the instant the tap was accepted: drawGraphBusyHint( ),
 * DisplayManager_Graph.cpp:64). Here the screens draw it from the model, so it
 * goes away with the frame that replaces it. The request is over when the data
 * on hand is no longer the data it was made against (`assinatura`), when the
 * mode is no longer the one it was made in, or after 8 s: Core 0 gives a read
 * 6 s (GRAPH_BUDGET_MS, AppManager_Graph.cpp:126) and a failed one goes to the
 * dashboard without a word. */
constexpr uint32_t OCUPADO_MAX_MS = 8000;
bool s_ocupado = false;
UiMode s_modoOcupado = MODE_DASHBOARD;
uint32_t s_assinaturaOcupado = 0, s_inicioOcupado = 0;

void pedirDados( UiMode modo, uint32_t assinatura ) {
  s_ocupado = true;
  s_modoOcupado = modo;
  s_assinaturaOcupado = assinatura;
  s_inicioOcupado = millis( );
}

bool aguardandoDados( UiMode modo, uint32_t assinatura ) {
  if ( !s_ocupado ) return false;
  if ( s_modoOcupado != modo || s_assinaturaOcupado != assinatura || (uint32_t)( millis( ) - s_inicioOcupado ) >= OCUPADO_MAX_MS )
    s_ocupado = false;
  return s_ocupado;
}

}  // namespace

/* ------------------------------------------------------------------ */
/* The update screen                                                   */
/* ------------------------------------------------------------------ */

/* The phase, the reason of a refusal and the percentage, from the one word
 * Core 0 publishes (OtaScreen.h). Called from Core 0 too, with Core 1 parked
 * (uiPaintOta( )), so it reads only the atomics. */
bool DisplayManager::uaMontarOta( UaQuadro &q ) {
  using namespace ua::tela;
  const uint32_t st = __atomic_load_n( &_otaState, __ATOMIC_ACQUIRE );
  VmOta &vm = uaVm<VmOta>( );
  vm.fase = static_cast<FaseOta>( otaPhaseOf( st ) );
  if ( otaPhaseOf( st ) == OTA_PH_REFUSED ) vm.motivo = static_cast<MotivoOta>( otaWhyOf( st ) );
  vm.pct = __atomic_load_n( &_otaPct, __ATOMIC_ACQUIRE );
  uaUsar<VmOta, desenharOta, alvosOta>( q, vm );
  return true;
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontarGraficos( UaQuadro &q ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_OTA_UPDATE:
      return uaMontarOta( q );

    case MODE_GRAPH_LOADING: {
      /* What the screen says it is loading. Core 0 asks for it with
       * requestLoadingScreen( ) and no arguments (AppManager_Events.cpp:168
       * and :230), right after taking EVT_OPEN_GRAPH or EVT_CALENDAR_DAY off
       * the ring, so the event that asked is the last one Core 0 consumed: its
       * sensor and, for a day, the 24 h that is forced. The request arrives
       * with _loadingDrawn = false, which marks it fresh; the pair is kept
       * until the next one. */
      static int s_sensor = -1, s_periodo = RANGE_24H;
      if ( !_loadingDrawn ) {
        const uint32_t h = __atomic_load_n( &_evHead, __ATOMIC_ACQUIRE );
        const UiEvent &ev = _evRing[( h - 1 ) % UI_EV_RING];
        s_sensor = -1;
        s_periodo = RANGE_24H;
        if ( ev.type == UiEvent::EVT_OPEN_GRAPH ) {
          s_sensor = ev.id;
          if ( ev.param >= RANGE_1H && ev.param <= RANGE_1W ) s_periodo = ev.param;
        } else if ( ev.type == UiEvent::EVT_CALENDAR_DAY ) {
          s_sensor = ev.id;
        }
      }
      VmCarregando &vm = uaVm<VmCarregando>( );
      ua::cola::nomeDoSensor( _sysConfigPtr, s_sensor, vm.sensor, sizeof vm.sensor );
      vm.periodo = (uint8_t)s_periodo;
      /* The segment of the bar moves every 250 ms: whole steps, not millis( ),
       * so the model changes four times a second and no faster. */
      vm.fase = (uint8_t)( ( millis( ) / 250 ) % FASES_CARREGANDO );
      uaUsar<VmCarregando, desenharCarregando, alvosCarregando>( q, vm );
      /* The notice drawLoadingScreen( ) gave Core 0, which waits for it up to
       * 500 ms before it reads the history (AppManager_Events.cpp:169-173):
       * uiPass( ) takes the request that called it, so this is where it is given
       * (DisplayManager_Graph.cpp:50 is the old place). */
      _loadingDrawn = true;
      return true;
    }

    case MODE_GRAPH_VIEW: {
      __dmb( );   /* as drawGraphScreen( ): Core 0 wrote the package, then the flag */
      VmGrafico &vm = uaVm<VmGrafico>( );
      ua::cola::preencherGrafico( vm, _graphData, _currentMinHum, _graphNavOffset < 0, _sysConfigPtr );
      vm.ocupado = aguardandoDados( MODE_GRAPH_VIEW, ua::cola::assinaturaDoGrafico( _graphData ) );
      uaUsar<VmGrafico, desenharGrafico, alvosGrafico>( q, vm );
      return true;
    }

    case MODE_GRAPH_DETAIL: {
      __dmb( );
      VmDetalhe &vm = uaVm<VmDetalhe>( );
      ua::cola::preencherDetalhe( vm, _graphData, _currentMinHum, _currentMaxHum, _graphNavOffset < 0, _detailPage );
      uaUsar<VmDetalhe, desenharDetalhe, alvosDetalhe>( q, vm );
      return true;
    }

    case MODE_CALENDAR: {
      VmCalendario &vm = uaVm<VmCalendario>( );
      ua::cola::preencherCalendario( vm, _calYear, _calMonth, _calDaysMask, time( nullptr ) );
      vm.ocupado = aguardandoDados( MODE_CALENDAR, ua::cola::assinaturaDoCalendario( _calYear, _calMonth, _calDaysMask ) );
      uaUsar<VmCalendario, desenharCalendario, alvosCalendario>( q, vm );
      return true;
    }

    default:
      return false;
  }
}

/* The touch zones of these modes, for the filters (acceptTouch( ) once a
 * press, acceptHoldTouch( ) repeating every 300 ms, acceptSlideTouch( ) again
 * on a new zone). The old branches used 0, 10..13 and 20, 21 (the months). */
namespace {
constexpr uint8_t ZONA_VOLTAR = 0xA0, ZONA_DIA = 0xA1, ZONA_PERIODO = 0xA2, ZONA_ANTERIOR = 0xA3, ZONA_SEGUINTE = 0xA4, ZONA_AREA = 0xA5,
                  ZONA_PAGINA = 0xA6, ZONA_MES_ANTERIOR = 0xA7, ZONA_MES_SEGUINTE = 0xA8, ZONA_DIA_DO_MES = 0xA9, ZONA_HOJE = 0xAA;
}

UaToque DisplayManager::uaToqueGraficos( const ua::Alvo *a, int16_t &x, int16_t &y ) {
  using namespace ua::tela;
  (void)x;
  (void)y;
  switch ( _uiMode ) {
    /* Controller: each target does what the old branch did for the same
     * request (DisplayManager_Touch.cpp:591-658), and a touch outside any
     * target is dead, as the gaps between the old buttons were. */
    case MODE_GRAPH_VIEW: {
      if ( !a ) return UaToque::TRATADO;
      switch ( a->tipo ) {
        case GRF_VOLTAR:
          /* The old close (:593): the offset goes back to 0 and the panel returns. */
          if ( !acceptTouch( ZONA_VOLTAR ) ) break;
          actGraphClose( );
          break;
        case GRF_ANTERIOR:
          /* Past, repeating while held (:613-619). */
          if ( !acceptHoldTouch( ZONA_ANTERIOR ) ) break;
          pedirDados( MODE_GRAPH_VIEW, ua::cola::assinaturaDoGrafico( _graphData ) );
          actGraphNav( _graphData, -1 );
          break;
        case GRF_SEGUINTE:
          /* Future, only in the past, repeating while held (:620-626). */
          if ( _graphNavOffset >= 0 || !acceptHoldTouch( ZONA_SEGUINTE ) ) break;
          pedirDados( MODE_GRAPH_VIEW, ua::cola::assinaturaDoGrafico( _graphData ) );
          actGraphNav( _graphData, +1 );
          break;
        case GRF_DIA:
          /* The calendar (:627-633). */
          if ( !acceptTouch( ZONA_DIA ) ) break;
          pedirDados( MODE_GRAPH_VIEW, ua::cola::assinaturaDoGrafico( _graphData ) );
          actGraphOpenCalendar( _graphData );
          break;
        case GRF_PERIODO:
          /* Another period, the way the zoom buttons asked for one (:634-649).
           * The chip that is already lit is inert, as a segmented control's
           * is: re-reading the same window would show nothing new and leave
           * the busy line up until it times out. */
          if ( a->indice == _graphData.timeRange || !acceptTouch( ZONA_PERIODO ) ) break;
          pedirDados( MODE_GRAPH_VIEW, ua::cola::assinaturaDoGrafico( _graphData ) );
          actGraphPeriod( _graphData, a->indice );
          break;
        case GRF_AREA:
          /* The central zone opens the detail on the temperature (:651-657). */
          if ( !acceptTouch( ZONA_AREA ) ) break;
          actGraphDetailOpen( );
          break;
        default:
          break;
      }
      return UaToque::TRATADO;
    }

    /* The detail: the pager changes the quantity, "Voltar" goes back to the
     * chart. The old screen had no pager: a tap on its middle cycled the pages
     * and the last one went back to the chart (DisplayManager_Touch.cpp:718-736);
     * its close went to the panel (:661), which "Voltar" no longer does. */
    case MODE_GRAPH_DETAIL: {
      if ( !a ) return UaToque::TRATADO;
      switch ( a->tipo ) {
        case DET_VOLTAR:
          /* What the old tap on the last page did (:730-735). */
          if ( !acceptTouch( ZONA_VOLTAR ) ) break;
          actDetailBack( );
          break;
        case DET_PAG_ANTERIOR:
        case DET_PAG_PROXIMA: {
          if ( !acceptTouch( ZONA_PAGINA ) ) break;
          uint8_t paginas[3];
          const int n = ua::cola::paginasDoDetalhe( _graphData, _currentMinHum, paginas );
          int k = 0;
          for ( int j = 0; j < n; j++ )
            if ( paginas[j] == _detailPage ) k = j;
          k += a->tipo == DET_PAG_PROXIMA ? 1 : -1;
          if ( k < 0 || k >= n ) break;
          actDetailPage( paginas[k] );   /* with the repaint request the old cycling raised (:725, :729) */
          break;
        }
        default:
          break;
      }
      return UaToque::TRATADO;
    }

    /* The calendar (DisplayManager_Touch.cpp:739-808). */
    case MODE_CALENDAR: {
      if ( !a ) return UaToque::TRATADO;
      switch ( a->tipo ) {
        case CAL_VOLTAR:
          /* Back to the chart with the data it already has (:741-746). */
          if ( !acceptTouch( ZONA_VOLTAR ) ) break;
          actCalendarBack( );
          break;
        case CAL_MES_ANTERIOR:
          /* The month before (:748-752, :789-793). */
          if ( !acceptSlideTouch( ZONA_MES_ANTERIOR ) ) break;
          pedirDados( MODE_CALENDAR, ua::cola::assinaturaDoCalendario( _calYear, _calMonth, _calDaysMask ) );
          actCalendarMonth( _graphData, -1 );
          break;
        case CAL_MES_SEGUINTE:
          /* The month after, which the screen offers only before the current one (:753-758, :801-805). */
          if ( !acceptSlideTouch( ZONA_MES_SEGUINTE ) ) break;
          pedirDados( MODE_CALENDAR, ua::cola::assinaturaDoCalendario( _calYear, _calMonth, _calDaysMask ) );
          actCalendarMonth( _graphData, +1 );
          break;
        case CAL_DIA:
          /* A day that has history (:759-785): Core 0 answers with the loading
           * screen, which is the feedback, so no busy line. */
          if ( !calDayHasHistory( _calDaysMask, a->indice ) || !acceptTouch( ZONA_DIA_DO_MES ) ) break;
          actCalendarDay( _graphData, a->indice );
          break;
        case CAL_HOJE:
          /* Today: the chart at the present, on the period it had (:794-800). The
           * calendar is not a graph mode, so Core 0 answers with the loading
           * screen here too (AppManager_Events.cpp:163-175): no busy line. */
          if ( !acceptTouch( ZONA_HOJE ) ) break;
          actCalendarToday( _graphData );
          break;
        default:
          break;
      }
      return UaToque::TRATADO;
    }

    /* The update screen lets the touch through to the old chain, where the core's action (actOtaDismiss( ), which holds the phase test and the
     * gate) dismisses a refused or cut update, and nothing else while one runs. It is not called from here: TRATADO would mark the frame
     * dirty on every touch sample, and the pass would build the screen's view model and hash it on each one while Core 1 runs the update
     * screen (the READY phase and the two endings; in RECEIVING Core 1 is parked and takes no sample). S7_PLANO.md R3 says to keep this
     * case TRADUZIDO until S10a, which retires the old chain, and the action is already the code that chain runs. */
    case MODE_OTA_UPDATE:
      return UaToque::TRADUZIDO;
    /* Nothing to touch while the history is read: no branch of handleTouch
     * answers MODE_GRAPH_LOADING either. */
    case MODE_GRAPH_LOADING:
      return UaToque::TRATADO;
    default:
      return UaToque::NAO_E_MEU;
  }
}

