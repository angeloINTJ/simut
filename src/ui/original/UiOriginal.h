/**
 * @file UiOriginal.h
 * @brief The original UI driver's hooks, the panel every release ships: their definitions, as inline members
 *        of DisplayManager. With UI_HOOK (src/ui/UiDriver.h) they are always_inline, so the code of a hook is
 *        the code it replaced. Included by ui/UiDriverHooks.h, last, from the two translation units that call
 *        a hook.
 *
 * Stage S4 of docs/analysis/UI_DRIVERS.md adds the hooks one commit at a time; each is defined below.
 */
#pragma once

#include "../../DisplayManager.h"

/* The update screen. The hook is the one call the two callers made, and not drawOtaScreen( )'s body: that body is
 * long and would be copied into both. */
UI_HOOK void DisplayManager::uiPaintOta( ) {
	drawOtaScreen( );
}
