/**
 * @file DisplayManager_UiA_Menus.cpp
 * @brief The Ângulo build's glue for the "menus" screens (tools/tft_ui/ESPEC_UI_A.md
 *        §6.2): the view models from the DisplayManager's state, and what a
 *        touch on each of their targets does. pico_wiretft_angulo only.
 *
 * Every mode here is a controller (ESPEC §7): a touch does what the old
 * screen's branch did for the same request (the same show*( ) calls, the same
 * UiEvents, the same state), and each case names the old branch it stands
 * for. The old branches themselves no longer run for these modes: the rows
 * and the pager of the new screens are not the old ones' zones. Where the old
 * branch's body is a core action (DisplayManager_Actions.h: the settings
 * list's Open button, the alarms list's row, the licence's page step), the
 * case calls that action behind its own gate, so the two drivers run the same
 * code; the lists' page arithmetic is display/ListPager.h's. The themes, the
 * language and the sensor's alarm menu keep copies of their logic until S8a
 * and S9 (docs/analysis/UI_DRIVERS.md, section 2.9).
 *
 * The page of a list is the old screen's own variable (_mainMenuPage,
 * _alarmPage, _statusPage, _licensePage): the old show*( ) functions reset it
 * when the mode is entered, so no entry has to be noticed here.
 */

#include "../../DisplayManager.h"


#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../HelpLicenseEN.h"             /* LICENSE_OPENING_EN, LICENSE_TEXT_EN: what the licence screen carries */
#include "../../sensors/SensorChannels.h"   /* CH_TEMP: the channel of the limits the list shows */
#include "../../simut_config.h"             /* SIMUT_NET_LAN, tested below: an undefined macro would read as 0 and show the radio's rows */
#include "Angulo.h"
#include "UiA.h"
#include "telas/MenusAlarmes.h"
#include "telas/MenusConfig.h"
#include "telas/MenusLicenca.h"
#include "telas/MenusStatus.h"
#include "telas/Telas.h"
#include "../../display/ListPager.h"   /* listPages( ), listClampPage( ), listPageStep( ): the lists' page arithmetic */
#include "../../DisplayManager_Actions.h"   /* the core's touch actions, which the lists' rows run; last */

/* The screens' ids are the firmware's, value for value: the menu copies _menuItems[ ] as
 * it is, and a language row is the language it applies. (Two enums are compared as ints:
 * -Wenum-compare is an error here.) */
constexpr bool mesmo( int a, int b ) { return a == b; }
static_assert( mesmo( ua::tela::IM_TEMA, MENU_THEMES ) && mesmo( ua::tela::IM_LIMITES, MENU_ALARMS ) &&
                   mesmo( ua::tela::IM_SONS, MENU_SOUNDS ) && mesmo( ua::tela::IM_IDIOMA, MENU_LANG ) &&
                   mesmo( ua::tela::IM_PIN_PROPRIO, MENU_OWN_PIN ) && mesmo( ua::tela::IM_CALIBRAR, MENU_TOUCH_CAL ) &&
                   mesmo( ua::tela::IM_LICENCA, MENU_LICENSE ) && mesmo( ua::tela::IM_STATUS, MENU_STATUS ) &&
                   mesmo( ua::tela::IM_ALINHAR, MENU_DISPLAY_OFFSET ) && mesmo( ua::tela::IM_USUARIOS, MENU_USERS ) &&
                   mesmo( ua::tela::IM_SEGURANCA_PIN, MENU_PIN_POLICY ) && mesmo( ua::tela::IM_MODO_CONFIG, MENU_SETUP_AP ) &&
                   mesmo( ua::tela::IM_DATA_HORA, MENU_CLOCK ) && mesmo( ua::tela::IM_N, MENU_ITEM_COUNT ),
               "ItemMenu must stay SettingsMenuItem (display/SettingsMenu.h)" );
static_assert( mesmo( ua::tela::ID_INGLES, LANG_EN ) && mesmo( ua::tela::ID_PACOTE, LANG_PT ),
               "EscolhaIdioma must stay the language ids" );

/* The licence's opening in the pack's language is composed on Core 0 into a file-static buffer of
 * DisplayManager.cpp (_licOpening): webDictValue( ) reads LittleFS, which only Core 0 may
 * ("Core 0 only", DisplayManager_LangParser.cpp), so Core 1 cannot compose it here.
 * DisplayManager::licenseOpening( ) offers that buffer; where it is empty (no pack, or English) the screen opens
 * with the English text of the page, which is what the old screen shows without a pack. */

