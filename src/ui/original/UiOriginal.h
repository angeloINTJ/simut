/**
 * @file UiOriginal.h
 * @brief The original UI driver's hooks, the panel every release ships: their definitions, as inline members
 *        of DisplayManager. With UI_HOOK (src/ui/UiDriver.h) they are always_inline, so the code of a hook is
 *        the code it replaced. Included by ui/UiDriverHooks.h, last, from the two translation units that call
 *        a hook.
 *
 * No hook yet: this is the scaffolding of stage S4 (docs/analysis/UI_DRIVERS.md), and each hook arrives with
 * its own commit.
 */
#pragma once
