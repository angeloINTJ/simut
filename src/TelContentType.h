/**
 * @file TelContentType.h
 * @brief The Content-Type header of a telemetry line, and what may go into it.
 *
 * @details JSON and CSV send a fixed header. The custom payload mode builds the
 * body from the operator's templates, so its format is whatever the server on
 * the other end expects — NDJSON, a vendor JSON, CSV under another name — and
 * only the operator can name it. Before v26 that mode fell into the `else` of
 * the header selection and went out as "text/plain", which a JSON endpoint
 * refuses: that is the report that started this.
 *
 * The stored value reaches an HTTP header verbatim, so it is checked twice: by
 * the commit handler, which refuses what is not a media type, and here, at the
 * sink, because a restored .bkp writes the config file raw and never meets
 * that handler. A value that fails the check is never sent — the line falls
 * back to "application/json", the header the standard mode already sends and
 * the one a migrated v25 config (field all zero) is meant to get.
 *
 * Pure and header-only, like TelemetryCursor.h: `pio test -e native` covers it.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The telemetry modes as this header sees them — TelMode, mirrored. It does
 * not include SystemDefs_Records.h because the native validator suite that
 * tests it cannot: that suite copies the few record helpers it needs instead
 * (see the note at the top of test/test_validators). The firmware pins the two
 * together with a static_assert in TelemetryManager.cpp, so a drift fails the
 * build rather than sending the wrong header. */
constexpr uint8_t TEL_CT_MODE_JSON   = 0;
constexpr uint8_t TEL_CT_MODE_CSV    = 1;
constexpr uint8_t TEL_CT_MODE_CUSTOM = 2;

/** The longest value a Content-Type field holds (TelemetryCustomConfig: 32 B). */
constexpr size_t TEL_CT_MAX = 31;

/** What an empty or unusable custom value is sent as. */
constexpr const char* TEL_CT_DEFAULT = "application/json";

/** RFC 9110 token character (tchar): what a type and a subtype are made of. */
inline bool telCtTchar(char c) {
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
	switch (c) {
		case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
		case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
			return true;
		default:
			return false;
	}
}

/** strnlen( ), spelled out: the bound is the point, and it must hold on the
 *  host and on newlib alike. */
inline size_t telCtLen(const char* s, size_t cap) {
	size_t n = 0;
	while (n < cap && s[n]) n++;
	return n;
}

/**
 * A media type as a header value: `type "/" subtype`, then optionally spaces and
 * `;` parameters. Every byte must be visible ASCII or a space (0x20..0x7E) — so
 * no CR/LF (a second header smuggled into the request), no DEL and no UTF-8,
 * which is not a header byte. Parameters are not parsed: once the whole value
 * is printable ASCII they cannot break the request, and "charset=utf-8" is the
 * only one anybody sends.
 */
inline bool isValidMediaType(const char* s, size_t maxLen = TEL_CT_MAX) {
	if (!s) return false;
	const size_t n = telCtLen(s, maxLen + 1);
	if (n == 0 || n > maxLen) return false;
	for (size_t i = 0; i < n; i++) {
		const unsigned char c = (unsigned char)s[i];
		if (c < 0x20 || c > 0x7E) return false;
	}
	size_t i = 0;
	while (i < n && telCtTchar(s[i])) i++;
	if (i == 0 || i == n || s[i] != '/') return false;
	const size_t sub = ++i;
	while (i < n && telCtTchar(s[i])) i++;
	if (i == sub) return false;
	while (i < n && s[i] == ' ') i++;
	return i == n || s[i] == ';';
}

/**
 * The Content-Type header for one telemetry line.
 *
 * @param mode      TelMode of the line (cfg.telMode or cfg.alarmTel.mode).
 * @param custom    The stored field. Read no further than @p customCap bytes: a
 *                  field without its terminator is exactly what a damaged or
 *                  hand-built blob looks like, and it is refused, not trusted.
 * @param buf       Scratch for the trimmed value, TEL_CT_MAX + 1 bytes.
 * @return          A string literal or @p buf — never NULL.
 */
inline const char* telContentTypeFor(uint8_t mode, const char* custom, size_t customCap,
                                     char* buf, size_t bufLen) {
	if (mode == TEL_CT_MODE_JSON) return "application/json";
	if (mode == TEL_CT_MODE_CSV) return "text/csv";
	if (mode != TEL_CT_MODE_CUSTOM) return "text/plain"; /* what every other mode always sent */
	if (!custom || !buf || bufLen == 0) return TEL_CT_DEFAULT;
	const size_t n = telCtLen(custom, customCap);
	if (n == customCap) return TEL_CT_DEFAULT; /* no terminator inside the field */
	size_t a = 0, b = n;
	while (a < b && custom[a] == ' ') a++;
	while (b > a && custom[b - 1] == ' ') b--;
	const size_t len = b - a;
	if (len == 0 || len >= bufLen) return TEL_CT_DEFAULT;
	memcpy(buf, custom + a, len);
	buf[len] = '\0';
	const size_t cap = (bufLen - 1 < TEL_CT_MAX) ? bufLen - 1 : TEL_CT_MAX;
	return isValidMediaType(buf, cap) ? buf : TEL_CT_DEFAULT;
}