namespace {

/* The licence's lines, indexed once (and again when the language changes, which changes the opening):
 * about 125 lines of 4 bytes, the pages are copied out of the sources. */
ua::tela::IndiceLicenca s_indiceLic;
int s_idiomaLic = -2;

/* The touch zones of this group, for acceptTouch( ) and acceptHoldTouch( ) to tell presses
 * apart. The old screens use 0..13, 20, 21, 0xC1, 0xE0 and 0xF0, and the dashboard's top
 * panel reads zone 0: 0xA0.. are ours. */
constexpr uint8_t ZONA_VOLTAR = 0xA0, ZONA_LINHA = 0xA1, ZONA_ANTERIOR = 0xA2, ZONA_PROXIMA = 0xA3;

/* The loaded pack's name as the language list reads it: the two packs the project ships by
 * their own names, any other as "name (code)" (the pack's @NAME has no accents). */
void nomeDoPacote( const char *nome, const char *codigo, char *out, int cap ) {
  static const struct { const char *codigo, *nome; } kConhecidos[] = {
    { "pt-BR", "Português (Brasil)" }, { "es-ES", "Español (España)" } };
  for ( const auto &c : kConhecidos ) {
    if ( !strcmp( codigo, c.codigo ) ) { ua::copiar( out, c.nome, cap ); return; }
  }
  snprintf( out, (size_t)cap, "%s (%s)", nome[0] ? nome : "?", codigo );
}

/* Minutes left in a slot's maintenance window, 0 when it is closed: maintMinutesLeft( ) of
 * maintMinutesLeft( ) in DisplayManager_Users.cpp (a static there), which uses the wall clock the way Core 0 does. */
uint32_t minutosDeManutencao( const SystemConfig *cfg, int slot ) {
  if ( !cfg || slot < 0 || slot >= MAX_SENSORS ) return 0;
  const uint32_t agora = (uint32_t)time( nullptr );
  const uint32_t ate = cfg->maint.until[slot];
  if ( ate <= agora ) return 0;
  return ( ate - agora + 59u ) / 60u;
}

/* A limit in tenths of a degree, for a view model of integers. The record is a blob, so a
 * NaN reads as 0 and the range is held to an int16. */
int16_t decimos( float v ) {
  if ( !( v == v ) ) return 0;
  const float d = roundf( v * 10.0f );
  return d > 32767.0f ? (int16_t)32767 : d < -32768.0f ? (int16_t)-32768 : (int16_t)d;
}

}  // namespace

