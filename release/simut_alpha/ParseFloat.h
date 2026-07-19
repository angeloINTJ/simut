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

/** Parses a decimal float string (e.g. "-12.5", "0.05", "100").
 *  Returns NAN on empty input. Handles leading minus sign.
 *  Does NOT handle scientific notation (1.5e3) — not used in SIMUT. */
inline float parseFloat(const char* s) {
	if (!s || !*s) return NAN;
	bool neg = (*s == '-');
	if (neg) s++;
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
