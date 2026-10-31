/**
 * @file DisplayManager_CalibrationCore.cpp
 * @brief The touch calibration and display offset state: the core share of DisplayManager_Calibration.cpp, moved word for word in S10a (docs/analysis/UI_DRIVERS.md); since S10c D3c also the two mode entries, showTouchCalibration( ) and showSettingsDisplayOffset( ), which draw nothing and say what happened through uiChanged( ).
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "UiWidgets.h"
#include <math.h>
#include "ui/UiDriverHooks.h"   /* uiChanged( ), which the two mode entries raise: the hook definitions need the complete class, so this comes last */

void DisplayManager::loadTouchCalibration(const TouchCalData* cal) {
 if (!cal || cal->magic != 0xCA) {
 _calValid = false;
 return;
 }
 _calSwapXY = (cal->flags & 0x01) != 0;
 _calXMin = cal->xMin;
 _calXMax = cal->xMax;
 _calYMin = cal->yMin;
 _calYMax = cal->yMax;
 _calValid = true;

 /* Sensitivity threshold: uses saved value, fallback 400 if zero */
 _sensZThreshold = (cal->zThreshold > 0) ? cal->zThreshold : 400;
}

void DisplayManager::fillCalData(TouchCalData* cal) const {
 if (!cal) return;
 cal->magic = 0xCA;
 cal->flags = _calSwapXY ? 0x01 : 0x00;
 cal->xMin = _calXMin;
 cal->xMax = _calXMax;
 cal->yMin = _calYMin;
 cal->yMax = _calYMax;
 cal->zThreshold = _sensZThreshold;
}

/* =========================================================================== */
/* DISPLAY OFFSET PERSISTENCE (public API) */
/* =========================================================================== */

/**
 * @brief Loads the display offset from the persistent config block: sets the RAM mirror of the
 * saved offset (_offsetSaved*) and applies it to the TFT. Called at boot by AppManager, and by the
 * Apply of the alignment screen (EVT_APPLY_DISPLAY_OFFSET), which has just persisted the new one.
 */
/* Called from Core 0 — at boot with Core 1 already rendering, and after the Apply — so it sets
 * the offset WITHOUT painting the margins. Two cores writing the same SPI bus
 * is not a race worth taking to blacken 4 pixel columns that the next frame
 * covers on its own (after the Apply the preview has blackened them already; at boot
 * tickDisplayOffset( ), on Core 1, paints them if it moves the glass first).
 *
 * The mirror is written BEFORE the glass (2026-10-31): tickDisplayOffset( ) takes the saved offset
 * for the target of every screen but the alignment's, so a Core 1 pass that saw the new glass with
 * the old saved value would put the old one back, in the Apply for a pass with the preview
 * already shown. Saved first, a pass in between sees the old glass and the new saved value and
 * puts the new one on, which is what this function is about to do. The release fence (a DMB on
 * both ISAs) keeps that order for the compiler, to which the two stores are independent, and for the
 * other core: the Pico 2 W's Cortex-M33 may reorder normal-memory accesses as the other core sees them. */
void DisplayManager::loadDisplayOffset(const DisplayOffsetData* data) {
 if (!data || data->magic != 0xD0) {
 _offsetSavedX = 0;
 _offsetSavedY = 0;
 __atomic_thread_fence(__ATOMIC_RELEASE);
 if (_driver.tft) _driver.tft->setDisplayOffset(0, 0, false);
 return;
 }
 int8_t ox = constrain((int)data->offsetX, -4, 4);
 int8_t oy = constrain((int)data->offsetY, -4, 4);
 _offsetSavedX = ox;
 _offsetSavedY = oy;
 __atomic_thread_fence(__ATOMIC_RELEASE);
 if (_driver.tft) _driver.tft->setDisplayOffset(ox, oy, false);
}

/**
 * @brief Fills the struct with the currently applied offset, for persistence.
 */
void DisplayManager::fillDisplayOffsetData(DisplayOffsetData* data) const {
 if (!data) return;
 data->magic = 0xD0;
 data->offsetX = _driver.tft ? _driver.tft->getOffsetX( ) : 0;
 data->offsetY = _driver.tft ? _driver.tft->getOffsetY( ) : 0;
 data->reserved = 0;
}

int8_t DisplayManager::getDisplayOffsetX( ) const {
 return _driver.tft ? _driver.tft->getOffsetX( ) : 0;
}

int8_t DisplayManager::getDisplayOffsetY( ) const {
 return _driver.tft ? _driver.tft->getOffsetY( ) : 0;
}

