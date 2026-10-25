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
 * @brief Loads the display offset from the persistent config block and applies
 * it immediately to the TFT. Called at boot by AppManager.
 */
/* Called from Core 0 during boot, with Core 1 already rendering — so it sets
 * the offset WITHOUT painting the margins. Two cores writing the same SPI bus
 * is not a race worth taking to blacken 4 pixel columns that the next frame
 * covers on its own. */
void DisplayManager::loadDisplayOffset(const DisplayOffsetData* data) {
 if (!data || data->magic != 0xD0) {
 if (_driver.tft) _driver.tft->setDisplayOffset(0, 0, false);
 _offsetSavedX = 0;
 _offsetSavedY = 0;
 return;
 }
 int8_t ox = constrain((int)data->offsetX, -4, 4);
 int8_t oy = constrain((int)data->offsetY, -4, 4);
 if (_driver.tft) _driver.tft->setDisplayOffset(ox, oy, false);
 _offsetSavedX = ox;
 _offsetSavedY = oy;
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
 * @brief Enters the display offset adjustment screen; snapshots saved state for BACK.
 *
 * The "preview" state is initialized with the offset currently applied to the TFT
 * (which corresponds to the persisted value, loaded via loadDisplayOffset() at
 * boot). Any change via arrows is applied live to _driver.tft, and BACK restores
 * the snapshot if the user cancels.
 */
void DisplayManager::showSettingsDisplayOffset( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_DISPLAY_OFFSET;
 _offsetSavedX = _driver.tft ? _driver.tft->getOffsetX( ) : 0;
 _offsetSavedY = _driver.tft ? _driver.tft->getOffsetY( ) : 0;
 _offsetPreviewX = _offsetSavedX;
 _offsetPreviewY = _offsetSavedY;
 /* The original sets the sentinels that force the redraw of the numeric values (UiChange::DISPLAY_OFFSET_ENTER, UiOriginal.h). Raised inside the mutex: an answer must not take it. */
 uiChanged( UiChange::DISPLAY_OFFSET_ENTER, 0 );
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}
