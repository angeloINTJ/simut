/**
 * @file GraphNav.h
 * @brief What the graph screens' touch decides, which has no hardware in it: whether a day of the calendar has history. No Arduino: the
 *        host tests run it (test/test_validators) and both drivers call it (the original's handleTouch( ), DisplayManager_Touch.cpp, and the
 *        Ângulo's glue, DisplayManager_UiA_Graficos.cpp), so what the tests pin is the code the panel runs.
 */
#pragma once

#include <stdint.h>

/* A day of the calendar that has history: bit N of `mask` (DisplayManager::_calDaysMask) is day N, so bit 0 is not a day. Only 1..31 reach the
 * shift: on the RP2040 unsigned long is 32 bits wide and a shift by 32 or more is undefined, which the range test keeps out. It is the
 * original's own expression, the range first and the mask last, and always_inline keeps it part of the branch that calls it. On the host
 * unsigned long is 64 bits, so there a day past 31 reads a bit the 32-bit mask does not have, and the range test's upper bound cannot be
 * checked by a case. */
inline __attribute__((always_inline)) bool calDayHasHistory( uint32_t mask, int day ) {
	return day >= 1 && day <= 31 && ( mask & ( 1UL << day ) );
}

/* Not here: the detail screen's cycle of pages (temperature, humidity, pressure, back to the chart), which stays as three arms in handleTouch( ).
 * It was written as detailNextPage( page, hasHum, hasPress ) returning 1, 2 or -1 and called from that branch in two shapes (`if ( next < 0 )
 * back else page( next )`, and the three arms on 1, 2 and the rest): both changed the release and the wiretft images (sha256 8436d197... and
 * f1cbcb57... against ddaf10c1... for pico_w_release), where the day test above leaves them byte for byte the same, so the call was not
 * made. The Ângulo's pager steps over the pages it offers and has no use for it. docs/analysis/UI_DRIVERS.md section 2.10. */
