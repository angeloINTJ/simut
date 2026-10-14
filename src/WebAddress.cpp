/**
 * @file WebAddress.cpp
 * @brief The forms of the web server's address (see WebAddress.h).
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebAddress.h"
#include <stdio.h>

const char* webAddress(char* out, size_t n, const char* ip, uint16_t port, bool tls, WebAddrForm form) {
	if (!out || n == 0) return out;
	const bool suffix = form == WEB_ADDR_SUFFIX;
	const char* pre = suffix ? "" : tls ? "https://" : (form == WEB_ADDR_URL ? "http://" : "");
	char p[8] = "";
	if (port != 0 && port != (tls ? 443 : 80)) snprintf(p, sizeof(p), ":%u", (unsigned)port);
	snprintf(out, n, "%s%s%s%s", pre, suffix ? "" : ip, p, (suffix && tls) ? " (https)" : "");
	return out;
}
