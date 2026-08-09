/**
 * @file CalibCurve.h
 * @brief Per-channel calibration curve: up to 5 (raw -> reference) points.
 *
 * @details One curve corrects one measurement axis of one sensor. The stored
 * form is the OFFSET at each anchor raw value, because interpolating the
 * offset linearly over raw is algebraically the same as drawing the piecewise
 * line through the (raw, reference) points — and it makes n=1 degenerate into
 * today's constant offset, which is how a legacy 4-column calib.csv row keeps
 * meaning exactly what it always meant.
 *
 *   n == 0   no correction — the sensor's own output stands
 *   n == 1   constant offset; raw[0] may be NAN ("anchor-free", the legacy
 *            offset-column form that never knew where it was measured)
 *   n >= 2   piecewise-linear offset over raw; beyond the first and last
 *            anchors the end offset is HELD (a parallel shift), never
 *            extrapolated — a slope projected outside the measured span
 *            manufactures corrections nobody calibrated
 *
 * Dependency-free on purpose (<stdint.h>/<math.h>/<stdio.h> only), same
 * discipline as SensorChannels.h: the firmware includes it from SensorManager
 * and StorageManager, and `pio test -e native` compiles it on the host.
 * Parsing avoids strtof/atof for the same reason ParseFloat.h exists — they
 * drag ~8 KB of newlib into a flash budget measured in hundreds of bytes.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>

#define CALIB_MAX_POINTS 5

/* Worst honest pts field: 5 pairs of "%.2f:%.2f" at the widest value the
 * channel table allows (lux, 167772.15) is 5 * 39 + 4 + NUL. Callers that
 * hold an encoded field should size with this, not with a guess. */
#define CALIB_PTS_BUF 224

struct CalibCurve {
	uint8_t n;
	float raw[CALIB_MAX_POINTS]; /* strictly increasing; NAN only when n==1 (anchor-free) */
	float off[CALIB_MAX_POINTS]; /* off[i] = reference_i - raw_i */

	CalibCurve( ) : n(0) {
		for (uint8_t i = 0; i < CALIB_MAX_POINTS; i++) { raw[i] = NAN; off[i] = 0.0f; }
	}
};

/** No correction defined — apply() is the identity. */
inline bool calibCurveIsIdentity(const CalibCurve& c) { return c.n == 0; }

/**
 * @brief Build a curve from parallel (raw, reference) arrays.
 *
 * Sorts by raw (the UI and the CSV are both allowed to be out of order) and
 * REJECTS duplicate raws instead of merging them: two references for one raw
 * is a contradiction the user has to resolve, not something to average away.
 * Duplicates are judged at 2 decimals — the file stores %.2f, so anything
 * closer than that would collide on the next round-trip anyway.
 *
 * @return false on count > CALIB_MAX_POINTS, non-finite input or duplicate
 *         raws; the curve is left as identity. count == 0 is a valid "no
 *         correction" and returns true.
 */
inline bool calibCurveBuild(CalibCurve& c, const float* raws, const float* refs, uint8_t count) {
	c = CalibCurve( );
	if (count == 0) return true;
	if (count > CALIB_MAX_POINTS || !raws || !refs) return false;

	for (uint8_t i = 0; i < count; i++) {
		if (!isfinite(raws[i]) || !isfinite(refs[i])) return false;
	}

	float r[CALIB_MAX_POINTS], v[CALIB_MAX_POINTS];
	for (uint8_t i = 0; i < count; i++) {
		/* Insertion sort by raw — count is at most 5. */
		uint8_t j = i;
		while (j > 0 && r[j - 1] > raws[i]) { r[j] = r[j - 1]; v[j] = v[j - 1]; j--; }
		r[j] = raws[i];
		v[j] = refs[i];
	}
	for (uint8_t i = 1; i < count; i++) {
		if (lroundf(r[i - 1] * 100.0f) == lroundf(r[i] * 100.0f)) return false;
	}

	for (uint8_t i = 0; i < count; i++) { c.raw[i] = r[i]; c.off[i] = v[i] - r[i]; }
	c.n = count;
	return true;
}

/** Corrected value for a raw reading. NAN passes through untouched. */
inline float calibCurveApply(const CalibCurve& c, float x) {
	if (c.n == 0 || !isfinite(x)) return x;
	if (c.n == 1) return x + c.off[0];
	if (x <= c.raw[0]) return x + c.off[0];
	if (x >= c.raw[c.n - 1]) return x + c.off[c.n - 1];
	for (uint8_t i = 1; i < c.n; i++) {
		if (x <= c.raw[i]) {
			/* raw[] is strictly increasing, so the span cannot be zero. */
			const float t = (x - c.raw[i - 1]) / (c.raw[i] - c.raw[i - 1]);
			return x + c.off[i - 1] + t * (c.off[i] - c.off[i - 1]);
		}
	}
	return x + c.off[c.n - 1]; /* unreachable — the >= guard above returns first */
}

