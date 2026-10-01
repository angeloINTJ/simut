/**
 * @file display/ClockEntry.h
 * @brief The date and time a person sets at the panel, field by field.
 *
 * Since 2026-10-01 a unit with no network configured asks for the date and
 * time when it boots, instead of opening its setup access point by itself (the
 * maintainer's decision: the access point opens from Settings, or from the `ap`
 * command). With no network there is no NTP, and without NTP or a person the
 * clock in force is the provisional one — extrapolated from the newest record
 * on flash, or from the build date on a unit that has none. While it is in
 * force the dashboard draws the time with a "?". The history cannot say so
 * afterwards: a sealed block carries no clock flag (H5_FLAG_CLOCK_SYNCED is
 * written only into the .wip snapshot, for the next boot's seed), so a record
 * stamped by the provisional clock reads like any other once it is sealed.
 *
 * Five fields, each with its own pair of buttons. The calendar's rules live
 * here, not in the screen, so a host test reaches them (test_validators):
 *  - the day stays inside its month: stepping the month or the year clamps it
 *    (31/03 back one month is 28/02, or 29/02), and the day itself wraps 1..N;
 *  - month, hour and minute wrap; the year stops at the range `conf time`
 *    accepts (SIMUT_CLOCK_YEAR_MIN..MAX in SimutTime.h).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#include <stdint.h>
#include <time.h>
#include "../SimutTime.h"

struct ClockEntry {
	int16_t year;
	int8_t  month, day, hour, minute;
};

/** Left to right as the screen shows them: dd / mm / yyyy  hh : mm. */
enum ClockEntryField : uint8_t { CE_DAY, CE_MONTH, CE_YEAR, CE_HOUR, CE_MINUTE, CE_FIELDS };

inline int clockEntryWrap(int v, int lo, int hi) {
	const int n = hi - lo + 1;
	v = (v - lo) % n;
	if (v < 0) v += n;
	return v + lo;
}

inline void clockEntryClampDay(ClockEntry& c) {
	const int dim = (int)simutDaysInMonth(c.year, (unsigned)c.month);
	if (c.day > dim) c.day = (int8_t)dim;
	if (c.day < 1) c.day = 1;
}

inline void clockEntryStep(ClockEntry& c, uint8_t field, int delta) {
	switch (field) {
	case CE_DAY:
		c.day = (int8_t)clockEntryWrap(c.day + delta, 1,
		                               (int)simutDaysInMonth(c.year, (unsigned)c.month));
		break;
	case CE_MONTH:
		c.month = (int8_t)clockEntryWrap(c.month + delta, 1, 12);
		clockEntryClampDay(c);
		break;
	case CE_YEAR: {
		int y = c.year + delta;
		if (y < SIMUT_CLOCK_YEAR_MIN) y = SIMUT_CLOCK_YEAR_MIN;
		if (y > SIMUT_CLOCK_YEAR_MAX) y = SIMUT_CLOCK_YEAR_MAX;
		c.year = (int16_t)y;
		clockEntryClampDay(c);
		break;
	}
	case CE_HOUR:   c.hour   = (int8_t)clockEntryWrap(c.hour + delta, 0, 23);   break;
	case CE_MINUTE: c.minute = (int8_t)clockEntryWrap(c.minute + delta, 0, 59); break;
	default: break;
	}
}

/** The fields of an instant at the device's fixed offset. A clock seeded from
 *  nothing (before the first settable year) starts on that year's 1 January. */
inline ClockEntry clockEntryFrom(time_t t, long offsetSec) {
	struct tm tmv;
	simutLocalTimeTz(t, &tmv, offsetSec);
	ClockEntry c;
	c.year = (int16_t)(tmv.tm_year + 1900);
	c.month = (int8_t)(tmv.tm_mon + 1);
	c.day = (int8_t)tmv.tm_mday;
	c.hour = (int8_t)tmv.tm_hour;
	c.minute = (int8_t)tmv.tm_min;
	if (c.year < SIMUT_CLOCK_YEAR_MIN || c.year > SIMUT_CLOCK_YEAR_MAX) {
		c.year = (int16_t)SIMUT_CLOCK_YEAR_MIN;
		c.month = 1; c.day = 1; c.hour = 0; c.minute = 0;
	}
	return c;
}

/** The instant the fields name, or 0 when they are not a date. Seconds are
 *  zero: SAVE is pressed at some second inside the minute that was typed.
 *  32-bit arithmetic on purpose: 2099-12-31 is 4 102 444 740, inside a uint32,
 *  and 64-bit multiplies cost the Cortex-M0+ library calls it otherwise has no
 *  use for here. Wrapping subtraction applies an offset of either sign. */
inline time_t clockEntryToEpoch(const ClockEntry& c, long offsetSec) {
	if (!simutValidCivil(c.year, c.month, c.day, c.hour, c.minute)) return 0;
	const uint32_t days = (uint32_t)simutDaysFromCivil(c.year, (unsigned)c.month,
	                                                   (unsigned)c.day);
	const uint32_t local = days * 86400u + (uint32_t)(c.hour * 3600 + c.minute * 60);
	return (time_t)(uint32_t)(local - (uint32_t)(int32_t)offsetSec);
}

/** The fields as the panel hands them to Core 0, in the two ints a UiEvent has
 *  (an epoch past 2038 does not fit in one): id = yyyymmdd, param = hhmm. */
inline void clockEntryPack(const ClockEntry& c, int& id, int& param) {
	id = (int)c.year * 10000 + (int)c.month * 100 + (int)c.day;
	param = (int)c.hour * 100 + (int)c.minute;
}

/** The other end, on Core 0: false for anything that is not a real date. */
inline bool clockEntryUnpack(int id, int param, ClockEntry& out) {
	if (id < 0 || param < 0 || param >= 10000) return false;
	/* Checked as ints, before the narrowing: 67562 would land on 2026 in an int16_t. */
	const int y = id / 10000, mo = (id / 100) % 100, d = id % 100;
	const int h = param / 100, mi = param % 100;
	if (!simutValidCivil(y, mo, d, h, mi)) return false;
	out.year = (int16_t)y; out.month = (int8_t)mo; out.day = (int8_t)d;
	out.hour = (int8_t)h; out.minute = (int8_t)mi;
	return true;
}

/** Whether the panel asks at boot: only when nothing else is going to set the
 *  clock — no network configured (so no NTP) and no real clock in force. */
inline bool clockEntryAtBoot(bool networkConfigured, bool clockTrusted) {
	return !networkConfigured && !clockTrusted;
}
