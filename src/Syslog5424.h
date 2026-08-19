/**
 * @file Syslog5424.h
 * @brief Pure RFC 5424 line formatter for SIMUT log records.
 * @details snprintf into a caller buffer with zero Arduino dependencies, so
 * test/test_validators compiles this header natively and pins the exact bytes
 * that leave the box. SyslogManager wraps it with the WiFiUDP transport; this
 * header knows nothing about sockets, which is why the wire format is testable
 * without a network.
 *
 * The mapping from a SIMUT log record to RFC 5424 is mechanical and was
 * decided before any code (see the interop note): the binary log already
 * carries everything syslog asks for.
 *
 *   SIMUT                         RFC 5424
 *   LogLevel 0..4 (DEBUG..FATAL)  severity 7/6/4/3/2
 *   tagId → "NET","CLI",…         APP-NAME
 *   code (logcodes.tsv)           MSGID (numeric — stable, language-independent)
 *   context (int16)               STRUCTURED-DATA ctx=
 *   the code's description        MSG (already translated by the caller)
 *
 * Two traps this format was built around:
 *  - The clock. getEpoch() never returns 0 — it falls back to the build epoch
 *    and time-travels. A line stamped 2025 arriving at a SIEM in 2026 sorts
 *    wrong or is dropped by retention. RFC 5424 has NILVALUE '-' for TIMESTAMP:
 *    below CLOCK_SYNCED_EPOCH we emit '-' rather than a lie.
 *  - Field separators. HOSTNAME and APP-NAME are space-delimited PRINTUSASCII;
 *    a device name with a space (isValidName allows it) would shear the line.
 *    Every structured field is sanitized to 33..126, empty → NILVALUE.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

namespace Syslog5424 {

/* Facility 16 = local0, the conventional slot for a local appliance. */
constexpr int FACILITY_LOCAL0 = 16;

/* Below this the clock is provisional (build-epoch fallback), so the timestamp
 * is a lie — emit NILVALUE instead. Same threshold LogManager uses to decide
 * "[HH:MM:SS]" vs "[BOOT+Ns]" on the serial line. */
constexpr time_t CLOCK_SYNCED_EPOCH = 1600000000; /* 2020-09-13 */

/* No IANA Private Enterprise Number is registered for SIMUT. 32473 is the PEN
 * IANA reserves for examples and documentation (RFC 5612 §3) — the correct
 * placeholder for structured data until a real PEN exists. A SIEM parses
 * `simut@32473` as valid SD-ID syntax; a deployment that registers a PEN swaps
 * this one constant. */
constexpr const char* SD_ID = "simut@32473";

/** SIMUT LogLevel (DEBUG=0..FATAL=4) → RFC 5424 severity. */
inline int severityForLevel(uint8_t level) {
	switch (level) {
		case 0: return 7; /* DEBUG  → Debug */
		case 1: return 6; /* INFO   → Informational */
		case 2: return 4; /* WARN   → Warning */
		case 3: return 3; /* ERROR  → Error */
		case 4: return 2; /* FATAL  → Critical */
		default: return 5; /* Notice — unknown level, still valid */
	}
}

/** PRI = facility*8 + severity. */
inline int priority(uint8_t level) {
	return FACILITY_LOCAL0 * 8 + severityForLevel(level);
}

/**
 * @brief Copy `src` into `dst` keeping only PRINTUSASCII (33..126); any other
 * byte (space, control, high-bit) becomes '-'. Result is at most cap-1 chars.
 * Returns the number of chars written (0 → caller should emit NILVALUE).
 *
 * This is what keeps a space in the device name from shearing the header:
 * HOSTNAME and APP-NAME are single space-delimited tokens.
 */
inline size_t sanitizeToken(const char* src, char* dst, size_t cap) {
	if (cap == 0) return 0;
	size_t o = 0;
	for (size_t i = 0; src && src[i] && o < cap - 1; i++) {
		const unsigned char c = (unsigned char)src[i];
		dst[o++] = (c >= 33 && c <= 126) ? (char)c : '-';
	}
	dst[o] = '\0';
	return o;
}

/** Append a sanitized token or NILVALUE '-' to out at position p (bounded). */
inline int appendToken(char* out, size_t cap, int p, const char* src) {
	char tok[64];
	const size_t n = sanitizeToken(src, tok, sizeof(tok));
	return p + snprintf(out + p, (p < (int)cap) ? cap - p : 0, "%s", n ? tok : "-");
}

/**
 * @brief Format one RFC 5424 line into `out`. Returns strlen written
 *        (may be >= cap if truncated, snprintf semantics).
 *
 * Shape:  <PRI>1 TIMESTAMP HOST APP - MSGID [simut@32473 ctx="…" core="…" up="…"] MSG
 *
 * TIMESTAMP is UTC ('Z'), whole seconds (RFC 5424 permits omitting the
 * fraction); below CLOCK_SYNCED_EPOCH it is NILVALUE '-'. PROCID is NILVALUE.
 * MSGID is the numeric log code. MSG is the caller's already-translated
 * description plus optional extra text, with control bytes replaced by spaces
 * (UTF-8 payload bytes are passed through — RFC 5424 MSG is UTF-8).
 */
inline size_t format(char* out, size_t cap,
                     uint8_t level, const char* hostname, const char* appName,
                     uint16_t msgid, int16_t context, uint8_t core,
                     uint32_t uptimeSec, time_t epoch,
                     const char* desc, const char* extra) {
	if (cap == 0) return 0;
	int p = 0;

	/* <PRI>VERSION */
	p += snprintf(out + p, cap - p, "<%d>1 ", priority(level));

	/* TIMESTAMP or NILVALUE */
	if (epoch >= CLOCK_SYNCED_EPOCH) {
		struct tm ti;
		time_t e = epoch;
		gmtime_r(&e, &ti);
		p += snprintf(out + p, (p < (int)cap) ? cap - p : 0,
		              "%04d-%02d-%02dT%02d:%02d:%02dZ ",
		              ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
		              ti.tm_hour, ti.tm_min, ti.tm_sec);
	} else {
		p += snprintf(out + p, (p < (int)cap) ? cap - p : 0, "- ");
	}

	/* HOSTNAME APP-NAME PROCID(-) MSGID */
	p = appendToken(out, cap, p, hostname);
	p += snprintf(out + p, (p < (int)cap) ? cap - p : 0, " ");
	p = appendToken(out, cap, p, appName);
	p += snprintf(out + p, (p < (int)cap) ? cap - p : 0, " - %u ", (unsigned)msgid);

	/* STRUCTURED-DATA — forensic fields as an SD element. */
	p += snprintf(out + p, (p < (int)cap) ? cap - p : 0,
	              "[%s ctx=\"%d\" core=\"%u\" up=\"%lu\"] ",
	              SD_ID, (int)context, (unsigned)core, (unsigned long)uptimeSec);

	/* MSG: description [+ ": " extra], control bytes → space. */
	char msg[192];
	int m = 0;
	m += snprintf(msg + m, sizeof(msg) - m, "%s", desc ? desc : "");
	if (extra && extra[0] && m < (int)sizeof(msg) - 2) {
		m += snprintf(msg + m, sizeof(msg) - m, ": %s", extra);
	}
	for (int i = 0; i < m; i++) {
		if ((unsigned char)msg[i] < 32) msg[i] = ' ';
	}
	p += snprintf(out + p, (p < (int)cap) ? cap - p : 0, "%s", msg);

	return (size_t)p;
}

} // namespace Syslog5424
