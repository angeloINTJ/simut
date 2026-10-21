/**
 * @file DisplayManager_Touch.cpp
 * @brief Touch handling: handleTouch (giant switch on _uiMode) + accept helpers.
 * @details Sub-file of DisplayManager.cpp.
 * handleTouch is called from loopCore1 every frame on Core 1.
 * Dispatches gestures by screen (dashboard / graph / settings /
 * auth / calibration / alarm action). acceptTouch/Hold/Slide
 * are debounce + repeat-on-hold gates used by handleTouch.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "simut_config.h"   /* SIMUT_NET_WIFI is tested below: included here, not left to another header */
#include "SensorManager.h"
#include "DisplayManager_Fonts.h"
#include "DisplayManager_FmtFloat.h"
#include "LogManager.h"
#include "PasswordKeyboard.h"
#include "sensors/SensorDrawing.h" /* min/max graph button geometry */
#include "ui/UiDriverHooks.h"   /* uiChanged( ), which the dashboard's touches raise: the hook definitions need the complete class, so after every other include */
#include "display/GraphNav.h"        /* calDayHasHistory( ): the calendar's day test */
#include "display/SettingsSteps.h"     /* langSlotCount( ): the language list's rows */
#include "DisplayManager_Actions.h"   /* the core's touch actions; last, they lean on the hooks above */

