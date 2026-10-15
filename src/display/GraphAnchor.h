/**
 * @file GraphAnchor.h
 * @brief Where the chart's window ends, which has no hardware in it: the place a run of the arrows' taps leaves it, the window a calendar day sets,
 *        and whether opening the chart from a screen keeps the window the last visit left. No Arduino: the host tests run it (test/test_validators)
 *        and Core 0 calls it (AppManager_Events.cpp, in the Ângulo's image only, under SIMUT_UI_ANGULO), so what the tests pin is the code the panel
 *        runs. The products keep their old lines: docs/analysis/UI_DRIVERS.md, "Product defects fixed for the Ângulo's bench image".
 */
#pragma once

#include <stdint.h>
#include <time.h>

/* Where the window stands: its end (0 is the live window, which the renderer ends at its own clock), the base it is measured back to (see
 * graphAnchorStep( )), and what the ▶ button is told: offset -1 is a window in the past (▶ enabled) and 0 is the present (▶ disabled). */
struct GraphAnchorPlace {
	time_t anchorEnd;
	time_t liveBase;
	int    offset;
};

/* One flush of the arrows' taps: `steps` periods of `step` seconds from the window that ends at `anchorEnd` (0: the live one), at the clock `now`.
 * `liveBase` goes with the anchor: the end of the live window at the moment the first tap left it, and 0 when the anchor did not come from the live
 * window (a calendar day sets the anchor itself). A window the taps have brought back to where they left the live window, or past it, IS the live
 * window again, however long they took: anchor 0, base 0, offset 0. So is a window that ends at or after the clock (a calendar window stepped
 * forward past it, which is also the old code's clamp against the future). Any other is the past and keeps anchor and base, with offset -1.
 * Core 0 used to anchor at the clock it read first and judge the result against a clock it read later: one period back and one forward, a few
 * seconds apart, left the window ending at the first tap's clock, marked as the past ("fim", ▶ still enabled), and a second ▶ reached the present
 * with the anchor frozen at that moment (host model of the old code, stages/hist_fix2/model_graphanchor.txt). The first version of this fix called
 * a window the present when it ended less than half a step before the clock, and that broke the calendar's alignment: its today on 24 h, at 10:00,
 * ends at the next midnight, and one step back is midnight, 10 h behind the clock and under the 12 h, so it came out as the live window instead
 * of yesterday (the review of 4122282, by a host probe). The base asks something else, whether the taps came back to where they started, so a
 * calendar window keeps its alignment (its base is 0), the wait between the taps does not matter, and neither does a period chosen in between:
 * three 24 h steps back and then 1 h steps take 71 forwards to stay in the past and the 72nd to be live. Where the old code kept a window in the
 * past, this keeps it the same, anchor and offset (the same model, 0 differences). */
inline GraphAnchorPlace graphAnchorStep( time_t anchorEnd, time_t liveBase, time_t now, int32_t steps, time_t step ) {
	if ( anchorEnd == 0 ) {
		anchorEnd = now;
		liveBase = now;
	}
	anchorEnd += (time_t)steps * step;
	if ( liveBase != 0 && anchorEnd >= liveBase ) return { 0, 0, 0 };
	if ( anchorEnd >= now ) return { 0, 0, 0 };
	return { anchorEnd, liveBase, -1 };
}

/* A day chosen in the calendar: the 24 h window of the day that begins at `dayStart` (a midnight) ends at the next one. That window did not come
 * from the live window, so the base goes: one left from an earlier trip back would call it the present after fewer steps than it takes to get there
 * (left the live window at 03:00, yesterday picked, one 6 h step forward: the base says live, the calendar says 06:00 today, in the past). */
inline void graphAnchorDay( time_t dayStart, time_t &anchorEnd, time_t &liveBase ) {
	anchorEnd = dayStart + 86400;
	liveBase = 0;
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

/* What an EVT_OPEN_GRAPH does to the pair: from another screen both go, since a base without its anchor means nothing; inside the chart both stay. */
inline void graphAnchorOpen( int uiMode, time_t &anchorEnd, time_t &liveBase ) {
	if ( graphOpenClearsAnchor( uiMode ) ) {
		anchorEnd = 0;
		liveBase = 0;
	}
}
