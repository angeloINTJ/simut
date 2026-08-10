/**
 * @file WebJsonSlice.h
 * @brief Brace-matching for the hand-rolled JSON walkers in the web handlers.
 *
 * @details Every array walker in WebManager_Commit.cpp and WebManager_Calib.cpp
 * used to find the end of an element with indexOf('}') — the FIRST closing
 * brace, not the matching one. That works only while no element contains a
 * nested object or array, which is why the slot field "al" (staged after the
 * nested "lim":{...}) was silently truncated off every commit, and why the
 * calibration payload could never carry a points array. This helper is the
 * depth-aware replacement: give it the position of an opening '{' or '[' and
 * it returns the position of the bracket that closes it.
 *
 * Quote-aware on purpose: friendlyName may legally contain braces (only ','
 * and '"' are sanitized out), so brackets inside string literals must not
 * count, and \" escapes must not end the string early.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Index of the bracket closing the one at openPos, or -1.
 *
 * s[openPos] must be '{' or '['. Returns -1 when it is not, when the input
 * ends before the bracket closes, or when the closer that brought the depth
 * back to zero is of the wrong kind (a "{...]" is malformed, and slicing it
 * as if it matched would hand the caller garbage).
 */
inline int jsonMatchEnd(const String& s, int openPos) {
	const int len = (int)s.length( );
	if (openPos < 0 || openPos >= len) return -1;

	const char open = s[openPos];
	char close;
	if (open == '{') close = '}';
	else if (open == '[') close = ']';
	else return -1;

	int depth = 0;
	bool inString = false;
	for (int i = openPos; i < len; i++) {
		const char ch = s[i];
		if (inString) {
			if (ch == '\\') { i++; continue; }
			if (ch == '"') inString = false;
			continue;
		}
		if (ch == '"') { inString = true; continue; }
		if (ch == '{' || ch == '[') {
			depth++;
		} else if (ch == '}' || ch == ']') {
			depth--;
			if (depth == 0) return (ch == close) ? i : -1;
			if (depth < 0) return -1;
		}
	}
	return -1;
}
