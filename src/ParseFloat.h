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

/** The most digits parseFloat( ) converts exactly — sign and point excluded,
 *  leading zeros included. 10^15 − 1 < 2^53, so every digit step of the
 *  accumulator below is an exact integer and 10^15 is an exact double; what is
 *  left is one IEEE divide and one cast, the same two roundings (float)strtod( )
 *  performs, so the result is bit-identical to it. At 16 digits neither holds
 *  (10^16 > 2^53), and the fuzz oracle proved it with a 27-digit input on
 *  2026-09-18. Counting leading zeros too is deliberate: it bounds the
 *  fractional scale by the same 10^15, one rule instead of two. */
constexpr int PARSE_FLOAT_EXACT_DIGITS = 15;

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

	/* The accumulator is a double even though the answer is a float, and that
	 * is the whole difference between this and a parser that is merely close.
	 * Every digit step is EXACT while the value fits 2^53, the single divide at
	 * the end is correctly rounded by IEEE, and the cast rounds once more — the
	 * same two roundings (float)strtod( ) performs. Accumulating in float
	 * instead rounds at every digit: "2147483647" came out one ulp off the
	 * nearest float, which is exactly what the fuzz oracle in
	 * test/test_fuzz/fuzz_validators.cpp exists to catch, and did (2026-09-18,
	 * when parseFloatStrict stopped calling atof).
	 *
	 * "While the value fits 2^53" is a limit, not a figure of speech: past
	 * PARSE_FLOAT_EXACT_DIGITS digits the steps round too, and the same oracle
	 * caught "33333333.0000000000030033000" (27 digits) one ulp off, later the
	 * same day. Here the answer stays best-effort — every caller of this raw
	 * parser reads a number the firmware's own UI wrote, never sixteen digits
	 * of it — and parseFloatStrict( ), the one with a contract, refuses instead.
	 *
	 * The doubles cost nothing new in flash: printing a %f already links
	 * newlib's double paths, and this parser reuses them. */
	double v = 0.0;
	while (*s >= '0' && *s <= '9') {
		v = v * 10.0 + (double)(*s - '0');
		s++;
	}
	if (*s != '.') return (float)(neg ? -v : v);
	s++;
	double scale = 1.0;
	while (*s >= '0' && *s <= '9') {
		v = v * 10.0 + (double)(*s - '0');
		scale *= 10.0;
		s++;
	}
	v /= scale;
	return (float)(neg ? -v : v);
}
