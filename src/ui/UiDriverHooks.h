/**
 * @file UiDriverHooks.h
 * @brief The UI driver's hook definitions that need the complete DisplayManager class, and the core's inline ticks
 *        (src/DisplayManager_Ticks.h), which render( ), drawAlarmEdit( ), drawTopBar( ) and the drivers' passes call.
 *        Included LAST, and only, by the translation units that call a hook or a tick (DisplayManager.cpp,
 *        DisplayManager_Ota.cpp, DisplayManager_Settings.cpp and DisplayManager_Dashboard.cpp): about fifty other units
 *        include DisplayManager.h and gain no header from this. A tick leans on the hook definitions (tickAlarmFlash( )
 *        calls uiChanged( )), so a unit that takes the ticks takes this header, not DisplayManager_Ticks.h alone.
 *
 * Both need the TFT's members, which the alpha display's class does not declare (measured on 2026-10-15: the ticks fail
 * on _permanentLockout in pico_w_alpha), so the alpha image includes this header and gets nothing from it. The original's
 * hooks are inline members, defined in ui/original/UiOriginal.h. The Ângulo includes no hooks: its hooks are ordinary
 * functions, defined in src/ui/angulo/DisplayManager_UiA.cpp, which includes the ticks itself.
 */
#pragma once

#include "../simut_config.h"   /* SIMUT_DISPLAY_ALPHA, tested below: included here, not left to the includer */
#include "UiCaps.h"

#if !SIMUT_DISPLAY_ALPHA
#include "../DisplayManager_Ticks.h"
#if !SIMUT_UI_ANGULO
#include "original/UiOriginal.h"
#endif
#endif
