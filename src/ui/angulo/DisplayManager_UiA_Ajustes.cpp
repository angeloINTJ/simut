/**
 * @file DisplayManager_UiA_Ajustes.cpp
 * @brief The Ângulo build's glue for the "ajustes" screens (tools/tft_ui/ESPEC_UI_A.md §6.3, §7):
 *        view models from the DisplayManager's state, and their touches.
 *        pico_wiretft_angulo only.
 *
 * Touch, per mode (ESPEC_UI_A.md §2):
 *  - Controller (C): the clock, the display alignment, the limits, the
 *    maintenance window, the sounds with their melody and the PIN policy do
 *    what the old branch does, with the old state, events and filters. The
 *    clock, the alignment, the limits (their focus, Back and Save), the maintenance
 *    window, the PIN policy, the melody picker and the sounds list run the core's
 *    actions (DisplayManager_Actions.h), each named in its case, behind the zones
 *    the old branch used, so a held arrow repeats as it did; the step of a limit
 *    still reproduces its branch here and cites the old lines it copies.
 *  - Raw (B): the touch sensitivity and the calibration read the touch before
 *    any hook (DisplayManager_Touch.cpp, the two blocks that end in return),
 *    so a touch on them never gets here; only the frame is built.
 */

#include "../../DisplayManager.h"


#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../SensorManager.h"   /* the channel table and the limit helpers the old editor used */
#include "Angulo.h"
#include "UiA.h"
#include "telas/Ajustes.h"
#include "telas/Telas.h"
#include "../../display/SettingsSteps.h"   /* DISPLAY_OFFSET_LIMIT: the alignment's scale */
#include "../../DisplayManager_Actions.h"   /* the core's touch actions, which the clock, the alignment and the sounds run; last */

namespace {

/* Minutes left in a slot's maintenance window, 0 when closed, rounded up: the
 * same arithmetic as the old screen's static helper
 * (DisplayManager_Users.cpp:656-662), which a static function cannot lend. */
uint32_t minutosRestantes( const SystemConfig *cfg, int slot ) {
  if ( !cfg || slot < 0 || slot >= MAX_SENSORS ) return 0;
  const uint32_t now = (uint32_t)time( nullptr );
  const uint32_t until = cfg->maint.until[slot];
  if ( until <= now ) return 0;
  return ( until - now + 59u ) / 60u;
}

/* MIN >= MAX of a channel is not representable and would arm an alarm that can
 * never clear: the old draw pulled every channel apart before painting
 * (DisplayManager_Settings.cpp:238-250). That draw no longer runs here, so the
 * frame's builder does the same to the working copy. */
void separarLimites( SensorRecord &rec ) {
  const SensorType st = (SensorType)rec.sensorType;
  const uint8_t n = sensorLimitCount( st );
  for ( uint8_t i = 0; i < n; i += 2 ) {
    const uint8_t c = sensorLimitChannel( st, i );
    if ( !channelValid( c ) ) continue;
    if ( rec.chMin[c] < rec.chMax[c] ) continue;
    const ChannelInfo &ci = channelInfo( c );
    rec.chMax[c] = round( ( rec.chMin[c] + 0.1f ) * 10.0f ) / 10.0f;
    if ( rec.chMax[c] > ci.saneMax ) {
      rec.chMax[c] = ci.saneMax;
      rec.chMin[c] = ci.saneMax - 0.1f;
    }
  }
}

/* One step of limit `idx` (2n is the n-th channel's MIN, 2n + 1 its MAX), as
 * the old editor's adjustVal( ) / enforceInterlock( ) / stepLimit( ) lambdas
 * did it (DisplayManager_Touch.cpp:884-922): a tenth-rounded value held inside
 * the channel's sane range, then MIN and MAX pulled apart by moving the one the
 * user is NOT holding, so the digit under the finger keeps going the way it
 * was pressed. Read-modify-write: chMin/chMax live in a packed struct. */
void passoLimite( SensorRecord &rec, int idx, float step ) {
  const SensorType st = (SensorType)rec.sensorType;
  const uint8_t n = sensorLimitCount( st );
  const uint8_t c = sensorLimitChannel( st, (uint8_t)idx );
  if ( !channelValid( c ) ) return;
  const ChannelInfo &ci = channelInfo( c );
  const bool isMax = sensorLimitIsMax( (uint8_t)idx );
  float v = isMax ? rec.chMax[c] : rec.chMin[c];
  v += step;
  v = round( v * 10.0f ) / 10.0f;
  if ( v < ci.saneMin ) v = ci.saneMin;
  if ( v > ci.saneMax ) v = ci.saneMax;
  if ( isMax ) rec.chMax[c] = v; else rec.chMin[c] = v;
  /* enforceInterlock( idx ), over every channel as the old one did. */
  for ( uint8_t i = 0; i < n; i += 2 ) {
    const uint8_t ch = sensorLimitChannel( st, i );
    if ( !channelValid( ch ) ) continue;
    const ChannelInfo &info = channelInfo( ch );
    if ( rec.chMin[ch] >= rec.chMax[ch] ) {
      if ( idx == (int)i ) rec.chMax[ch] = round( ( rec.chMin[ch] + 0.1f ) * 10.0f ) / 10.0f;
      else rec.chMin[ch] = round( ( rec.chMax[ch] - 0.1f ) * 10.0f ) / 10.0f;
    }
    if ( rec.chMax[ch] > info.saneMax ) rec.chMax[ch] = info.saneMax;
    if ( rec.chMin[ch] < info.saneMin ) rec.chMin[ch] = info.saneMin;
  }
}

}  // namespace