/**
 * @brief Resets the touch calibration to factory default values.
 *
 * Restores generic raw limits (200..3800) and invalidates the flag.
 * The next interaction will use estimated mapping until new calibration.
 */
void DisplayManager::resetTouchCalibration( ) {
 _calValid = false;
 _calSwapXY = false;
 _calXMin = 200;
 _calXMax = 3800;
 _calYMin = 200;
 _calYMax = 3800;
 _sensZThreshold = 400;
}

/**
 * @brief Starts the touch sensitivity calibration screen.
 * Resets counters and enters sample collection mode.
 */
void DisplayManager::showTouchSensitivity( ) {
 _sensCount = 0;
 _sensStability = 0.0f;
 _sensThreshold = 0;
 _sensDone = false;
 _sensDoneTime = 0;
 _uiMode = MODE_SETTINGS_TOUCH_SENS;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
}

void DisplayManager::mapTouchPoint(TS_Point raw, int16_t &outX, int16_t &outY) {
 /* Bypass mapping if simulated touch is active.
 * CLI 'touch sim'/'touch hold' sets _simTouchActive + _simTouchX/Y; here
 * we return coords already in screen-space, ignoring raw ADC.
 * The flag is not cleared here: loopCore1( ) ends the injected touch on its
 * own clock, once per pass, right after handleTouch( ) (_simTouchHoldMs: 100 ms
 * is a tap, longer is a hold). Clearing it in this function ended it only on
 * the screens that call it, and two never do for an injection (2026-10-16). */
 if (__atomic_load_n(&_simTouchActive, __ATOMIC_ACQUIRE)) {
 outX = __atomic_load_n(&_simTouchX, __ATOMIC_ACQUIRE);
 outY = __atomic_load_n(&_simTouchY, __ATOMIC_ACQUIRE);
 return;
 }

 int16_t rawForX, rawForY;

 if (_calSwapXY) {
 rawForX = raw.y;
 rawForY = raw.x;
 } else {
 rawForX = raw.x;
 rawForY = raw.y;
 }

 outX = (int16_t)constrain(map(rawForX, _calXMin, _calXMax, 0, DisplayDriver::W), 0, DisplayDriver::W - 1);
 outY = (int16_t)constrain(map(rawForY, _calYMin, _calYMax, 0, DisplayDriver::H), 0, DisplayDriver::H - 1);
}

