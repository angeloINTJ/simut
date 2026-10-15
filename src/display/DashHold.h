/**
 * @file DashHold.h
 * @brief The dashboard's two rules that have no hardware in them: the clock of the 3 s hold on the upper card, and the turn
 *        of the page button. No Arduino: the host tests run them (test/test_validators) and the firmware's touch actions call
 *        them (DisplayManager_Actions.h), so what the tests pin is the code the panel runs.
 */
#pragma once

#include <stdint.h>

/* One sample of a touch that goes on in the upper card's zone, after the first one (which starts the hold). `start` is when the
 * hold began (0: none is running), `seen` the last sample at which the finger was OBSERVED down, `fired` whether the hold has
 * fired, `region` the zone the press began in (0: the upper card). A gap longer than gapMs between two samples means the finger
 * was lifted or the loop was paused, and neither is evidence of a continuous press: the hold restarts at `now` rather than count
 * time nobody observed. Returns true when the hold fires on this sample: it has not fired, the press began in the upper card, a
 * hold is running and it has lasted holdMs. The caller marks `fired` and does what firing means. The elapsed time is a
 * subtraction, so the wrap of the millisecond counter does not matter. */
inline bool dashHoldStep( uint32_t now, uint32_t &start, uint32_t &seen, bool &fired, uint8_t region, uint32_t holdMs, uint32_t gapMs ) {
	if (start != 0 && now - seen > gapMs) {
		start = now;
		fired = false;
	}
	seen = now;
	return !fired && region == 0 && start != 0 && now - start >= holdMs;
}

/* The page button: the next page, and the first one after the last (or past it, when the layout shrank under the page). */
inline int dashNextPage( int page, int totalPages ) {
	int next = page + 1;
	return next >= totalPages ? 0 : next;
}