/* ------------------------------------------------------------------ */
/* The frame of each mode                                              */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontarMenus( UaQuadro &q ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_SETTINGS_MAIN: {
      /* drawSettingsMain( ) (DisplayManager_Settings.cpp): the list is _menuItems[0.._menuCount)
       * as showSettingsMain( ) built it for this session's bits, and the title carried the account. */
      VmMenu &vm = uaVm<VmMenu>( );
      const int n = _menuCount < MENU_ITENS_MAX ? _menuCount : MENU_ITENS_MAX;
      vm.n = (uint8_t)n;
      memcpy( vm.itens, _menuItems, (size_t)n );
      _mainMenuPage = listClampPage( _mainMenuPage, listPages( n, LISTA_FILAS ) );   // as the old draw held it
      vm.pagina = (uint8_t)_mainMenuPage;
      ua::copiar( vm.conta, panelUserName( ), sizeof vm.conta );
      uaUsar<VmMenu, desenharMenu, alvosMenu>( q, vm );
      return true;
    }
    case MODE_SETTINGS_THEMES: {
      /* cfg.themeIndex is the choice among three: 0 light, 1 dark, 2 automatic (uaTema( ) reads it the same way). */
      VmTemas &vm = uaVm<VmTemas>( );
      const int8_t t = _sysConfigPtr ? _sysConfigPtr->themeIndex : 0;
      vm.atual = t == 1 ? TM_ESCURO : t == 2 ? TM_AUTOMATICO : TM_CLARO;
      uaUsar<VmTemas, desenharTemas, alvosTemas>( q, vm );
      return true;
    }
    case MODE_SETTINGS_LANG: {
      /* drawSettingsLang( ) (DisplayManager_i18n.cpp): English, and the pack when one is loaded. */
      VmIdioma &vm = uaVm<VmIdioma>( );
      if ( _activeLangLoaded ) {
        nomeDoPacote( _activeLang.name, _activeLang.code, vm.pacote, sizeof vm.pacote );
        vm.atual = _currentLangIdx == LANG_EN ? ID_INGLES : ID_PACOTE;
      } else {
        vm.atual = ID_INGLES;
      }
      uaUsar<VmIdioma, desenharIdioma, alvosIdioma>( q, vm );
      return true;
    }
    case MODE_SETTINGS_ALARMS: {
      /* renderAlarmRow( ) (DisplayManager_Settings.cpp): one row per active sensor, in the order
       * showSettingsAlarms( ) listed them in _activeSensorsMap; the new rows also say the temperature
       * band and an open maintenance window. */
      VmAlarmes &vm = uaVm<VmAlarmes>( );
      int n = _sysConfigPtr ? _activeSensorCount : 0;
      if ( n > ALARMES_MAX ) n = ALARMES_MAX;
      vm.n = (uint8_t)n;
      for ( int k = 0; k < n; k++ ) {
        const int slot = _activeSensorsMap[k];
        if ( slot < 0 || slot >= MAX_SENSORS ) continue;
        const SensorRecord &rec = _sysConfigPtr->sensors[slot];
        LinhaAlarme &l = vm.sensores[k];
        ua::copiar( l.nome, rec.friendlyName, sizeof l.nome );
        l.lo = decimos( rec.chMin[CH_TEMP] );
        l.hi = decimos( rec.chMax[CH_TEMP] );
        l.ligado = rec.alarmsActive;
        l.manutencao = minutosDeManutencao( _sysConfigPtr, slot ) != 0;
      }
      /* rows of two lines: three to a page */
      _alarmPage = listClampPage( _alarmPage, listPages( n, LISTA_FILAS_2 ) );   // as the old draw held it
      vm.pagina = (uint8_t)_alarmPage;
      uaUsar<VmAlarmes, desenharAlarmes, alvosAlarmes>( q, vm );
      return true;
    }
    case MODE_SETTINGS_ALARM_SENSOR: {
      /* drawAlarmSensorMenu( ) (DisplayManager_Users.cpp): the sensor's name, the switch (alarmsActive),
       * the maintenance window and, row by row, whether this session holds the bit. The window is given
       * in whole minutes (as seconds): a count of seconds would repaint the frame every second. */
      VmSensorAlarme &vm = uaVm<VmSensorAlarme>( );
      const int slot = _sensorMenuIdx;
      if ( _sysConfigPtr && slot >= 0 && slot < MAX_SENSORS ) {
        const SensorRecord &rec = _sysConfigPtr->sensors[slot];
        ua::copiar( vm.nome, rec.friendlyName, sizeof vm.nome );
        vm.alarmesLigados = rec.alarmsActive;
        vm.manutencaoSeg = minutosDeManutencao( _sysConfigPtr, slot ) * 60u;
      }
      vm.podeLimites = ( _panelPerms & PERM_ALARM_LIMITS ) != 0;
      vm.podeBloquear = ( _panelPerms & PERM_ALARM_BLOCK ) != 0;
      vm.podeManutencao = ( _panelPerms & PERM_MAINT ) != 0;
      uaUsar<VmSensorAlarme, desenharSensorAlarme, alvosSensorAlarme>( q, vm );
      return true;
    }
    case MODE_SETTINGS_STATUS: {
      /* drawSystemStatus( ) (DisplayManager_Settings.cpp): the rows come from _statusData, the copy
       * Core 0 pushes once a second without a lock, so a copy of our own keeps a frame from mixing two
       * seconds. Only the shown page's fields are filled: the view model is hashed byte for byte, and the
       * uptime ticking must not repaint what is not on it. */
      static_assert( STATUS_PAGES == STATUS_PAGINAS, "the screen has the pages the old one had" );
      VmStatusSistema &vm = uaVm<VmStatusSistema>( );
      const SystemStatusData d = _statusData;
      if ( _statusPage >= STATUS_PAGES ) _statusPage = STATUS_PAGES - 1;
      if ( _statusPage < 0 ) _statusPage = 0;
      vm.pagina = (uint8_t)_statusPage;
      switch ( _statusPage ) {
        case PS_SISTEMA:
          ua::copiar( vm.aparelho, d.deviceName, sizeof vm.aparelho );
          ua::copiar( vm.firmware, d.fwVersion, sizeof vm.firmware );
          vm.ligadoSeg = d.uptimeSec;
          vm.memLivre = d.heapFree;
          vm.flashUsada = d.flashUsed;
          vm.flashTotal = d.flashTotal;
          vm.placaDecimos = decimos( d.boardTemp );
          vm.fuso = d.timezone;
          break;
        case PS_REDE:
#if SIMUT_NET_LAN
          vm.cabo = true;   /* the wired build: a cable's link, no network name, no signal (drawSystemStatus( )) */
#endif
          vm.conectado = d.wifiConnected;
          vm.ntpSincronizado = d.ntpSynced;
          vm.rssi = (int16_t)( d.rssi < -32768 ? -32768 : d.rssi > 32767 ? 32767 : d.rssi );
          ua::copiar( vm.ssid, d.ssid, sizeof vm.ssid );
          ua::copiar( vm.ip, d.ip, sizeof vm.ip );
          ua::copiar( vm.mac, d.mac, sizeof vm.mac );
          ua::copiar( vm.ntpServidor, d.ntpServer, sizeof vm.ntpServidor );
          break;
        case PS_SENSORES: {
          /* The old page said only how many were active (drawSystemStatus( )). The count is taken from the
           * config here, with the rows, so the two agree. Core 1 knows which slots are alarming from two masks
           * (an alarm firing, a sensor failing) but reads only the selected and the pinned sensor's value,
           * so each sensor is told in the alarm system's words: ESS_ERRO, ESS_ALARME, ESS_MANUTENCAO,
           * ESS_DESLIGADO (alarms off) or ESS_NA_FAIXA (alarms on, none firing). */
          int ativos = 0, mostrados = 0;
          if ( _sysConfigPtr ) {
            for ( int slot = 0; slot < MAX_SENSORS; slot++ ) {
              const SensorRecord &rec = _sysConfigPtr->sensors[slot];
              if ( !rec.active ) continue;
              ativos++;
              if ( mostrados >= STATUS_SENSORES_MAX ) continue;
              SensorStatus &r = vm.sensores[mostrados++];
              ua::copiar( r.nome, rec.friendlyName, sizeof r.nome );
              const uint16_t bit = (uint16_t)( 1u << slot );
              r.estado = ( _alarmErrMask & bit )                             ? ESS_ERRO
                         : ( _alarmSlotMask & bit )                          ? ESS_ALARME
                         : minutosDeManutencao( _sysConfigPtr, slot ) != 0   ? ESS_MANUTENCAO
                         : !rec.alarmsActive                                 ? ESS_DESLIGADO
                                                                             : ESS_NA_FAIXA;
            }
          } else {
            ativos = d.activeSensors;
          }
          vm.ativos = (uint8_t)ativos;
          vm.slots = MAX_SENSORS;
          vm.nSensores = (uint8_t)mostrados;
          break;
        }
        default:
          vm.mqtt = d.telTransport == 1;   /* drawSystemStatus( ): 1 is MQTT, else HTTP */
          vm.mqttConectado = d.mqttConnected;
          vm.pendentes = d.telPending;
          vm.lote = d.telInterval;
          ua::copiar( vm.servidor, d.telServer, sizeof vm.servidor );
          break;
      }
      /* The old loop repainted this screen every second by _statusLastDraw, which its pass reads and drawSystemStatus( ) writes;
       * this driver keeps no such field, because it looks at the frame every 250 ms. */
      uaUsar<VmStatusSistema, desenharStatusSistema, alvosStatusSistema>( q, vm );
      return true;
    }
    case MODE_SETTINGS_LICENSE: {
      /* drawSettingsLicense( ) (DisplayManager.cpp): the opening, then the MIT text and the third-party
       * list, as one text. Wrapped once into an index; the page's lines are copied out of it. */
      VmLicenca &vm = uaVm<VmLicenca>( );
      if ( s_indiceLic.n == 0 || s_idiomaLic != _currentLangIdx ) {
        ua::tela::FonteLicenca fontes[ua::tela::LICENCA_FONTES_MAX];
        int nf = 0;
        const char *abertura = licenseOpening( );
        if ( abertura && abertura[0] ) {
          fontes[nf++] = ua::tela::FonteLicenca{ abertura, 0 };   /* composed: its own blank lines are in it */
        } else {
          /* What kLicOpeningSep[ ] (DisplayManager.cpp) puts after each string: "\n\n" is one blank line, "\n" none. */
          static const uint8_t kBrancas[5] = { 1, 0, 1, 1, 1 };
          for ( int k = 0; k < 5; k++ ) fontes[nf++] = ua::tela::FonteLicenca{ LICENSE_OPENING_EN[k], kBrancas[k] };
        }
        fontes[nf++] = ua::tela::FonteLicenca{ LICENSE_TEXT_EN, 0 };
        indexarLicenca( s_indiceLic, fontes, nf );
        s_idiomaLic = _currentLangIdx;
      }
      paginaDaLicenca( s_indiceLic, _licensePage, vm );
      _licensePage = vm.pagina;           /* the old draw held them (drawSettingsLicense( )) */
      _licenseTotalPages = vm.paginas;
      uaUsar<VmLicenca, desenharLicenca, alvosLicenca>( q, vm );
      return true;
    }
    default:
      return false;
  }
}