/* ------------------------------------------------------------------ */
/* The frames                                                          */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontarAjustes( UaQuadro &q ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_SET_CLOCK: {
      /* The fields drawClockValues( ) prints (DisplayManager_Settings.cpp:889-918)
       * and the boot flag drawClockEntry( ) reads (Settings.cpp:934). */
      VmRelogio &vm = uaVm<VmRelogio>( );
      vm.boot = _clockAtBoot;
      vm.dia = (uint8_t)_clockEntry.day;
      vm.mes = (uint8_t)_clockEntry.month;
      vm.ano = (uint16_t)_clockEntry.year;
      vm.hora = (uint8_t)_clockEntry.hour;
      vm.minuto = (uint8_t)_clockEntry.minute;
      uaUsar<VmRelogio, desenharRelogio, alvosRelogio>( q, vm );
      return true;
    }
    case MODE_SETTINGS_DISPLAY_OFFSET: {
      /* The preview the arrows move (and setDisplayOffset( ) applies live). */
      VmAlinhar &vm = uaVm<VmAlinhar>( );
      vm.x = _offsetPreviewX;
      vm.y = _offsetPreviewY;
      vm.limite = DISPLAY_OFFSET_LIMIT;
      uaUsar<VmAlinhar, desenharAlinhar, alvosAlinhar>( q, vm );
      return true;
    }
    case MODE_SETTINGS_ALARM_EDIT: {
      /* The sensor's limits as the old editor held them (DisplayManager_Settings.cpp:
       * 225-337): a working copy, _tempAlarmConfig, and _editFieldFocus, an index
       * into the limit list (2n MIN and 2n + 1 MAX of the n-th channel the sensor
       * reports). The new screen pages by channel and focuses a row of it, so
       * both come out of that one index: it needs no state of its own, and it is
       * zeroed by showAlarmEdit( ) on every entry. */
      const SensorType st = (SensorType)_tempAlarmConfig.sensorType;
      const int nLim = sensorLimitCount( st ) > 0 ? sensorLimitCount( st ) : 2;
      separarLimites( _tempAlarmConfig );
      if ( _editFieldFocus >= nLim ) _editFieldFocus = nLim - 1;
      if ( _editFieldFocus < 0 ) _editFieldFocus = 0;
      const int pagina = _editFieldFocus / 2;
      const uint8_t ch = sensorLimitCount( st ) > 0 ? sensorLimitChannel( st, (uint8_t)( pagina * 2 ) ) : (uint8_t)CH_TEMP;
      const ChannelInfo &ci = channelInfo( ch );
      /* Read by value: the record is packed. */
      const float lo = channelValid( ch ) ? _tempAlarmConfig.chMin[ch] : 0.0f;
      const float hi = channelValid( ch ) ? _tempAlarmConfig.chMax[ch] : 0.0f;
      VmLimites &vm = uaVm<VmLimites>( );
      if ( _tempAlarmConfig.friendlyName[0] ) ua::copiar( vm.sensor, _tempAlarmConfig.friendlyName, sizeof vm.sensor );
      else snprintf( vm.sensor, sizeof vm.sensor, "Sensor %d", _editSensorIdx );
      vm.grandeza = ch == CH_HUM ? GLIM_UMIDADE : ch == CH_PRESS ? GLIM_PRESSAO : GLIM_TEMPERATURA;
      vm.pagina = (uint8_t)pagina;
      vm.total = (uint8_t)( nLim / 2 );
      vm.foco = (uint8_t)( _editFieldFocus % 2 );
      vm.minimo = lroundf( lo * 10.0f );
      vm.maximo = lroundf( hi * 10.0f );
      vm.piso = lroundf( ci.saneMin * 10.0f );
      vm.teto = lroundf( ci.saneMax * 10.0f );
      uaUsar<VmLimites, desenharLimites, alvosLimites>( q, vm );
      return true;
    }
    case MODE_SETTINGS_MAINT: {
      /* drawMaintEntry( ) (DisplayManager_Users.cpp:756-830) says what is open from the
       * clock at the moment it draws, not from a label painted on the way in
       * (the stale-label trap of pin_usuarios_alarmes.md §19.2): so does this. */
      const SensorRecord *rec = ( _sysConfigPtr && _maintSlot >= 0 && _maintSlot < MAX_SENSORS ) ? &_sysConfigPtr->sensors[_maintSlot] : nullptr;
      const uint32_t left = minutosRestantes( _sysConfigPtr, _maintSlot );
      VmManutencao &vm = uaVm<VmManutencao>( );
      if ( rec ) ua::copiar( vm.sensor, rec->friendlyName, sizeof vm.sensor );
      vm.aberta = left != 0;
      vm.permitido = ( _panelPerms & PERM_MAINT ) != 0;
      vm.foco = _maintFocus ? 1 : 0;
      vm.minutos = (uint8_t)_maintMinutes;
      vm.horas = (uint16_t)_maintHours;
      vm.horasMax = (uint16_t)( MAINT_MAX_SEC / 3600u );
      vm.restanteMin = left;
      uaUsar<VmManutencao, desenharManutencao, alvosManutencao>( q, vm );
      return true;
    }
    case MODE_SETTINGS_SOUNDS: {
      /* The melody's choice is the same mode with _inMelodySelect set
       * (DisplayManager.cpp:1659-1665); here it is a screen of its own. */
      if ( _inMelodySelect ) {
        VmMelodia &vm = uaVm<VmMelodia>( );
        vm.som = _melSelectType < SOM_COM_MELODIA ? _melSelectType : 0;
        vm.escolhida = _melSelectIdx < MELODIAS_POR_SOM ? _melSelectIdx : 0;
        uaUsar<VmMelodia, desenharMelodia, alvosMelodia>( q, vm );
        return true;
      }
      /* The copy the list edits (_soundSettings). The page, and which volume was
       * touched last, are the screen's own: they live in _soundSelection, which the
       * old list used for the selected row and nothing else reads now, packed as
       * page + 3 * volume, so showSettingsSounds( ) zeroes them on every entry and
       * the return from the mute confirmation keeps them. */
      VmSons &vm = uaVm<VmSons>( );
      vm.pagina = (uint8_t)( _soundSelection % SONS_PAGINAS );
      vm.foco = _soundSelection / SONS_PAGINAS ? 1 : 0;
      vm.ligado[SOM_TOQUE] = _soundSettings.touchEnabled;
      vm.ligado[SOM_CONFIRMACAO] = _soundSettings.confirmEnabled;
      vm.ligado[SOM_ERRO] = _soundSettings.errorEnabled;
      vm.ligado[SOM_ALARME] = _soundSettings.alarmEnabled;
      vm.ligado[SOM_ATENCAO] = _soundSettings.attentionEnabled;
      vm.ligado[SOM_WEB] = _soundSettings.webEnabled;
      vm.ligado[SOM_MUDO] = _soundSettings.muted;
      const uint8_t mel[SOM_COM_MELODIA] = { _soundSettings.touchMelody, _soundSettings.confirmMelody, _soundSettings.errorMelody,
                                             _soundSettings.alarmMelody, _soundSettings.attentionMelody };
      for ( int i = 0; i < SOM_COM_MELODIA; i++ ) vm.melodia[i] = mel[i] < MELODIAS_POR_SOM ? mel[i] : 0;
      vm.volumeSistema = _soundSettings.volume;
      vm.volumeAlarme = _soundSettings.alarmVolume;
      uaUsar<VmSons, desenharSons, alvosSons>( q, vm );
      return true;
    }
    case MODE_SETTINGS_PIN_POLICY: {
      /* The copy the editor works on, taken by showPinPolicy( )
       * (DisplayManager_Users.cpp:1235-1241): the length, the keypad and the
       * alphabet. The ceiling is the chosen keypad's (PinKb::maxLenFor( )), the
       * floor PinKb::PIN_LEN_MIN. */
      VmPoliticaPin &vm = uaVm<VmPoliticaPin>( );
      vm.minimo = _policyMin;
      vm.piso = PinKb::PIN_LEN_MIN;
      vm.teto = PinKb::maxLenFor( _policyKb );
      vm.teclado = _policyKb;
      vm.alfabeto = _policyAlpha;
      uaUsar<VmPoliticaPin, desenharPoliticaPin, alvosPoliticaPin>( q, vm );
      return true;
    }
    case MODE_SETTINGS_TOUCH_SENS: {
      /* What drawTouchSensitivity( ) shows (DisplayManager_Calibration.cpp:306-388):
       * the stability bar from 0.0..1.0, the threshold and the done flag; the
       * count is _sensCount, which counts readings 15 ms apart and not taps,
       * so the screen says "Leituras". */
      VmSensibilidade &vm = uaVm<VmSensibilidade>( );
      vm.leituras = _sensCount;
      const long pct = lroundf( _sensStability * 100.0f );
      vm.estabilidade = (uint8_t)( pct < 0 ? 0 : pct > 100 ? 100 : pct );
      vm.limiar = _sensThreshold;
      vm.concluido = _sensDone;
      uaUsar<VmSensibilidade, desenharSensibilidade, alvosSensibilidade>( q, vm );
      return true;
    }
    case MODE_SETTINGS_TOUCH_CAL: {
      /* drawTouchCalibration( ) (DisplayManager_Calibration.cpp:497-610): the
       * point is _calStep % 4 and the lap _calStep / 4 + 1; the sight is green
       * once the hold passed CAL_HOLD_MS (_calHoldReady). From _calPhase 1 on
       * it draws the verdict drawCalibrationMessage( ) draws (Calibration.cpp:
       * 440-488): success is _calPhase == 2, a refusal is 1. A step of 8 with
       * no verdict yet lasts one pass: the last point stays. */
      static_assert( MIRA_X[0] == CAL_SCR_X[0] && MIRA_X[1] == CAL_SCR_X[1] && MIRA_X[2] == CAL_SCR_X[2] &&
                     MIRA_X[3] == CAL_SCR_X[3] && MIRA_Y[0] == CAL_SCR_Y[0] && MIRA_Y[1] == CAL_SCR_Y[1] &&
                     MIRA_Y[2] == CAL_SCR_Y[2] && MIRA_Y[3] == CAL_SCR_Y[3],
                     "the Angulo sight (telas/AjustesTela.h MIRA_X/Y) must stand on the core's calibration targets (CAL_SCR_X/Y)" );
      VmCalibrar &vm = uaVm<VmCalibrar>( );
      vm.resultado = _calPhase >= 1 ? ( _calPhase == 2 ? CALRES_CONCLUIDA : CALRES_IMPRECISA ) : CALRES_MEDINDO;
      const int passo = _calStep < 0 ? 0 : _calStep > 7 ? 7 : _calStep;
      vm.passo = (uint8_t)( passo % 4 );
      vm.volta = (uint8_t)( passo / 4 + 1 );
      vm.segurando = _calHoldReady;
      uaUsar<VmCalibrar, desenharCalibrar, alvosCalibrar>( q, vm );
      return true;
    }
    default:
      return false;
  }
}

