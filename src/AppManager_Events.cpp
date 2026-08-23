/**
 * @file AppManager_Events.cpp
 * @brief UI event dispatch: touch, graph, calendar, settings, alarms.
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "SoundManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include "TelemetryManager.h" /* pushAlarm + AlarmErrCode (AlarmQueue.h) */
#include "Themes.h"
#include <time.h>


void AppManager::pushAlarmAction(int8_t slot, uint8_t errCodeErr, uint8_t errCodeLim) {
	if (slot < 0 || slot >= MAX_SENSORS) return;
	SystemConfig &cfg = _storageMgr->getConfig( );
	if (!cfg.sensors[slot].active) return;
	/* canal = primeiro que o tipo reporta (mesma convenção da borda de erro) */
	uint8_t firstCh = CH_TEMP;
	for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
		if (sensorHasChannel((SensorType)cfg.sensors[slot].sensorType, c)) { firstCh = c; break; }
	}
	/* erro de sensor tem prioridade sobre limite (mesma regra do display) */
	const bool errState = _displayMgr->isSlotErrAlarming(slot);
	_telemetryMgr->pushAlarm((uint8_t)slot, firstCh, NAN,
	                         errState ? errCodeErr : errCodeLim);
}


void AppManager::core0Yield( ) {
 static bool _isRenderingGraph = false;
 static bool _inYield = false;
 static uint32_t _yieldEntryTime = 0;

 /* Safety: reset guard if stuck >10s (partial crash) */
 if (_inYield && timeSince(_yieldEntryTime, 10000)) {
 _inYield = false;
 _isRenderingGraph = false;
 LOG_CODE(LOG_WARN, "APP", APP_YIELD_STUCK, 0, TRL("Yield stuck >10s, force reset."));
 }

 if (_inYield) return;
 _inYield = true;
 _yieldEntryTime = millis( );

 /*
 * Context-aware WdtWindow: core0Yield can be called from within
 * web handlers (via _lightYieldCb). It processes UI events that may
 * trigger graph preloads (5x renderGraphOptimized, each 6s budget).
 * Without a window here, cumulative fits only in 15s default —
 * insufficient for bursts. 30s covers a typical session.
 */
 LogManager::WdtWindow _wdtYield(30000);

 /*
 * Highest priority: process touch sound BEFORE any other
 * processing. Reduces beep latency from ~50ms to ~5ms.
 */
 if (_displayMgr->consumeTouchSound( )) {
 _soundMgr->play(SND_TOUCH_CLICK);
 _soundMgr->update( ); /* Execute immediately, without waiting for yield end */
 }
 if (_displayMgr->consumeErrorSound( )) {
 _soundMgr->play(SND_ERROR);
 _soundMgr->update( );
 }

 UiEvent uiEv;
 if (!_isRenderingGraph) {
 /* Graph navigation is COALESCED across the drain.
  *
  * This loop drains the whole 16-slot ring in one pass, and each EVT_GRAPH_NAV
  * used to run a full renderGraphOptimized with a 6 s budget. Tapping the
  * arrows faster than a render completes therefore queued renders that ran
  * back to back: two of them already exceed the watchdog's real 8388 ms
  * ceiling (see WATCHDOG_TIMEOUT_MS), and on a 1-week range each step is a
  * whole week, so a few taps land before the start of history where nothing
  * matches and every render burns its full budget.
  *
  * The steps are relative (_graphAnchorEnd += param * step), so summing them
  * and rendering once lands on the same place. The intermediate positions were
  * never visible anyway. One deliberate difference: forward-then-back now
  * cancels out, where stepping one at a time could drift because each step
  * clamped against "now" separately.
  *
  * Non-nav events flush the pending navigation first, so ordering is kept. */
 int32_t navAccum = 0;
 int navId = -1;
 bool navPending = false;
 auto flushGraphNav = [&]( ) {
 if (!navPending) return;
 navPending = false;
 const int32_t steps = navAccum;
 navAccum = 0;
 if (steps == 0) return;   /* forward-then-back cancelled out: nothing to redraw */

 static const time_t rangeDur[] = { 3600, 21600, 43200, 86400, 604800 };
 time_t step = (_lastGraphRange >= 0 && _lastGraphRange <= 4)
 ? rangeDur[_lastGraphRange] : 86400;

 /* No anchor (graph opened without the calendar): anchor at now. */
 if (_graphAnchorEnd == 0) _graphAnchorEnd = time(nullptr);

 _graphAnchorEnd += (time_t)steps * step;

 /* Don't allow viewing the future. */
 time_t nowNav = time(nullptr);
 if (_graphAnchorEnd > nowNav) _graphAnchorEnd = nowNav;

 /* Offset derived from position: negative = past (▶ enabled) */
 _graphNavOffset = (_graphAnchorEnd < nowNav) ? -1 : 0;
 _displayMgr->setGraphNavOffset(_graphNavOffset);

 _isRenderingGraph = true;
#if SIMUT_DISPLAY_TFT
 renderGraphOptimized(navId, _lastGraphRange, true, 0, _graphAnchorEnd);
#endif // SIMUT_DISPLAY_TFT
 _isRenderingGraph = false;
 };

 while (_displayMgr->getUiEvent(uiEv)) {
 if (uiEv.type == UiEvent::EVT_GRAPH_NAV) {
 navAccum += (int32_t)uiEv.param;
 navId = uiEv.id;
 navPending = true;
 continue;
 }
 flushGraphNav( );
 if (uiEv.type == UiEvent::EVT_SLOT_SELECT) { _currentSensorIdx = uiEv.id; _lastSlotChangeTime = millis( ); refreshSelectedSlot( ); }
 else if (uiEv.type == UiEvent::EVT_OPEN_GRAPH) {
 if (uiEv.param == 99) openStatsScreen(uiEv.id);
 else {
 int sensorId = uiEv.id;
 int range = uiEv.param;
 bool hasAnchor = (_graphAnchorEnd != 0);

 if (!hasAnchor) {
 _graphNavOffset = 0;
 _displayMgr->setGraphNavOffset(0);
 }
 _lastGraphRange = range;

 /* Always render directly from flash.
 *
 * Feedback rule: ENTERING the graph from another screen shows the
 * loading screen for every range — it paints in ~45 ms now, so even a
 * 1H read presents as a clean transition instead of a frozen screen.
 * Zoom/nav INSIDE the graph deliberately does not blank: the old plot
 * stays for context and the touch handler already painted the busy
 * hint the moment the tap landed. */
 {
 UiMode m = _displayMgr->getUiMode( );
 bool onGraph = (m == MODE_GRAPH_VIEW || m == MODE_GRAPH_DETAIL ||
 m == MODE_GRAPH_LOADING);
 if (!onGraph) {
 _displayMgr->requestLoadingScreen( );
 uint32_t waitStart = millis( );
 while (!_displayMgr->isLoadingDrawn( ) && (millis( ) - waitStart < 500)) {
 feedWdt( );
 delay(5);
 }
 }
 }

 _isRenderingGraph = true;
#if SIMUT_DISPLAY_TFT
 renderGraphOptimized(sensorId, range, true, 0,
 hasAnchor ? _graphAnchorEnd : 0);
 _isRenderingGraph = false;
#endif // SIMUT_DISPLAY_TFT
 }
 }
 /* ── Calendar open ── */
 else if (uiEv.type == UiEvent::EVT_OPEN_CALENDAR) {
 time_t now = _netMgr->getEpoch( );
 struct tm nowTm;
 localtime_r(&now, &nowTm);
 int year = nowTm.tm_year + 1900;
 int month = nowTm.tm_mon + 1;

 uint32_t mask = _storageMgr->getHistoryDaysMask(year, month);
 _displayMgr->showCalendar(year, month, mask);
 }
 /* ── Day selection in calendar ── */
 else if (uiEv.type == UiEvent::EVT_CALENDAR_DAY) {
 int sensorId = uiEv.id;
 int dayNum = uiEv.param;

 /* Midnight of the selected day */
 struct tm selTm = {};
 selTm.tm_year = _displayMgr->getCalYear( ) - 1900;
 selTm.tm_mon = _displayMgr->getCalMonth( ) - 1;
 selTm.tm_mday = dayNum;
 time_t selMidnight = mktime(&selTm);

 /*
 * Anchor = midnight of the following day.
 * 24H window will be exactly [00:00, 23:59] of the selected day.
 * ◀▶ navigation shifts from this anchor, not from now.
 */
 _graphAnchorEnd = selMidnight + 86400;

 /*
 * Offset for ▶ button control:
 * negative = we are in the past (▶ enabled),
 * zero = present (▶ disabled).
 */
 time_t now = time(nullptr);
 _graphNavOffset = (_graphAnchorEnd < now) ? -1 : 0;
 _displayMgr->setGraphNavOffset(_graphNavOffset);

 /* Direct render, no cache */
 _lastGraphRange = RANGE_24H;

 /* Same feedback rule as EVT_OPEN_GRAPH: leaving the calendar for the
 * graph is a screen change — show the (now fast) loading screen
 * instead of a calendar frozen for up to a second. */
 _displayMgr->requestLoadingScreen( );
 {
 uint32_t waitStart = millis( );
 while (!_displayMgr->isLoadingDrawn( ) && (millis( ) - waitStart < 500)) {
 feedWdt( );
 delay(5);
 }
 }

 _isRenderingGraph = true;
#if SIMUT_DISPLAY_TFT
 renderGraphOptimized(sensorId, RANGE_24H, true, 0, _graphAnchorEnd);
 _isRenderingGraph = false;
#endif // SIMUT_DISPLAY_TFT
 }
 /* ── Month change in calendar ── */
 else if (uiEv.type == UiEvent::EVT_CALENDAR_MONTH) {
 int newMonth = _displayMgr->getCalMonth( ) + uiEv.param;
 int newYear = _displayMgr->getCalYear( );

 if (newMonth < 1) { newMonth = 12; newYear--; }
 if (newMonth > 12) { newMonth = 1; newYear++; }

 uint32_t mask = _storageMgr->getHistoryDaysMask(newYear, newMonth);
 _displayMgr->showCalendar(newYear, newMonth, mask);
 }
 else if (uiEv.type == UiEvent::EVT_OPEN_SETTINGS) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 String authPin = String(cfg.displayPin);
 if (authPin.length( ) == 0) authPin = "1234";
 _displayMgr->showAuthScreen(authPin);
 }
 else if (uiEv.type == UiEvent::EVT_AUTH_SUCCESS) {
 _soundMgr->play(SND_CONFIRM);


 if (_pendingAlarmDeactivate) {
 _pendingAlarmDeactivate = false;

 SystemConfig &cfg = _storageMgr->getConfig( );
 /* Desativação POR SLOT E POR DOMÍNIO (RAM only): o domínio ATIVO do slot
 * da tela de ação é que é desligado — se o sensor está em ERRO, muta só o
 * erro (o LIMITE permanece armado e dispara se o valor sair da faixa após
 * o sensor se reestabelecer); se está em LIMITE, desliga só o limite (o
 * erro continua reportando). Um domínio é independente do outro. */
 if (_alarmDeactivateSlot >= 0 && _alarmDeactivateSlot < MAX_SENSORS) {
 const bool errNow = _displayMgr->isSlotErrAlarming(_alarmDeactivateSlot);
 if (errNow) {
 _displayMgr->setAlarmErrMuted(_alarmDeactivateSlot, true);
 LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_DEACTIVATED, _alarmDeactivateSlot,
 "erro mutado (limite permanece)");
 } else {
 cfg.sensors[_alarmDeactivateSlot].alarmsActive = false;
 LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_DEACTIVATED, _alarmDeactivateSlot,
 "limite desligado (erro permanece)");
 }
 }

 _soundMgr->stopAlarm( );
 _displayMgr->setAlarmState(0, -1);
 /* limpa também o mask de ERRO: sem isso o âmbar ficava preso — o stopAlarm
 * derruba isAlarming() e o else do checkAlarmConditions (que limparia o
 * display) só roda com o som ativo ou silenciado. */
 _displayMgr->setAlarmErrState(0);
 _displayMgr->setAlarmSilenced(false, 0);
 /* 2ª linha de telemetria: registra a AÇÃO de desativar — o {err} do
 * registro carrega "err_off" (erro) ou "off" (limite). */
 pushAlarmAction(_alarmDeactivateSlot, ALARM_ERR_ERR_OFF, ALARM_ERR_ALARM_OFF);
 _alarmDeactivateSlot = -1;
 _displayMgr->forceDashboard( );
 LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_DEACTIVATED, 0, "");
 } else if (_storageMgr->mustChangePin( )) {
 /* PIN is still the default factory "1234";
 * force the change screen before allowing the main menu. */
 _displayMgr->showSettingsPassword( );
 LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, 0,
 TRL("Default PIN detected; forcing change."));
 } else {
 _displayMgr->showSettingsMain( );
 }
 }
 else if (uiEv.type == UiEvent::EVT_MENU_SELECT) {
 if (uiEv.id == 0) {
 _displayMgr->showSettingsThemes(_storageMgr->getConfig( ).themeIndex);
 }
 else if (uiEv.id == 1) {
 _displayMgr->showSettingsAlarms(&_storageMgr->getConfig( ));
 }
 else if (uiEv.id == 2) {

 _displayMgr->showSettingsSounds(_soundMgr->getSettingsState( ));
 }
 else if (uiEv.id == 3) {
 _displayMgr->showSettingsLang(_storageMgr->getConfig( ).displayLang);
 }
 else if (uiEv.id == 4) {
 _displayMgr->showSettingsPassword( );
 }
 else if (uiEv.id == 5) {
 _displayMgr->showTouchCalibration( );
 }
 else if (uiEv.id == 6) {
 _displayMgr->showSettingsLicense( );
 }
 else if (uiEv.id == 7) {
 _displayMgr->showSystemStatus( );
 }
 else if (uiEv.id == 8) {
 _displayMgr->showSettingsDisplayOffset( );
 }
 }
 else if (uiEv.type == UiEvent::EVT_APPLY_THEME) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 cfg.themeIndex = uiEv.id;
 loadTheme(cfg.themeIndex);
 _storageMgr->saveConfiguration( );
 _displayMgr->refreshTheme( );
 _soundMgr->play(SND_CONFIRM);
 LOG_CODE(LOG_INFO, "APP", APP_UI_THEME_CHANGED, 0, "");
 }
 else if (uiEv.type == UiEvent::EVT_APPLY_LANG) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 cfg.displayLang = uiEv.id;
 _displayMgr->setLanguage(cfg.displayLang);
 _storageMgr->saveConfiguration( );
 _soundMgr->play(SND_CONFIRM);
 _displayMgr->forceDashboard( );
 LOG_CODE(LOG_INFO, "APP", APP_UI_LANG_CHANGED, 0, "");
 }
 else if (uiEv.type == UiEvent::EVT_SAVE_ALARMS) {
 _storageMgr->saveConfiguration( );

 _sensorMgr->syncAlarmLimits(_storageMgr->getConfig( ));

 checkAlarmConditions( );
 _soundMgr->play(SND_CONFIRM);
 /* param=1 means the ON/OFF flag was toggled from the list and the screen
  * is already the right one. Re-entering it would repaint all of it and
  * send the cursor back to the first sensor — the "unpleasant effect".
  * param=0 comes from the limit editor, which does need the list back. */
 if (uiEv.param == 1) _displayMgr->refreshAlarmStatus( );
 else _displayMgr->showSettingsAlarms(&_storageMgr->getConfig( ));
 LOG_CODE(LOG_INFO, "APP", APP_UI_ALARM_SAVED, 0, "");
 }

 else if (uiEv.type == UiEvent::EVT_APPLY_TOUCH_CAL) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
 _displayMgr->fillCalData(cal);
 _storageMgr->saveConfiguration( );
 _soundMgr->play(SND_CONFIRM);
 LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_CAL_SAVED, 0, "");
 }

 else if (uiEv.type == UiEvent::EVT_SAVE_TOUCH_CAL) {
 /* Save calibrated sensitivity threshold */
 SystemConfig &cfg = _storageMgr->getConfig( );
 TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
 _displayMgr->fillCalData(cal);
 _storageMgr->saveConfiguration( );
 _soundMgr->play(SND_CONFIRM);
 LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_SENS_SAVED, 0, "");
 }

 else if (uiEv.type == UiEvent::EVT_APPLY_DISPLAY_OFFSET) {
 /*
 * Applies the new offset to the TFT (already applied by the screen
 * in preview, but values come from the event to ensure consistency),
 * persists in reserved[] and restarts touch calibration: the
 * raw→pixel mapping directly depends on the image position on the LCD,
 * so keeping the old calibration after shifting makes no sense.
 */
 SystemConfig &cfg = _storageMgr->getConfig( );

 int8_t ox = (int8_t)uiEv.id;
 int8_t oy = (int8_t)uiEv.param;
 DisplayOffsetData* ofs = reinterpret_cast<DisplayOffsetData*>(
 cfg.reserved + sizeof(TouchCalData) + sizeof(SoundConfigData));
 ofs->magic = 0xD0;
 ofs->offsetX = ox;
 ofs->offsetY = oy;
 ofs->reserved = 0;

 /* Invalidates touch calibration: magic=0 forces recalibration; the rest
 * of the block can remain as garbage — magic is the sole validity
 * criterion in loadTouchCalibration( ). */
 TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
 cal->magic = 0;

 _storageMgr->saveConfiguration( );
 _displayMgr->resetTouchCalibration( );
 _soundMgr->play(SND_CONFIRM);
 LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_CAL_SAVED, 0,
 "Display offset applied; touch calibration reset.");

 /* Immediately open touch calibration for the user to remap
 * touches with the new LCD image position. */
 _displayMgr->showTouchCalibration( );
 }

 else if (uiEv.type == UiEvent::EVT_SAVE_PASSWORD) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 char newPwd[9];
 _displayMgr->getNewPassword(newPwd, sizeof(newPwd));
 if (strlen(newPwd) >= 4 && strlen(newPwd) <= 7) {
 /* Only clear mustChangePin if the user chose a PIN != default
 * "1234". If they set "1234" again, keep the flag active —
 * swapping to the same value doesn't solve anything.
 * Note: "1234" is still accepted as a value; the policy only
 * prevents it from counting as a "real change". */
 safeCopy(cfg.displayPin, newPwd, sizeof(cfg.displayPin));
 cfg.displayPin[7] = '\0';
 if (strcmp(newPwd, "1234") != 0) {
 _storageMgr->clearMustChangePin( );
 }
 _storageMgr->saveConfiguration( );
 _soundMgr->play(SND_CONFIRM);
 LOG_CODE(LOG_INFO, "APP", APP_UI_PIN_CHANGED, 0, "");
 } else {
 _soundMgr->play(SND_ERROR);
 }
 }

 else if (uiEv.type == UiEvent::EVT_SAVE_SOUNDS) {
 SoundSettingsState sndState = _displayMgr->getSoundSettings( );
 _soundMgr->applySettingsState(sndState);

 SystemConfig &cfg = _storageMgr->getConfig( );
 SoundConfigData* sndCfg = reinterpret_cast<SoundConfigData*>(
 cfg.reserved + sizeof(TouchCalData));
 _soundMgr->fillConfig(sndCfg);
 _storageMgr->saveConfiguration( );

 _soundMgr->play(SND_CONFIRM);
 _displayMgr->showSettingsMain( );
 LOG_CODE(LOG_INFO, "APP", APP_UI_SOUND_SAVED, 0, "");
 }


 else if (uiEv.type == UiEvent::EVT_ALARM_SILENCE) {
 uint32_t silenceSec = (uiEv.param > 0) ? uiEv.param : 120;
 _soundMgr->stopAlarm( );
 _displayMgr->setAlarmSilenced(true, millis( ) + (silenceSec * 1000));
 /* 2ª linha de telemetria: registra a AÇÃO de silenciar — o {err} do
 * registro carrega "err_sil" (erro de sensor) ou "sil" (limite). */
 pushAlarmAction((int8_t)uiEv.id, ALARM_ERR_ERR_SIL, ALARM_ERR_ALARM_SIL);
 _displayMgr->forceDashboard( );
 LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_SILENCED, 120, "");
 }


 else if (uiEv.type == UiEvent::EVT_ALARM_DEACTIVATE) {

 _pendingAlarmDeactivate = true;
 _alarmDeactivateSlot = (int8_t)uiEv.id;
 SystemConfig &cfg = _storageMgr->getConfig( );
 _displayMgr->showAuthScreen(String(cfg.displayPin));
 }
 }

 /* Last run of arrow taps has no non-nav event after it to flush it. */
 flushGraphNav( );
 }


 {
 uint8_t volPreview;
 if (_displayMgr->consumeVolumePreview(volPreview)) {
 _soundMgr->setVolume(volPreview);
 _soundMgr->play(SND_TOUCH_CLICK);
 _displayMgr->consumeTouchSound( );
 }
 }


 {
 uint8_t alarmVolPreview;
 if (_displayMgr->consumeAlarmVolumePreview(alarmVolPreview)) {
 _soundMgr->setAlarmVolume(alarmVolPreview);

 SoundSettingsState sndState = _displayMgr->getSoundSettings( );
 _soundMgr->playPreview(SND_ALARM_START, sndState.alarmMelody);
 _displayMgr->consumeTouchSound( );
 }
 }


 if (_displayMgr->consumeTouchSound( )) {
 _soundMgr->play(SND_TOUCH_CLICK);
 }


 if (_displayMgr->consumeErrorSound( )) {
 _soundMgr->play(SND_ERROR);
 }


 {
 SoundEvent prevEvt;
 uint8_t prevIdx;
 if (_displayMgr->consumePreviewSound(prevEvt, prevIdx)) {
 _soundMgr->playPreview(prevEvt, prevIdx);
 }
 }

 _soundMgr->update( );

 _sensorMgr->update( );
 updateLiveDisplay( );


 if (_bootCompletedAt > 0 && timeSince(_bootCompletedAt, 5000)) {


 if (_displayMgr->isAlarmSilenced( )) {
 uint32_t silEnd = _displayMgr->getAlarmSilenceEnd( );
 if (silEnd > 0 && millis( ) >= silEnd) {
 _displayMgr->setAlarmSilenced(false, 0);
 LOG_CODE(LOG_INFO, "APP", APP_UI_ALARM_SILENCE_EXP, 0, "");
 }
 }

 checkAlarmConditions( );
 }


 _soundMgr->update( );

 _inYield = false;
}
