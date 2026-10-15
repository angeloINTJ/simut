/**
 * @file DisplayManager_Actions.h
 * @brief The core's touch actions of the Painel group (the dashboard and the alarm's action screen): what a touch
 *        on one of their zones does to the state, defined once, inline, so that the original's handleTouch( ) and a
 *        driver's own touch glue (the Ângulo's uaToquePainel( )) run the same code and not a copy (stage S5 of
 *        docs/analysis/UI_DRIVERS.md).
 *
 * Each action is declared in class DisplayManager as `inline __attribute__((always_inline))`, next to the core's
 * ticks, and called where its code used to sit, so the compiled image keeps the code it had. The zone decides which
 * action; the action holds the gates (acceptTouch( ) and its kin), the state it moves and the events it raises. What
 * the action draws it asks of the driver through the hooks (uiChanged( ), redrawTopPanel( )'s uiTopPanel( )), never
 * by name.
 *
 * Included LAST, after ui/UiDriverHooks.h, by DisplayManager_Touch.cpp and by the Ângulo's DisplayManager_UiA_Painel.cpp:
 * an action leans on the hook definitions the original inlines, so a unit that takes the actions takes the hooks first.
 */
#pragma once

#include "DisplayManager.h"

/* The actions follow, one commit each (S5): the dashboard's cards and buttons, the alarm screen's three buttons and
 * the boot screen's skip. */
