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
#include "SensorManager.h"
#include "DisplayManager_Fonts.h"
#include "DisplayManager_FmtFloat.h"
#include "LogManager.h"

void DisplayManager::handleTouch( ) {
 /* Use _rawTouchState (already OR'd with sim flag in
 * loopCore1) so simulated touches register as "touched". When sim is
 * active, _driver.ts->getPoint() may return zeros, but mapTouchPoint() now
 * bypasses the ADC mapping and returns _simTouchX/Y directly. */
 if (!_rawTouchState) {
 /* Finger released — enables next single touch */
 _touchReleased = true;

 /* Short tap on top panel (release before 1s): toggle min/max */
 if (_topPanel.holdStart != 0 && !_topPanel.holdFired && _lastTouchRegion == 0) {
 if (!_topPanel.fixed) {
 /* Interactive mode: short tap exits to fixed */
 _topPanel.fixed = true;
 _topPanel.fixedIdx = _sharedState.selectedSlotIdx;
 } else {
 /* Fixed mode: toggle min/max */
 _topPanel.showMinMax = !_topPanel.showMinMax;
 }
 redrawTopPanel( );
 }
 
 _topPanel.holdStart = 0; _topPanel.holdFired = false;
 

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

 if (_calSwapXY) {
 float spanX = rawRight_Y - rawLeft_Y;
 _calXMin = (int16_t)(rawLeft_Y - 20.0f * spanX / 280.0f);
 _calXMax = (int16_t)(rawRight_Y + 20.0f * spanX / 280.0f);
 float spanY = rawBot_X - rawTop_X;
 _calYMin = (int16_t)(rawTop_X - 20.0f * spanY / 200.0f);
 _calYMax = (int16_t)(rawBot_X + 20.0f * spanY / 200.0f);
 } else {
 float spanX = rawRight_X - rawLeft_X;
 _calXMin = (int16_t)(rawLeft_X - 20.0f * spanX / 280.0f);
 _calXMax = (int16_t)(rawRight_X + 20.0f * spanX / 280.0f);
 float spanY = rawBot_Y - rawTop_Y;
 _calYMin = (int16_t)(rawTop_Y - 20.0f * spanY / 200.0f);
 _calYMax = (int16_t)(rawBot_Y + 20.0f * spanY / 200.0f);
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
 if (_uiMode != MODE_DASHBOARD && !_sharedState.isBooting) {
 if (timeSince(_lastTouchTime, 30000)) forceDashboard( );
 }
 return;
 }


 if (!timeSince(_lastTouchTime, 15)) return;
 TS_Point p = _driver.ts->getPoint( );

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

 /* CANCEL button: accepts touch at any pressure */
 if (p.z >= 50) {
 int16_t sx, sy;
 mapTouchPoint(p, sx, sy);
 if (sy > 195 && sx < 125) {
 if (acceptTouch(0)) {
 if (_sharedState.isBooting) {
 _calXMin = 300; _calXMax = 3800;
 _calYMin = 200; _calYMax = 3700;
 _sensZThreshold = 400;
 _calValid = true;
 _calSwapXY = false;
 _uiMode = MODE_DASHBOARD;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 } else {
 showSettingsMain( );
 }
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

 if (p.z < _sensZThreshold) return;


 if (_uiMode == MODE_SETTINGS_TOUCH_CAL) {
 _lastTouchTime = millis( );


 if (_calPhase >= 1) {
 int16_t screenX, screenY;
 mapTouchPoint(p, screenX, screenY);
 if (screenY >= 185) {
 if (_calPhase == 2) {

 if (_sharedState.isBooting) {
 _uiMode = MODE_DASHBOARD;
 _isDirty = true;
 _forceFullRedraw = true;
 } else {
 showSettingsMain( );
 }
 } else {

 _calStep = 0;
 _calPhase = 0;
 memset(_calRawX, 0, sizeof(_calRawX));
 memset(_calRawY, 0, sizeof(_calRawY));
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 }
 return;
 }


 if (_calStep < 8) {
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
 if (_sharedState.showSkipButton) {
 if (y > 190 && x > 80 && x < 240) _skipPressed = true;
 }
 return;
 }


 _lastTouchTime = millis( );


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

 if (_uiMode == MODE_DASHBOARD) {
 if (y > 35 && y < 110) {
 bool firstTouch = acceptTouch(0);

 /* Mode indicator tap [Amb]/[Sx] (right corner, x > 280): immediate toggle */
 if (firstTouch && x > 280) {
 _topPanel.fixed = !_topPanel.fixed;
 if (_topPanel.fixed)
 _topPanel.fixedIdx = _sharedState.selectedSlotIdx;
 else
 _topPanel.showMinMax = false;
 redrawTopPanel( );
 return;
 }

 /* Right corner: graph button (priority over alarm) — touch-down immediate */
 if (_topPanel.showMinMax && x > 266 && firstTouch) {
 _topPanel.showMinMax = false;
 /* topSlotIdx, not -1. The hardcoded -1 was the sentinel from when this panel
  * was always the ambient sensor; it now follows _topPanel.fixedIdx or mirrors
  * the selection, and pullSnapshot keeps topSlotIdx in step with both.
  *
  * -1 did not crash — renderGraphOptimized bounds-checks it — it just left
  * pkg.hwId empty, so no record in any history file matched and the graph came
  * up "No Data". Worse, an hwId that matches nothing never fills pkg.count, so
  * the V4 read loop ran to the end of every file and rebooted the device. The
  * guard mirrors the bottom panel's, which had it all along. */
 int topIdx = _sharedState.topSlotIdx;
 if (topIdx >= 0 && topIdx <= 10) {
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = topIdx; ev.param = 0;
 pushUiEvent(ev);
 }
 return;
 }

 /* Alarm action on touch-down — check slot mask for any alarming slot */
 if (firstTouch && _alarmSlotMask != 0) {
 /* Navigate to first alarming slot */
 for (int8_t s = 0; s < MAX_SENSORS; s++) {
 if (_alarmSlotMask & (1 << s)) { showAlarmAction(s); break; }
 }
 return;
 }

 if (firstTouch) {
 /* Start hold tracking — action deferred to release or 1s timeout */
 _topPanel.holdStart = millis();
 _topPanel.holdFired = false;
 return;
 }

 /* Holding: long-press (1s) toggles fixed ↔ interactive */
 if (!_topPanel.holdFired && _lastTouchRegion == 0 &&
 _topPanel.holdStart != 0 &&
 millis() - _topPanel.holdStart >= 1000) {
 _topPanel.holdFired = true;
 _touchSoundPending = true;
 _topPanel.fixed = !_topPanel.fixed;
 if (_topPanel.fixed)
 _topPanel.fixedIdx = _sharedState.selectedSlotIdx;
 redrawTopPanel( );
 }
 return;
 }
 if (y > 115 && y < 190) {
 if (!acceptTouch(1)) return;
 int sensorIdToGraph = -1;
 if (_sharedState.selectedSlotIdx >= 0 && _sharedState.selectedSlotIdx <= 10)
 sensorIdToGraph = _sharedState.selectedSlotIdx;

 /* Right corner: graph button (priority over alarm) */
 if (_bottomPanel.showMinMax && x > 266) {
 _bottomPanel.showMinMax = false;
 if (sensorIdToGraph != -1) {
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = sensorIdToGraph; ev.param = 0;
 pushUiEvent(ev);
 }
 return;
 }

 if (sensorIdToGraph >= 0 && isSlotAlarming(sensorIdToGraph)) {
 showAlarmAction((int8_t)sensorIdToGraph);
 return;
 }

 /* Toggle min/max */
 _bottomPanel.showMinMax = !_bottomPanel.showMinMax;
 {
 SystemState snap;
 mutex_enter_blocking(&_stateMutex);
 snap = _sharedState;
 mutex_exit(&_stateMutex);
 drawSlotPanel(snap.slotTemp, snap.slotHum, snap.slotType, snap.slotValid,
 snap.selectedSlotIdx, snap.slotName, true, _bottomPanel);
 }
 return;
 }
 if (y > 195) {
 const int btnW = 58, gap = 5, pitch = btnW + gap;
 int btnIdx = (x - 5) / pitch;
 DashBtn btns[5];
 int totalPages = 1; bool paging = false;
 (void)buildDashLayout(btns, &totalPages, &paging);
 if (btnIdx < 0 || btnIdx > 4) return;
 const DashBtn &b = btns[btnIdx];
 if (b.kind < 0) return; /* touch on empty position (gap) — ignore */
 if (b.kind == 2) { /* PAGE */
 if (!acceptTouch(14)) return;
 _currentPage++;
 if (_currentPage >= totalPages) _currentPage = 0;
 drawBottomButtons(_sharedState.selectedSlotIdx); return;
 }
 if (b.kind == 1) { /* CFG */
 if (!acceptSlideTouch(20)) return;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_SETTINGS;
 pushUiEvent(ev); return;
 }
 /* SLOT */
 if (!acceptSlideTouch(10 + b.slotId)) return;
 _bottomPanel.showMinMax = false;
 if (!_topPanel.fixed) _topPanel.showMinMax = false;
 drawBottomButtons(b.slotId);
 UiEvent ev; ev.type = UiEvent::EVT_SLOT_SELECT; ev.id = b.slotId;
 pushUiEvent(ev);
 }
 }
 else if (_uiMode == MODE_STATS_VIEW) {
 if (y < 40 && x > 270) { if (!acceptTouch(0)) return; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
 if (y > 170) {
 if (!acceptTouch(1)) return;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = 3;
 pushUiEvent(ev); return;
 }
 }
 else if (_uiMode == MODE_GRAPH_VIEW) {
 /* X button (close) — top right corner */
 if (y < 40 && x > 284) { if (!acceptTouch(0)) return; _graphNavOffset = 0; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
 /* Tap on header — show sensor name for 3s */
 if (y < 28 && x < 284) {
 if (!acceptTouch(0)) return;
 _headerShowName = true;
 _headerNameTimer = millis( );
 drawGraphHeaderBar( );
 return;
 }
 /* ── Bottom bar: [◀Past][▶Fut][📅Cal][🔍+ZoomIn][🔍-ZoomOut] ── */
 if (y >= 195) {
 const int btnW = 60, gap = 4, startX = 2;
 int btn = -1;
 for (int i = 0; i < 5; i++) {
 int bx = startX + i * (btnW + gap);
 if (x >= bx && x <= bx + btnW) { btn = i; break; }
 }

 if (btn == 0) {
 /* Past (◀) */
 if (!acceptHoldTouch(10)) return;
 UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = -1;
 pushUiEvent(ev); return;
 }
 if (btn == 1 && _graphNavOffset < 0) {
 /* Future (▶) — only if offset < 0 */
 if (!acceptHoldTouch(11)) return;
 UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = +1;
 pushUiEvent(ev); return;
 }
 if (btn == 2) {
 /* Calendar (📅) */
 if (!acceptTouch(0)) return;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_CALENDAR; ev.id = _graphData.sensorIdx; ev.param = 0;
 pushUiEvent(ev); return;
 }
 if (btn == 3 && _graphData.timeRange > 0) {
 /* Zoom In — shorter range (more detail) */
 if (!acceptHoldTouch(12)) return;
 int newRange = _graphData.timeRange - 1;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
 pushUiEvent(ev); return;
 }
 if (btn == 4 && _graphData.timeRange < 4) {
 /* Zoom Out — longer range (less detail) */
 if (!acceptHoldTouch(13)) return;
 int newRange = _graphData.timeRange + 1;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
 pushUiEvent(ev); return;
 }
 }
 /* Tap on central zone -> temperature details (page 0) */
 if (y >= 40 && y < 195) {
 if (!acceptTouch(10)) return;
 _detailPage = 0;
 _uiMode = MODE_GRAPH_DETAIL;
 _repaintGraph = true;
 }
 }
 else if (_uiMode == MODE_GRAPH_DETAIL) {
 /* X button — close to dashboard */
 if (y < 40 && x > 284) { if (!acceptTouch(0)) return; _graphNavOffset = 0; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
 /* Tap on header — show sensor name for 3s */
 if (y < 28 && x < 284) {
 if (!acceptTouch(0)) return;
 _headerShowName = true;
 _headerNameTimer = millis( );
 drawGraphHeaderBar( );
 return;
 }
 /* Bottom bar — same logic as graph view */
 if (y >= 195) {
 const int btnW = 60, gap = 4, startX = 2;
 int btn = -1;
 for (int i = 0; i < 5; i++) {
 int bx = startX + i * (btnW + gap);
 if (x >= bx && x <= bx + btnW) { btn = i; break; }
 }

 if (btn == 0) {
 /* Past (◀) */
 if (!acceptHoldTouch(10)) return;
 UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = -1;
 pushUiEvent(ev); return;
 }
 if (btn == 1 && _graphNavOffset < 0) {
 /* Future (▶) — only if offset < 0 */
 if (!acceptHoldTouch(11)) return;
 UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = +1;
 pushUiEvent(ev); return;
 }
 if (btn == 2) {
 /* Calendar (📅) */
 if (!acceptTouch(0)) return;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_CALENDAR; ev.id = _graphData.sensorIdx; ev.param = 0;
 pushUiEvent(ev); return;
 }
 if (btn == 3 && _graphData.timeRange > 0) {
 /* Zoom In — shorter range (more detail) */
 if (!acceptHoldTouch(12)) return;
 int newRange = _graphData.timeRange - 1;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
 pushUiEvent(ev); return;
 }
 if (btn == 4 && _graphData.timeRange < 4) {
 /* Zoom Out — longer range (less detail) */
 if (!acceptHoldTouch(13)) return;
 int newRange = _graphData.timeRange + 1;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
 pushUiEvent(ev); return;
 }
 }
 /* Tap on central zone -> next page or return to graph */
 if (y >= 40 && y < 195) {
 if (!acceptTouch(10)) return;
 bool hasHumNow = _graphData.hasHumidity && !isnan(_currentMinHum);
 if (_detailPage == 0 && hasHumNow) {
 /* Temperature -> Humidity */
 _detailPage = 1;
 _repaintGraph = true;
 } else {
 /* Humidity (or temp without hum) -> return to graph */
 _detailPage = 0;
 _uiMode = MODE_GRAPH_VIEW;
 _repaintGraph = true;
 }
 }
 }
 /* ── CALENDAR ── */
 else if (_uiMode == MODE_CALENDAR) {
 /* X button (back to graph) — top right corner */
 if (y < 28 && x >= 270) {
 if (!acceptTouch(0)) return;
 _uiMode = MODE_GRAPH_VIEW;
 _repaintGraph = true;
 return;
 }
 /* ◀ Month arrow — header left */
 if (y < 28 && x < 30) {
 if (!acceptSlideTouch(20)) return;
 UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = -1;
 pushUiEvent(ev); return;
 }
 /* ▶ Month arrow — header right */
 if (y < 28 && x > 290) {
 if (!acceptSlideTouch(21)) return;
 UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = +1;
 pushUiEvent(ev); return;
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
 if (dayNum >= 1 && dayNum <= 31 && (_calDaysMask & (1UL << dayNum))) {
 if (!acceptTouch(0)) return;
 UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_DAY;
 ev.id = _graphData.sensorIdx;
 ev.param = dayNum;
 pushUiEvent(ev);
 }
 }
 }
 /* ── Bottom bar: [◀ Month] [Today] [Month ▶] ── */
 if (y >= 195) {
 if (x < 106) {
 /* ◀ Month */
 if (!acceptSlideTouch(20)) return;
 UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = -1;
 pushUiEvent(ev);
 } else if (x >= 108 && x < 212) {
 /* Today — returns to graph with offset 0 */
 if (!acceptTouch(0)) return;
 _graphNavOffset = 0;
 UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = _graphData.timeRange;
 pushUiEvent(ev);
 } else if (x >= 217) {
 /* Month ▶ */
 if (!acceptSlideTouch(21)) return;
 UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = +1;
 pushUiEvent(ev);
 }
 }
 }
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
 UiEvent ev; ev.type = UiEvent::EVT_APPLY_THEME; ev.id = _previewThemeIdx; pushUiEvent(ev);
 }
 }
 }
 else if (_uiMode == MODE_SETTINGS_ALARMS) {
 if (y >= 40 && y <= 185) {
 int clickedIndex = 0;
 if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
 int mapIdx = (_alarmPage * 4) + clickedIndex;
 if (mapIdx < _activeSensorCount) {
 /*
 * ON/OFF touch zone: right side of the item.
 * Items rendered at x=10..295, ON/OFF stays in the ~60 final px.
 * Toggle zone: x >= 230 (screen).
 */
 bool touchOnStatus = (x >= 230);

 if (touchOnStatus && mapIdx == _alarmSelection) {
 /* Tap on ON/OFF of the selected item: toggle or edit */
 if (!acceptTouch(clickedIndex + 4)) return;
 int actualSensorId = _activeSensorsMap[_alarmSelection];
 SensorRecord* rec = &_sysConfigPtr->sensors[actualSensorId];

 if (rec->alarmsActive) {
 /* ON -> OFF: deactivate and save immediately */
 rec->alarmsActive = false;
 UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
 ev.id = actualSensorId;
 pushUiEvent(ev);
 _repaintSettings = true;
 } else {
 /* OFF -> enter limit editing screen */
 showAlarmEdit(actualSensorId);
 }
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
 bool hasHum = sensorHasHumidity((SensorType)_tempAlarmConfig.sensorType);


 if (y >= 50 && y <= 115) {
 uint8_t zone = (x < 160) ? 0 : 1;
 if (!acceptTouch(zone)) return;
 _editFieldFocus = zone;
 _repaintSettings = true;
 }

 else if (hasHum && y >= 125 && y <= 170) {
 uint8_t zone = (x < 160) ? 2 : 3;
 if (!acceptTouch(zone)) return;
 _editFieldFocus = zone;
 _repaintSettings = true;
 }

 else if (y >= 190) {
 auto adjustVal = [](float val, float step, float minV, float maxV) -> float {
 val += step; val = round(val * 10.0f) / 10.0f;
 if (val < minV) val = minV;
 if (val > maxV) val = maxV;
 return val;
 };

 auto enforceInterlock = [&]( ) {
 if (_tempAlarmConfig.tempMin >= _tempAlarmConfig.tempMax) {
 if (_editFieldFocus == 0)
 _tempAlarmConfig.tempMax = round((_tempAlarmConfig.tempMin + 0.1f) * 10.0f) / 10.0f;
 else
 _tempAlarmConfig.tempMin = round((_tempAlarmConfig.tempMax - 0.1f) * 10.0f) / 10.0f;
 }
 if (hasHum && _tempAlarmConfig.humMin >= _tempAlarmConfig.humMax) {
 if (_editFieldFocus == 2)
 _tempAlarmConfig.humMax = round((_tempAlarmConfig.humMin + 0.1f) * 10.0f) / 10.0f;
 else
 _tempAlarmConfig.humMin = round((_tempAlarmConfig.humMax - 0.1f) * 10.0f) / 10.0f;
 }
 if (_tempAlarmConfig.tempMax > 150.0f) _tempAlarmConfig.tempMax = 150.0f;
 if (_tempAlarmConfig.tempMin < -50.0f) _tempAlarmConfig.tempMin = -50.0f;
 if (hasHum) {
 if (_tempAlarmConfig.humMax > 100.0f) _tempAlarmConfig.humMax = 100.0f;
 if (_tempAlarmConfig.humMin < 0.0f) _tempAlarmConfig.humMin = 0.0f;
 }
 };

 if (x < 70) {
 /* Decrement with hold-repeat (300ms) and acceleration */
 if (!acceptHoldTouch(10)) return;
 if (_lastPressedBtn != 0) { _btnHoldStartTime = millis( ); _lastPressedBtn = 0; }
 uint32_t holdTime = millis( ) - _btnHoldStartTime;
 float step = -0.1f; if (holdTime > 6000) step = -10.0f; else if (holdTime > 4000) step = -1.0f; else if (holdTime > 2000) step = -0.5f;
 if (_editFieldFocus == 0) _tempAlarmConfig.tempMin = adjustVal(_tempAlarmConfig.tempMin, step, -50.0f, 150.0f);
 if (_editFieldFocus == 1) _tempAlarmConfig.tempMax = adjustVal(_tempAlarmConfig.tempMax, step, -50.0f, 150.0f);
 if (_editFieldFocus == 2) _tempAlarmConfig.humMin = adjustVal(_tempAlarmConfig.humMin, step, 0.0f, 100.0f);
 if (_editFieldFocus == 3) _tempAlarmConfig.humMax = adjustVal(_tempAlarmConfig.humMax, step, 0.0f, 100.0f);
 enforceInterlock( );
 _repaintSettings = true;
 }
 else if (x < 138) {
 /* Increment with hold-repeat (300ms) and acceleration */
 if (!acceptHoldTouch(11)) return;
 if (_lastPressedBtn != 1) { _btnHoldStartTime = millis( ); _lastPressedBtn = 1; }
 uint32_t holdTime = millis( ) - _btnHoldStartTime;
 float step = 0.1f; if (holdTime > 6000) step = 10.0f; else if (holdTime > 4000) step = 1.0f; else if (holdTime > 2000) step = 0.5f;
 if (_editFieldFocus == 0) _tempAlarmConfig.tempMin = adjustVal(_tempAlarmConfig.tempMin, step, -50.0f, 150.0f);
 if (_editFieldFocus == 1) _tempAlarmConfig.tempMax = adjustVal(_tempAlarmConfig.tempMax, step, -50.0f, 150.0f);
 if (_editFieldFocus == 2) _tempAlarmConfig.humMin = adjustVal(_tempAlarmConfig.humMin, step, 0.0f, 100.0f);
 if (_editFieldFocus == 3) _tempAlarmConfig.humMax = adjustVal(_tempAlarmConfig.humMax, step, 0.0f, 100.0f);
 enforceInterlock( );
 _repaintSettings = true;
 }
 else if (x < 219) {
 /* BACK: deactivate alarm and save */
 if (!acceptTouch(12)) return;
 _lastPressedBtn = -1;
 _tempAlarmConfig.alarmsActive = false;
 _sysConfigPtr->sensors[_editSensorIdx] = _tempAlarmConfig;
 UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
 ev.id = _editSensorIdx; pushUiEvent(ev);
 showSettingsAlarms(_sysConfigPtr);
 }
 else {
 /* SAVE: activate alarm and save */
 if (!acceptTouch(13)) return;
 _lastPressedBtn = -1;
 _tempAlarmConfig.alarmsActive = true;
 _sysConfigPtr->sensors[_editSensorIdx] = _tempAlarmConfig;
 UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
 ev.id = _editSensorIdx; pushUiEvent(ev);
 showSettingsAlarms(_sysConfigPtr);
 }
 }
 }
 else if (_uiMode == MODE_AUTH) {
 if (y > 200 && x < 120) { if (!acceptTouch(0)) return; forceDashboard( ); return; }
 /* License button — accessible even during lockout */
 if (y > 200 && x > 195) { if (!acceptTouch(5)) return; _licenseFromAuth = true; showSettingsLicense( ); return; }
 if (_permanentLockout || !timeReached(_lockoutUntil)) return;
 if (y >= 80 && y <= 185) {
 int row = (y < 135) ? 0 : 1; int col = (x > 160) ? 1 : 0; int btnIdx = (row * 2) + col;
 if (!acceptTouch(1 + btnIdx)) return;
 /* T1.2: strchr on the fixed keypad table — the String wrapper was a
  * per-tap heap allocation on the Core-1 touch path. */
 const char* clickedChars = _keypadChars[btnIdx]; char expected = _expectedPin[_authStep];
 if (strchr(clickedChars, expected) == nullptr) _isCurrentAttemptValid = false;
 _authStep++; _authFailed = false;
 if ((size_t)_authStep >= _expectedPin.length( )) {
 if (_isCurrentAttemptValid) {
 _failedAttempts = 0; UiEvent ev; ev.type = UiEvent::EVT_AUTH_SUCCESS; pushUiEvent(ev); return;
 } else {
 _authFailed = true; _failedAttempts++; _authStep = 0; _isCurrentAttemptValid = true;
 _errorSoundPending = true;
 if (_failedAttempts <= 2) _lockoutUntil = 0;
 else if (_failedAttempts == 3) _lockoutUntil = millis( ) + 5000;
 else if (_failedAttempts == 4) _lockoutUntil = millis( ) + 15000;
 else if (_failedAttempts == 5) _lockoutUntil = millis( ) + 60000;
 else { _permanentLockout = true; _lockoutUntil = millis( ) + 10000; }
 _forceSettingsRedraw = true;
 }
 }
 scrambleKeys( ); _repaintSettings = true;
 }
 }
 else if (_uiMode == MODE_SETTINGS_MAIN) {
 if (y >= 40 && y <= 185) {
 int clickedIndex = 0;
 if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
 int mapIdx = (_mainMenuPage * 4) + clickedIndex;
 if (mapIdx < 9 && mapIdx != _menuSelection) {
 if (!acceptSlideTouch(clickedIndex)) return;
 _menuSelection = mapIdx; _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
 }
 }
 else if (y > 185) {
 if (x < 70) {
 if (!acceptHoldTouch(10)) return;
 if (_menuSelection > 0) _menuSelection--; else _menuSelection = 8;
 _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
 }
 else if (x < 138) {
 if (!acceptHoldTouch(11)) return;
 if (_menuSelection < 8) _menuSelection++; else _menuSelection = 0;
 _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
 }
 else if (x < 219) {
 if (!acceptTouch(12)) return;
 forceDashboard( );
 }
 else {
 if (!acceptTouch(13)) return;
 UiEvent ev; ev.type = UiEvent::EVT_MENU_SELECT; ev.id = _menuSelection; pushUiEvent(ev);
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
 if (_offsetPreviewY > -4) { _offsetPreviewY--; changed = true; }
 }
 else if (y >= 145 && y <= 185 && x >= 130 && x <= 190) {
 if (!acceptHoldTouch(21)) return;
 if (_offsetPreviewY < 4) { _offsetPreviewY++; changed = true; }
 }
 else if (y >= 100 && y <= 140 && x >= 80 && x <= 140) {
 if (!acceptHoldTouch(22)) return;
 if (_offsetPreviewX > -4) { _offsetPreviewX--; changed = true; }
 }
 else if (y >= 100 && y <= 140 && x >= 180 && x <= 240) {
 if (!acceptHoldTouch(23)) return;
 if (_offsetPreviewX < 4) { _offsetPreviewX++; changed = true; }
 }
 else if (y >= 108 && y <= 132 && x >= 148 && x <= 172) {
 if (!acceptTouch(24)) return;
 if (_offsetPreviewX != 0 || _offsetPreviewY != 0) {
 _offsetPreviewX = 0; _offsetPreviewY = 0; changed = true;
 }
 }
 else if (y >= 200 && x <= 130) {
 if (!acceptTouch(25)) return;
 /* Discard preview adjustment: restore saved offset before exiting. */
 _offsetPreviewX = _offsetSavedX;
 _offsetPreviewY = _offsetSavedY;
 if (_driver.tft) _driver.tft->setDisplayOffset(_offsetSavedX, _offsetSavedY);
 showSettingsMain( );
 return;
 }
 else if (y >= 200 && x >= 190) {
 if (!acceptTouch(26)) return;
 UiEvent ev;
 ev.type = UiEvent::EVT_APPLY_DISPLAY_OFFSET;
 ev.id = _offsetPreviewX;
 ev.param = _offsetPreviewY;
 pushUiEvent(ev);
 return;
 }

 if (changed) {
 if (_driver.tft) _driver.tft->setDisplayOffset(_offsetPreviewX, _offsetPreviewY);
 /* Full redraw: previous frame was drawn with a different offset,
 * old pixels remain outside the new area and need to be cleared. */
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 }
 else if (_uiMode == MODE_SETTINGS_LANG) {
 /* Limit interaction to slot 0 when no .lng loaded.
 * Dynamic bound avoids slot 1 "ghost". */
 int activeSlots = _activeLangLoaded ? LANG_COUNT : 1;
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
 UiEvent ev; ev.type = UiEvent::EVT_APPLY_LANG; ev.id = _previewLangIdx; pushUiEvent(ev);
 }
 }
 }


 else if (_uiMode == MODE_SETTINGS_PASSWORD) {


 if (_kbPhase >= 2) {
 if (y >= 185) {
 if (!acceptTouch(0)) return;
 if (_kbPhase == 3) {
 showSettingsMain( );
 } else {
 _kbPhase = 0;
 _kbCursor = 0;
 _kbShowRaw = false;
 memset(_kbBuffer, 0, sizeof(_kbBuffer));
 memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 }
 return;
 }

 char* activeBuf = (_kbPhase == 0) ? _kbBuffer : _kbConfirmBuf;


 if (y < 28 && x > 280) {
 if (!acceptTouch(1)) return;
 showSettingsMain( ); return;
 }


 if (y >= 33 && y < 66) {
 if (!acceptTouch(2)) return;
 _kbShowRaw = !_kbShowRaw;
 _repaintSettings = true;
 return;
 }


 if (y >= 72 && y < 168) {
 int row = (y - 72) / 32;
 int col = (x - 1) / 32;
 if (row < 0) row = 0;
 if (row > 2) row = 2;
 if (col < 0) col = 0;
 if (col > 9) col = 9;


 if (!acceptTouch((uint8_t)(row * 10 + col + 10))) return;

 /* Update visual selection cursor */
 _kbSelRow = row;
 _kbSelCol = col;

 static const char layer0[3][10] = {
 {'q','w','e','r','t','y','u','i','o','p'},
 {'a','s','d','f','g','h','j','k','l','.'},
 {'z','x','c','v','b','n','m',',','!','?'}
 };
 static const char layer1[3][10] = {
 {'Q','W','E','R','T','Y','U','I','O','P'},
 {'A','S','D','F','G','H','J','K','L',':'},
 {'Z','X','C','V','B','N','M',';','"','\''}
 };
 static const char layer2[3][10] = {
 {'1','2','3','4','5','6','7','8','9','0'},
 {'@','#','$','%','&','*','-','+','=','~'},
 {'(',')','[',']','{','}','/','\\','^','_'}
 };

 const char (*active)[10] = (_kbLayer == 2) ? layer2
 : (_kbLayer == 1) ? layer1
 : layer0;

 if (_kbCursor < 7) {
 activeBuf[_kbCursor++] = active[row][col];
 activeBuf[_kbCursor] = '\0';
 if (_kbLayer == 1 && !_kbShiftLock) _kbLayer = 0;
 }
 _repaintSettings = true;
 return;
 }


 if (y >= 170 && y < 195) {
 /* Positions: Shift=1..49, 123=51..99, Space=101..219, Bksp=221..269, OK=271..319 */
 if (x < 49) {
 /* Shift */
 if (!acceptTouch(50)) return;
 if (_kbLayer == 1) {
 _kbShiftLock = !_kbShiftLock;
 if (!_kbShiftLock) _kbLayer = 0;
 } else {
 _kbLayer = 1;
 _kbShiftLock = false;
 }
 _repaintSettings = true;
 }
 else if (x < 99) {
 /* 123 */
 if (!acceptTouch(51)) return;
 _kbLayer = (_kbLayer == 2) ? 0 : 2;
 _kbShiftLock = false;
 _repaintSettings = true;
 }
 else if (x < 219) {
 /* Space */
 if (!acceptTouch(52)) return;
 if (_kbCursor < 7) {
 activeBuf[_kbCursor++] = ' ';
 activeBuf[_kbCursor] = '\0';
 }
 _repaintSettings = true;
 }
 else if (x < 269) {
 /* Backspace */
 if (!acceptTouch(53)) return;
 if (_kbCursor > 0) {
 activeBuf[--_kbCursor] = '\0';
 }
 _repaintSettings = true;
 }
 else {
 /* OK — same confirmation logic */
 if (!acceptTouch(54)) return;
 if (_kbPhase == 0) {
 if (_kbCursor < 4) {
 _kbPhase = 2;
 _kbMsgKey = TR_PWD_TOO_SHORT;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 } else {
 _kbPhase = 1;
 _kbCursor = 0;
 _kbShowRaw = false;
 memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
 /* Partial redraw: title and boxes change, keys don't */
 _repaintSettings = true;
 }
 }
 else if (_kbPhase == 1) {
 if (_kbCursor < 4) {
 _kbPhase = 2;
 _kbMsgKey = TR_PWD_TOO_SHORT;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 else if (strcmp(_kbBuffer, _kbConfirmBuf) != 0) {
 _kbPhase = 2;
 _kbMsgKey = TR_PWD_MISMATCH;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 else {
 _kbPhase = 3;
 _kbMsgKey = TR_PWD_SAVED;
 _forceSettingsRedraw = true;
 _repaintSettings = true;

 UiEvent ev;
 ev.type = UiEvent::EVT_SAVE_PASSWORD;
 ev.id = 0;
 ev.param = 0;
 pushUiEvent(ev);
 }
 }
 }
 return;
 }


 if (y >= 195) {
 int btnW = 58; int bGap = 5; int bStartX = 5;
 int btnIdx = (x - bStartX) / (btnW + bGap);
 if (btnIdx < 0) btnIdx = 0;
 if (btnIdx > 4) btnIdx = 4;
 /* Check if touch is inside the button (not in the gap) */
 int btnX = bStartX + btnIdx * (btnW + bGap);
 if (x < btnX || x > btnX + btnW) return;

 /* Column limits: row 3 (bar) has 5 items, rows 0-2 have 10 */
 int maxCol = (_kbSelRow == 3) ? 4 : 9;

 if (btnIdx == 0) {
 /* ◄ Left */
 if (!acceptTouch(60)) return;
 _kbSelCol--;
 if (_kbSelCol < 0) _kbSelCol = maxCol;
 _repaintSettings = true;
 }
 else if (btnIdx == 1) {
 /* ► Right */
 if (!acceptTouch(61)) return;
 _kbSelCol++;
 if (_kbSelCol > maxCol) _kbSelCol = 0;
 _repaintSettings = true;
 }
 else if (btnIdx == 2) {
 /* ▲ Up */
 if (!acceptTouch(62)) return;
 _kbSelRow--;
 if (_kbSelRow < 0) _kbSelRow = 3;
 /* Adjust col when switching to/from the bar */
 if (_kbSelRow == 3 && _kbSelCol > 4) _kbSelCol = 4;
 _repaintSettings = true;
 }
 else if (btnIdx == 3) {
 /* ▼ Down */
 if (!acceptTouch(63)) return;
 _kbSelRow++;
 if (_kbSelRow > 3) _kbSelRow = 0;
 /* Adjust col when switching to/from the bar */
 if (_kbSelRow == 3 && _kbSelCol > 4) _kbSelCol = 4;
 _repaintSettings = true;
 }
 else if (btnIdx == 4) {
 /* ✓ Confirm selection */
 if (!acceptTouch(64)) return;

 if (_kbSelRow == 3) {
 /*
 * Action bar: execute the action of the selected item.
 * 0=Shift, 1=123, 2=Space, 3=Backspace, 4=OK
 */
 if (_kbSelCol == 0) {
 /* Shift */
 if (_kbLayer == 1) {
 _kbShiftLock = !_kbShiftLock;
 if (!_kbShiftLock) _kbLayer = 0;
 } else {
 _kbLayer = 1;
 _kbShiftLock = false;
 }
 }
 else if (_kbSelCol == 1) {
 /* 123 */
 _kbLayer = (_kbLayer == 2) ? 0 : 2;
 _kbShiftLock = false;
 }
 else if (_kbSelCol == 2) {
 /* Space */
 if (_kbCursor < 7) {
 activeBuf[_kbCursor++] = ' ';
 activeBuf[_kbCursor] = '\0';
 }
 }
 else if (_kbSelCol == 3) {
 /* Backspace */
 if (_kbCursor > 0) {
 activeBuf[--_kbCursor] = '\0';
 }
 }
 else if (_kbSelCol == 4) {
 /* OK — password confirmation */
 if (_kbPhase == 0) {
 if (_kbCursor < 4) {
 _kbPhase = 2;
 _kbMsgKey = TR_PWD_TOO_SHORT;
 _forceSettingsRedraw = true;
 } else {
 _kbPhase = 1;
 _kbCursor = 0;
 _kbShowRaw = false;
 memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
 }
 }
 else if (_kbPhase == 1) {
 if (_kbCursor < 4) {
 _kbPhase = 2;
 _kbMsgKey = TR_PWD_TOO_SHORT;
 _forceSettingsRedraw = true;
 }
 else if (strcmp(_kbBuffer, _kbConfirmBuf) != 0) {
 _kbPhase = 2;
 _kbMsgKey = TR_PWD_MISMATCH;
 _forceSettingsRedraw = true;
 }
 else {
 _kbPhase = 3;
 _kbMsgKey = TR_PWD_SAVED;
 _forceSettingsRedraw = true;
 UiEvent ev;
 ev.type = UiEvent::EVT_SAVE_PASSWORD;
 ev.id = 0; ev.param = 0;
 pushUiEvent(ev);
 }
 }
 }
 } else {
 /* Key row (0-2): insert the selected character */
 static const char lay0[3][10] = {
 {'q','w','e','r','t','y','u','i','o','p'},
 {'a','s','d','f','g','h','j','k','l','.'},
 {'z','x','c','v','b','n','m',',','!','?'}
 };
 static const char lay1[3][10] = {
 {'Q','W','E','R','T','Y','U','I','O','P'},
 {'A','S','D','F','G','H','J','K','L',':'},
 {'Z','X','C','V','B','N','M',';','"','\''}
 };
 static const char lay2[3][10] = {
 {'1','2','3','4','5','6','7','8','9','0'},
 {'@','#','$','%','&','*','-','+','=','~'},
 {'(',')','[',']','{','}','/','\\','^','_'}
 };
 const char (*sel)[10] = (_kbLayer == 2) ? lay2
 : (_kbLayer == 1) ? lay1
 : lay0;
 if (_kbCursor < 7) {
 activeBuf[_kbCursor++] = sel[_kbSelRow][_kbSelCol];
 activeBuf[_kbCursor] = '\0';
 if (_kbLayer == 1 && !_kbShiftLock) _kbLayer = 0;
 }
 }
 _repaintSettings = true;
 }
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

 _melSelectIdx = (uint8_t)mapIdx;
 SoundEvent evType = SND_NONE;
 switch (_melSelectType) {
 case 0: evType = SND_TOUCH_CLICK; break;
 case 1: evType = SND_CONFIRM; break;
 case 2: evType = SND_ERROR; break;
 case 3: evType = SND_ALARM_START; break;
 case 4: evType = SND_ATTENTION; break;
 }
 if (evType != SND_NONE) requestPreviewSound(evType, _melSelectIdx);
 _repaintSettings = true;
 }
 else if (y > 185) {
 if (x < 70) {
 if (!acceptTouch(0x90)) return;
 _melSelectIdx = (_melSelectIdx > 0) ? _melSelectIdx - 1 : TOTAL_VARIANTS - 1;
 SoundEvent evType = SND_NONE;
 switch (_melSelectType) {
 case 0: evType = SND_TOUCH_CLICK; break;
 case 1: evType = SND_CONFIRM; break;
 case 2: evType = SND_ERROR; break;
 case 3: evType = SND_ALARM_START; break;
 case 4: evType = SND_ATTENTION; break;
 }
 if (evType != SND_NONE) requestPreviewSound(evType, _melSelectIdx);
 _repaintSettings = true;
 }
 else if (x < 138) {
 if (!acceptTouch(0x91)) return;
 _melSelectIdx = (_melSelectIdx < TOTAL_VARIANTS - 1) ? _melSelectIdx + 1 : 0;
 SoundEvent evType = SND_NONE;
 switch (_melSelectType) {
 case 0: evType = SND_TOUCH_CLICK; break;
 case 1: evType = SND_CONFIRM; break;
 case 2: evType = SND_ERROR; break;
 case 3: evType = SND_ALARM_START; break;
 case 4: evType = SND_ATTENTION; break;
 }
 if (evType != SND_NONE) requestPreviewSound(evType, _melSelectIdx);
 _repaintSettings = true;
 }
 else if (x < 219) {
 if (!acceptTouch(0x92)) return;
 _inMelodySelect = false;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 else {
 if (!acceptTouch(0x93)) return;
 switch (_melSelectType) {
 case 0:
 _soundSettings.touchEnabled = true;
 _soundSettings.touchMelody = _melSelectIdx;
 break;
 case 1:
 _soundSettings.confirmEnabled = true;
 _soundSettings.confirmMelody = _melSelectIdx;
 break;
 case 2:
 _soundSettings.errorEnabled = true;
 _soundSettings.errorMelody = _melSelectIdx;
 break;
 case 3:
 _soundSettings.alarmEnabled = true;
 _soundSettings.alarmMelody = _melSelectIdx;
 break;
 case 4:
 _soundSettings.attentionEnabled = true;
 _soundSettings.attentionMelody = _melSelectIdx;
 break;
 }
 /* Parity with web /alarms: turning on any individual sound turns off global mute. */
 _soundSettings.muted = false;
 requestPreviewSound(SND_CONFIRM, _soundSettings.confirmMelody);

 _inMelodySelect = false;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
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
 if (x < 160) { if (_soundSettings.volume >= 10) _soundSettings.volume -= 10; }
 else { if (_soundSettings.volume <= 90) _soundSettings.volume += 10; }
 _touchSoundPending = false; /* cancel beep to avoid overlapping preview */
 requestVolumePreview(_soundSettings.volume);
 _repaintSettings = true;
 }
 else if (mapIdx == 1) {
 if (!acceptHoldTouch(21)) return;
 if (x < 160) { if (_soundSettings.alarmVolume >= 10) _soundSettings.alarmVolume -= 10; }
 else { if (_soundSettings.alarmVolume <= 90) _soundSettings.alarmVolume += 10; }
 _touchSoundPending = false;
 requestAlarmVolumePreview(_soundSettings.alarmVolume);
 _repaintSettings = true;
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
 _inMelodySelect = true;
 _melSelectType = melType;
 _melSelectIdx = curMel;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 }
 else if (mapIdx == 7) {
 if (!acceptSlideTouch(clickedIndex)) return;
 _soundSettings.webEnabled = !_soundSettings.webEnabled;
 /* Parity with web /alarms: turning on Web turns off global mute. */
 if (_soundSettings.webEnabled) _soundSettings.muted = false;
 _repaintSettings = true;
 }
 else if (mapIdx == 8) {
 if (!acceptSlideTouch(clickedIndex)) return;
 if (!_soundSettings.muted) {
 /* Will ENABLE Mute All -> confirmation screen. */
 showMuteConfirm( );
 } else {
 /* Disabling is direct, no confirmation. */
 _soundSettings.muted = false;
 _repaintSettings = true;
 }
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
 UiEvent ev; ev.type = UiEvent::EVT_SAVE_SOUNDS; ev.id = 0; ev.param = 0;
 pushUiEvent(ev);
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
 if (_licensePage > 0) _licensePage--;
 } else {
 if (!acceptTouch(1)) return;
 if (_licensePage < _licenseTotalPages - 1) _licensePage++;
 }
 _repaintSettings = true;
 }
 else if (y > 190) {
 if (x < 107) {
 if (!acceptHoldTouch(10)) return;
 if (_licensePage > 0) _licensePage--;
 _repaintSettings = true;
 }
 else if (x < 213) {
 if (!acceptHoldTouch(11)) return;
 if (_licensePage < _licenseTotalPages - 1) _licensePage++;
 _repaintSettings = true;
 }
 else {
 if (!acceptTouch(12)) return;
 if (_licenseFromAuth) {
 _licenseFromAuth = false;
 _uiMode = MODE_AUTH;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 /* License covered the keypad with fillScreen — force
 * repaint without resetting partial PIN or auth state. */
 requestAuthKeypadRedraw( );
 } else {
 showSettingsMain( );
 }
 }
 }
 }


 else if (_uiMode == MODE_ALARM_ACTION) {
 if (y >= 60 && y <= 105) {
 if (!acceptTouch(0)) return;
 UiEvent ev;
 ev.type = UiEvent::EVT_ALARM_SILENCE;
 ev.id = _alarmActionSlot;
 ev.param = 120;
 pushUiEvent(ev);
 }
 else if (y >= 115 && y <= 160) {
 if (!acceptTouch(1)) return;
 UiEvent ev;
 ev.type = UiEvent::EVT_ALARM_DEACTIVATE;
 ev.id = _alarmActionSlot;
 ev.param = 0;
 pushUiEvent(ev);
 }
 else if (y >= 170 && y <= 215) {
 if (!acceptTouch(2)) return;
 /* Return to dashboard with the panel in min/max mode */
 if (_alarmActionSlot < 0) {
 _topPanel.showMinMax = true;
 } else {
 _bottomPanel.showMinMax = true;
 }
 _uiMode = MODE_DASHBOARD;
 _forceFullRedraw = true;
 mutex_enter_blocking(&_stateMutex);
 _isDirty = true;
 mutex_exit(&_stateMutex);
 }
 }
 else if (_uiMode == MODE_CONFIRM_MUTE_ALL) {
 /* 2 buttons at y=190..230. Back (x=20..150), Confirm (x=170..300). */
 if (y >= 190 && y <= 230) {
 if (x >= 20 && x <= 150) {
 if (!acceptTouch(0xC0)) return;
 /* Back — cancel, return to Sounds menu without changes. */
 _uiMode = MODE_SETTINGS_SOUNDS;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 else if (x >= 170 && x <= 300) {
 /* Manual acceptance gate (no acceptTouch which would fire
 * SND_TOUCH_CLICK). We fire SND_CONFIRM before applying mute to
 * give "action confirmed" feedback — the sound reaches Core 0 and is
 * processed before save. _muted in SoundManager only changes on save. */
 if (!_touchReleased) return;
 _touchReleased = false;
 _lastTouchRegion = 0xC1;
 _lastRegionTouchTime = millis( );
 _lastTouchTimestamp = millis( );
 _touchSoundPending = false; /* no touch beep */
 requestPreviewSound(SND_CONFIRM, _soundSettings.confirmMelody);

 /* Confirm — applies mute=true and turns off all sounds (web parity). */
 _soundSettings.muted = true;
 _soundSettings.touchEnabled = false;
 _soundSettings.confirmEnabled = false;
 _soundSettings.errorEnabled = false;
 _soundSettings.alarmEnabled = false;
 _soundSettings.webEnabled = false;
 _soundSettings.attentionEnabled = false;
 _uiMode = MODE_SETTINGS_SOUNDS;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 }
 }
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
