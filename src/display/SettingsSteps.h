/**
 * @file SettingsSteps.h
 * @brief What the settings screens' touches decide, which has no hardware in it: one step of a volume, one step of the display alignment's preview and how many language slots the screen offers. No Arduino: the host tests run it
 *        (test/test_validators) and the core's touch code calls it (DisplayManager_Actions.h, DisplayManager_Touch.cpp and the Ângulo's DisplayManager_UiA_Menus.cpp), so what the tests pin is the code the
 *        panel runs. Every function is always_inline, so it stays part of the branch that calls it.
 *        S8b (C9) adds limitStepMag( ), the ramp of a limit's arrow, which actLimitStep( ) (DisplayManager_Actions.h) calls for both drivers.
 */
#pragma once

#include <stdint.h>

/* The display alignment's limit: TftWithOffset::setDisplayOffset( ) holds the offset to [-4, +4] and the arrows stop at the same numbers, so the
 * preview never leaves that range; the Ângulo's alignment screen draws its scale from it as well. */
constexpr int8_t DISPLAY_OFFSET_LIMIT = 4;

/* One step of a volume, the system's or the alarm's (the sounds list's two volume rows): `down` takes 10 off and any other call puts 10 on, and a
 * volume that the step would take past 0 or past 100 stays where it is. The volume is taken by reference and the store stays conditional, as the
 * rows had it. The panel's own levels are multiples of 10; whether the web stores others was not checked, and a level that is not one is stepped
 * from where it stands, up to the ends. */
inline __attribute__((always_inline)) void volumeStep( uint8_t &v, bool down ) {
	if ( down ) { if ( v >= 10 ) v -= 10; }
	else { if ( v <= 90 ) v += 10; }
}

/* One step of the display alignment's preview along one axis: `dir` -1 takes one off and any other call puts one on, held inside
 * [-DISPLAY_OFFSET_LIMIT, +DISPLAY_OFFSET_LIMIT]; true when the preview moved. The four arrows call it with a constant `dir`, -1 or +1 and never 0,
 * so it compiles to the four branches it replaced. */
inline __attribute__((always_inline)) bool offsetStep( int8_t &v, int dir ) {
	if ( dir < 0 ) { if ( v > -DISPLAY_OFFSET_LIMIT ) { v--; return true; } }
	else { if ( v < DISPLAY_OFFSET_LIMIT ) { v++; return true; } }
	return false;
}

/* How many language slots the screen offers: English alone until a pack is loaded, then English and the pack, `langCount` of them. The caller passes
 * LANG_COUNT (SystemDefs_Records.h) because this header must stand alone: the native suite declares things that header declares as well, so it cannot
 * include it. The original's list takes the count as the bound of its rows and arrows, the Ângulo's as the bound of a tapped row. */
inline __attribute__((always_inline)) int langSlotCount( bool packLoaded, int langCount ) {
	return packLoaded ? langCount : 1;
}

/* The step of a limit's arrow from how long the same arrow has been held (the limits editor's ramp): 0.1, then 0.5 after 2 s, 1 after 4 s and 10 after 6 s. A threshold is exclusive: held for exactly
 * 2000, 4000 or 6000 ms, the arrow still steps by the arm below. actLimitStep( ) (DisplayManager_Actions.h) calls it, for the original's handleTouch( ) and the Ângulo's uaToqueAjustes( ). */
inline __attribute__((always_inline)) float limitStepMag( uint32_t heldMs ) {
	if ( heldMs > 6000 ) return 10.0f;
	if ( heldMs > 4000 ) return 1.0f;
	if ( heldMs > 2000 ) return 0.5f;
	return 0.1f;
}
