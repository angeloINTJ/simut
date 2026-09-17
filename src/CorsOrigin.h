/**
 * @file CorsOrigin.h
 * @brief The one rule for what may go into Access-Control-Allow-Origin.
 *
 * @details The web fleet manager is a single HTML page that runs in the
 * operator's browser, on a PC where nothing is installed, and talks to every
 * device on the network by fetch( ). A browser blocks all of that unless the
 * device answers with an Access-Control-Allow-Origin naming the page's exact
 * origin — so the origin is configuration, written by the operator into
 * /config/cors.txt (serial console: `system cors <origin>`).
 *
 * That value ends up **verbatim inside a response header**, which is why the
 * check below is a whitelist and not a hunt for bad characters: a CR or LF in
 * the middle of it would close the header and let whoever wrote the file append
 * headers of their own — a Set-Cookie, a second Allow-Origin — to every
 * response the device ever sends. "Written by the operator" is exactly what a
 * config file stops being the day an upload route has a bug, so the rule is
 * enforced twice, on the way in (the CLI, before writing) and on the way out
 * (WebManager, before the listener is built).
 *
 * Header-only and dependency-free, like FsSecretPath.h and ApPsk.h, so both
 * callers share one definition and the host test can reach it. Two copies of
 * this rule would be two rules, and the one that drifts is the one that runs.
 * See test/test_validators/test_main.cpp.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>

/** Longest origin accepted. An origin is a scheme, a host and maybe a port —
 *  "https://gerenciador.hospital.local:8443" is 39 — so 64 is roomy for the
 *  real thing and still a bound. */
#define CORS_ORIGIN_MAX_LEN 64

/**
 * Is this a well-formed "scheme://host[:port]" origin?
 *
 * Rejects, deliberately:
 *   - anything but http:// or https:// — no file://, no data:, no bare host;
 *   - a trailing slash or any path. An Origin never carries one (RFC 6454
 *     §6.1), and a browser comparing its own "http://x" against a sent
 *     "http://x/" finds them different and blocks — which would read as "CORS
 *     is broken" rather than as "there is one slash too many in the file";
 *   - "*". The wildcard is what this whole mechanism exists to avoid: it would
 *     let any page on the internet drive the device through the browser of
 *     whoever opens it. It is also rejected on its own merits here, since '*'
 *     is not in the allowed character set.
 *
 * @param o  The candidate origin.
 * @return   true when it is safe to write into a response header.
 */
inline bool isValidCorsOrigin(const String& o) {
	/* 8 = the shortest thing that could possibly be one: "http://x". */
	if (o.length( ) < 8 || o.length( ) > CORS_ORIGIN_MAX_LEN) return false;

	size_t hostStart;
	if      (o.startsWith("http://"))  hostStart = 7;
	else if (o.startsWith("https://")) hostStart = 8;
	else return false;
	if (o.length( ) == hostStart) return false;   /* scheme and nothing else */

	for (size_t i = hostStart; i < o.length( ); i++) {
		const char c = o[i];
		const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
		                  || (c >= '0' && c <= '9')
		                  || c == '.' || c == '-' || c == ':'
		                  || c == '[' || c == ']';   /* IPv6 literal */
		if (!allowed) return false;
	}
	return true;
}