void DisplayManager::showTouchCalibration( ) {
 /*
 * Integrated flow: sensitivity first, then position.
 * 1. MODE_SETTINGS_TOUCH_SENS — taps to calibrate pressure threshold
 * 2. MODE_SETTINGS_TOUCH_CAL — crosshairs to calibrate position
 * Transition 1→2 is automatic after sensitivity completion.
 */
 _sensCount = 0;
 _sensStability = 0.0f;
 _sensThreshold = 0;
 _sensDone = false;
 _sensDoneTime = 0;
 _calStep = 0;
 _calPhase = 0;
 memset(_calRawX, 0, sizeof(_calRawX));
 memset(_calRawY, 0, sizeof(_calRawY));

 /* The original's draw caches go back to "nothing painted" (resetTouchCalCaches( ), its answer to this kind). Nothing is drawn: the glass is Core 1's. */
 uiChanged( UiChange::TOUCH_CAL_ENTER, 0 );

 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_TOUCH_SENS;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

/**
 * @brief Enters the display offset adjustment screen.
 *
 * The "preview" state starts at the saved offset, the RAM mirror _offsetSaved* that loadDisplayOffset( )
 * sets at boot and after the Apply, and not at what is on the glass. Until 2026-10-31 this read the
 * glass ("which corresponds to the persisted value"), and that held only while every exit put the
 * saved offset back: the idle return, the CLI's `screen *`, a theme change and an update did not, and
 * `screen offset` over a preview made the preview the "saved" one (the walk of 2026-10-30 stopped on
 * this screen). Any change via arrows is applied live to _driver.tft; BACK puts the saved offset back at once and
 * tickDisplayOffset( ) does on every other exit. Core 0, under _stateMutex: the glass is Core 1's, so
 * this does not touch it.
 */
void DisplayManager::showSettingsDisplayOffset( ) {
 mutex_enter_blocking(&_stateMutex);
 /* The preview BEFORE the mode (2026-10-31): Core 1 reads _uiMode without the mutex, and tickDisplayOffset( ) takes the preview for the glass's target as soon as
  * the mode says this screen is shown. With the mode first, a pass between the two stores finds the screen shown and the preview of the LAST visit (an idle return,
  * `screen dash` or a theme change leaves it in the members), and puts that on the glass until the next pass. Written this way no pass sees the old preview. The release
  * fence keeps it so for the compiler and for the Pico 2 W's Cortex-M33, which may publish the mode ahead of the preview; its pair is the acquire fence in the tick, between
  * its read of the mode and its reads of the targets. */
 _offsetPreviewX = _offsetSavedX;
 _offsetPreviewY = _offsetSavedY;
 __atomic_thread_fence(__ATOMIC_RELEASE);
 _uiMode = MODE_SETTINGS_DISPLAY_OFFSET;
 /* The original sets the sentinels that force the redraw of the numeric values (UiChange::DISPLAY_OFFSET_ENTER, UiOriginal.h). Raised inside the mutex: an answer must not take it. */
 uiChanged( UiChange::DISPLAY_OFFSET_ENTER, 0 );
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

/**
 * @brief Core 1, once a pass: the glass shows the offset it is meant to show.
 *
 * The target is the preview while the alignment screen is shown and the saved offset (_offsetSaved*) otherwise.
 * Before 2026-10-31 only BACK (actOffsetBack( )) put the saved offset back, so the exits that skip it left the
 * preview on the glass: the 30 s idle return, the CLI's `screen *`, a theme change by the web or the CLI, an update.
 * Whoever leaves the screen now does nothing; this sees that it is not shown and puts the saved offset back, the way the
 * preview was put on (with the margins: Core 1 owns the SPI bus). It then asks for a full repaint of the screen that is up, and
 * of that one only: what is on the glass was drawn at the other offset, and each screen takes its own request. The original's
 * dashboard takes _isDirty and _forceFullRedraw (render( )), its chart _repaintGraph, its loading screen _repaintLoading, its calendar
 * _repaintCalendar, its update screen _otaRedrawAll (otaTick( )) and every other screen _repaintSettings with _forceSettingsRedraw
 * (uiPass( ) in ui/original/UiOriginal.h); the Angulo takes all of them in its uiPass( ) and also sees the alignment in its frame's
 * identity. A request nobody takes stays up, and with it repaintPending in loopCore1( ): on the dashboard the pass would wait 1 ms and
 * not 2 ms until a screen that takes it is shown. A mode that is not listed gets the settings pair: every other mode is a screen of the
 * settings tree, and tools/tft_ui/test_alinhar.py checks that each of them has a branch in uiPass( ) that tests _repaintSettings.
 * While a firmware update is staged Core 1 is parked from the first byte to the end, this does not run, and Core 0 paints the update screen on the glass as it is
 * (showOta( ) in DisplayManager.cpp): a preview left on the glass stays on that screen, shifted by up to 4 px, until the restart if the update succeeds and until
 * Core 1 resumes and this corrects it if it fails.
 * Never inside _stateMutex (it takes it) and never on Core 0 (the glass is Core 1's); loopCore1( ) calls it after handleTouch( ).
 *
 * The glass and the mode are read first and the targets after them, with an acquire fence between: it is the pair of two release fences. loadDisplayOffset( )
 * writes the saved offset before the glass, so a pass that has read the new glass reads the new saved offset too; showSettingsDisplayOffset( ) writes the preview
 * before the mode, so a pass that has read the mode "shown" reads the new preview. One fence serves both, because it orders every load above it before every access
 * below it; no pass puts an old value back.
 */
void DisplayManager::tickDisplayOffset( ) {
 if (!_driver.tft) return;
 const int8_t glassX = _driver.tft->getOffsetX( );
 const int8_t glassY = _driver.tft->getOffsetY( );
 const UiMode mode = _uiMode;
 __atomic_thread_fence(__ATOMIC_ACQUIRE);
 const bool shown = mode == MODE_SETTINGS_DISPLAY_OFFSET;
 const int8_t targetX = shown ? _offsetPreviewX : _offsetSavedX;
 const int8_t targetY = shown ? _offsetPreviewY : _offsetSavedY;
 if (glassX == targetX && glassY == targetY) return;
 _driver.tft->setDisplayOffset(targetX, targetY);
 switch (mode) {
 case MODE_DASHBOARD:
 mutex_enter_blocking(&_stateMutex);
 _isDirty = true;
 _forceFullRedraw = true;
 mutex_exit(&_stateMutex);
 break;
 case MODE_GRAPH_LOADING:
 _repaintLoading = true;
 break;
 case MODE_GRAPH_VIEW:
 case MODE_GRAPH_DETAIL:
 _repaintGraph = true;
 break;
 case MODE_CALENDAR:
 _repaintCalendar = true;
 break;
 case MODE_OTA_UPDATE:
 _otaRedrawAll = true;
 break;
 default:
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 break;
 }
}
