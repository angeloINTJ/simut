/**
 * @file    test/test_lan/standalone_settings_menu.cpp
 * @brief   display/SettingsMenu.h as the first project header of a unit.
 * @details The header tests SIMUT_NET_LAN (a wired build has no setup access
 *          point row) and has to include net/NetCaps.h itself: an undefined
 *          macro reads as 0, and the Wire would be given the radio's menu.
 *          test_main.cpp cannot tell whether it does. The first project header
 *          it includes, LangPackIndex.h, defines the macros for every header
 *          after it, so the loss shows only for a header that nothing precedes.
 *          Here nothing does: a SettingsMenu.h without its include leaves
 *          SIMUT_NET_LAN undefined, and this unit stops at the #error below.
 *          The native_lan environment builds the Wire (-DSIMUT_NET_ENC28J60=1),
 *          so the macro is 1 once the header has brought it in.
 *
 * @project SIMUT
 * @license MIT License
 */

#include "display/SettingsMenu.h"   /* first: nothing above this line may bring simut_config.h or NetCaps.h */

#ifndef SIMUT_NET_LAN
#error "display/SettingsMenu.h does not include net/NetCaps.h itself: SIMUT_NET_LAN is undefined after it, and a wired build reads it as 0 (the radio's menu)"
#endif
