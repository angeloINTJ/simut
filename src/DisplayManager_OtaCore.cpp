/**
 * @file DisplayManager_OtaCore.cpp
 * @brief The OTA screen's Core 0 entry points and the reason-order static_assert: the core share of DisplayManager_Ota.cpp, moved word for word in S10a (docs/analysis/UI_DRIVERS.md).
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "FlashIrqProbe.h" /* C1_PHASE */
#include "UiWidgets.h"
#include "ui/UiDriverHooks.h"   /* the UI driver's hook definitions: last, they need the complete class */

static_assert(TR_OTA_WHY_DAMAGED - TR_OTA_WHY_MODEL == OTA_WHY_DAMAGED - OTA_WHY_MODEL,
              "one string per reason, in OtaWhy's order");

/* Core 0 paints only while Core 1 is held where it cannot be inside an SPI
 * transfer: parked at the top of its loop by a flash pause (the stage's, for
 * the whole upload), or reset by one — a pause's fallback when Core 1 never
 * parked. A pause that froze Core 1 elsewhere is neither, and then nothing is
 * drawn until Core 1 runs again. Core 0 is the only core that pauses, so a
 * pause it started has finished its handshake by the time it gets here. */
bool DisplayManager::otaPaintFromCore0( ) {
	if (!_driver.tft || !_driver.canvas) return false;
	if (__atomic_load_n(&_pauseRefCount, __ATOMIC_ACQUIRE) <= 0) return false;
	if (!__atomic_load_n(&_core1Parked, __ATOMIC_ACQUIRE) &&
	    !__atomic_load_n(&_core1HardReset, __ATOMIC_ACQUIRE)) return false;
	uiPaintOta( );
	return true;
}

/* Core 1, once a pass, before the screen dispatch. While a phase is published
 * the update screen has the panel, whatever screen Core 0 or a touch put up
 * meanwhile; the boot screen of a reboot outranks it. */
void DisplayManager::otaTick( ) {
	const uint32_t st = __atomic_load_n(&_otaState, __ATOMIC_ACQUIRE);
	const OtaPhase ph = otaPhaseOf(st);
	if (ph == OTA_PH_NONE) {
		if (_uiMode == MODE_OTA_UPDATE) forceDashboard( );
		return;
	}
	if (_sharedState.isBooting) return;
	if (_uiMode != MODE_OTA_UPDATE) {
		mutex_enter_blocking(&_stateMutex);
		_uiMode = MODE_OTA_UPDATE;
		mutex_exit(&_stateMutex);
		_otaRedrawAll = true;
	}
	const uint32_t hold = otaHoldMs(ph);
	if (hold && timeSince(__atomic_load_n(&_otaShownMs, __ATOMIC_ACQUIRE), hold)) {
		if (otaEnd(st)) forceDashboard( );
		return;
	}
	C1_PHASE(C1P_UI_SETTINGS);
	uiPaintOta( );
}