/**
 * @brief The offset the curve contributes at raw value x.
 *
 * n==1 answers even for a NAN x — a constant offset does not depend on where
 * it is evaluated, and the legacy "offset" API field is served from here while
 * a sensor is still warming up.
 */
inline float calibCurveOffsetAt(const CalibCurve& c, float x) {
	if (c.n == 0) return 0.0f;
	if (c.n == 1) return c.off[0];
	if (!isfinite(x)) return 0.0f;
	return calibCurveApply(c, x) - x;
}

/** A legacy offset-column value as a curve: n=1 anchor-free, or identity for 0. */
inline void calibCurveFromOffset(CalibCurve& c, float offset) {
	c = CalibCurve( );
	if (!isfinite(offset) || offset == 0.0f) return;
	c.n = 1;
	c.raw[0] = NAN;
	c.off[0] = offset;
}

/**
 * @brief Encode the curve as the calib.csv trailing point columns:
 *        "raw,ref,raw,ref" — every number its own CSV cell, so a spreadsheet
 *        opens the file with one value per column instead of one packed blob.
 *
 * Identity and anchor-free n==1 both encode as "" — the first has nothing to
 * say, the second is exactly the legacy row shape (the offset column already
 * carries it), and writing nothing keeps that row 4 columns and readable by
 * older firmware.
 *
 * @return chars written (0 for ""); on a cap too small, writes "" and returns
 *         0 rather than shipping a truncated curve into the file.
 */
inline size_t calibCurveEncodePts(const CalibCurve& c, char* out, size_t cap) {
	if (!out || cap == 0) return 0;
	out[0] = '\0';
	if (c.n == 0) return 0;
	if (c.n == 1 && !isfinite(c.raw[0])) return 0;

	size_t used = 0;
	for (uint8_t i = 0; i < c.n; i++) {
		const int w = snprintf(out + used, cap - used, "%s%.2f,%.2f",
		                       (i > 0) ? "," : "", (double)c.raw[i], (double)(c.raw[i] + c.off[i]));
		if (w < 0 || (size_t)w >= cap - used) { out[0] = '\0'; return 0; }
		used += (size_t)w;
	}
	return used;
}

/* Local decimal parser (see header note on strtof). Advances *p past the
 * number; requires at least one digit. Scientific notation is not a format
 * this file ever wrote, so it is not a format this parser reads. */
inline bool calibParseNumber(const char** p, float& out) {
	const char* s = *p;
	while (*s == ' ' || *s == '\t') s++;
	bool neg = false;
	if (*s == '-' || *s == '+') { neg = (*s == '-'); s++; }
	bool any = false;
	float intPart = 0.0f;
	while (*s >= '0' && *s <= '9') { intPart = intPart * 10.0f + (float)(*s - '0'); s++; any = true; }
	float frac = 0.0f, div = 1.0f;
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') { frac = frac * 10.0f + (float)(*s - '0'); div *= 10.0f; s++; any = true; }
	}
	if (!any) return false;
	float v = intPart + frac / div;
	out = neg ? -v : v;
	*p = s;
	return true;
}

/**
 * @brief Decode the trailing point cells back into a curve.
 *
 * The canonical form is what the encoder writes — numbers separated by
 * commas, alternating raw,ref — but the parser treats ',', ';' and ':' as
 * the same separator: it reads a flat number list and pairs it up. That one
 * relaxation makes it accept the packed "raw:ref;raw:ref" shape this file
 * briefly used on the bench, plus any sane hand edit, at zero extra code.
 * The structure lives in the counting instead: an odd number of values, an
 * eleventh value, a non-number, duplicate raws — any of these fails the
 * whole field, and the caller falls back to the offset column, which is
 * always intact on a row this reader is given.
 *
 * NULL or empty decodes to identity and returns true: an absent tail IS the
 * legacy 4-column format, not an error.
 */
inline bool calibCurveDecodePts(const char* pts, CalibCurve& c) {
	c = CalibCurve( );
	if (!pts) return true;
	const char* p = pts;
	while (*p == ' ' || *p == '\t') p++;
	if (*p == '\0' || *p == '\r' || *p == '\n') return true;

	float vals[CALIB_MAX_POINTS * 2];
	uint8_t n = 0;
	while (true) {
		if (n >= CALIB_MAX_POINTS * 2) return false; /* an eleventh value is coming */
		if (!calibParseNumber(&p, vals[n])) return false;
		n++;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0' || *p == '\r' || *p == '\n') break;
		if (*p != ',' && *p != ';' && *p != ':') return false;
		p++;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0' || *p == '\r' || *p == '\n') break; /* tolerated trailing separator */
	}
	if (n < 2 || (n & 1u)) return false;

	float r[CALIB_MAX_POINTS], v[CALIB_MAX_POINTS];
	const uint8_t count = (uint8_t)(n / 2);
	for (uint8_t i = 0; i < count; i++) { r[i] = vals[2 * i]; v[i] = vals[2 * i + 1]; }
	return calibCurveBuild(c, r, v, count);
}
