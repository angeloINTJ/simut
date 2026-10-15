/**
 * @file UiDriverHooks.h
 * @brief The UI driver's hook definitions that need the complete DisplayManager class. Included LAST, and only,
 *        by DisplayManager.cpp and DisplayManager_Ota.cpp, the translation units that call a hook: about fifty
 *        other units include DisplayManager.h and gain no header from this.
 *
 * The original's hooks are inline members, defined in ui/original/UiOriginal.h. They draw on the TFT, which the alpha
 * display has not, so the original's side is taken only where there is one: the alpha image includes this header and
 * gets nothing from it. The Ângulo includes nothing: its hooks are ordinary functions, defined in
 * src/ui/angulo/DisplayManager_UiA.cpp.
 */
#pragma once

#include "../simut_config.h"   /* SIMUT_DISPLAY_ALPHA, tested below: included here, not left to the includer */
#include "UiCaps.h"

#if !SIMUT_UI_ANGULO && !SIMUT_DISPLAY_ALPHA
#include "original/UiOriginal.h"
#endif
