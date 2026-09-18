/**
 * @file ParseFloat.h
 * @brief Inline float parser — replaces atof()/String.toFloat() without libc.
 * @details atof() pulls _strtod_l (4KB) + _dtoa_r (4.5KB) from newlib.
 * This inline parser handles the subset of formats used by SIMUT
 * (decimal floats with up to 2 decimal places) with zero libc dependency.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <math.h>

/** Parses a decimal float string (e.g. "-12.5", "0.05", "+100", " 3").
 *  Returns NAN on empty input. Stops at the first character it cannot use and
 *  returns what it had, which is what atof( ) did for the callers that moved
 *  here — "3,\"hwId\":..." is a slot number followed by the rest of a JSON
 *  object, and it has to parse as 3.
 *  Does NOT handle scientific notation (1.5e3) — not used in SIMUT.
 *
 *  Leading blanks and a leading '+' are accepted since 2026-09-18, when the
 *  last two atof( ) callers moved here and took newlib's strtod out of the
 *  image with them (lever 5 of docs/analysis/DIETA_FLASH.md). Both were things
 *  atof( ) accepted and this did not: parseFloatStrict( ) admits "+100", and a
 *  hand-written JSON body may put a space after the colon. Dropping either on
 *  the floor would have been a silent 0. */
inline float parseFloat(const char* s) {
	if (!s || !*s) return NAN;
	while (*s == ' ' || *s == '\t') s++;
	if (!*s) return NAN;
	const bool neg = (*s == '-');
	if (neg || *s == '+') s++;
	float intPart = 0.0f;
	while (*s >= '0' && *s <= '9') {
		intPart = intPart * 10.0f + (float)(*s - '0');
		s++;
	}
	if (*s != '.') return neg ? -intPart : intPart;
	s++;
	float decPart = 0.0f, decDiv = 1.0f;
	while (*s >= '0' && *s <= '9') {
		decPart = decPart * 10.0f + (float)(*s - '0');
		decDiv *= 10.0f;
		s++;
	}
	float v = intPart + decPart / decDiv;
	return neg ? -v : v;
}
