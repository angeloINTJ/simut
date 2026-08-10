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
#include <string.h>
#include <math.h>
#include <stdio.h>

#define CALIB_MAX_POINTS 5

/* Worst honest pts field: 5 pairs of "%.2f:%.2f" at the widest value the
 * channel table allows (lux, 167772.15) is 5 * 39 + 4 + NUL. Callers that
 * hold an encoded field should size with this, not with a guess. */
#define CALIB_PTS_BUF 224

/* Interpolation between anchors. LINEAR is the piecewise straight line;
 * SMOOTH is a monotone cubic Hermite (Fritsch-Carlson / PCHIP) on the
 * offsets. Monotone cubic and not Catmull-Rom or a natural spline on
 * purpose: those overshoot between anchors, and an overshoot here is a
 * correction larger than anything the reference instrument ever showed.
 * PCHIP stays inside the anchor values in every interval. SMOOTH with
 * fewer than 3 points is evaluated as LINEAR — with two anchors the
 * monotone cubic IS the straight line. */
#define CALIB_MODE_LINEAR 0
#define CALIB_MODE_SMOOTH 1

struct CalibCurve {
	uint8_t n;
	uint8_t mode;                /* CALIB_MODE_LINEAR / CALIB_MODE_SMOOTH */
	float raw[CALIB_MAX_POINTS]; /* strictly increasing; NAN only when n==1 (anchor-free) */
	float off[CALIB_MAX_POINTS]; /* off[i] = reference_i - raw_i */
	float m[CALIB_MAX_POINTS];   /* Hermite slopes dΔ/dx at each anchor (SMOOTH only);
	                              * derived at build time, never persisted. Both end
	                              * slopes are 0 so the curve meets the held zones
	                              * beyond the anchors without a kink. */

