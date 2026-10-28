/**
 * @file DisplayManager_Touch.cpp
 * @brief Touch handling: handleTouch (the core's gates, then the driver's touch chain) + accept helpers.
 * @details Sub-file of DisplayManager.cpp.
 * handleTouch is called from loopCore1 every frame on Core 1. The gestures by screen (dashboard / graph /
 * settings / auth / calibration / alarm action) are the driver's: the original's mode chain is the fragment
 * ui/original/UiTouchChain.h, the Ângulo's is one call, uiTouch( x, y ), each included inside handleTouch( )
 * by ui/UiDriverTouch.h (S10b, C3). acceptTouch/Hold/Slide are debounce + repeat-on-hold gates.
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
#include "display/SettingsSteps.h"     /* unused here since the Idioma touch (ui/original/UiTouchChain.h) counts its rows with langRowsListed( ), display/LangChoice.h */
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

#include "ui/UiDriverTouch.h"
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
