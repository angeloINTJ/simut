/**
 * @file UiDriverHooks.h
 * @brief The UI driver's hook definitions that need the complete DisplayManager class, and the core's inline ticks
 *        (src/DisplayManager_Ticks.h), which render( ), drawAlarmEdit( ), drawTopBar( ) and the drivers' passes call.
 *        Included LAST, and only, by the translation units that call a hook or a tick, or that take the core's touch actions
 *        (DisplayManager.cpp, DisplayManager_CalibrationCore.cpp, DisplayManager_Dashboard.cpp, DisplayManager_Ota.cpp, DisplayManager_OtaCore.cpp, DisplayManager_Panel.cpp,
 *        DisplayManager_Render.cpp, DisplayManager_Settings.cpp, DisplayManager_SettingsCore.cpp, DisplayManager_Touch.cpp and DisplayManager_UsersUi.cpp): about fifty other units include DisplayManager.h and gain no
 *        header from this. A tick leans on the hook definitions (tickAlarmFlash( ) calls uiChanged( )), so a unit that takes the
 *        ticks takes this header, not DisplayManager_Ticks.h alone; so does a unit of the core or of the original that takes DisplayManager_Actions.h
 *        (DisplayManager_Touch.cpp and DisplayManager_UsersUi.cpp), whose redrawTopPanel( ) calls uiTopPanel( ). The Ângulo's glue
 *        units that take the actions (DisplayManager_UiA.cpp, DisplayManager_UiA_Painel.cpp, DisplayManager_UiA_Menus.cpp,
 *        DisplayManager_UiA_Graficos.cpp, DisplayManager_UiA_Ajustes.cpp and DisplayManager_UiA_Identidade.cpp) do not take this header: for them uiTopPanel( ) is an ordinary function, defined in
 *        DisplayManager_UiA.cpp.
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