	CalibCurve( ) : n(0), mode(CALIB_MODE_LINEAR) {
		for (uint8_t i = 0; i < CALIB_MAX_POINTS; i++) { raw[i] = NAN; off[i] = 0.0f; m[i] = 0.0f; }
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
 * SMOOTH mode precomputes the Fritsch-Carlson slopes here, once, on the
 * (raw, offset) knots: interior slopes are the weighted harmonic mean of the
 * neighboring secants (zeroed across a sign change — that is what kills the
 * overshoot), end slopes are 0 so the curve flattens into the held zones.
 *
 * @return false on count > CALIB_MAX_POINTS, non-finite input or duplicate
 *         raws; the curve is left as identity. count == 0 is a valid "no
 *         correction" and returns true.
 */
inline bool calibCurveBuild(CalibCurve& c, const float* raws, const float* refs, uint8_t count,
                            uint8_t mode = CALIB_MODE_LINEAR) {
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
	c.mode = (mode == CALIB_MODE_SMOOTH) ? CALIB_MODE_SMOOTH : CALIB_MODE_LINEAR;

	if (c.mode == CALIB_MODE_SMOOTH && count >= 3) {
		float h[CALIB_MAX_POINTS - 1], d[CALIB_MAX_POINTS - 1];
		for (uint8_t i = 0; i + 1 < count; i++) {
			h[i] = c.raw[i + 1] - c.raw[i];
			d[i] = (c.off[i + 1] - c.off[i]) / h[i];
		}
		c.m[0] = 0.0f;
		c.m[count - 1] = 0.0f;
		for (uint8_t i = 1; i + 1 < count; i++) {
			if (d[i - 1] == 0.0f || d[i] == 0.0f || ((d[i - 1] > 0.0f) != (d[i] > 0.0f))) {
				c.m[i] = 0.0f;
			} else {
				c.m[i] = 3.0f * (h[i - 1] + h[i]) /
				         ((2.0f * h[i] + h[i - 1]) / d[i - 1] + (h[i] + 2.0f * h[i - 1]) / d[i]);
			}
		}
	}
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
			const float h = c.raw[i] - c.raw[i - 1];
			const float t = (x - c.raw[i - 1]) / h;
			if (c.mode == CALIB_MODE_SMOOTH && c.n >= 3) {
				/* Cubic Hermite on the offset knots with the precomputed
				 * monotone slopes. Basis expanded once; t powers by
				 * multiplication — no libm on the hot path. */
				const float t2 = t * t, t3 = t2 * t;
				const float dOff = c.off[i - 1] * (2.0f * t3 - 3.0f * t2 + 1.0f)
				                 + h * c.m[i - 1] * (t3 - 2.0f * t2 + t)
				                 + c.off[i] * (-2.0f * t3 + 3.0f * t2)
				                 + h * c.m[i] * (t3 - t2);
				return x + dOff;
			}
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
inline bool calibCurveDecodePts(const char* pts, CalibCurve& c, uint8_t mode = CALIB_MODE_LINEAR) {
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
	return calibCurveBuild(c, r, v, count, mode);
}

/* The interpolation-mode cell: sits right after the name, only on rows whose
 * curve is SMOOTH ("cub" — cubic). Exact-match on a tiny vocabulary, which is
 * what disambiguates a mode row (even count, non-numeric second field) from
 * the transitional offset,name,pairs shape (even count, numeric first). */
inline int calibModeToken(const char* s, const char* e) {
	const size_t len = (size_t)(e - s);
	if (len == 3 && strncmp(s, "cub", 3) == 0) return CALIB_MODE_SMOOTH;
	if (len == 3 && strncmp(s, "lin", 3) == 0) return CALIB_MODE_LINEAR;
	return -1;
}

/**
 * @brief Parse everything after the id column of a calib.csv row.
 *
 * The row shape is identified by FIELD COUNT, which is why this lives here
 * as a pure function instead of inside StorageManager — the dispatch is the
 * part worth pinning with host tests:
 *
 *   1 field       name                        identity (DS18B20 ROM->id/name DB row)
 *   2 fields      offset,name                 legacy constant offset (anchor-free —
 *                                             the one shape that has no point cells
 *                                             to be written as)
 *   odd  >= 3     name,raw,ref[,...]          canonical points row (linear)
 *   even >= 4, f1 is a mode token
 *                 name,cub,raw,ref[,...]      canonical points row, smooth (monotone
 *                                             cubic); "lin" also accepted from hand
 *                                             edits though the writer never emits it
 *   even >= 4     offset,name,raw,ref[,...]   transitional shape; offset doubles as
 *                                             the fallback if the cells fail to parse
 *
 * Trailing empty fields (a row ending in commas) are ignored before counting,
 * but never below 2 fields — `0.50,` is a legacy row with an empty name, not
 * a name-only row called "0.50".
 *
 * @return false when a fallback was taken (malformed point cells) — the
 *         caller decides whether that earns a log line. The curve is always
 *         left in a valid state.
 */
inline bool calibRowParseTail(const char* tail, CalibCurve& c, char* nameOut, size_t nameCap) {
	c = CalibCurve( );
	if (nameOut && nameCap) nameOut[0] = '\0';
	if (!tail) return true;

	/* Field boundaries on raw commas — names are comma-stripped at write
	 * time, so a comma is always a separator here. */
	const int MAXF = 2 + CALIB_MAX_POINTS * 2 + 2;
	const char* fs[MAXF];
	const char* fe[MAXF];
	int nf = 0;
	const char* p = tail;
	bool overflow = false;
	while (true) {
		const char* comma = strchr(p, ',');
		if (nf >= MAXF) { overflow = true; break; }
		fs[nf] = p;
		fe[nf] = comma ? comma : p + strlen(p);
		nf++;
		if (!comma) break;
		p = comma + 1;
	}

	/* Trim each field view; drop trailing empties (min 2, see doc). */
	auto trimField = [](const char*& s, const char*& e) {
		while (s < e && (*s == ' ' || *s == '\t')) s++;
		while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	};
	for (int i = 0; i < nf; i++) trimField(fs[i], fe[i]);
	while (nf > 2 && fs[nf - 1] == fe[nf - 1]) nf--;
	if (nf == 2 && fs[1] == fe[1] && fs[0] == fe[0]) nf = 0;

	auto copyName = [&](int idx) {
		if (!nameOut || nameCap == 0 || idx >= nf) return;
		size_t len = (size_t)(fe[idx] - fs[idx]);
		if (len >= nameCap) len = nameCap - 1;
		memcpy(nameOut, fs[idx], len);
		nameOut[len] = '\0';
	};
	auto fieldNumber = [&](int idx, float& out) -> bool {
		const char* q = fs[idx];
		if (!calibParseNumber(&q, out)) return false;
		while (q < fe[idx] && (*q == ' ' || *q == '\t')) q++;
		return q == fe[idx]; /* the whole field, not a numeric prefix */
	};

	if (nf == 0) return true;
	if (nf == 1) { copyName(0); return true; }
	if (nf == 2) {
		float off = 0.0f;
		fieldNumber(0, off); /* garbage keeps 0.0 — same tolerance as ever */
		calibCurveFromOffset(c, off);
		copyName(1);
		return true;
	}

	const bool even = (nf % 2) == 0;
	int mode = even ? calibModeToken(fs[1], fe[1]) : -1;
	const bool modeRow = (mode >= 0);
	const int nameIdx = (even && !modeRow) ? 1 : 0;
	const int pairIdx = modeRow ? 2 : nameIdx + 1;
	float legacyOff = 0.0f;
	if (even && !modeRow) fieldNumber(0, legacyOff);
	copyName(nameIdx);

	bool clean = !overflow;
	char pbuf[CALIB_PTS_BUF];
	const char* span0 = fs[pairIdx];
	const char* span1 = fe[nf - 1];
	const size_t spanLen = (size_t)(span1 - span0);
	if (!overflow && spanLen < sizeof(pbuf)) {
		memcpy(pbuf, span0, spanLen);
		pbuf[spanLen] = '\0';
		if (calibCurveDecodePts(pbuf, c, modeRow ? (uint8_t)mode : CALIB_MODE_LINEAR)) return clean;
	}

	/* Point cells did not parse. An even row still has its offset column; an
	 * odd row whose first field is numeric is a transitional packed row
	 * ("0.50,NAME,r:v;...") that field-counting reads one off — rescue the
	 * offset in both cases rather than zeroing a correction. */
	float off = 0.0f;
	if (even) {
		off = legacyOff;
	} else if (fieldNumber(0, off)) {
		copyName(1);
	} else {
		off = 0.0f;
	}
	calibCurveFromOffset(c, off);
	return false;
}
