/**
 * @file WebJsonSlice.h
 * @brief Brace-matching and scalar field reads for the hand-rolled JSON
 *        walkers in the web handlers.
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
#include <stdio.h>              /* snprintf, for the key needle */
#include "SystemDefs_Validate.h" /* parseBoolStrict */

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


/* ── Scalar field readers ──────────────────────────────────────────────────
 *
 * The commit parser grew one key-finder per section — getNum over `sys`, getN
 * over `net`, valuePos over a slot object — and they drifted, because nothing
 * forced them to agree. `sys` learned to skip the whitespace a pretty-printed
 * payload puts after the colon; `net` never did, so `{"net":{"use_dhcp": 0}}`
 * read the token as " 0" and turned DHCP on. Same predicate, three spellings,
 * three different bugs. These are the one copy all of them now call.
 *
 * The needle stays the flat `"key":` that WebCommitSections.h scans for, and
 * deliberately so: the authorization gate matches the parser's predicate, and
 * a reader here that understood nesting while the gate did not is exactly the
 * disagreement that header exists to forbid.
 */

/** Tri-state returns of jsonFlag. Both are negative so the caller's `>= 0`
 *  test means "a value was read" and nothing else. */
enum { JSON_FLAG_ABSENT = -1, /**< key not in this object */
       JSON_FLAG_BAD    = -2  /**< key present, value not a boolean */ };

/**
 * @brief First character of the value for `key`, or -1 when absent.
 *
 * Skips the space/tab/CR/LF that JSON allows after the colon. JSON.stringify
 * never emits it and json.dumps always does, which is why every handler that
 * forgot this step worked from the browser and failed from a script.
 */
inline int jsonValuePos(const String& s, const char* key) {
	char needle[48];
	const int n = snprintf(needle, sizeof(needle), "\"%s\":", key ? key : "");
	/* A key too long to build a needle for would match a TRUNCATED prefix —
	 * silently reading some other field. Absent is the only safe answer. */
	if (n <= 0 || n >= (int)sizeof(needle)) return -1;
	const int p = s.indexOf(needle);
	if (p < 0) return -1;
	int v = p + n;
	const int len = (int)s.length( );
	while (v < len && (s[v] == ' ' || s[v] == '\t' || s[v] == '\r' || s[v] == '\n')) v++;
	return (v < len) ? v : -1;
}

/**
 * @brief Raw scalar token for `key`: unquoted if quoted, else read to the
 *        first ',' '}' or ']' and trimmed. Empty when absent or malformed.
 *
 * Quoted and bare both come back as the same string on purpose — the page
 * sends `"tz":-3` and the CLI-shaped payloads send `"tz":"-3"`, and the field
 * validators (parseIntStrict, parseBoolStrict) are what decide whether the
 * contents are acceptable. Reading is not validating.
 */
inline String jsonRawToken(const String& s, const char* key) {
	const int v = jsonValuePos(s, key);
	if (v < 0) return String( );
	if (s[v] == '"') {
		const int e = s.indexOf('"', v + 1);
		return (e < 0) ? String( ) : s.substring(v + 1, e);
	}
	const int len = (int)s.length( );
	int e = v;
	while (e < len && s[e] != ',' && s[e] != '}' && s[e] != ']') e++;
	String out = s.substring(v, e);
	out.trim( );   /* a space before the comma is legal too */
	return out;
}

/**
 * @brief Boolean field read: 1, 0, or one of the negative codes above.
 *
 * The three readers this replaces each answered a different wrong thing for
 * a value they did not understand — `!= "0"` said true, `startsWith("true")`
 * said false, and jsonBoolValue kept the stored value without a word. Only
 * one of those is defensible, and only when the caller is told: absent means
 * keep, unreadable means keep AND report, so the commit response can name the
 * field it refused instead of quietly storing the opposite of the request.
 */
inline int jsonFlag(const String& s, const char* key) {
	if (jsonValuePos(s, key) < 0) return JSON_FLAG_ABSENT;
	bool b = false;
	if (!parseBoolStrict(jsonRawToken(s, key), b)) return JSON_FLAG_BAD;
	return b ? 1 : 0;
}
