/**
 * @file UiDriverHooks.h
 * @brief The UI driver's hook definitions that need the complete DisplayManager class, and the core's inline ticks
 *        (src/DisplayManager_Ticks.h), which render( ) and the drivers' passes call. Included LAST, and only, by
 *        DisplayManager.cpp and DisplayManager_Ota.cpp, the translation units that call a hook: about fifty other units
 *        include DisplayManager.h and gain no header from this.
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