void DisplayManager::handleTouch( ) {
 /* Use _rawTouchState (already OR'd with sim flag in
 * loopCore1) so simulated touches register as "touched". When sim is
 * active, _driver.ts->getPoint() may return zeros, but mapTouchPoint() now
 * bypasses the ADC mapping and returns _simTouchX/Y directly. */
 if (!_rawTouchState) {
 /* Finger released — enables next single touch */
 _touchReleased = true;
 _pressActive = false;

 /* Short tap on top panel (release before 1s): toggle min/max.
  * Not during the tail of a long press — see DASH_HOLD_LOCK_MS. And only if
  * the dashboard is still up: a screen Core 0 put up under the finger (the
  * firmware update's, 2026-10-02) would get the panel painted over it. */
 if (_topPanel.holdStart != 0 && !_topPanel.holdFired && _lastTouchRegion == 0 &&
 _uiMode == MODE_DASHBOARD && timeReached(_topPanel.lockUntil)) {
 /* A short tap is min/max. The WHOLE panel, and from either mode.
  *
  * It used to depend on which mode the panel was in: interactive, and the
  * tap pinned it instead. Two gestures meaning two different things from
  * the same finger movement is what kept producing "I tapped and it went
  * into selection" — the user is not tracking which of the two states the
  * panel is in before deciding what a tap will do. Pinning is the long
  * press now, and only the long press (2026-09-18, at the maintainer's
  * request). The bottom panel has always worked this way. */
 _topPanel.showMinMax = !_topPanel.showMinMax;
 redrawTopPanel( );
 }
 
 _topPanel.holdStart = 0; _topPanel.holdSeen = 0; _topPanel.holdFired = false;
 

 /*
 * Release detection during hold-and-release calibration.
 * If the user held the point for the minimum time, records the
 * average of accumulated samples on release.
 */
 if (_uiMode == MODE_SETTINGS_TOUCH_CAL && _calHolding) {
 if (_calHoldReady && _calHoldSamples > 0 && _calStep < 8) {
 /* Record point: average of samples accumulated during hold */
 _calRawX[_calStep] = (int16_t)(_calHoldSumX / _calHoldSamples);
 _calRawY[_calStep] = (int16_t)(_calHoldSumY / _calHoldSamples);
 _calStep++;

 if (_calStep < 8) {
 /* Next point */
 _repaintSettings = true;
 } else {
 /* All 8 points captured — validate and compute */
 const int16_t TOLERANCE = 200;
 bool rejected = false;

 for (int i = 0; i < 4; i++) {
 int16_t dx = abs(_calRawX[i] - _calRawX[i + 4]);
 int16_t dy = abs(_calRawY[i] - _calRawY[i + 4]);
 if (dx > TOLERANCE || dy > TOLERANCE) {
 rejected = true;
 break;
 }
 }

 if (rejected) {
 _calPhase = 1;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 } else {
 float avgRawX[4], avgRawY[4];
 for (int i = 0; i < 4; i++) {
 avgRawX[i] = (_calRawX[i] + _calRawX[i + 4]) / 2.0f;
 avgRawY[i] = (_calRawY[i] + _calRawY[i + 4]) / 2.0f;
 }

 float rawLeft_X = (avgRawX[0] + avgRawX[2]) / 2.0f;
 float rawRight_X = (avgRawX[1] + avgRawX[3]) / 2.0f;
 float rawTop_Y = (avgRawY[0] + avgRawY[1]) / 2.0f;
 float rawBot_Y = (avgRawY[2] + avgRawY[3]) / 2.0f;

 float rawLeft_Y = (avgRawY[0] + avgRawY[2]) / 2.0f;
 float rawRight_Y = (avgRawY[1] + avgRawY[3]) / 2.0f;
 float rawTop_X = (avgRawX[0] + avgRawX[1]) / 2.0f;
 float rawBot_X = (avgRawX[2] + avgRawX[3]) / 2.0f;

 float dxInRawX = fabsf(rawRight_X - rawLeft_X);
 float dxInRawY = fabsf(rawRight_Y - rawLeft_Y);
 _calSwapXY = (dxInRawY > dxInRawX);

 /* A raw span measured between the targets stands for winX (winY) px of glass,
  * and the window reaches one inset past each target: the numbers are the
  * panel's less two insets, not 280 and 200 (CAL_SCR_X/Y in DisplayManager.h). */
 constexpr float inset = CAL_INSET, winX = CAL_SPAN_X, winY = CAL_SPAN_Y;
 if (_calSwapXY) {
 float spanX = rawRight_Y - rawLeft_Y;
 _calXMin = (int16_t)(rawLeft_Y - inset * spanX / winX);
 _calXMax = (int16_t)(rawRight_Y + inset * spanX / winX);
 float spanY = rawBot_X - rawTop_X;
 _calYMin = (int16_t)(rawTop_X - inset * spanY / winY);
 _calYMax = (int16_t)(rawBot_X + inset * spanY / winY);
 } else {
 float spanX = rawRight_X - rawLeft_X;
 _calXMin = (int16_t)(rawLeft_X - inset * spanX / winX);
 _calXMax = (int16_t)(rawRight_X + inset * spanX / winX);
 float spanY = rawBot_Y - rawTop_Y;
 _calYMin = (int16_t)(rawTop_Y - inset * spanY / winY);
 _calYMax = (int16_t)(rawBot_Y + inset * spanY / winY);
 }

 _calValid = true;
 UiEvent ev;
 ev.type = UiEvent::EVT_APPLY_TOUCH_CAL;
 pushUiEvent(ev);

 _calPhase = 2;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 }
 }
 _calHolding = false;
 _calHoldReady = false;
 _calHoldSamples = 0;
 }

 _btnHoldStartTime = 0;
 _lastPressedBtn = -1;
 /* The update screen leaves by its own clock (otaHoldMs), or not at all
  * while an update is under way — never by this one. */
 if (_uiMode != MODE_DASHBOARD && _uiMode != MODE_OTA_UPDATE && !_sharedState.isBooting) {
 if (timeSince(_lastTouchTime, 30000)) forceDashboard( );
 }
 return;
 }


 if (!timeSince(_lastTouchTime, 15)) return;
 TS_Point p = _driver.ts->getPoint( );

 /* An injected touch is read ONCE per pass, here: the gates below (the sensitivity
 * screen's Cancelar, the calibration's measuring phase, the pressure gate) all use
 * this one value, so they cannot disagree about whether a finger is behind the
 * touch. */
 const bool simNow = __atomic_load_n(&_simTouchActive, __ATOMIC_ACQUIRE);

 /* ── Sensitivity calibration mode: minimum threshold ──
 * Uses p.z > 50 (ADC noise floor) instead of the calibrated
 * threshold, to capture the full user pressure range. */
 if (_uiMode == MODE_SETTINGS_TOUCH_SENS) {
 /*
 * Sensitivity calibration based on continuous HOLD.
 *
 * User presses and holds the crosshair. System samples
 * p.z continuously and computes rolling stability. When
 * it finds the lowest pressure with stable reading (no
 * oscillation), it sets the threshold and saves.
 *
 * _sensCount — total samples collected
 * _sensSamples[30] — circular buffer of recent samples
 * _sensStability — visual bar progress (0..1)
 * _sensThreshold — lowest stable p.z found
 */

 /* CANCEL button: accepts touch at any pressure — an injected tap too (POST
 * /api/touch, 'touch sim'). It carries no pressure (nothing is on the glass, so
 * the library reads z = 0): the p.z gate shut it out, mapTouchPoint( ) was never
 * reached, and it was the only place the flag was cleared. Bench, 2026-10-16, on
 * the UI-branch image (the same handleTouch logic as main): the board stayed on
 * this screen for 42 s and beyond, until a web reboot. It is still never a
 * pressure sample: with nothing on the glass z = 0, and the p.z < 30 return below
 * drops it. */
 if (p.z >= 50 || simNow) {
 int16_t sx, sy;
 mapTouchPoint(p, sx, sy);
 if (uiSensCancelHit(sx, sy)) {
 /* Provenance before the accept, as below: stale, it blinds the mirror that sent the tap for 5 s. */
 _lastTouchInjected = simNow;
 if (acceptTouch(0)) {
 actSensCancel( );
 return;
 }
 }
 }

 /* After completion, ignores touches until auto-return */
 if (_sensDone) return;

 /* Needs minimum pressure to collect (above ADC noise) */
 if (p.z < 30) return;

 /* Continuous sampling: collects without requiring release */
 uint8_t idx = _sensCount % 30;
 _sensSamples[idx] = p.z;
 _sensCount++;

 /* Needs at least 10 samples for analysis */
 if (_sensCount < 10) {
 _sensStability = (float)_sensCount / 10.0f * 0.3f;
 _repaintSettings = true;
 return;
 }

 /*
 * Rolling stability analysis (last 10 samples).
 * Computes stddev/mean of last 10 values. If < 15%,
 * current pressure is stable.
 */
 int n = (_sensCount < 30) ? _sensCount : 30;
 if (n > 10) n = 10; /* Analysis of last 10 */

 float sum = 0;
 uint16_t minZ = 65535, maxZ = 0;
 int base = (int)((_sensCount - n) % 30);
 for (int i = 0; i < n; i++) {
 uint16_t v = _sensSamples[(base + i) % 30];
 sum += v;
 if (v < minZ) minZ = v;
 if (v > maxZ) maxZ = v;
 }
 float mean = sum / n;

 float varSum = 0;
 for (int i = 0; i < n; i++) {
 float d = _sensSamples[(base + i) % 30] - mean;
 varSum += d * d;
 }
 float stddev = sqrtf(varSum / n);
 float cv = (mean > 0) ? (stddev / mean) : 1.0f; /* coefficient of variation */

 /* Update threshold when a stable zone is found */
 bool isStable = (cv < 0.15f) && (_sensCount >= 10);

 if (isStable) {
 /* Found stable zone: threshold = lowest stable value x 0.8 */
 uint16_t candidate = (uint16_t)(minZ * 0.8f);
 if (candidate < 50) candidate = 50;

 /* Accept if better (lower) than previous, or first found */
 if (_sensThreshold == 0 || candidate < _sensThreshold) {
 _sensThreshold = candidate;
 }

 /* Progress: advances according to time in stable zone */
 _sensStability += 0.02f;
 if (_sensStability > 1.0f) _sensStability = 1.0f;

 /* After bar full (~2s stable): save and complete */
 if (_sensStability >= 1.0f) {
 _sensZThreshold = _sensThreshold;
 _sensDone = true;
 _sensDoneTime = millis( );

 UiEvent ev;
 ev.type = UiEvent::EVT_SAVE_TOUCH_CAL;
 pushUiEvent(ev);
 _touchSoundPending = false;
 }
 } else {
 /* Unstable zone: bar slowly recedes */
 if (_sensStability > 0.0f) _sensStability -= 0.005f;
 if (_sensStability < 0.0f) _sensStability = 0.0f;
 }

 _repaintSettings = true;
 return;
 }

 /* A simulated touch carries no pressure: nothing is on the panel, so
	 * getPoint( ) reads z near zero and this gate discarded every injected
	 * tap before it reached a handler. loopCore1 ORs the sim flag into
	 * _rawTouchState and mapTouchPoint bypasses the ADC mapping, but the
	 * pressure check between them was never taught about the bypass — so
	 * `touch sim` answered "injected" and moved nothing. */
 if (!simNow && p.z < _sensZThreshold) return;

 /* Provenance of the touch that is about to open the 5 s priority window,
	 * recorded here because this is the one place that already has to tell a
	 * finger from an injection. POST /api/touch reads it back: the window is
	 * right to arm for both kinds (flash and telemetry should still back off),
	 * but it must not blind the mirror that sent the tap. */
 _lastTouchInjected = simNow;


 if (_uiMode == MODE_SETTINGS_TOUCH_CAL) {
 _lastTouchTime = millis( );


 if (_calPhase >= 1) {
 int16_t screenX, screenY;
 mapTouchPoint(p, screenX, screenY);
 if (uiCalVerdictHit(screenX, screenY)) {
 actCalVerdictTap( );
 }
 return;
 }


 if (_calStep < 8) {
 /* An injected touch is not a sample: it has no raw position. getPoint( ) hands
 * back the library's cached x/y of the last real touch, and the pressure gate above
 * lets the injection in (simNow), so it would start a hold from those stale values,
 * and a hold of CAL_HOLD_MS (400 ms) or more would record a corner from them on
 * release: a wedge turned into a corrupted calibration, now that the flag ends on its
 * own clock (2026-10-16). Ignored before the hold starts, so no corner is ever taken
 * from it. It sits after the verdict buttons (_calPhase >= 1), which an injected tap
 * still presses; an injection over a real finger's hold only pauses that
 * finger's samples until it ends. */
 if (simNow) return;
 if (!_calHolding) {
 /* Hold start: reset accumulators */
 _calHolding = true;
 _calHoldReady = false;
 _calHoldStart = millis( );
 _calHoldSumX = 0;
 _calHoldSumY = 0;
 _calHoldSamples = 0;
 }

 /* Accumulate samples while holding */
 _calHoldSumX += p.x;
 _calHoldSumY += p.y;
 _calHoldSamples++;

 /* After minimum hold time, signal ready to release */
 if (!_calHoldReady && timeSince(_calHoldStart, CAL_HOLD_MS)) {
 _calHoldReady = true;
 _repaintSettings = true; /* Redraw green crosshair */
 }
 }
 return;
 }


 int16_t x, y;
 mapTouchPoint(p, x, y);

 if (_sharedState.isBooting) {
 if (_sharedState.showSkipButton && uiBootSkipHit( x, y )) actBootSkip( );
 return;
 }


 _lastTouchTime = millis( );

 /* A press belongs to the screen it started on. Core 0 changes _uiMode
  * between two samples of the same press — the keypad's OK becomes the
  * settings menu while the finger is still down — and the next sample used
  * to reach the NEW screen's handler: the OK key sits where the menu's
  * fourth row is, and acceptSlideTouch( ) selected that row before the
  * finger ever left the glass. Measured on the rig with /api/touch on
  * 2026-09-19: every login landed on "System Status". A press that started
  * elsewhere is ignored until it is released. */
 if (!_pressActive) { _pressActive = true; _pressMode = _uiMode; }
 else if (_pressMode != _uiMode) return;

 /* Fallback to _lastWebBusy instead of false when
 * mutex_try_enter fails — avoids processing the touch as if there were no
 * active overlay (visual bypass of the block). */
 bool webBusyNow = _lastWebBusy;
 if (mutex_try_enter(&_stateMutex, NULL)) {
 webBusyNow = _webBusy;
 _lastWebBusy = webBusyNow;
 mutex_exit(&_stateMutex);
 }


 if (webBusyNow && _uiMode == MODE_DASHBOARD) {
 /* Touch stays rejected while a web client holds the device — that decision is
  * unchanged. What is gone is the full-screen overlay this used to paint: the
  * reason is now permanently visible in the top-bar banner, so the user learns it
  * BEFORE touching instead of after, and the dashboard keeps updating. */
 if (!acceptTouch(0xF0)) return;
 return;
 }

#if SIMUT_UI_ANGULO
 /* The Ângulo build (src/ui/angulo/): the touch lands on the frame it
  * painted. Its screens either act on it themselves (true: nothing below
  * runs) or move the point to where the old layout has the same element, for
  * the branch below to do what it always did. */
 if (uaToque(x, y)) return;
#endif

 if (_uiMode == MODE_DASHBOARD) {
 if (y > 35 && y < 110) {
 /* The min/max graph button, on touch-down, over the whole rectangle it is
  * painted on and not a hand-written slice of it. It used to read `x > 266`
  * while the button is drawn from CARD_X + minMaxBtnX(CARD_W) = 245 to 302, so
  * its left third fell through to the short-tap path and turned min/max OFF —
  * the mirror image of the mode-indicator bug (see actTopCardPress( )), on the same button. Both
  * zones now come from the same three numbers the drivers draw with. */
 constexpr int16_t MM_BTN_X0 = CARD_X + minMaxBtnX(CARD_W);
 constexpr int16_t MM_BTN_X1 = MM_BTN_X0 + MINMAX_BTN_W;   /* exclusive */
 actTopCardPress( x >= MM_BTN_X0 && x < MM_BTN_X1 );
 return;
 }
 if (y > 115 && y < 190) {
 if (!acceptTouch(1)) return;
 actBottomCardTap( x > 266 );
 return;
 }
 if (y > 195) {
 const int btnW = 58, gap = 5, pitch = btnW + gap;
 int btnIdx = (x - 5) / pitch;
 actNavButton( btnIdx );
 }
 }
#if SIMUT_TFT_GRAPH
 else if (_uiMode == MODE_GRAPH_VIEW) {
 /* X button (close) — top right corner */
 if (y < 40 && x > 284) { if (!acceptTouch(0)) return; actGraphClose( ); return; }
 /* Tap on header — show sensor name for 3s */
 if (y < 28 && x < 284) {
 if (!acceptTouch(0)) return;
 actGraphShowName( );
 return;
 }
 /* ── Bottom bar: [◀Past][▶Fut][📅Cal][🔍+ZoomIn][🔍-ZoomOut] ──
 * Zones derive from the same GRAPH_PBTN_* constants the renderer draws
 * from (DisplayManager.h), so the two cannot drift apart. */
 if (y >= 195) {
 const int btnW = GRAPH_PBTN_W, gap = GRAPH_PBTN_GAP, startX = GRAPH_PBTN_X0;
 int btn = -1;
 for (int i = 0; i < 5; i++) {
 int bx = startX + i * (btnW + gap);
 if (x >= bx && x <= bx + btnW) { btn = i; break; }
 }

 if (btn == 0) {
 /* Past (◀) */
 if (!acceptHoldTouch(10)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 ); /* instant feedback; Core 0 read may take a while */
 actGraphNav( _graphData, -1 ); return;
 }
 if (btn == 1 && _graphNavOffset < 0) {
 /* Future (▶) — only if offset < 0 */
 if (!acceptHoldTouch(11)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actGraphNav( _graphData, +1 ); return;
 }
 if (btn == 2) {
 /* Calendar (📅) */
 if (!acceptTouch(0)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 ); /* days-mask scan of /history is not instant */
 actGraphOpenCalendar( _graphData ); return;
 }
 if (btn == 3 && _graphData.timeRange > 0) {
 /* Zoom In — shorter range (more detail) */
 if (!acceptHoldTouch(12)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actGraphPeriod( _graphData, _graphData.timeRange - 1 ); return;
 }
 if (btn == 4 && _graphData.timeRange < 4) {
 /* Zoom Out — longer range (less detail) */
 if (!acceptHoldTouch(13)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actGraphPeriod( _graphData, _graphData.timeRange + 1 ); return;
 }
 }
 /* Tap on central zone -> temperature details (page 0) */
 if (y >= 40 && y < 195) {
 if (!acceptTouch(10)) return;
 actGraphDetailOpen( );
 }
 }
 else if (_uiMode == MODE_GRAPH_DETAIL) {
 /* X button — close to dashboard */
 if (y < 40 && x > 284) { if (!acceptTouch(0)) return; actGraphClose( ); return; }
 /* Tap on header — show sensor name for 3s */
 if (y < 28 && x < 284) {
 if (!acceptTouch(0)) return;
 actGraphShowName( );
 return;
 }
 /* Bottom bar — same logic as graph view, same GRAPH_PBTN_* geometry */
 if (y >= 195) {
 const int btnW = GRAPH_PBTN_W, gap = GRAPH_PBTN_GAP, startX = GRAPH_PBTN_X0;
 int btn = -1;
 for (int i = 0; i < 5; i++) {
 int bx = startX + i * (btnW + gap);
 if (x >= bx && x <= bx + btnW) { btn = i; break; }
 }

 if (btn == 0) {
 /* Past (◀) */
 if (!acceptHoldTouch(10)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actGraphNav( _graphData, -1 ); return;
 }
 if (btn == 1 && _graphNavOffset < 0) {
 /* Future (▶) — only if offset < 0 */
 if (!acceptHoldTouch(11)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actGraphNav( _graphData, +1 ); return;
 }
 if (btn == 2) {
 /* Calendar (📅) */
 if (!acceptTouch(0)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actGraphOpenCalendar( _graphData ); return;
 }
 if (btn == 3 && _graphData.timeRange > 0) {
 /* Zoom In — shorter range (more detail) */
 if (!acceptHoldTouch(12)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actGraphPeriod( _graphData, _graphData.timeRange - 1 ); return;
 }
 if (btn == 4 && _graphData.timeRange < 4) {
 /* Zoom Out — longer range (less detail) */
 if (!acceptHoldTouch(13)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actGraphPeriod( _graphData, _graphData.timeRange + 1 ); return;
 }
 }
 /* Tap on central zone -> next page or return to graph */
 if (y >= 40 && y < 195) {
 if (!acceptTouch(10)) return;
 bool hasHumNow = _graphData.hasHumidity && !isnan(_currentMinHum);
 bool hasPressNow = _graphData.hasPressure;
 if (_detailPage == 0 && hasHumNow) {
 /* Temperature -> Humidity */
 actDetailPage( 1 );
 } else if (_detailPage < 2 && hasPressNow) {
 /* Temperature or Humidity -> Pressure (BMP280 skips the hum page) */
 actDetailPage( 2 );
 } else {
 /* Last page for this sensor -> return to graph */
 actDetailBack( );
 }
 }
 }
 /* ── CALENDAR ── */
 else if (_uiMode == MODE_CALENDAR) {
 /* X button (back to graph) — top right corner */
 if (y < 28 && x >= 270) {
 if (!acceptTouch(0)) return;
 actCalendarBack( );
 return;
 }
 /* ◀ Month arrow — header left */
 if (y < 28 && x < 30) {
 if (!acceptSlideTouch(20)) return;
 actCalendarMonth( _graphData, -1 ); return;
 }
 /* ▶ Month arrow — header right */
 if (y < 28 && x > 290) {
 if (!acceptSlideTouch(21)) return;
 actCalendarMonth( _graphData, +1 ); return;
 }
 /* ── Day grid (y=44..190) ── */
 if (y >= 44 && y < 190) {
 const int gridStartY = 46, cellW = 44, cellH = 24;
 int row = (y - gridStartY) / cellH;
 int col = x / cellW;
 if (col >= 0 && col < 7 && row >= 0 && row < 6) {
 /* Calculate first day of week */
 struct tm firstTm = {};
 firstTm.tm_year = _calYear - 1900;
 firstTm.tm_mon = _calMonth - 1;
 firstTm.tm_mday = 1;
 mktime(&firstTm);
 int firstDow = firstTm.tm_wday;

 int cell = row * 7 + col;
 int dayNum = cell - firstDow + 1;

 /* Check if it's a valid day with data */
 if (calDayHasHistory( _calDaysMask, dayNum )) {
 if (!acceptTouch(0)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 ); /* day read can take a second on 24H */
 actCalendarDay( _graphData, dayNum );
 }
 }
 }
 /* ── Bottom bar: [◀ Month] [Today] [Month ▶] ── */
 if (y >= 195) {
 if (x < 106) {
 /* ◀ Month */
 if (!acceptSlideTouch(20)) return;
 actCalendarMonth( _graphData, -1 );
 } else if (x >= 108 && x < 212) {
 /* Today — returns to graph with offset 0 */
 if (!acceptTouch(0)) return;
 uiChanged( UiChange::GRAPH_BUSY, 0 );
 actCalendarToday( _graphData );
 } else if (x >= 217) {
 /* Month ▶ */
 if (!acceptSlideTouch(21)) return;
 actCalendarMonth( _graphData, +1 );
 }
 }
 }
#endif
 else if (_uiMode == MODE_SETTINGS_THEMES) {
 if (y >= 40 && y <= 185) {
 int clickedIndex = 0;
 if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
 int actualIndex = (_themePage * 4) + clickedIndex;
 if (actualIndex < getThemeCount( ) && actualIndex != _previewThemeIdx) {
 if (!acceptSlideTouch(clickedIndex)) return;
 _previewThemeIdx = actualIndex; _themePage = _previewThemeIdx / 4; _repaintSettings = true;
 }
 }
 else if (y > 185) {
 if (x < 70) {
 if (!acceptHoldTouch(10)) return;
 if (_previewThemeIdx > 0) _previewThemeIdx--; else _previewThemeIdx = getThemeCount( ) - 1;
 _themePage = _previewThemeIdx / 4; _repaintSettings = true;
 } else if (x < 138) {
 if (!acceptHoldTouch(11)) return;
 if (_previewThemeIdx < getThemeCount( ) - 1) _previewThemeIdx++; else _previewThemeIdx = 0;
 _themePage = _previewThemeIdx / 4; _repaintSettings = true;
 } else if (x < 219) {
 if (!acceptTouch(12)) return;
 showSettingsMain( );
 } else {
 if (!acceptTouch(13)) return;
 actApplyTheme( _previewThemeIdx );
 }
 }
 }
 else if (_uiMode == MODE_SETTINGS_ALARMS) {
 if (y >= 40 && y <= 185) {
 int clickedIndex = 0;
 if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
 int mapIdx = (_alarmPage * 4) + clickedIndex;
 if (mapIdx < _activeSensorCount) {
 if (mapIdx == _alarmSelection) {
 if (!acceptTouch(clickedIndex + 4)) return;
 actAlarmListOpen( _alarmSelection );
 } else if (mapIdx != _alarmSelection) {
 /* Tap on name/bar: select the item */
 if (!acceptSlideTouch(clickedIndex)) return;
 _alarmSelection = mapIdx; _alarmPage = _alarmSelection / 4; _repaintSettings = true;
 }
 }
 }
 else if (y > 185) {
 if (x < 70) {
 if (!acceptHoldTouch(10)) return;
 if (_alarmSelection > 0) _alarmSelection--; else _alarmSelection = _activeSensorCount - 1;
 _alarmPage = _alarmSelection / 4; _repaintSettings = true;
 } else if (x < 138) {
 if (!acceptHoldTouch(11)) return;
 if (_alarmSelection < _activeSensorCount - 1) _alarmSelection++; else _alarmSelection = 0;
 _alarmPage = _alarmSelection / 4; _repaintSettings = true;
 } else {
 if (!acceptTouch(12)) return;
 showSettingsMain( );
 }
 }
 }
 else if (_uiMode == MODE_SETTINGS_ALARM_EDIT) {
 /* One limit per bar, four bars per page. The value is adjusted on the bar
  * itself — the arrows at its ends — and the footer arrows move the
  * selection, which is what pages the list. Bounds come from the channel
  * table, so a quantity that is neither temperature nor humidity is clamped
  * to its own plausible range instead of a hand-written -50..150. */
 const SensorType sType = (SensorType)_tempAlarmConfig.sensorType;
 const uint8_t nLimits = sensorLimitCount(sType);
 if (nLimits == 0) return;
 if (_editFieldFocus >= (int)nLimits) _editFieldFocus = (int)nLimits - 1;
 if (_editFieldFocus < 0) _editFieldFocus = 0;

 auto adjustVal = [](float val, float step, float minV, float maxV) -> float {
 val += step; val = round(val * 10.0f) / 10.0f;
 if (val < minV) val = minV;
 if (val > maxV) val = maxV;
 return val;
 };

 /* MIN and MAX of one channel may not cross. Push the one the user is NOT
  * holding, so the digit under their finger keeps moving the way they
  * pressed. Runs over every channel, not the first two. */
 auto enforceInterlock = [&](int heldIdx) {
 for (uint8_t i = 0; i < nLimits; i += 2) {
 const uint8_t c = sensorLimitChannel(sType, i);
 if (!channelValid(c)) continue;
 const ChannelInfo& ci = channelInfo(c);
 if (_tempAlarmConfig.chMin[c] >= _tempAlarmConfig.chMax[c]) {
 if (heldIdx == (int)i) /* holding this channel's MIN */
 _tempAlarmConfig.chMax[c] = round((_tempAlarmConfig.chMin[c] + 0.1f) * 10.0f) / 10.0f;
 else
 _tempAlarmConfig.chMin[c] = round((_tempAlarmConfig.chMax[c] - 0.1f) * 10.0f) / 10.0f;
 }
 if (_tempAlarmConfig.chMax[c] > ci.saneMax) _tempAlarmConfig.chMax[c] = ci.saneMax;
 if (_tempAlarmConfig.chMin[c] < ci.saneMin) _tempAlarmConfig.chMin[c] = ci.saneMin;
 }
 };

 auto stepLimit = [&](int idx, float step) {
 const uint8_t c = sensorLimitChannel(sType, (uint8_t)idx);
 if (!channelValid(c)) return;
 const ChannelInfo& ci = channelInfo(c);
 const bool isMax = sensorLimitIsMax((uint8_t)idx);
 /* Read-modify-write rather than a reference: chMin/chMax live in a packed
  * struct, whose fields have no alignment guarantee to bind a float& to. */
 float v = isMax ? _tempAlarmConfig.chMax[c] : _tempAlarmConfig.chMin[c];
 v = adjustVal(v, step, ci.saneMin, ci.saneMax);
 if (isMax) _tempAlarmConfig.chMax[c] = v; else _tempAlarmConfig.chMin[c] = v;
 enforceInterlock(idx);
 };

 const int page = _editFieldFocus / ALARM_EDIT_ROWS;

 if (y >= ALARM_EDIT_Y0 && y <= 188) {
 int row;
 if (y < 80) row = 0; else if (y < 118) row = 1; else if (y < 156) row = 2; else row = 3;
 const int idx = page * ALARM_EDIT_ROWS + row;
 if (idx >= (int)nLimits) return;

 if (x <= ALARM_EDIT_HIT_DEC || x >= ALARM_EDIT_HIT_INC) {
 const bool inc = (x >= ALARM_EDIT_HIT_INC);
 /* A distinct zone per (row, direction) so sliding off the arrow ends the
  * repeat instead of silently retargeting it at another limit. */
 const uint8_t zone = (uint8_t)(30 + row * 2 + (inc ? 1 : 0));
 if (!acceptHoldTouch(zone)) return;
 if (_lastPressedBtn != (int)zone) { _btnHoldStartTime = millis( ); _lastPressedBtn = (int)zone; }
 uint32_t holdTime = millis( ) - _btnHoldStartTime;
 float mag = limitStepMag( holdTime );
 _editFieldFocus = idx;
 stepLimit(idx, inc ? mag : -mag);
 _repaintSettings = true;
 }
 else {
 if (!acceptSlideTouch((uint8_t)row)) return;
 actLimitFocus( idx );
 }
 }

 else if (y >= 190) {
 if (x < 70) {
 /* Selection up, with hold-repeat. Paging follows the selection, and
  * the ends wrap instead of stopping — same as the alarm list above. */
 if (!acceptHoldTouch(10)) return;
 _editFieldFocus = (_editFieldFocus > 0) ? _editFieldFocus - 1
 : (int)nLimits - 1;
 _repaintSettings = true;
 }
 else if (x < 138) {
 if (!acceptHoldTouch(11)) return;
 _editFieldFocus = (_editFieldFocus < (int)nLimits - 1) ? _editFieldFocus + 1
 : 0;
 _repaintSettings = true;
 }
 else if (x < 219) {
 /* BACK: discard. Until v24 this ALSO switched the alarm off and saved —
  * the editor was only reachable from an OFF row, so leaving meant "keep
  * it off". Enable/disable is its own action now (EVT_ALARM_BLOCK), with
  * its own bit and its own record on the alarm line. */
 if (!acceptTouch(12)) return;
 actLimitsBack( );
 }
 else {
 /* SAVE: the working copy stays here for Core 0 to diff against the
  * config (editedAlarmRecord) — it decides, saves, logs and emits the
  * alarm_lim record, then brings the sensor menu back. */
 if (!acceptTouch(13)) return;
 actLimitsSave( );
 }
 }
 }
 else if (handleTouchPanelV24(x, y)) {
 /* v24: PIN keypad, per-sensor actions, maintenance, users, result screen —
  * DisplayManager_Users.cpp. */
 }
 else if (_uiMode == MODE_SETTINGS_MAIN) {
 if (y >= 40 && y <= 185) {
 int clickedIndex = 0;
 if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
 int mapIdx = (_mainMenuPage * 4) + clickedIndex;
 if (mapIdx < _menuCount && mapIdx != _menuSelection) {
 if (!acceptSlideTouch(clickedIndex)) return;
 _menuSelection = mapIdx; _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
 }
 }
 else if (y > 185) {
 if (x < 70) {
 if (!acceptHoldTouch(10)) return;
 if (_menuSelection > 0) _menuSelection--; else _menuSelection = (_menuCount > 0) ? _menuCount - 1 : 0;
 _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
 }
 else if (x < 138) {
 if (!acceptHoldTouch(11)) return;
 if (_menuSelection < _menuCount - 1) _menuSelection++; else _menuSelection = 0;
 _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
 }
 else if (x < 219) {
 if (!acceptTouch(12)) return;
 forceDashboard( );
 }
 else {
 if (!acceptTouch(13)) return;
 actMenuSelect( _menuSelection );
 }
 }
 }
 else if (_uiMode == MODE_SETTINGS_DISPLAY_OFFSET) {
 /*
 * Control layout (logical coordinates — the offset itself applied
 * to the TFT already shifts the image):
 * Direction pad centered at (160, 120):
 * ▲ (130..190, 55..95) -> Y -= 1
 * ▼ (130..190, 145..185) -> Y += 1
 * ◀ (80..140, 100..140) -> X -= 1
 * ▶ (180..240, 100..140) -> X += 1
 * Reset button center (148..172, 108..132) -> zeros both
 * Footer:
 * BACK (10..130, 200..240) -> discard and go back
 * APPLY (190..310, 200..240) -> fires EVT_APPLY_DISPLAY_OFFSET
 */
 /*
 * Each adjustment is applied to the TFT in real time via
 * setDisplayOffset(), allowing immediate visual calibration. BACK
 * reverts to the saved offset; APPLY fires
 * EVT_APPLY_DISPLAY_OFFSET (Core 0 persists + resets touch).
 * Every change forces a full redraw to avoid artifacts from
 * content drawn with the previous offset on the previous screen.
 */
 bool changed = false;
 if (y >= 55 && y <= 95 && x >= 130 && x <= 190) {
 if (!acceptHoldTouch(20)) return;
 changed = actOffsetUp( );
 }
 else if (y >= 145 && y <= 185 && x >= 130 && x <= 190) {
 if (!acceptHoldTouch(21)) return;
 changed = actOffsetDown( );
 }
 else if (y >= 100 && y <= 140 && x >= 80 && x <= 140) {
 if (!acceptHoldTouch(22)) return;
 changed = actOffsetLeft( );
 }
 else if (y >= 100 && y <= 140 && x >= 180 && x <= 240) {
 if (!acceptHoldTouch(23)) return;
 changed = actOffsetRight( );
 }
 else if (y >= 108 && y <= 132 && x >= 148 && x <= 172) {
 if (!acceptTouch(24)) return;
 changed = actOffsetCentre( );
 }
 else if (y >= 200 && x <= 130) {
 if (!acceptTouch(25)) return;
 actOffsetBack( );
 return;
 }
 else if (y >= 200 && x >= 190) {
 if (!acceptTouch(26)) return;
 actOffsetApply( );
 return;
 }

 if (changed) {
 actOffsetPreview( );
 }
 }
 else if (_uiMode == MODE_SETTINGS_LANG) {
 /* Limit interaction to slot 0 when no .lng loaded.
 * Dynamic bound avoids slot 1 "ghost". */
 int activeSlots = langSlotCount( _activeLangLoaded, LANG_COUNT );
 if (y >= 40 && y <= 185) {
 int clickedIndex = 0;
 if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
 int actualIndex = (_langPage * 4) + clickedIndex;
 if (actualIndex < activeSlots && actualIndex != _previewLangIdx) {
 if (!acceptSlideTouch(clickedIndex)) return;
 _previewLangIdx = actualIndex;
 _langPage = _previewLangIdx / 4;
 _repaintSettings = true;
 }
 }
 else if (y > 185) {
 if (x < 70) {
 if (!acceptHoldTouch(10)) return;
 if (_previewLangIdx > 0) _previewLangIdx--; else _previewLangIdx = activeSlots - 1;
 _langPage = _previewLangIdx / 4;
 _repaintSettings = true;
 }
 else if (x < 138) {
 if (!acceptHoldTouch(11)) return;
 if (_previewLangIdx < activeSlots - 1) _previewLangIdx++; else _previewLangIdx = 0;
 _langPage = _previewLangIdx / 4;
 _repaintSettings = true;
 }
 else if (x < 219) {
 if (!acceptTouch(12)) return;
 showSettingsMain( );
 }
 else {
 if (!acceptTouch(13)) return;
 actApplyLang( _previewLangIdx );
 }
 }
 }


 else if (_uiMode == MODE_SETTINGS_PASSWORD) {
 using namespace PwdKb;

 /* Result screens (error/success): single button at the bottom. */
 if (_kbPhase >= 2) {
 if (y >= 185) {
 if (!acceptTouch(0)) return;
 actPwdResult( );
 }
 return;
 }

 char* activeBuf = (_kbPhase == 0) ? _kbBuffer : _kbConfirmBuf;

 /* An open popup owns the whole surface: a key inserts its
  * character, the card between keys swallows the tap, and anywhere
  * outside the card cancels (including the title-bar X). */
 if (_kbPopup != POPUP_NONE) {
 int hit = popupHit(_kbPopup, x, y);
 if (hit == -1) return;
 if (!acceptTouch((uint8_t)(hit >= 0 ? 0x60 + hit : 0x5F))) return;
 if (hit >= 0 && _kbCursor < kbMaxLen( )) {
 activeBuf[_kbCursor++] = popupChar(_kbPopup, hit);
 activeBuf[_kbCursor] = '\0';
 }
 _kbPopup = POPUP_NONE;
 _repaintSettings = true;
 return;
 }

 /* X on the title bar: leave the screen. */
 if (y < 28 && x > 280) {
 if (!acceptTouch(1)) return;
 actPwdLeave( );
 return;
 }

 /* OK — finger button beside the boxes. */
 if (x >= OK_X && y >= OK_Y && y < OK_Y + OK_H + 4) {
 if (!acceptTouch(54)) return;
 if (_kbPurpose == 1) {
 /* v24: the name of a new account. One character satisfies the
  * keyboard; Core 0 applies the real rule (isValidName, no duplicate,
  * not "admin") and answers on the result screen. */
 if (_kbCursor < 1) { _errorSoundPending = true; return; }
 actPwdNameAccept(_kbBuffer);
 return;
 }
 actPwdLegacyOk( );
 return;
 }

 /* Password boxes: toggle mask/plain. */
 if (y >= BOX_Y && y < GRID_Y0 && x < OK_X - 4) {
 if (!acceptTouch(2)) return;
 actPwdToggleShow( );
 return;
 }

 /* Group grid — gaps count as the nearest key (finger-friendly). */
 if (y >= GRID_Y0) {
 int row = (y - GRID_Y0) / GRID_ROW_H;
 int col = (x - GRID_X0) / GRID_COL_W;
 if (row > 2) row = 2;
 if (col < 0) col = 0;
 if (col > 3) col = 3;
 if (!acceptTouch((uint8_t)(10 + row * 4 + col))) return;

 if (row < 2) {
 _kbPopup = (int8_t)(POPUP_GROUP0 + row * 4 + col);
 }
 else if (col == 0) _kbPopup = POPUP_DIGITS;
 else if (col == 1) _kbPopup = POPUP_SYMBOLS;
 else if (col == 2) {
 if (_kbCursor < kbMaxLen( )) {
 activeBuf[_kbCursor++] = ' ';
 activeBuf[_kbCursor] = '\0';
 }
 }
 else {
 if (_kbCursor > 0) {
 activeBuf[--_kbCursor] = '\0';
 }
 }
 _repaintSettings = true;
 return;
 }
 }


 else if (_uiMode == MODE_SETTINGS_SOUNDS) {


 if (_inMelodySelect) {
 const int TOTAL_VARIANTS = 6;
 int melPage = _melSelectIdx / 4;

 if (y >= 40 && y <= 185) {
 int clickedIndex = 0;
 if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1;
 else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
 int mapIdx = (melPage * 4) + clickedIndex;
 if (mapIdx >= TOTAL_VARIANTS) return;
 if (!acceptSlideTouch(0x80 + clickedIndex)) return;

 actMelodyPick( (uint8_t)mapIdx );
 }
 else if (y > 185) {
 if (x < 70) {
 if (!acceptTouch(0x90)) return;
 actMelodyPick( (_melSelectIdx > 0) ? _melSelectIdx - 1 : TOTAL_VARIANTS - 1 );
 }
 else if (x < 138) {
 if (!acceptTouch(0x91)) return;
 actMelodyPick( (_melSelectIdx < TOTAL_VARIANTS - 1) ? _melSelectIdx + 1 : 0 );
 }
 else if (x < 219) {
 if (!acceptTouch(0x92)) return;
 actMelodyBack( );
 }
 else {
 if (!acceptTouch(0x93)) return;
 actMelodyUse( );
 }
 }
 return;
 }


 /* 9 items (Attention added between Web and Mute). */
 const int TOTAL_SOUND_ITEMS = 9;
 int soundPage = _soundSelection / 4;

 if (y >= 40 && y <= 185) {
 int clickedIndex = 0;
 if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1;
 else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
 int mapIdx = (soundPage * 4) + clickedIndex;
 if (mapIdx >= TOTAL_SOUND_ITEMS) return;

 /* Touch gate MOVED inside the branches. A single gate at the top
 * (acceptSlideTouch) sets _lastTouchRegion to clickedIndex (0-3);
 * the acceptHoldTouch(20/21) of the volume branches below would
 * fail due to zoneId mismatch when the finger remained pressed —
 * blocking inc/dec. Now each branch uses the accept appropriate
 * to its interaction mode. */

 if (mapIdx != _soundSelection) {
 if (!acceptSlideTouch(clickedIndex)) return;
 _soundSelection = mapIdx;
 _repaintSettings = true;
 } else {
 /* 0=volume, 1=alarmVol, 2=touch, 3=confirm,
 * 4=error, 5=alarm, 6=attention, 7=web, 8=mute. */
 if (mapIdx == 0) {
 if (!acceptHoldTouch(20)) return; /* sole accept on this path */
 actSoundVolume( x < 160 );
 }
 else if (mapIdx == 1) {
 if (!acceptHoldTouch(21)) return;
 actAlarmVolume( x < 160 );
 }
 else if (mapIdx >= 2 && mapIdx <= 6) {
 /* Individual sounds with melody selection: touch (2),
 * confirm (3), error (4), alarm (5), attention (6). */
 if (!acceptSlideTouch(clickedIndex)) return;
 bool* enablePtr = nullptr;
 uint8_t melType = 0;
 uint8_t curMel = 0;
 switch (mapIdx) {
 case 2: enablePtr = &_soundSettings.touchEnabled;
 melType = 0; curMel = _soundSettings.touchMelody; break;
 case 3: enablePtr = &_soundSettings.confirmEnabled;
 melType = 1; curMel = _soundSettings.confirmMelody; break;
 case 4: enablePtr = &_soundSettings.errorEnabled;
 melType = 2; curMel = _soundSettings.errorMelody; break;
 case 5: enablePtr = &_soundSettings.alarmEnabled;
 melType = 3; curMel = _soundSettings.alarmMelody; break;
 case 6: enablePtr = &_soundSettings.attentionEnabled;
 melType = 4; curMel = _soundSettings.attentionMelody; break;
 }

 if (enablePtr && *enablePtr) {
 *enablePtr = false;
 _repaintSettings = true;
 } else {
 actMelodyOpen( melType, curMel );
 }
 }
 else if (mapIdx == 7) {
 if (!acceptSlideTouch(clickedIndex)) return;
 actSoundToggle( _soundSettings.webEnabled );
 }
 else if (mapIdx == 8) {
 if (!acceptSlideTouch(clickedIndex)) return;
 actMuteToggle( );
 }
 }
 }
 else if (y > 185) {
 if (x < 70) {
 if (!acceptHoldTouch(10)) return;
 if (_soundSelection > 0) _soundSelection--; else _soundSelection = TOTAL_SOUND_ITEMS - 1;
 _repaintSettings = true;
 }
 else if (x < 138) {
 if (!acceptHoldTouch(11)) return;
 if (_soundSelection < TOTAL_SOUND_ITEMS - 1) _soundSelection++; else _soundSelection = 0;
 _repaintSettings = true;
 }
 else if (x < 219) {
 if (!acceptTouch(12)) return;
 showSettingsMain( );
 }
 else {
 if (!acceptTouch(13)) return;
 actSaveSounds( );
 }
 }
 }


 else if (_uiMode == MODE_SETTINGS_STATUS) {
 if (y > 185) {
 if (x < 70) {
 if (!acceptHoldTouch(10)) return;
 if (_statusPage > 0) _statusPage--; else _statusPage = STATUS_PAGES - 1;
 _forceSettingsRedraw = true; _repaintSettings = true;
 }
 else if (x < 138) {
 if (!acceptHoldTouch(11)) return;
 if (_statusPage < STATUS_PAGES - 1) _statusPage++; else _statusPage = 0;
 _forceSettingsRedraw = true; _repaintSettings = true;
 }
 else if (x < 219) {
 if (!acceptTouch(12)) return;
 showSettingsMain( );
 }
 }
 }

 else if (_uiMode == MODE_SETTINGS_LICENSE) {

 if (y >= 32 && y <= 189) {
 /* Tap on text area: upper half = previous page, lower = next */
 if (y < 110) {
 if (!acceptTouch(0)) return;
 actLicensePage( -1 );
 } else {
 if (!acceptTouch(1)) return;
 actLicensePage( +1 );
 }
 _repaintSettings = true;
 }
 else if (y > 190) {
 if (x < 107) {
 if (!acceptHoldTouch(10)) return;
 actLicensePage( -1 );
 _repaintSettings = true;
 }
 else if (x < 213) {
 if (!acceptHoldTouch(11)) return;
 actLicensePage( +1 );
 _repaintSettings = true;
 }
 else {
 if (!acceptTouch(12)) return;
 /* Always back to the menu: the license is a menu item and nothing else
  * reaches it. The keypad used to carry a button for it and had to come
  * back to a half-typed PIN; that button is gone (2026-09-19). */
 showSettingsMain( );
 }
 }
 }


 else if (_uiMode == MODE_ALARM_ACTION) {
 if (y >= 60 && y <= 105) {
 if (!acceptTouch(0)) return;
 actAlarmSilence( );
 }
 else if (y >= 115 && y <= 160) {
 if (!acceptTouch(1)) return;
 actAlarmDeactivate( );
 }
 else if (y >= 170 && y <= 215) {
 if (!acceptTouch(2)) return;
 actAlarmShowMinMax( );
 }
 }
 else if (_uiMode == MODE_CONFIRM_MUTE_ALL) {
 /* 2 buttons at y=190..230. Back (x=20..150), Confirm (x=170..300). */
 if (y >= 190 && y <= 230) {
 if (x >= 20 && x <= 150) {
 if (!acceptTouch(0xC0)) return;
 actMuteAllBack( );
 }
 else if (x >= 170 && x <= 300) {
 actMuteAllConfirm( );
 }
 }
 }
#if SIMUT_NET_WIFI   /* the wired build has no setup AP (2026-10-13) */
 else if (_uiMode == MODE_CONFIRM_AP) {
 /* Same two buttons, same rects as the mute confirmation above. */
 if (y >= 190 && y <= 230) {
 if (x >= 20 && x <= 150) {
 if (!acceptTouch(0xC2)) return;
 showSettingsMain( );
 }
 else if (x >= 170 && x <= 300) {
 if (!acceptTouch(0xC3)) return;
 actStartAp( );
 }
 }
 }
#endif
 else if (_uiMode == MODE_SET_CLOCK) {
 /* The two buttons are the AP confirmation's rects. SKIP (at boot) or BACK
  * stays on this core — nothing changes but the screen. SAVE hands the fields
  * to Core 0, which checks them and decides whether this session may set the
  * clock (AppManager_Events.cpp); the panel draws nothing until it answers. */
 if (y >= 190 && y <= 230) {
 if (x >= 20 && x <= 150) {
 if (!acceptTouch(0xC4)) return;
 actClockLeave( );
 }
 else if (x >= 170 && x <= 300) {
 if (!acceptTouch(0xC5)) return;
 actClockSave( );
 }
 return;
 }
 /* A column's +1 button is above its value and its -1 below, each a full
  * CLOCK_ROW_H band — the value band between them is dead, so a press that
  * lands on a number changes nothing. Held, a button repeats every
  * HOLD_REPEAT_MS, the limit editor's behaviour: thirty days is a held press
  * of nine seconds, not thirty taps. */
 int delta = 0;
 if (y >= CLOCK_UP_Y && y < CLOCK_UP_Y + CLOCK_ROW_H) delta = +1;
 else if (y >= CLOCK_DOWN_Y && y < CLOCK_DOWN_Y + CLOCK_ROW_H + 4) delta = -1;
 if (delta == 0) return;
 for (uint8_t i = 0; i < CE_FIELDS; i++) {
 if (x < CLOCK_COL_X[i] || x >= CLOCK_COL_X[i] + CLOCK_COL_W[i]) continue;
 if (!acceptHoldTouch((uint8_t)(0xD0 + i * 2 + (delta > 0 ? 1 : 0)))) return;
 actClockStep( i, delta );
 return;
 }
 }
 else if (_uiMode == MODE_OTA_UPDATE) {
 actOtaDismiss( );   /* deaf while an update runs: the phase test and the gate are inside */
 }
}

bool DisplayManager::acceptTouch(uint8_t zoneId) {
 if (!_touchReleased) return false;

 _touchReleased = false;
 _lastTouchRegion = zoneId;
 _lastRegionTouchTime = millis( );
 _lastTouchTimestamp = millis( );
 _touchSoundPending = true;
 return true;
}

/**
 * @brief Accepts touch with hold-repeat.
 *
 * First touch: accepts immediately and plays beep.
 * While holding: repeats every HOLD_REPEAT_MS (300ms) with beep.
 * Used for list navigation buttons and increment/decrement.
 */
bool DisplayManager::acceptHoldTouch(uint8_t zoneId) {
 uint32_t now = millis( );

 if (_touchReleased) {
 /* First touch: accept and play beep */
 _touchReleased = false;
 _lastTouchRegion = zoneId;
 _lastRegionTouchTime = now;
 _lastTouchTimestamp = now;
 _holdRepeatLastFire = now;
 _touchSoundPending = true;
 return true;
 }

 /* Holding: repeat every 300ms with beep */
 if (zoneId == _lastTouchRegion && (now - _holdRepeatLastFire >= HOLD_REPEAT_MS)) {
 _holdRepeatLastFire = now;
 _lastTouchTimestamp = now;
 _touchSoundPending = true;
 return true;
 }

 return false;
}

/**
 * @brief Accepts touch with sliding between zones.
 *
 * First touch: accepts immediately with beep.
 * Sliding to a different zone: accepts with beep (no release required).
 * Holding on the same zone: does not repeat.
 * Used for slots, graph periods, and selection lists.
 */
bool DisplayManager::acceptSlideTouch(uint8_t zoneId) {
 /* First touch or slid to a different zone */
 if (_touchReleased || zoneId != _lastTouchRegion) {
 _touchReleased = false;
 _lastTouchRegion = zoneId;
 _lastRegionTouchTime = millis( );
 _lastTouchTimestamp = millis( );
 _touchSoundPending = true;
 return true;
 }

 /* Same zone, holding: do not repeat */
 return false;
}
