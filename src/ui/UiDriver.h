/**
 * @file UiDriver.h
 * @brief The UI driver's share of DisplayManager.h that has to come BEFORE the class: the macro that declares
 *        a hook, the Ângulo's types, and the macro the old draw entry points hand a frame to the Ângulo with.
 *
 * An image has ONE UI driver (src/ui/UiCaps.h). Class DisplayManager is the same for every driver except for
 * the part that is the driver's, and four selection headers pick that part, each included at the one place
 * C++ lets its declarations stand:
 *   ui/UiDriver.h         before the class (this file)
 *   ui/UiDriverMembers.h  inside the class, as its last lines: the driver's members
 *   ui/UiDriverHooks.h    after the class, from DisplayManager.cpp and DisplayManager_Ota.cpp only: the
 *                         original's hook definitions, which need the complete class
 *   ui/UiDriverWeb.h      inside class WebManager: the driver's routes
 * Two would not do: the Ângulo's member types must be complete before the class that holds them, and the
 * original's hook bodies need the class complete. tools/check_ui_boundary.py lets these four, and only these,
 * include the Ângulo's headers: picking a driver is what they are for. docs/analysis/UI_DRIVERS.md has the
 * contract and the stages.
 */
#pragma once

#include <stdint.h>
#include "UiCaps.h"   /* SIMUT_UI_ANGULO, tested below: included here, not left to the includer */

#if SIMUT_UI_ANGULO
#include "angulo/UiA.h"   /* the Ângulo build's screens: UaQuadro, UaToque */
#endif

/* UI_HOOK declares a hook of the driver contract in the class. The original's hooks are the core's own code,
 * moved: always_inline, so a hook costs the call it replaced and the product images keep their bytes. The
 * Ângulo's are ordinary functions, defined in src/ui/angulo/. The macro reads the same in every translation
 * unit of an image, so a hook is inline everywhere or nowhere ([dcl.inline]/6). */
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
};

/* The Ângulo build (pico_wiretft_angulo, src/ui/angulo/) paints every screen
 * itself, from the state, at the end of each Core 1 pass. An old draw entry
 * point then only says that the frame may have changed and returns, and none
 * of the old drawing reaches the glass; the Ângulo's uiPass( ) runs the core's
 * timers and transitions that the old chain ran around the draw calls. Every
 * other image compiles UA_DESVIO( ) to nothing. */
#if SIMUT_UI_ANGULO
#define UA_DESVIO( ) do { uaSujo( ); return; } while ( 0 )
#else
#define UA_DESVIO( ) do { } while ( 0 )
#endif