/* ------------------------------------------------------------------ */
/* The touch on each mode                                              */
/* ------------------------------------------------------------------ */

UaToque DisplayManager::uaToqueMenus( const ua::Alvo *a, int16_t &, int16_t & ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_SETTINGS_MAIN:
    case MODE_SETTINGS_THEMES:
    case MODE_SETTINGS_LANG:
    case MODE_SETTINGS_ALARMS:
    case MODE_SETTINGS_ALARM_SENSOR:
    case MODE_SETTINGS_STATUS:
    case MODE_SETTINGS_LICENSE:
      break;
    default:
      return UaToque::NAO_E_MEU;
  }
  if ( !a ) return UaToque::TRATADO;   /* outside every target: a dead zone */
  const bool voltar = a->tipo == LST_VOLTAR, linha = a->tipo == LST_LINHA;
  const int i = a->indice;
  /* A pager arrow: a page less or more, held at the ends (the old arrows wrapped around; the
   * new pager stops, MenusLista.h; listPageStep( ), display/ListPager.h, is the step), repeating
   * while the finger stays on it like the old ones. */
  auto virar = [&]( int &pagina, int paginas ) {
    if ( a->tipo == LST_PAG_ANTERIOR ) {
      if ( acceptHoldTouch( ZONA_ANTERIOR ) ) pagina = listPageStep( pagina, paginas, -1 );
    } else if ( a->tipo == LST_PAG_PROXIMA ) {
      if ( acceptHoldTouch( ZONA_PROXIMA ) ) pagina = listPageStep( pagina, paginas, +1 );
    }
  };
  switch ( _uiMode ) {
    case MODE_SETTINGS_MAIN:
      if ( voltar ) {
        /* The original's Back (handleTouch( ), Touch.cpp): SAIR leaves the settings for the dashboard. */
        if ( acceptTouch( ZONA_VOLTAR ) ) forceDashboard( );
      } else if ( linha ) {
        /* ENTRAR, which the old rows kept apart from the tap that selected: a tap on a row is both, at once. actMenuSelect( ), the
         * core's, asks Core 0 to open the ITEM the row stands for (display/SettingsMenu.h), not the row; the row is tested ahead
         * of the gate so that a tap on no row clicks nothing. */
        if ( i >= 0 && i < _menuCount && acceptTouch( ZONA_LINHA ) ) actMenuSelect( i );
      } else {
        /* The original's arrows (handleTouch( ), Touch.cpp), without the wrap. */
        virar( _mainMenuPage, listPages( _menuCount, LISTA_FILAS ) );
      }
      break;
    case MODE_SETTINGS_THEMES:
      if ( voltar ) {
        /* The original's Back (handleTouch( ), Touch.cpp): SAIR is back to the menu, nothing applied. */
        if ( acceptTouch( ZONA_VOLTAR ) ) showSettingsMain( );
      } else if ( linha && i >= 0 && i < 3 && acceptTouch( ZONA_LINHA ) ) {
        /* The original's Apply (handleTouch( ), Touch.cpp): APLICAR sends EVT_APPLY_THEME with the previewed index, and Core 0 saves it
         * and takes the panel to the dashboard (uiThemeChanged( )). A tap on a row is that, at once. */
        UiEvent ev{ };
        ev.type = UiEvent::EVT_APPLY_THEME;
        ev.id = i;
        pushUiEvent( ev );
      }
      break;
    case MODE_SETTINGS_LANG:
      if ( voltar ) {
        /* The original's Back (handleTouch( ), Touch.cpp): SAIR is back to the menu. */
        if ( acceptTouch( ZONA_VOLTAR ) ) showSettingsMain( );
      } else if ( linha && i >= 0 && i < ( _activeLangLoaded ? LANG_COUNT : 1 ) && acceptTouch( ZONA_LINHA ) ) {
        /* The original's slots are English, plus the pack, and its Apply (handleTouch( ), Touch.cpp): APLICAR sends
         * EVT_APPLY_LANG with the row; Core 0 saves it and leaves for the dashboard. */
        UiEvent ev{ };
        ev.type = UiEvent::EVT_APPLY_LANG;
        ev.id = i;
        pushUiEvent( ev );
      }
      break;
    case MODE_SETTINGS_ALARMS:
      if ( voltar ) {
        /* The original's Back (handleTouch( ), Touch.cpp): SAIR is back to the menu. */
        if ( acceptTouch( ZONA_VOLTAR ) ) showSettingsMain( );
      } else if ( linha ) {
        /* The row opens the sensor's action menu (actAlarmListOpen( ), the core's: showAlarmSensorMenu( ) with the slot from
         * _activeSensorsMap); a tap on a row is the second tap of the old two, the one on the selected row (the first only selected). */
        if ( i >= 0 && i < _activeSensorCount && acceptTouch( ZONA_LINHA ) ) actAlarmListOpen( i );
      } else {
        /* The original's arrows (handleTouch( ), Touch.cpp), without the wrap. */
        virar( _alarmPage, listPages( _activeSensorCount, LISTA_FILAS_2 ) );
      }
      break;
    case MODE_SETTINGS_ALARM_SENSOR:
      if ( a->tipo == SA_VOLTAR ) {
        /* handleTouchPanelV24( ) (DisplayManager_Users.cpp), the sensor menu: SAIR is back to the list of alarms, which starts over. */
        if ( acceptTouch( ZONA_VOLTAR ) ) showSettingsAlarms( _sysConfigPtr );
      } else if ( a->tipo == SA_LIMITES || a->tipo == SA_ALARMES || a->tipo == SA_MANUTENCAO ) {
        if ( !acceptTouch( ZONA_LINHA ) ) break;
        /* handleTouchPanelV24( )'s activate( row ) lambda, the sensor menu: the bit is checked on the screen too (this screen leaves out
         * the rows the session cannot use, so it only guards a frame that was on the glass a moment ago). */
        static const uint16_t kPrecisa[3] = { PERM_ALARM_LIMITS, PERM_ALARM_BLOCK, PERM_MAINT };
        const int row = a->tipo - SA_LIMITES;
        const int slot = _sensorMenuIdx;
        if ( !( _panelPerms & kPrecisa[row] ) ) { _errorSoundPending = true; break; }
        if ( !_sysConfigPtr || slot < 0 || slot >= MAX_SENSORS ) break;
        if ( row == 0 ) {
          showAlarmEdit( slot );
        } else if ( row == 1 ) {
          /* The flip is Core 0's: it checks the bit again, saves, logs and puts the record on the alarm
           * line, and the screen follows the record. No confirmation, as before. */
          UiEvent ev{ };
          ev.type = UiEvent::EVT_ALARM_BLOCK;
          ev.id = slot;
          ev.param = _sysConfigPtr->sensors[slot].alarmsActive ? 0 : 1;
          pushUiEvent( ev );
        } else {
          showMaintEntry( slot );
        }
      }
      break;
    case MODE_SETTINGS_STATUS:
      if ( voltar ) {
        /* The original's Back (handleTouch( ), Touch.cpp): SAIR is back to the menu. */
        if ( acceptTouch( ZONA_VOLTAR ) ) showSettingsMain( );
      } else {
        /* The original's arrows (handleTouch( ), Touch.cpp), without the wrap; the new frame needs only the page. */
        virar( _statusPage, STATUS_PAGES );
      }
      break;
    case MODE_SETTINGS_LICENSE:
      if ( voltar ) {
        /* The original's Back (handleTouch( ), Touch.cpp): always back to the menu. */
        if ( acceptTouch( ZONA_VOLTAR ) ) showSettingsMain( );
      } else if ( a->tipo == LST_PAG_ANTERIOR ) {
        /* The arrows step the page with the core's action (actLicensePage( ), held at the ends as the old arrows were), repeating while
         * the finger stays on them. The old taps on the text, upper half for the page before and lower for the page after, have no
         * target on the new screen. */
        if ( acceptHoldTouch( ZONA_ANTERIOR ) ) actLicensePage( -1 );
      } else if ( a->tipo == LST_PAG_PROXIMA ) {
        if ( acceptHoldTouch( ZONA_PROXIMA ) ) actLicensePage( +1 );
      }
      break;
    default:
      break;
  }
  return UaToque::TRATADO;
}