/* ------------------------------------------------------------------ */
/* The touches                                                         */
/* ------------------------------------------------------------------ */

UaToque DisplayManager::uaToqueAjustes( const ua::Alvo *a, int16_t &x, int16_t &y ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_SET_CLOCK: {
      /* Controller: what the old branch does (handleTouch( ), the clock's branch in DisplayManager_Touch.cpp), behind the zones it used, so that a held
       * arrow repeats as before: 0xD0 + 2 * column, plus 1 for a column's +1 button, for the arrows, 0xC4 for Pular and Voltar and 0xC5 for Salvar. The
       * actions are the core's. A touch farther than FOLGA_TOQUE from every target is dead, as the value band between the arrows was. */
      if ( !a ) return UaToque::TRATADO;
      switch ( a->tipo ) {
        case RELOG_MAIS:
        case RELOG_MENOS: {
          if ( a->indice < 0 || a->indice >= CE_FIELDS ) return UaToque::TRATADO;
          const int delta = a->tipo == RELOG_MAIS ? +1 : -1;
          if ( !acceptHoldTouch( (uint8_t)( 0xD0 + a->indice * 2 + ( delta > 0 ? 1 : 0 ) ) ) ) return UaToque::TRATADO;
          actClockStep( (uint8_t)a->indice, delta );
          return UaToque::TRATADO;
        }
        case RELOG_PULAR:
        case RELOG_VOLTAR:
          /* Skip at boot, back from the settings: the boot flag tells them apart, inside the action. */
          if ( !acceptTouch( 0xC4 ) ) return UaToque::TRATADO;
          actClockLeave( );
          return UaToque::TRATADO;
        case RELOG_SALVAR:
          /* SAVE hands the fields to Core 0, which checks them again. */
          if ( !acceptTouch( 0xC5 ) ) return UaToque::TRATADO;
          actClockSave( );
          return UaToque::TRATADO;
        default:
          return UaToque::TRATADO;
      }
    }
    case MODE_SETTINGS_DISPLAY_OFFSET: {
      /* Controller: what the old branch does (handleTouch( ), the alignment's branch in DisplayManager_Touch.cpp), in the zones it used, so that holding an
       * arrow repeats as before: the arrows 20 (up), 21 (down), 22 (left) and 23 (right), Redefinir 24, Voltar 25 and Salvar 26. An arrow's action says
       * whether the preview moved, and the tail then applies it to the panel at once. Voltar puts the saved offset back; Salvar only asks Core 0. */
      if ( !a ) return UaToque::TRATADO;
      bool changed = false;
      switch ( a->tipo ) {
        case ALINH_CIMA:
          if ( !acceptHoldTouch( 20 ) ) return UaToque::TRATADO;
          changed = actOffsetUp( );
          break;
        case ALINH_BAIXO:
          if ( !acceptHoldTouch( 21 ) ) return UaToque::TRATADO;
          changed = actOffsetDown( );
          break;
        case ALINH_ESQUERDA:
          if ( !acceptHoldTouch( 22 ) ) return UaToque::TRATADO;
          changed = actOffsetLeft( );
          break;
        case ALINH_DIREITA:
          if ( !acceptHoldTouch( 23 ) ) return UaToque::TRATADO;
          changed = actOffsetRight( );
          break;
        case ALINH_REDEFINIR:
          if ( !acceptTouch( 24 ) ) return UaToque::TRATADO;
          changed = actOffsetCentre( );
          break;
        case ALINH_VOLTAR:
          if ( !acceptTouch( 25 ) ) return UaToque::TRATADO;
          actOffsetBack( );
          return UaToque::TRATADO;
        case ALINH_SALVAR:
          if ( !acceptTouch( 26 ) ) return UaToque::TRATADO;
          actOffsetApply( );
          return UaToque::TRATADO;
        default:
          return UaToque::TRATADO;
      }
      if ( changed ) actOffsetPreview( );
      return UaToque::TRATADO;
    }
    case MODE_SETTINGS_ALARM_EDIT: {
      /* Controller: what the old branch does (DisplayManager_Touch.cpp:872-988). */
      if ( !a ) return UaToque::TRATADO;
      const int nLim = sensorLimitCount( (SensorType)_tempAlarmConfig.sensorType ) > 0
                           ? sensorLimitCount( (SensorType)_tempAlarmConfig.sensorType ) : 2;
      switch ( a->tipo ) {
        case LIMITES_MENOS:
        case LIMITES_MAIS: {
          /* The arrows on a bar: -/+ with hold-repeat in a zone of their own per
           * (row, direction), so sliding off one ends the repeat; the focus goes to
           * the row, and the step grows with how long the same arrow has been held:
           * 0.1, then 0.5 after 2 s, 1.0 after 4 s and 10 after 6 s (Touch.cpp:
           * 931-946). */
          const int row = a->indice;
          if ( row < 0 || row > 1 ) return UaToque::TRATADO;
          const int idx = ( _editFieldFocus / 2 ) * 2 + row;
          if ( idx >= nLim ) return UaToque::TRATADO;
          const bool inc = a->tipo == LIMITES_MAIS;
          const uint8_t zone = (uint8_t)( 30 + row * 2 + ( inc ? 1 : 0 ) );
          if ( !acceptHoldTouch( zone ) ) return UaToque::TRATADO;
          if ( _lastPressedBtn != (int)zone ) { _btnHoldStartTime = millis( ); _lastPressedBtn = (int)zone; }
          const uint32_t holdTime = millis( ) - _btnHoldStartTime;
          float mag = 0.1f;
          if ( holdTime > 6000 ) mag = 10.0f; else if ( holdTime > 4000 ) mag = 1.0f; else if ( holdTime > 2000 ) mag = 0.5f;
          _editFieldFocus = idx;
          passoLimite( _tempAlarmConfig, idx, inc ? mag : -mag );
          return UaToque::TRATADO;
        }
        case LIMITES_PAG_ANTERIOR:
        case LIMITES_PAG_PROXIMA: {
          /* New on the screen (the old one paged by moving the selection): the
           * channel before or after, on the same row. The ends do not wrap. */
          const int nova = _editFieldFocus / 2 + ( a->tipo == LIMITES_PAG_PROXIMA ? 1 : -1 );
          if ( nova < 0 || nova * 2 >= nLim ) return UaToque::TRATADO;
          if ( !acceptTouch( 0x60 ) ) return UaToque::TRATADO;
          actLimitFocus( nova * 2 + _editFieldFocus % 2 );
          return UaToque::TRATADO;
        }
        case LIMITES_VOLTAR:
        case LIMITES_CANCELAR:
          /* BACK: the core's actLimitsBack( ): the copy is discarded and the sensor's menu comes back. */
          if ( !acceptTouch( 12 ) ) return UaToque::TRATADO;
          actLimitsBack( );
          return UaToque::TRATADO;
        case LIMITES_SALVAR:
          /* SAVE: the core's actLimitsSave( ): the working copy stays here for Core 0 to diff against the config (editedAlarmRecord( )); it decides,
           * saves, logs and brings the sensor's menu back. */
          if ( !acceptTouch( 13 ) ) return UaToque::TRATADO;
          actLimitsSave( );
          return UaToque::TRATADO;
        default:
          return UaToque::TRATADO;
      }
    }
    case MODE_SETTINGS_MAINT: {
      /* Controller: what the old branch does (DisplayManager_Users.cpp:1466-1507). */
      if ( !a ) return UaToque::TRATADO;
      const bool aberta = minutosRestantes( _sysConfigPtr, _maintSlot ) != 0;
      switch ( a->tipo ) {
        case MANUT_VOLTAR:
        case MANUT_CANCELAR:
          /* BACK (Users.cpp:1469). */
          if ( !acceptTouch( 12 ) ) return UaToque::TRATADO;
          showAlarmSensorMenu( _maintSlot );
          return UaToque::TRATADO;
        case MANUT_INICIAR:
        case MANUT_ENCERRAR: {
          /* The primary button (the core's actMaintPrimary( ): the permission, the zero test and the event). It acts by what is open NOW: a frame that
           * went stale under the finger (the window ran out, or the web opened one) is not obeyed, the next frame shows the truth. */
          if ( !acceptTouch( 13 ) ) return UaToque::TRATADO;
          if ( ( a->tipo == MANUT_ENCERRAR ) != aberta ) return UaToque::TRATADO;
          actMaintPrimary( aberta );
          return UaToque::TRATADO;
        }
        case MANUT_MENOS:
        case MANUT_MAIS: {
          /* The ends of a bar, hold-repeat in a zone per (row, direction), no acceleration: the core's actMaintFocus( ) and actMaintStep( ) (hours
           * 0..720 without wrapping, minutes in fives, wrapping 55 <-> 0). Only while the window is closed. */
          const int row = a->indice;
          if ( aberta || row < 0 || row > 1 ) return UaToque::TRATADO;
          const int delta = a->tipo == MANUT_MAIS ? +1 : -1;
          if ( !acceptHoldTouch( (uint8_t)( 30 + row * 2 + ( delta > 0 ) ) ) ) return UaToque::TRATADO;
          actMaintFocus( row );
          actMaintStep( row, delta );
          return UaToque::TRATADO;
        }
        default:
          return UaToque::TRATADO;
      }
    }
    case MODE_SETTINGS_SOUNDS: {
      if ( !a ) return UaToque::TRATADO;
      if ( _inMelodySelect ) {
        /* Controller, the melody's choice: what the melody screen's branch of handleTouch( ) does (DisplayManager_Touch.cpp), through the core's actions. */
        switch ( a->tipo ) {
          case MELOD_ITEM: {
            /* A row chooses it and plays the preview; sliding to another one does the same (actMelodyPick( ), the core's, which the original's arrows
             * run too). */
            const int j = a->indice;
            if ( j < 0 || j >= MELODIAS_POR_SOM ) return UaToque::TRATADO;
            if ( !acceptSlideTouch( (uint8_t)( 0x80 + j ) ) ) return UaToque::TRATADO;
            actMelodyPick( (uint8_t)j );
            return UaToque::TRATADO;
          }
          case MELOD_VOLTAR:
          case MELOD_CANCELAR:
            /* Back to the list, nothing taken (actMelodyBack( ), the core's). */
            if ( !acceptTouch( 0x92 ) ) return UaToque::TRATADO;
            actMelodyBack( );
            return UaToque::TRATADO;
          case MELOD_USAR:
            /* The selector's own SAVE (actMelodyUse( ), the core's): it turns the sound on with the chosen melody, in the copy only (the list's SAVE
             * writes it), turns the global mute off, as the web does, and plays the confirmation sound. */
            if ( !acceptTouch( 0x93 ) ) return UaToque::TRATADO;
            actMelodyUse( );
            return UaToque::TRATADO;
          default:
            return UaToque::TRATADO;
        }
      }
      /* Controller, the list: what the sounds screen's branch of handleTouch( ) does (DisplayManager_Touch.cpp), through the core's actions. */
      const int pagina = _soundSelection % SONS_PAGINAS;
      switch ( a->tipo ) {
        case SONS_INTERRUPTOR: {
          /* A switch flips in the copy, and turning a sound or the web access on turns the global mute off, as the web does: the core's actSoundToggle( ),
           * which the original runs for its web row. Deliberate difference: the old row opened the melody's choice to turn a sound on; here the switch only
           * flips and the row's name opens the choice. Silenciar tudo is the core's actMuteToggle( ): turned on it asks first, turned off it is direct. */
          const int som = a->indice;
          if ( som < 0 || som >= SOM_N ) return UaToque::TRATADO;
          if ( !acceptTouch( 0x61 ) ) return UaToque::TRATADO;
          switch ( som ) {
            case SOM_TOQUE: actSoundToggle( _soundSettings.touchEnabled ); break;
            case SOM_CONFIRMACAO: actSoundToggle( _soundSettings.confirmEnabled ); break;
            case SOM_ERRO: actSoundToggle( _soundSettings.errorEnabled ); break;
            case SOM_ALARME: actSoundToggle( _soundSettings.alarmEnabled ); break;
            case SOM_ATENCAO: actSoundToggle( _soundSettings.attentionEnabled ); break;
            case SOM_WEB: actSoundToggle( _soundSettings.webEnabled ); break;
            case SOM_MUDO: actMuteToggle( ); break;
            default: break;
          }
          return UaToque::TRATADO;
        }
        case SONS_LINHA: {
          /* The name and chevron of a sound that is on open the choice of its melody, from the one it has (actMelodyOpen( ), the core's). */
          const int som = a->indice;
          if ( som < 0 || som >= SOM_COM_MELODIA ) return UaToque::TRATADO;
          if ( !acceptTouch( 0x62 ) ) return UaToque::TRATADO;
          const uint8_t mel[SOM_COM_MELODIA] = { _soundSettings.touchMelody, _soundSettings.confirmMelody, _soundSettings.errorMelody,
                                                 _soundSettings.alarmMelody, _soundSettings.attentionMelody };
          actMelodyOpen( (uint8_t)som, mel[som] );
          return UaToque::TRATADO;
        }
        case SONS_MENOS:
        case SONS_MAIS: {
          /* A volume, 10 % a press, repeating while held, in the old zones 20 (system) and 21 (alarm); the core's actions step it, cancel the touch beep and
           * play the preview at the new level, as the original's volume rows do. The volume touched last is this screen's own page and focus. */
          const int i = a->indice;
          if ( i < 0 || i > 1 ) return UaToque::TRATADO;
          if ( !acceptHoldTouch( (uint8_t)( 20 + i ) ) ) return UaToque::TRATADO;
          const bool down = a->tipo == SONS_MENOS;
          if ( i == 0 ) actSoundVolume( down ); else actAlarmVolume( down );
          _soundSelection = ( SONS_PAGINAS - 1 ) + SONS_PAGINAS * i;
          return UaToque::TRATADO;
        }
        case SONS_PAG_ANTERIOR:
        case SONS_PAG_PROXIMA: {
          /* New on the screen: the old one paged with its selection. The ends do
           * not wrap. */
          const int nova = pagina + ( a->tipo == SONS_PAG_PROXIMA ? 1 : -1 );
          if ( nova < 0 || nova >= SONS_PAGINAS ) return UaToque::TRATADO;
          if ( !acceptTouch( 0x63 ) ) return UaToque::TRATADO;
          _soundSelection = nova;
          return UaToque::TRATADO;
        }
        case SONS_VOLTAR:
          /* BACK: the copy is dropped (the volume previews already played stay, as they did). */
          if ( !acceptTouch( 12 ) ) return UaToque::TRATADO;
          showSettingsMain( );
          return UaToque::TRATADO;
        case SONS_SALVAR:
          /* SAVE: Core 0 applies the copy and writes it (actSaveSounds( ), the core's). */
          if ( !acceptTouch( 13 ) ) return UaToque::TRATADO;
          actSaveSounds( );
          return UaToque::TRATADO;
        default:
          return UaToque::TRATADO;
      }
    }
    case MODE_SETTINGS_PIN_POLICY: {
      /* Controller: what the old branch does (DisplayManager_Users.cpp:1562-1624).
       * The rows edit a COPY; nothing reaches Core 0 until SAVE. */
      if ( !a ) return UaToque::TRATADO;
      uint8_t minLen = _policyMin, keypad = _policyKb, alphabet = _policyAlpha;
      switch ( a->tipo ) {
        case POLPIN_MENOS:
        case POLPIN_MAIS: {
          /* The length stops at the floor and at the CEILING OF THE CURRENT KEYPAD
           * (the old row cycled and wrapped to the floor; here the buttons stop,
           * and are drawn disabled there). Held, it repeats like the other steppers. */
          const bool mais = a->tipo == POLPIN_MAIS;
          if ( !acceptHoldTouch( mais ? 0x65 : 0x64 ) ) return UaToque::TRATADO;
          if ( mais ) { if ( minLen < PinKb::maxLenFor( keypad ) ) minLen++; }
          else { if ( minLen > PinKb::PIN_LEN_MIN ) minLen--; }
          break;
        }
        case POLPIN_TECLADO: {
          /* A segment sets the keypad outright (the old row stepped to the next
           * one that has a layout for the alphabet). */
          const int kb = a->indice;
          if ( !acceptTouch( 0x66 ) ) return UaToque::TRATADO;
          if ( kb < PinKb::KB_MIN || kb > PinKb::KB_MAX || !PinKb::comboSupported( alphabet, (uint8_t)kb ) ) return UaToque::TRATADO;
          keypad = (uint8_t)kb;
          break;
        }
        case POLPIN_CARACTERES: {
          const int al = a->indice;
          if ( !acceptTouch( 0x67 ) ) return UaToque::TRATADO;
          if ( al < 0 || al >= PinKb::ALPHA_COUNT ) return UaToque::TRATADO;
          alphabet = (uint8_t)al;
          /* Every pair has a layout since KB_PLAIN became ordered, so this does not
           * step today; it stays so that a pair without one moves the keypad
           * instead of refusing the alphabet just asked for (Users.cpp:1590).
           * Bounded, which the old loop was not. */
          for ( int guarda = 0; !PinKb::comboSupported( alphabet, keypad ) && guarda < PinKb::KB_MAX; guarda++ )
            keypad = keypad >= PinKb::KB_MAX ? (uint8_t)PinKb::KB_MIN : (uint8_t)( keypad + 1 );
          break;
        }
        case POLPIN_VOLTAR:
        case POLPIN_CANCELAR:
          /* Nothing to put back: the rows edited a copy (Users.cpp:1612-1615). */
          if ( !acceptTouch( 12 ) ) return UaToque::TRATADO;
          showSettingsMain( );
          return UaToque::TRATADO;
        case POLPIN_SALVAR:
          /* SAVE: the core's actPolicySave( ): the policy goes to Core 0, which marks every account whose PIN no longer complies. Implemented; never
           * pressed on the bench account. */
          if ( !acceptTouch( 13 ) ) return UaToque::TRATADO;
          actPolicySave( );
          return UaToque::TRATADO;
        default:
          return UaToque::TRATADO;
      }
      /* Changing the keypad or the alphabet re-clamps the length, so the pair on screen is always one the panel can run: the core's actPolicySet( ). */
      actPolicySet( minLen, keypad, alphabet );
      return UaToque::TRATADO;
    }
    case MODE_SETTINGS_TOUCH_SENS:
    case MODE_SETTINGS_TOUCH_CAL:
      /* Raw: handleTouch( ) took the touch above this hook and returned (the
       * blocks at DisplayManager_Touch.cpp:159-278 and 297-351). Nothing to
       * move, nothing to do here. */
      return UaToque::TRADUZIDO;
    default:
      return UaToque::NAO_E_MEU;
  }
}

