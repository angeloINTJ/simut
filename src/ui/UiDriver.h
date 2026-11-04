/**
 * @file UiDriver.h
 * @brief The UI driver's share of DisplayManager.h that has to come BEFORE the class: the macro that declares
 *        a hook, the Ângulo's types, and the enum of what the core tells the driver has changed.
 *
 * An image has ONE UI driver (src/ui/UiCaps.h). Class DisplayManager is the same for every driver except for
 * the part that is the driver's, and five selection headers pick that part, each included at the one place
 * C++ lets its text stand:
 *   ui/UiDriver.h         before the class (this file)
 *   ui/UiDriverMembers.h  inside the class, as its last lines: the driver's members
 *   ui/UiDriverHooks.h    after the class, last in each unit that calls a hook or a core tick (they need the
 *                         complete class): the original's hook definitions
 *   ui/UiDriverWeb.h      inside class WebManager: the driver's routes
 *   ui/UiDriverTouch.h    inside DisplayManager::handleTouch( ), from DisplayManager_Touch.cpp alone: the
 *                         original's mode chain (a fragment of that function, so that it keeps its bytes) or
 *                         the Ângulo's one call, uiTouch( x, y ) (S10b, C3)
 * One place would not do: the Ângulo's member types must be complete before the class that holds them, the
 * original's hook bodies need the class complete, and the touch chain has to stand inside the function it is
 * part of. tools/check_ui_boundary.py lets these five, and only these, include the Ângulo's headers: picking
 * a driver is what they are for. docs/analysis/UI_DRIVERS.md has the contract and the stages.
 */
#pragma once

#include <stdint.h>
#include "UiCaps.h"   /* SIMUT_UI_ANGULO, tested below: included here, not left to the includer */

#if SIMUT_UI_ANGULO
#include "angulo/UiA.h"   /* the Ângulo build's screens: UaQuadro, UaToque */
#endif

/* UI_HOOK declares a hook of the driver contract in the class. The original's hooks are the core's own code,
 * moved: always_inline, so a hook costs the call it replaced and the product images keep their bytes. The
 * Ângulo's are ordinary functions, defined in src/ui/angulo/, except one that answers a constant:
 * uiKeypadTargets( ) is defined inside the class (angulo/Members.h), inline by the language and not by this macro,
 * so that WebManager::handleApiKeypad( ), in another unit, sees the constant and the branch its image does not take
 * folds out (S12, F4; uaSujo( ) there is the other member defined in the class). The macro reads the same in every
 * translation unit of an image, so a hook is inline everywhere or nowhere ([dcl.inline]/6). */
#if SIMUT_UI_ANGULO
#define UI_HOOK
#else
#define UI_HOOK inline __attribute__((always_inline))
#endif

/* What the core tells the driver has changed, through the hook uiChanged( ). The core keeps the timers and the state;
 * the driver decides what the change looks like. */
enum class UiChange : uint8_t {
	ALARM_FLASH,     /**< the alarm flash changed phase: the cards in alarm take the other phase's colours */
	ALARM_RESTORE,   /**< the alarm ended: the normal cards come back */
	BOTTOM_PANEL,    /**< the bottom card's min/max face was toggled: it shows the other face */
	BOTTOM_BUTTONS,  /**< the row of buttons changed (a page turned, a slot was chosen): `arg` is the slot it highlights */
	GRAPH_HEADER,    /**< the graph's header was asked for the sensor's name (a tap on it): it shows the name for 3 s */
	GRAPH_BUSY,      /**< a request that takes a while went to Core 0 (a step in time, a period, the calendar, a day, Today): the screen says so until the data lands */
	TOUCH_CAL_ENTER, /**< the touch calibration was entered (showTouchCalibration( ), before it publishes the mode): the original sets the draw caches of its two calibration screens back to "nothing painted"; the Ângulo keeps none. The first kind raised on CORE 0, by the entry of a screen: the others come from the core's touch actions and ticks, on Core 1. A driver's answer must not draw, because the glass belongs to Core 1 */
	DISPLAY_OFFSET_ENTER, /**< the display offset screen was entered (showSettingsDisplayOffset( ), INSIDE _stateMutex, after it set the mode): the original sets the sentinels of its numeric redraw (_lastOffsetDrawX and _lastOffsetDrawY to 99); the Ângulo keeps none. Raised on CORE 0 like TOUCH_CAL_ENTER: an answer must not draw, because the glass belongs to Core 1, and must not take _stateMutex, because it is raised inside it */
};
