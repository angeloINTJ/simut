/**
 * @file SimutTime.h
 * @brief Local time as a fixed offset, without newlib's TZ machinery.
 *
 * @details SIMUT's timezone is an int8_t of whole hours in the config and
 * nothing else: no DST rule, no zone database, no half-hour zones. It was
 * being applied by writing a POSIX TZ string ("UTC3") into the environment
 * and calling tzset( ), which makes newlib parse that string with siscanf( )
 * and drags in the integer scanf engine, the TZ state machine and the
 * localtime/mktime pair that read it — 13,188 B of flash, measured 2026-09-18
 * (lever 4 of docs/analysis/DIETA_FLASH.md), to hold one number.
 *
 * The functions here take the offset as an argument, so the arithmetic can be
 * tested on the host against its own libc (test/test_validators). The libc
 * entry points the firmware actually links — localtime_r( ), localtime( ) and
 * mktime( ) — are thin wrappers in SimutTime.cpp over exactly these, reading
 * the one offset that simutTimeSetOffset( ) holds. Every one of the ~30 call
 * sites in src/ keeps calling the standard names and does not change.
 *
 * WHAT DOES NOT CHANGE: the order in which the zone is applied. The midnight
 * hole of 2026-08-16 (docs, and the comment in AppManager_Boot.cpp) was not
 * about how the zone was stored but about when it was set — the seed ran
 * before it. applyTimezone( ) is still called from the same three places, and
 * still has to run before anything turns a day name into a window.
 *
 * WHAT DOES CHANGE: nothing reads the TZ environment variable any more, so
 * setting it by hand has no effect; and mktime( ) no longer honours tm_isdst
 * (there is no DST to honour — it is normalised to 0, which is what a fixed
 * offset means).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>
#include <time.h>

/**
 * @brief Days from 1970-01-01 to a civil date, proleptic Gregorian.
 * @param y  full year (2026, not 126)
 * @param m  month 1..12
 * @param d  day of month; 0 and values past the month's end are fine — the
 *           result is simply the day that many days from the 1st, which is
 *           what makes mktime( ) normalisation fall out of the arithmetic.
 *
 * Howard Hinnant's days_from_civil, shifted to a March-based year so the leap
 * day lands at the end and no month table is needed. Exact for every date a
 * long can express, which on this target is far beyond the 2106 that a 32-bit
 * epoch stops at anyway.
 */
inline long simutDaysFromCivil(long y, unsigned m, unsigned d) {
	y -= (m <= 2);
	const long     era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);              /* 0..399   */
	const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
	const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy; /* 0..146096 */
	return era * 146097L + (long)doe - 719468L;
}

/**
 * @brief UTC epoch -> broken-down local time at a fixed offset.
 * @details gmtime_r( ) does the calendar work — it is 448 B, already linked
 * for the syslog timestamp, and correct. This only shifts the instant and
 * pins tm_isdst, because a fixed offset never has a daylight rule.
 */
inline struct tm* simutLocalTimeTz(time_t t, struct tm* out, long offsetSec) {
	const time_t shifted = (time_t)(t + (time_t)offsetSec);
	struct tm* r = gmtime_r(&shifted, out);
	if (r) r->tm_isdst = 0;
	return r;
}

/**
 * @brief Broken-down local time -> UTC epoch, normalising @p tmv in place.
 *
 * The normalisation is not a nicety here: two callers in this codebase ignore
 * the return value entirely and read the struct back. DisplayManager_Calendar
 * asks for {year, month, mday=1} and reads tm_wday to know where the month
 * starts, then for {month+1, mday=0} and reads tm_mday back to learn how many
 * days the month has; the history day walk does tm_mday += 1 and expects the
 * month and year to roll. All three fall out of computing the instant and
 * converting it back, which is what POSIX mktime( ) does and what this does.
 */
inline time_t simutMkTimeTz(struct tm* tmv, long offsetSec) {
	/* Month first, so tm_mon = 12 (the "next month" idiom) becomes January of
	 * the following year before it reaches the day arithmetic. Floor division:
	 * C truncates towards zero and a negative tm_mon would otherwise land a
	 * year late. */
	long mon  = (long)tmv->tm_mon;
	long yadj = mon / 12;
	long m    = mon % 12;
	if (m < 0) { m += 12; yadj -= 1; }

	const long year = (long)tmv->tm_year + 1900L + yadj;
	const long days = simutDaysFromCivil(year, (unsigned)(m + 1), 1)
	                + (long)tmv->tm_mday - 1L;

	const long long secs = (long long)days * 86400LL
	                     + (long long)tmv->tm_hour * 3600LL
	                     + (long long)tmv->tm_min  * 60LL
	                     + (long long)tmv->tm_sec;

	const time_t t = (time_t)(secs - (long long)offsetSec);
	simutLocalTimeTz(t, tmv, offsetSec);
	return t;
}

/**
 * @brief Set the offset localtime_r( )/localtime( )/mktime( ) will use.
 * @param hours whole hours east of UTC — the sign of the config field, not the
 *        inverted sign a POSIX TZ string uses.
 *
 * Called only from NetworkManager::applyTimezone( ). Defined in SimutTime.cpp,
 * which is where the libc entry points live too.
 */
void simutTimeSetOffset(int8_t hours);

/** @brief The offset in seconds, for anything that needs to reason about it. */
long simutTimeOffsetSeconds( );
