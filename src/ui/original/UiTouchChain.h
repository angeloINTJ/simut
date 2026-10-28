/**
 * @file UiTouchChain.h
 * @brief The original's touch chain: what a touch does on each screen, one branch per _uiMode. Old lines 408-1225 of
 *        src/DisplayManager_Touch.cpp (50828bdf), word for word. Included INSIDE handleTouch( ) by src/ui/UiDriverTouch.h, after the early
 *        gates; x and y are its locals.
 *
 * Not self-contained and no include guard: it is a fragment of a function body.
 */
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
 actLimitStep(sType, nLimits, idx, zone, inc);
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
  * DisplayManager_UsersUi.cpp. */
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
 /* Limit interaction to the rows the screen lists: slot 0, and slot 1 when a
 * pack is loaded or only listed (display/LangChoice.h). Dynamic bound avoids
 * slot 1 "ghost". */
 int activeSlots = langRowsListed(isLangLoaded( ), isLangListed( ));
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
 actPwdAppend(activeBuf, popupChar(_kbPopup, hit));
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
 actPwdAppend(activeBuf, ' ');
 }
 }
 else {
 if (_kbCursor > 0) {
 actPwdBackspace(activeBuf);
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
