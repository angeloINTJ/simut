/**
 * @file UiDriverHooks.h
 * @brief The UI driver's hook definitions that need the complete DisplayManager class. Included LAST, and only,
 *        by DisplayManager.cpp and DisplayManager_Ota.cpp, the translation units that call a hook: about fifty
 *        other units include DisplayManager.h and gain no header from this.
 *
 * The original's hooks are inline members, defined in ui/original/UiOriginal.h. The Ângulo includes nothing:
 * its hooks are ordinary functions, defined in src/ui/angulo/DisplayManager_UiA.cpp.
 */
#pragma once

#include "UiCaps.h"

#if !SIMUT_UI_ANGULO
#include "original/UiOriginal.h"
#endif
