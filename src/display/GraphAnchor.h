/**
 * @file GraphAnchor.h
 * @brief Where the chart's window ends, which has no hardware in it: the place a run of the arrows' taps leaves it, and whether opening
 *        the chart from a screen keeps the window the last visit left. No Arduino: the host tests run it (test/test_validators) and Core 0
 *        calls it (AppManager_Events.cpp, in the Ângulo's image only, under SIMUT_UI_ANGULO), so what the tests pin is the code the panel
 *        runs. The products keep their old lines: docs/analysis/UI_DRIVERS.md, "Product defects fixed for the Ângulo's bench image".
 */
#pragma once

#include <stdint.h>
#include <time.h>

/* Where the window stands: its end, and what the ▶ button is told. anchorEnd 0 is the live window, which the renderer ends at its own clock;
 * offset -1 is a window in the past (▶ enabled) and 0 is the present (▶ disabled). */
struct GraphAnchorPlace {
	time_t anchorEnd;
	int    offset;
};

/* One flush of the arrows' taps: `steps` periods of `step` seconds from the window ending at `anchorEnd` (0: the live one), at the clock `now`.
 * A window that ends less than half a step before now IS the present, so it comes back as the live window, anchor 0 and offset 0; a window
 * that ends after now is less than half a step before it too (the distance is negative), so the old code's clamp against the future is part
 * of this rule, not a line of its own. Core 0 used to anchor at the clock it read first and judge the result against a clock it read later: one
 * period back and one forward, a few seconds apart, left the window ending at the first tap's clock, marked as the past ("fim", ▶ still
 * enabled); a second ▶ reached the present with the anchor frozen at that moment (host model of the old code, stages/hist_fix/model_graphanchor.txt).
 * Here the clock is read once, and the distance to it decides. Half a step is a strict bound: a window ending exactly half a step back is
 * the past. A window further back than that comes out as the old code gave it, anchor and offset (the same model, 0 differences). */
inline GraphAnchorPlace graphAnchorStep( time_t anchorEnd, time_t now, int32_t steps, time_t step ) {
	const time_t end = ( anchorEnd != 0 ? anchorEnd : now ) + (time_t)steps * step;
	if ( now - end < step / 2 ) return { 0, 0 };
	return { end, -1 };
}

/* The three values of UiMode (SystemDefs_Records.h) that ARE the chart, written out here because the host suite cannot include that header:
 * it carries Arduino.h, and the suite keeps its own copies of the helpers the header declares. A copied number is a number that goes
 * wrong later, so AppManager_Events.cpp pins each against the enum with a static_assert, in every image: a reordered enum stops the build
 * instead of moving the rule (the way TelContentType.h pins TelMode). */
constexpr int GRAPH_SCREEN_LOADING = 1;   /* MODE_GRAPH_LOADING: the chart's loading screen */
constexpr int GRAPH_SCREEN_VIEW    = 2;   /* MODE_GRAPH_VIEW: the chart */
constexpr int GRAPH_SCREEN_DETAIL  = 3;   /* MODE_GRAPH_DETAIL: the chart's numeric detail */

/* Whether an EVT_OPEN_GRAPH that arrives while the panel shows `uiMode` starts at the present. Asked from another screen (the dashboard's
 * chart buttons, the calendar's Today) it does, and the anchor a calendar day or an arrow left is dropped: nothing cleared it, so every later
 * opening showed that window as if it were now (on the bench, 62094 s behind the clock, image 9c3e0a25, 2026-10-14). A period chosen inside
 * the chart, or the way back from its detail, keeps the window. Any mode that is not one of the three is another screen, so a mode appended
 * to the enum later is one too. */
inline bool graphOpenClearsAnchor( int uiMode ) {
	return uiMode != GRAPH_SCREEN_LOADING && uiMode != GRAPH_SCREEN_VIEW && uiMode != GRAPH_SCREEN_DETAIL;
}
